"""Loop condition caching/uncaching — toggle local caching of loop bounds.

Win rate: untested (new pattern).

When a loop condition reads a member through a pointer chain (e.g., mObj->mFrames),
the compiler re-loads through the pointer on each iteration. Caching the value in a
local variable eliminates the reload but changes register allocation. Sometimes the
target wants the uncached version (re-read each iteration), sometimes the cached version.

Transformations:
    Cache:
        while (i < mObj->mField) { ... }
        ->
        int _limit = mObj->mField;
        while (i < _limit) { ... }

    Uncache:
        int limit = mObj->mField;
        while (i < limit) { ... }
        ->
        while (i < mObj->mField) { ... }
        (removes the now-unused local)

Detection signals:
    - Callee-saved register swaps
    - Clusters (instruction reordering from different load patterns)
    - lwz load differences in loop back-edge regions
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, get_line_start, node_text
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Callee-saved GPR range
_CALLEE_SAVED_RE = re.compile(r"r(1[3-9]|2\d|3[01])")

# Member naming convention
_MEMBER_RE = re.compile(rb"^m[A-Z]")


class LoopConditionCachePattern(Pattern):
    name = "loop_condition_cache"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Callee-saved GPR swaps
        for (r1, r2) in diagnosis.reg_swap_pairs:
            if _CALLEE_SAVED_RE.match(r1) or _CALLEE_SAVED_RE.match(r2):
                return True

        # Clusters
        if diagnosis.clusters:
            return True

        # Prologue mismatch
        if diagnosis.has_prologue_mismatch:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        score = 0.0
        for (r1, r2) in diagnosis.reg_swap_pairs:
            if _CALLEE_SAVED_RE.match(r1) or _CALLEE_SAVED_RE.match(r2):
                score = max(score, 0.4)
        if diagnosis.clusters:
            score = max(score, 0.3)
        return score if score > 0 else 0.2

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        # Strategy 1: Cache member accesses in loop conditions
        for variant in _cache_loop_conditions(source, body, counter):
            yield variant
            counter += 1
            if counter >= 6:
                return

        # Strategy 2: Uncache locals used only as loop bounds
        for variant in _uncache_loop_conditions(ctx, source, body, counter):
            yield variant
            counter += 1
            if counter >= 6:
                return


def _cache_loop_conditions(
    source: bytes, body: Node, counter: int
) -> Iterator[Variant]:
    """Find loops with member access in condition and cache to local."""
    for loop in _find_loops(body):
        cond_expr = _get_loop_condition(loop, source)
        if cond_expr is None:
            continue

        # Find member accesses in the condition (field expressions with ->)
        member_accesses = _find_member_accesses_in(cond_expr, source)
        for access_node in member_accesses:
            if counter >= 6:
                return

            access_text = node_text(source, access_node)

            # Skip simple identifiers — we want ptr->field or obj.field chains
            if b"->" not in access_text and b"." not in access_text:
                continue

            # Generate a variable name from the field
            var_name = _make_cache_name(access_node, source, counter)

            # Insert declaration before the loop
            indent = get_indent(source, loop)
            line_start = get_line_start(source, loop)
            decl = indent + b"int " + var_name + b" = " + access_text + b";\n"

            ed = SourceEditor(source)
            ed.insert_at(line_start, decl)

            # Replace the access in the condition with the variable
            ed.replace_node(access_node, var_name)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            access_str = access_text.decode("utf-8", errors="replace")
            yield Variant(
                name=f"loopcache_{counter}",
                pattern_name="loop_condition_cache",
                description=f"Cache loop bound {access_str} to local",
                source=new_source,
            )
            counter += 1


def _uncache_loop_conditions(
    ctx: FunctionContext, source: bytes, body: Node, counter: int
) -> Iterator[Variant]:
    """Find locals used only as loop bounds and inline them back."""
    # Find local variable declarations
    for stmt in body.named_children:
        if stmt.type != "declaration":
            continue

        decl = stmt.child_by_field_name("declarator")
        if decl is None:
            continue

        # Get the init declarator
        init_decl = None
        for child in stmt.named_children:
            if child.type == "init_declarator":
                init_decl = child
                break
        if init_decl is None:
            if decl.type == "init_declarator":
                init_decl = decl
            else:
                continue

        # Get the initializer value
        value_node = init_decl.child_by_field_name("value")
        name_node = init_decl.child_by_field_name("declarator")
        if value_node is None or name_node is None:
            continue

        value_text = node_text(source, value_node)
        # Must be a member access
        if b"->" not in value_text and b"." not in value_text:
            continue

        if name_node.type != "identifier" or name_node.text is None:
            continue
        var_name = name_node.text

        # Check if next sibling is a loop that uses this variable in its condition
        stmt_idx = None
        for i, child in enumerate(body.named_children):
            if child.id == stmt.id:
                stmt_idx = i
                break
        if stmt_idx is None:
            continue

        # Look at the next statement(s)
        for j in range(stmt_idx + 1, len(body.named_children)):
            next_stmt = body.named_children[j]
            if next_stmt.type not in ("for_statement", "while_statement", "do_statement"):
                break

            cond = _get_loop_condition(next_stmt, source)
            if cond is None:
                break

            # Check if var_name is used in the condition
            uses_in_cond = _find_identifier_in(cond, var_name)
            if not uses_in_cond:
                break

            # Check this variable isn't used elsewhere in the loop body
            loop_body = _get_loop_body(next_stmt)
            if loop_body is not None:
                uses_in_body = _find_identifier_in(loop_body, var_name)
                if uses_in_body:
                    break  # Used in body too, don't uncache

            ed = SourceEditor(source)

            # Remove the declaration line
            decl_line_start = get_line_start(source, stmt)
            # Find end of line (including newline)
            decl_line_end = stmt.end_byte
            while decl_line_end < len(source) and source[decl_line_end:decl_line_end + 1] in (b"\n", b"\r"):
                decl_line_end += 1
            ed.delete_range(decl_line_start, decl_line_end)

            # Replace uses in condition with the original expression
            for use in sorted(uses_in_cond, key=lambda n: n.start_byte, reverse=True):
                ed.replace_node(use, value_text)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            var_str = var_name.decode("utf-8", errors="replace")
            yield Variant(
                name=f"loopuncache_{counter}",
                pattern_name="loop_condition_cache",
                description=f"Uncache loop bound {var_str} back to member access",
                source=new_source,
            )
            counter += 1
            break  # Only one uncache per declaration


def _find_loops(body: Node) -> Iterator[Node]:
    """Find all loop nodes in the function body."""
    for node in walk(body):
        if node.type in ("for_statement", "while_statement", "do_statement"):
            yield node


def _get_loop_condition(loop: Node, source: bytes) -> Node | None:
    """Extract the condition expression from a loop."""
    if loop.type == "while_statement":
        cond = loop.child_by_field_name("condition")
        if cond is not None and cond.type == "parenthesized_expression":
            children = cond.named_children
            return children[0] if children else None
        return cond

    elif loop.type == "for_statement":
        # For loops: condition is the second semicolon-separated part
        cond = loop.child_by_field_name("condition")
        return cond

    elif loop.type == "do_statement":
        cond = loop.child_by_field_name("condition")
        if cond is not None and cond.type == "parenthesized_expression":
            children = cond.named_children
            return children[0] if children else None
        return cond

    return None


def _get_loop_body(loop: Node) -> Node | None:
    """Get the body compound_statement of a loop."""
    return loop.child_by_field_name("body")


def _find_member_accesses_in(node: Node, source: bytes) -> list[Node]:
    """Find field_expression nodes with -> or . in subtree.

    Only matches actual field accesses (ptr->mField), NOT method calls
    (obj.size()). Method calls show up as call_expression with a
    field_expression as the function — we skip those.
    """
    results = []
    for n in walk(node):
        if n.type != "field_expression":
            continue

        text = node_text(source, n)
        if b"->" not in text and b"." not in text:
            continue

        # Skip if this field_expression is the function of a call_expression
        # (i.e., obj.size() or ptr->begin() — these are method calls)
        parent = n.parent
        if parent and parent.type == "call_expression":
            func = parent.child_by_field_name("function")
            if func and func.id == n.id:
                continue  # This is a method call, not a field access

        # Don't include if parent is also a field_expression argument
        # (we want the outermost chain)
        if parent and parent.type == "field_expression":
            parent_arg = parent.child_by_field_name("argument")
            if parent_arg and parent_arg.id == n.id:
                continue  # Part of a longer chain

        results.append(n)
    return results


def _find_identifier_in(node: Node, name: bytes) -> list[Node]:
    """Find all uses of an identifier in a subtree."""
    results = []
    for n in walk(node):
        if n.type == "identifier" and n.text == name:
            results.append(n)
    return results


def _make_cache_name(access_node: Node, source: bytes, counter: int) -> bytes:
    """Generate a cache variable name from a member access."""
    field = access_node.child_by_field_name("field")
    if field is not None:
        field_text = node_text(source, field)
        if _MEMBER_RE.match(field_text):
            short = field_text[1:2].lower() + field_text[2:]
            return b"_" + short
    return b"_limit" + str(counter).encode()
