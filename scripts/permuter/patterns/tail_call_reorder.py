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
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, find_calls, identifiers_in, node_text
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


class TailCallReorderPattern(Pattern):
    name = "tail_call_reorder"

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

        # Try to get Ghidra's last call for guided reordering
        ghidra_last_call = None
        if ctx.ghidra_code:
            ghidra_last_call = _extract_ghidra_last_call(ctx.ghidra_code)

        # Strategy 1: Swap consecutive calls at end of function body
        for v in _swap_trailing_calls(ctx, source, ghidra_last_call, counter):
            yield v
            counter += 1
            if counter >= 8:
                return

        # Strategy 2: Swap consecutive calls before a bare return
        for v in _swap_calls_before_return(ctx, source, ghidra_last_call, counter):
            yield v
            counter += 1
            if counter >= 8:
                return

        # Strategy 3: Swap calls at end of terminal blocks (last if/else branches)
        for v in _swap_calls_in_terminal_blocks(ctx, source, ghidra_last_call, counter):
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

    # Find runs of consecutive call statements at the end
    end = len(stmts)
    call_run_start = end
    for i in range(end - 1, -1, -1):
        if _is_call_statement(stmts[i]):
            call_run_start = i
        else:
            break

    call_run = stmts[call_run_start:end]
    if len(call_run) < 2:
        return

    # Try swapping adjacent pairs (prioritize last pair)
    for i in range(len(call_run) - 1, 0, -1):
        if counter - start >= 4:
            return
        a = call_run[i - 1]
        b = call_run[i]

        if not _are_independent_calls(a, b, source):
            continue

        # If Ghidra tells us which call should be last, only try that
        if ghidra_last_call:
            a_name = _get_call_name(a, source)
            if a_name and a_name == ghidra_last_call:
                # a should be last, but it's currently first -> swap
                pass
            elif not a_name:
                pass  # can't determine, try anyway
            else:
                b_name = _get_call_name(b, source)
                if b_name and b_name == ghidra_last_call:
                    # b is already last, skip
                    continue

        ed = SourceEditor(source)
        _swap_statement_ranges(ed, source, a, b)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        a_name = _get_call_name(a, source) or "?"
        b_name = _get_call_name(b, source) or "?"
        guided = " (Ghidra-guided)" if ghidra_last_call else ""
        yield Variant(
            name=f"tailcall_{counter}",
            pattern_name="tail_call_reorder",
            description=(
                f"Swap {a_name}() and {b_name}() for tail-call"
                f"{guided}"
            ),
            source=new_source,
        )
        counter += 1


def _swap_calls_before_return(
    ctx: FunctionContext,
    source: bytes,
    ghidra_last_call: str | None,
    start: int,
) -> Iterator[Variant]:
    """Swap consecutive calls that appear before a bare `return;`."""
    stmts = ctx.statements
    if len(stmts) < 3:
        return

    counter = start

    # Look for pattern: call; call; return;
    for i in range(len(stmts) - 2):
        if counter - start >= 4:
            return

        ret = stmts[i + 2] if i + 2 < len(stmts) else None
        if ret is None or ret.type != "return_statement":
            continue

        # Check if return is bare (no value)
        ret_text = source[ret.start_byte:ret.end_byte].strip()
        if ret_text not in (b"return;", b"return ;"):
            continue

        a = stmts[i]
        b = stmts[i + 1]
        if not _is_call_statement(a) or not _is_call_statement(b):
            continue
        if not _are_independent_calls(a, b, source):
            continue

        ed = SourceEditor(source)
        _swap_statement_ranges(ed, source, a, b)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        a_name = _get_call_name(a, source) or "?"
        b_name = _get_call_name(b, source) or "?"
        yield Variant(
            name=f"tailcall_{counter}",
            pattern_name="tail_call_reorder",
            description=f"Swap {a_name}() and {b_name}() before return for tail-call",
            source=new_source,
        )
        counter += 1


def _swap_calls_in_terminal_blocks(
    ctx: FunctionContext,
    source: bytes,
    ghidra_last_call: str | None,
    start: int,
) -> Iterator[Variant]:
    """Swap consecutive calls at the end of terminal blocks (if/else/loop bodies)."""
    counter = start

    for node in walk(ctx.body_node):
        if counter - start >= 4:
            return

        if node.type != "compound_statement":
            continue
        # Skip the function body itself (handled by strategy 1)
        if node.id == ctx.body_node.id:
            continue

        children = [c for c in node.named_children if c.type != "comment"]
        if len(children) < 2:
            continue

        # Check if last two are call statements
        a = children[-2]
        b = children[-1]
        if not _is_call_statement(a) or not _is_call_statement(b):
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

        if not _are_independent_calls(a, b, source):
            continue

        ed = SourceEditor(source)
        _swap_statement_ranges(ed, source, a, b)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        a_name = _get_call_name(a, source) or "?"
        b_name = _get_call_name(b, source) or "?"
        yield Variant(
            name=f"tailcall_{counter}",
            pattern_name="tail_call_reorder",
            description=(
                f"Swap {a_name}() and {b_name}() in nested block for tail-call"
            ),
            source=new_source,
        )
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


def _get_writes(stmt: Node, source: bytes) -> set[str]:
    """Get variable names written by a statement."""
    writes: set[str] = set()
    for node in walk(stmt):
        if node.type == "assignment_expression":
            left = node.child_by_field_name("left")
            if left and left.type == "identifier" and left.text:
                writes.add(left.text.decode())
    return writes


def _are_independent_calls(a: Node, b: Node, source: bytes) -> bool:
    """Check if two call statements can be safely reordered.

    Two calls are independent if:
    - Neither contains an assertion/guard macro
    - Neither reads a variable written by the other
    - The second doesn't dereference a pointer checked by the first
    """
    # Never reorder assertions — they guard subsequent code
    text_a = source[a.start_byte:a.end_byte]
    text_b = source[b.start_byte:b.end_byte]
    for guard in (b"MILO_ASSERT", b"MILO_FAIL", b"assert(", b"ASSERT("):
        if guard in text_a or guard in text_b:
            return False

    ids_a = identifiers_in(a)
    ids_b = identifiers_in(b)
    writes_a = _get_writes(a, source)
    writes_b = _get_writes(b, source)

    # WAR/RAW/WAW check
    if writes_a & ids_b:
        return False
    if ids_a & writes_b:
        return False
    if writes_a & writes_b:
        return False

    # Check for pointer dereference dependency:
    # If a checks/uses a pointer that b dereferences (->), don't reorder
    for node in walk(b):
        if node.type == "field_expression":
            op = node.child_by_field_name("operator")
            if op and op.text == b"->":
                arg = node.child_by_field_name("argument")
                if arg and arg.type == "identifier" and arg.text:
                    ptr_name = arg.text.decode("utf-8", errors="replace")
                    if ptr_name in ids_a:
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

# Match function calls in Ghidra output — captures the function name
_GHIDRA_CALL_RE = re.compile(r"\b([a-zA-Z_]\w*)\s*\(")
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
    for m in _GHIDRA_CALL_RE.finditer(ghidra_code):
        name = m.group(1)
        if name not in _NOT_CALLS and not name.startswith("local_"):
            calls.append(name)

    # The last call in the decompilation is the tail-call candidate
    if calls:
        return calls[-1]
    return None
