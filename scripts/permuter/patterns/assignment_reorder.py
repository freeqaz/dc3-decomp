"""Assignment statement reordering.

Win rate: untested (new pattern, but proven in manual fix for PlayBack::Reset).

Unlike declaration_reorder which permutes variable declarations, this pattern
reorders consecutive assignment statements (a.x = ...; a.y = ...; a.z = ...).
The MSVC compiler emits stores in source order, so reordering assignments
can fix offset swap and instruction ordering mismatches.

Transformations:
    w.unk18 = 0;     w.unk14 = 0;
    w.unk1c = 0;  -> w.unk18 = 0;
    w.unk14 = 0;     w.unk1c = 0;

Detection signals:
    - Offset swap patterns
    - Replace mismatches (store instruction reordering)
    - Clusters (contiguous insert/delete from reordered stores)
"""

from __future__ import annotations

import itertools
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


class AssignmentReorderPattern(Pattern):
    name = "assignment_reorder"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Offset swaps suggest store reordering
        if diagnosis.offset_deltas:
            return True

        # Clusters suggest instruction reordering
        if diagnosis.clusters:
            return True

        # Replace mismatches with stw/stfs (store instructions)
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("stw", "stfs", "stfd", "sth", "stb") or \
               d.base_opcode in ("stw", "stfs", "stfd", "sth", "stb"):
                return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if diagnosis.offset_deltas:
            return 0.6
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        stmts = ctx.statements
        counter = 0

        # Find runs of consecutive assignment statements
        runs = _find_assignment_runs(stmts, source)

        for run_start, run_end, assignments in runs:
            if counter >= 10:
                break

            run_len = len(assignments)
            if run_len < 2 or run_len > 6:
                continue

            # Generate pairwise swaps for small runs
            if run_len <= 4:
                # Try all pairwise swaps
                for i in range(run_len):
                    for j in range(i + 1, run_len):
                        if counter >= 10:
                            break

                        ed = SourceEditor(source)
                        _swap_statements(ed, source, assignments[i], assignments[j])

                        try:
                            new_source = ed.apply()
                        except ValueError:
                            continue

                        yield Variant(
                            name=f"asgn_swap_{counter}",
                            pattern_name=self.name,
                            description=f"Swap assignment {i} <-> {j} in run of {run_len}",
                            source=new_source,
                        )
                        counter += 1

            # For larger runs, try reverse order
            if run_len >= 3 and counter < 10:
                ed = SourceEditor(source)
                valid = _reverse_run(ed, source, assignments)

                if valid:
                    try:
                        new_source = ed.apply()
                        yield Variant(
                            name=f"asgn_rev_{counter}",
                            pattern_name=self.name,
                            description=f"Reverse {run_len} consecutive assignments",
                            source=new_source,
                        )
                        counter += 1
                    except ValueError:
                        pass


def _find_assignment_runs(
    stmts: list[Node], source: bytes
) -> list[tuple[int, int, list[Node]]]:
    """Find consecutive runs of assignment/expression statements.

    Groups consecutive statements that are:
    - expression_statement containing assignment_expression
    - All assigning to the same base object (e.g., w.member = ...)
    """
    runs = []
    i = 0

    while i < len(stmts):
        # Check if this is an assignment statement
        if not _is_assignment_stmt(stmts[i]):
            i += 1
            continue

        # Start a run
        base = _get_assignment_base(stmts[i], source)
        run = [stmts[i]]
        j = i + 1

        while j < len(stmts):
            if not _is_assignment_stmt(stmts[j]):
                break
            stmt_base = _get_assignment_base(stmts[j], source)
            # Group assignments to same base, or if no clear base
            if base is None or stmt_base is None or base == stmt_base:
                run.append(stmts[j])
                j += 1
            else:
                break

        if len(run) >= 2:
            runs.append((i, j, run))

        i = max(j, i + 1)

    return runs


def _is_assignment_stmt(stmt: Node) -> bool:
    """Check if a statement is a simple assignment (not compound like +=)."""
    if stmt.type != "expression_statement":
        return False

    # Should contain an assignment_expression
    for child in stmt.named_children:
        if child.type == "assignment_expression":
            return True
    return False


def _get_assignment_base(stmt: Node, source: bytes) -> bytes | None:
    """Get the base object of an assignment (e.g., 'w' from 'w.unk18 = 0').

    Returns None if no clear base object.
    """
    for child in stmt.named_children:
        if child.type != "assignment_expression":
            continue

        left = child.child_by_field_name("left")
        if left is None:
            continue

        # field_expression: base.member or base->member
        if left.type == "field_expression":
            arg = left.child_by_field_name("argument")
            if arg is not None:
                return source[arg.start_byte:arg.end_byte]

        # subscript_expression: base[i]
        if left.type == "subscript_expression":
            arg = left.child_by_field_name("argument") or left.named_children[0]
            if arg is not None:
                return source[arg.start_byte:arg.end_byte]

    return None


def _swap_statements(
    ed: SourceEditor, source: bytes, stmt_a: Node, stmt_b: Node
) -> None:
    """Swap two statement nodes in the source."""
    # Get full line content including indentation
    a_start = _line_start(source, stmt_a.start_byte)
    a_end = _line_end(source, stmt_a.end_byte)
    b_start = _line_start(source, stmt_b.start_byte)
    b_end = _line_end(source, stmt_b.end_byte)

    text_a = source[a_start:a_end]
    text_b = source[b_start:b_end]

    ed.replace_range(b_start, b_end, text_a)
    ed.replace_range(a_start, a_end, text_b)


def _reverse_run(
    ed: SourceEditor, source: bytes, assignments: list[Node]
) -> bool:
    """Reverse the order of a run of assignment statements."""
    # Collect the text of each statement line
    lines = []
    ranges = []
    for stmt in assignments:
        start = _line_start(source, stmt.start_byte)
        end = _line_end(source, stmt.end_byte)
        lines.append(source[start:end])
        ranges.append((start, end))

    # Replace each range with the reversed text
    reversed_lines = list(reversed(lines))
    for i, (start, end) in enumerate(ranges):
        ed.replace_range(start, end, reversed_lines[i])

    return True


def _line_start(source: bytes, pos: int) -> int:
    """Find the start of the line containing pos."""
    while pos > 0 and source[pos - 1:pos] not in (b"\n", b"\r"):
        pos -= 1
    return pos


def _line_end(source: bytes, pos: int) -> int:
    """Find the end of the line containing pos (after newline)."""
    while pos < len(source) and source[pos:pos + 1] not in (b"\n", b"\r"):
        pos += 1
    if pos < len(source):
        pos += 1  # include the newline
    return pos
