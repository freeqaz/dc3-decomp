"""Parameter live range pattern — kill bs parameter after LOAD_REVS.

In Load functions using LOAD_REVS(bs), the macro creates BinStreamRev d(bs, revs).
If bs is still used after that point, the compiler keeps it alive in a callee-saved
register. Replacing bs with d.stream kills the parameter's live range, freeing a
register and potentially matching the target's prologue.

Strategies:
1. Replace bs identifiers after LOAD_REVS with d.stream
2. Replace LOAD_SUPERCLASS(ClassName) macros with ClassName::Load(d.stream)
3. Combined: replace ALL bs sources (identifiers + macros) at once
4. Merge consecutive d >> x; d >> y; into d >> x >> y; (chain merging)
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, get_line_start
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Matches LOAD_SUPERCLASS(ClassName) — class name may be qualified (Hmx::Object)
_LOAD_SUPERCLASS_RE = re.compile(rb'LOAD_SUPERCLASS\s*\(\s*([A-Za-z_][\w:]*)\s*\)')


class ParameterLiveRangePattern(Pattern):
    name = "parameter_live_range"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Most useful for prologue mismatches where target needs fewer regs
        if diagnosis.has_prologue_mismatch:
            return True
        # Also relevant when there are register swaps or insert/delete clusters,
        # which can indicate different register allocation from bs live range
        if diagnosis.reg_swap_pairs or diagnosis.clusters:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if diagnosis.has_prologue_mismatch and diagnosis.gpr_save_delta < 0:
            return 0.9
        if diagnosis.has_prologue_mismatch:
            return 0.7
        # Lower priority when no prologue mismatch but regswaps/clusters present
        if diagnosis.reg_swap_pairs or diagnosis.clusters:
            return 0.4
        return 0.0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        stmts = ctx.statements

        # Find LOAD_REVS position — it's a macro that expands to:
        #   int revs; bs >> revs; BinStreamRev d(bs, revs);
        # In source, look for LOAD_REVS(bs) or the expanded form BinStreamRev d(bs
        load_revs_idx = _find_load_revs(source, stmts)
        if load_revs_idx is None:
            return

        counter = 0

        # Strategy 1: Replace bs identifiers with d.stream after LOAD_REVS
        for v in self._replace_bs_with_dstream(ctx, stmts, load_revs_idx, counter):
            yield v
            counter += 1
            if counter >= 18:
                return

        # Strategy 2: Replace LOAD_SUPERCLASS(X) macros with X::Load(d.stream)
        for v in self._replace_load_superclass_macro(ctx, stmts, load_revs_idx, counter):
            yield v
            counter += 1
            if counter >= 18:
                return

        # Strategy 3: Combined — replace ALL bs sources (identifiers + macros)
        for v in self._replace_all_bs_sources(ctx, stmts, load_revs_idx, counter):
            yield v
            counter += 1
            if counter >= 18:
                return

        # Strategy 4: Merge consecutive d >> chains
        # Chaining keeps BinStreamRev& return value in a callee-saved register,
        # eliminating repeated reloads of d's address (proven fix, MEMORY.md)
        for v in self._merge_dstream_chains(ctx, stmts, counter):
            yield v
            counter += 1
            if counter >= 18:
                return

    def _replace_bs_with_dstream(
        self,
        ctx: FunctionContext,
        stmts: list[Node],
        load_revs_idx: int,
        start: int,
    ) -> Iterator[Variant]:
        """Replace bs identifiers after LOAD_REVS with d.stream."""
        source = ctx.file_source
        counter = start

        # Find all bs identifiers after LOAD_REVS
        bs_uses: list[tuple[int, Node]] = []
        for i in range(load_revs_idx + 1, len(stmts)):
            for node in walk(stmts[i]):
                if node.type == "identifier" and node.text == b"bs":
                    # Skip if this bs is part of a declaration (e.g., BinStream &bs)
                    parent = node.parent
                    if parent and parent.type in (
                        "reference_declarator",
                        "pointer_declarator",
                        "init_declarator",
                    ):
                        continue
                    bs_uses.append((i, node))

        if not bs_uses:
            return

        # Generate variant replacing each individual bs with d.stream
        for idx, (stmt_idx, bs_node) in enumerate(bs_uses):
            if counter - start >= 8:
                break
            ed = SourceEditor(source)
            ed.replace_node(bs_node, b"d.stream")
            try:
                new_source = ed.apply()
            except ValueError:
                continue

            # Describe what we're replacing
            stmt_text = source[stmts[stmt_idx].start_byte:stmts[stmt_idx].end_byte]
            stmt_str = stmt_text.decode("utf-8", errors="replace").strip()
            if len(stmt_str) > 50:
                stmt_str = stmt_str[:47] + "..."

            yield Variant(
                name=f"parlr_bs2ds_{counter}",
                pattern_name=self.name,
                description=f"Replace bs→d.stream in: {stmt_str}",
                source=new_source,
            )
            counter += 1

        # Try replacing ALL bs uses at once
        if len(bs_uses) > 1 and counter - start < 10:
            ed = SourceEditor(source)
            for _, bs_node in bs_uses:
                ed.replace_node(bs_node, b"d.stream")
            try:
                new_source = ed.apply()
            except ValueError:
                return

            yield Variant(
                name=f"parlr_bs2ds_all_{counter}",
                pattern_name=self.name,
                description=f"Replace ALL {len(bs_uses)} bs→d.stream after LOAD_REVS",
                source=new_source,
            )
            counter += 1

        # Try replacing bs with just d (BinStreamRev also has operator>>)
        if counter - start < 11:
            ed = SourceEditor(source)
            for _, bs_node in bs_uses:
                ed.replace_node(bs_node, b"d")
            try:
                new_source = ed.apply()
            except ValueError:
                return

            yield Variant(
                name=f"parlr_bs2d_all_{counter}",
                pattern_name=self.name,
                description=f"Replace ALL {len(bs_uses)} bs→d after LOAD_REVS",
                source=new_source,
            )

    def _replace_load_superclass_macro(
        self,
        ctx: FunctionContext,
        stmts: list[Node],
        load_revs_idx: int,
        start: int,
    ) -> Iterator[Variant]:
        """Replace LOAD_SUPERCLASS(ClassName) with ClassName::Load(d.stream); after LOAD_REVS."""
        source = ctx.file_source
        counter = start

        macro_uses = _find_load_superclass_uses(source, stmts, load_revs_idx)
        if not macro_uses:
            return

        # Generate variant replacing each individual LOAD_SUPERCLASS
        for stmt_idx, class_name in macro_uses:
            if counter - start >= 6:
                break

            stmt = stmts[stmt_idx]
            indent = get_indent(source, stmt)
            line_start = get_line_start(source, stmt)
            replacement = indent + class_name + b"::Load(d.stream);"

            ed = SourceEditor(source)
            ed.replace_range(line_start, stmt.end_byte, replacement)
            try:
                new_source = ed.apply()
            except ValueError:
                continue

            class_str = class_name.decode("utf-8", errors="replace")
            yield Variant(
                name=f"parlr_lsmacro_{counter}",
                pattern_name=self.name,
                description=f"Replace LOAD_SUPERCLASS({class_str}) -> {class_str}::Load(d.stream)",
                source=new_source,
            )
            counter += 1

        # Try replacing ALL LOAD_SUPERCLASS macros at once
        if len(macro_uses) > 1 and counter - start < 8:
            ed = SourceEditor(source)
            for stmt_idx, class_name in macro_uses:
                stmt = stmts[stmt_idx]
                indent = get_indent(source, stmt)
                line_start = get_line_start(source, stmt)
                replacement = indent + class_name + b"::Load(d.stream);"
                ed.replace_range(line_start, stmt.end_byte, replacement)
            try:
                new_source = ed.apply()
            except ValueError:
                return

            yield Variant(
                name=f"parlr_lsmacro_all_{counter}",
                pattern_name=self.name,
                description=f"Replace ALL {len(macro_uses)} LOAD_SUPERCLASS -> ::Load(d.stream)",
                source=new_source,
            )
            counter += 1

    def _replace_all_bs_sources(
        self,
        ctx: FunctionContext,
        stmts: list[Node],
        load_revs_idx: int,
        start: int,
    ) -> Iterator[Variant]:
        """Combined: replace ALL bs identifiers AND LOAD_SUPERCLASS macros at once."""
        source = ctx.file_source
        counter = start

        # Find bs identifiers
        bs_uses: list[tuple[int, Node]] = []
        for i in range(load_revs_idx + 1, len(stmts)):
            for node in walk(stmts[i]):
                if node.type == "identifier" and node.text == b"bs":
                    parent = node.parent
                    if parent and parent.type in (
                        "reference_declarator",
                        "pointer_declarator",
                        "init_declarator",
                    ):
                        continue
                    bs_uses.append((i, node))

        # Find LOAD_SUPERCLASS macros
        macro_uses = _find_load_superclass_uses(source, stmts, load_revs_idx)

        # Only useful as combined if we have BOTH types
        if not bs_uses or not macro_uses:
            return

        # Variant A: all bs -> d.stream + all LOAD_SUPERCLASS -> ::Load(d.stream)
        ed = SourceEditor(source)
        for _, bs_node in bs_uses:
            ed.replace_node(bs_node, b"d.stream")
        for stmt_idx, class_name in macro_uses:
            stmt = stmts[stmt_idx]
            indent = get_indent(source, stmt)
            line_start = get_line_start(source, stmt)
            ed.replace_range(line_start, stmt.end_byte, indent + class_name + b"::Load(d.stream);")
        try:
            new_source = ed.apply()
            total = len(bs_uses) + len(macro_uses)
            yield Variant(
                name=f"parlr_combined_ds_{counter}",
                pattern_name=self.name,
                description=f"Replace ALL bs sources: {len(bs_uses)} bs->d.stream + {len(macro_uses)} LOAD_SUPERCLASS",
                source=new_source,
            )
            counter += 1
        except ValueError:
            pass

        # Variant B: all bs -> d + all LOAD_SUPERCLASS -> ::Load(d.stream)
        if counter - start < 3:
            ed = SourceEditor(source)
            for _, bs_node in bs_uses:
                ed.replace_node(bs_node, b"d")
            for stmt_idx, class_name in macro_uses:
                stmt = stmts[stmt_idx]
                indent = get_indent(source, stmt)
                line_start = get_line_start(source, stmt)
                ed.replace_range(line_start, stmt.end_byte, indent + class_name + b"::Load(d.stream);")
            try:
                new_source = ed.apply()
                yield Variant(
                    name=f"parlr_combined_d_{counter}",
                    pattern_name=self.name,
                    description=f"Replace ALL: {len(bs_uses)} bs->d + {len(macro_uses)} LOAD_SUPERCLASS->d.stream",
                    source=new_source,
                )
                counter += 1
            except ValueError:
                pass

    def _merge_dstream_chains(
        self,
        ctx: FunctionContext,
        stmts: list[Node],
        start: int,
    ) -> Iterator[Variant]:
        """Merge consecutive d >> x; d >> y; into d >> x >> y;"""
        source = ctx.file_source
        counter = start

        # Find runs of consecutive d >> statements
        runs = _find_dstream_runs(source, stmts)

        for run_start, run_end in runs:
            if counter - start >= 6:
                break

            # Merge the entire run into one statement
            run_stmts = stmts[run_start:run_end + 1]
            if len(run_stmts) < 2:
                continue

            # Build merged statement: take first stmt's d >> prefix,
            # then append each subsequent stmt's RHS
            first_text = source[run_stmts[0].start_byte:run_stmts[0].end_byte]
            first_stripped = first_text.rstrip()
            if first_stripped.endswith(b";"):
                first_stripped = first_stripped[:-1]

            rhs_parts = []
            for stmt in run_stmts[1:]:
                text = source[stmt.start_byte:stmt.end_byte].strip()
                # Remove leading "d >>" or "d>>"
                rhs = _strip_d_prefix(text)
                if rhs is None:
                    break
                # Remove trailing semicolon
                rhs = rhs.rstrip(b";").strip()
                if rhs:
                    rhs_parts.append(rhs)
            else:
                if not rhs_parts:
                    continue

                merged = first_stripped
                for part in rhs_parts:
                    merged += b"\n" + get_indent(source, run_stmts[0]) + b"  >> " + part
                merged += b";"

                ed = SourceEditor(source)
                # Replace the entire range from first stmt start to last stmt end
                # including any trailing newline
                replace_start = run_stmts[0].start_byte
                replace_end = run_stmts[-1].end_byte
                # Consume trailing whitespace/newlines for all but the last
                # We need to delete from first stmt start to last stmt end
                # But preserve the indent of the first statement
                indent = get_indent(source, run_stmts[0])
                line_start = get_line_start(source, run_stmts[0])

                # Delete all statements in the run, replace with merged
                ed.replace_range(line_start, replace_end, indent + merged)

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                n = len(run_stmts)
                yield Variant(
                    name=f"parlr_chain_{counter}",
                    pattern_name=self.name,
                    description=f"Merge {n} consecutive d>> statements into chain",
                    source=new_source,
                )
                counter += 1

        # Also try merging pairs (less aggressive)
        for run_start, run_end in runs:
            if counter - start >= 6:
                break
            if run_end - run_start < 2:
                # Already tried as a run, now try individual pairs within larger runs
                continue

            for i in range(run_start, run_end):
                if counter - start >= 6:
                    break

                a = stmts[i]
                b = stmts[i + 1]
                text_a = source[a.start_byte:a.end_byte].rstrip()
                text_b = source[b.start_byte:b.end_byte].strip()

                if text_a.endswith(b";"):
                    text_a = text_a[:-1]
                rhs_b = _strip_d_prefix(text_b)
                if rhs_b is None:
                    continue
                rhs_b = rhs_b.rstrip(b";").strip()
                if not rhs_b:
                    continue

                indent = get_indent(source, a)
                merged = text_a + b"\n" + indent + b"  >> " + rhs_b + b";"

                ed = SourceEditor(source)
                line_a = get_line_start(source, a)
                ed.replace_range(line_a, b.end_byte, indent + merged)

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                yield Variant(
                    name=f"parlr_pair_{counter}",
                    pattern_name=self.name,
                    description=f"Merge pair d>> statements at index {i},{i+1}",
                    source=new_source,
                )
                counter += 1


# -- Helpers ------------------------------------------------------------------

def _find_load_superclass_uses(
    source: bytes, stmts: list[Node], load_revs_idx: int
) -> list[tuple[int, bytes]]:
    """Find LOAD_SUPERCLASS(ClassName) calls after LOAD_REVS.

    Returns list of (stmt_idx, class_name_bytes).
    """
    uses: list[tuple[int, bytes]] = []
    for i in range(load_revs_idx + 1, len(stmts)):
        text = source[stmts[i].start_byte:stmts[i].end_byte]
        m = _LOAD_SUPERCLASS_RE.search(text)
        if m:
            uses.append((i, m.group(1)))
    return uses


def _find_load_revs(source: bytes, stmts: list[Node]) -> int | None:
    """Find the index of the statement containing LOAD_REVS(bs).

    Handles both the macro form and the expanded form:
    - LOAD_REVS(bs)
    - BinStreamRev d(bs, revs)
    """
    for i, stmt in enumerate(stmts):
        text = source[stmt.start_byte:stmt.end_byte]
        if b"LOAD_REVS" in text:
            return i
        if b"BinStreamRev" in text and b"d(" in text:
            return i
    return None


def _find_dstream_runs(
    source: bytes, stmts: list[Node]
) -> list[tuple[int, int]]:
    """Find runs of consecutive statements starting with d >> or d>>.

    Returns list of (start_idx, end_idx) inclusive.
    """
    runs: list[tuple[int, int]] = []
    i = 0
    while i < len(stmts):
        text = source[stmts[i].start_byte:stmts[i].end_byte].strip()
        if _is_dstream_stmt(text):
            start = i
            while i + 1 < len(stmts):
                next_text = source[stmts[i + 1].start_byte:stmts[i + 1].end_byte].strip()
                if _is_dstream_stmt(next_text):
                    i += 1
                else:
                    break
            if i > start:  # At least 2 consecutive
                runs.append((start, i))
        i += 1
    return runs


def _is_dstream_stmt(text: bytes) -> bool:
    """Check if a statement starts with d >> (BinStreamRev extraction)."""
    stripped = text.lstrip()
    return stripped.startswith(b"d >>") or stripped.startswith(b"d>>")


def _strip_d_prefix(text: bytes) -> bytes | None:
    """Strip leading 'd >>' or 'd>>' from a statement, returning the RHS."""
    stripped = text.lstrip()
    if stripped.startswith(b"d >>"):
        return stripped[4:]
    if stripped.startswith(b"d>>"):
        return stripped[3:]
    return None
