"""Declaration reorder pattern — permute variable declaration order.

Highest-value pattern for fixing register allocation mismatches.
PowerPC compiler assigns callee-saved registers (r19-r31) based on
variable declaration/first-use order, so reordering declarations can
fix register swap pairs.

Supports BSF-guided mode: when enabled, traces the compiler's BSF
(Bit Scan Forward) calls to capture the exact register allocation
sequence, then generates targeted reorderings instead of blind permutation.

Example:
    int a = 1;
    int b = 2;
    int c = 3;
    ->
    int b = 2;
    int a = 1;
    int c = 3;
"""

from __future__ import annotations

import itertools
import random
import sys
from pathlib import Path
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from .. import clang_types
from ..ast_queries import identifiers_in
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Maximum permutations to generate per group before switching to sampling
_MAX_PERMS = 20


def _resolve_decl_types(
    decls: list[Node], decl_names: list[str], ctx: FunctionContext
) -> dict[str, clang_types.TypeInfo]:
    """Resolve types for all declarations via libclang.

    Returns a dict mapping variable name to TypeInfo.
    Only populated if libclang is available.
    """
    if not clang_types.is_available():
        return {}
    result: dict[str, clang_types.TypeInfo] = {}
    for decl, name in zip(decls, decl_names):
        if name == "?":
            continue
        ti = clang_types.resolve_decl_type(
            ctx.file_path, decl.start_byte, ctx.file_source
        )
        if ti is not None:
            result[name] = ti
    return result


def _is_cross_regfile_swap(
    name_a: str, name_b: str,
    type_map: dict[str, clang_types.TypeInfo],
) -> bool:
    """Return True if swapping these two vars crosses GPR/FPR register files.

    Swapping a float-typed var with an int/pointer-typed var can never
    fix a GPR regswap because they live in different register files.
    """
    if not type_map:
        return False
    ti_a = type_map.get(name_a)
    ti_b = type_map.get(name_b)
    if ti_a is None or ti_b is None:
        return False
    a_is_fpr = ti_a.is_float
    b_is_fpr = ti_b.is_float
    return a_is_fpr != b_is_fpr


