"""Reorder consecutive calls at end of function to enable tail-call optimization.

Win rate: proven on Multiply(Transform, Matrix3, Transform) (65% -> 100%).

MSVC PPC can tail-call the last `bl` in a void function (emitting `b` instead
of `bl` + epilogue), eliminating prologue/epilogue overhead entirely. When two
independent calls end a function (or end a block before a bare return), the
call order determines which one gets the tail-call optimization.

This pattern detects consecutive independent calls at function/block ends and
tries swapping them. It also uses Ghidra decompilation to detect which call
order the target uses, enabling targeted reordering.

Transformations:
    Swap last two calls:
        FuncA(x, y);          FuncB(a, b);
        FuncB(a, b);    ->    FuncA(x, y);   // FuncA becomes tail call

Detection signals:
    - Prologue mismatch (target saves fewer registers = tail call present)
    - Size difference (base larger due to extra prologue/epilogue)
    - Last instruction: target uses `b` (branch), base uses `bl` (call)
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..cfg import build_cfg, get_terminal_blocks
from ..control_flow import (
    is_bare_return_statement,
    noncomment_named_children,
)
from ..editor import SourceEditor
from ..m2c import extract_last_call_name as extract_m2c_last_call_name
from ..statement_effects import StatementEffectAnalyzer
from ..types import Diagnosis, FunctionContext, Variant


@dataclass(frozen=True)
class _TailCallUnit:
    """A tail-position unit that can potentially be swapped."""

    swap_node: Node
    call_stmt: Node
    condition_node: Node | None = None


class TailCallReorderPattern(Pattern):
    name = "tail_call_reorder"
    safety_tier = "moderate"
    structural_domain = "control_flow"
    follow_ups = ("branch_polarity", "declaration_reorder")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Primary signal: prologue mismatch where target saves fewer regs
        if diagnosis.has_prologue_mismatch:
            if diagnosis.gpr_save_delta < 0 or diagnosis.fpr_save_delta < 0:
                return True

        # Secondary signal: insert/delete pattern at function boundaries
        # (prologue at start, epilogue at end)
        if diagnosis.clusters:
            has_early = any(c.start_idx < 5 for c in diagnosis.clusters)
            has_late = any(
                c.end_idx > diagnosis.total_instructions - 10
                for c in diagnosis.clusters
            )
            if has_early and has_late:
                return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strong signal: target needs fewer saves (tail call eliminated them)
        if diagnosis.has_prologue_mismatch and diagnosis.gpr_save_delta < 0:
            return 0.85
        return 0.4

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0

        guided_last_call = _extract_guided_last_call(ctx)

        # Strategy 1: Swap consecutive calls at end of function body
        for v in _swap_trailing_calls(ctx, source, guided_last_call, counter):
            yield v
            counter += 1
            if counter >= 8:
                return

        # Strategy 2: Swap consecutive calls before a bare return
        for v in _swap_calls_before_return(ctx, source, guided_last_call, counter):
            yield v
            counter += 1
            if counter >= 8:
                return

        # Strategy 3: Swap calls at end of terminal blocks (last if/else branches)
        for v in _swap_calls_in_terminal_blocks(ctx, source, guided_last_call, counter):
            yield v
            counter += 1
            if counter >= 8:
                return

        # Strategy 4: CFG-guided terminal block discovery
        for v in _swap_calls_in_cfg_terminals(ctx, source, guided_last_call, counter):
            yield v
            counter += 1
            if counter >= 8:
                return


def _swap_trailing_calls(
    ctx: FunctionContext,
    source: bytes,
    ghidra_last_call: str | None,
    start: int,
) -> Iterator[Variant]:
    """Swap consecutive call statements at the end of the function body."""
    stmts = ctx.statements
    if len(stmts) < 2:
        return

    counter = start
    analyzer = StatementEffectAnalyzer(source)

    unit_run = _trailing_call_units(stmts, source)
    if len(unit_run) < 2:
        return

    for variant in _variants_for_unit_run(
        unit_run,
        source,
        analyzer,
        ghidra_last_call,
        counter,
        max_variants=4,
        description_template="Swap {a}() and {b}() for tail-call{guided}",
    ):
        yield variant
        counter += 1


def _swap_calls_before_return(
    ctx: FunctionContext,
    source: bytes,
    ghidra_last_call: str | None,
    start: int,
) -> Iterator[Variant]:
    """Swap trailing call runs that appear before a bare `return;`."""
    counter = start
    analyzer = StatementEffectAnalyzer(source)

    for node in walk(ctx.body_node):
        if counter - start >= 4:
            return

        if node.type != "compound_statement":
            continue

        children = noncomment_named_children(node)
        if len(children) < 3:
            continue
        if not is_bare_return_statement(children[-1], source):
            continue

        unit_run = _trailing_call_units(children[:-1], source)
        if len(unit_run) < 2:
            continue

        if node.id == ctx.body_node.id:
            template = "Swap {a}() and {b}() before return for tail-call{guided}"
        else:
            template = (
                "Swap {a}() and {b}() in nested block before return "
                "for tail-call{guided}"
            )

        for variant in _variants_for_unit_run(
            unit_run,
            source,
            analyzer,
            ghidra_last_call,
            counter,
            max_variants=4 - (counter - start),
            description_template=template,
        ):
            yield variant
            counter += 1


def _swap_calls_in_terminal_blocks(
    ctx: FunctionContext,
    source: bytes,
    ghidra_last_call: str | None,
    start: int,
) -> Iterator[Variant]:
    """Swap consecutive calls at the end of terminal blocks (if/else/loop bodies)."""
    counter = start
    analyzer = StatementEffectAnalyzer(source)

    for node in walk(ctx.body_node):
        if counter - start >= 4:
            return

        if node.type != "compound_statement":
            continue
        # Skip the function body itself (handled by strategy 1)
        if node.id == ctx.body_node.id:
            continue

        children = noncomment_named_children(node)
        if len(children) < 2:
            continue

        unit_run = _trailing_call_units(children, source)
        if len(unit_run) < 2:
            continue

        # Must be at a terminal position (end of if/else, end of function path)
        parent = node.parent
        if parent is None:
            continue
        # Only swap in if/else branches and loop bodies
        if parent.type not in (
            "if_statement", "else_clause", "for_statement",
            "while_statement", "do_statement",
        ):
            continue

        for variant in _variants_for_unit_run(
            unit_run,
            source,
            analyzer,
            ghidra_last_call,
            counter,
            max_variants=4 - (counter - start),
            description_template=(
                "Swap {a}() and {b}() in nested block for tail-call{guided}"
            ),
        ):
            yield variant
            counter += 1


def _swap_calls_in_cfg_terminals(
    ctx: FunctionContext,
    source: bytes,
    ghidra_last_call: str | None,
    start: int,
) -> Iterator[Variant]:
    """Use CFG terminal-block analysis to find tail-call swap sites.

    This complements the AST-walk strategies by using control-flow graph
    analysis to find blocks that are provably terminal (all paths from them
    reach function exit).  This catches cases where compound nesting makes
    the AST walk miss valid swap sites.
    """
    counter = start
    analyzer = StatementEffectAnalyzer(source)

    try:
        cfg = build_cfg(ctx.body_node, source)
    except Exception:
        return

    seen_stmt_ids: set[int] = set()
    for block in get_terminal_blocks(cfg):
        if counter - start >= 4:
            return

        stmts = block.statements
        if len(stmts) < 2:
            continue

        # Find trailing call run in this terminal block
        unit_run = _trailing_call_units(stmts, source, allow_bare_return_suffix=True)
        if len(unit_run) < 2:
            continue

        # Deduplicate against strategies 1-3 (same statement pair)
        pair_id = (unit_run[-2].swap_node.id, unit_run[-1].swap_node.id)
        if pair_id in seen_stmt_ids:
            continue
        seen_stmt_ids.add(pair_id[0])
        seen_stmt_ids.add(pair_id[1])

        for variant in _variants_for_unit_run(
            unit_run,
            source,
            analyzer,
            ghidra_last_call,
            counter,
            max_variants=4 - (counter - start),
            description_template=(
                "Swap {a}() and {b}() in CFG terminal block for tail-call{guided}"
            ),
        ):
            yield variant
            counter += 1


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _is_call_statement(node: Node) -> bool:
    """Check if a statement is an expression_statement containing a call."""
    if node.type != "expression_statement":
        return False
    for child in node.named_children:
        if child.type == "call_expression":
            return True
    return False


def _single_call_stmt_in_node(node: Node, source: bytes) -> Node | None:
    """Extract a single call statement from a simple wrapper node."""
    if _is_call_statement(node):
        return node

    if node.type != "compound_statement":
        return None

    children = noncomment_named_children(node)
    if len(children) != 1:
        return None
    child = children[0]
    if is_bare_return_statement(child, source):
        return None
    if _is_call_statement(child):
        return child
    return None


def _extract_tail_call_unit(node: Node, source: bytes) -> _TailCallUnit | None:
    """Extract a reorderable tail-position call unit from a statement node."""
    direct = _single_call_stmt_in_node(node, source)
    if direct is not None:
        return _TailCallUnit(swap_node=node, call_stmt=direct)

    if node.type != "if_statement":
        return None

    if node.child_by_field_name("alternative") is not None:
        return None

    consequence = node.child_by_field_name("consequence")
    condition = node.child_by_field_name("condition")
    if consequence is None or condition is None:
        return None

    # Only lift simple null/flag guards with a single call and no callful condition.
    if any(child.type == "call_expression" for child in walk(condition)):
        return None

    guarded = _single_call_stmt_in_node(consequence, source)
    if guarded is None:
        return None

    return _TailCallUnit(
        swap_node=node,
        call_stmt=guarded,
        condition_node=condition,
    )


def _trailing_call_units(
    stmts: list[Node],
    source: bytes,
    allow_bare_return_suffix: bool = False,
) -> list[_TailCallUnit]:
    """Collect trailing reorderable call units from a statement list."""
    run: list[_TailCallUnit] = []
    started = False

    for stmt in reversed(stmts):
        if allow_bare_return_suffix and not started and is_bare_return_statement(stmt, source):
            continue

        unit = _extract_tail_call_unit(stmt, source)
        if unit is None:
            break

        started = True
        run.insert(0, unit)

    return run
def _get_call_name(stmt: Node, source: bytes) -> str | None:
    """Extract the function name from a call statement."""
    for child in walk(stmt):
        if child.type == "call_expression":
            func = child.child_by_field_name("function")
            if func is not None:
                text = source[func.start_byte:func.end_byte].decode(
                    "utf-8", errors="replace"
                )
                # Get the bare name (strip Class::, obj->, obj.)
                for sep in ("::", "->", "."):
                    if sep in text:
                        text = text.rsplit(sep, 1)[-1]
                return text.strip()
    return None


def _should_try_swap(
    a: _TailCallUnit,
    b: _TailCallUnit,
    source: bytes,
    ghidra_last_call: str | None,
) -> bool:
    """Use Ghidra's last-call hint to suppress already-correct orderings."""
    if ghidra_last_call is None:
        return True

    a_name = _get_call_name(a.call_stmt, source)
    b_name = _get_call_name(b.call_stmt, source)
    if b_name == ghidra_last_call:
        return False
    if a_name == ghidra_last_call:
        return True
    return a_name is None or b_name is None


