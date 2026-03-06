"""Switch/if-else conversion — convert between switch and if/else if chains.

For small enum dispatches, switch and if/else if generate different code.
Proven relevant from TexBlender session where switch matched but if/else didn't.

Direction 1: switch -> if/else if chain
Direction 2: if/else if chain (comparing same var to constants) -> switch
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent
from ..types import Diagnosis, FunctionContext, Variant

_BRANCH_OPCODES = {"beq", "bne", "ble", "bgt", "bge", "blt",
                   "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
                   "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-"}


class SwitchIfConvertPattern(Pattern):
    name = "switch_if_convert"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Branch opcode mismatches suggest control flow structure differences
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        if len(diagnosis.clusters) >= 2:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        return 0.4  # Moderate — structural change

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0

        for stmt in ctx.statements:
            # Direction 1: switch -> if/else if
            for variant in _switch_to_if(stmt, ctx, counter):
                yield variant
                counter += 1

            # Direction 2: if/else if -> switch
            for variant in _if_to_switch(stmt, ctx, counter):
                yield variant
                counter += 1


def _switch_to_if(node: Node, ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Convert switch statement to if/else if chain."""
    if node.type != "switch_statement":
        for child in node.children:
            yield from _switch_to_if(child, ctx, counter)
        return

    condition = node.child_by_field_name("condition")
    body = node.child_by_field_name("body")
    if condition is None or body is None:
        return

    source = ctx.file_source

    # Get the switch expression
    switch_expr = _get_inner_expr(condition)
    if switch_expr is None:
        return
    switch_var = source[switch_expr.start_byte:switch_expr.end_byte]

    # Extract cases from the switch body
    cases = _extract_switch_cases(body, source)
    if not cases or len(cases) < 2 or len(cases) > 8:
        return

    indent = get_indent(source, node)
    inner_indent = indent + b"    "
    nl = b"\n"

    parts = []
    default_body = None

    for i, (case_val, case_body_lines) in enumerate(cases):
        if case_val is None:
            # default case
            default_body = case_body_lines
            continue

        body_text = nl.join(inner_indent + line for line in case_body_lines)

        if not parts:
            parts.append(
                indent + b"if (" + switch_var + b" == " + case_val + b") {" + nl
                + body_text + nl
                + indent + b"}"
            )
        else:
            parts.append(
                b" else if (" + switch_var + b" == " + case_val + b") {" + nl
                + body_text + nl
                + indent + b"}"
            )

    if default_body:
        body_text = nl.join(inner_indent + line for line in default_body)
        parts.append(
            b" else {" + nl
            + body_text + nl
            + indent + b"}"
        )

    if not parts:
        return

    new_code = parts[0]
    for p in parts[1:]:
        new_code += p

    new_source = source[:node.start_byte] + new_code + source[node.end_byte:]

    yield Variant(
        name=f"switchif_{counter}",
        pattern_name="switch_if_convert",
        description=f"switch -> if/else if ({len(cases)} cases)",
        source=new_source,
    )


