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
from ..control_flow import else_compound_body, noncomment_named_children
from ..types import Diagnosis, FunctionContext, Variant

_BRANCH_OPCODES = {"beq", "bne", "ble", "bgt", "bge", "blt",
                   "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
                   "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-"}


class SwitchIfConvertPattern(Pattern):
    name = "switch_if_convert"
    safety_tier = "moderate"
    structural_domain = "control_flow"
    follow_ups = ("branch_polarity", "declaration_reorder", "ternary_swap")
    cross_unit_modes = ("inline_header",)

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
        tags=frozenset({"converted_switch_to_if"}),
    )


def _if_to_switch(node: Node, ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Convert if/else if chain to switch statement."""
    if node.type != "if_statement":
        for child in node.children:
            yield from _if_to_switch(child, ctx, counter)
        return

    source = ctx.file_source

    # Collect the if/else if chain
    collected = _collect_if_chain(node, source)
    if collected is None:
        return

    switch_var, chain = collected

    indent = get_indent(source, node)
    inner_indent = indent + b"    "
    nl = b"\n"

    parts = [indent + b"switch (" + switch_var + b") {" + nl]

    for val, body_lines in chain:
        if val is None:
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
        tags=frozenset({"converted_if_to_switch"}),
    )


def _extract_switch_cases(body: Node, source: bytes) -> list[tuple[bytes | None, list[bytes]]]:
    """Extract (case_value_or_None_for_default, body_lines) from switch body.

    Returns list of (value, lines) tuples. Strips `break;` from end of each case.
    """
    cases: list[tuple[bytes | None, list[bytes]]] = []
    current_value: bytes | None = None
    current_lines: list[bytes] = []
    in_case = False

    for child in noncomment_named_children(body):
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
            for stmt_child in noncomment_named_children(child):
                if stmt_child != value_node:
                    line = source[stmt_child.start_byte:stmt_child.end_byte].strip()
                    if line:
                        current_lines.append(line)
            in_case = True

        elif child.type == "default_statement":
            if in_case:
                cases.append((current_value, _strip_break(current_lines)))
            current_value = None
            current_lines = []
            for stmt_child in noncomment_named_children(child):
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
) -> tuple[bytes, list[tuple[bytes | None, list[bytes]]]] | None:
    """Collect if/else if chain into switch expression + (case_value, body_lines).

    Returns None if conditions don't all compare the same expression to constants.
    The final `else` has (None, body_lines).
    """
    chain: list[tuple[bytes | None, list[bytes]]] = []
    switch_expr: bytes | None = None
    switch_key: bytes | None = None
    handled_int_values: set[int] = set()
    handled_case_values: set[bytes] = set()
    current = node

    while current is not None and current.type == "if_statement":
        condition = current.child_by_field_name("condition")
        consequence = current.child_by_field_name("consequence")
        alternative = current.child_by_field_name("alternative")

        if condition is None or consequence is None:
            return None

        parsed = _parse_chain_condition(
            condition, source, switch_key, handled_int_values
        )
        if parsed is None:
            return None

        branch_expr, branch_key, case_value, case_int = parsed
        if case_value in handled_case_values:
            return None

        if switch_expr is None:
            switch_expr = branch_expr
            switch_key = branch_key
        elif branch_key != switch_key:
            return None

        # Get body lines
        body_lines = _extract_body_lines(consequence, source)
        chain.append((case_value, body_lines))
        handled_case_values.add(case_value)
        if case_int is not None:
            handled_int_values.add(case_int)

        # Follow the else chain
        if alternative is None:
            break

        # Find the next if or else body
        current = None
        for child in alternative.children:
            if child.type == "if_statement":
                current = child
                break
        if current is None:
            alt_body = else_compound_body(alternative)
            if alt_body is not None:
                body_lines = _extract_body_lines(alt_body, source)
                chain.append((None, body_lines))

    if switch_expr is None or len(chain) < 3:
        return None
    return switch_expr, chain


def _parse_chain_condition(
    condition: Node,
    source: bytes,
    expected_key: bytes | None,
    handled_int_values: set[int],
) -> tuple[bytes, bytes, bytes, int | None] | None:
    """Parse a chain condition into switch expr, canonical key, and case value."""
    inner = _get_inner_expr(condition)
    if inner is None or inner.type != "binary_expression":
        return None

    op = inner.child_by_field_name("operator")
    left = inner.child_by_field_name("left")
    right = inner.child_by_field_name("right")
    if op is None or left is None or right is None:
        return None

    var_node, const_node = _select_var_and_const(left, right, source, expected_key)
    if var_node is None or const_node is None or _has_side_effects(var_node):
        return None

    switch_expr = source[var_node.start_byte:var_node.end_byte]
    switch_key = _expr_key(var_node, source)
    if switch_key is None:
        return None

    if op.text == b"==":
        case_value = source[const_node.start_byte:const_node.end_byte]
        return switch_expr, switch_key, case_value, _parse_int_constant(const_node)

    case_int = _infer_case_value(
        op.text, var_node.id == left.id, const_node, handled_int_values
    )
    if case_int is None:
        return None

    return switch_expr, switch_key, str(case_int).encode(), case_int


def _select_var_and_const(
    left: Node,
    right: Node,
    source: bytes,
    expected_key: bytes | None,
) -> tuple[Node | None, Node | None]:
    """Choose the variable side and case-value side of a comparison."""
    left_key = _expr_key(left, source)
    right_key = _expr_key(right, source)

    if expected_key is not None:
        if left_key == expected_key:
            return left, right
        if right_key == expected_key:
            return right, left
        return None, None

    left_const = _looks_like_constant(left)
    right_const = _looks_like_constant(right)
    if left_const and not right_const:
        return right, left
    if right_const and not left_const:
        return left, right
    return left, right


def _looks_like_constant(node: Node) -> bool:
    """Heuristic for choosing const-vs-var on the first branch."""
    inner = _unwrap_expr(node)
    if inner.type in {
        "number_literal",
        "char_literal",
        "true",
        "false",
        "null",
        "nullptr",
        "qualified_identifier",
        "scoped_identifier",
    }:
        return True
    if inner.type == "identifier":
        return False
    return _parse_int_constant(inner) is not None


def _expr_key(node: Node, source: bytes) -> bytes | None:
    """Canonicalize casts/parens so mixed `(unsigned)i` / `i` chains still match."""
    inner = _unwrap_expr(node)
    return source[inner.start_byte:inner.end_byte] if inner is not None else None


def _unwrap_expr(node: Node) -> Node:
    """Strip wrappers that do not change which value is being tested."""
    current = node
    while True:
        if current.type == "parenthesized_expression":
            children = [c for c in current.named_children if c.type != "comment"]
            if len(children) == 1:
                current = children[0]
                continue
        if current.type in ("cast_expression", "c_style_cast_expression"):
            value = current.child_by_field_name("value")
            if value is not None:
                current = value
                continue
        return current


def _has_side_effects(node: Node) -> bool:
    """Reject expressions that would change semantics if moved to switch()."""
    inner = _unwrap_expr(node)
    if inner.type in {
        "assignment_expression",
        "update_expression",
        "call_expression",
        "new_expression",
        "delete_expression",
        "comma_expression",
    }:
        return True
    return any(_has_side_effects(child) for child in inner.named_children)


def _parse_int_constant(node: Node) -> int | None:
    """Parse integer literals used for conservative inequality inference."""
    inner = _unwrap_expr(node)
    if inner.type == "number_literal":
        try:
            return int(inner.text.decode("ascii"), 0)
        except ValueError:
            return None

    if inner.type == "char_literal":
        text = inner.text.decode("ascii", errors="ignore")
        if len(text) == 3 and text.startswith("'") and text.endswith("'"):
            return ord(text[1])
        return None

    if inner.type == "unary_expression":
        op = inner.child_by_field_name("operator")
        arg = inner.child_by_field_name("argument")
        if op is not None and op.text == b"-" and arg is not None:
            value = _parse_int_constant(arg)
            return -value if value is not None else None

    return None


def _infer_case_value(
    op_text: bytes,
    var_is_left: bool,
    const_node: Node,
    handled_int_values: set[int],
) -> int | None:
    """Infer the one remaining dense case from a bounded inequality branch."""
    bound = _parse_int_constant(const_node)
    if bound is None:
        return None

    if op_text == b"<" and var_is_left:
        return _infer_missing_case_below(bound, handled_int_values)
    if op_text == b"<=" and var_is_left:
        return _infer_missing_case_below(bound + 1, handled_int_values)
    if op_text == b">" and not var_is_left:
        return _infer_missing_case_below(bound, handled_int_values)
    if op_text == b">=" and not var_is_left:
        return _infer_missing_case_below(bound + 1, handled_int_values)
    return None


def _infer_missing_case_below(limit: int, handled_int_values: set[int]) -> int | None:
    """Infer one missing case from a dense prefix [0, limit)."""
    if limit <= 0 or 0 not in handled_int_values:
        return None

    candidates = [value for value in range(limit) if value not in handled_int_values]
    if len(candidates) == 1:
        return candidates[0]
    return None


def _extract_body_lines(node: Node, source: bytes) -> list[bytes]:
    """Extract statement lines from a compound_statement or single statement."""
    if node.type == "compound_statement":
        lines = []
        for child in noncomment_named_children(node):
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
        children = noncomment_named_children(current)
        if len(children) == 1:
            current = children[0]
        else:
            break
    if current.id == condition.id:
        children = noncomment_named_children(condition)
        for child in children:
            return child
        return None
    return current
