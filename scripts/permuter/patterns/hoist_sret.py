"""Hoist sret pattern — move variable declarations in/out of loops.

Moving a variable declaration that receives a struct-return value from
inside a loop to before the loop can change stack allocation and
eliminate extra lwz/stw instructions. The reverse can also help.

Example:
    for (int i = 0; i < n; i++) {
        Vector3 pos = GetPos(i);
        Use(pos);
    }
    ->
    Vector3 pos;
    for (int i = 0; i < n; i++) {
        pos = GetPos(i);
        Use(pos);
    }
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent
from ..types import Diagnosis, FunctionContext, Variant

_LOOP_TYPES = {"for_statement", "while_statement", "do_statement"}


class HoistSretPattern(Pattern):
    name = "hoist_sret"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Relevant when there are clusters or store/load mismatches
        if diagnosis.clusters:
            return True
        store_load_ops = {"lwz", "stw", "lfs", "stfs", "lfd", "stfd"}
        for d in diagnosis.diff_ops:
            if d.target_opcode in store_load_ops or d.base_opcode in store_load_ops:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        store_load_ops = {"lwz", "stw", "lfs", "stfs", "lfd", "stfd"}
        sl_count = sum(
            1 for d in diagnosis.diff_ops
            if d.target_opcode in store_load_ops or d.base_opcode in store_load_ops
        )
        if sl_count >= 3:
            return 0.5
        if sl_count > 0:
            return 0.3
        return 0.15

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            for variant in _hoist_out_of_loop(stmt, ctx, counter):
                yield variant
                counter += 1
        # Sink looks at sibling pairs (decl followed by loop)
        for variant in _sink_into_loop(ctx.body_node, ctx, counter):
            yield variant
            counter += 1


def _hoist_out_of_loop(node: Node, ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Find declarations inside loops and hoist them before the loop."""
    for loop_node in walk(node):
        if loop_node.type not in _LOOP_TYPES:
            continue

        body = loop_node.child_by_field_name("body")
        if body is None or body.type != "compound_statement":
            continue

        for child in body.named_children:
            if child.type != "declaration":
                continue

            # Must have an init_declarator with a call_expression value
            declarator = child.child_by_field_name("declarator")
            if declarator is None or declarator.type != "init_declarator":
                continue

            name_node = declarator.child_by_field_name("declarator")
            value_node = declarator.child_by_field_name("value")
            if name_node is None or value_node is None:
                continue

            # Get type
            type_node = child.child_by_field_name("type")
            if type_node is None:
                continue

            type_text = ctx.source_text(type_node)
            var_name = _get_declarator_text(name_node)
            if var_name is None:
                continue

            value_text = ctx.source_text(value_node)

            source = ctx.file_source
            loop_indent = get_indent(source, loop_node)
            body_indent = get_indent(source, child)

            # Build: Type var; before loop, var = value; inside loop
            decl_line = loop_indent + type_text.encode() + b" " + var_name.encode() + b";\n"
            assign_line = body_indent + var_name.encode() + b" = " + value_text.encode() + b";"

            # Replace the declaration inside the loop with assignment
            new_source = source[:child.start_byte] + assign_line + source[child.end_byte:]
            # Insert declaration before the loop
            loop_start = loop_node.start_byte
            # Find start of the line
            line_start = new_source.rfind(b"\n", 0, loop_start)
            insert_pos = line_start + 1 if line_start >= 0 else 0
            new_source = new_source[:insert_pos] + decl_line + new_source[insert_pos:]

            yield Variant(
                name=f"hoist_{counter}",
                pattern_name="hoist_sret",
                description=f"Hoist '{type_text} {var_name}' declaration before loop",
                source=new_source,
            )
            counter += 1


def _sink_into_loop(node: Node, ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Find declarations before loops that are only used inside, and sink them in."""
    if node.type not in ("compound_statement", "function_definition"):
        return

    body = node
    if node.type == "function_definition":
        body = node.child_by_field_name("body")
        if body is None:
            return

    stmts = list(body.named_children)
    for i in range(len(stmts) - 1):
        decl = stmts[i]
        loop = stmts[i + 1]

        if decl.type != "declaration":
            continue
        if loop.type not in _LOOP_TYPES:
            continue

        # Get declaration details
        type_node = decl.child_by_field_name("type")
        declarator = decl.child_by_field_name("declarator")
        if type_node is None or declarator is None:
            continue

        # Must be an uninitialised declaration (no init value)
        # OR an init_declarator
        if declarator.type == "init_declarator":
            # Has initializer — skip, it's already initialized outside
            continue
        elif declarator.type in ("identifier", "pointer_declarator", "reference_declarator"):
            # Uninitialized declaration — check if there's an assignment in the loop
            var_name = _get_declarator_text(declarator)
            if var_name is None:
                continue
        else:
            continue

        type_text = ctx.source_text(type_node)

        # Find first assignment to this var in the loop body
        loop_body = loop.child_by_field_name("body")
        if loop_body is None or loop_body.type != "compound_statement":
            continue

        assign_info = _find_first_assignment(loop_body, var_name, ctx)
        if assign_info is None:
            continue

        assign_node, assign_value = assign_info

        source = ctx.file_source
        body_indent = get_indent(source, assign_node)

        # Build init_declarator inside loop
        init_decl = (
            body_indent + type_text.encode() + b" " + var_name.encode()
            + b" = " + assign_value.encode() + b";"
        )

        # Replace assignment with declaration+init
        new_source = source[:assign_node.start_byte] + init_decl + source[assign_node.end_byte:]

        # Remove the original declaration (and trailing newline)
        decl_end = decl.end_byte
        if decl_end < len(new_source) and new_source[decl_end:decl_end + 1] == b"\n":
            decl_end += 1
        # Adjust for the text we already changed (init_decl vs assign)
        offset = len(init_decl) - (assign_node.end_byte - assign_node.start_byte)
        decl_start = decl.start_byte
        # Since decl comes before the loop, its positions are unchanged
        new_source = new_source[:decl_start] + new_source[decl_end:]

        yield Variant(
            name=f"hoist_{counter}",
            pattern_name="hoist_sret",
            description=f"Sink '{type_text} {var_name}' declaration into loop",
            source=new_source,
        )
        counter += 1


def _get_declarator_text(node: Node) -> str | None:
    """Get the variable name from a declarator node."""
    if node.type == "identifier":
        return node.text.decode() if node.text else None
    if node.type in ("pointer_declarator", "reference_declarator"):
        inner = node.child_by_field_name("declarator")
        return _get_declarator_text(inner) if inner else None
    return None


def _find_first_assignment(
    body: Node, var_name: str, ctx: FunctionContext
) -> tuple[Node, str] | None:
    """Find first `var_name = expr;` in body. Returns (statement_node, value_text)."""
    for child in body.named_children:
        if child.type != "expression_statement":
            continue
        for expr in child.named_children:
            if expr.type != "assignment_expression":
                continue
            left = expr.child_by_field_name("left")
            right = expr.child_by_field_name("right")
            if left is None or right is None:
                continue
            if left.type == "identifier" and left.text and left.text.decode() == var_name:
                return child, ctx.source_text(right)
    return None