def _variants_for_unit_run(
    unit_run: list[_TailCallUnit],
    source: bytes,
    analyzer: StatementEffectAnalyzer,
    ghidra_last_call: str | None,
    start: int,
    max_variants: int,
    description_template: str,
) -> Iterator[Variant]:
    """Yield adjacent swaps from a trailing tail-unit run, prioritizing the tail pair."""
    counter = start

    for i in range(len(unit_run) - 1, 0, -1):
        if counter - start >= max_variants:
            return

        a = unit_run[i - 1]
        b = unit_run[i]
        if not _are_independent_units(a, b, source, analyzer):
            continue
        if not _should_try_swap(a, b, source, ghidra_last_call):
            continue

        ed = SourceEditor(source)
        _swap_statement_ranges(ed, source, a.swap_node, b.swap_node)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        a_name = _get_call_name(a.call_stmt, source) or "?"
        b_name = _get_call_name(b.call_stmt, source) or "?"
        guided = " (Ghidra-guided)" if ghidra_last_call else ""
        yield Variant(
            name=f"tailcall_{counter}",
            pattern_name="tail_call_reorder",
            description=description_template.format(
                a=a_name,
                b=b_name,
                guided=guided,
            ),
            source=new_source,
            tags=frozenset({"reordered_tail_calls"}),
        )
        counter += 1


