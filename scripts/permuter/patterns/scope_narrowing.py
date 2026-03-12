"""Scope narrowing pattern — move declarations into narrower scopes.

Win rate: untested (new pattern, based on UIListDir::DrawWidgets 71.8->100% fix).

Moves variable declarations from an outer scope into a narrower one (if-body,
else-body, loop body, compound block) when the variable is only used inside
that narrower scope. This changes when the compiler "sees" a variable in the
linear scan register allocator, affecting callee-saved assignment order.

Example (into-if):
    bool isFocused = GetFocused();
    for (...) {
        if (cond) {
            Use(isFocused);
        }
    }
    ->
    for (...) {
        if (cond) {
            bool isFocused = GetFocused();
            Use(isFocused);
        }
    }

Transformations:
- into-if: Move declaration into if-body when only used there
- into-else: Move declaration into else-body when only used there
- into-loop: Move declaration into loop body when re-assigned each iteration
- into-block: Move declaration into a nested compound statement

Safety checks:
- All uses must be within the target scope (no uses outside)
- Variable must not be used in the scope condition itself
- Variable must not be address-taken (&var)
- Side-effectful initializers are allowed (moved, not duplicated)
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import identifiers_in, get_indent, get_line_start, walk
from ..types import Diagnosis, FunctionContext, Variant


# Scope node types that have a body we can move declarations into
_SCOPE_TYPES = {
    "if_statement",
    "else_clause",
    "for_statement",
    "while_statement",
    "do_statement",
    "compound_statement",
}

# Max variants to generate per invocation
_MAX_VARIANTS = 12


class ScopeNarrowingPattern(Pattern):
    name = "scope_narrowing"
    safety_tier = "normal"
    structural_domain = "data_flow"
    follow_ups = ("declaration_reorder", "value_address_caching")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Relevant when there are callee-saved GPR swaps
        if diagnosis.reg_swap_pairs:
            for (r0, r1) in diagnosis.reg_swap_pairs:
                if r0.startswith("r") or r1.startswith("r"):
                    return True
        # Also relevant when there are clusters (structural mismatches)
        if diagnosis.clusters:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        score = 0.0
        if diagnosis.reg_swap_pairs:
            gpr_swaps = sum(
                1 for (a, b) in diagnosis.reg_swap_pairs
                if a.startswith("r") and b.startswith("r")
            )
            if gpr_swaps > 0:
                score = max(score, 0.6)
        if diagnosis.clusters:
            score = max(score, 0.3)
        return score if score > 0 else 0.1

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0

        # Walk all compound_statements in the function body
        for move in _find_narrowing_moves(ctx.body_node, ctx.file_source):
            if counter >= _MAX_VARIANTS:
                return

            decl_node, target_scope, scope_kind = move

            # Apply the move
            new_source = _apply_narrowing(
                ctx.file_source, decl_node, target_scope, scope_kind
            )
            if new_source is None or new_source == ctx.file_source:
                continue

            decl_name = _get_declared_name(decl_node) or "decl"
            yield Variant(
                name=f"scope_narrow_{counter}",
                pattern_name=self.name,
                description=(
                    f"Move '{decl_name}' declaration into {scope_kind} scope"
                ),
                source=new_source,
                func_byte_range=ctx.func_byte_range,
                original_source=ctx.file_source,
                tags=frozenset({"narrowed_scope"}),
            )
            counter += 1


def _find_narrowing_moves(
    body_node: Node, source: bytes
) -> list[tuple[Node, Node, str]]:
    """Find valid declaration-to-scope moves.

    Returns list of (declaration_node, target_scope_body, scope_kind) tuples.
    scope_kind is one of: "if", "else", "loop", "block".
    """
    results: list[tuple[Node, Node, str]] = []
    _scan_compound(body_node, source, results)
    return results


def _scan_compound(
    compound: Node,
    source: bytes,
    results: list[tuple[Node, Node, str]],
) -> None:
    """Scan a compound_statement for declaration statements that can be
    moved into narrower scopes among its siblings."""
    stmts = list(compound.named_children)

    for i, stmt in enumerate(stmts):
        if stmt.type != "declaration":
            # Recurse into nested compound statements
            _recurse_into_children(stmt, source, results)
            continue

        var_name = _get_declared_name(stmt)
        if var_name is None:
            _recurse_into_children(stmt, source, results)
            continue

        # Safety: reject address-taken variables
        if _is_address_taken(compound, var_name, source):
            _recurse_into_children(stmt, source, results)
            continue

        # Find all uses of this variable in sibling statements after the declaration
        sibling_uses = _find_uses_in_siblings(stmts, i, var_name, source)
        if not sibling_uses:
            _recurse_into_children(stmt, source, results)
            continue

        # Check if ALL uses are confined to a single narrower scope
        for sibling_idx in sibling_uses:
            sibling = stmts[sibling_idx]
            # Try to find a scope within this sibling that contains all uses
            scope_match = _find_containing_scope(
                sibling, var_name, stmts, i, sibling_uses, source
            )
            if scope_match is not None:
                target_body, scope_kind = scope_match
                results.append((stmt, target_body, scope_kind))
                break  # One move per declaration

        # Always recurse
        _recurse_into_children(stmt, source, results)


def _recurse_into_children(
    node: Node,
    source: bytes,
    results: list[tuple[Node, Node, str]],
) -> None:
    """Recurse into any compound_statement children of a node."""
    for child in node.children:
        if child.type == "compound_statement":
            _scan_compound(child, source, results)
        elif child.type in (
            "if_statement", "else_clause", "for_statement",
            "while_statement", "do_statement", "switch_statement",
        ):
            _recurse_into_children(child, source, results)


def _find_uses_in_siblings(
    stmts: list[Node],
    decl_idx: int,
    var_name: str,
    source: bytes,
) -> list[int]:
    """Find indices of sibling statements (after decl_idx) that use var_name."""
    uses = []
    for j in range(decl_idx + 1, len(stmts)):
        ids = identifiers_in(stmts[j])
        if var_name in ids:
            uses.append(j)
    return uses


def _find_containing_scope(
    sibling: Node,
    var_name: str,
    all_stmts: list[Node],
    decl_idx: int,
    all_use_indices: list[int],
    source: bytes,
) -> tuple[Node, str] | None:
    """Check if all uses of var_name are within a single narrower scope
    inside `sibling` (or if sibling IS the only use point).

    Returns (target_compound_statement, scope_kind) if valid, None otherwise.
    """
    # All uses must be in a single sibling statement
    if len(all_use_indices) != 1:
        # Multiple siblings use the variable -- check if they're all
        # inside the same sibling (shouldn't happen since they're different stmts)
        return None

    # The sole sibling that uses the variable
    use_sibling = all_stmts[all_use_indices[0]]

    # Case 1: if_statement -- variable used only in consequence (if-body)
    if use_sibling.type == "if_statement":
        condition = use_sibling.child_by_field_name("condition")
        consequence = use_sibling.child_by_field_name("consequence")
        alternative = use_sibling.child_by_field_name("alternative")

        # Reject if variable used in condition
        if condition is not None and var_name in identifiers_in(condition):
            return None

        if consequence is not None and consequence.type == "compound_statement":
            cons_ids = identifiers_in(consequence)
            if var_name in cons_ids:
                # Make sure it's NOT used in the else branch
                if alternative is None or var_name not in identifiers_in(alternative):
                    return (consequence, "if")

        # Case 2: else clause -- variable used only in alternative
        if alternative is not None:
            else_body = None
            if alternative.type == "else_clause":
                for child in alternative.children:
                    if child.type == "compound_statement":
                        else_body = child
                        break
            elif alternative.type == "compound_statement":
                else_body = alternative

            if else_body is not None:
                else_ids = identifiers_in(else_body)
                if var_name in else_ids:
                    # Make sure it's NOT used in the if-body
                    if consequence is None or var_name not in identifiers_in(consequence):
                        return (else_body, "else")

    # Case 3: for/while/do loop -- variable used only in loop body
    if use_sibling.type in ("for_statement", "while_statement", "do_statement"):
        # Reject if variable used in loop condition/init/update
        if _var_in_loop_control(use_sibling, var_name):
            return None

        loop_body = _get_loop_body(use_sibling)
        if loop_body is not None and loop_body.type == "compound_statement":
            body_ids = identifiers_in(loop_body)
            if var_name in body_ids:
                return (loop_body, "loop")

    # Case 4: compound_statement (bare block) -- variable only used inside
    if use_sibling.type == "compound_statement":
        block_ids = identifiers_in(use_sibling)
        if var_name in block_ids:
            return (use_sibling, "block")

    return None


def _var_in_loop_control(loop_node: Node, var_name: str) -> bool:
    """Check if var_name appears in a loop's condition/init/update."""
    if loop_node.type == "for_statement":
        for field_name in ("initializer", "condition", "update"):
            field = loop_node.child_by_field_name(field_name)
            if field is not None and var_name in identifiers_in(field):
                return True
    elif loop_node.type == "while_statement":
        cond = loop_node.child_by_field_name("condition")
        if cond is not None and var_name in identifiers_in(cond):
            return True
    elif loop_node.type == "do_statement":
        cond = loop_node.child_by_field_name("condition")
        if cond is not None and var_name in identifiers_in(cond):
            return True
    return False


