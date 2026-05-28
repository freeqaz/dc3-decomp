"""Bool cast pattern — add bool() cast or extract bool to local variable.

The MSVC PowerPC compiler generates `clrlwi rN, rN, 24` (mask to 8 bits)
when it knows a value is bool. Missing this instruction causes BOOL_MASK
mismatches. Adding an explicit `bool()` cast or extracting to a `bool`
local variable forces the compiler to emit the mask.

Example:
    return ptr->IsActive();
    ->
    bool active = ptr->IsActive();
    return active;

    // or:
    return bool(ptr->IsActive());
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent, walk, find_calls
from ..types import Diagnosis, FunctionContext, Variant


class BoolCastPattern(Pattern):
    name = "bool_cast"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Relevant when there are replace instructions (clrlwi vs no clrlwi)
        # or diff_ops suggesting bool handling differences
        if diagnosis.replace_real > 0:
            return True
        if diagnosis.clusters:
            return True
        for d in diagnosis.diff_ops:
            # rlwinm is the generic form, clrlwi is a pseudo-op
            if "rlwinm" in d.target_opcode or "rlwinm" in d.base_opcode:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        for d in diagnosis.diff_ops:
            if "rlwinm" in d.target_opcode or "rlwinm" in d.base_opcode:
                return 0.6
        if diagnosis.replace_real > 0:
            return 0.4
        return 0.15

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            # Region filter: skip statements outside mismatch regions
            if not ctx.node_in_mismatch_region(stmt):
                continue
            for variant in _generate_bool_casts(stmt, ctx, counter):
                yield variant
                counter += 1


def _generate_bool_casts(
    stmt: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate bool cast variants for function calls that might return bool."""
    source = ctx.file_source

    for node in walk(stmt):
        # Pattern 1: bool() cast around call in return statement
        if node.type == "return_statement":
            expr = None
            for child in node.named_children:
                if child.type != "comment":
                    expr = child
                    break
            if expr is None:
                continue

            # Skip if already a bool cast
            if _is_bool_cast(expr):
                continue

            # Only wrap calls or method calls
            if expr.type in ("call_expression",):
                expr_text = source[expr.start_byte:expr.end_byte]
                indent = get_indent(source, node)

                # Variant A: wrap with bool()
                new_source = (
                    source[:expr.start_byte]
                    + b"bool(" + expr_text + b")"
                    + source[expr.end_byte:]
                )
                yield Variant(
                    name=f"boolcast_{counter}",
                    pattern_name="bool_cast",
                    description=f"Wrap return call with bool(): {expr_text.decode(errors='replace')[:40]}",
                    source=new_source,
                )
                counter += 1

        # Pattern 2: Extract condition call to bool local
        if node.type == "if_statement":
            condition = node.child_by_field_name("condition")
            if condition is None:
                continue
            inner = _get_inner_expr(condition)
            if inner is None:
                continue

            # Skip if already a bool cast or simple identifier
            if inner.type in ("identifier", "true", "false"):
                continue
            if _is_bool_cast(inner):
                continue

            # Only for call expressions or negated calls
            call = inner
            if inner.type == "unary_expression":
                op = inner.child_by_field_name("operator")
                if op and op.text == b"!":
                    call = inner.child_by_field_name("argument")

            if call is None or call.type != "call_expression":
                continue

            inner_text = source[inner.start_byte:inner.end_byte]
            indent = get_indent(source, node)

            # Extract to bool local before the if
            new_source = (
                source[:node.start_byte]
                + b"bool _cond = " + inner_text + b";\n"
                + indent + b"if (_cond)"
                + source[condition.end_byte:]
            )
            yield Variant(
                name=f"boolcast_{counter}",
                pattern_name="bool_cast",
                description=f"Extract condition to bool local: {inner_text.decode(errors='replace')[:40]}",
                source=new_source,
            )
            counter += 1

        # Pattern 3: Bool cast in assignment
        if node.type == "expression_statement":
            for child in node.named_children:
                if child.type != "assignment_expression":
                    continue
                right = child.child_by_field_name("right")
                if right is None or right.type != "call_expression":
                    continue
                if _is_bool_cast(right):
                    continue

                # Fix (Wave F3): only wrap when the LHS plausibly accepts a
                # bool. `bool(X)` returns a bool that implicitly converts to
                # arithmetic types, but it does NOT convert to record/class
                # lvalues — so `arr[i] = bool(RGGemInfo(...))` is a hard
                # compile error. Without libclang we can't always check the
                # LHS type, so reject the dangerous shapes by syntax:
                #   - LHS is a subscript_expression (arr[i])  -> record-likely
                #   - LHS is a field_expression (a.b / a->b) -> record-likely
                # Keep LHS == identifier (simple local/parameter), which is
                # the win shape the pattern was authored for (bool flag = X).
                # See Wave E2c BUILD FAILED sweep on
                # SongParser::HandleRGGemStart.
                left = child.child_by_field_name("left")
                if left is None or not _bool_assignable_lvalue(left):
                    continue

                right_text = source[right.start_byte:right.end_byte]
                new_source = (
                    source[:right.start_byte]
                    + b"bool(" + right_text + b")"
                    + source[right.end_byte:]
                )
                yield Variant(
                    name=f"boolcast_{counter}",
                    pattern_name="bool_cast",
                    description=f"Wrap assignment RHS with bool(): {right_text.decode(errors='replace')[:40]}",
                    source=new_source,
                )
                counter += 1


def _bool_assignable_lvalue(node: Node) -> bool:
    """True if *node* (an assignment LHS) plausibly accepts an implicit bool RHS.

    Wave-F3 syntactic filter: ``bool(X)`` returns a bool which converts to
    arithmetic types but NEVER to record/class lvalues. Without libclang we
    can't always know the field's true type, but we CAN reject the shapes
    that are overwhelmingly non-bool by syntax — subscript and member access.
    Plain identifiers (local/parameter assignments to a ``bool flag`` are
    the win-shape this pattern was authored for) are allowed through.
    """
    if node.type == "identifier":
        return True
    # Anything else (subscript_expression, field_expression, pointer_expression,
    # parenthesized_expression, etc.) is rejected — too risky without types.
    return False


def _is_bool_cast(node: Node) -> bool:
    """Check if node is already a bool() cast."""
    if node.type == "call_expression":
        func = node.child_by_field_name("function")
        if func and func.text == b"bool":
            return True
    return False


def _get_inner_expr(condition: Node) -> Node | None:
    """Extract the inner expression from a condition_clause."""
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None
