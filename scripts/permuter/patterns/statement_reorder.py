"""General statement reorder -- reorder independent statements within a block.

Win rate: untested (proven in manual fix for CharLipSyncDriver::Poll, DirLoader::LoadHeader).

Unlike assignment_reorder which only handles consecutive same-target assignments,
this pattern reorders arbitrary independent statements. Two statements are
independent if neither reads a variable written by the other.

Example:
    w = 0.0f;                       if (unkc4 < 0.0f)
    if (unkc4 < 0.0f)                   MILO_FAIL("...", unkc4);
        MILO_FAIL("...", unkc4);    if (weight < 0.0f)
    if (weight < 0.0f)          ->      MILO_FAIL("...", weight);
        MILO_FAIL("...", weight);   w = 0.0f;

Detection signals:
    - Clusters (insert/delete from reordered instructions)
    - Store instruction mismatches (stw/stfs reordering)
    - Offset deltas (instructions at wrong positions)
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..control_flow import iter_compound_statements
from ..editor import SourceEditor
from ..statement_effects import StatementEffectAnalyzer
from ..types import Diagnosis, FunctionContext, Variant


class StatementReorderPattern(Pattern):
    name = "statement_reorder"
    safety_tier = "conservative"
    structural_domain = "control_flow"
    follow_ups = ("declaration_reorder", "assignment_reorder", "declaration_movement")
    cross_unit_modes = ("inline_header",)

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Clusters suggest structural differences
        if diagnosis.clusters:
            return True

        # Store instruction mismatches
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("stw", "stfs", "stfd") or \
               d.base_opcode in ("stw", "stfs", "stfd"):
                return True

        # Offset deltas suggest instructions at wrong positions
        if diagnosis.offset_deltas:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if diagnosis.offset_deltas:
            return 0.5
        if diagnosis.clusters:
            return 0.4
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0
        analyzer = StatementEffectAnalyzer(source)

        # Collect all compound_statement nodes (function body + nested blocks)
        compound_stmts = list(iter_compound_statements(ctx.body_node))

        for compound in compound_stmts:
            if counter >= 8:
                break

            # Get direct child statements (named children only)
            children = [c for c in compound.named_children
                        if c.type not in ("comment",)]

            if len(children) < 2:
                continue

            # Find runs of 2-4 consecutive independent statements
            runs = _find_independent_runs(children, analyzer)

            for run in runs:
                if counter >= 8:
                    break

                # Try pairwise adjacent swaps
                for i in range(len(run) - 1):
                    if counter >= 8:
                        break

                    stmt_a = run[i]
                    stmt_b = run[i + 1]

                    if not analyzer.can_reorder_statement_pair(stmt_a, stmt_b):
                        continue

                    ed = SourceEditor(source)
                    _swap_statements(ed, source, stmt_a, stmt_b)

                    try:
                        new_source = ed.apply()
                    except ValueError:
                        continue

                    yield Variant(
                        name=f"stmt_reorder_{counter}",
                        pattern_name=self.name,
                        description=f"Swap statements in block",
                        source=new_source,
                        tags=frozenset({"reordered_statements"}),
                    )
                    counter += 1

                # Try moving a single statement to a different position
                if len(run) >= 3 and counter < 8:
                    # Move last to first position
                    last = run[-1]
                    first = run[0]

                    if all(analyzer.can_reorder_statement_pair(last, run[j])
                           for j in range(len(run) - 1)):
                        ed = SourceEditor(source)
                        _move_statement(ed, source, last, first)

                        try:
                            new_source = ed.apply()
                        except ValueError:
                            pass
                        else:
                            yield Variant(
                                name=f"stmt_reorder_{counter}",
                                pattern_name=self.name,
                                description=f"Move last statement to first in run of {len(run)}",
                                source=new_source,
                                tags=frozenset({"reordered_statements"}),
                            )
                            counter += 1
def _find_independent_runs(
    stmts: list[Node], analyzer: StatementEffectAnalyzer
) -> list[list[Node]]:
    """Find runs of 2-4 consecutive statements that are pairwise independent."""
    runs: list[list[Node]] = []
    i = 0

    while i < len(stmts):
        # Start a potential run
        run = [stmts[i]]
        j = i + 1

        while j < len(stmts) and len(run) < 4:
            # Check if the new statement is independent of at least one in the run
            # (we check pairwise independence during swap generation)
            candidate = stmts[j]

            # Skip control flow that can't be reordered
            if analyzer.analyze(candidate).has_control_flow:
                break

            if analyzer.analyze(run[-1]).has_control_flow:
                break

            run.append(candidate)
            j += 1

        if len(run) >= 2:
            runs.append(run)

        i = max(j, i + 1)

    return runs


def _swap_statements(
    ed: SourceEditor, source: bytes, stmt_a: Node, stmt_b: Node
) -> None:
    """Swap two statement nodes in the source."""
    a_start = _line_start(source, stmt_a.start_byte)
    a_end = _line_end(source, stmt_a.end_byte)
    b_start = _line_start(source, stmt_b.start_byte)
    b_end = _line_end(source, stmt_b.end_byte)

    text_a = source[a_start:a_end]
    text_b = source[b_start:b_end]

    ed.replace_range(b_start, b_end, text_a)
    ed.replace_range(a_start, a_end, text_b)


def _move_statement(
    ed: SourceEditor, source: bytes, stmt_to_move: Node, target_before: Node
) -> None:
    """Move a statement to just before another statement."""
    # Get the full line text of the statement to move
    move_start = _line_start(source, stmt_to_move.start_byte)
    move_end = _line_end(source, stmt_to_move.end_byte)
    move_text = source[move_start:move_end]

    # Get insertion point (start of target line)
    insert_at = _line_start(source, target_before.start_byte)

    # Delete from original position and insert at new position
    ed.delete_range(move_start, move_end)
    ed.insert_at(insert_at, move_text)


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
