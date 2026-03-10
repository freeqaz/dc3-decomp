"""u8 to unsigned long — prevent rlwinm fusion by widening intermediate types.

The MSVC PPC compiler fuses shift+mask into rlwinm (extrwi/clrlslwi) when the
source type is u8, because c1xx.dll emits IL CAST opcode for u8 narrowing.
Using unsigned long intermediates + & 0xFF at the return point forces AND
opcode in IL instead, generating separate srwi/slwi that match the target.

See docs/plans/synthesis-engine/IL_TYPE_CONTROL.md for the full mechanism.

Proven on 20+ ByteGrinder functions (op7, op15-op53).

Generates two kinds of variants:
1. Type widening: u8 locals → unsigned long (preserves u8 on initializer)
2. Return masking: return u8(expr) → DataNode(kDataInt, (int)((expr) & 0xFF))
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..types import Diagnosis, FunctionContext, Variant


_FUSION_OPS = {"extrwi", "clrlslwi"}
_MASK_OPS = {"clrlwi"}
_NARROW_TYPES = {b"u8", b"unsigned char", b"uint8_t"}


class U8ToUnsignedLongPattern(Pattern):
    name = "u8_to_unsigned_long"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _FUSION_OPS or d.base_opcode in _FUSION_OPS:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        count = 0
        for d in diagnosis.diff_ops:
            if d.target_opcode in _FUSION_OPS or d.base_opcode in _FUSION_OPS:
                count += 1
        # High priority — this pattern has a very high hit rate
        return min(0.4 + count * 0.2, 1.0)

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0

        # Strategy 1: Widen all u8 locals to unsigned long
        variant = _widen_all_u8_locals(ctx, counter)
        if variant is not None:
            yield variant
            counter += 1

        # Strategy 2: Convert return u8(expr) to DataNode with & 0xFF
        for v in _convert_u8_returns(ctx, counter):
            yield v
            counter += 1

        # Strategy 3: Combined — widen locals AND convert returns
        variant = _combined_widen_and_mask(ctx, counter)
        if variant is not None:
            yield variant
            counter += 1


def _widen_all_u8_locals(ctx: FunctionContext, counter: int) -> Variant | None:
    """Replace all u8 local variable types with unsigned long."""
    source = ctx.file_source
    replacements: list[tuple[int, int, bytes]] = []

    for stmt in ctx.statements:
        for node in walk(stmt):
            if node.type != "declaration":
                continue
            # Check if declaration has u8 type
            type_node = _get_declaration_type(node)
            if type_node is None:
                continue
            type_text = source[type_node.start_byte:type_node.end_byte].strip()
            if type_text not in _NARROW_TYPES:
                continue

            # Get the initializer value
            init = _get_declaration_init(node, source)
            if init is None:
                continue

            # Build replacement: unsigned long var = u8(init_expr);
            var_name = _get_declaration_name(node, source)
            if var_name is None:
                continue

            init_text = source[init.start_byte:init.end_byte]

            # If init is already u8(something), keep u8() wrapping for input mask
            # Otherwise wrap in u8() to preserve the mask
            if _is_u8_call(init, source):
                # Already u8() wrapped — keep as-is
                new_init = init_text
            else:
                new_init = b"u8(" + init_text + b")"

            new_decl = b"unsigned long " + var_name + b" = " + new_init + b";"
            replacements.append((node.start_byte, node.end_byte, new_decl))

    if not replacements:
        return None

    # Apply replacements in reverse order
    new_source = bytearray(source)
    for start, end, replacement in reversed(sorted(replacements)):
        new_source[start:end] = replacement

    return Variant(
        name=f"u8widen_{counter}",
        pattern_name="u8_to_unsigned_long",
        description=f"Widen {len(replacements)} u8 local(s) to unsigned long",
        source=bytes(new_source),
    )


def _convert_u8_returns(ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Convert return u8(expr) to return DataNode(kDataInt, (int)((expr) & 0xFF))."""
    source = ctx.file_source

    for stmt in ctx.statements:
        for node in walk(stmt):
            if node.type != "return_statement":
                continue

            # Find the return value
            ret_expr = None
            for child in node.named_children:
                if child.type != "comment":
                    ret_expr = child
                    break
            if ret_expr is None:
                continue

            # Check if return value is u8(something)
            u8_call = _find_u8_call(ret_expr, source)
            if u8_call is None:
                continue

            # Get the inner expression of u8()
            inner = _get_u8_inner(u8_call, source)
            if inner is None:
                continue

            inner_text = source[inner.start_byte:inner.end_byte]

            # Check if u8() is inside a DataNode constructor
            if _is_inside_datanode(u8_call, source):
                # Replace u8(expr) with (int)((expr) & 0xFF) inside the DataNode call
                replacement = b"(int)((" + inner_text + b") & 0xFF)"
                new_source = (
                    source[:u8_call.start_byte]
                    + replacement
                    + source[u8_call.end_byte:]
                )
            elif ret_expr == u8_call:
                # Direct return u8(expr) — wrap in DataNode
                replacement = b"DataNode(kDataInt, (int)((" + inner_text + b") & 0xFF))"
                new_source = (
                    source[:u8_call.start_byte]
                    + replacement
                    + source[u8_call.end_byte:]
                )
            else:
                continue

            yield Variant(
                name=f"u8mask_{counter}",
                pattern_name="u8_to_unsigned_long",
                description="Convert u8() return to & 0xFF (prevent IL CAST backward propagation)",
                source=new_source,
            )
            counter += 1


