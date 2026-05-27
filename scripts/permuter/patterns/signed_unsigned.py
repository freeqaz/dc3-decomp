"""Signed/unsigned cast pattern — wrap comparison operands in casts.

Win rate: ~30% from attempt database.

Finds binary_expression nodes with comparison operators and generates variants
wrapping each operand in (int), (unsigned int), or (unsigned long) casts.
Also tries swapping != 0 <-> > 0 for unsigned comparisons.

Example:
    if (ptr != 0)
    ->
    if ((int)ptr != 0)
    if ((unsigned int)ptr != 0)
    if (ptr > 0)
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import find_comparisons
from .. import clang_types
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

_COMPARISON_OPS = {"==", "!=", "<", ">", "<=", ">="}
_CAST_TYPES = [b"(int)", b"(unsigned int)", b"(unsigned long)"]
_NULL_LITERALS = {"nullptr", "NULL"}


_RELEVANT_OPCODES = {"cmpw", "cmpwi", "cmplw", "cmplwi", "cmpd", "cmpdi", "cmpld", "cmpldi",
                     "beq", "bne", "ble", "bge", "blt", "bgt"}


class SignedUnsignedPattern(Pattern):
    name = "signed_unsigned"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _RELEVANT_OPCODES or d.base_opcode in _RELEVANT_OPCODES:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        score = 0.0
        for d in diagnosis.diff_ops:
            # Direct signed/unsigned comparison mismatch — strong signal
            pair = {d.target_opcode, d.base_opcode}
            if pair & {"cmpw", "cmplw"} == {"cmpw", "cmplw"}:
                score += 0.4
            elif pair & {"cmpwi", "cmplwi"} == {"cmpwi", "cmplwi"}:
                score += 0.4
            # beq/bne near comparison ops — weaker signal
            elif d.target_opcode in {"beq", "bne"} or d.base_opcode in {"beq", "bne"}:
                score += 0.1
        return min(score, 1.0)

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            # Region filter: skip statements outside mismatch regions
            if not ctx.node_in_mismatch_region(stmt):
                continue
            for cmp_node in find_comparisons(stmt):
                left = cmp_node.child_by_field_name("left")
                right = cmp_node.child_by_field_name("right")
                op_node = cmp_node.child_by_field_name("operator")
                if left is None or right is None:
                    continue

                op_text = ctx.source_text(op_node) if op_node else None

                # Type-guided filtering: use libclang if available
                left_type = _resolve_operand_type(left, ctx)
                right_type = _resolve_operand_type(right, ctx)

                if left_type is not None or right_type is not None:
                    # libclang available — use precise type info
                    either_pointer = (
                        (left_type is not None and left_type.is_pointer)
                        or (right_type is not None and right_type.is_pointer)
                    )
                    if not either_pointer:
                        casts = _type_guided_casts(left_type, right_type)
                        for cast, side, operand in _cast_candidates(
                            casts, left, right
                        ):
                            ed = SourceEditor(ctx.file_source)
                            ed.insert_before(operand, cast)
                            new_source = ed.apply()
                            cast_str = cast.decode()
                            yield Variant(
                                name=f"signunsign_{counter}",
                                pattern_name=self.name,
                                description=f"Cast {side} of '{op_text}' to {cast_str}",
                                source=new_source,
                            )
                            counter += 1
                elif not _is_likely_pointer(left, right, ctx):
                    # Fallback: heuristic-based (no libclang)
                    # Cast left operand
                    for cast in _CAST_TYPES:
                        ed = SourceEditor(ctx.file_source)
                        ed.insert_before(left, cast)
                        new_source = ed.apply()
                        cast_str = cast.decode()
                        yield Variant(
                            name=f"signunsign_{counter}",
                            pattern_name=self.name,
                            description=f"Cast left of '{op_text}' to {cast_str}",
                            source=new_source,
                        )
                        counter += 1

                    # Cast right operand
                    for cast in _CAST_TYPES:
                        ed = SourceEditor(ctx.file_source)
                        ed.insert_before(right, cast)
                        new_source = ed.apply()
                        cast_str = cast.decode()
                        yield Variant(
                            name=f"signunsign_{counter}",
                            pattern_name=self.name,
                            description=f"Cast right of '{op_text}' to {cast_str}",
                            source=new_source,
                        )
                        counter += 1

                # Double-cast for subscript/smart-pointer operands:
                # (unsigned int)(Type*)expr — extract through conversion operator then cast
                for operand, side in [(left, "left"), (right, "right")]:
                    if operand.type == "subscript_expression":
                        for outer_cast in [b"(unsigned int)(void*)", b"(int)(void*)"]:
                            ed = SourceEditor(ctx.file_source)
                            ed.insert_before(operand, outer_cast)
                            try:
                                new_source = ed.apply()
                            except ValueError:
                                continue
                            cast_str = outer_cast.decode()
                            yield Variant(
                                name=f"signunsign_{counter}",
                                pattern_name=self.name,
                                description=f"Double-cast {side} subscript: {cast_str}",
                                source=new_source,
                            )
                            counter += 1

                # Swap != 0 <-> > 0 (always worth trying, 0 is ambiguous)
                right_text = ctx.file_source[
                    right.start_byte : right.end_byte
                ]
                if right_text.strip() == b"0" and op_text in ("!=", ">"):
                    new_op = b">" if op_text == "!=" else b"!="
                    # Replace the operator, preserving surrounding whitespace
                    if op_node is not None:
                        ed = SourceEditor(ctx.file_source)
                        ed.replace_node(op_node, new_op)
                        new_source = ed.apply()
                        swap_desc = (
                            "!= 0 -> > 0" if op_text == "!=" else "> 0 -> != 0"
                        )
                        yield Variant(
                            name=f"signunsign_{counter}",
                            pattern_name=self.name,
                            description=f"Swap comparison: {swap_desc}",
                            source=new_source,
                        )
                        counter += 1


def _resolve_operand_type(node: Node, ctx: FunctionContext):
    """Try to resolve the type of a comparison operand via libclang.

    Returns a clang_types.TypeInfo or None if unavailable.
    """
    if not clang_types.is_available():
        return None
    return clang_types.resolve_type_at(
        ctx.file_path, node.start_byte, ctx.file_source
    )


def _type_guided_casts(left_type, right_type):
    """Return targeted cast bytes based on resolved types.

    Returns dict mapping side ("left"/"right") to list of cast bytes to try.
    """
    casts: dict[str, list[bytes]] = {"left": [], "right": []}

    if left_type is not None:
        if left_type.is_signed_int:
            casts["left"] = [b"(unsigned int)", b"(unsigned long)"]
        elif left_type.is_unsigned_int:
            casts["left"] = [b"(int)"]
        elif left_type.kind == clang_types.TypeKind.BOOL:
            casts["left"] = [b"(int)", b"(unsigned int)"]
        elif left_type.kind == clang_types.TypeKind.ENUM:
            casts["left"] = [b"(int)", b"(unsigned int)"]
        else:
            # Float, record, other — try all
            casts["left"] = list(_CAST_TYPES)

    if right_type is not None:
        if right_type.is_signed_int:
            casts["right"] = [b"(unsigned int)", b"(unsigned long)"]
        elif right_type.is_unsigned_int:
            casts["right"] = [b"(int)"]
        elif right_type.kind == clang_types.TypeKind.BOOL:
            casts["right"] = [b"(int)", b"(unsigned int)"]
        elif right_type.kind == clang_types.TypeKind.ENUM:
            casts["right"] = [b"(int)", b"(unsigned int)"]
        else:
            casts["right"] = list(_CAST_TYPES)

    # If one side couldn't be resolved, still try all casts for it
    if left_type is None:
        casts["left"] = list(_CAST_TYPES)
    if right_type is None:
        casts["right"] = list(_CAST_TYPES)

    return casts


def _cast_candidates(casts, left, right):
    """Yield (cast_bytes, side_str, operand_node) tuples."""
    for cast in casts.get("left", []):
        yield cast, "left", left
    for cast in casts.get("right", []):
        yield cast, "right", right


def _ident_is_pointer_like(ident: str, ctx: FunctionContext) -> bool:
    """Heuristic: True when *ident* is declared or used as a pointer in the TU.

    Used by the no-libclang fallback to avoid emitting ``(int)<ptr>`` casts,
    which are hard compile errors. Two signals: arrow usage (``ident->``) and a
    pointer declaration (``Type *ident`` / ``Type* ident``). False positives
    (e.g. matching a multiplication ``a * ident``) only cost a skipped variant,
    so the bias toward detecting pointers is intentional.
    """
    src = ctx.file_source.decode("utf-8", errors="replace")
    esc = re.escape(ident)
    # Used as a pointer: ident->member
    if re.search(rf"\b{esc}\s*->", src):
        return True
    # Declared as a pointer: `Type *ident` or `Type* ident`
    if re.search(rf"[\w>\)]\s*\*\s*{esc}\b", src):
        return True
    return False


def _is_likely_pointer(left: Node, right: Node, ctx: FunctionContext) -> bool:
    """Heuristic: return True if this comparison likely involves pointers.

    Casting pointers to (int) causes build failures, so we skip those.
    """
    for operand in (left, right):
        # address-of expression: &foo
        if operand.type == "unary_expression":
            op = operand.child_by_field_name("operator")
            if op and op.text == b"&":
                return True

        # nullptr or NULL literal
        text = ctx.source_text(operand)
        if text in _NULL_LITERALS:
            return True

        # Call expression (likely returns pointer)
        if operand.type == "call_expression":
            return True

        # Arrow/dot member access: obj->field, obj.field (likely pointer context)
        if operand.type == "field_expression":
            return True

        # Pointer dereference: *ptr
        if operand.type == "pointer_expression":
            return True

        # Plain identifier that is declared or used as a pointer elsewhere
        # (e.g. `CharClip *drivclip` or `drivclip->Foo()`). Casting it to (int)
        # is a hard compile error — the single biggest signed_unsigned build
        # failure once varext was fixed. Erring toward skipping is safe here:
        # we only forgo a cast variant, never emit a wrong match.
        if operand.type == "identifier":
            ident = ctx.source_text(operand)
            if ident and _ident_is_pointer_like(ident, ctx):
                return True

    # Check if whole comparison is X != nullptr / X == NULL pattern
    left_text = ctx.source_text(left)
    right_text = ctx.source_text(right)
    if left_text in _NULL_LITERALS or right_text in _NULL_LITERALS:
        return True

    return False
