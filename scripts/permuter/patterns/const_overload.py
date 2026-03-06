"""Const overload pattern — add const to local variable declarations.

When the original binary calls a const method overload but our decomp calls
the non-const version (or vice versa), the ICF-merged bl target differs only
in the MSVC mangling qualifier (@@QAA vs @@QBA). Adding `const` to the local
variable's type makes the compiler select the const overload instead.

Example:
    UIListState& state = GetState();
    state.Provider();  // calls Provider() non-const -> @@QAA
    ->
    const UIListState& state = GetState();
    state.Provider();  // calls Provider() const -> @@QBA
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import FunctionContext, Variant


def _find_declarations(node: Node) -> Iterator[Node]:
    """Find declaration nodes in the function body."""
    for n in walk(node):
        if n.type == "declaration":
            yield n


def _is_reference_decl(ctx: FunctionContext, decl: Node) -> bool:
    """Check if a declaration is a reference type (not pointer).

    We only apply const to references, not pointers, because:
    - `const T& x` is valid and changes method overload selection
    - `const T* x` changes pointer-to-const semantics and often fails
      (e.g., `const T* x = this;` fails in non-const method)
    """
    text = ctx.source_text(decl)
    eq_pos = text.find('=')
    type_part = text[:eq_pos] if eq_pos >= 0 else text
    # Must have & but not * (reference only, not pointer)
    return '&' in type_part and '*' not in type_part


def _already_const(ctx: FunctionContext, decl: Node) -> bool:
    """Check if the declaration already has const qualifier."""
    text = ctx.source_text(decl)
    eq_pos = text.find('=')
    type_part = text[:eq_pos] if eq_pos >= 0 else text
    # Check for 'const' as a word boundary
    return bool(re.search(r'\bconst\b', type_part))


def _get_type_node(decl: Node) -> Node | None:
    """Get the type specifier node from a declaration."""
    for child in decl.children:
        if child.type in (
            "type_identifier", "primitive_type", "sized_type_specifier",
            "template_type", "qualified_identifier", "scoped_type_identifier",
        ):
            return child
    return None


def _get_declarator(decl: Node) -> Node | None:
    """Get the declarator node (variable name with &/* qualifier)."""
    for child in decl.children:
        if child.type in (
            "reference_declarator", "pointer_declarator",
            "init_declarator",
        ):
            return child
    return None


class ConstOverloadPattern(Pattern):
    name = "const_overload"

    def relevant(self, diagnosis) -> bool:
        # This pattern is relevant when there are diff_arg mismatches
        # (potential ICF const qualifier diffs show up as diff_arg on bl)
        mc = diagnosis.match_counts
        return mc.get("diff_arg", 0) > 0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for decl in _find_declarations(ctx.body_node):
            # Skip if already const
            if _already_const(ctx, decl):
                continue

            # Only try on reference/pointer declarations
            if not _is_reference_decl(ctx, decl):
                continue

            type_node = _get_type_node(decl)
            if type_node is None:
                continue

            decl_text = ctx.source_text(decl)
            type_text = ctx.source_text(type_node)

            # Strategy 1: Add 'const' before the type
            ed = SourceEditor(ctx.file_source)
            ed.insert_before(type_node, b"const ")
            try:
                new_source = ed.apply()
            except ValueError:
                continue

            yield Variant(
                name=f"const_{counter}",
                pattern_name=self.name,
                description=f"Add const to '{type_text}' declaration",
                source=new_source,
            )
            counter += 1

            # Strategy 2: For non-const refs, also try removing const
            # (in case the original used non-const but we have const)
            # This is handled by the fact that we skip already-const decls above

        # Strategy 3: For existing const declarations, try removing const
        for decl in _find_declarations(ctx.body_node):
            if not _already_const(ctx, decl):
                continue
            if not _is_reference_decl(ctx, decl):
                continue

            decl_text = ctx.source_text(decl)
            # Remove 'const ' or ' const' from the type part
            eq_pos = decl_text.find('=')
            type_part = decl_text[:eq_pos] if eq_pos >= 0 else decl_text
            rest = decl_text[eq_pos:] if eq_pos >= 0 else ''

            # Try removing 'const ' prefix
            new_type = re.sub(r'\bconst\s+', '', type_part, count=1)
            if new_type == type_part:
                # Try removing ' const' suffix (e.g., "Type const&")
                new_type = re.sub(r'\s+const\b', '', type_part, count=1)
            if new_type == type_part:
                continue

            new_decl = new_type + rest
            ed = SourceEditor(ctx.file_source)
            ed.replace_range(decl.start_byte, decl.end_byte,
                             new_decl.encode('utf-8'))
            try:
                new_source = ed.apply()
            except ValueError:
                continue

            yield Variant(
                name=f"const_{counter}",
                pattern_name=self.name,
                description=f"Remove const from declaration",
                source=new_source,
            )
            counter += 1
