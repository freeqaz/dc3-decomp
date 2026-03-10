"""Iterator index comparison — convert direct iterator compare to index-based.

When comparing iterators from the same container, `it1 < it2` (direct pointer
compare) generates different code than `(it1 - c.begin()) < (it2 - c.begin())`
(index-based compare).

MSVC PPC codegen difference:
    it1 < it2           -> subfc + subfe (2 instr, unsigned pointer compare)
    (it1-begin)<(it2-begin) -> subf + clrrwi + subfc + eqv + srwi + addze
                               (signed index compare with alignment mask)

This pattern is critical for comparator functors (e.g., VectorSort) because
when the body is visible, MSVC inlines it into ALL STL sort templates. A wrong
comparator body cascades to 7-14 sort template regressions.

Detection signals:
    - clrrwi in target delete clusters (alignment masking on ptr difference)
    - eqv + srwi + addze sequence (signed index comparison)
    - subfe in base (simpler unsigned pointer compare)

Proven example:
    VectorSort<RndMesh*>::operator() — 60.5% -> 100%, plus 14 sort templates
    recovered to 100%.
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, find_comparisons, node_text
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Comparison operators that make sense for iterator ordering
_ORDER_OPS = {"<", ">", "<=", ">="}


class IteratorIndexComparePattern(Pattern):
    name = "iterator_index_compare"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Look for clrrwi in delete clusters (target has alignment masking)
        for cluster in diagnosis.clusters:
            if cluster.deletes > 0:
                return True
        # Also relevant if we see subfe vs eqv replace
        for d in diagnosis.diff_ops:
            if d.base_opcode == "subfe" or d.target_opcode == "eqv":
                return True
            if d.target_opcode == "clrrwi" or d.base_opcode == "subfe":
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Boost if we see clrrwi or eqv in the diagnosis
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("clrrwi", "eqv"):
                return 0.6
        return 0.2

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node

        # Strategy 1: Find std::find assignments and their comparisons
        yield from _find_and_transform_find_pattern(body, source, ctx)

        # Strategy 2: Find any iterator-like comparison with a known container
        yield from _find_and_transform_generic_iter_compare(body, source, ctx)


def _find_and_transform_find_pattern(
    body: Node, source: bytes, ctx: FunctionContext
) -> Iterator[Variant]:
    """Find std::find(...) assignments and transform their comparisons.

    Pattern:
        auto it1 = std::find(vec.begin(), vec.end(), x);
        auto it2 = std::find(vec.begin(), vec.end(), y);
        return it1 < it2;
    ->
        return (it1 - vec.begin()) < (it2 - vec.begin());
    """
    # Collect iterator variable -> container.begin() mapping
    # by scanning for std::find(container.begin(), ...) assignments
    iter_to_begin: dict[str, bytes] = {}

    for node in walk(body):
        # Look for declarations: Type it = std::find(c.begin(), c.end(), val)
        if node.type in ("declaration", "init_declarator"):
            _extract_find_assignment(node, source, iter_to_begin)
        # Also look for assignment expressions: it = std::find(...)
        if node.type == "assignment_expression":
            _extract_find_assignment(node, source, iter_to_begin)

    if len(iter_to_begin) < 2:
        return

    # Now find comparisons between two tracked iterators
    counter = 0
    for cmp_node in find_comparisons(body, ops=_ORDER_OPS):
        left = cmp_node.child_by_field_name("left")
        right = cmp_node.child_by_field_name("right")
        if left is None or right is None:
            continue

        left_text = node_text(source, left).strip()
        right_text = node_text(source, right).strip()

        left_name = left_text.decode("utf-8", errors="replace")
        right_name = right_text.decode("utf-8", errors="replace")

        if left_name not in iter_to_begin or right_name not in iter_to_begin:
            continue

        begin_left = iter_to_begin[left_name]
        begin_right = iter_to_begin[right_name]

        op_node = cmp_node.child_by_field_name("operator")
        if op_node is None:
            continue
        op_text = node_text(source, op_node)

        # Build replacement: (it1 - container.begin()) < (it2 - container.begin())
        new_expr = (
            b"(" + left_text + b" - " + begin_left + b") "
            + op_text
            + b" (" + right_text + b" - " + begin_right + b")"
        )

        ed = SourceEditor(source)
        ed.replace_node(cmp_node, new_expr)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        yield Variant(
            name=f"itidx_{counter}",
            pattern_name="iterator_index_compare",
            description=(
                f"Iterator index compare: {left_name} {op_text.decode()} {right_name} "
                f"-> index subtraction"
            ),
            source=new_source,
        )
        counter += 1
        if counter >= 4:
            return


def _extract_find_assignment(
    node: Node, source: bytes, iter_to_begin: dict[str, bytes]
) -> None:
    """Extract iterator name and container.begin() from a std::find assignment.

    Handles:
        Type it = std::find(container.begin(), container.end(), val);
        it = std::find(container.begin(), container.end(), val);
    """
    # Find the call expression containing std::find
    call = _find_child_call(node, source, b"find")
    if call is None:
        return

    # Get the iterator variable name
    var_name = _get_assigned_var_name(node, source)
    if var_name is None:
        return

    # Get the first argument (container.begin())
    args = call.child_by_field_name("arguments")
    if args is None:
        return

    named_args = args.named_children
    if len(named_args) < 2:
        return

    begin_expr = node_text(source, named_args[0]).strip()
    iter_to_begin[var_name] = begin_expr


def _find_child_call(node: Node, source: bytes, func_name: bytes) -> Node | None:
    """Find a call_expression in node's subtree where the function name contains func_name."""
    for child in walk(node):
        if child.type != "call_expression":
            continue
        func = child.child_by_field_name("function")
        if func is None:
            continue
        func_text = node_text(source, func)
        if func_name in func_text:
            return child
    return None


