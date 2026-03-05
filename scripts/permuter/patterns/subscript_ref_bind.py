"""Subscript reference binding — hoist repeated subscript accesses into local refs.

Win rate: untested (new pattern).

When a subscript expression (container[i]) appears 2+ times with the same
index in a function body, bind it to a local reference to shift register
allocation. This is the inverse of reference_elimination and extends
member_ref_bind (which only handles member accesses, not subscripts).

Transformations:
    if (subDirs[i] != NULL)
        MergeObjectsRecurse(subDirs[i], toDir, filt, false);
    ->
    auto& _sub0 = subDirs[i];
    if (_sub0 != NULL)
        MergeObjectsRecurse(_sub0, toDir, filt, false);

Detection signals:
    - Callee-saved register swaps (binding changes allocation order)
    - Clusters (different load patterns)
    - Prologue mismatch where target needs more callee-saved regs
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, get_line_start
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Callee-saved register pattern
_CALLEE_SAVED_RE = re.compile(r"[rf](1[3-9]|2\d|3[01])")


class SubscriptRefBindPattern(Pattern):
    name = "subscript_ref_bind"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Callee-saved register swaps
        for (r1, r2) in diagnosis.reg_swap_pairs:
            if _CALLEE_SAVED_RE.match(r1) or _CALLEE_SAVED_RE.match(r2):
                return True

        # Clusters suggest instruction reordering
        if diagnosis.clusters:
            return True

        # Prologue mismatch where target needs more vars
        if diagnosis.has_prologue_mismatch and diagnosis.gpr_save_delta > 0:
            return True

        # Load/store ordering
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("lwz", "stw") or d.base_opcode in ("lwz", "stw"):
                return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        base = 1.0 if self.relevant(diagnosis) else 0.0
        if diagnosis.has_prologue_mismatch and diagnosis.gpr_save_delta > 0:
            base = min(1.0, base + 0.3)
        return base

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        stmts = ctx.statements
        counter = 0

        # Find repeated subscript expressions
        subscript_groups = _find_repeated_subscripts(body, source)

        for subscript_text, nodes in subscript_groups.items():
            if counter >= 5:
                break

            if len(nodes) < 2:
                continue

            # Find the innermost compound_statement that contains ALL uses
            first_node = nodes[0]
            scope = _find_common_scope(nodes)
            if scope is None:
                scope = body

            # Find the first use's containing statement within that scope
            containing_stmt = _get_containing_stmt(first_node, scope)
            if containing_stmt is None:
                continue

            var_name = f"_sub{counter}".encode("utf-8")
            subscript_bytes = subscript_text.encode("utf-8")

            indent = get_indent(source, containing_stmt)
            line_start = get_line_start(source, containing_stmt)

            # auto& _sub0 = container[i];
            decl_line = indent + b"auto& " + var_name + b" = " + subscript_bytes + b";\n"

            ed = SourceEditor(source)
            ed.insert_at(line_start, decl_line)

            # Replace all uses (reverse order to preserve positions)
            sorted_nodes = sorted(nodes, key=lambda n: n.start_byte, reverse=True)
            for node in sorted_nodes:
                ed.replace_node(node, var_name)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            sub_str = subscript_text
            if len(sub_str) > 40:
                sub_str = sub_str[:37] + "..."
            yield Variant(
                name=f"subbind_{counter}",
                pattern_name=self.name,
                description=f"Bind {sub_str} ({len(nodes)} uses) to local ref {var_name.decode()}",
                source=new_source,
            )
            counter += 1


def _find_repeated_subscripts(body: Node, source: bytes) -> dict[str, list[Node]]:
    """Find subscript expressions used 2+ times with identical text.

    Only considers subscript expressions at the top level of the function body
    (not nested inside other subscript expressions). Groups by the full text
    of the subscript expression (e.g., "mDirs[i]").
    """
    uses: dict[str, list[Node]] = {}
    local_vars: set[str] = set()

    for node in walk(body):
        # Track local variable declarations to avoid binding to them
        if node.type == "declaration":
            for child in node.named_children:
                if child.type == "init_declarator":
                    decl = child.child_by_field_name("declarator")
                    if decl is not None:
                        name = _get_identifier_name(decl)
                        if name:
                            local_vars.add(name)

        if node.type == "subscript_expression":
            # Don't match subscripts that are nested inside other subscripts
            parent = node.parent
            if parent is not None and parent.type == "subscript_expression":
                continue

            text = source[node.start_byte:node.end_byte].decode("utf-8", errors="replace")

            # The subscript_argument_list must contain a simple expression
            # (identifier, number) to avoid aliasing issues
            sub_arg_list = None
            for child in node.named_children:
                if child.type == "subscript_argument_list":
                    sub_arg_list = child
                    break
            if sub_arg_list is not None:
                # Check the content of the subscript argument list
                inner_nodes = [c for c in sub_arg_list.named_children]
                if len(inner_nodes) != 1:
                    continue
                if inner_nodes[0].type not in ("identifier", "number_literal"):
                    continue

            # The array part should be an identifier or member access
            array_node = node.child_by_field_name("argument")
            if array_node is None:
                continue

            # Don't bind if the array is a local variable (only worth it for
            # member accesses and parameters)
            if array_node.type == "identifier" and array_node.text:
                array_name = array_node.text.decode("utf-8", errors="replace")
                if array_name in local_vars:
                    continue

            uses.setdefault(text, []).append(node)

    return {k: v for k, v in uses.items() if len(v) >= 2}


def _get_identifier_name(node: Node) -> str | None:
    """Extract identifier name from a declarator, unwrapping ref/ptr."""
    while node.type in ("reference_declarator", "pointer_declarator"):
        inner = node.child_by_field_name("declarator")
        if inner is None:
            inner = node.named_children[-1] if node.named_children else None
        if inner is None:
            break
        node = inner
    if node.type == "identifier" and node.text:
        return node.text.decode("utf-8", errors="replace")
    return None


def _find_common_scope(nodes: list[Node]) -> Node | None:
    """Find the innermost compound_statement that contains all nodes."""
    if not nodes:
        return None

    # Get ancestor chains for the first node
    def ancestors(n: Node) -> list[Node]:
        chain = []
        current = n.parent
        while current is not None:
            if current.type == "compound_statement":
                chain.append(current)
            current = current.parent
        return chain

    # Start with first node's ancestors
    common = ancestors(nodes[0])
    if not common:
        return None

    # Intersect with each other node's ancestors (by id)
    for node in nodes[1:]:
        other_ids = {a.id for a in ancestors(node)}
        common = [a for a in common if a.id in other_ids]

    # Return the innermost (first in the list, closest to the nodes)
    return common[0] if common else None


def _get_containing_stmt(node: Node, body: Node) -> Node | None:
    """Walk up from node to find the direct child statement of body."""
    current = node
    while current is not None:
        if current.parent is not None and current.parent.id == body.id:
            return current
        current = current.parent
    return None
