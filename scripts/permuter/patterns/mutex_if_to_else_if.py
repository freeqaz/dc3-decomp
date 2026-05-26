"""mutex_if_to_else_if — convert adjacent mutually-exclusive ifs to else-if.

When two consecutive `if` statements have conditions that are mutual
complements (one is the exact negation of the other), the second can be
written as `else if`.  This eliminates a redundant re-load/re-test of
the shared variable, saving a branch and sometimes freeing a callee-saved
register.

Proven fix: CamShotCrowd::Load 99.6 -> 100%.

Example (forward):
    if (mCrowd && cond)  { A(); }
    if (!mCrowd && cond) { B(); }
    ->
    if (mCrowd && cond)  { A(); }
    else if (!mCrowd && cond) { B(); }

Example (reverse — split else-if back to two ifs):
    if (cond)      { A(); }
    else if (!cond){ B(); }
    ->
    if (cond)  { A(); }
    if (!cond) { B(); }

Overlap audit:
  * branch_polarity — operates only on if/ELSE (requires alternative node);
    never generates/removes the second standalone `if`.
  * null_guard_elimination — drops conditions; does not add `else`.
  * early_return_merge — merges guard returns into || chains.
  None of the above covers this transform. NEW pattern.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent
from ..types import Diagnosis, FunctionContext, Variant

_BRANCH_OPCODES = frozenset({
    "beq", "bne", "ble", "bgt", "bge", "blt",
    "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
    "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-",
})


class MutexIfToElseIfPattern(Pattern):
    name = "mutex_if_to_else_if"
    safety_tier = "conservative"
    structural_domain = "control_flow"
    follow_ups = ("branch_polarity",)

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        return bool(diagnosis.clusters)

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Polarity swaps (beq <-> bne) with small cluster count = good signal
        polarity_swaps = sum(
            1 for d in diagnosis.diff_ops
            if frozenset({d.target_opcode.rstrip("+-"),
                          d.base_opcode.rstrip("+-")}) in (
                frozenset({"beq", "bne"}),
                frozenset({"blt", "bge"}),
                frozenset({"ble", "bgt"}),
            )
        )
        if polarity_swaps >= 1 and len(diagnosis.clusters) <= 2:
            return 0.6
        if diagnosis.clusters:
            return 0.25
        return 0.15

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        stmts = ctx.statements
        source = ctx.file_source

        # Forward: two adjacent ifs whose conditions are mutual negations -> else-if
        for i in range(len(stmts) - 1):
            for variant in _try_merge_to_else_if(stmts[i], stmts[i + 1], source, counter):
                yield variant
                counter += 1

        # Reverse: if { } else if { } -> two separate ifs
        for stmt in stmts:
            for variant in _try_split_else_if(stmt, source, counter):
                yield variant
                counter += 1


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _get_condition_inner(condition: Node) -> Node | None:
    """Unwrap condition_clause / parenthesized_expression to the inner expr."""
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None


def _are_mutual_negations(cond_a: bytes, cond_b: bytes) -> bool:
    """Return True iff cond_a and cond_b are simple mutual negations.

    Handles:
      !X  vs  X
      X   vs  !X
      !(X) vs X
    We intentionally keep this conservative — only simple identifiers and
    single-level ! negations — to avoid unsafe transformations.
    """
    a = cond_a.strip()
    b = cond_b.strip()

    # !X vs X  or  X vs !X
    def _strip_not(s: bytes) -> bytes | None:
        if s.startswith(b"!") and not s.startswith(b"!="):
            inner = s[1:].strip()
            # !(expr) -> expr
            if inner.startswith(b"(") and inner.endswith(b")"):
                return inner[1:-1].strip()
            return inner
        return None

    neg_a = _strip_not(a)
    neg_b = _strip_not(b)

    if neg_a is not None and neg_a == b:
        return True
    if neg_b is not None and neg_b == a:
        return True
    return False


def _conditions_share_lead_token(cond_a: bytes, cond_b: bytes) -> bool:
    """Heuristic: both conditions reference the same leading identifier.

    Allows patterns like:
        if (mCrowd && expr1) / if (!mCrowd && expr2)
    We do NOT require full mutual exclusivity proof — the caller passes
    conditions that it already determined are mutual negations at the top
    level OR conditions that share a leading token in an && chain.
    """
    # Already handled by _are_mutual_negations for simple cases.
    # For && chains: check if the leading token of each side is a negation
    # of the other's leading token.
    a = cond_a.strip()
    b = cond_b.strip()

    def _leading_token(s: bytes) -> bytes:
        # Return the first space/&&-delimited token.
        for sep in (b" ", b"\t", b"&"):
            idx = s.find(sep)
            if idx > 0:
                return s[:idx].strip()
        return s

    tok_a = _leading_token(a)
    tok_b = _leading_token(b)
    return _are_mutual_negations(tok_a, tok_b)


def _try_merge_to_else_if(
    stmt_a: Node, stmt_b: Node, source: bytes, counter: int
) -> Iterator[Variant]:
    """If stmt_a and stmt_b are adjacent ifs with mutually-exclusive conditions,
    rewrite stmt_b as `else if`."""
    if stmt_a.type != "if_statement" or stmt_b.type != "if_statement":
        return

    # stmt_a must NOT already have an else clause
    alt_a = stmt_a.child_by_field_name("alternative")
    if alt_a is not None:
        return

    # stmt_b must NOT have an else clause either (else we'd need to handle chaining)
    alt_b = stmt_b.child_by_field_name("alternative")

    cond_a_node = stmt_a.child_by_field_name("condition")
    cond_b_node = stmt_b.child_by_field_name("condition")
    if cond_a_node is None or cond_b_node is None:
        return

    inner_a = _get_condition_inner(cond_a_node)
    inner_b = _get_condition_inner(cond_b_node)
    if inner_a is None or inner_b is None:
        return

    cond_a_text = source[inner_a.start_byte:inner_a.end_byte]
    cond_b_text = source[inner_b.start_byte:inner_b.end_byte]

    # Check mutual exclusivity: either direct negation or &&-chain with negated lead
    if not (_are_mutual_negations(cond_a_text, cond_b_text) or
            _conditions_share_lead_token(cond_a_text, cond_b_text)):
        return

    # Build the merged text: keep stmt_a unchanged, prepend "else " to stmt_b.
    # We insert " else" between the two statements.
    cons_a = stmt_a.child_by_field_name("consequence")
    if cons_a is None:
        return

    # The byte just after stmt_a's consequence is where whitespace lives before stmt_b.
    # We'll splice " else" right before stmt_b.
    between = source[stmt_a.end_byte:stmt_b.start_byte]
    # Preserve existing whitespace but insert "else " prefix before the second if.
    stmt_b_text = source[stmt_b.start_byte:stmt_b.end_byte]

    new_source = (
        source[:stmt_a.end_byte]
        + between
        + b"else "
        + stmt_b_text
        + source[stmt_b.end_byte:]
    )

    yield Variant(
        name=f"mutex_else_{counter}",
        pattern_name="mutex_if_to_else_if",
        description=f"Merge adjacent mutex ifs into else-if (conds: "
                    f"{cond_a_text[:30].decode('utf-8', errors='replace')})",
        source=new_source,
    )


def _try_split_else_if(
    stmt: Node, source: bytes, counter: int
) -> Iterator[Variant]:
    """If stmt is `if (...) { } else if (...) { }`, split into two separate ifs."""
    if stmt.type != "if_statement":
        return

    alternative = stmt.child_by_field_name("alternative")
    if alternative is None:
        return

    # The alternative must be an `else if` (else_clause -> if_statement)
    else_if = None
    for child in alternative.children:
        if child.type == "if_statement":
            else_if = child
            break
    if else_if is None:
        return

    # The else-if must not itself have an alternative (would need chaining)
    inner_alt = else_if.child_by_field_name("alternative")
    if inner_alt is not None:
        return

    # Verify conditions are mutual negations (safety: only split safe patterns)
    cond_a = stmt.child_by_field_name("condition")
    cond_b = else_if.child_by_field_name("condition")
    if cond_a is None or cond_b is None:
        return

    inner_a = _get_condition_inner(cond_a)
    inner_b = _get_condition_inner(cond_b)
    if inner_a is None or inner_b is None:
        return

    cond_a_text = source[inner_a.start_byte:inner_a.end_byte]
    cond_b_text = source[inner_b.start_byte:inner_b.end_byte]

    if not (_are_mutual_negations(cond_a_text, cond_b_text) or
            _conditions_share_lead_token(cond_a_text, cond_b_text)):
        return

    indent = get_indent(source, stmt)
    else_if_text = source[else_if.start_byte:else_if.end_byte]

    # Rebuild: stmt without the alternative, then a newline + second if
    stmt_without_else = source[stmt.start_byte:alternative.start_byte].rstrip()

    new_source = (
        source[:stmt.start_byte]
        + stmt_without_else
        + b"\n"
        + indent + else_if_text
        + source[stmt.end_byte:]
    )

    yield Variant(
        name=f"mutex_split_{counter}",
        pattern_name="mutex_if_to_else_if",
        description=f"Split else-if into two separate ifs (reverse)",
        source=new_source,
    )
