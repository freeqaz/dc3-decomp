"""NOR peephole prevention — widen narrow types to u32 before XOR.

The MSVC PPC compiler converts `u8_value ^ all_ones_mask` to a bitwise NOT
(`nor` instruction) when the XOR mask covers all bits of the type. Widening
to u32 before the XOR prevents this optimization.

Example:
    u8 w = msg->Int(2);
    u32 tmp = (u8)(w >> 3) ^ 0x1F;
    ->
    u8 w = msg->Int(2);
    u32 w32 = w;
    u32 tmp = (w32 >> 3) ^ 0x1F;

This is opt-in since it only applies to narrow-type bitwise XOR code.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent, walk
from ..types import Diagnosis, FunctionContext, Variant


class NorPreventionPattern(Pattern):
    name = "nor_prevention"
    opt_in = True

    _NOR_OPS = {"nor", "nor."}
    _XOR_OPS = {"xor", "xori", "xoris", "xor."}

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in self._NOR_OPS or d.base_opcode in self._NOR_OPS:
                return True
            if d.target_opcode in self._XOR_OPS or d.base_opcode in self._XOR_OPS:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        for d in diagnosis.diff_ops:
            if d.target_opcode in self._NOR_OPS or d.base_opcode in self._NOR_OPS:
                return 0.6
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            for variant in _generate_widenings(stmt, ctx, counter):
                yield variant
                counter += 1


def _generate_widenings(
    stmt: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate u32 widening variants for narrow-type XOR expressions."""
    source = ctx.file_source

    for node in walk(stmt):
        # Look for XOR binary expressions
        if node.type != "binary_expression":
            continue

        op_node = node.child_by_field_name("operator")
        if op_node is None or op_node.text != b"^":
            continue

        left = node.child_by_field_name("left")
        if left is None:
            continue

        # Check if left side has a narrow-type cast (u8, unsigned char)
        cast_node = _find_narrow_cast(left, source)
        if cast_node is None:
            continue

        # Find the containing statement
        containing_stmt = _find_containing_statement(node, ctx)
        if containing_stmt is None:
            continue

        # Find the innermost identifier being cast — that's what we widen
        inner_var = _find_inner_var(cast_node, source)
        if inner_var is None:
            continue

        inner_var_text = source[inner_var.start_byte:inner_var.end_byte]
        indent = get_indent(source, containing_stmt)

        # Remove the narrow cast and insert a u32 widening before the statement
        cast_text = source[cast_node.start_byte:cast_node.end_byte]
        # Get the inner expression (what's being cast)
        inner_expr = _get_cast_inner(cast_node)
        if inner_expr is None:
            continue

        inner_expr_text = source[inner_expr.start_byte:inner_expr.end_byte]

        stmt_source = source[containing_stmt.start_byte:containing_stmt.end_byte]
        # Replace the cast expression with just the inner expression using the widened var
        new_stmt = stmt_source.replace(cast_text, inner_expr_text.replace(inner_var_text, b"_w32"))

        new_source = (
            source[:containing_stmt.start_byte]
            + indent + b"u32 _w32 = " + inner_var_text + b";\n"
            + indent + new_stmt
            + source[containing_stmt.end_byte:]
        )
        yield Variant(
            name=f"norprev_{counter}",
            pattern_name="nor_prevention",
            description=f"Widen {inner_var_text.decode(errors='replace')} to u32 before XOR",
            source=new_source,
        )
        counter += 1


def _find_narrow_cast(node: Node, source: bytes) -> Node | None:
    """Find a narrow-type cast (u8, unsigned char) in a node tree."""
    if node.type == "cast_expression":
        type_node = node.child_by_field_name("type")
        if type_node:
            type_text = source[type_node.start_byte:type_node.end_byte].strip()
            if type_text in (b"u8", b"unsigned char", b"uint8_t", b"u16", b"unsigned short", b"uint16_t"):
                return node

    if node.type == "call_expression":
        func = node.child_by_field_name("function")
        if func:
            # Handle both bare `u8(...)` and `(u8)(...)` parsed as call
            func_text = func.text.strip(b"()")
            if func_text in (b"u8", b"u16"):
                return node

    if node.type == "parenthesized_expression":
        for child in node.named_children:
            result = _find_narrow_cast(child, source)
            if result:
                return result

    # Recurse into sub-expressions
    for child in node.named_children:
        result = _find_narrow_cast(child, source)
        if result:
            return result

    return None


def _find_inner_var(node: Node, source: bytes) -> Node | None:
    """Find the innermost identifier in a cast/call expression."""
    for child in walk(node):
        if child.type == "identifier" and child.text not in (b"u8", b"u16", b"u32"):
            return child
    return None


def _get_cast_inner(node: Node) -> Node | None:
    """Get the inner expression of a cast or u8() call."""
    if node.type == "cast_expression":
        return node.child_by_field_name("value")
    if node.type == "call_expression":
        args = node.child_by_field_name("arguments")
        if args:
            for child in args.named_children:
                return child
    return None


def _find_containing_statement(node: Node, ctx: FunctionContext) -> Node | None:
    """Find the top-level statement in the function body that contains this node."""
    for stmt in ctx.statements:
        if stmt.start_byte <= node.start_byte and node.end_byte <= stmt.end_byte:
            return stmt
    return None
