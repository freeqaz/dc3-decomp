"""Single return pattern — merge early returns into a result variable.

When a function has if (cond) { ...; return A; } return B; the compiler
generates different branch structures vs using a single return point
with a result variable.

Example:
    if (cond) {
        doSomething();
        return 1;
    }
    return 0;
    ->
    int result = 0;
    if (cond) {
        doSomething();
        result = 1;
    }
    return result;
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


class SingleReturnPattern(Pattern):
    name = "single_return"
    safety_tier = "moderate"
    structural_domain = "control_flow"
    follow_ups = ("branch_polarity", "early_return_merge")
    cross_unit_modes = ("inline_header",)

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        return bool(diagnosis.clusters)

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Control flow restructure — moderate priority with clusters
        if len(diagnosis.clusters) >= 2:
            return 0.5
        if diagnosis.clusters:
            return 0.3
        return 0.15

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        stmts = ctx.statements
        source = ctx.file_source

        # Look for pattern: if (...) { ...; return X; } return Y;
        for i in range(len(stmts) - 1):
            if_node = stmts[i]
            ret_node = stmts[i + 1]

            if if_node.type != "if_statement" or ret_node.type != "return_statement":
                continue

            # Get the return value from the trailing return
            trail_ret_val = _get_return_value(ret_node, source)
            if trail_ret_val is None:
                continue

            # Get the if consequence
            consequence = if_node.child_by_field_name("consequence")
            alternative = if_node.child_by_field_name("alternative")
            condition = if_node.child_by_field_name("condition")
            if consequence is None or condition is None:
                continue
            # Skip if there's an else — more complex
            if alternative is not None:
                continue

            # Find return statements inside the if body
            inner_returns = _find_returns_in(consequence)
            if not inner_returns:
                continue

            # For simplicity, handle the case where there's exactly one
            # return at the end of the if body
            if len(inner_returns) != 1:
                continue

            inner_ret = inner_returns[0]
            inner_ret_val = _get_return_value(inner_ret, source)
            if inner_ret_val is None:
                continue

            indent = get_indent(source, if_node)

            # Build: replace "return X;" inside if with "_result = X;"
            # and replace trailing "return Y;" with "return _result;"
            # and add "_result = Y;" before the if

            # Get the type from the trailing return value (guess int for simple literals)
            result_type = _guess_type(trail_ret_val)

            # Build the new if body: replace the inner return with assignment
            inner_body = source[consequence.start_byte:inner_ret.start_byte]
            inner_ret_indent = get_indent(source, inner_ret)
            inner_body += inner_ret_indent + b"_result = " + inner_ret_val.encode() + b";"
            # Skip past the inner return statement
            inner_body += source[inner_ret.end_byte:consequence.end_byte]

            cond_text = source[condition.start_byte:condition.end_byte]

            new_text = (
                result_type.encode() + b" _result = " + trail_ret_val.encode() + b";\n"
                + indent + b"if " + cond_text + b" "
                + inner_body + b"\n"
                + indent + b"return _result;"
            )

            new_source = (
                source[:if_node.start_byte]
                + new_text
                + source[ret_node.end_byte:]
            )

            yield Variant(
                name=f"singleret_{counter}",
                pattern_name="single_return",
                description="Merge early return into result variable",
                source=new_source,
            )
            counter += 1


def _get_return_value(ret_node: Node, source: bytes) -> str | None:
    """Extract the return value text from a return_statement."""
    for child in ret_node.named_children:
        if child.type != "comment":
            return source[child.start_byte:child.end_byte].decode("utf-8")
    return None


def _find_returns_in(node: Node) -> list[Node]:
    """Find all return_statement nodes within a compound_statement."""
    returns = []
    for child in walk(node):
        if child.type == "return_statement" and child != node:
            returns.append(child)
    return returns


def _guess_type(value: str) -> str:
    """Guess a simple C++ type from a literal value."""
    v = value.strip()
    if v in ("true", "false"):
        return "bool"
    if v.endswith("f") or "." in v:
        return "float"
    try:
        int(v)
        return "int"
    except ValueError:
        return "auto"