def _get_loop_body(loop_node: Node) -> Node | None:
    """Get the body compound_statement of a loop node."""
    body = loop_node.child_by_field_name("body")
    if body is not None:
        return body
    # do_statement uses 'body' field
    for child in loop_node.children:
        if child.type == "compound_statement":
            return child
    return None


def _is_address_taken(scope: Node, var_name: str, source: bytes) -> bool:
    """Check if &var_name appears anywhere in the scope.

    tree-sitter C parses address-of as ``pointer_expression`` with children
    ``&`` (anonymous) and ``identifier``.
    """
    for node in walk(scope):
        if node.type == "pointer_expression":
            children = node.children
            if len(children) == 2:
                op_text = source[children[0].start_byte:children[0].end_byte]
                if op_text == b"&" and children[1].type == "identifier":
                    ident = children[1].text
                    if ident and ident.decode("utf-8", errors="replace") == var_name:
                        return True
    return False


def _get_declared_name(decl: Node) -> str | None:
    """Extract the variable name from a declaration node."""
    if decl.type != "declaration":
        return None

    declarator = decl.child_by_field_name("declarator")
    if declarator is None:
        return None

    if declarator.type == "init_declarator":
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            declarator = inner

    # Handle pointer/reference declarators
    while declarator.type in ("pointer_declarator", "reference_declarator"):
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            declarator = inner
        else:
            break

    if declarator.text:
        return declarator.text.decode("utf-8", errors="replace")
    return None


