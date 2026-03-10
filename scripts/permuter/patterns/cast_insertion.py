"""Cast insertion pattern — add/remove/swap casts guided by Ghidra decompilation.

Compares cast expressions in Ghidra output against source to find
missing or mismatched casts that affect signed/unsigned comparison
instruction selection (cmpw vs cmplw, cmpwi vs cmplwi).

Requires --ghidra mode (needs Ghidra AST data).

Example:
    // Ghidra shows: if ((uint)x < 10)
    // Source has:   if (x < 10)
    // Pattern adds: if ((unsigned int)x < 10)
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import find_comparisons
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

_RELEVANT_OPCODES = {
    "cmpw", "cmpwi", "cmplw", "cmplwi",
    "cmpd", "cmpdi", "cmpld", "cmpldi",
}

# Ghidra C type names → C++ cast equivalents
_GHIDRA_TO_CPP_CAST: dict[str, bytes] = {
    "uint": b"(unsigned int)",
    "unsigned int": b"(unsigned int)",
    "int": b"(int)",
    "long": b"(long)",
    "ulong": b"(unsigned long)",
    "unsigned long": b"(unsigned long)",
    "short": b"(short)",
    "ushort": b"(unsigned short)",
    "unsigned short": b"(unsigned short)",
    "char": b"(char)",
    "uchar": b"(unsigned char)",
    "unsigned char": b"(unsigned char)",
    "byte": b"(unsigned char)",
    "bool": b"(bool)",
}

# Match Ghidra-style cast: (type)expr or (type *)expr
_GHIDRA_CAST_RE = re.compile(
    r"\((\w[\w\s\*]*?)\)\s*(\w+)"
)


class CastInsertionPattern(Pattern):
    name = "cast_insertion"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if (d.target_opcode in _RELEVANT_OPCODES
                    or d.base_opcode in _RELEVANT_OPCODES):
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        score = 0.0
        for d in diagnosis.diff_ops:
            pair = {d.target_opcode, d.base_opcode}
            # Direct signed/unsigned comparison mismatch
            if pair & {"cmpw", "cmplw"} == {"cmpw", "cmplw"}:
                score += 0.3
            elif pair & {"cmpwi", "cmplwi"} == {"cmpwi", "cmplwi"}:
                score += 0.3
            # Same comparison, different operand — weaker
            elif pair & _RELEVANT_OPCODES:
                score += 0.1
        return min(score, 0.8)

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        if ctx.ghidra_ast is None:
            return

        ghidra_ast = ctx.ghidra_ast
        if not hasattr(ghidra_ast, "body_node") or ghidra_ast.body_node is None:
            return

        ghidra_code = ghidra_ast.code
        ghidra_casts = _extract_ghidra_comparison_casts(ghidra_code)
        if not ghidra_casts:
            return

        # Find source comparisons
        source_cmps = []
        for stmt in ctx.statements:
            for cmp_node in find_comparisons(stmt):
                source_cmps.append(cmp_node)

        counter = 0

        # Strategy 1: For each Ghidra cast in a comparison context,
        # try adding it to the corresponding source comparison operand
        for gc in ghidra_casts:
            cast_type = gc["cast_type"]
            cpp_cast = _GHIDRA_TO_CPP_CAST.get(cast_type.strip())
            if cpp_cast is None:
                continue

            # Try applying to each source comparison's left and right operands
            for cmp_node in source_cmps:
                left = cmp_node.child_by_field_name("left")
                right = cmp_node.child_by_field_name("right")
                if left is None or right is None:
                    continue

                # Try adding cast to left operand (if not already cast)
                if not _is_cast_expression(left):
                    ed = SourceEditor(ctx.file_source)
                    ed.insert_before(left, cpp_cast)
                    try:
                        new_source = ed.apply()
                    except ValueError:
                        continue
                    yield Variant(
                        name=f"castins_{counter}",
                        pattern_name=self.name,
                        description=(
                            f"Add {cpp_cast.decode()} cast to left of comparison "
                            f"(from Ghidra {cast_type})"
                        ),
                        source=new_source,
                    )
                    counter += 1

                # Try adding cast to right operand (if not already cast)
                if not _is_cast_expression(right):
                    ed = SourceEditor(ctx.file_source)
                    ed.insert_before(right, cpp_cast)
                    try:
                        new_source = ed.apply()
                    except ValueError:
                        continue
                    yield Variant(
                        name=f"castins_{counter}",
                        pattern_name=self.name,
                        description=(
                            f"Add {cpp_cast.decode()} cast to right of comparison "
                            f"(from Ghidra {cast_type})"
                        ),
                        source=new_source,
                    )
                    counter += 1

        # Strategy 2: Remove existing casts in source that Ghidra doesn't have
        for cmp_node in source_cmps:
            for operand, side in [
                (cmp_node.child_by_field_name("left"), "left"),
                (cmp_node.child_by_field_name("right"), "right"),
            ]:
                if operand is None:
                    continue
                if _is_cast_expression(operand):
                    # Remove the cast — extract the inner value
                    value = operand.child_by_field_name("value")
                    if value is None:
                        continue
                    value_text = ctx.file_source[
                        value.start_byte : value.end_byte
                    ]
                    ed = SourceEditor(ctx.file_source)
                    ed.replace_node(operand, value_text)
                    try:
                        new_source = ed.apply()
                    except ValueError:
                        continue
                    yield Variant(
                        name=f"castins_{counter}",
                        pattern_name=self.name,
                        description=f"Remove cast from {side} of comparison",
                        source=new_source,
                    )
                    counter += 1

        # Strategy 3: Swap existing casts (int <-> unsigned int)
        for cmp_node in source_cmps:
            for operand, side in [
                (cmp_node.child_by_field_name("left"), "left"),
                (cmp_node.child_by_field_name("right"), "right"),
            ]:
                if operand is None or not _is_cast_expression(operand):
                    continue
                type_node = operand.child_by_field_name("type")
                if type_node is None:
                    continue
                type_text = ctx.file_source[
                    type_node.start_byte : type_node.end_byte
                ].decode("utf-8", errors="replace").strip()

                swaps = _get_cast_swaps(type_text)
                for swap_cast in swaps:
                    # Rebuild the cast expression with the swapped type
                    value = operand.child_by_field_name("value")
                    if value is None:
                        continue
                    value_text = ctx.file_source[
                        value.start_byte : value.end_byte
                    ]
                    new_expr = b"(" + swap_cast + b")" + value_text
                    ed = SourceEditor(ctx.file_source)
                    ed.replace_node(operand, new_expr)
                    try:
                        new_source = ed.apply()
                    except ValueError:
                        continue
                    yield Variant(
                        name=f"castins_{counter}",
                        pattern_name=self.name,
                        description=(
                            f"Swap {side} cast from ({type_text}) to "
                            f"({swap_cast.decode()})"
                        ),
                        source=new_source,
                    )
                    counter += 1


def _extract_ghidra_comparison_casts(ghidra_code: str) -> list[dict]:
    """Extract cast types used in comparison contexts from Ghidra output.

    Returns list of dicts with 'cast_type' and 'var_name' keys.
    """
    results = []
    # Find comparisons in Ghidra code and look for casts in their operands
    # Simple heuristic: find (type)var patterns near comparison operators
    for m in _GHIDRA_CAST_RE.finditer(ghidra_code):
        cast_type = m.group(1).strip()
        var_name = m.group(2)
        # Only include if it's a sign-changing cast type
        if cast_type in _GHIDRA_TO_CPP_CAST:
            results.append({
                "cast_type": cast_type,
                "var_name": var_name,
            })
    return results


def _is_cast_expression(node: Node) -> bool:
    """Check if a tree-sitter node is a cast_expression or c_style_cast."""
    return node.type in ("cast_expression", "c_style_cast_expression")


def _get_cast_swaps(type_text: str) -> list[bytes]:
    """Return alternative cast types for a given type."""
    swaps: list[bytes] = []
    t = type_text.strip()
    if t == "int":
        swaps.append(b"unsigned int")
    elif t in ("unsigned int", "unsigned"):
        swaps.append(b"int")
    elif t == "long":
        swaps.append(b"unsigned long")
    elif t in ("unsigned long",):
        swaps.append(b"long")
    elif t == "short":
        swaps.append(b"unsigned short")
    elif t in ("unsigned short",):
        swaps.append(b"short")
    elif t == "char":
        swaps.append(b"unsigned char")
    elif t in ("unsigned char",):
        swaps.append(b"char")
    return swaps
