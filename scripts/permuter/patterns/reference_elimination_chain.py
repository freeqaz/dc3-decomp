"""reference_elimination_chain — eliminate multiple ref bindings in one shot.

This pattern extends ``reference_elimination`` to chain multiple eliminations
in a single variant, covering the case where 2-3 consecutive ref-binding
variables all need to be inlined.

Observed win: WorldCrowd::SetFullness (83.1 -> 91.0%), where ``reference_elimination``
had to be applied three times manually to eliminate ``multiMesh``, ``instances``,
and ``backup`` local references.

The single-shot pattern eliminates the first ref, re-parses the modified
source with fresh AST nodes, finds the next eliminable ref, and repeats —
yielding variants for chains of depth 2, 3, and 4.

Overlap audit:
  * reference_elimination — applies ONE elimination per variant. This pattern
    produces COMPOUND variants (two or more eliminations fused). There is no
    overlap; they complement each other.
  * member_ref_bind — ADDS refs; this pattern REMOVES them. Opposite direction.
  * deep_member_ref_bind — adds deep double-indirection refs; this removes refs.
    Different AST shape; complementary.
  Not covered by any existing pattern. NEW pattern.
"""

from __future__ import annotations

from typing import Iterator

from .base import Pattern
from .reference_elimination import (
    _extract_ref_decl,
    _find_compound_statements,
    _find_identifier_uses,
    _has_address_of_use,
)
from ..editor import SourceEditor
from ..extractor import reparse_variant
from ..types import Diagnosis, FunctionContext, Variant

# Maximum chain depth — avoid combinatorial explosion
_MAX_CHAIN_DEPTH = 4
# Minimum chain depth — single-elimination is already handled by reference_elimination
_MIN_CHAIN_DEPTH = 2


class ReferenceEliminationChainPattern(Pattern):
    name = "reference_elimination_chain"
    safety_tier = "moderate"
    structural_domain = "general"
    follow_ups = ("reference_elimination",)

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Same signals as reference_elimination: callee-saved swaps or clusters
        from re import compile as _re_compile
        _CALLEE_SAVED_RE = _re_compile(r"[rf](1[3-9]|2\d|3[01])")

        for (r1, r2) in diagnosis.reg_swap_pairs:
            if _CALLEE_SAVED_RE.match(r1) or _CALLEE_SAVED_RE.match(r2):
                return True
        if diagnosis.clusters:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Slightly lower than reference_elimination (0.6) since chains are
        # higher-budget and we want single-elim to run first in round 1.
        return 0.5

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        """Yield variants that eliminate 2-4 ref bindings in sequence.

        For each eliminable ref found in the original context, we apply it,
        re-parse, and look for the next eliminable ref in the updated AST —
        yielding compound variants of depth 2 through _MAX_CHAIN_DEPTH.
        """
        counter = 0

        # Collect the first-level elimination candidates from the original ctx
        first_level = _collect_eliminations(ctx)

        for first_elim in first_level:
            if counter >= 6:
                break

            # Apply first elimination
            try:
                source_after_1 = _apply_elimination(ctx.file_source, first_elim)
            except ValueError:
                continue

            # Re-parse to get fresh AST
            try:
                ctx2 = reparse_variant(ctx, source_after_1)
            except ValueError:
                continue

            second_level = _collect_eliminations(ctx2)

            for second_elim in second_level:
                if counter >= 6:
                    break

                # Skip if same declaration position (stale reference to deleted decl)
                if second_elim[2] == first_elim[2]:
                    continue

                try:
                    source_after_2 = _apply_elimination(source_after_1, second_elim)
                except ValueError:
                    continue

                # Depth-2 variant
                desc1 = first_elim[0].decode("utf-8", errors="replace")
                desc2 = second_elim[0].decode("utf-8", errors="replace")
                yield Variant(
                    name=f"refelim_chain_{counter}",
                    pattern_name=self.name,
                    description=f"Eliminate refs '{desc1}' then '{desc2}'",
                    source=source_after_2,
                )
                counter += 1
                if counter >= 6:
                    break

                # Try depth-3 if budget allows
                if counter < 6:
                    try:
                        ctx3 = reparse_variant(ctx2, source_after_2)
                    except ValueError:
                        continue

                    third_level = _collect_eliminations(ctx3)
                    for third_elim in third_level:
                        if counter >= 6:
                            break
                        if third_elim[2] in (first_elim[2], second_elim[2]):
                            continue
                        try:
                            source_after_3 = _apply_elimination(source_after_2, third_elim)
                        except ValueError:
                            continue

                        desc3 = third_elim[0].decode("utf-8", errors="replace")
                        yield Variant(
                            name=f"refelim_chain_{counter}",
                            pattern_name=self.name,
                            description=(
                                f"Eliminate refs '{desc1}', '{desc2}', '{desc3}'"
                            ),
                            source=source_after_3,
                        )
                        counter += 1

                        # Try depth-4
                        if counter < 6:
                            try:
                                ctx4 = reparse_variant(ctx3, source_after_3)
                            except ValueError:
                                continue
                            fourth_level = _collect_eliminations(ctx4)
                            for fourth_elim in fourth_level:
                                if counter >= 6:
                                    break
                                if fourth_elim[2] in (
                                    first_elim[2], second_elim[2], third_elim[2]
                                ):
                                    continue
                                try:
                                    source_after_4 = _apply_elimination(
                                        source_after_3, fourth_elim
                                    )
                                except ValueError:
                                    continue
                                desc4 = fourth_elim[0].decode("utf-8", errors="replace")
                                yield Variant(
                                    name=f"refelim_chain_{counter}",
                                    pattern_name=self.name,
                                    description=(
                                        f"Eliminate refs '{desc1}', '{desc2}', "
                                        f"'{desc3}', '{desc4}'"
                                    ),
                                    source=source_after_4,
                                )
                                counter += 1


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _collect_eliminations(ctx: FunctionContext) -> list[tuple]:
    """Return list of (var_name, init_expr, decl_start_byte) for each eliminable ref.

    Walks all compound statements in the function body, same logic as
    ``reference_elimination``'s generate() loop but returns the raw info
    rather than yielding Variants.

    Returns tuples of (var_name, init_expr, decl_start, decl_end).
    """
    source = ctx.file_source
    results = []

    for compound in _find_compound_statements(ctx.body_node):
        stmts = list(compound.named_children)
        for i, stmt in enumerate(stmts):
            decl_info = _extract_ref_decl(stmt, source)
            if decl_info is None:
                continue

            var_name, init_expr, decl_start, decl_end = decl_info

            uses = []
            for j in range(i + 1, len(stmts)):
                uses.extend(_find_identifier_uses(stmts[j], var_name))

            if len(uses) < 1:
                continue

            if _has_address_of_use(uses):
                continue

            results.append((var_name, init_expr, decl_start, decl_end))

    return results