def _combined_widen_and_mask(ctx: FunctionContext, counter: int) -> Variant | None:
    """Apply both widening and return masking together."""
    source = ctx.file_source
    replacements: list[tuple[int, int, bytes]] = []

    # Collect u8 local widenings
    for stmt in ctx.statements:
        for node in walk(stmt):
            if node.type != "declaration":
                continue
            type_node = _get_declaration_type(node)
            if type_node is None:
                continue
            type_text = source[type_node.start_byte:type_node.end_byte].strip()
            if type_text not in _NARROW_TYPES:
                continue
            init = _get_declaration_init(node, source)
            if init is None:
                continue
            var_name = _get_declaration_name(node, source)
            if var_name is None:
                continue
            init_text = source[init.start_byte:init.end_byte]
            if _is_u8_call(init, source):
                new_init = init_text
            else:
                new_init = b"u8(" + init_text + b")"
            new_decl = b"unsigned long " + var_name + b" = " + new_init + b";"
            replacements.append((node.start_byte, node.end_byte, new_decl))

    # Collect u8() return conversions
    for stmt in ctx.statements:
        for node in walk(stmt):
            if node.type != "return_statement":
                continue
            ret_expr = None
            for child in node.named_children:
                if child.type != "comment":
                    ret_expr = child
                    break
            if ret_expr is None:
                continue
            u8_call = _find_u8_call(ret_expr, source)
            if u8_call is None:
                continue
            inner = _get_u8_inner(u8_call, source)
            if inner is None:
                continue
            inner_text = source[inner.start_byte:inner.end_byte]
            if _is_inside_datanode(u8_call, source):
                replacement = b"(int)((" + inner_text + b") & 0xFF)"
                replacements.append((u8_call.start_byte, u8_call.end_byte, replacement))
            elif ret_expr == u8_call:
                replacement = b"DataNode(kDataInt, (int)((" + inner_text + b") & 0xFF))"
                replacements.append((u8_call.start_byte, u8_call.end_byte, replacement))

    if len(replacements) < 2:
        return None

    # Apply replacements in reverse order
    new_source = bytearray(source)
    for start, end, replacement in reversed(sorted(replacements)):
        new_source[start:end] = replacement

    return Variant(
        name=f"u8combo_{counter}",
        pattern_name="u8_to_unsigned_long",
        description=f"Widen u8 locals + convert returns to & 0xFF (combined)",
        source=bytes(new_source),
    )


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _get_declaration_type(node: Node) -> Node | None:
    """Get the type specifier node from a declaration."""
    for child in node.children:
        if child.type in ("type_identifier", "primitive_type", "sized_type_specifier"):
            return child
    return None


def _get_declaration_name(node: Node, source: bytes) -> bytes | None:
    """Get the variable name from a declaration."""
    decl = node.child_by_field_name("declarator")
    if decl is None:
        # Try init_declarator
        for child in node.named_children:
            if child.type == "init_declarator":
                name_node = child.child_by_field_name("declarator")
                if name_node:
                    return source[name_node.start_byte:name_node.end_byte]
        return None
    return source[decl.start_byte:decl.end_byte]


def _get_declaration_init(node: Node, source: bytes) -> Node | None:
    """Get the initializer expression from a declaration."""
    for child in node.named_children:
        if child.type == "init_declarator":
            return child.child_by_field_name("value")
    return None


def _is_u8_call(node: Node, source: bytes) -> bool:
    """Check if node is a u8() functional cast."""
    if node.type == "call_expression":
        func = node.child_by_field_name("function")
        if func and source[func.start_byte:func.end_byte].strip() in (b"u8", b"(u8)"):
            return True
    if node.type == "cast_expression":
        type_node = node.child_by_field_name("type")
        if type_node:
            type_text = source[type_node.start_byte:type_node.end_byte].strip()
            if type_text in _NARROW_TYPES:
                return True
    return False


def _find_u8_call(node: Node, source: bytes) -> Node | None:
    """Find a u8() call/cast within an expression tree."""
    if _is_u8_call(node, source):
        return node
    for child in node.named_children:
        result = _find_u8_call(child, source)
        if result:
            return result
    return None


def _get_u8_inner(node: Node, source: bytes) -> Node | None:
    """Get the inner expression of a u8() call or cast."""
    if node.type == "call_expression":
        args = node.child_by_field_name("arguments")
        if args:
            for child in args.named_children:
                return child
    if node.type == "cast_expression":
        return node.child_by_field_name("value")
    return None


def _is_inside_datanode(node: Node, source: bytes) -> bool:
    """Check if node is inside a DataNode() constructor call."""
    parent = node.parent
    while parent:
        if parent.type == "call_expression":
            func = parent.child_by_field_name("function")
            if func and source[func.start_byte:func.end_byte].strip() == b"DataNode":
                return True
        parent = parent.parent
    return False
