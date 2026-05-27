"""Signed/unsigned cast polarity pattern — target branch polarity flips.

Addresses bge<->ble and blt<->bgt branch polarity flips that arise when MWCC
interprets a comparison operand as signed vs unsigned.  This is a focused
sibling of ``signed_unsigned``: it only fires when the diff shows a polarity-
flip pair and only generates casts that would actually flip the signedness of
the comparison.

The key difference from ``signed_unsigned``:
- ``signed_unsigned`` fires on any cmpw/cmplw/bge/blt mismatch.
- This pattern gates specifically on the *polarity-flip* pairs:
      target bge <-> base ble  (or reversed)
      target blt <-> base bgt  (or reversed)
  OR on a cmpw<->cmplw pair where the comparison feeds a bge/ble/blt/bgt.
- Only generates casts for ``<``/``<=``/``>``/``>=`` comparisons — never
  ``==``/``!=``, which don't have polarity.
- Skips pointer-like operands (same heuristic as ``signed_unsigned``).
- With libclang available, only emits casts that would actually flip
  signedness (signed->unsigned or unsigned->signed).

Win target: bge<->ble / blt<->bgt flips from signed loop counters cast to
unsigned, or unsigned size values compared against signed indices.

Example transforms:
    if (a > b)            ->  if ((unsigned int)a > b)
    if (a < some_size)    ->  if (a < (int)some_size)
    for (int i = 0; i < n; i++)  ->  for (int i = 0; (unsigned)i < n; i++)
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

# Only polarity-bearing operators — never == or !=
_POLARITY_OPS = {"<", ">", "<=", ">="}

# Polarity-flip branch pairs: {(target, base)} where the two differ in polarity
_POLARITY_FLIP_PAIRS: frozenset[frozenset[str]] = frozenset({
    frozenset({"bge", "ble"}),
    frozenset({"blt", "bgt"}),
    # Also capture the strict-vs-nonstrict variants within the polarity axis:
    frozenset({"bge", "blt"}),
    frozenset({"ble", "bgt"}),
})

# Signed vs unsigned compare opcode pairs (feed bge/ble/blt/bgt)
_SIGNEDNESS_CMP_PAIRS: frozenset[frozenset[str]] = frozenset({
    frozenset({"cmpw",  "cmplw"}),
    frozenset({"cmpwi", "cmplwi"}),
    frozenset({"cmpd",  "cmpld"}),
    frozenset({"cmpdi", "cmpldi"}),
})

# Branch opcodes that care about the unsigned/signed distinction
_POLARITY_BRANCH_OPS = frozenset({"bge", "ble", "blt", "bgt"})

# Casts to try when a flip is needed: prioritise the two most common cases
_UNSIGNED_CAST = b"(unsigned int)"
_SIGNED_CAST   = b"(int)"
_BOTH_UNSIGNED = [_UNSIGNED_CAST]
_BOTH_SIGNED   = [_SIGNED_CAST]
_ALL_CAST_TYPES = [_UNSIGNED_CAST, _SIGNED_CAST]

_NULL_LITERALS = {"nullptr", "NULL"}


class SignedUnsignedCastPolarityPattern(Pattern):
    """Apply targeted sign casts to flip bge<->ble / blt<->bgt branch polarity."""

    name = "signed_unsigned_cast_polarity"

    # -----------------------------------------------------------------------
    # Relevance
    # -----------------------------------------------------------------------

    def relevant(self, diagnosis: Diagnosis) -> bool:
        """True if any diff_op pair is a polarity flip or signedness cmp pair."""
        for d in diagnosis.diff_ops:
            pair = frozenset({d.target_opcode, d.base_opcode})
            if pair in _POLARITY_FLIP_PAIRS:
                return True
            if pair in _SIGNEDNESS_CMP_PAIRS:
                # Only relevant if the surrounding diff also contains a
                # polarity-sensitive branch — avoid double-triggering on
                # cmpw<->cmplw that feeds beq/bne (that's signed_unsigned's job).
                return self._has_polarity_branch(diagnosis)
        return False

    @staticmethod
    def _has_polarity_branch(diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _POLARITY_BRANCH_OPS or d.base_opcode in _POLARITY_BRANCH_OPS:
                return True
        return False

    # -----------------------------------------------------------------------
    # Priority
    # -----------------------------------------------------------------------

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        score = 0.0
        for d in diagnosis.diff_ops:
            pair = frozenset({d.target_opcode, d.base_opcode})
            if pair in _POLARITY_FLIP_PAIRS:
                score += 0.7   # Direct polarity flip — strong signal
            elif pair in _SIGNEDNESS_CMP_PAIRS:
                score += 0.4   # Signedness cmp with polarity branch nearby
        return min(score, 1.0)

    # -----------------------------------------------------------------------
    # Generation
    # -----------------------------------------------------------------------

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0

        for stmt in ctx.statements:
            if not ctx.node_in_mismatch_region(stmt):
                continue

            # Only walk polarity-bearing comparisons
            for cmp_node in find_comparisons(stmt, _POLARITY_OPS):
                left = cmp_node.child_by_field_name("left")
                right = cmp_node.child_by_field_name("right")
                op_node = cmp_node.child_by_field_name("operator")
                if left is None or right is None:
                    continue

                op_text = ctx.source_text(op_node) if op_node else "?"

                # Skip pointer-like operands (same guard as signed_unsigned)
                if _is_likely_pointer(left, right, ctx):
                    continue

                # Try type-guided casts when libclang is available
                left_type = _resolve_operand_type(left, ctx)
                right_type = _resolve_operand_type(right, ctx)

                if left_type is not None or right_type is not None:
                    # Precise path: only emit casts that flip signedness
                    cast_plan = _polarity_guided_casts(left_type, right_type)
                else:
                    # Heuristic path: try all sign-flipping casts on both sides
                    cast_plan = _heuristic_casts()

                for cast, side, operand in _cast_candidates(cast_plan, left, right):
                    ed = SourceEditor(ctx.file_source)
                    ed.insert_before(operand, cast)
                    try:
                        new_source = ed.apply()
                    except ValueError:
                        continue
                    cast_str = cast.decode()
                    yield Variant(
                        name=f"polarity_cast_{counter}",
                        pattern_name=self.name,
                        description=f"Polarity-cast {side} of '{op_text}' to {cast_str}",
                        source=new_source,
                    )
                    counter += 1

                # Also try casting BOTH sides simultaneously when types differ
                for combo_variants in _both_sides_cast_variants(
                    left, right, op_text, left_type, right_type, ctx, counter
                ):
                    yield combo_variants
                    counter += 1


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _resolve_operand_type(node: Node, ctx: FunctionContext):
    """Return clang_types.TypeInfo for a comparison operand, or None."""
    if not clang_types.is_available():
        return None
    return clang_types.resolve_type_at(
        ctx.file_path, node.start_byte, ctx.file_source
    )


def _polarity_guided_casts(left_type, right_type) -> dict[str, list[bytes]]:
    """Return targeted cast plan that would flip the signedness polarity.

    For a polarity flip the goal is to make *one or both* operands change from
    signed to unsigned (or vice-versa).  We only emit casts that would produce
    a meaningful type change.
    """
    plan: dict[str, list[bytes]] = {"left": [], "right": []}

    for side, typ in [("left", left_type), ("right", right_type)]:
        if typ is None:
            # No type info — try both directions
            plan[side] = list(_ALL_CAST_TYPES)
        elif typ.is_pointer:
            plan[side] = []   # Never cast pointers
        elif typ.is_signed_int:
            plan[side] = [_UNSIGNED_CAST]   # signed -> unsigned flips polarity
        elif typ.is_unsigned_int:
            plan[side] = [_SIGNED_CAST]     # unsigned -> signed flips polarity
        elif hasattr(clang_types, "TypeKind"):
            kind = getattr(clang_types.TypeKind, "BOOL", None)
            enum_kind = getattr(clang_types.TypeKind, "ENUM", None)
            if typ.kind == kind or typ.kind == enum_kind:
                plan[side] = list(_ALL_CAST_TYPES)
            else:
                plan[side] = list(_ALL_CAST_TYPES)
        else:
            plan[side] = list(_ALL_CAST_TYPES)

    return plan


def _heuristic_casts() -> dict[str, list[bytes]]:
    """Fallback cast plan when libclang is unavailable."""
    return {
        "left":  list(_ALL_CAST_TYPES),
        "right": list(_ALL_CAST_TYPES),
    }


def _cast_candidates(
    plan: dict[str, list[bytes]],
    left: Node,
    right: Node,
) -> Iterator[tuple[bytes, str, Node]]:
    """Yield (cast_bytes, side, operand_node) for single-side casts."""
    for cast in plan.get("left", []):
        yield cast, "left", left
    for cast in plan.get("right", []):
        yield cast, "right", right


def _both_sides_cast_variants(
    left: Node,
    right: Node,
    op_text: str,
    left_type,
    right_type,
    ctx: FunctionContext,
    start_counter: int,
) -> Iterator[Variant]:
    """Yield variants that cast both operands to the same type (forces consistent signedness).

    Uses a single SourceEditor with two insert_before calls.  The editor
    applies both at once in reverse offset order, so the later-in-source
    insert (right) is processed first, keeping the left offset stable.

    Only emits both-sides casts when:
    - libclang says the two operands have opposite signedness, OR
    - libclang is unavailable (heuristic path: try both)
    """
    # Only emit when we have a mixed-sign situation or no type info at all
    mixed_sign = (
        (left_type is not None and right_type is not None and
         ((left_type.is_signed_int and right_type.is_unsigned_int) or
          (left_type.is_unsigned_int and right_type.is_signed_int)))
    )
    no_type_info = left_type is None or right_type is None

    if not (mixed_sign or no_type_info):
        return

    for combo_cast in [_UNSIGNED_CAST, _SIGNED_CAST]:
        # Single editor — two zero-width inserts at different offsets
        ed = SourceEditor(ctx.file_source)
        ed.insert_before(left, combo_cast)
        ed.insert_before(right, combo_cast)
        try:
            new_source = ed.apply()
        except ValueError:
            continue

        cast_str = combo_cast.decode()
        yield Variant(
            name=f"polarity_cast_{start_counter}",
            pattern_name="signed_unsigned_cast_polarity",
            description=f"Cast both sides of '{op_text}' to {cast_str}",
            source=new_source,
        )


def _ident_is_pointer_like(ident: str, ctx: FunctionContext) -> bool:
    """Heuristic: True when ident is declared or used as a pointer in the TU."""
    src = ctx.file_source.decode("utf-8", errors="replace")
    esc = re.escape(ident)
    if re.search(rf"\b{esc}\s*->", src):
        return True
    if re.search(rf"[\w>\)]\s*\*\s*{esc}\b", src):
        return True
    return False


def _is_likely_pointer(left: Node, right: Node, ctx: FunctionContext) -> bool:
    """Return True if either operand is likely a pointer (skip casting)."""
    for operand in (left, right):
        if operand.type == "unary_expression":
            op = operand.child_by_field_name("operator")
            if op and op.text == b"&":
                return True

        text = ctx.source_text(operand)
        if text in _NULL_LITERALS:
            return True

        if operand.type == "call_expression":
            return True

        if operand.type == "field_expression":
            return True

        if operand.type == "pointer_expression":
            return True

        if operand.type == "identifier":
            ident = ctx.source_text(operand)
            if ident and _ident_is_pointer_like(ident, ctx):
                return True

    left_text  = ctx.source_text(left)
    right_text = ctx.source_text(right)
    if left_text in _NULL_LITERALS or right_text in _NULL_LITERALS:
        return True

    return False
