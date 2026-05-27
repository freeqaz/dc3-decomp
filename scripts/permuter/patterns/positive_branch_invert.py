"""positive_branch_invert — move the success path to the positive branch.

When a function is structured as:

    if (cond) return <falsy>;
    <stmts>;
    return <truthy>;

the compiler takes the "early failure" path on the true branch. Inverting
to put the success work on the positive branch sometimes matches the target:

    if (!cond) {
        <stmts>;
        return <truthy>;
    }
    return <falsy>;

Both directions are generated.

Proven fix: GemPlayer::GetCodaFreestyleExtents 88.7 -> 96.8%.

Overlap audit:
  * branch_polarity — requires an if/ELSE pair; does not add/remove returns.
  * single_return — converts early-return-then-body to result-variable form;
    different transform, single return point.
  * early_return_merge — merges/splits multiple guards; does not rewrite
    into a positive-branch wrapper.
  Not covered by any existing pattern. NEW pattern.
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

# Falsy / truthy literals we recognize for the heuristic return-value check
_FALSY_VALUES = frozenset({b"false", b"0", b"NULL", b"nullptr"})
_TRUTHY_VALUES = frozenset({b"true", b"1"})


class PositiveBranchInvertPattern(Pattern):
    name = "positive_branch_invert"
    safety_tier = "moderate"
    structural_domain = "control_flow"
    follow_ups = ("branch_polarity", "single_return")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        return bool(diagnosis.clusters)

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # A polarity swap (beq/bne) with small cluster count is a strong signal
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
            return 0.65
        if diagnosis.clusters:
            return 0.3
        return 0.15

    def context_priority(
        self, diagnosis: Diagnosis, ctx: FunctionContext
    ) -> float:
        """AST fast-path: shape `if (cond) return false; ...; return true;`.

        Per feedback_invert_early_return_positive_branch.md the high-confidence
        trigger is the function-level AST shape, not opcode flavor. When the
        body matches (small body, leading guard-return of a falsy literal,
        trailing return of a truthy literal), upgrade to >=0.8.

        Falls back to the regular `priority()` if the shape doesn't match.
        """
        base_priority = self.priority(diagnosis)
        if _matches_positive_branch_shape(ctx):
            return max(base_priority, 0.85)
        return base_priority

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        stmts = ctx.statements
        source = ctx.file_source

        # Forward: `if (cond) return falsy; stmts; return truthy;`
        #       -> `if (!cond) { stmts; return truthy; } return falsy;`
        for variant in _try_invert_to_positive_branch(stmts, source, counter):
            yield variant
            counter += 1

        # Reverse: `if (!cond) { stmts; return truthy; } return falsy;`
        #        -> `if (cond) return falsy; stmts; return truthy;`
        for variant in _try_split_positive_branch(stmts, source, counter):
            yield variant
            counter += 1


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _matches_positive_branch_shape(ctx: FunctionContext) -> bool:
    """AST fast-path for: `if (cond) return false; ...; return true;`

    True when the function body has:
      - first statement = `if (cond) return <falsy>;` (no else)
      - last statement  = `return <truthy>;`
      - middle is non-trivial OR shape matches even with no middle (degenerate)
      - body is small enough to be plausibly a returns-bool helper (<= ~30 stmts)

    Either polarity (falsy-guard/truthy-final or truthy-guard/falsy-final)
    counts — the transform applies in both directions.
    """
    stmts = [s for s in ctx.statements if s.type != "comment"]
    if len(stmts) < 2:
        return False
    if len(stmts) > 30:
        return False

    source = ctx.file_source
    first = stmts[0]
    last = stmts[-1]

    guard = _is_guard_return(first, source)
    if guard is None:
        return False
    _guard_cond, guard_ret_val = guard

    if last.type != "return_statement":
        return False
    final_ret_val = _get_return_value(last, source)
    if final_ret_val is None:
        return False

    gv = guard_ret_val.strip()
    fv = final_ret_val.strip()
    if gv == fv:
        return False

    # The high-confidence shape: one side falsy, the other truthy.
    falsy_truthy = (
        (gv in _FALSY_VALUES and fv in _TRUTHY_VALUES)
        or (gv in _TRUTHY_VALUES and fv in _FALSY_VALUES)
    )
    return falsy_truthy


def _get_condition_inner(condition: Node) -> Node | None:
    """Unwrap condition_clause to the inner expression."""
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None


def _negate_condition(cond_text: bytes) -> bytes:
    """Negate a condition expression conservatively."""
    s = cond_text.strip()
    # !(expr) -> expr
    if s.startswith(b"!(") and s.endswith(b")"):
        inner = s[2:-1]
        depth = 0
        for ch in inner:
            if ch == ord(b"("):
                depth += 1
            elif ch == ord(b")"):
                depth -= 1
            if depth < 0:
                break
        if depth == 0:
            return inner
    # !var -> var
    if s.startswith(b"!") and not s.startswith(b"!="):
        return s[1:]
    # Compound: wrap with !()
    if b" " in s or b"&&" in s or b"||" in s:
        return b"!(" + s + b")"
    return b"!" + s


def _get_return_value(ret_node: Node, source: bytes) -> bytes | None:
    """Extract return value bytes from a return_statement, or None for bare return."""
    for child in ret_node.named_children:
        if child.type != "comment":
            return source[child.start_byte:child.end_byte]
    return None


def _is_guard_return(stmt: Node, source: bytes) -> tuple[bytes, bytes] | None:
    """Return (inner_cond_text, return_value) if stmt is `if (cond) return val;`.

    Returns None otherwise.
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

    # Consequence must be a single return statement
    ret = None
    if consequence.type == "return_statement":
        ret = consequence
    elif consequence.type == "compound_statement":
        inner = [c for c in consequence.named_children if c.type != "comment"]
        if len(inner) == 1 and inner[0].type == "return_statement":
            ret = inner[0]
    if ret is None:
        return None

    ret_val = _get_return_value(ret, source)
    if ret_val is None:
        return None  # void return — skip

    inner = _get_condition_inner(condition)
    if inner is None:
        return None

    cond_text = source[inner.start_byte:inner.end_byte]
    return cond_text, ret_val


