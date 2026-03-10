"""Boolean materialization pattern — trigger branchless codegen via (bool) cast.

MSVC PPC generates different instruction sequences based on how a comparison
result is used:

1. `a && (x > 1)` → cmpwi + ble (branching, short-circuit)
2. `a && (bool)(x > 1)` → short-circuit a, then subfc/eqv/srwi/addze/clrlwi.
   (branchless materialization of comparison with record bit)
3. `a & (x > 1)` → subfc/eqv/srwi/addze + and. (fully branchless, no short-circuit)

The (bool) cast on the comparison RHS of && is the KEY trigger: it forces
the compiler to materialize the comparison as a boolean value (subfc sequence)
while preserving short-circuit evaluation on the LHS.

This was discovered empirically: ContentLoadingPanel::ShowIfPossible went
from 87% to 100% by changing `mAllowedToShow && mContentCount > 1` to
`mAllowedToShow && (bool)(mContentCount > 1)`.

Equivalent forms that produce the same codegen:
- `if (a) { bool gt = x > 1; if (gt) { ... } }`  (split check)

Example:
    if (a && x > 1)              → cmpwi cr6, r11, 1 / ble (branching)
    if (a && (bool)(x > 1))      → subfc/eqv/srwi/addze/clrlwi. (branchless + record)
    if (a & (x > 1))             → subfc/eqv/srwi/addze/clrlwi + and. (fully branchless)
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, get_line_start
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


class BoolMaterializePattern(Pattern):
    name = "bool_materialize"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        if getattr(diagnosis, "bool_materialization_sequences", 0) > 0:
            return True
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("subfc", "eqv", "addze"):
                return True
        if diagnosis.clusters:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if getattr(diagnosis, "bool_materialization_sequences", 0) > 0:
            return 0.85
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("subfc", "eqv", "addze"):
                return 0.7
        if diagnosis.clusters:
            return 0.15
        return 0.0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            for v in _generate_variants(stmt, ctx, counter):
                yield v
                counter += 1
                if counter >= 20:
                    return


def _generate_variants(
    stmt: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate boolean materialization variants."""
    source = ctx.file_source

    for node in walk(stmt):
        if counter > 20:
            return

        if node.type != "binary_expression":
            continue

        op_node = node.child_by_field_name("operator")
        if op_node is None:
            continue

        op_text = source[op_node.start_byte:op_node.end_byte]

        if op_text == b"&&":
            left = node.child_by_field_name("left")
            right = node.child_by_field_name("right")
            if left is None or right is None:
                continue

            left_text = source[left.start_byte:left.end_byte]
            right_text = source[right.start_byte:right.end_byte]

            # --- Variant A (HIGHEST PRIORITY): Wrap RHS comparison with (bool) ---
            # This preserves short-circuit on LHS while forcing branchless
            # materialization on the comparison. PROVEN to fix real functions.
            if _is_comparison(right):
                new_rhs = b"(bool)(" + right_text + b")"
                new_source = (
                    source[:right.start_byte]
                    + new_rhs
                    + source[right.end_byte:]
                )
                yield Variant(
                    name=f"boolmat_{counter}",
                    pattern_name="bool_materialize",
                    description=f"Add (bool) cast to && RHS: {_preview(source, right)}",
                    source=new_source,
                )
                counter += 1

            # --- Variant B: Split into nested if with bool variable ---
            # if (a && x > 1) → if (a) { bool _gt = x > 1; if (_gt) ... }
            if _is_comparison(right):
                insert_stmt = _find_enclosing_if(node)
                if insert_stmt is not None:
                    consequence = insert_stmt.child_by_field_name("consequence")
                    alternative = insert_stmt.child_by_field_name("alternative")
                    # Only split simple if-statements (no else)
                    if consequence is not None and alternative is None:
                        condition = insert_stmt.child_by_field_name("condition")
                        if condition is not None:
                            indent = get_indent(source, insert_stmt)
                            inner_indent = indent + b"    "
                            var_name = f"_gt{counter}".encode()

                            # Build: if (LEFT) { bool _gt = RIGHT; if (_gt) BODY }
                            body_text = source[consequence.start_byte:consequence.end_byte]
                            new_block = (
                                b"if (" + left_text + b") {\n"
                                + inner_indent + b"bool " + var_name + b" = " + right_text + b";\n"
                                + inner_indent + b"if (" + var_name + b") "
                                + body_text + b"\n"
                                + indent + b"}"
                            )
                            new_source = (
                                source[:insert_stmt.start_byte]
                                + new_block
                                + source[insert_stmt.end_byte:]
                            )
                            yield Variant(
                                name=f"boolmat_{counter}",
                                pattern_name="bool_materialize",
                                description=f"Split && into nested if with bool var: {_preview(source, right)}",
                                source=new_source,
                            )
                            counter += 1

            # --- Variant C: Swap && → & (fully branchless, no short-circuit) ---
            right_needs_parens = _contains_comparison(right)
            left_needs_parens = _contains_comparison(left)

            r_text = b"(" + right_text + b")" if right_needs_parens else right_text
            l_text = b"(" + left_text + b")" if left_needs_parens else left_text

            new_expr = l_text + b" & " + r_text
            new_source = (
                source[:node.start_byte]
                + new_expr
                + source[node.end_byte:]
            )
            yield Variant(
                name=f"boolmat_{counter}",
                pattern_name="bool_materialize",
                description=f"Swap && to & (fully branchless): {_preview(source, node)}",
                source=new_source,
            )
            counter += 1

        elif op_text == b"&":
            left = node.child_by_field_name("left")
            right = node.child_by_field_name("right")
            if left is None or right is None:
                continue

            left_text = source[left.start_byte:left.end_byte]
            right_text = source[right.start_byte:right.end_byte]

            # --- Variant D: Swap & → && (add short-circuit) ---
            right_text = _strip_outer_parens(right_text)
            new_expr = left_text + b" && " + right_text
            new_source = (
                source[:node.start_byte]
                + new_expr
                + source[node.end_byte:]
            )
            yield Variant(
                name=f"boolmat_{counter}",
                pattern_name="bool_materialize",
                description=f"Swap & to && (add short-circuit): {_preview(source, node)}",
                source=new_source,
            )
            counter += 1

            # --- Variant E: Swap & → && with (bool) cast on RHS ---
            if _is_comparison_or_parened_comparison(right, source):
                inner = _strip_outer_parens(right_text)
                new_expr = left_text + b" && (bool)(" + inner + b")"
                new_source = (
                    source[:node.start_byte]
                    + new_expr
                    + source[node.end_byte:]
                )
                yield Variant(
                    name=f"boolmat_{counter}",
                    pattern_name="bool_materialize",
                    description=f"Swap & to && with (bool) cast: {_preview(source, node)}",
                    source=new_source,
                )
                counter += 1


