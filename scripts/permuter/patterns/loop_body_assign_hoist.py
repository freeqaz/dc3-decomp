"""Loop body assignment hoisting.

Win rate: proven (RndBitmap::Load 94.3% -> 100%).

Inside a loop body, a ``dst = src;`` assignment that FOLLOWS a call statement
is hoisted BEFORE the call when the two statements are independent (neither
reads a variable written by the other, and the assignment does not assign the
call's return value). MWCC/MSVC may schedule the assignment before the call to
free a register; if our source has it after, the binary differs.

Canonical example (Bitmap::Load mip-chain loop):

    while (mipCt--) {
        RndBitmap *newMip = new RndBitmap();
        workingMip->mMip = newMip;
        workingW = workingW >> 1;
        workingH = workingH >> 1;
        newMip->Create(workingW, workingH, 0, mBpp, mOrder, mPalette, 0, 0);
        ReadChunks(bs, newMip->mPixels, newMip->PixelBytes(), 0x8000);
        workingMip = newMip;   // <- AFTER the calls; compiler wanted it BEFORE Create()
    }

The fix hoists the assignment above Create():

    while (mipCt--) {
        ...
        workingMip = newMip;   // hoisted here
        newMip->Create(...);
        ReadChunks(...);
    }

Detection signals:
    - Clusters (insert/delete from reordered instructions in loop body)
    - ``mr rN, rM`` adjacent to ``bl`` in diff (register move + call reordering)
    - Offset deltas near loop-body cluster boundaries
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..control_flow import iter_compound_statements
from ..editor import SourceEditor
from ..statement_effects import StatementEffectAnalyzer
from ..types import Diagnosis, FunctionContext, Variant

# Loop statement types recognized by tree-sitter C++
_LOOP_TYPES = frozenset({"for_statement", "while_statement", "do_statement"})

# Max loop bodies to inspect per function
_MAX_LOOP_BODIES = 4

# Max variant count total
_MAX_VARIANTS = 8


class LoopBodyAssignHoistPattern(Pattern):
    """Hoist a post-call assignment to before the call inside a loop body."""

    name = "loop_body_assign_hoist"
    safety_tier = "conservative"
    structural_domain = "control_flow"
    follow_ups = ("statement_reorder", "assignment_reorder")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Clusters suggest structural reordering — the primary signal
        if diagnosis.clusters:
            return True
        # Offset deltas can accompany loop-body reordering
        if diagnosis.offset_deltas:
            return True
        # mr-adjacent-to-bl is a register-move-before-call signal
        for d in diagnosis.diff_ops:
            if d.target_opcode == "mr" or d.base_opcode == "mr":
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        if diagnosis.clusters:
            return 0.5
        if diagnosis.offset_deltas:
            return 0.4
        return 0.3

    def context_priority(self, diagnosis: Diagnosis, ctx: FunctionContext) -> float:
        """Boost if the function body actually contains a loop."""
        base = self.priority(diagnosis)
        if base == 0.0:
            return 0.0
        if _has_loop_body(ctx.body_node):
            return min(1.0, base + 0.1)
        return base

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        analyzer = StatementEffectAnalyzer(source)
        counter = 0

        # Collect all compound_statement nodes (loop bodies + nested blocks)
        for compound in iter_compound_statements(ctx.body_node):
            if counter >= _MAX_VARIANTS:
                break

            # Only process compounds that are direct children of loop nodes
            if not _is_loop_body(compound):
                continue

            # Region filter: skip if entirely outside mismatch regions
            if not ctx.node_in_mismatch_region(compound):
                continue

            stmts = [c for c in compound.named_children if c.type != "comment"]
            if len(stmts) < 2:
                continue

            # Scan for: <call_stmt> immediately before <assign_stmt>
            # where the two are independent and the assignment is not a
            # local-variable initializer that received the call's return value.
            for i in range(len(stmts) - 1):
                if counter >= _MAX_VARIANTS:
                    break

                call_stmt = stmts[i]
                assign_stmt = stmts[i + 1]

                # The current statement must contain a call
                call_effects = analyzer.analyze(call_stmt)
                if not call_effects.has_call:
                    continue

                # The next statement must be a simple assignment or pointer assignment
                if not _is_simple_assignment(assign_stmt, source):
                    continue

                assign_effects = analyzer.analyze(assign_stmt)

                # Reject if assignment writes something the call also writes
                # (e.g., the assignment captures the call's return via a decl)
                if _assign_is_call_result(call_stmt, assign_stmt, source):
                    continue

                # Reject if the call reads anything written by the assignment
                # (hoisting would change the value the call sees)
                if call_effects.reads & assign_effects.writes:
                    continue

                # Reject if the assignment reads anything the call writes
                if assign_effects.reads & call_effects.writes:
                    continue

                # Reject if the call has control flow (break/return/goto)
                if call_effects.has_control_flow:
                    continue

                # Safety: reject if the assignment has side effects beyond write
                if assign_effects.has_call:
                    continue

                # Found a hoist candidate: move assign_stmt to just before call_stmt
                # Find the immediately preceding non-call statement to hoist before
                # (we want to place it just before call_stmt at index i)
                ed = SourceEditor(source)
                try:
                    _move_statement_before(ed, source, assign_stmt, call_stmt)
                    new_source = ed.apply()
                except ValueError:
                    continue

                if new_source == source:
                    continue

                assign_text = source[assign_stmt.start_byte:assign_stmt.end_byte]
                assign_str = assign_text.decode("utf-8", errors="replace").strip()
                if len(assign_str) > 60:
                    assign_str = assign_str[:57] + "..."

                yield Variant(
                    name=f"hoist_assign_{counter}",
                    pattern_name=self.name,
                    description=f"Hoist loop assignment before call: {assign_str}",
                    source=new_source,
                    tags=frozenset({"hoisted_assignment", "loop_body"}),
                )
                counter += 1

            # Also try hoisting assignments that are TWO statements after a call
            # (e.g., two calls then the assignment — hoist before the first call)
            for i in range(len(stmts) - 2):
                if counter >= _MAX_VARIANTS:
                    break

                call_stmt = stmts[i]
                mid_stmt = stmts[i + 1]
                assign_stmt = stmts[i + 2]

                call_effects = analyzer.analyze(call_stmt)
                if not call_effects.has_call:
                    continue

                if not _is_simple_assignment(assign_stmt, source):
                    continue

                assign_effects = analyzer.analyze(assign_stmt)
                mid_effects = analyzer.analyze(mid_stmt)

                if _assign_is_call_result(call_stmt, assign_stmt, source):
                    continue
                if _assign_is_call_result(mid_stmt, assign_stmt, source):
                    continue

                # Must be independent of both intermediate statements
                if call_effects.reads & assign_effects.writes:
                    continue
                if assign_effects.reads & call_effects.writes:
                    continue
                if mid_effects.reads & assign_effects.writes:
                    continue
                if assign_effects.reads & mid_effects.writes:
                    continue

                if call_effects.has_control_flow or mid_effects.has_control_flow:
                    continue
                if assign_effects.has_call:
                    continue

                ed = SourceEditor(source)
                try:
                    _move_statement_before(ed, source, assign_stmt, call_stmt)
                    new_source = ed.apply()
                except ValueError:
                    continue

                if new_source == source:
                    continue

                assign_text = source[assign_stmt.start_byte:assign_stmt.end_byte]
                assign_str = assign_text.decode("utf-8", errors="replace").strip()
                if len(assign_str) > 60:
                    assign_str = assign_str[:57] + "..."

                yield Variant(
                    name=f"hoist_assign2_{counter}",
                    pattern_name=self.name,
                    description=f"Hoist loop assignment 2 before call: {assign_str}",
                    source=new_source,
                    tags=frozenset({"hoisted_assignment", "loop_body"}),
                )
                counter += 1


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _has_loop_body(node: Node) -> bool:
    """Return True if any descendant is a loop statement."""
    for n in walk(node):
        if n.type in _LOOP_TYPES:
            return True
    return False


def _is_loop_body(compound: Node) -> bool:
    """Return True if this compound_statement is the body of a loop node."""
    parent = compound.parent
    if parent is None:
        return False
    return parent.type in _LOOP_TYPES


def _is_simple_assignment(stmt: Node, source: bytes) -> bool:
    """Return True if the statement is a pure assignment (no compound ops, no call).

    Accepts:
    - ``x = y;``             (expression_statement with assignment_expression)
    - ``x = y->z;``          (field/subscript access RHS)
    - ``x = new Foo();``     (new expression — still a simple assignment)

    Rejects:
    - ``x += y;``            (compound assignment)
    - ``int x = call();``    (declaration — captured by _assign_is_call_result)
    - ``x = f();``           (RHS has a call — may have side effects)
    """
    if stmt.type != "expression_statement":
        return False

    # Find the assignment_expression child
    for child in stmt.named_children:
        if child.type == "assignment_expression":
            # Reject compound operators (+=, -=, etc.)
            op = child.child_by_field_name("operator")
            if op is None or op.text != b"=":
                return False
            return True

    return False


def _assign_is_call_result(call_stmt: Node, assign_stmt: Node, source: bytes) -> bool:
    """Return True when the assignment captures the return value of call_stmt.

    Pattern: the preceding statement declared a local via the call's return
    (``Foo *newFoo = call();``) AND the assignment's RHS is that new local.
    This is safe to detect by checking whether the assignment's RHS text
    matches any name declared in the preceding statement.
    """
    # Collect names declared (written) by the call statement
    declared: set[bytes] = set()

    for node in walk(call_stmt):
        if node.type == "declaration":
            declarator = node.child_by_field_name("declarator")
            if declarator is not None:
                _collect_declared_names(declarator, declared)
        elif node.type == "init_declarator":
            decl_part = node.child_by_field_name("declarator")
            if decl_part is not None:
                _collect_declared_names(decl_part, declared)

    if not declared:
        return False

    # Check whether the assignment's RHS contains one of those names
    for child in assign_stmt.named_children:
        if child.type == "assignment_expression":
            rhs = child.child_by_field_name("right")
            if rhs is not None:
                rhs_text = source[rhs.start_byte:rhs.end_byte]
                for name in declared:
                    if name in rhs_text:
                        return True
    return False


def _collect_declared_names(declarator: Node, out: set[bytes]) -> None:
    """Collect identifier names from a declarator node."""
    if declarator.type == "identifier" and declarator.text:
        out.add(declarator.text)
        return
    if declarator.type == "pointer_declarator":
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            _collect_declared_names(inner, out)
        return
    if declarator.type == "reference_declarator":
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            _collect_declared_names(inner, out)
        return
    if declarator.type == "init_declarator":
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            _collect_declared_names(inner, out)
        return
    for child in declarator.named_children:
        _collect_declared_names(child, out)


def _move_statement_before(
    ed: SourceEditor, source: bytes, stmt_to_move: Node, target_before: Node
) -> None:
    """Move stmt_to_move to just before target_before in the source."""
    move_start = _line_start(source, stmt_to_move.start_byte)
    move_end = _line_end(source, stmt_to_move.end_byte)
    move_text = source[move_start:move_end]

    insert_at = _line_start(source, target_before.start_byte)

    # Delete the statement from its current position, insert before target
    ed.delete_range(move_start, move_end)
    ed.insert_at(insert_at, move_text)


def _line_start(source: bytes, pos: int) -> int:
    """Find the start of the line containing pos."""
    while pos > 0 and source[pos - 1:pos] not in (b"\n", b"\r"):
        pos -= 1
    return pos


def _line_end(source: bytes, pos: int) -> int:
    """Find the end of the line containing pos (including the newline)."""
    while pos < len(source) and source[pos:pos + 1] not in (b"\n", b"\r"):
        pos += 1
    if pos < len(source):
        pos += 1  # include the newline
    return pos
