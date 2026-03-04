"""Negation split pattern — split -func() into f = func(); f = -f;

When a float function result is negated inline (-func()), the compiler
generates fneg before frsp (float round to single). But the original code
may have done frsp then fneg. Splitting the negation into a separate
statement fixes the instruction order.

Also handles the reverse: splitting a negation allows the compiler
to schedule differently.

Example:
    float angle = -acos(Dot(dir1, dir2));
    ->
    float angle = acos(Dot(dir1, dir2));
    angle = -angle;
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent, walk
from ..types import Diagnosis, FunctionContext, Variant


class NegationSplitPattern(Pattern):
    name = "negation_split"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Relevant when there are clusters (fneg/frsp scheduling) or diff_ops
        # on fneg/frsp instructions
        if diagnosis.clusters:
            return True
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("fneg", "frsp") or d.base_opcode in ("fneg", "frsp"):
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strong: fneg/frsp diff_ops — this is exactly what this pattern fixes
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("fneg", "frsp") or d.base_opcode in ("fneg", "frsp"):
                return 0.9
        return 0.15  # clusters only — weak signal

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            for variant in _find_negation_splits(stmt, ctx, counter):
                yield variant
                counter += 1


def _find_negation_splits(
    stmt: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Find -func() or -expr patterns and split them."""
    source = ctx.file_source

    for node in walk(stmt):
        # Look for unary - applied to a call expression or parenthesized expression
        if node.type != "unary_expression":
            continue

        op = node.child_by_field_name("operator")
        if op is None or op.text != b"-":
            continue

        operand = node.child_by_field_name("argument")
        if operand is None:
            continue

        # Only split if operand is a call or parenthesized call
        if operand.type not in ("call_expression", "parenthesized_expression"):
            continue

        # Find the containing declaration or expression statement
        # Pattern 1: float x = -func();  ->  float x = func(); x = -x;
        parent = node.parent
        if parent is None:
            continue

        # Walk up to find the init_declarator or assignment
        decl_node = _find_containing_declaration(node, stmt)
        if decl_node is not None:
            var_name = _get_declarator_name(decl_node)
            if var_name is None:
                continue

            # Replace -expr with expr, then insert negation after the declaration statement
            containing_stmt = _find_containing_statement(decl_node, stmt)
            if containing_stmt is None:
                continue

            operand_text = source[operand.start_byte:operand.end_byte]
            indent = get_indent(source, containing_stmt)

            # Replace the -expr with just expr
            new_source = (
                source[:node.start_byte]
                + operand_text
                + source[node.end_byte:containing_stmt.end_byte]
                + b"\n" + indent + var_name.encode() + b" = -" + var_name.encode() + b";"
                + source[containing_stmt.end_byte:]
            )

            yield Variant(
                name=f"negsplit_{counter}",
                pattern_name="negation_split",
                description=f"Split -{operand.type} into assignment + negation for {var_name}",
                source=new_source,
            )
            counter += 1
            continue

        # Pattern 2: x = -func();  ->  x = func(); x = -x;
        assign_node = _find_containing_assignment(node, stmt)
        if assign_node is not None:
            left = assign_node.child_by_field_name("left")
            if left is None:
                continue
            var_name = source[left.start_byte:left.end_byte].decode()

            containing_stmt = _find_containing_statement(assign_node, stmt)
            if containing_stmt is None:
                continue

            operand_text = source[operand.start_byte:operand.end_byte]
            indent = get_indent(source, containing_stmt)

            new_source = (
                source[:node.start_byte]
                + operand_text
                + source[node.end_byte:containing_stmt.end_byte]
                + b"\n" + indent + var_name.encode() + b" = -" + var_name.encode() + b";"
                + source[containing_stmt.end_byte:]
            )

            yield Variant(
                name=f"negsplit_{counter}",
                pattern_name="negation_split",
                description=f"Split negation into assignment + negate for {var_name}",
                source=new_source,
            )
            counter += 1


def _find_containing_declaration(node: Node, boundary: Node) -> Node | None:
    """Walk up to find init_declarator containing this node."""
    cur = node
    while cur and cur != boundary:
        if cur.type == "init_declarator":
            return cur
        cur = cur.parent
    return None


def _find_containing_assignment(node: Node, boundary: Node) -> Node | None:
    """Walk up to find assignment_expression containing this node."""
    cur = node
    while cur and cur != boundary:
        if cur.type == "assignment_expression":
            return cur
        cur = cur.parent
    return None


def _find_containing_statement(node: Node, boundary: Node) -> Node | None:
    """Walk up to find the statement (expression_statement or declaration) containing this node."""
    cur = node
    while cur and cur != boundary:
        if cur.type in ("expression_statement", "declaration"):
            return cur
        cur = cur.parent
    # If boundary itself is the statement
    if boundary.type in ("expression_statement", "declaration"):
        return boundary
    return None


def _get_declarator_name(init_decl: Node) -> str | None:
    """Get the variable name from an init_declarator."""
    for child in init_decl.children:
        if child.type == "identifier":
            return child.text.decode()
        if child.type == "pointer_declarator":
            for sub in child.children:
                if sub.type == "identifier":
                    return sub.text.decode()
    return None
