"""Merge duplicate MILO macro calls via shared variable + goto.

Win rate: untested (new pattern).

When multiple MILO macro calls share the same format string but differ in
one argument, the original code often used a single call reached via goto
with a variable set beforehand. The decomp may have duplicated the call
for readability.

Merging saves ~10-20 instructions per eliminated duplicate call.

Transformations:
    if (cond1) {                      const char *arg;
        MILO_WARN("fmt", a, X);      if (cond1) {
        goto label;                       arg = X;
    }                          ->     } else if (cond2) {
    if (cond2) {                          arg = Y;
        MILO_WARN("fmt", a, Y);      } else {
        goto label;                       goto skip;
    }                                 }
                                      MILO_WARN("fmt", a, arg);
                                      goto label;
                                      skip:

Detection signals:
    - Large insert/delete clusters (duplicated macro = 10-20 extra instructions)
    - Multiple bl to same MakeString template in diff_ops
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, node_text
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# MILO macros that take format strings
_MILO_MACROS = {
    b"MILO_NOTIFY", b"MILO_WARN", b"MILO_LOG", b"MILO_FAIL",
    b"MILO_ASSERT", b"MILO_ASSERT_FMT", b"MILO_NOTIFY_ONCE",
}


class MiloCallMergePattern(Pattern):
    name = "milo_call_merge"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Large clusters suggest duplicated code
        for c in diagnosis.clusters:
            if c.size >= 6:
                return True
        # Significant insert/delete count (via clusters)
        total_inserts = sum(c.inserts for c in diagnosis.clusters)
        total_deletes = sum(c.deletes for c in diagnosis.clusters)
        if total_inserts > 5 or total_deletes > 5:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        return 0.2

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        # Find all MILO macro calls and group by format string
        calls = _find_milo_calls_with_args(body, source)
        groups = _group_by_format_string(calls, source)

        for fmt_str, group in groups.items():
            if len(group) < 2 or counter >= 4:
                break

            # Try merging pairs
            for i in range(len(group) - 1):
                if counter >= 4:
                    break
                for j in range(i + 1, len(group)):
                    if counter >= 4:
                        break

                    call_a = group[i]
                    call_b = group[j]

                    variant = _try_merge_pair(source, call_a, call_b, counter)
                    if variant is not None:
                        yield variant
                        counter += 1


def _find_milo_calls_with_args(
    node: Node, source: bytes
) -> list[tuple[Node, bytes, list[Node]]]:
    """Find MILO macro calls, returning (call_node, macro_name, arg_nodes)."""
    results = []
    for n in walk(node):
        if n.type != "call_expression":
            continue
        func = n.child_by_field_name("function")
        if func is None:
            continue
        func_text = source[func.start_byte:func.end_byte]
        if func_text not in _MILO_MACROS:
            continue
        args_node = n.child_by_field_name("arguments")
        if args_node is None:
            continue
        args = list(args_node.named_children)
        if len(args) < 1:
            continue
        results.append((n, func_text, args))
    return results


def _group_by_format_string(
    calls: list[tuple[Node, bytes, list[Node]]], source: bytes
) -> dict[bytes, list[tuple[Node, bytes, list[Node]]]]:
    """Group calls by (macro_name, format_string)."""
    groups: dict[bytes, list[tuple[Node, bytes, list[Node]]]] = {}
    for call_node, macro_name, args in calls:
        if not args:
            continue
        fmt_arg = args[0]
        fmt_text = source[fmt_arg.start_byte:fmt_arg.end_byte]
        key = macro_name + b":" + fmt_text
        groups.setdefault(key, []).append((call_node, macro_name, args))
    return groups


def _try_merge_pair(
    source: bytes,
    call_a: tuple[Node, bytes, list[Node]],
    call_b: tuple[Node, bytes, list[Node]],
    counter: int,
) -> Variant | None:
    """Try to merge two MILO calls that differ in exactly one argument.

    Only handles the simple case: both calls are direct expression_statements
    (not nested in complex control flow beyond a simple if-block).
    """
    node_a, macro_a, args_a = call_a
    node_b, macro_b, args_b = call_b

    if len(args_a) != len(args_b) or len(args_a) < 2:
        return None

    # Find which argument positions differ
    differing = []
    for idx in range(len(args_a)):
        text_a = source[args_a[idx].start_byte:args_a[idx].end_byte]
        text_b = source[args_b[idx].start_byte:args_b[idx].end_byte]
        if text_a != text_b:
            differing.append(idx)

    # Only handle single-argument differences for now
    if len(differing) != 1:
        return None

    diff_idx = differing[0]
    arg_a_text = source[args_a[diff_idx].start_byte:args_a[diff_idx].end_byte]
    arg_b_text = source[args_b[diff_idx].start_byte:args_b[diff_idx].end_byte]

    # Find the enclosing expression_statement for each call
    stmt_a = _find_enclosing_statement(node_a)
    stmt_b = _find_enclosing_statement(node_b)
    if stmt_a is None or stmt_b is None:
        return None

    # Find the enclosing if-block for each statement (if any)
    if_a = _find_enclosing_if(stmt_a)
    if_b = _find_enclosing_if(stmt_b)
    if if_a is None or if_b is None:
        return None

    # Get conditions
    cond_a = if_a.child_by_field_name("condition")
    cond_b = if_b.child_by_field_name("condition")
    if cond_a is None or cond_b is None:
        return None

    cond_a_text = source[cond_a.start_byte:cond_a.end_byte]
    cond_b_text = source[cond_b.start_byte:cond_b.end_byte]

    indent = get_indent(source, if_a)

    # Check for goto statements after the MILO call in each if-block
    goto_a = _find_goto_in_block(if_a, source)
    goto_b = _find_goto_in_block(if_b, source)

    # Build the merged code
    # Use auto to let the compiler deduce the type (args may be float, int, etc.)
    var_type = b"auto"
    var_name = b"_mergedArg"

    # Build the shared MILO call with the variable substituted
    shared_args = []
    for idx in range(len(args_a)):
        if idx == diff_idx:
            shared_args.append(var_name)
        else:
            shared_args.append(source[args_a[idx].start_byte:args_a[idx].end_byte])

    macro_call = macro_a + b"(" + b", ".join(shared_args) + b")"

    lines = []
    lines.append(indent + var_type + b" " + var_name + b";")
    lines.append(indent + b"if " + cond_a_text + b" {")
    lines.append(indent + b"    " + var_name + b" = " + arg_a_text + b";")
    lines.append(indent + b"} else if " + cond_b_text + b" {")
    lines.append(indent + b"    " + var_name + b" = " + arg_b_text + b";")
    lines.append(indent + b"} else {")

    # Use the goto from the original if available, otherwise skip
    if goto_a:
        goto_text = source[goto_a.start_byte:goto_a.end_byte]
        lines.append(indent + b"    " + goto_text)
    else:
        lines.append(indent + b"    goto _mergeSkip;")

    lines.append(indent + b"}")
    lines.append(indent + macro_call + b";")

    if goto_a:
        goto_text = source[goto_a.start_byte:goto_a.end_byte]
        lines.append(indent + goto_text)

    if not goto_a:
        lines.append(indent + b"_mergeSkip:;")

    merged = b"\n".join(lines)

    # Determine the full range to replace (from start of first if to end of second if)
    start = min(if_a.start_byte, if_b.start_byte)
    end = max(if_a.end_byte, if_b.end_byte)

    # Include any whitespace/newlines between the two if blocks
    # Find the line start of the first if
    line_start = start
    while line_start > 0 and source[line_start - 1:line_start] not in (b"\n",):
        line_start -= 1

    ed = SourceEditor(source)
    ed.replace_range(start, end, merged)

    try:
        new_source = ed.apply()
    except ValueError:
        return None

    return Variant(
        name=f"callmerge_{counter}",
        pattern_name="milo_call_merge",
        description=f"Merge duplicate {macro_a.decode()} calls (arg {diff_idx} differs)",
        source=new_source,
    )


def _find_enclosing_statement(node: Node) -> Node | None:
    """Walk up to find the expression_statement containing this node."""
    cur = node
    while cur is not None:
        if cur.type == "expression_statement":
            return cur
        cur = cur.parent
    return None


def _find_enclosing_if(node: Node) -> Node | None:
    """Walk up to find the if_statement containing this node."""
    cur = node
    while cur is not None:
        if cur.type == "if_statement":
            return cur
        cur = cur.parent
    return None


def _find_goto_in_block(if_node: Node, source: bytes) -> Node | None:
    """Find a goto_statement in the consequence block of an if_statement."""
    consequence = if_node.child_by_field_name("consequence")
    if consequence is None:
        return None
    for n in walk(consequence):
        if n.type == "goto_statement":
            return n
    return None