def _are_independent_calls(
    a: Node,
    b: Node,
    source: bytes,
    analyzer: StatementEffectAnalyzer,
) -> bool:
    """Check if two call statements can be safely reordered.

    Two calls are independent if:
    - Neither contains an assertion/guard macro
    - Neither reads a variable written by the other
    - The second doesn't dereference a pointer checked by the first
    """
    effects_a = analyzer.analyze(a)
    effects_b = analyzer.analyze(b)
    if effects_a.has_assert_like_guard or effects_b.has_assert_like_guard:
        return False

    if not analyzer.can_reorder_call_pair(a, b):
        return False

    # Check for pointer dereference dependency:
    # If a checks/uses a pointer that b dereferences (->), don't reorder
    if effects_a.reads & effects_b.dereferenced_identifiers:
        return False

    return True


def _are_independent_units(
    a: _TailCallUnit,
    b: _TailCallUnit,
    source: bytes,
    analyzer: StatementEffectAnalyzer,
) -> bool:
    """Check if two tail-call units can be safely reordered."""
    if not _are_independent_calls(a.call_stmt, b.call_stmt, source, analyzer):
        return False

    reads_a = set()
    reads_b = set()
    if a.condition_node is not None:
        reads_a |= analyzer.analyze(a.condition_node).reads
    if b.condition_node is not None:
        reads_b |= analyzer.analyze(b.condition_node).reads

    effects_a = analyzer.analyze(a.call_stmt)
    effects_b = analyzer.analyze(b.call_stmt)

    if reads_a & effects_b.writes:
        return False
    if reads_b & effects_a.writes:
        return False
    if reads_a & effects_b.dereferenced_identifiers:
        return False
    if reads_b & effects_a.dereferenced_identifiers:
        return False

    return True