def _is_comparison(node: Node) -> bool:
    """Check if node is a comparison expression."""
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        return op is not None and op.text in (b">", b"<", b">=", b"<=", b"==", b"!=")
    return False


def _is_comparison_or_parened_comparison(node: Node, source: bytes) -> bool:
    """Check if node is a comparison or parenthesized comparison."""
    if _is_comparison(node):
        return True
    if node.type == "parenthesized_expression":
        for child in node.named_children:
            if _is_comparison(child):
                return True
    return False


def _contains_comparison(node: Node) -> bool:
    """Check if node contains a comparison operator that would need parens with &."""
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op and op.text in (b">", b"<", b">=", b"<=", b"==", b"!=", b"&&", b"||"):
            return True
    for child in node.named_children:
        if _contains_comparison(child):
            return True
    return False


def _find_enclosing_if(node: Node) -> Node | None:
    """Walk up to find the enclosing if_statement."""
    current = node.parent
    while current is not None:
        if current.type == "if_statement":
            return current
        if current.type == "condition_clause":
            current = current.parent
            continue
        current = current.parent
    return None


def _strip_outer_parens(text: bytes) -> bytes:
    """Strip one layer of outer parentheses if balanced."""
    if text.startswith(b"(") and text.endswith(b")"):
        inner = text[1:-1]
        depth = 0
        for ch in inner:
            if ch == ord(b"("):
                depth += 1
            elif ch == ord(b")"):
                depth -= 1
            if depth < 0:
                return text
        if depth == 0:
            return inner
    return text


def _preview(source: bytes, node: Node) -> str:
    """Get a truncated preview of a node's source text."""
    text = source[node.start_byte:node.end_byte].decode("utf-8", errors="replace")
    if len(text) > 50:
        text = text[:47] + "..."
    return text