class DeclarationReorderPattern(Pattern):
    name = "declaration_reorder"
    safety_tier = "conservative"
    structural_domain = "data_flow"
    follow_ups = ("variable_extraction", "prologue_pressure", "declaration_movement")

    # BSF-guided mode: traces compiler's register allocator for targeted reorders.
    # Default True — disable with --no-bsf-guided.
    bsf_guided: bool = True
    # When True, fail instead of falling back to unguided generation
    bsf_required: bool = False
    # Cache BSF trace to avoid re-tracing on composition passes
    _bsf_cache: object = None  # BSFTrace or None
    _bsf_cache_path: object = None  # Path that was traced
    # Cache isolation result: (file_path, symbol) → function_calls or None
    _isolation_cache: dict | None = None
    # Cache assembly listing lines: (file_path,) → asm_lines or None
    _asm_lines_cache: dict | None = None
    # Whether BSF summary has already been printed this run
    _bsf_printed: bool = False

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Relevant when there are GPR or FPR callee-saved swap pairs
        for (r0, r1) in diagnosis.reg_swap_pairs:
            if r0.startswith("r") or r1.startswith("r"):
                return True
            if r0.startswith("f") or r1.startswith("f"):
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # More swap pairs = stronger signal for declaration reorder
        callee_pairs = sum(
            1 for (r0, r1) in diagnosis.reg_swap_pairs
            if r0.startswith("r") or r1.startswith("r")
            or r0.startswith("f") or r1.startswith("f")
        )
        if callee_pairs >= 3:
            base = 0.9
        elif callee_pairs >= 2:
            base = 0.7
        else:
            base = 0.5
        # Prologue mismatch boost — reordering can shift register allocation
        if diagnosis.has_prologue_mismatch:
            base = min(1.0, base + 0.2)
        return base

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0

        # Priority chain: Ghidra+ASM crossref -> Ghidra -> ASM-guided -> BSF -> blind
        # Try compound Ghidra+ASM crossref first (highest confidence)
        crossref_produced = False
        if (ctx.ghidra_ast is not None and ctx.target_var_order is not None
                and ctx.asm_listing_path is not None):
            for variant in self._try_ghidra_asm_crossref(ctx, counter):
                yield variant
                counter += 1
                crossref_produced = True

        # Try Ghidra-guided generation
        ghidra_produced = False
        if ctx.ghidra_ast is not None and ctx.target_var_order is not None and not crossref_produced:
            for variant in self._try_ghidra_guided(ctx, counter):
                yield variant
                counter += 1
                ghidra_produced = True

        # Try standalone ASM-guided generation (lightweight: /FAs compile + parse)
        asm_produced = False
        if not crossref_produced and not ghidra_produced:
            for variant in self._try_asm_guided(ctx, counter):
                yield variant
                counter += 1
                asm_produced = True

        # Try BSF-guided generation
        bsf_produced = False
        if self.bsf_guided and not ghidra_produced and not crossref_produced and not asm_produced:
            for variant in self._try_bsf_guided(ctx, counter):
                yield variant
                counter += 1
                bsf_produced = True

            # If --bsf-required, skip unguided fallback
            if self.bsf_required and not bsf_produced:
                print(
                    "  BSF mode: required but no guided candidates — "
                    "skipping unguided fallback",
                    file=sys.stderr,
                )
                return

        # Skip blind permutations if any guided method produced candidates
        if crossref_produced or ghidra_produced or asm_produced or bsf_produced:
            return

        # Then fill remaining budget with random permutations
        for group in _find_declaration_groups(ctx):
            if len(group) < 2:
                continue
            # Region filter: skip groups entirely outside mismatch regions
            if not any(ctx.node_in_mismatch_region(decl) for decl in group):
                continue

            # Build dependency graph to avoid use-before-declaration errors
            deps = _build_dependency_edges(group)

            # Generate dependency-safe permutations of this group
            for perm in _get_permutations(group, deps):
                new_source = _apply_reorder(ctx.file_source, group, perm)
                if new_source == ctx.file_source:
                    continue  # Skip identity permutation

                desc_parts = []
                for i, node in enumerate(perm):
                    orig_idx = group.index(node)
                    if orig_idx != i:
                        name = _get_decl_name(node, ctx)
                        desc_parts.append(name)

                desc = f"Reorder declarations: {', '.join(desc_parts)}"
                yield Variant(
                    name=f"declreorder_{counter}",
                    pattern_name=self.name,
                    description=desc,
                    source=new_source,
                    tags=frozenset({"reordered_declarations"}),
                )
                counter += 1

    def _try_ghidra_asm_crossref(self, ctx: FunctionContext, start_counter: int) -> Iterator[Variant]:
        """Generate high-confidence reorder variants using Ghidra + ASM crossref.

        Compound signal: Ghidra tells us the target's register order,
        ASM listing tells us our current order. The diff is the exact swap.
        """
        from ..ghidra_var_match import infer_target_register_order

        if not ctx.diagnosis or not ctx.asm_listing_path:
            return

        swap_pairs = [
            pair for pair in ctx.diagnosis.reg_swap_pairs
            if pair[0].startswith("r") or pair[1].startswith("r")
        ]
        if not swap_pairs:
            return

        all_decls = [s for s in ctx.statements if s.type == "declaration"]
        if len(all_decls) < 2:
            return

        decl_names = []
        for decl in all_decls:
            name = _get_declared_name(decl)
            decl_names.append(name or "?")

        # Resolve types for register-class filtering
        type_map = _resolve_decl_types(all_decls, decl_names, ctx)

        # Get target register allocation from Ghidra
        target_mappings = infer_target_register_order(
            ctx.target_var_order, ctx.target_gpr_saves
        )
        if not target_mappings:
            return

        # Get our current register allocation from ASM listing
        try:
            from tools.compiler_trace.asm_regmap import parse_asm_listing
            asm_lines = ctx.asm_listing_path.read_text().splitlines()
            asm_regmap = parse_asm_listing(asm_lines, ctx.symbol or "")
            if not asm_regmap or not asm_regmap.var_to_reg:
                return
        except Exception:
            return

        # Build crossref: for each swap pair, find which vars need to swap
        # target_reg_to_ghidra_var: which Ghidra var should be in which register
        target_reg_to_var: dict[str, str] = {}
        for m in target_mappings:
            target_reg_to_var[m.inferred_register] = m.ghidra_name

        # asm_var_to_reg: which source var is currently in which register
        our_reg_to_var: dict[str, str] = {}
        for var_name, reg in asm_regmap.var_to_reg.items():
            our_reg_to_var[reg] = var_name

        # Find exact swaps needed
        targeted_swaps: list[tuple[int, int]] = []
        n_vars = len(decl_names)
        n_filtered = 0

        for rA, rB in swap_pairs:
            if not (rA.startswith("r") and rB.startswith("r")):
                continue

            # Under callee-saved rule: r31 = index 0, r30 = index 1, etc.
            idxA = 31 - int(rA[1:])
            idxB = 31 - int(rB[1:])

            if 0 <= idxA < n_vars and 0 <= idxB < n_vars:
                # Filter: skip cross-register-file swaps (float vs int)
                if _is_cross_regfile_swap(
                    decl_names[idxA], decl_names[idxB], type_map
                ):
                    n_filtered += 1
                    continue
                pair = (min(idxA, idxB), max(idxA, idxB))
                if pair not in targeted_swaps:
                    targeted_swaps.append(pair)

        if not targeted_swaps:
            return

        filter_msg = f", filtered {n_filtered} cross-regfile" if n_filtered else ""
        print(
            f"  Ghidra+ASM crossref: {len(targeted_swaps)} swap(s) from "
            f"{len(target_mappings)} Ghidra vars + {len(asm_regmap.var_to_reg)} ASM mappings"
            f"{filter_msg}",
            file=sys.stderr,
        )

        # Build dependency edges
        deps = _build_dependency_edges(all_decls)
        base_order = list(range(n_vars))
        counter = start_counter
        seen: set[tuple[str, ...]] = set()

        # Generate single-swap variants
        for i, j in targeted_swaps:
            new_order = list(base_order)
            new_order[i], new_order[j] = new_order[j], new_order[i]
            candidate = [decl_names[k] for k in new_order]
            key = tuple(candidate)
            if key in seen or new_order == base_order:
                continue
            seen.add(key)

            if not _respects_deps(new_order, deps):
                continue

            reordered = [all_decls[k] for k in new_order]
            new_source = _apply_reorder(ctx.file_source, all_decls, reordered)
            if new_source == ctx.file_source:
                continue

            moved = [candidate[k] for k in range(n_vars) if candidate[k] != decl_names[k]]
            yield Variant(
                name=f"ghidra_asm_declreorder_{counter}",
                pattern_name=self.name,
                description=f"Ghidra+ASM crossref reorder: {', '.join(moved[:4])}",
                source=new_source,
                tags=frozenset({"reordered_declarations"}),
            )
            counter += 1

        # Multi-swap: all targeted swaps simultaneously
        if len(targeted_swaps) > 1:
            new_order = list(base_order)
            for i, j in targeted_swaps:
                new_order[i], new_order[j] = new_order[j], new_order[i]
            candidate = [decl_names[k] for k in new_order]
            key = tuple(candidate)
            if key not in seen and new_order != base_order and _respects_deps(new_order, deps):
                seen.add(key)
                reordered = [all_decls[k] for k in new_order]
                new_source = _apply_reorder(ctx.file_source, all_decls, reordered)
                if new_source != ctx.file_source:
                    moved = [candidate[k] for k in range(n_vars) if candidate[k] != decl_names[k]]
                    yield Variant(
                        name=f"ghidra_asm_declreorder_{counter}",
                        pattern_name=self.name,
                        description=f"Ghidra+ASM multi-swap: {', '.join(moved[:4])}",
                        source=new_source,
                        tags=frozenset({"reordered_declarations"}),
                    )
                    counter += 1

    def _try_ghidra_guided(self, ctx: FunctionContext, start_counter: int) -> Iterator[Variant]:
        """Generate Ghidra-guided reorder variants.

        Uses variable first-use order from Ghidra decompilation to infer
        the target's register allocation, then generates targeted swaps.
        """
        from ..ghidra_var_match import ghidra_guided_reorder

        all_decls = [s for s in ctx.statements if s.type == "declaration"]
        if len(all_decls) < 2:
            return

        decl_names = []
        for decl in all_decls:
            name = _get_declared_name(decl)
            decl_names.append(name or "?")

        # Resolve types for register-class filtering
        type_map = _resolve_decl_types(all_decls, decl_names, ctx)

        # Get swap pairs from diagnosis
        if not ctx.diagnosis:
            return
        swap_pairs = [
            pair for pair in ctx.diagnosis.reg_swap_pairs
            if pair[0].startswith("r") or pair[1].startswith("r")
        ]
        if not swap_pairs:
            return

        candidates = ghidra_guided_reorder(
            ghidra_vars=ctx.target_var_order,
            source_decl_names=decl_names,
            swap_pairs=swap_pairs,
            gpr_save_count=ctx.target_gpr_saves,
        )

        if not candidates:
            return

        # Filter candidates with cross-register-file swaps
        if type_map:
            filtered = []
            for cand in candidates:
                has_cross = False
                for i, name in enumerate(cand):
                    if i < len(decl_names) and name != decl_names[i]:
                        # This name moved — check if it crosses regfile with its new neighbor
                        if _is_cross_regfile_swap(name, decl_names[i], type_map):
                            has_cross = True
                            break
                if not has_cross:
                    filtered.append(cand)
            if len(filtered) < len(candidates):
                print(
                    f"  Type filter: {len(candidates) - len(filtered)} "
                    f"cross-regfile candidate(s) removed",
                    file=sys.stderr,
                )
            candidates = filtered
            if not candidates:
                return

        print(
            f"  Ghidra-guided reorder: {len(candidates)} candidate(s) "
            f"for {len(swap_pairs)} swap pair(s)",
            file=sys.stderr,
        )

        # Build dependency edges for safety checking
        deps = _build_dependency_edges(all_decls)

        counter = start_counter
        for candidate_names in candidates:
            # Map candidate name order back to node order
            name_to_node = {}
            for decl in all_decls:
                name = _get_declared_name(decl)
                if name:
                    name_to_node[name] = decl

            reordered = []
            valid = True
            for name in candidate_names:
                if name in name_to_node:
                    reordered.append(name_to_node[name])
                else:
                    valid = False
                    break

            if not valid or len(reordered) != len(all_decls):
                continue

            # Check dependency safety
            perm_indices = [all_decls.index(n) for n in reordered]
            if not _respects_deps(perm_indices, deps):
                continue

            new_source = _apply_reorder(ctx.file_source, all_decls, reordered)
            if new_source == ctx.file_source:
                continue

            moved = [n for i, n in enumerate(candidate_names)
                     if candidate_names[i] != decl_names[i]]
            desc = f"Ghidra-guided reorder: {', '.join(moved[:4])}"

            yield Variant(
                name=f"ghidra_declreorder_{counter}",
                pattern_name=self.name,
                description=desc,
                source=new_source,
                tags=frozenset({"reordered_declarations"}),
            )
            counter += 1

    def _try_asm_guided(self, ctx: FunctionContext, start_counter: int) -> Iterator[Variant]:
        """Generate ASM-guided reorder variants using /FAs assembly listing.

        Lightweight strategy: compiles with /FAs to get source-interleaved
        assembly, extracts var→register mapping, then uses asm_guided_search()
        to generate targeted swaps. No BSF tracing required.

        Priority: runs after Ghidra-guided, before BSF-guided.
        """
        try:
            from tools.compiler_trace.asm_regmap import parse_asm_listing
            from tools.compiler_trace.regmap_solver import asm_guided_search
            from tools.compiler_trace.invoker import CompilerInvoker
        except ImportError:
            return

        # Need diagnosis with swap pairs (GPR or FPR)
        if not ctx.diagnosis:
            return
        swap_pairs = [
            pair for pair in ctx.diagnosis.reg_swap_pairs
            if (pair[0].startswith("r") and pair[1].startswith("r"))
            or (pair[0].startswith("f") and pair[1].startswith("f"))
        ]
        if not swap_pairs:
            return

        # Need declarations to reorder
        all_decls = [s for s in ctx.statements if s.type == "declaration"]
        if len(all_decls) < 2:
            return

        decl_names = []
        for decl in all_decls:
            name = _get_declared_name(decl)
            decl_names.append(name or "?")

        # Resolve types for register-class filtering
        type_map = _resolve_decl_types(all_decls, decl_names, ctx)

        # Check cached asm lines first (from a previous BSF run in this session)
        asm_lines_key = str(ctx.file_path)
        cached_asm = (
            self._asm_lines_cache.get(asm_lines_key)
            if self._asm_lines_cache else None
        )

        if cached_asm is None:
            # Compile with /FAs to get source-interleaved assembly listing
            try:
                import tempfile
                import shutil

                invoker = CompilerInvoker()
                asm_dir = Path(tempfile.mkdtemp(prefix="asm_guided_", dir="/tmp/claude"))
                result = invoker.compile_with_asm(ctx.file_path, asm_dir, listing_type="/FAs")
                if result.returncode != 0:
                    shutil.rmtree(asm_dir, ignore_errors=True)
                    return

                # Find the listing file
                asm_file = None
                for ext in (".cod", ".asm"):
                    files = list(asm_dir.glob(f"*{ext}"))
                    if files:
                        asm_file = files[0]
                        break

                if asm_file:
                    cached_asm = asm_file.read_text().splitlines()
                    # Cache for reuse
                    if self._asm_lines_cache is None:
                        self._asm_lines_cache = {}
                    self._asm_lines_cache[asm_lines_key] = cached_asm

                shutil.rmtree(asm_dir, ignore_errors=True)
            except Exception:
                return

        if not cached_asm:
            return

        # Parse listing for target function
        func_name = ctx.symbol or ""
        asm_regmap = parse_asm_listing(cached_asm, func_name)
        if not asm_regmap or not asm_regmap.var_to_reg:
            return

        # Generate candidates via asm_guided_search
        candidates = asm_guided_search(asm_regmap, swap_pairs, decl_names)
        if not candidates:
            return

        print(
            f"  ASM-guided: {len(asm_regmap.var_to_reg)} var\u2192reg mappings "
            f"\u2192 {len(candidates)} candidate(s) "
            f"for {len(swap_pairs)} swap pair(s)",
            file=sys.stderr,
        )

        # Build dependency edges and emit variants
        deps = _build_dependency_edges(all_decls)
        counter = start_counter

        for candidate_names in candidates:
            # Map candidate name order back to node order
            name_to_node = {}
            for decl in all_decls:
                name = _get_declared_name(decl)
                if name:
                    name_to_node[name] = decl

            reordered = []
            valid = True
            for name in candidate_names:
                if name in name_to_node:
                    reordered.append(name_to_node[name])
                else:
                    valid = False
                    break

            if not valid or len(reordered) != len(all_decls):
                continue

            # Check dependency safety
            perm_indices = [all_decls.index(n) for n in reordered]
            if not _respects_deps(perm_indices, deps):
                continue

            # Filter cross-register-file swaps
            if type_map:
                has_cross = False
                for i, name in enumerate(candidate_names):
                    if i < len(decl_names) and name != decl_names[i]:
                        if _is_cross_regfile_swap(name, decl_names[i], type_map):
                            has_cross = True
                            break
                if has_cross:
                    continue

            new_source = _apply_reorder(ctx.file_source, all_decls, reordered)
            if new_source == ctx.file_source:
                continue

            moved = [n for i, n in enumerate(candidate_names)
                     if candidate_names[i] != decl_names[i]]
            desc = f"ASM-guided reorder: {', '.join(moved[:4])}"

            yield Variant(
                name=f"asm_declreorder_{counter}",
                pattern_name=self.name,
                description=desc,
                source=new_source,
                tags=frozenset({"reordered_declarations"}),
            )
            counter += 1

    def _try_bsf_guided(self, ctx: FunctionContext, start_counter: int) -> Iterator[Variant]:
        """Generate BSF-guided reorder variants.

        Traces the compiler's register allocation, identifies which
        variables get which colors, then generates targeted pairwise
        swaps instead of blind permutation.

        When possible, compiles with /FAs to get assembly listing and
        partitions BSF calls by function, isolating the target function's
        colorings from noise caused by other functions in the TU.
        """
        try:
            from tools.compiler_trace.bsf_trace import trace_bsf
            from tools.compiler_trace.regmap_solver import (
                extract_initial_colorings,
                guided_pairwise_search,
                asm_guided_search,
            )
            from tools.compiler_trace.asm_regmap import parse_asm_listing
        except ImportError:
            print(
                "  BSF mode: unavailable (tools.compiler_trace not found)",
                file=sys.stderr,
            )
            return

        # Need diagnosis with swap pairs — check BEFORE expensive BSF trace
        if not ctx.diagnosis:
            return

        # Get all declarations as a flat group
        all_decls = [s for s in ctx.statements if s.type == "declaration"]
        if len(all_decls) < 2:
            return

        decl_names = []
        for decl in all_decls:
            name = _get_declared_name(decl)
            decl_names.append(name or "?")

        # Trace BSF on current source (cached across composition passes)
        if self._bsf_cache is not None and self._bsf_cache_path == ctx.file_path:
            bsf = self._bsf_cache
        else:
            try:
                print("  BSF tracing...", end="", flush=True, file=sys.stderr)
                bsf = trace_bsf(ctx.file_path)
                print(f" {bsf.total_calls} calls", file=sys.stderr)
                self._bsf_cache = bsf
                self._bsf_cache_path = ctx.file_path
            except Exception as e:
                print(f"  BSF mode: fallback (trace failed: {e})", file=sys.stderr)
                return
        swap_pairs = [
            pair for pair in ctx.diagnosis.reg_swap_pairs
            if pair[0].startswith("r") or pair[1].startswith("r")
        ]
        if not swap_pairs:
            print("  BSF mode: fallback (no GPR swap pairs in diagnosis)", file=sys.stderr)
            return

        # Try to partition BSF trace by function using assembly listing (cached)
        cache_key = (str(ctx.file_path), ctx.symbol or "")
        asm_lines_key = str(ctx.file_path)
        if self._isolation_cache is not None and cache_key in self._isolation_cache:
            function_calls = self._isolation_cache[cache_key]
        else:
            function_calls = None
            try:
                from tools.compiler_trace.invoker import CompilerInvoker
                import tempfile

                invoker = CompilerInvoker()
                asm_dir = Path(tempfile.mkdtemp(prefix="bsf_asm_", dir="/tmp/claude"))
                result = invoker.compile_with_asm(ctx.file_path, asm_dir, listing_type="/FAs")
                if result.returncode == 0:
                    # Find the listing file
                    asm_file = None
                    for ext in (".cod", ".asm"):
                        files = list(asm_dir.glob(f"*{ext}"))
                        if files:
                            asm_file = files[0]
                            break
                    if asm_file:
                        asm_lines = asm_file.read_text().splitlines()

                        # Cache asm_lines for ASM-guided fallback
                        if self._asm_lines_cache is None:
                            self._asm_lines_cache = {}
                        self._asm_lines_cache[asm_lines_key] = asm_lines

                        partitions = bsf.partition_by_function(asm_lines)

                        # Tier 0: exact mangled symbol match (most reliable)
                        if ctx.symbol:
                            for part_name, part_trace in partitions.items():
                                if part_name in ("__all__", "__remainder__"):
                                    continue
                                if part_name == ctx.symbol:
                                    function_calls = part_trace.calls
                                    break

                        # Tier 1: qualified name match (Class::Method in mangled name)
                        if function_calls is None:
                            func_declarator = ctx.func_node.child_by_field_name("declarator")
                            func_name = ""
                            if func_declarator and func_declarator.text:
                                func_name = func_declarator.text.decode("utf-8", errors="replace")
                                paren_idx = func_name.find("(")
                                if paren_idx > 0:
                                    func_name = func_name[:paren_idx].strip()

                            class_name = ""
                            method_name = func_name
                            if "::" in func_name:
                                parts = func_name.rsplit("::", 1)
                                class_name = parts[0]
                                method_name = parts[1]

                            for part_name, part_trace in partitions.items():
                                if part_name in ("__all__", "__remainder__"):
                                    continue
                                if class_name and method_name:
                                    if (class_name in part_name and
                                            method_name in part_name):
                                        function_calls = part_trace.calls
                                        break
                                elif method_name:
                                    if method_name in part_name:
                                        function_calls = part_trace.calls
                                        break

                # Cleanup temp dir
                import shutil
                shutil.rmtree(asm_dir, ignore_errors=True)
            except Exception as e:
                if not self._bsf_printed:
                    print(f"  BSF isolation: skipped ({e})", file=sys.stderr)

            # Cache the result (even if None, to avoid retrying)
            if self._isolation_cache is None:
                self._isolation_cache = {}
            self._isolation_cache[cache_key] = function_calls

        # When BSF isolation yields 0 calls, try ASM-guided fallback
        use_asm_fallback = False
        if function_calls is not None and len(function_calls) == 0:
            use_asm_fallback = True

        candidates: list[list[str]] = []

        if not use_asm_fallback:
            # Generate BSF-guided candidates
            candidates = guided_pairwise_search(
                bsf, swap_pairs, decl_names, function_calls=function_calls
            )
            if not candidates:
                use_asm_fallback = True

        if use_asm_fallback:
            # ASM-guided fallback: parse /FAs listing for var→reg mapping
            cached_asm = (
                self._asm_lines_cache.get(asm_lines_key)
                if self._asm_lines_cache else None
            )
            if cached_asm:
                func_name = ctx.symbol or ""
                asm_regmap = parse_asm_listing(cached_asm, func_name)
                if asm_regmap and asm_regmap.var_to_reg:
                    gpr_swap_pairs = [
                        (r0, r1) for r0, r1 in swap_pairs
                        if r0.startswith("r") and r1.startswith("r")
                    ]
                    candidates = asm_guided_search(
                        asm_regmap, gpr_swap_pairs, decl_names
                    )
                    if candidates and not self._bsf_printed:
                        print(
                            f"  ASM-guided: {len(asm_regmap.var_to_reg)} var→reg mappings "
                            f"→ {len(candidates)} candidate(s) "
                            f"for {len(gpr_swap_pairs)} swap pair(s)",
                            file=sys.stderr,
                        )
                        self._bsf_printed = True

        if not candidates:
            if not self._bsf_printed:
                n_isolated = len(function_calls) if function_calls else bsf.total_calls
                print(
                    f"  BSF: traced {bsf.total_calls} calls, "
                    f"isolated {n_isolated} for target → no guided candidates",
                    file=sys.stderr,
                )
                self._bsf_printed = True
            return

        if not self._bsf_printed:
            n_isolated = len(function_calls) if function_calls else bsf.total_calls
            print(
                f"  BSF: traced {bsf.total_calls} calls, "
                f"isolated {n_isolated} → {len(candidates)} guided candidate(s) "
                f"for {len(swap_pairs)} swap pair(s)",
                file=sys.stderr,
            )
            self._bsf_printed = True

        # Build dependency edges for the full declarations group
        deps = _build_dependency_edges(all_decls)

        counter = start_counter
        for candidate_names in candidates:
            # Map candidate name order back to node order
            name_to_node = {}
            for decl in all_decls:
                name = _get_declared_name(decl)
                if name:
                    name_to_node[name] = decl

            reordered = []
            valid = True
            for name in candidate_names:
                if name in name_to_node:
                    reordered.append(name_to_node[name])
                else:
                    valid = False
                    break

            if not valid or len(reordered) != len(all_decls):
                continue

            # Check dependency safety
            perm_indices = [all_decls.index(n) for n in reordered]
            if not _respects_deps(perm_indices, deps):
                continue

            new_source = _apply_reorder(ctx.file_source, all_decls, reordered)
            if new_source == ctx.file_source:
                continue

            moved = [n for i, n in enumerate(candidate_names)
                     if candidate_names[i] != decl_names[i]]
            desc = f"BSF-guided reorder: {', '.join(moved[:4])}"

            yield Variant(
                name=f"bsf_declreorder_{counter}",
                pattern_name=self.name,
                description=desc,
                source=new_source,
                tags=frozenset({"reordered_declarations"}),
            )
            counter += 1


