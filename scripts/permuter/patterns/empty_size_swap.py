"""Empty/size swap pattern — swap between empty() and size() == 0.

Targets the documented codegen difference (TECHNICAL_NOTES line 465-480):
empty() generates pointer comparison, size() == 0 generates division.

Direction detection:
    divw/divwu in TARGET means target uses size() -> swap empty() to size()
    cmplw in TARGET means target uses empty() -> swap size() to empty()

Transformations:
    x.empty()        -> x.size() == 0
    !x.empty()       -> x.size() != 0  (and > 0 variant)
    x.size() == 0    -> x.empty()
    x.size() != 0    -> !x.empty()
    x.size() > 0     -> !x.empty()

Example:
    if (mList.empty())
    ->
    if (mList.size() == 0)

Exclusions:
    * Calls inside MILO_ASSERT / MILO_ASSERT_MSG / MILO_ASSERT_RANGE /
      MILO_ASSERT_RANGE_EQ macros are NOT swapped — the macro stringifies
      its condition for the assert pool, and changing the source text
      shifts the pool slot, which can regress codegen elsewhere.
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..types import Diagnosis, FunctionContext, Variant

# Assertion-family macros that stringify their argument; calls inside these
# must not be rewritten or the assert pool slot shifts and regresses codegen
# elsewhere in the TU. See feedback_milo_assert_pool_shift.md.
_MILO_ASSERT_MACROS = (
    b"MILO_ASSERT",
    b"MILO_ASSERT_MSG",
    b"MILO_ASSERT_RANGE",
    b"MILO_ASSERT_RANGE_EQ",
    b"MILO_ASSERT_FMT",
    b"MILO_ASSERT_NOT_NULL",
)

# Pre-compiled regex for fast line-text matching. A call site is "inside"
# a MILO_ASSERT macro if the same source line (or any line in a wrapped
# macro invocation) opens with one of these macros followed by ``(``.
_MILO_ASSERT_RE = re.compile(
    rb"\b(?:MILO_ASSERT|MILO_ASSERT_MSG|MILO_ASSERT_RANGE|"
    rb"MILO_ASSERT_RANGE_EQ|MILO_ASSERT_FMT|MILO_ASSERT_NOT_NULL)\s*\("
)


class EmptySizeSwapPattern(Pattern):
    name = "empty_size_swap"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        return self._detect_direction(diagnosis) is not None

    def _detect_direction(self, diagnosis: Diagnosis) -> str | None:
        """Detect which direction to swap based on opcode mismatches.

        Returns "to_size" if target uses size()-style codegen, "to_empty" if
        target uses empty()-style codegen, or None if no signal.

        Two detection paths:

        1. ``diff_ops`` with ``divw``/``divwu``/``mulli`` — the classic signature
           for ``size() != 0`` on STL containers whose ``size()`` divides by a
           non-power-of-2 ``sizeof(T)``.

        2. Clustered target-only or base-only signature for power-of-2
           ``sizeof(T)`` containers like ``deque<T*>``/``vector<T*>``, where
           ``(_M_finish - _M_start) / sizeof(T)`` inlines as a multi-instruction
           arithmetic-shift idiom. The signature requires BOTH ``subf`` and
           ``srawi`` in the same insert/delete cluster (plus often ``addze`` or
           ``mulhw``). Checking same-cluster co-occurrence avoids firing on
           unrelated div-by-constant idioms elsewhere in the function.

        The previous ``subf`` secondary heuristic on ``diff_ops`` was removed
        because ``subf`` appears in unrelated contexts (loop conditions, binary
        search). The new path looks at insert/delete clusters instead, where
        target-only ``subf`` arithmetic is a much stronger inlined-arithmetic
        signal.
        """
        _SIZE_OPS = {"divw", "divwu", "mulli"}

        target_has_size = any(
            d.target_opcode in _SIZE_OPS for d in diagnosis.diff_ops
        )
        base_has_size = any(
            d.base_opcode in _SIZE_OPS for d in diagnosis.diff_ops
        )

        # Secondary path: clustered pointer-diff arithmetic (deque::size on
        # power-of-2-sized element types). Require BOTH subf and srawi in the
        # same cluster — either alone is too common to be a reliable signal.
        if not target_has_size:
            target_has_size = self._has_pointer_diff_cluster(
                diagnosis, side="target"
            )
        if not base_has_size:
            base_has_size = self._has_pointer_diff_cluster(
                diagnosis, side="base"
            )

        if target_has_size and not base_has_size:
            return "to_size"  # target uses size(), swap empty→size
        if base_has_size and not target_has_size:
            return "to_empty"  # we use size(), target uses empty()

        return None

    @staticmethod
    def _has_pointer_diff_cluster(diagnosis: Diagnosis, side: str) -> bool:
        """Detect the inlined ``(end-begin)/sizeof(T)`` size() signature.

        Looks for at least one insert/delete cluster whose ``side``-only
        opcodes contain BOTH ``subf`` and a divide-by-power-of-2 op
        (``srawi``), or both ``mulhw`` and ``srawi`` (non-power-of-2 div).

        ``side`` is "target" (target-only deletes) or "base" (our-side inserts).
        """
        for c in diagnosis.clusters:
            ops = c.target_opcodes if side == "target" else c.base_opcodes
            if not ops:
                continue
            ops_set = set(ops)
            # Power-of-2 sizeof(T): (end - begin) >> log2(sizeof) then addze
            if "subf" in ops_set and "srawi" in ops_set:
                return True
            # Non-power-of-2 sizeof(T): multiply-high by reciprocal + srawi
            if "mulhw" in ops_set and "srawi" in ops_set:
                return True
        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        direction = None
        if ctx.diagnosis is not None:
            direction = self._detect_direction(ctx.diagnosis)
        # If no direction detected (shouldn't happen since relevant() checks),
        # stay silent rather than generating both directions
        if direction is None:
            return

        counter = 0
        for stmt in ctx.statements:
            for variant in _find_swaps(stmt, ctx, counter, direction):
                yield variant
                counter += 1


def _find_swaps(
    node: Node, ctx: FunctionContext, counter: int, direction: str
) -> Iterator[Variant]:
    """Find empty()/size() swap opportunities recursively."""
    source = ctx.file_source

    # Block-out: skip nodes inside MILO_ASSERT family macros. Rewriting a
    # call inside such a macro shifts the stringified assert pool slot and
    # has been observed to regress codegen at other sites in the TU.
    if _node_inside_milo_assert(node, source):
        return

    # Pattern 1: x.empty() -> x.size() == 0
    # Pattern 2: !x.empty() -> x.size() != 0 / x.size() > 0
    if node.type == "call_expression" and direction == "to_size":
        func = node.child_by_field_name("function")
        args = node.child_by_field_name("arguments")
        if (
            func is not None
            and func.type == "field_expression"
            and args is not None
        ):
            field = func.child_by_field_name("field")
            obj = func.child_by_field_name("argument")
            if (
                field is not None
                and field.text == b"empty"
                and obj is not None
                and _arg_count(args) == 0
            ):
                obj_text = source[obj.start_byte : obj.end_byte]
                # Determine the operator between obj and field (. or ->)
                op = _get_field_operator(func, source)

                # Check if this call is negated: !x.empty()
                parent = node.parent
                is_negated = (
                    parent is not None
                    and parent.type == "unary_expression"
                    and _get_unary_op(parent) == b"!"
                )

                if is_negated:
                    # !x.empty() -> x.size() != 0
                    replace_node = parent
                    new_expr = obj_text + op + b"size() != 0"
                    new_source = (
                        source[: replace_node.start_byte]
                        + new_expr
                        + source[replace_node.end_byte :]
                    )
                    yield Variant(
                        name=f"emptysize_{counter}",
                        pattern_name="empty_size_swap",
                        description="!x.empty() -> x.size() != 0",
                        source=new_source,
                    )
                    counter += 1

                    # !x.empty() -> x.size() > 0
                    new_expr2 = obj_text + op + b"size() > 0"
                    new_source2 = (
                        source[: replace_node.start_byte]
                        + new_expr2
                        + source[replace_node.end_byte :]
                    )
                    yield Variant(
                        name=f"emptysize_{counter}",
                        pattern_name="empty_size_swap",
                        description="!x.empty() -> x.size() > 0",
                        source=new_source2,
                    )
                    counter += 1
                else:
                    # x.empty() -> x.size() == 0
                    new_expr = obj_text + op + b"size() == 0"
                    new_source = (
                        source[: node.start_byte]
                        + new_expr
                        + source[node.end_byte :]
                    )
                    yield Variant(
                        name=f"emptysize_{counter}",
                        pattern_name="empty_size_swap",
                        description="x.empty() -> x.size() == 0",
                        source=new_source,
                    )
                    counter += 1

                return  # Don't recurse into this call's children

    # Pattern 3: x.size() == 0 -> x.empty()
    # Pattern 4: x.size() != 0 -> !x.empty()
    # Pattern 5: x.size() > 0 -> !x.empty()
    if node.type == "binary_expression" and direction == "to_empty":
        left = node.child_by_field_name("left")
        right = node.child_by_field_name("right")
        op_node = node.child_by_field_name("operator")
        if left is not None and right is not None and op_node is not None:
            op_text = op_node.text
            right_text = right.text

            if (
                right_text is not None
                and right_text.strip() == b"0"
                and left.type == "call_expression"
            ):
                func = left.child_by_field_name("function")
                args = left.child_by_field_name("arguments")
                if (
                    func is not None
                    and func.type == "field_expression"
                    and args is not None
                ):
                    field = func.child_by_field_name("field")
                    obj = func.child_by_field_name("argument")
                    if (
                        field is not None
                        and field.text == b"size"
                        and obj is not None
                        and _arg_count(args) == 0
                    ):
                        obj_text = source[obj.start_byte : obj.end_byte]
                        member_op = _get_field_operator(func, source)

                        if op_text == b"==":
                            # x.size() == 0 -> x.empty()
                            new_expr = obj_text + member_op + b"empty()"
                            new_source = (
                                source[: node.start_byte]
                                + new_expr
                                + source[node.end_byte :]
                            )
                            yield Variant(
                                name=f"emptysize_{counter}",
                                pattern_name="empty_size_swap",
                                description="x.size() == 0 -> x.empty()",
                                source=new_source,
                            )
                            counter += 1

                        elif op_text in (b"!=", b">"):
                            # x.size() != 0 -> !x.empty()
                            # x.size() > 0 -> !x.empty()
                            new_expr = b"!" + obj_text + member_op + b"empty()"
                            new_source = (
                                source[: node.start_byte]
                                + new_expr
                                + source[node.end_byte :]
                            )
                            op_str = op_text.decode("utf-8")
                            yield Variant(
                                name=f"emptysize_{counter}",
                                pattern_name="empty_size_swap",
                                description=f"x.size() {op_str} 0 -> !x.empty()",
                                source=new_source,
                            )
                            counter += 1

                        return  # Don't recurse into this comparison's children

    # Recurse into children
    for child in node.children:
        for variant in _find_swaps(child, ctx, counter, direction):
            yield variant
            counter += 1


def _node_inside_milo_assert(node: Node, source: bytes) -> bool:
    """Return True if ``node`` is inside a MILO_ASSERT-family macro call.

    Two-stage check:
      1. Fast: scan ancestor call_expression nodes for a function name in
         ``_MILO_ASSERT_MACROS``.
      2. Fallback (line-text): if the line containing this node has a
         MILO_ASSERT match BEFORE the node start, treat it as inside —
         this catches multi-line macro invocations the AST parser may
         not fully resolve as a single call_expression in C++ source.
    """
    # Stage 1: ancestor call_expression check
    cur = node.parent
    while cur is not None:
        if cur.type == "call_expression":
            func = cur.child_by_field_name("function")
            if func is not None:
                fn_text = source[func.start_byte:func.end_byte]
                if fn_text in _MILO_ASSERT_MACROS:
                    return True
        cur = cur.parent

    # Stage 2: line-text fallback. Walk back to the line start for `node`
    # and search the line region for the macro name.
    start = node.start_byte
    line_start = source.rfind(b"\n", 0, start) + 1
    # Scan only up to the node start; we want to detect the opening macro.
    if _MILO_ASSERT_RE.search(source, line_start, start):
        return True
    return False


def _arg_count(args_node: Node) -> int:
    """Count non-punctuation children in an argument_list."""
    return sum(1 for c in args_node.named_children)


def _get_field_operator(field_expr: Node, source: bytes) -> bytes:
    """Get the . or -> operator from a field_expression."""
    # The operator is between the argument and field children
    arg = field_expr.child_by_field_name("argument")
    field = field_expr.child_by_field_name("field")
    if arg is not None and field is not None:
        between = source[arg.end_byte : field.start_byte]
        if b"->" in between:
            return b"->"
        return b"."
    return b"."


def _get_unary_op(node: Node) -> bytes | None:
    """Get the operator of a unary_expression."""
    op = node.child_by_field_name("operator")
    return op.text if op is not None else None


# ---------------------------------------------------------------------------
# TODO: post-apply regression guard
# ---------------------------------------------------------------------------
# Desired behavior: after a variant produced by this pattern is APPLIED to
# the workspace and the next objdiff run shows a LOWER match% than the
# baseline at apply time, the orchestrator should auto-revert the edit and
# mark the (file, line, swap-kind) site as exhausted so the same swap is
# not re-attempted on subsequent rounds.
#
# Pattern infrastructure today is generation-only: Pattern.generate() yields
# Variant() objects with no callback hook for "apply succeeded / regressed".
# The natural place to add this guard is in the orchestrator loop where
# variants are scored, not here in the pattern. The required hook is:
#
#   scripts/permuter/hill_climber.py (or scripts/permuter/beam_search.py)
#     after `score = run_objdiff(variant)`:
#       if score < baseline_score and variant.pattern_name == "empty_size_swap":
#           record_exhausted_site(
#               ctx.file_path,
#               line=variant_first_edit_line(variant),
#               kind="empty_size_swap",
#           )
#           # Already-reverted via the climber's "don't keep regressions" path.
#
#   scripts/permuter/types.py: add `RoundHints.exhausted_sites: set[tuple]`
#   read at generation time. Then this pattern's generate() can consult
#   ctx.round_hints (currently absent from FunctionContext) to skip
#   previously-exhausted sites.
#
# Until the hook exists, this pattern's only safeguard is the MILO_ASSERT
# block-out above. A round-level suppression_factor in RoundHints already
# exists for the WHOLE pattern, but per-site exhaustion would be finer-
# grained and is what's needed here.