def _try_invert_to_positive_branch(
    stmts: list[Node], source: bytes, counter: int
) -> Iterator[Variant]:
    """Forward transform: if (cond) return falsy; <middle>; return truthy;
    -> if (!cond) { <middle>; return truthy; } return falsy;
    """
    non_comment = [s for s in stmts if s.type != "comment"]
    if len(non_comment) < 2:
        return

    # Pattern: first statement = guard return, last statement = trailing return
    first = non_comment[0]
    last = non_comment[-1]

    guard = _is_guard_return(first, source)
    if guard is None:
        return

    guard_cond, guard_ret_val = guard

    if last.type != "return_statement":
        return

    final_ret_val = _get_return_value(last, source)
    if final_ret_val is None:
        return

    # Safety: guard should return a falsy/truthy value so the transform is
    # clearly semantics-preserving even for the permuter's blind mode.
    # We also accept non-literal returns (the transform is correct regardless).
    # But to avoid spurious variants, only fire when the polarity makes sense:
    # guard returns falsy (early fail) or truthy (early success).
    guard_val_stripped = guard_ret_val.strip()
    final_val_stripped = final_ret_val.strip()

    # Allow any boolean-ish or literal returns, or non-literal (function call etc.)
    # We don't restrict to _FALSY_VALUES only — the transform is always correct.
    # Skip only when both returns are identical (no-op transform).
    if guard_val_stripped == final_val_stripped:
        return

    # Middle statements (everything except first and last)
    middle = non_comment[1:-1]
    if not middle:
        # Two-statement form: if (c) return A; return B;
        # -> if (!c) return B; return A;  (this is branch_polarity territory)
        # Emit it anyway — slightly different from branch_polarity's swap.
        pass

    negated_cond = _negate_condition(guard_cond)
    indent = get_indent(source, first)

    # Build the body for the positive branch
    if middle:
        mid_start = middle[0].start_byte
        mid_end = middle[-1].end_byte
        mid_text = source[mid_start:mid_end]
        body_lines = mid_text.replace(b"\n", b"\n" + b"    ") + b"\n"
        positive_body = (
            indent + b"if (" + negated_cond + b") {\n"
            + indent + b"    " + body_lines
            + indent + b"    return " + final_ret_val + b";\n"
            + indent + b"}\n"
            + indent + b"return " + guard_ret_val + b";"
        )
    else:
        # Degenerate: just swap the two returns
        positive_body = (
            indent + b"if (" + negated_cond + b")\n"
            + indent + b"    return " + final_ret_val + b";\n"
            + indent + b"return " + guard_ret_val + b";"
        )

    new_source = (
        source[:first.start_byte]
        + positive_body
        + source[last.end_byte:]
    )

    yield Variant(
        name=f"posbranch_{counter}",
        pattern_name="positive_branch_invert",
        description=(
            f"Invert to positive branch: if (!{guard_cond[:20].decode('utf-8', errors='replace')})"
            f" {{ ... return {final_val_stripped.decode('utf-8', errors='replace')}; }}"
        ),
        source=new_source,
    )