def _if_to_switch(node: Node, ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Convert if/else if chain to switch statement."""
    if node.type != "if_statement":
        for child in node.children:
            yield from _if_to_switch(child, ctx, counter)
        return

    source = ctx.file_source

    # Collect the if/else if chain
    chain = _collect_if_chain(node, source)
    if chain is None or len(chain) < 3:
        return

    # All conditions must compare the same variable to constants
    switch_var = chain[0][0]
    if switch_var is None:
        return

    indent = get_indent(source, node)
    inner_indent = indent + b"    "
    nl = b"\n"

    parts = [indent + b"switch (" + switch_var + b") {" + nl]

    for var, val, body_lines in chain:
        if var is None and val is None:
            # else clause -> default
            body_text = nl.join(inner_indent + line for line in body_lines)
            parts.append(inner_indent + b"default:" + nl + body_text + nl + inner_indent + b"break;" + nl)
        else:
            body_text = nl.join(inner_indent + line for line in body_lines)
            parts.append(inner_indent + b"case " + val + b":" + nl + body_text + nl + inner_indent + b"break;" + nl)

    parts.append(indent + b"}")

    new_code = b"".join(parts)
    new_source = source[:node.start_byte] + new_code + source[node.end_byte:]

    yield Variant(
        name=f"switchif_{counter}",
        pattern_name="switch_if_convert",
        description=f"if/else if -> switch ({len(chain)} branches)",
        source=new_source,
    )


def _extract_switch_cases(body: Node, source: bytes) -> list[tuple[bytes | None, list[bytes]]]:
    """Extract (case_value_or_None_for_default, body_lines) from switch body.

    Returns list of (value, lines) tuples. Strips `break;` from end of each case.
    """
    cases: list[tuple[bytes | None, list[bytes]]] = []
    current_value: bytes | None = None
    current_lines: list[bytes] = []
    in_case = False

    for child in body.named_children:
        if child.type == "case_statement":
            # Save previous case
            if in_case:
                cases.append((current_value, _strip_break(current_lines)))
            # Get case value
            value_node = child.child_by_field_name("value")
            if value_node is not None:
                current_value = source[value_node.start_byte:value_node.end_byte]
            else:
                current_value = None
            # Collect body statements
            current_lines = []
            for stmt_child in child.named_children:
                if stmt_child.type != "comment" and stmt_child != value_node:
                    line = source[stmt_child.start_byte:stmt_child.end_byte].strip()
                    if line:
                        current_lines.append(line)
            in_case = True

        elif child.type == "default_statement":
            if in_case:
                cases.append((current_value, _strip_break(current_lines)))
            current_value = None
            current_lines = []
            for stmt_child in child.named_children:
                if stmt_child.type != "comment":
                    line = source[stmt_child.start_byte:stmt_child.end_byte].strip()
                    if line:
                        current_lines.append(line)
            in_case = True

    # Save last case
    if in_case:
        cases.append((current_value, _strip_break(current_lines)))

    return cases


def _strip_break(lines: list[bytes]) -> list[bytes]:
    """Remove trailing `break;` from case body lines."""
    if lines and lines[-1].strip() == b"break;":
        return lines[:-1]
    return lines


def _collect_if_chain(
    node: Node, source: bytes
) -> list[tuple[bytes | None, bytes | None, list[bytes]]] | None:
    """Collect if/else if chain into (var, const_value, body_lines) tuples.

    Returns None if conditions don't all compare the same variable to constants.
    The final `else` has (None, None, body_lines).
    """
    chain: list[tuple[bytes | None, bytes | None, list[bytes]]] = []
    switch_var: bytes | None = None
    current = node

    while current is not None and current.type == "if_statement":
        condition = current.child_by_field_name("condition")
        consequence = current.child_by_field_name("consequence")
        alternative = current.child_by_field_name("alternative")

        if condition is None or consequence is None:
            return None

        # Extract var == const from condition
        inner = _get_inner_expr(condition)
        if inner is None or inner.type != "binary_expression":
            return None

        op = inner.child_by_field_name("operator")
        if op is None or op.text != b"==":
            return None

        left = inner.child_by_field_name("left")
        right = inner.child_by_field_name("right")
        if left is None or right is None:
            return None

        var_text = source[left.start_byte:left.end_byte]
        val_text = source[right.start_byte:right.end_byte]

        if switch_var is None:
            switch_var = var_text
        elif var_text != switch_var:
            return None

        # Get body lines
        body_lines = _extract_body_lines(consequence, source)
        chain.append((var_text, val_text, body_lines))

        # Follow the else chain
        if alternative is None:
            break

        # Find the next if or else body
        current = None
        for child in alternative.children:
            if child.type == "if_statement":
                current = child
                break
            elif child.type == "compound_statement":
                # Final else
                body_lines = _extract_body_lines(child, source)
                chain.append((None, None, body_lines))
                break

    return chain if len(chain) >= 3 else None


def _extract_body_lines(node: Node, source: bytes) -> list[bytes]:
    """Extract statement lines from a compound_statement or single statement."""
    if node.type == "compound_statement":
        lines = []
        for child in node.named_children:
            if child.type != "comment":
                line = source[child.start_byte:child.end_byte].strip()
                if line:
                    lines.append(line)
        return lines
    else:
        line = source[node.start_byte:node.end_byte].strip()
        return [line] if line else []


def _get_inner_expr(condition: Node) -> Node | None:
    """Extract the inner expression from a condition_clause."""
    current = condition
    while current.type in ("condition_clause", "parenthesized_expression"):
        children = [c for c in current.named_children if c.type != "comment"]
        if len(children) == 1:
            current = children[0]
        else:
            break
    if current.id == condition.id:
        for child in condition.named_children:
            if child.type != "comment":
                return child
        return None
    return current