def _is_static_declaration(node: Node) -> bool:
    """Return True if a declaration has a `static` storage class specifier.

    Static local declarations use $S guards (compiler-internal), so reordering
    them never fixes ??_B vs $S guard naming differences. Exclude them from
    permutation groups to avoid wasting budget on unfixable candidates.
    """
    for child in node.children:
        if child.type == "storage_class_specifier" and child.text == b"static":
            return True
    return False


def _find_declaration_groups(ctx: FunctionContext) -> list[list[Node]]:
    """Find groups of declaration statements in the body.

    First pass: consecutive declarations (original behavior).
    Second pass: sparse pairs — declarations separated by 1-2 non-declaration
    statements. These are returned as 2-element groups for pairwise swapping.

    Only considers top-level statements in the function body.
    Static declarations are excluded: their guard ordering is compiler-internal
    (??_B vs $S naming) and cannot be fixed by reordering source declarations.
    """
    groups: list[list[Node]] = []
    current: list[Node] = []

    # Pass 1: consecutive groups
    consecutive_indices: set[int] = set()
    for i, stmt in enumerate(ctx.statements):
        if stmt.type == "declaration" and not _is_static_declaration(stmt):
            current.append(stmt)
        else:
            if len(current) >= 2:
                groups.append(current)
                # Track which declarations are already in consecutive groups
                start = i - len(current)
                for j in range(start, i):
                    consecutive_indices.add(j)
            current = []

    if len(current) >= 2:
        n = len(ctx.statements)
        start = n - len(current)
        for j in range(start, n):
            consecutive_indices.add(j)
        groups.append(current)

    # Pass 2: sparse pairs — find declarations separated by 1-2 statements
    # Exclude static declarations for same reason as pass 1.
    decl_indices = [
        i for i, stmt in enumerate(ctx.statements)
        if stmt.type == "declaration"
        and not _is_static_declaration(stmt)
        and i not in consecutive_indices
    ]

    for ai, a_idx in enumerate(decl_indices):
        for b_idx in decl_indices[ai + 1:]:
            gap = b_idx - a_idx - 1
            if gap < 1 or gap > 2:
                continue
            # Only pair if all intervening statements are non-declarations
            all_non_decl = all(
                ctx.statements[k].type != "declaration"
                for k in range(a_idx + 1, b_idx)
            )
            if all_non_decl:
                groups.append([ctx.statements[a_idx], ctx.statements[b_idx]])

    return groups


