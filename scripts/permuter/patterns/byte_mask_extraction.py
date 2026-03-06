"""Byte mask extraction pattern — break rlwimi recognition by extracting byte masks.

When a byte-mask expression (u8(x), (unsigned char)(x), (x & 0xFF)) appears
inside a bitwise expression (|, &, ^, <<), extracting it to a named local
variable breaks the compiler's rlwimi (rotate-left-word-immediate-then-mask-insert)
recognition, causing it to fall back to separate clrlwi + slwi + or instructions.

Example:
    unsigned long ret = u8(w) | ((w << 8) & 0xFF00);
    ->
    unsigned long bw = u8(w);
    unsigned long ret = bw | (bw << 8);

This is opt-in since it only applies to bitwise byte manipulation code.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent, walk
from ..types import Diagnosis, FunctionContext, Variant


class ByteMaskExtractionPattern(Pattern):
    name = "byte_mask_extraction"
    opt_in = True

    # rlwimi pseudo-ops and related rotate-mask instructions
    _ROTATE_OPS = {"rlwimi", "rlwinm", "clrlwi", "clrrwi", "clrlslwi",
                   "extrwi", "slwi", "srwi", "rotlwi", "rotrwi", "inslwi",
                   "insrwi"}

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in self._ROTATE_OPS or d.base_opcode in self._ROTATE_OPS:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        for d in diagnosis.diff_ops:
            if d.target_opcode == "rlwimi" or d.base_opcode == "rlwimi":
                return 0.7
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            for variant in _generate_extractions(stmt, ctx, counter):
                yield variant
                counter += 1


def _generate_extractions(
    stmt: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate byte mask extraction variants."""
    source = ctx.file_source

    for node in walk(stmt):
        # Look for binary expressions with bitwise operators
        if node.type != "binary_expression":
            continue

        op_node = node.child_by_field_name("operator")
        if op_node is None:
            continue

        op_text = op_node.text.decode()
        if op_text not in {"|", "&", "^"}:
            continue

        left = node.child_by_field_name("left")
        right = node.child_by_field_name("right")
        if left is None or right is None:
            continue

        # Check each operand for byte-mask patterns
        for operand in (left, right):
            mask_expr = _find_byte_mask(operand, source)
            if mask_expr is None:
                continue

            # Find the containing statement to insert before
            containing_stmt = _find_containing_statement(node, ctx)
            if containing_stmt is None:
                continue

            mask_text = source[mask_expr.start_byte:mask_expr.end_byte]
            indent = get_indent(source, containing_stmt)

            # Replace the mask expression with a local variable and also
            # replace other occurrences of the same expression in this statement
            stmt_source = source[containing_stmt.start_byte:containing_stmt.end_byte]
            new_stmt = stmt_source.replace(mask_text, b"_bm")

            new_source = (
                source[:containing_stmt.start_byte]
                + indent + b"unsigned long _bm = " + mask_text + b";\n"
                + indent + new_stmt
                + source[containing_stmt.end_byte:]
            )
            yield Variant(
                name=f"bytemask_{counter}",
                pattern_name="byte_mask_extraction",
                description=f"Extract byte mask to local: {mask_text.decode(errors='replace')[:40]}",
                source=new_source,
            )
            counter += 1
            # Only yield one variant per bitwise expression
            break


def _find_byte_mask(node: Node, source: bytes) -> Node | None:
    """Check if node is a byte-mask expression. Returns the node if so."""
    # u8(x) — call to u8 macro/function
    if node.type == "call_expression":
        func = node.child_by_field_name("function")
        if func and func.text == b"u8":
            return node

    # (unsigned char)(x) — cast expression
    if node.type == "cast_expression":
        type_node = node.child_by_field_name("type")
        if type_node:
            type_text = source[type_node.start_byte:type_node.end_byte].strip()
            if type_text in (b"unsigned char", b"uint8_t"):
                return node

    # (x & 0xFF) — binary expression with & 0xFF
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op and op.text == b"&":
            right = node.child_by_field_name("right")
            if right and right.type == "number_literal":
                val = right.text.decode().lower()
                if val in ("0xff", "255"):
                    return node

    # parenthesized_expression wrapping any of the above
    if node.type == "parenthesized_expression":
        for child in node.named_children:
            result = _find_byte_mask(child, source)
            if result:
                return result

    return None


def _find_containing_statement(node: Node, ctx: FunctionContext) -> Node | None:
    """Find the top-level statement in the function body that contains this node."""
    for stmt in ctx.statements:
        if stmt.start_byte <= node.start_byte and node.end_byte <= stmt.end_byte:
            return stmt
    return None
