"""Type width change — swap integer type widths based on Ghidra hints or heuristics.

Generalizes bool_to_uchar to handle all safe integer type narrowing/widening.

Ghidra variable prefixes reveal target types:
    cVar → char / unsigned char
    iVar → int / signed
    uVar → unsigned int
    fVar → float
    pVar → pointer

Cross-reference with source type to find mismatches:
    Source has `unsigned int x` but Ghidra shows `cVar3` → narrow to unsigned char
    Source has `int x` but Ghidra shows `uVar2` → widen to unsigned int

Heuristic fallback (no Ghidra): if diagnosis has comparison sign mismatches
AND body has integer locals, try common narrowings.

Detection signals:
    - cmpw / cmplw mismatches
    - rlwinm / clrlwi (masking ops suggest width mismatch)
    - Comparison sign mismatches (cmpwi vs cmplwi)
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

_CMP_OPCODES = {"cmpw", "cmplw", "cmpwi", "cmplwi"}
_MASK_OPCODES = {"rlwinm", "clrlwi", "clrrwi", "extrwi", "rlwinm."}

# Safe type transitions (source_type -> list of candidate replacements)
_TYPE_TRANSITIONS: dict[bytes, list[bytes]] = {
    b"int": [b"unsigned int", b"unsigned char", b"short", b"char"],
    b"unsigned int": [b"int", b"unsigned char", b"unsigned short"],
    b"short": [b"int", b"char"],
    b"unsigned short": [b"unsigned int", b"unsigned char"],
    b"bool": [b"unsigned char", b"int"],
    b"char": [b"int", b"unsigned char"],
    b"unsigned char": [b"int", b"unsigned int"],
}

# Map Ghidra prefix to preferred target types
_GHIDRA_PREFIX_TYPES: dict[str, list[bytes]] = {
    "c": [b"char", b"unsigned char"],
    "i": [b"int"],
    "u": [b"unsigned int", b"unsigned short"],
    "f": [b"float"],
    "p": [],  # pointer — not a simple type swap
}


class TypeWidthChangePattern(Pattern):
    name = "type_width_change"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _CMP_OPCODES or d.base_opcode in _CMP_OPCODES:
                return True
            if d.target_opcode in _MASK_OPCODES or d.base_opcode in _MASK_OPCODES:
                return True
        if diagnosis.replace_real > 0:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0

        has_cmp_mismatch = False
        has_mask = False
        for d in diagnosis.diff_ops:
            pair = {d.target_opcode, d.base_opcode}
            # Cross-mismatch between signed and unsigned compare
            if pair & {"cmpw", "cmplw"} and len(pair & {"cmpw", "cmplw"}) == 2:
                has_cmp_mismatch = True
            if pair & {"cmpwi", "cmplwi"} and len(pair & {"cmpwi", "cmplwi"}) == 2:
                has_cmp_mismatch = True
            if d.target_opcode in _MASK_OPCODES or d.base_opcode in _MASK_OPCODES:
                has_mask = True

        if has_cmp_mismatch and has_mask:
            return 0.6
        if has_cmp_mismatch:
            return 0.4
        return 0.2

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        ghidra_hints = _get_ghidra_hints(ctx)

        for decl_info in _find_typed_declarations(ctx.body_node, ctx):
            var_name = decl_info["name"]
            type_node = decl_info["type_node"]
            init_node = decl_info["init_node"]
            source_type = decl_info["source_type"]

            # Determine which types to try
            candidates = _get_candidates(source_type, var_name, ghidra_hints)
            if not candidates:
                continue

            for target_type in candidates:
                if counter >= 10:
                    return

                ed = SourceEditor(ctx.file_source)
                ed.replace_node(type_node, target_type)

                # Adjust initializers for bool->non-bool transitions
                if source_type == b"bool" and init_node is not None:
                    init_text = ctx.file_source[init_node.start_byte:init_node.end_byte].strip()
                    if init_text == b"true":
                        ed.replace_node(init_node, b"1")
                    elif init_text == b"false":
                        ed.replace_node(init_node, b"0")

                # Adjust assignments of true/false for bool->non-bool
                if source_type == b"bool":
                    for assign_info in _find_assignments(ctx.body_node, var_name, ctx):
                        rhs_node = assign_info["rhs_node"]
                        rhs_text = ctx.file_source[rhs_node.start_byte:rhs_node.end_byte].strip()
                        if rhs_text == b"true":
                            ed.replace_node(rhs_node, b"1")
                        elif rhs_text == b"false":
                            ed.replace_node(rhs_node, b"0")

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                target_str = target_type.decode("utf-8")
                yield Variant(
                    name=f"typewidth_{counter}",
                    pattern_name=self.name,
                    description=f"Change {source_type.decode()} {var_name} to {target_str}",
                    source=new_source,
                )
                counter += 1


def _get_ghidra_hints(ctx: FunctionContext) -> dict[str, str]:
    """Extract Ghidra type prefix hints from target_var_order.

    Returns {ghidra_var_name: type_prefix} for variables with known prefixes.
    """
    hints: dict[str, str] = {}
    if ctx.target_var_order:
        for var_info in ctx.target_var_order:
            if var_info.type_prefix:
                hints[var_info.name] = var_info.type_prefix
    return hints


def _get_candidates(
    source_type: bytes, var_name: str, ghidra_hints: dict[str, str]
) -> list[bytes]:
    """Get candidate replacement types for a variable.

    If Ghidra hints available and type prefix disagrees with source type,
    prioritize the Ghidra-suggested types.
    """
    base_candidates = _TYPE_TRANSITIONS.get(source_type, [])
    if not base_candidates:
        return []

    # Without Ghidra hints, use default narrowing order
    # Limit to most impactful: narrowing to unsigned char, widening to int/uint
    if not ghidra_hints:
        # Heuristic: try the two most common transitions
        return base_candidates[:2]

    # With Ghidra hints, we can't match by name (Ghidra uses iVar2, source uses count)
    # So we return all base candidates (Ghidra mode has higher priority anyway)
    return base_candidates


def _find_typed_declarations(
    body: Node, ctx: FunctionContext
) -> Iterator[dict]:
    """Find integer/bool variable declarations in the function body."""
    for node in walk(body):
        if node.type != "declaration":
            continue

        type_node = node.child_by_field_name("type")
        if type_node is None:
            continue
        type_text = ctx.file_source[type_node.start_byte:type_node.end_byte].strip()

        # Only handle types we know how to transform
        if type_text not in _TYPE_TRANSITIONS:
            continue

        declarator = node.child_by_field_name("declarator")
        if declarator is None:
            continue

        if declarator.type == "init_declarator":
            name_node = declarator.child_by_field_name("declarator")
            value_node = declarator.child_by_field_name("value")
            if name_node is None:
                continue
            var_name = ctx.file_source[
                name_node.start_byte:name_node.end_byte
            ].decode("utf-8")
            yield {
                "name": var_name,
                "type_node": type_node,
                "init_node": value_node,
                "source_type": type_text,
            }
        elif declarator.type == "identifier":
            var_name = ctx.file_source[
                declarator.start_byte:declarator.end_byte
            ].decode("utf-8")
            yield {
                "name": var_name,
                "type_node": type_node,
                "init_node": None,
                "source_type": type_text,
            }


def _find_assignments(
    body: Node, var_name: str, ctx: FunctionContext
) -> Iterator[dict]:
    """Find assignments to a variable in the body."""
    for node in walk(body):
        if node.type != "assignment_expression":
            continue

        left = node.child_by_field_name("left")
        right = node.child_by_field_name("right")
        if left is None or right is None:
            continue

        left_text = ctx.file_source[left.start_byte:left.end_byte].decode("utf-8").strip()
        if left_text != var_name:
            continue

        yield {"rhs_node": right}
