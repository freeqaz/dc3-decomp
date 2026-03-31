"""Variable inline pattern — substitute single-assignment locals at use sites.

Inverse of variable_extraction. Finds local variables assigned exactly once
and used 1-3 times, then substitutes the defining expression at each use site
and removes the declaration.

This is a proven last-mile fix pattern — removing a named local reduces
register pressure when the compiler would otherwise cache the value in a
callee-saved register.

Example:
    float crossFader = mCrossFader->CrossFade();
    ... use crossFader ...
    ->
    ... use mCrossFader->CrossFade() ...
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import identifiers_in
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


# Maximum uses to substitute (beyond this, inlining bloats the code)
_MAX_USES = 3


class VariableInlinePattern(Pattern):
    name = "variable_inline"
    safety_tier = "moderate"
    structural_domain = "data_flow"
    follow_ups = ("declaration_reorder", "statement_reorder")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # GPR register swaps can be fixed by removing cached locals
        for (r0, r1) in diagnosis.reg_swap_pairs:
            if r0.startswith("r") or r1.startswith("r"):
                return True
        # Prologue mismatch where we have too many callee-saved regs
        if diagnosis.has_prologue_mismatch and diagnosis.gpr_save_delta < 0:
            return True
        # Offset deltas (caching address vs value changes offsets)
        if len(diagnosis.offset_deltas) > 0:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        base = 1.0 if self.relevant(diagnosis) else 0.0
        # Boost when prologue shows we have too many callee-saved regs
        if diagnosis.has_prologue_mismatch and diagnosis.gpr_save_delta < 0:
            base = min(1.0, base + 0.3)
        return base

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        stmts = ctx.statements
        if len(stmts) < 2:
            return

        counter = 0
        for decl_idx, info in _find_inlinable_decls(stmts, ctx.file_source):
            # Region filter
            decl = stmts[decl_idx]
            if not ctx.node_in_mismatch_region(decl):
                continue

            var_name = info["name"]
            init_expr = info["init_text"]
            use_indices = info["use_indices"]
            use_nodes = info["use_nodes"]

            # Build source with declaration removed and uses substituted
            ed = SourceEditor(ctx.file_source)

            # Remove the declaration line
            line_start = _line_start(ctx.file_source, decl.start_byte)
            line_end = _line_end(ctx.file_source, decl.end_byte)
            ed.delete_range(line_start, line_end)

            # Replace each use of the variable with the init expression
            # Wrap in parens if the init expr contains operators (to preserve precedence)
            needs_parens = _needs_parens(init_expr)
            replacement = (b"(" + init_expr + b")") if needs_parens else init_expr

            for use_node in use_nodes:
                ed.replace_node(use_node, replacement)

            new_source = ed.apply()
            if new_source == ctx.file_source:
                continue

            n_uses = len(use_nodes)
            yield Variant(
                name=f"varinline_{counter}",
                pattern_name=self.name,
                description=(
                    f"Inline '{var_name}' ({n_uses} use{'s' if n_uses > 1 else ''}) "
                    f"= {init_expr.decode('utf-8', errors='replace')[:40]}"
                ),
                source=new_source,
                tags=frozenset({"inlined_variable"}),
            )
            counter += 1


def _find_inlinable_decls(
    stmts: list[Node], source: bytes
) -> list[tuple[int, dict]]:
    """Find declarations suitable for inlining.

    Criteria:
    - Single variable declared with an initializer
    - Variable used 1-3 times after declaration
    - Initializer is a single expression (call, member access, etc.)
    - No address-of (&var) usage
    """
    results = []

    for i, stmt in enumerate(stmts):
        if stmt.type != "declaration":
            continue

        declarator = stmt.child_by_field_name("declarator")
        if declarator is None or declarator.type != "init_declarator":
            continue

        # Get the variable name
        name_node = declarator.child_by_field_name("declarator")
        if name_node is None:
            continue
        # Unwrap pointer/reference declarators
        while name_node.type in ("pointer_declarator", "reference_declarator"):
            inner = name_node.child_by_field_name("declarator")
            if inner is not None:
                name_node = inner
            else:
                break

        var_name = source[name_node.start_byte : name_node.end_byte].decode(
            "utf-8", errors="replace"
        )

        # Get the initializer expression
        value = declarator.child_by_field_name("value")
        if value is None:
            continue

        # Skip complex initializers (initializer lists, lambda, etc.)
        if value.type in ("initializer_list", "lambda_expression", "compound_literal_expression"):
            continue

        init_text = source[value.start_byte : value.end_byte]

        # Find all uses of this variable in subsequent statements
        use_indices = []
        use_nodes_list = []

        for j in range(i + 1, len(stmts)):
            nodes = _find_identifier_nodes(stmts[j], var_name, source)
            if nodes:
                use_indices.append(j)
                use_nodes_list.extend(nodes)

        # Check usage count
        if len(use_nodes_list) < 1 or len(use_nodes_list) > _MAX_USES:
            continue

        # Check no address-of usage
        if _has_address_of(stmts[i + 1:], var_name, source):
            continue

        # Check the variable isn't reassigned after declaration
        if _is_reassigned(stmts[i + 1:], var_name, source):
            continue

        results.append((i, {
            "name": var_name,
            "init_text": init_text,
            "use_indices": use_indices,
            "use_nodes": use_nodes_list,
        }))

    return results


def _find_identifier_nodes(node: Node, name: str, source: bytes) -> list[Node]:
    """Find all identifier nodes matching `name` in the subtree."""
    results = []
    if node.type == "identifier":
        text = source[node.start_byte : node.end_byte].decode("utf-8", errors="replace")
        if text == name:
            results.append(node)
    for child in node.children:
        results.extend(_find_identifier_nodes(child, name, source))
    return results


def _has_address_of(stmts: list[Node], name: str, source: bytes) -> bool:
    """Check if any statement takes the address of the variable (&var)."""
    for stmt in stmts:
        text = source[stmt.start_byte : stmt.end_byte].decode("utf-8", errors="replace")
        if f"&{name}" in text:
            return True
    return False


def _is_reassigned(stmts: list[Node], name: str, source: bytes) -> bool:
    """Check if the variable is assigned to (other than the initial declaration)."""
    for stmt in stmts:
        # Quick text check for "name =" or "name +=" etc.
        text = source[stmt.start_byte : stmt.end_byte].decode("utf-8", errors="replace")
        # Look for assignment patterns
        import re
        if re.search(rf'\b{re.escape(name)}\s*[+\-*/&|^]?=', text):
            # But not "== name" (comparison)
            if not re.search(rf'\b{re.escape(name)}\s*==', text):
                return True
        # Also check prefix/postfix ++/--
        if f"++{name}" in text or f"{name}++" in text:
            return True
        if f"--{name}" in text or f"{name}--" in text:
            return True
    return False


def _needs_parens(expr: bytes) -> bool:
    """Check if an expression needs parentheses when substituted.

    Simple calls and identifiers don't need parens. Expressions with
    operators (binary, ternary, comma) do.
    """
    text = expr.decode("utf-8", errors="replace").strip()
    # Simple cases that don't need parens
    if text.isidentifier():
        return False
    # Function call: ends with ")" and has matching "("
    if text.endswith(")"):
        depth = 0
        for ch in text:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
        if depth == 0:
            return False
    # Member access chains don't need parens
    if "->" in text or "." in text:
        # Check no binary operators at top level
        depth = 0
        for ch in text:
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
            elif depth == 0 and ch in "+-*/%&|^<>?:,":
                return True
        return False
    return True


def _line_start(source: bytes, pos: int) -> int:
    """Find start of line containing pos."""
    idx = pos
    while idx > 0 and source[idx - 1:idx] != b"\n":
        idx -= 1
    return idx


def _line_end(source: bytes, pos: int) -> int:
    """Find end of line containing pos (including newline)."""
    idx = pos
    while idx < len(source) and source[idx:idx + 1] != b"\n":
        idx += 1
    if idx < len(source):
        idx += 1  # Include the newline
    return idx
