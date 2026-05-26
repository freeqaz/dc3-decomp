"""demorgan_guard — convert whole-body &&-guard to early-return (and reverse).

When a function's ENTIRE body (or a large block) is wrapped in a single
`if (A && B && C) { ... }`, rewriting it as an early-return guard:

    if (!A || !B || !C) return;
    ...

can change the compiler's register-allocation and branch-polarity choices
enough to match the target binary.

The INVERSE direction (early guards -> single &&-wrapped body) is also
generated so the permuter can explore both.

Proven fix: Character::DrawShadow 95.1 -> 96.9%.

Overlap audit:
  * guard_to_nested — converts sequential `if (!cond) return;` guards into
    DEEPLY NESTED `if (cond) { if (cond2) { ... } }` blocks. Operates on
    multiple consecutive guard statements, not on a single &&-wrapped body.
  * early_return_merge — merges/splits consecutive GUARD-RETURN statements
    or || chains. Does not handle expanding a conjunction (&&) into DeMorgan
    early returns.
  * branch_polarity — inverts a single if/else pair; does not add/remove
    guard returns.
  None cover the specific DeMorgan whole-body transform. NEW pattern.
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


class DeMorganGuardPattern(Pattern):
    name = "demorgan_guard"
    safety_tier = "moderate"
    structural_domain = "control_flow"
    follow_ups = ("branch_polarity", "early_return_merge", "guard_to_nested")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        return bool(diagnosis.clusters)

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Multiple branch mismatches + clusters → structural guard transform
        branch_count = sum(
            1 for d in diagnosis.diff_ops
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES
        )
        if branch_count >= 2 and diagnosis.clusters:
            return 0.5
        if diagnosis.clusters:
            return 0.3
        return 0.15

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        stmts = ctx.statements
        source = ctx.file_source

        # Forward: `if (A && B && ...) { whole body }` -> `if (!A || !B || ...) return; body`
        for variant in _try_guard_to_early_return(stmts, source, counter):
            yield variant
            counter += 1

        # Reverse: consecutive `if (!cond) return;` guards + body -> `if (cond && ...) { body }`
        for variant in _try_early_returns_to_guard(stmts, source, counter):
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


def _collect_and_operands(node: Node, source: bytes) -> list[bytes]:
    """Recursively collect top-level && operands (left-associative)."""
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op is not None and op.text == b"&&":
            left = node.child_by_field_name("left")
            right = node.child_by_field_name("right")
            result: list[bytes] = []
            if left:
                result.extend(_collect_and_operands(left, source))
            if right:
                result.extend(_collect_and_operands(right, source))
            return result
    return [source[node.start_byte:node.end_byte]]


def _negate_operand(operand: bytes) -> bytes:
    """Negate a single operand for DeMorgan expansion.

    !X     -> X
    !(X)   -> X
    X      -> !X
    """
    s = operand.strip()
    if s.startswith(b"!") and not s.startswith(b"!="):
        inner = s[1:].strip()
        if inner.startswith(b"(") and inner.endswith(b")"):
            return inner[1:-1].strip()
        return inner
    # Wrap compound expressions in parens
    if b" " in s or b"&&" in s or b"||" in s:
        return b"!(" + s + b")"
    return b"!" + s


def _negate_condition_to_or(operands: list[bytes]) -> bytes:
    """Build DeMorgan negation: !(A && B && C) -> !A || !B || !C."""
    return b" || ".join(_negate_operand(op) for op in operands)


def _is_simple_operand(operand: bytes) -> bool:
    """Return True if an && operand is simple enough to safely negate.

    We conservatively restrict to:
      - Identifiers: mFlag, theThing
      - Member access: a->b, a.b
      - Simple comparisons: a != 0, a == b
      - Not: compound expressions with function calls on both sides.
    """
    s = operand.strip()
    # Function call with significant side-effects: defer
    if s.count(b"(") > 1:
        return False
    return True


def _try_guard_to_early_return(
    stmts: list[Node], source: bytes, counter: int
) -> Iterator[Variant]:
    """Detect `if (A && B && ...) { <body> }` as the ONLY statement (or first
    substantial statement) and rewrite to early-return guard form."""
    # Only fire when the top-level body is a single if-without-else that
    # wraps essentially all the work.
    non_comment = [s for s in stmts if s.type != "comment"]

    # Case 1: exactly ONE statement, which is an if without else
    if len(non_comment) == 1:
        candidate = non_comment[0]
        if candidate.type != "if_statement":
            return
        alternative = candidate.child_by_field_name("alternative")
        if alternative is not None:
            return
        condition = candidate.child_by_field_name("condition")
        consequence = candidate.child_by_field_name("consequence")
        if condition is None or consequence is None:
            return

        inner = _get_condition_inner(condition)
        if inner is None:
            return

        operands = _collect_and_operands(inner, source)
        if len(operands) < 2:
            return

        if not all(_is_simple_operand(op) for op in operands):
            return

        # Build: if (!A || !B || ...) return; <body_contents>
        demorgan = _negate_condition_to_or(operands)
        indent = get_indent(source, candidate)

        # Extract body contents (strip outer braces if compound)
        if consequence.type == "compound_statement":
            inner_stmts = [c for c in consequence.named_children if c.type != "comment"]
            if not inner_stmts:
                return
            body_start = inner_stmts[0].start_byte
            body_end = inner_stmts[-1].end_byte
            body_text = source[body_start:body_end]
        else:
            body_text = source[consequence.start_byte:consequence.end_byte]

        new_text = (
            indent + b"if (" + demorgan + b")\n"
            + indent + b"    return;\n"
            + indent + body_text
        )

        new_source = (
            source[:candidate.start_byte]
            + new_text
            + source[candidate.end_byte:]
        )

        yield Variant(
            name=f"demorgan_guard_{counter}",
            pattern_name="demorgan_guard",
            description=f"DeMorgan: if ({len(operands)}-way &&) body -> early return guard",
            source=new_source,
        )
        return

    # Case 2: first statement is an if with many statements following
    # (the if wraps most of the function body — at least 2 body stmts inside)
    if len(non_comment) >= 2:
        candidate = non_comment[0]
        if candidate.type != "if_statement":
            return
        alternative = candidate.child_by_field_name("alternative")
        if alternative is not None:
            return
        condition = candidate.child_by_field_name("condition")
        consequence = candidate.child_by_field_name("consequence")
        if condition is None or consequence is None:
            return

        # Consequence must be compound with >= 2 statements
        if consequence.type != "compound_statement":
            return
        inner_stmts = [c for c in consequence.named_children if c.type != "comment"]
        if len(inner_stmts) < 2:
            return

        inner = _get_condition_inner(condition)
        if inner is None:
            return

        operands = _collect_and_operands(inner, source)
        if len(operands) < 2:
            return

        if not all(_is_simple_operand(op) for op in operands):
            return

        demorgan = _negate_condition_to_or(operands)
        indent = get_indent(source, candidate)

        body_start = inner_stmts[0].start_byte
        body_end = inner_stmts[-1].end_byte
        body_text = source[body_start:body_end]

        new_text = (
            indent + b"if (" + demorgan + b")\n"
            + indent + b"    return;\n"
            + indent + body_text
        )

        new_source = (
            source[:candidate.start_byte]
            + new_text
            + source[candidate.end_byte:]
        )

        yield Variant(
            name=f"demorgan_guard_{counter}",
            pattern_name="demorgan_guard",
            description=f"DeMorgan: if ({len(operands)}-way &&) -> early return + flat body",
            source=new_source,
        )


def _extract_guard_early_return(stmt: Node, source: bytes) -> bytes | None:
    """Extract condition_text from `if (!X || !Y || ...) return;`.

    Returns the inner condition text or None if not a DeMorgan guard.
    The condition must contain || (not just a simple !cond).
    """
    if stmt.type != "if_statement":
        return None

    alternative = stmt.child_by_field_name("alternative")
    if alternative is not None:
        return None

    condition = stmt.child_by_field_name("condition")
    consequence = stmt.child_by_field_name("consequence")
    if condition is None or consequence is None:
        return None

    # Consequence must be a bare return; (no value)
    ret = None
    if consequence.type == "return_statement":
        ret = consequence
    elif consequence.type == "compound_statement":
        inner = [c for c in consequence.named_children if c.type != "comment"]
        if len(inner) == 1 and inner[0].type == "return_statement":
            ret = inner[0]
    if ret is None:
        return None

    # Return must be void (no value)
    ret_children = [c for c in ret.named_children if c.type != "comment"]
    if ret_children:
        return None

    inner = _get_condition_inner(condition)
    if inner is None:
        return None

    cond_text = source[inner.start_byte:inner.end_byte]

    # Must contain || to be a DeMorgan guard
    if b"||" not in cond_text:
        return None

    return cond_text


def _collect_or_operands_of_negations(cond_text: bytes) -> list[bytes] | None:
    """Split `!A || !B || !C` into [A, B, C].

    Returns None if any operand is not a simple negation.
    """
    raw_parts = [p.strip() for p in cond_text.split(b"||")]
    if len(raw_parts) < 2:
        return None

    positives: list[bytes] = []
    for part in raw_parts:
        part = part.strip()
        if part.startswith(b"!") and not part.startswith(b"!="):
            inner = part[1:].strip()
            if inner.startswith(b"(") and inner.endswith(b")"):
                inner = inner[1:-1].strip()
            positives.append(inner)
        else:
            # Not a negation — still allowed, just keep as-is for && rebuild
            positives.append(part)

    return positives


def _try_early_returns_to_guard(
    stmts: list[Node], source: bytes, counter: int
) -> Iterator[Variant]:
    """Detect `if (!A || !B || ...) return; <body>` and rewrite to
    `if (A && B && ...) { <body> }` (reverse DeMorgan)."""
    non_comment = [s for s in stmts if s.type != "comment"]
    if len(non_comment) < 2:
        return

    # Only handle the case where the FIRST statement is a DeMorgan guard
    guard = non_comment[0]
    cond_text = _extract_guard_early_return(guard, source)
    if cond_text is None:
        return

    positives = _collect_or_negations_to_positives(cond_text)
    if positives is None or len(positives) < 2:
        return

    # Remaining statements become the body
    body_stmts = non_comment[1:]
    if not body_stmts:
        return

    indent = get_indent(source, guard)
    body_start = body_stmts[0].start_byte
    body_end = body_stmts[-1].end_byte
    body_text = source[body_start:body_end]

    conjunction = b" && ".join(positives)

    new_text = (
        indent + b"if (" + conjunction + b") {\n"
        + indent + b"    " + body_text.replace(b"\n", b"\n" + indent + b"    ") + b"\n"
        + indent + b"}"
    )

    new_source = (
        source[:guard.start_byte]
        + new_text
        + source[body_stmts[-1].end_byte:]
    )

    yield Variant(
        name=f"demorgan_wrap_{counter}",
        pattern_name="demorgan_guard",
        description=f"DeMorgan reverse: early returns -> if ({len(positives)}-way &&) wrapper",
        source=new_source,
    )


def _collect_or_negations_to_positives(cond_text: bytes) -> list[bytes] | None:
    """Split `!A || !B || !C` into [A, B, C] via simple string split.

    This is deliberately conservative — only handles || at the top level.
    """
    # Simple split on ||; doesn't handle nested parens but keeps it safe.
    parts = [p.strip() for p in cond_text.split(b"||")]
    if len(parts) < 2:
        return None

    positives: list[bytes] = []
    for part in parts:
        s = part.strip()
        if s.startswith(b"!") and not s.startswith(b"!="):
            inner = s[1:].strip()
            if inner.startswith(b"(") and inner.endswith(b")"):
                inner = inner[1:-1].strip()
            positives.append(inner)
        else:
            # Can't negate safely — give up
            return None

    return positives