def _swap_statement_ranges(
    ed: SourceEditor, source: bytes, a: Node, b: Node
) -> None:
    """Swap the full line ranges of two statements."""
    a_start = _line_start(source, a.start_byte)
    a_end = _line_end(source, a.end_byte)
    b_start = _line_start(source, b.start_byte)
    b_end = _line_end(source, b.end_byte)

    text_a = source[a_start:a_end]
    text_b = source[b_start:b_end]

    ed.replace_range(b_start, b_end, text_a)
    ed.replace_range(a_start, a_end, text_b)


def _line_start(source: bytes, pos: int) -> int:
    while pos > 0 and source[pos - 1:pos] not in (b"\n", b"\r"):
        pos -= 1
    return pos


def _line_end(source: bytes, pos: int) -> int:
    while pos < len(source) and source[pos:pos + 1] not in (b"\n", b"\r"):
        pos += 1
    if pos < len(source):
        pos += 1
    return pos


# ---------------------------------------------------------------------------
# Ghidra integration
# ---------------------------------------------------------------------------

_GUIDE_CALL_RE = re.compile(r"\b([a-zA-Z_]\w*)\s*\(")
_NOT_CALLS = frozenset({
    "if", "while", "for", "switch", "return", "sizeof", "typeof",
    "int", "long", "short", "char", "void", "float", "double",
    "uint", "ulong", "ushort", "uchar", "undefined", "undefined4",
    "undefined8", "undefined2", "undefined1", "bool", "byte",
})


def _extract_ghidra_last_call(ghidra_code: str) -> str | None:
    """Extract the name of the last function call in Ghidra's decompilation.

    This identifies which call the target uses as the tail call (the one
    that gets `b` instead of `bl`).
    """
    # Find all calls in order
    calls = []
    for m in _GUIDE_CALL_RE.finditer(ghidra_code):
        name = m.group(1)
        if name not in _NOT_CALLS and not name.startswith("local_"):
            calls.append(name)

    # The last call in the decompilation is the tail-call candidate
    if calls:
        return calls[-1]
    return None


def _extract_guided_last_call(ctx: FunctionContext) -> str | None:
    """Choose a preferred last-call hint from Ghidra and/or m2c."""
    ghidra_last_call = (
        _extract_ghidra_last_call(ctx.ghidra_code)
        if ctx.ghidra_code
        else None
    )
    m2c_last_call = (
        extract_m2c_last_call_name(ctx.m2c_code)
        if ctx.m2c_code
        else None
    )

    if ghidra_last_call and m2c_last_call:
        if ghidra_last_call == m2c_last_call:
            return ghidra_last_call
        return None
    return ghidra_last_call or m2c_last_call