def _apply_narrowing(
    source: bytes,
    decl_node: Node,
    target_body: Node,
    scope_kind: str,
) -> bytes | None:
    """Apply the scope narrowing transformation.

    Removes the declaration from its original position and inserts it
    at the beginning of the target scope body.
    """
    # Get the full line extent of the declaration
    decl_line_start = _line_start(source, decl_node.start_byte)
    decl_line_end = _line_end(source, decl_node.end_byte)
    decl_text = source[decl_node.start_byte:decl_node.end_byte]

    # Determine indent of the target scope body's contents
    target_indent = _get_body_indent(source, target_body)

    # Build the declaration line with new indentation
    new_decl_line = target_indent + decl_text + b"\n"

    # Find insertion point: right after the opening brace of target_body
    # target_body is a compound_statement: { ... }
    insert_pos = target_body.start_byte + 1  # after '{'
    # Skip any whitespace/newline right after the opening brace
    if insert_pos < len(source) and source[insert_pos:insert_pos + 1] == b"\n":
        insert_pos += 1

    # Build the new source:
    # 1. Remove the declaration line from original position
    # 2. Insert at the beginning of target scope body

    # Determine order of operations based on positions
    if decl_line_start < insert_pos:
        # Declaration comes before insertion point
        # Remove first, then insert (adjust position)
        removed = source[:decl_line_start] + source[decl_line_end:]
        adj_insert = insert_pos - (decl_line_end - decl_line_start)
        result = removed[:adj_insert] + new_decl_line + removed[adj_insert:]
    else:
        # Declaration comes after insertion point (shouldn't normally happen
        # since we look at siblings after the decl, but handle it)
        result = source[:insert_pos] + new_decl_line + source[insert_pos:decl_line_start] + source[decl_line_end:]

    return result


def _get_body_indent(source: bytes, body_node: Node) -> bytes:
    """Get the indentation used by the first statement in a compound_statement body."""
    for child in body_node.named_children:
        # Get the indent of the first named child
        pos = child.start_byte
        line_start = _line_start(source, pos)
        indent = b""
        for i in range(line_start, pos):
            ch = source[i:i + 1]
            if ch in (b" ", b"\t"):
                indent += ch
            else:
                break
        return indent

    # No children -- derive from the body's own indent + one level
    pos = body_node.start_byte
    line_start = _line_start(source, pos)
    indent = b""
    for i in range(line_start, pos):
        ch = source[i:i + 1]
        if ch in (b" ", b"\t"):
            indent += ch
        else:
            break
    return indent + b"    "


def _line_start(source: bytes, pos: int) -> int:
    """Find the start of the line containing byte offset pos."""
    while pos > 0 and source[pos - 1:pos] not in (b"\n", b"\r"):
        pos -= 1
    return pos


def _line_end(source: bytes, pos: int) -> int:
    """Find the end of the line containing byte offset pos (past newline)."""
    length = len(source)
    while pos < length and source[pos:pos + 1] not in (b"\n", b""):
        pos += 1
    if pos < length and source[pos:pos + 1] == b"\n":
        pos += 1
    return pos
