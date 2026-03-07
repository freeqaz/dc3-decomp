"""Comma-split pattern — split multi-declarators into separate statements.

Splits `int a = 1, b = 2;` into `int a = 1; int b = 2;` and also tries
reversed order. This gives the compiler different register allocation
opportunities since comma-declarators may be treated as a single allocation
unit.

Example:
    int a = 1, b = 2;
    ->
    int a = 1;
    int b = 2;
    (and also: int b = 2; int a = 1;)
"""

from __future__ import annotations

import itertools
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Max comma-declarations to process
_MAX_COMMA_DECLS = 3


class CommaSplitPattern(Pattern):
    name = "comma_split"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        return bool(diagnosis.reg_swap_pairs)

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        comma_decls = _find_comma_declarations(ctx.statements)
        if not comma_decls:
            return

        if len(comma_decls) > _MAX_COMMA_DECLS:
            comma_decls = comma_decls[:_MAX_COMMA_DECLS]

        counter = 0
        for decl in comma_decls:
            parts = _extract_declarator_parts(decl, ctx.file_source)
            if len(parts) < 2:
                continue

            type_spec = _get_type_specifier(decl, ctx.file_source)
            if type_spec is None:
                continue

            indent = get_indent(ctx.file_source, decl)

            # Variant 1: split in original order
            split_stmts = _build_split_statements(type_spec, parts, indent)
            new_source = _apply_split(ctx.file_source, decl, split_stmts)
            if new_source != ctx.file_source:
                names = [p[0] for p in parts]
                yield Variant(
                    name=f"commasplit_{counter}",
                    pattern_name=self.name,
                    description=f"Split comma-decl: {', '.join(names)}",
                    source=new_source,
                )
                counter += 1

            # Variant 2: split in reversed order
            reversed_parts = list(reversed(parts))
            split_stmts_rev = _build_split_statements(type_spec, reversed_parts, indent)
            new_source_rev = _apply_split(ctx.file_source, decl, split_stmts_rev)
            if new_source_rev != ctx.file_source:
                names_rev = [p[0] for p in reversed_parts]
                yield Variant(
                    name=f"commasplit_{counter}",
                    pattern_name=self.name,
                    description=f"Split comma-decl reversed: {', '.join(names_rev)}",
                    source=new_source_rev,
                )
                counter += 1

            # For 3+ declarators, try all permutations (capped)
            if len(parts) >= 3:
                seen = {tuple(range(len(parts))), tuple(reversed(range(len(parts))))}
                for perm in itertools.permutations(range(len(parts))):
                    if perm in seen:
                        continue
                    seen.add(perm)
                    perm_parts = [parts[i] for i in perm]
                    split_stmts_p = _build_split_statements(type_spec, perm_parts, indent)
                    new_source_p = _apply_split(ctx.file_source, decl, split_stmts_p)
                    if new_source_p != ctx.file_source:
                        names_p = [p[0] for p in perm_parts]
                        yield Variant(
                            name=f"commasplit_{counter}",
                            pattern_name=self.name,
                            description=f"Split comma-decl permuted: {', '.join(names_p)}",
                            source=new_source_p,
                        )
                        counter += 1
                    if counter >= 6:
                        return


def _find_comma_declarations(stmts: list[Node]) -> list[Node]:
    """Find declaration statements with multiple init_declarator children."""
    result = []
    for stmt in stmts:
        if stmt.type != "declaration":
            continue
        init_count = sum(1 for c in stmt.named_children if c.type == "init_declarator")
        # Also count plain declarators (uninitialized vars in comma-decls)
        plain_count = sum(
            1 for c in stmt.named_children
            if c.type in ("identifier", "pointer_declarator", "reference_declarator")
            and c != stmt.child_by_field_name("type")
        )
        if init_count + plain_count >= 2:
            result.append(stmt)
    return result


def _get_type_specifier(decl: Node, source: bytes) -> bytes | None:
    """Extract the type specifier text from a declaration.

    For `int a = 1, b = 2;`, returns b'int'.
    For `const char *a, *b;`, returns b'const char'.
    """
    type_node = decl.child_by_field_name("type")
    if type_node is None:
        return None

    # Include any qualifiers before the type
    type_text = source[type_node.start_byte:type_node.end_byte]

    # Check for qualifiers (const, volatile, etc.) that may precede the type node
    # in the declaration but are separate children
    parts = []
    for child in decl.children:
        if child.start_byte >= type_node.start_byte:
            break
        if child.type in ("type_qualifier", "storage_class_specifier"):
            parts.append(source[child.start_byte:child.end_byte])

    if parts:
        parts.append(type_text)
        return b" ".join(parts)

    return type_text


def _extract_declarator_parts(
    decl: Node, source: bytes
) -> list[tuple[str, bytes]]:
    """Extract (name, full_declarator_text) for each declarator in a comma-declaration.

    Returns pairs of (variable_name, declarator_bytes) where declarator_bytes
    includes the pointer/reference markers, name, and initializer.
    """
    parts = []
    type_node = decl.child_by_field_name("type")
    type_end = type_node.end_byte if type_node else decl.start_byte

    for child in decl.named_children:
        if child.type == "init_declarator":
            name = _declarator_name(child)
            text = source[child.start_byte:child.end_byte]
            parts.append((name, text))
        elif child.type in ("identifier", "pointer_declarator", "reference_declarator"):
            # Skip the type identifier itself
            if child.start_byte >= type_end:
                name = _declarator_name(child)
                text = source[child.start_byte:child.end_byte]
                parts.append((name, text))

    return parts


def _declarator_name(node: Node) -> str:
    """Extract the variable name from a declarator node."""
    if node.type == "init_declarator":
        inner = node.child_by_field_name("declarator")
        if inner is not None:
            return _declarator_name(inner)
    if node.type in ("pointer_declarator", "reference_declarator"):
        inner = node.child_by_field_name("declarator")
        if inner is not None:
            return _declarator_name(inner)
    if node.text:
        return node.text.decode("utf-8", errors="replace")
    return "?"


def _build_split_statements(
    type_spec: bytes, parts: list[tuple[str, bytes]], indent: bytes
) -> bytes:
    """Build separate declaration statements from split parts.

    For type b'int' and parts [('a', b'a = 1'), ('b', b'b = 2')],
    produces: b'int a = 1;\\n    int b = 2;'
    """
    lines = []
    for _name, decl_text in parts:
        lines.append(type_spec + b" " + decl_text + b";")
    return (b"\n" + indent).join(lines)


def _apply_split(source: bytes, decl: Node, replacement: bytes) -> bytes:
    """Replace a comma-declaration with split statements."""
    ed = SourceEditor(source)
    ed.replace_range(decl.start_byte, decl.end_byte, replacement)
    return ed.apply()