def _get_declared_name(decl: Node) -> str | None:
    """Extract the variable name from a declaration node."""
    declarator = decl.child_by_field_name("declarator")
    if declarator is None:
        return None
    # Unwrap init_declarator
    if declarator.type == "init_declarator":
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            declarator = inner
    # Unwrap pointer/reference declarators
    while declarator.type in ("pointer_declarator", "reference_declarator"):
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            declarator = inner
        else:
            break
    if declarator.text:
        return declarator.text.decode("utf-8", errors="replace")
    return None


def _get_initializer_identifiers(decl: Node) -> set[str]:
    """Collect all identifier names used in a declaration's initializer.

    Walks the init_declarator's value subtree to find referenced names.
    """
    declarator = decl.child_by_field_name("declarator")
    if declarator is None or declarator.type != "init_declarator":
        return set()
    value = declarator.child_by_field_name("value")
    if value is None:
        return set()
    return identifiers_in(value)


def _build_dependency_edges(group: list[Node]) -> dict[int, set[int]]:
    """Build a dependency graph for a declaration group.

    Returns a dict mapping each index to the set of indices that must come
    before it (i.e., decl[i] depends on decl[j] means j is in deps[i]).
    This prevents reorderings like `int y = x + 1;` before `int x = 5;`.
    """
    # Map variable name -> index in group for names declared in this group
    name_to_idx: dict[str, int] = {}
    for i, decl in enumerate(group):
        name = _get_declared_name(decl)
        if name:
            name_to_idx[name] = i

    # For each decl, check if its initializer references any earlier declaration
    deps: dict[int, set[int]] = {i: set() for i in range(len(group))}
    for i, decl in enumerate(group):
        init_ids = _get_initializer_identifiers(decl)
        for ref_name in init_ids:
            if ref_name in name_to_idx:
                j = name_to_idx[ref_name]
                if j != i:  # Don't depend on self
                    deps[i].add(j)

    return deps


