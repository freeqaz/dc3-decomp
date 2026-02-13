"""Declaration reorder pattern — permute variable declaration order.

Highest-value pattern for fixing register allocation mismatches.
PowerPC compiler assigns callee-saved registers (r19-r31) based on
variable declaration/first-use order, so reordering declarations can
fix register swap pairs.

Example:
    int a = 1;
    int b = 2;
    int c = 3;
    ->
    int b = 2;
    int a = 1;
    int c = 3;
"""

from __future__ import annotations

import itertools
import random
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import identifiers_in
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Maximum permutations to generate per group before switching to sampling
_MAX_PERMS = 20


class DeclarationReorderPattern(Pattern):
    name = "declaration_reorder"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Only relevant when there are GPR swap pairs
        for (r0, r1) in diagnosis.reg_swap_pairs:
            if r0.startswith("r") or r1.startswith("r"):
                return True
        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        # Find groups of consecutive declarations in the function body
        for group in _find_declaration_groups(ctx):
            if len(group) < 2:
                continue

            # Build dependency graph to avoid use-before-declaration errors
            deps = _build_dependency_edges(group)

            # Generate dependency-safe permutations of this group
            for perm in _get_permutations(group, deps):
                new_source = _apply_reorder(ctx.file_source, group, perm)
                if new_source == ctx.file_source:
                    continue  # Skip identity permutation

                desc_parts = []
                for i, node in enumerate(perm):
                    orig_idx = group.index(node)
                    if orig_idx != i:
                        name = _get_decl_name(node, ctx)
                        desc_parts.append(name)

                desc = f"Reorder declarations: {', '.join(desc_parts)}"
                yield Variant(
                    name=f"declreorder_{counter}",
                    pattern_name=self.name,
                    description=desc,
                    source=new_source,
                )
                counter += 1


def _find_declaration_groups(ctx: FunctionContext) -> list[list[Node]]:
    """Find groups of consecutive declaration statements in the body.

    A group is broken when a non-declaration statement is encountered.
    Only considers top-level statements in the function body.
    """
    groups: list[list[Node]] = []
    current: list[Node] = []

    for stmt in ctx.statements:
        if stmt.type == "declaration":
            current.append(stmt)
        else:
            if len(current) >= 2:
                groups.append(current)
            current = []

    if len(current) >= 2:
        groups.append(current)

    return groups


def _get_declared_name(decl: Node) -> str | None:
    """Extract the variable name from a declaration node."""
    declarator = decl.child_by_field_name("declarator")
    if declarator is None:
        return None
    # Unwrap init_declarator
    if declarator.type == "init_declarator":
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            declarator = inner
    # Unwrap pointer/reference declarators
    while declarator.type in ("pointer_declarator", "reference_declarator"):
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            declarator = inner
        else:
            break
    if declarator.text:
        return declarator.text.decode("utf-8", errors="replace")
    return None


def _get_initializer_identifiers(decl: Node) -> set[str]:
    """Collect all identifier names used in a declaration's initializer.

    Walks the init_declarator's value subtree to find referenced names.
    """
    declarator = decl.child_by_field_name("declarator")
    if declarator is None or declarator.type != "init_declarator":
        return set()
    value = declarator.child_by_field_name("value")
    if value is None:
        return set()
    return identifiers_in(value)


def _build_dependency_edges(group: list[Node]) -> dict[int, set[int]]:
    """Build a dependency graph for a declaration group.

    Returns a dict mapping each index to the set of indices that must come
    before it (i.e., decl[i] depends on decl[j] means j is in deps[i]).
    This prevents reorderings like `int y = x + 1;` before `int x = 5;`.
    """
    # Map variable name -> index in group for names declared in this group
    name_to_idx: dict[str, int] = {}
    for i, decl in enumerate(group):
        name = _get_declared_name(decl)
        if name:
            name_to_idx[name] = i

    # For each decl, check if its initializer references any earlier declaration
    deps: dict[int, set[int]] = {i: set() for i in range(len(group))}
    for i, decl in enumerate(group):
        init_ids = _get_initializer_identifiers(decl)
        for ref_name in init_ids:
            if ref_name in name_to_idx:
                j = name_to_idx[ref_name]
                if j != i:  # Don't depend on self
                    deps[i].add(j)

    return deps


def _respects_deps(perm_indices: list[int] | tuple[int, ...], deps: dict[int, set[int]]) -> bool:
    """Check if a permutation respects dependency ordering.

    For each element in perm_indices, all its dependencies must appear
    at an earlier position in the permutation.
    """
    pos = {idx: p for p, idx in enumerate(perm_indices)}
    for idx in perm_indices:
        for dep in deps[idx]:
            if pos[dep] > pos[idx]:
                return False
    return True


def _get_permutations(group: list[Node], deps: dict[int, set[int]] | None = None) -> list[list[Node]]:
    """Generate dependency-safe permutations, capping at _MAX_PERMS."""
    n = len(group)
    total_perms = 1
    for i in range(2, n + 1):
        total_perms *= i

    if deps is None:
        deps = {i: set() for i in range(n)}

    if total_perms <= _MAX_PERMS * 3:
        # Small enough to enumerate all and filter
        result = []
        for perm in itertools.permutations(range(n)):
            if list(perm) == list(range(n)):
                continue  # Skip identity
            if _respects_deps(perm, deps):
                result.append([group[i] for i in perm])
                if len(result) >= _MAX_PERMS:
                    break
        return result
    else:
        # Sample random permutations, filter for dependency safety
        seen: set[tuple[int, ...]] = set()
        identity = tuple(range(n))
        seen.add(identity)
        result = []
        attempts = 0
        while len(result) < _MAX_PERMS and attempts < _MAX_PERMS * 20:
            indices = list(range(n))
            random.shuffle(indices)
            key = tuple(indices)
            if key not in seen:
                seen.add(key)
                if _respects_deps(indices, deps):
                    result.append([group[i] for i in indices])
            attempts += 1
        return result


def _apply_reorder(source: bytes, original: list[Node], reordered: list[Node]) -> bytes:
    """Reorder declaration statements using SourceEditor.

    Each declaration in the reordered list takes the position (byte range)
    of the corresponding declaration in the original list, but with the
    content from the reordered node.
    """
    ed = SourceEditor(source)
    for orig_node, new_node in zip(original, reordered):
        if orig_node is not new_node:
            new_content = source[new_node.start_byte:new_node.end_byte]
            ed.replace_range(orig_node.start_byte, orig_node.end_byte, new_content)
    return ed.apply()


def _get_decl_name(node: Node, ctx: FunctionContext) -> str:
    """Extract a short name for a declaration node."""
    # Try to find the declarator identifier
    declarator = node.child_by_field_name("declarator")
    if declarator is not None:
        # Unwrap init_declarator
        if declarator.type == "init_declarator":
            inner = declarator.child_by_field_name("declarator")
            if inner is not None:
                declarator = inner
        if declarator.text:
            return declarator.text.decode("utf-8", errors="replace")

    # Fallback: first 30 chars of source
    text = ctx.source_text(node)
    return text[:30].strip()