def _get_assigned_var_name(node: Node, source: bytes) -> str | None:
    """Get the variable name being assigned to in a declaration or assignment."""
    if node.type == "declaration":
        # Look for init_declarator child
        for child in node.named_children:
            if child.type == "init_declarator":
                declarator = child.child_by_field_name("declarator")
                if declarator is not None:
                    return node_text(source, declarator).decode("utf-8", errors="replace").strip()
    elif node.type == "init_declarator":
        declarator = node.child_by_field_name("declarator")
        if declarator is not None:
            return node_text(source, declarator).decode("utf-8", errors="replace").strip()
    elif node.type == "assignment_expression":
        left = node.child_by_field_name("left")
        if left is not None:
            return node_text(source, left).decode("utf-8", errors="replace").strip()
    return None


def _find_and_transform_generic_iter_compare(
    body: Node, source: bytes, ctx: FunctionContext
) -> Iterator[Variant]:
    """Find iterator-like comparisons and try index-based rewrites.

    More aggressive: looks for comparisons where both operands are simple
    identifiers with iterator-like names, and searches for a .begin() call
    on a container used nearby.
    """
    # Collect all .begin() expressions used in the function body
    begin_exprs: dict[str, bytes] = {}  # container_text -> container.begin()
    for node in walk(body):
        if node.type != "call_expression":
            continue
        func = node.child_by_field_name("function")
        if func is None or func.type != "field_expression":
            continue
        field = func.child_by_field_name("field")
        obj = func.child_by_field_name("argument")
        if field is None or obj is None:
            continue
        if node_text(source, field) != b"begin":
            continue
        obj_text = node_text(source, obj).decode("utf-8", errors="replace")
        begin_call = node_text(source, node)
        begin_exprs[obj_text] = begin_call

    if not begin_exprs:
        return

    counter = 0
    for cmp_node in find_comparisons(body, ops=_ORDER_OPS):
        left = cmp_node.child_by_field_name("left")
        right = cmp_node.child_by_field_name("right")
        if left is None or right is None:
            continue

        # Only match simple identifiers
        if left.type != "identifier" or right.type != "identifier":
            continue

        left_text = node_text(source, left)
        right_text = node_text(source, right)
        left_name = left_text.decode("utf-8", errors="replace")
        right_name = right_text.decode("utf-8", errors="replace")

        # Check if both look like iterator names
        if not (_looks_like_iterator(left_name) and _looks_like_iterator(right_name)):
            continue

        op_node = cmp_node.child_by_field_name("operator")
        if op_node is None:
            continue
        op_text = node_text(source, op_node)

        # Try each known container's begin()
        for container_name, begin_call in begin_exprs.items():
            new_expr = (
                b"(" + left_text + b" - " + begin_call + b") "
                + op_text
                + b" (" + right_text + b" - " + begin_call + b")"
            )

            ed = SourceEditor(source)
            ed.replace_node(cmp_node, new_expr)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            yield Variant(
                name=f"itidx_gen_{counter}",
                pattern_name="iterator_index_compare",
                description=(
                    f"Generic iterator index compare: "
                    f"{left_name} {op_text.decode()} {right_name} "
                    f"via {container_name}.begin()"
                ),
                source=new_source,
            )
            counter += 1
            if counter >= 4:
                return


def _looks_like_iterator(name: str) -> bool:
    """Heuristic: does this variable name look like an iterator?"""
    if name in ("it", "iter", "itr", "i", "j"):
        return True
    if re.match(r"^it\d*$", name):
        return True
    if re.match(r"^it[A-Z_]", name):
        return True
    if "iter" in name.lower() or "itr" in name.lower():
        return True
    return False