def _apply_elimination(
    source: bytes,
    elim_info: tuple,
) -> bytes:
    """Apply a single reference elimination to source bytes.

    ``elim_info`` is a (var_name, init_expr, decl_start, decl_end) tuple
    as returned by ``_collect_eliminations``.

    Raises ValueError if the SourceEditor encounters overlapping edits.
    """
    var_name, init_expr, decl_start, decl_end = elim_info

    # We need to re-find the uses in the current source via a lightweight
    # tree-sitter re-parse.
    from ..extractor import _PARSER

    tree = _PARSER.parse(source)

    # Find the declaration node at the stored byte offset
    decl_node = _find_node_at_byte(tree.root_node, decl_start)
    if decl_node is None:
        raise ValueError(f"Cannot find declaration node at byte {decl_start}")

    # Walk up to declaration
    node = decl_node
    while node is not None and node.type != "declaration":
        node = node.parent
    if node is None or node.type != "declaration":
        raise ValueError("Could not find declaration node")

    # Collect uses of the variable in the same compound statement scope
    compound = node.parent
    if compound is None or compound.type != "compound_statement":
        raise ValueError("Declaration not inside compound_statement")

    stmts = list(compound.named_children)
    decl_idx = None
    for i, stmt in enumerate(stmts):
        if stmt.start_byte == node.start_byte:
            decl_idx = i
            break

    if decl_idx is None:
        raise ValueError("Cannot locate declaration in sibling list")

    uses = []
    for j in range(decl_idx + 1, len(stmts)):
        uses.extend(_find_identifier_uses(stmts[j], var_name))

    if not uses:
        raise ValueError("No uses found after re-parse")

    if _has_address_of_use(uses):
        raise ValueError("Address-of use found — skip")

    # Determine delete range for declaration line
    del_end = node.end_byte
    while del_end < len(source) and source[del_end:del_end + 1] in (b"\n", b"\r"):
        del_end += 1
    del_start = node.start_byte
    while del_start > 0 and source[del_start - 1:del_start] in (b" ", b"\t"):
        del_start -= 1

    ed = SourceEditor(source)
    ed.delete_range(del_start, del_end)

    for use_node in sorted(uses, key=lambda n: n.start_byte, reverse=True):
        ed.replace_node(use_node, init_expr)

    return ed.apply()


def _find_node_at_byte(root, byte_offset: int):
    """Find the deepest AST node whose range contains ``byte_offset``."""
    node = root
    while True:
        found = None
        for child in node.children:
            if child.start_byte <= byte_offset < child.end_byte:
                found = child
                break
        if found is None:
            return node
        node = found