def _respects_deps(perm_indices: list[int] | tuple[int, ...], deps: dict[int, set[int]]) -> bool:
    """Check if a permutation respects dependency ordering.

    For each element in perm_indices, all its dependencies must appear
    at an earlier position in the permutation.
    """
    pos = {idx: p for p, idx in enumerate(perm_indices)}
    for idx in perm_indices:
        for dep in deps[idx]:
            if pos[dep] > pos[idx]:
                return False
    return True


def _get_permutations(group: list[Node], deps: dict[int, set[int]] | None = None) -> list[list[Node]]:
    """Generate dependency-safe permutations, capping at _MAX_PERMS."""
    n = len(group)
    total_perms = 1
    for i in range(2, n + 1):
        total_perms *= i

    if deps is None:
        deps = {i: set() for i in range(n)}

    if total_perms <= _MAX_PERMS * 3:
        # Small enough to enumerate all and filter
        result = []
        for perm in itertools.permutations(range(n)):
            if list(perm) == list(range(n)):
                continue  # Skip identity
            if _respects_deps(perm, deps):
                result.append([group[i] for i in perm])
                if len(result) >= _MAX_PERMS:
                    break
        return result
    else:
        # Sample random permutations, filter for dependency safety
        seen: set[tuple[int, ...]] = set()
        identity = tuple(range(n))
        seen.add(identity)
        result = []
        attempts = 0
        while len(result) < _MAX_PERMS and attempts < _MAX_PERMS * 20:
            indices = list(range(n))
            random.shuffle(indices)
            key = tuple(indices)
            if key not in seen:
                seen.add(key)
                if _respects_deps(indices, deps):
                    result.append([group[i] for i in indices])
            attempts += 1
        return result


def _apply_reorder(source: bytes, original: list[Node], reordered: list[Node]) -> bytes:
    """Reorder declaration statements using SourceEditor.

    Each declaration in the reordered list takes the position (byte range)
    of the corresponding declaration in the original list, but with the
    content from the reordered node.
    """
    ed = SourceEditor(source)
    for orig_node, new_node in zip(original, reordered):
        if orig_node is not new_node:
            new_content = source[new_node.start_byte:new_node.end_byte]
            ed.replace_range(orig_node.start_byte, orig_node.end_byte, new_content)
    return ed.apply()


def _get_decl_name(node: Node, ctx: FunctionContext) -> str:
    """Extract a short name for a declaration node."""
    # Try to find the declarator identifier
    declarator = node.child_by_field_name("declarator")
    if declarator is not None:
        # Unwrap init_declarator
        if declarator.type == "init_declarator":
            inner = declarator.child_by_field_name("declarator")
            if inner is not None:
                declarator = inner
        if declarator.text:
            return declarator.text.decode("utf-8", errors="replace")

    # Fallback: first 30 chars of source
    text = ctx.source_text(node)
    return text[:30].strip()