def _try_split_positive_branch(
    stmts: list[Node], source: bytes, counter: int
) -> Iterator[Variant]:
    """Reverse: `if (!cond) { stmts; return truthy; } return falsy;`
    -> `if (cond) return falsy; stmts; return truthy;`
    """
    non_comment = [s for s in stmts if s.type != "comment"]
    if len(non_comment) < 2:
        return

    first = non_comment[0]
    last = non_comment[-1]

    if first.type != "if_statement":
        return
    if last.type != "return_statement":
        return

    alternative = first.child_by_field_name("alternative")
    if alternative is not None:
        return

    condition = first.child_by_field_name("condition")
    consequence = first.child_by_field_name("consequence")
    if condition is None or consequence is None:
        return

    # Consequence must be a compound_statement ending in a return
    if consequence.type != "compound_statement":
        return

    inner_stmts = [c for c in consequence.named_children if c.type != "comment"]
    if len(inner_stmts) < 1:
        return

    last_inner = inner_stmts[-1]
    if last_inner.type != "return_statement":
        return

    inner_ret_val = _get_return_value(last_inner, source)
    if inner_ret_val is None:
        return

    outer_ret_val = _get_return_value(last, source)
    if outer_ret_val is None:
        return

    # Skip no-op (same return value)
    if inner_ret_val.strip() == outer_ret_val.strip():
        return

    inner_cond = _get_condition_inner(condition)
    if inner_cond is None:
        return

    cond_text = source[inner_cond.start_byte:inner_cond.end_byte]
    negated = _negate_condition(cond_text)
    indent = get_indent(source, first)

    # Middle of the if-body (all statements except the final return)
    middle_stmts = inner_stmts[:-1]

    # Build: if (negated_cond) return outer_ret; middle_stmts; return inner_ret;
    guard_line = indent + b"if (" + negated + b")\n" + indent + b"    return " + outer_ret_val + b";\n"

    if middle_stmts:
        mid_start = middle_stmts[0].start_byte
        mid_end = middle_stmts[-1].end_byte
        mid_text = source[mid_start:mid_end]
        trailing = b"\n" + indent + b"return " + inner_ret_val + b";"
        new_text = guard_line + indent + mid_text + trailing
    else:
        trailing = indent + b"return " + inner_ret_val + b";"
        new_text = guard_line + trailing

    new_source = (
        source[:first.start_byte]
        + new_text
        + source[last.end_byte:]
    )

    yield Variant(
        name=f"posbranch_split_{counter}",
        pattern_name="positive_branch_invert",
        description="Split positive branch back to guard + trailing return",
        source=new_source,
    )
