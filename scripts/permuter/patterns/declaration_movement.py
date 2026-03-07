"""Declaration movement pattern — move declarations across statement boundaries.

Highest-impact pattern for fixing register allocation mismatches when
declarations are separated by non-declaration statements. The PowerPC
compiler assigns registers based on declaration/first-use order, so
moving a declaration past a statement (or pulling it earlier) can fix
register swap pairs.

Example:
    int total = 0;
    CampaignEraSongProgress *p = GetEraSongProgress(name);
    ->
    CampaignEraSongProgress *p = GetEraSongProgress(name);
    int total = 0;
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import identifiers_in
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Max declarations to try moving
_MAX_DECLS = 5
# Max positions to try per declaration
_MAX_MOVES = 5


class DeclarationMovementPattern(Pattern):
    name = "declaration_movement"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for (r0, r1) in diagnosis.reg_swap_pairs:
            if r0.startswith("r") or r1.startswith("r"):
                return True
        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        stmts = ctx.statements
        if len(stmts) < 2:
            return

        movable = _find_movable_decls(stmts)
        if not movable:
            return

        # Cap movable declarations
        if len(movable) > _MAX_DECLS:
            movable = movable[:_MAX_DECLS]

        counter = 0
        for decl_idx in movable:
            decl = stmts[decl_idx]
            decl_name = _get_declared_name(decl) or "decl"
            first_use = _find_first_use(stmts, decl_idx)

            moves = _compute_moves(decl_idx, first_use, len(stmts))
            for new_idx in moves:
                if not _is_safe_move(stmts, decl_idx, new_idx):
                    continue

                new_source = _apply_move(ctx.file_source, stmts, decl_idx, new_idx)
                if new_source == ctx.file_source:
                    continue

                direction = "down" if new_idx > decl_idx else "up"
                dist = abs(new_idx - decl_idx)
                yield Variant(
                    name=f"declmove_{counter}",
                    pattern_name=self.name,
                    description=f"Move '{decl_name}' {direction} by {dist}",
                    source=new_source,
                )
                counter += 1


def _is_movable_decl(node: Node) -> bool:
    """Check if a declaration is safe to relocate.

    We allow declarations with:
    - No initializer (e.g., `int x;`)
    - Constant/zero initializers (e.g., `int x = 0;`)
    - Single call initializers (e.g., `Foo *p = GetFoo();`)
    - Simple expressions (identifier, number, string, etc.)
    """
    if node.type != "declaration":
        return False

    declarator = node.child_by_field_name("declarator")
    if declarator is None:
        return True  # No declarator at all, unusual but movable

    # Multiple declarators (comma-declarations) — not movable by this pattern
    decl_count = 0
    for child in node.named_children:
        if child.type == "init_declarator":
            decl_count += 1
    if decl_count > 1:
        return False

    if declarator.type != "init_declarator":
        return True  # No initializer

    value = declarator.child_by_field_name("value")
    if value is None:
        return True

    # Allow simple initializer types
    simple_types = {
        "number_literal", "string_literal", "char_literal",
        "true", "false", "null", "nullptr",
        "identifier", "call_expression", "cast_expression",
        "unary_expression", "parenthesized_expression",
        "field_expression", "subscript_expression",
    }
    return value.type in simple_types


def _find_movable_decls(stmts: list[Node]) -> list[int]:
    """Find indices of movable declaration statements."""
    result = []
    for i, stmt in enumerate(stmts):
        if _is_movable_decl(stmt):
            result.append(i)
    return result


def _get_declared_name(decl: Node) -> str | None:
    """Extract the variable name from a declaration node."""
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


def _find_first_use(stmts: list[Node], decl_idx: int) -> int:
    """Find the index of the first statement after decl_idx that uses the declared variable.

    Returns len(stmts) if no use is found.
    """
    decl = stmts[decl_idx]
    name = _get_declared_name(decl)
    if name is None:
        return decl_idx + 1  # Conservative: assume immediate use

    for i in range(decl_idx + 1, len(stmts)):
        ids = identifiers_in(stmts[i])
        if name in ids:
            return i

    return len(stmts)


def _compute_moves(decl_idx: int, first_use: int, num_stmts: int) -> list[int]:
    """Compute target positions for moving a declaration.

    Generates positions moving down (toward first use) and up (away from it).
    Never moves past first_use (would break code).
    """
    moves = []

    # Move down: 1, 2, 3 positions (but not past first_use - 1, since the
    # declaration must appear before its first use)
    max_down = min(first_use - 1, num_stmts - 1)
    for offset in range(1, 4):
        target = decl_idx + offset
        if target <= max_down:
            moves.append(target)

    # Move up: 1, 2 positions
    for offset in range(1, 3):
        target = decl_idx - offset
        if target >= 0:
            moves.append(target)

    return moves[:_MAX_MOVES]


def _is_safe_move(stmts: list[Node], from_idx: int, to_idx: int) -> bool:
    """Check if moving a declaration is dependency-safe.

    The declaration's initializer must not reference variables that are
    declared between the new position and the old position (when moving up),
    or the declaration must not be referenced by statements between old and
    new positions (when moving down past first use — already handled by
    _compute_moves capping at first_use).
    """
    decl = stmts[from_idx]
    init_ids = _get_init_identifiers(decl)

    if to_idx < from_idx:
        # Moving up: check that the initializer doesn't reference
        # any variables declared in stmts[to_idx..from_idx)
        for i in range(to_idx, from_idx):
            stmt = stmts[i]
            if stmt.type == "declaration":
                name = _get_declared_name(stmt)
                if name and name in init_ids:
                    return False
    else:
        # Moving down: check that no statement in (from_idx..to_idx]
        # declares a variable used in our initializer
        for i in range(from_idx + 1, to_idx + 1):
            stmt = stmts[i]
            if stmt.type == "declaration":
                name = _get_declared_name(stmt)
                if name and name in init_ids:
                    return False

    return True


def _get_init_identifiers(decl: Node) -> set[str]:
    """Get all identifiers referenced in a declaration's initializer."""
    declarator = decl.child_by_field_name("declarator")
    if declarator is None or declarator.type != "init_declarator":
        return set()
    value = declarator.child_by_field_name("value")
    if value is None:
        return set()
    return identifiers_in(value)


def _apply_move(source: bytes, stmts: list[Node], from_idx: int, to_idx: int) -> bytes:
    """Move a statement from from_idx to to_idx position.

    Extracts the full line (including leading whitespace and trailing newline)
    of the declaration, removes it from its original position, and inserts it
    at the new position.
    """
    decl = stmts[from_idx]

    # Get full line extent: from start-of-line to end (including newline)
    line_start = _line_start(source, decl.start_byte)
    line_end = _line_end(source, decl.end_byte)

    decl_text = source[line_start:line_end]

    if to_idx < from_idx:
        # Moving up: insert before target, then delete original
        target = stmts[to_idx]
        insert_pos = _line_start(source, target.start_byte)

        # Remove original
        result = source[:line_start] + source[line_end:]
        # Insert at new position (which is before the removed range,
        # so offsets are still valid)
        result = result[:insert_pos] + decl_text + result[insert_pos:]
    else:
        # Moving down: insert after target, then delete original
        target = stmts[to_idx]
        insert_pos = _line_end(source, target.end_byte)

        # Remove original first (comes before insert point)
        result = source[:line_start] + source[line_end:]
        # Adjust insert position for the removal
        removed_len = line_end - line_start
        insert_pos -= removed_len
        result = result[:insert_pos] + decl_text + result[insert_pos:]

    return result


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
