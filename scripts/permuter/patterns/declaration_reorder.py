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
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import identifiers_in
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Maximum permutations to generate per group before switching to sampling
_MAX_PERMS = 20


class DeclarationReorderPattern(Pattern):
    name = "declaration_reorder"

    # Set by the permuter when --bsf-guided is enabled
    bsf_guided: bool = False
    # Cache BSF trace to avoid re-tracing on composition passes
    _bsf_cache: object = None  # BSFTrace or None
    _bsf_cache_path: object = None  # Path that was traced

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Only relevant when there are GPR swap pairs
        for (r0, r1) in diagnosis.reg_swap_pairs:
            if r0.startswith("r") or r1.startswith("r"):
                return True
        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0

        # Try BSF-guided generation first
        if self.bsf_guided:
            for variant in self._try_bsf_guided(ctx, counter):
                yield variant
                counter += 1

        # Then fill remaining budget with random permutations
        for group in _find_declaration_groups(ctx):
            if len(group) < 2:
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
                )
                counter += 1

    def _try_bsf_guided(self, ctx: FunctionContext, start_counter: int) -> Iterator[Variant]:
        """Generate BSF-guided reorder variants.

        Traces the compiler's register allocation, identifies which
        variables get which colors, then generates targeted pairwise
        swaps instead of blind permutation.
        """
        try:
            from tools.compiler_trace.bsf_trace import trace_bsf
            from tools.compiler_trace.regmap_solver import (
                extract_initial_colorings,
                guided_pairwise_search,
            )
        except ImportError:
            print(
                "BSF guidance unavailable (tools.compiler_trace not found)",
                file=sys.stderr,
            )
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
                print(f" failed: {e}", file=sys.stderr)
                return

        # Get swap pairs from diagnosis
        if not ctx.diagnosis:
            return
        swap_pairs = [
            pair for pair in ctx.diagnosis.reg_swap_pairs
            if pair[0].startswith("r") or pair[1].startswith("r")
        ]
        if not swap_pairs:
            return

        # Generate guided candidates
        candidates = guided_pairwise_search(bsf, swap_pairs, decl_names)
        if not candidates:
            return

        print(
            f"  BSF guidance: {len(candidates)} candidates for "
            f"{len(swap_pairs)} swap pair(s)",
            file=sys.stderr,
        )

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
            )
            counter += 1


def _find_declaration_groups(ctx: FunctionContext) -> list[list[Node]]:
    """Find groups of declaration statements in the body.

    First pass: consecutive declarations (original behavior).
    Second pass: sparse pairs — declarations separated by 1-2 non-declaration
    statements. These are returned as 2-element groups for pairwise swapping.

    Only considers top-level statements in the function body.
    """
    groups: list[list[Node]] = []
    current: list[Node] = []

    # Pass 1: consecutive groups
    consecutive_indices: set[int] = set()
    for i, stmt in enumerate(ctx.statements):
        if stmt.type == "declaration":
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
    decl_indices = [
        i for i, stmt in enumerate(ctx.statements)
        if stmt.type == "declaration" and i not in consecutive_indices
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
