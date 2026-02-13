"""Variable extraction pattern — extract inline calls into auto locals.

Win rate: ~42% from attempt database.

Finds call_expression nodes nested inside argument_list, binary_expression,
or condition_clause at depth > 1. Extracts each into an `auto` local variable
declared before the containing statement.

Example:
    MILO_ASSERT(display < mElements.size(), 0x74);
    ->
    auto _tmp0 = mElements.size();
    MILO_ASSERT(display < _tmp0, 0x74);
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent, get_line_start
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Node types that indicate a call is nested (not a standalone expression_statement)
_NESTING_TYPES = {
    "argument_list",
    "binary_expression",
    "condition_clause",
    "parenthesized_expression",
    "assignment_expression",
    "return_statement",
}


class VariableExtractionPattern(Pattern):
    name = "variable_extraction"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Skip when there are no actionable mismatches (pure noise/unfixable)
        if diagnosis.diff_ops:
            return True
        if diagnosis.clusters:
            return True
        if diagnosis.replace_real > 0:
            return True
        # GPR swaps can sometimes be fixed by variable extraction changing alloc order
        if any(p[0].startswith("r") or p[1].startswith("r")
               for p in diagnosis.reg_swap_pairs):
            return True
        # Unexplained diff_arg might respond to extraction
        unexplained = diagnosis.noise_total - diagnosis.noise_explained
        if unexplained > 0:
            return True
        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        # Walk all compound_statements to find extractable calls in their direct children
        for compound, stmt, call_node in _find_extractable_calls(ctx.body_node):
            call_text = ctx.file_source[call_node.start_byte : call_node.end_byte]

            indent = get_indent(ctx.file_source, stmt)
            line_start = get_line_start(ctx.file_source, stmt)
            var_name = f"_tmp{counter}".encode("utf-8")
            counter += 1

            # Build the declaration line
            decl_line = indent + b"auto " + var_name + b" = " + call_text + b";\n"

            # Use SourceEditor: insert decl at line start, replace call with var_name
            ed = SourceEditor(ctx.file_source)
            ed.insert_at(line_start, decl_line)
            ed.replace_node(call_node, var_name)
            new_source = ed.apply()

            desc = (
                f"Extract '{call_text.decode('utf-8', errors='replace')}' "
                f"into auto {var_name.decode()}"
            )
            yield Variant(
                name=f"varext_{counter - 1}",
                pattern_name=self.name,
                description=desc,
                source=new_source,
            )


def _call_priority(call_node: Node) -> int:
    """Score extraction priority for a call node (higher = better candidate).

    Data shows wins come from method chains and complex getter calls,
    not simple expressions. Prioritize accordingly.
    """
    score = 0

    # Method chain: a->b()->c() — high value
    func = call_node.child_by_field_name("function")
    if func is not None and func.type == "field_expression":
        arg = func.child_by_field_name("argument")
        if arg is not None and arg.type == "call_expression":
            score += 30  # Method chain like a->Foo()->Bar()

    # Nested call: f(g(x)) — the inner g(x) is high value
    parent = call_node.parent
    if parent is not None and parent.type == "argument_list":
        score += 20  # Call used as argument to another call

    # Call with arguments (more complex = more likely to benefit)
    args = call_node.child_by_field_name("arguments")
    if args is not None:
        n_args = len(args.named_children)
        score += min(n_args * 5, 15)

    # Arithmetic context: call inside binary_expression
    if parent is not None and parent.type == "binary_expression":
        score += 10

    # Simple getter with no args — lower priority
    if args is not None and len(args.named_children) == 0:
        # Still potentially useful but lower priority
        score += 5

    return score


def _find_extractable_calls(
    body_node: Node,
) -> Iterator[tuple[Node, Node, Node]]:
    """Find (compound_statement, containing_statement, call_node) tuples.

    Walks all compound_statements (function body, loop bodies, if/else bodies)
    and for each direct child statement, finds nested call expressions that
    can be extracted to a variable before that statement.

    Results are sorted by priority (highest first) so the max_variants cap
    keeps the best candidates.
    """
    candidates: list[tuple[int, Node, Node, Node]] = []

    for stmt in body_node.named_children:
        for call_node in _find_nested_calls(stmt):
            pri = _call_priority(call_node)
            candidates.append((pri, body_node, stmt, call_node))

        for compound in _find_compound_children(stmt):
            for pri, comp, st, cn in _find_extractable_calls_scored(compound):
                candidates.append((pri, comp, st, cn))

    # Sort by priority descending
    candidates.sort(key=lambda x: x[0], reverse=True)
    for _, comp, st, cn in candidates:
        yield comp, st, cn


def _find_extractable_calls_scored(
    body_node: Node,
) -> list[tuple[int, Node, Node, Node]]:
    """Internal scored version for recursion."""
    candidates: list[tuple[int, Node, Node, Node]] = []
    for stmt in body_node.named_children:
        for call_node in _find_nested_calls(stmt):
            pri = _call_priority(call_node)
            candidates.append((pri, body_node, stmt, call_node))
        for compound in _find_compound_children(stmt):
            candidates.extend(_find_extractable_calls_scored(compound))
    return candidates


def _find_compound_children(node: Node) -> Iterator[Node]:
    """Find compound_statement children (loop/if/else bodies)."""
    for child in node.children:
        if child.type == "compound_statement":
            yield child
        elif child.type in ("if_statement", "else_clause", "for_statement",
                            "while_statement", "do_statement", "switch_statement"):
            yield from _find_compound_children(child)


def _find_nested_calls(node: Node, depth: int = 0) -> Iterator[Node]:
    """Find call_expression nodes nested inside other expressions.

    Only yields calls where the call is inside a nesting context (argument,
    binary expression, condition), not standalone call statements.
    Does NOT recurse into compound_statement children (those are handled
    by _find_extractable_calls to maintain proper scoping).
    """
    if node.type == "call_expression" and depth > 0:
        parent = node.parent
        if parent is not None and parent.type in _NESTING_TYPES:
            yield node
            return  # Don't recurse deeper into this call's own args

    next_depth = depth
    if node.type in _NESTING_TYPES or node.type == "call_expression":
        next_depth = depth + 1

    for child in node.children:
        # Don't cross compound_statement boundaries — inner scopes
        # are handled by _find_extractable_calls recursion
        if child.type == "compound_statement":
            continue
        yield from _find_nested_calls(child, next_depth)
