"""Scope widening pattern — hoist declarations OUT of nested scopes to function scope.

Inverse of scope_narrowing. Targets the OFFSET_SWAP pattern when two locals
of the same type get assigned swapped frame slots (e.g. RndText::WrapText has
emptyLine@0xa8 + tmpLine@0x168 in target, but tmpLine@0xa8 + emptyLine@0x168
in source — the loop-local tmpLine "wins" the lower slot because it's declared
first in its inner scope, not because of overall declaration order).

By hoisting a declaration from a loop body to the enclosing function scope,
the compiler sees it earlier in the linear scan, often changing slot order.

Example:
    void f() {
        if (cond) {
            Line emptyLine;  // slot 0x168 in source, 0xa8 in target
            ...
        }
        while (...) {
            Line tmpLine;  // slot 0xa8 in source (declared first per-iter)
            ...
        }
    }
    ->
    void f() {
        Line tmpLine;  // hoisted: now slot 0x168 in source
        if (cond) {
            Line emptyLine;  // now slot 0xa8
            ...
        }
        while (...) {
            // re-uses hoisted tmpLine
            tmpLine.field = ...;
            ...
        }
    }

Trade-off: hoisting eliminates the per-iteration constructor call. For PODs
or types with trivial ctors this is fine. For types with non-trivial ctors
that target re-invokes per iteration, hoisting will REGRESS — the pattern
will produce the variant anyway and let the scorer reject it.

Safety:
- Only hoist declarations with default ctors (no initializer expression).
- Don't hoist address-taken locals (live-range issues).
- Don't hoist if a same-named local already exists at function scope.
- Inner-scope declarations of the same name shadow the outer; we deliberately
  pick the OUTER scope to host the hoisted decl.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import identifiers_in, walk
from ..types import Diagnosis, FunctionContext, Variant


_NARROW_SCOPE_TYPES = {
    "for_statement",
    "while_statement",
    "do_statement",
    "if_statement",
    "compound_statement",
}

_MAX_VARIANTS = 8


class ScopeWideningPattern(Pattern):
    name = "scope_widening"
    safety_tier = "moderate"
    structural_domain = "data_flow"
    follow_ups = ("declaration_reorder", "declaration_movement")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Most useful when there's a dominant offset_swap (slot inversion)
        # or many callee-saved register swaps.
        if getattr(diagnosis, "offset_swap_count", 0) > 10:
            return True
        if diagnosis.reg_swap_pairs:
            for (r0, r1) in diagnosis.reg_swap_pairs:
                if r0.startswith("r") or r1.startswith("r"):
                    return True
        if diagnosis.clusters:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        score = 0.0
        if getattr(diagnosis, "offset_swap_count", 0) > 10:
            score = max(score, 0.8)
        if diagnosis.reg_swap_pairs:
            score = max(score, 0.5)
        return score if score > 0 else 0.2

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0

        moves = _find_widening_moves(ctx.body_node, ctx.file_source)
        for move in moves:
            if counter >= _MAX_VARIANTS:
                return
            decl_node, target_body, scope_kind = move
            new_source = _apply_widening(
                ctx.file_source, decl_node, target_body
            )
            if new_source is None or new_source == ctx.file_source:
                continue
            decl_name = _get_declared_name(decl_node) or "decl"
            yield Variant(
                name=f"scope_widen_{counter}",
                pattern_name=self.name,
                description=(
                    f"Hoist '{decl_name}' from {scope_kind} to enclosing scope"
                ),
                source=new_source,
                func_byte_range=ctx.func_byte_range,
                original_source=ctx.file_source,
                tags=frozenset({"widened_scope"}),
            )
            counter += 1


def _find_widening_moves(
    body_node: Node, source: bytes
) -> list[tuple[Node, Node, str]]:
    """Find (declaration, target_compound, scope_kind) for valid hoists.

    Only considers declarations that:
    - Have no initializer (default-constructed)
    - Are not address-taken
    - Live in a narrower scope than `body_node`
    """
    results: list[tuple[Node, Node, str]] = []
    _scan_for_hoists(body_node, body_node, source, results)
    return results


def _scan_for_hoists(
    node: Node,
    outer_compound: Node,
    source: bytes,
    results: list[tuple[Node, Node, str]],
) -> None:
    """Recursively find declarations in nested scopes that could be hoisted
    to `outer_compound`. `outer_compound` is the candidate destination
    (function body's compound_statement, typically)."""
    for child in node.children:
        if child.type in _NARROW_SCOPE_TYPES and child != outer_compound:
            # Find declarations inside this narrower scope
            inner_body = _get_scope_body(child)
            if inner_body is not None and inner_body != outer_compound:
                for stmt in inner_body.named_children:
                    if stmt.type != "declaration":
                        continue
                    if not _is_simple_default_decl(stmt):
                        continue
                    var_name = _get_declared_name(stmt)
                    if var_name is None:
                        continue
                    # Skip if address-taken anywhere in outer compound
                    if _is_address_taken(outer_compound, var_name, source):
                        continue
                    # Skip if same-named decl already exists at function scope
                    if _has_outer_decl(outer_compound, var_name, source, inner_body):
                        continue
                    scope_kind = _scope_kind_for(child)
                    results.append((stmt, outer_compound, scope_kind))

            # Recurse into the inner scope too
            _scan_for_hoists(child, outer_compound, source, results)
        else:
            _scan_for_hoists(child, outer_compound, source, results)


def _get_scope_body(scope_node: Node) -> Node | None:
    """Return the compound_statement body of a scope node, or None."""
    if scope_node.type == "compound_statement":
        return scope_node
    body = scope_node.child_by_field_name("body")
    if body is not None and body.type == "compound_statement":
        return body
    # if/else "consequence" / "alternative"
    for field in ("consequence", "alternative"):
        c = scope_node.child_by_field_name(field)
        if c is not None and c.type == "compound_statement":
            return c
    # Plain inner compound_statement child
    for child in scope_node.children:
        if child.type == "compound_statement":
            return child
    return None


def _scope_kind_for(scope_node: Node) -> str:
    if scope_node.type in ("for_statement", "while_statement", "do_statement"):
        return "loop"
    if scope_node.type == "if_statement":
        return "if"
    if scope_node.type == "compound_statement":
        return "block"
    return scope_node.type


def _is_simple_default_decl(decl: Node) -> bool:
    """A 'simple' decl: `T name;` with NO initializer, NO assignment.

    Multiple declarators or any init_declarator are rejected.
    """
    declarators = [c for c in decl.named_children if c.type in (
        "identifier", "init_declarator", "pointer_declarator",
        "reference_declarator", "array_declarator", "function_declarator",
    )]
    if len(declarators) != 1:
        return False
    declarator = declarators[0]
    # Reject init_declarator (has `= value`)
    if declarator.type == "init_declarator":
        return False
    return True


def _has_outer_decl(
    outer_compound: Node, var_name: str, source: bytes, exclude: Node
) -> bool:
    """Check if outer_compound (or one of its direct compound_statement
    children, excluding `exclude`) already declares `var_name`."""
    # Direct children
    for child in outer_compound.named_children:
        if child.type == "declaration":
            n = _get_declared_name(child)
            if n == var_name:
                return True
    return False


def _is_address_taken(scope: Node, var_name: str, source: bytes) -> bool:
    """Whether `&var_name` appears anywhere in `scope`."""
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
    if decl.type != "declaration":
        return None
    declarator = decl.child_by_field_name("declarator")
    if declarator is None:
        return None
    if declarator.type == "init_declarator":
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            declarator = inner
    while declarator.type in ("pointer_declarator", "reference_declarator"):
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            declarator = inner
        else:
            break
    if declarator.text:
        return declarator.text.decode("utf-8", errors="replace")
    return None


def _apply_widening(
    source: bytes,
    decl_node: Node,
    target_compound: Node,
) -> bytes | None:
    """Move `decl_node` from its current position to the start of
    `target_compound`'s body.

    Implementation: delete the original declaration line; insert a copy at
    the top of the target compound's body. Note: target_compound is BEFORE
    decl_node in source order (outer scope), so we insert FIRST then delete.
    """
    decl_line_start = _line_start(source, decl_node.start_byte)
    decl_line_end = _line_end(source, decl_node.end_byte)
    decl_text = source[decl_node.start_byte:decl_node.end_byte]

    # Insertion point: after the opening brace of target_compound
    insert_pos = target_compound.start_byte + 1  # past '{'
    # Skip the newline immediately after the brace
    if insert_pos < len(source) and source[insert_pos:insert_pos + 1] == b"\n":
        insert_pos += 1

    target_indent = _get_body_indent(source, target_compound)
    new_decl_line = target_indent + decl_text + b"\n"

    if insert_pos > decl_line_start:
        # Target precedes the decl: NOT this case for hoisting outward
        return None

    # Insert at the top of target_compound, then remove original
    result = (
        source[:insert_pos]
        + new_decl_line
        + source[insert_pos:decl_line_start]
        + source[decl_line_end:]
    )
    return result


def _get_body_indent(source: bytes, body_node: Node) -> bytes:
    for child in body_node.named_children:
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
    # Fallback: derive from body's own indent + one level
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
    while pos > 0 and source[pos - 1:pos] not in (b"\n", b"\r"):
        pos -= 1
    return pos


def _line_end(source: bytes, pos: int) -> int:
    length = len(source)
    while pos < length and source[pos:pos + 1] not in (b"\n", b""):
        pos += 1
    if pos < length and source[pos:pos + 1] == b"\n":
        pos += 1
    return pos
