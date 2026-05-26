"""Hoist repeated identical call expressions into a local and replace all occurrences.

Win rate: untested (new pattern, proven in AccomplishmentPanel x2 fns 98.x->100%).

When the SAME side-effect-free call expression (e.g. `v.end()`, `Foo()`) appears
2+ times within a single statement, hoist it into a local `<type> _e = v.end();`
and replace all occurrences.  Eliminates a recomputed call instruction cluster.

Classic trigger:
    MILO_ASSERT(std::find(v.begin(), v.end(), x) == v.end(), line);
    ->
    auto _e = v.end();
    MILO_ASSERT(std::find(v.begin(), _e, x) == _e, line);

`variable_extraction` extracts ONE call at a time (any nested call).  This
pattern is complementary: it ONLY fires when the same expression text appears
2+ times and replaces ALL occurrences simultaneously, which `variable_extraction`
never does in a single edit.

Detection signals:
    - Clusters (extra call overhead)
    - replace_real > 0 (extra bl to end() in target)
    - bl mismatches (extra call target)
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, get_line_start
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


class CacheRepeatedCallPattern(Pattern):
    name = "cache_repeated_call"
    follow_ups = ("temp_elimination",)

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Extra bl (call overhead) or clusters caused by recomputed call
        if diagnosis.clusters:
            return True
        if diagnosis.replace_real > 0:
            return True
        for d in diagnosis.diff_ops:
            if d.target_opcode == "bl" or d.base_opcode == "bl":
                return True
        # Unexplained extra instructions
        unexplained = diagnosis.noise_total - diagnosis.noise_explained
        if unexplained > 2:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Clusters strongly suggest repeated-call overhead
        if diagnosis.clusters:
            return 0.7
        if diagnosis.replace_real > 0:
            return 0.5
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        stmts = ctx.statements
        counter = 0

        # Build set of already-used temp names to avoid collisions
        used_names: set[str] = set()
        source_text = source.decode("utf-8", errors="replace")

        for stmt in stmts:
            if counter >= 8:
                break

            # Walk nested compound_statements too
            for body_stmt in _iter_statements_in_subtree(stmt):
                if counter >= 8:
                    break

                # Find groups of identical call expressions within this statement
                groups = _find_repeated_calls(body_stmt, source)
                for call_text_bytes, nodes in groups:
                    if counter >= 8:
                        break
                    if len(nodes) < 2:
                        continue

                    call_str = call_text_bytes.decode("utf-8", errors="replace")
                    # Skip very short calls that are trivially cheap (e.g. bare `x()`)
                    if len(call_str) < 4:
                        continue

                    # Find the containing top-level statement for insertion
                    top_stmt = _get_top_stmt(nodes[0], ctx.body_node) or body_stmt
                    indent = get_indent(source, top_stmt)
                    line_start = get_line_start(source, top_stmt)

                    # Pick a unique temp name
                    var_name_str = _unique_tmp_name(counter, source_text, used_names)
                    used_names.add(var_name_str)
                    var_name = var_name_str.encode("utf-8")

                    # Build declaration: auto _e0 = v.end();
                    decl_line = indent + b"auto " + var_name + b" = " + call_text_bytes + b";\n"

                    ed = SourceEditor(source)
                    ed.insert_at(line_start, decl_line)

                    # Replace all occurrences (reverse order for stable offsets)
                    for n in sorted(nodes, key=lambda x: x.start_byte, reverse=True):
                        ed.replace_node(n, var_name)

                    try:
                        new_source = ed.apply()
                    except ValueError:
                        continue

                    if len(call_str) > 40:
                        call_str_short = call_str[:37] + "..."
                    else:
                        call_str_short = call_str

                    yield Variant(
                        name=f"cachercall_{counter}",
                        pattern_name=self.name,
                        description=(
                            f"Cache repeated call '{call_str_short}' "
                            f"({len(nodes)}x) into {var_name_str}"
                        ),
                        source=new_source,
                        tags=frozenset({"introduced_temp"}),
                    )
                    counter += 1


def _iter_statements_in_subtree(node: Node) -> Iterator[Node]:
    """Yield the node itself plus all compound_statement children recursively."""
    yield node
    for child in node.children:
        if child.type == "compound_statement":
            for sub in child.named_children:
                yield from _iter_statements_in_subtree(sub)
        elif child.type in (
            "if_statement", "else_clause", "for_statement",
            "while_statement", "do_statement",
        ):
            for sub in _iter_statements_in_subtree(child):
                yield sub


def _find_repeated_calls(
    stmt: Node, source: bytes
) -> list[tuple[bytes, list[Node]]]:
    """Find groups of call_expression nodes with identical source text.

    Returns [(call_text_bytes, [node, node, ...]), ...] for calls with count >= 2.
    Only considers calls that are likely side-effect-free (no assignments in args).
    """
    # Collect ALL call_expression nodes in this statement (not crossing compound boundaries)
    all_calls = _collect_calls_no_cross(stmt)

    # Group by source text
    by_text: dict[bytes, list[Node]] = {}
    for call_node in all_calls:
        text = source[call_node.start_byte:call_node.end_byte]
        # Skip calls with side-effect args (assignments, update expressions)
        if _has_side_effect_args(call_node):
            continue
        by_text.setdefault(text, []).append(call_node)

    # Return groups with 2+ occurrences, sorted by count descending
    result = [(t, ns) for t, ns in by_text.items() if len(ns) >= 2]
    result.sort(key=lambda x: -len(x[1]))
    return result


def _collect_calls_no_cross(node: Node) -> list[Node]:
    """Collect call_expression nodes without crossing compound_statement boundaries."""
    results = []
    for child in node.children:
        if child.type == "compound_statement":
            continue
        if child.type == "call_expression":
            results.append(child)
        results.extend(_collect_calls_no_cross(child))
    return results


def _has_side_effect_args(call_node: Node) -> bool:
    """Return True if any argument contains an assignment or update expression."""
    args = call_node.child_by_field_name("arguments")
    if args is None:
        return False
    for node in walk(args):
        if node.type in ("assignment_expression", "update_expression",
                          "compound_assignment_expr"):
            return True
    return False


def _get_top_stmt(node: Node, body: Node) -> Node | None:
    """Walk up from node to find the direct child statement of body."""
    current = node
    while current is not None:
        if current.parent is not None and current.parent.id == body.id:
            return current
        current = current.parent
    return None


def _unique_tmp_name(start: int, source_text: str, used_names: set[str]) -> str:
    """Return a ``_e<N>`` name not present in source_text or used_names."""
    n = start
    while True:
        candidate = f"_e{n}"
        if candidate not in used_names and not re.search(
            rf"\b{re.escape(candidate)}\b", source_text
        ):
            return candidate
        n += 1
