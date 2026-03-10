"""Guard-to-nested-if — convert consecutive guard returns into nested ifs (and reverse).

Win rate: untested (proven in manual fix for CharLipSyncDriver::Poll, ~15 instances in HEAD~3).

Multiple consecutive `if (!cond) return;` guard statements can be restructured
into deeply nested `if (cond) { if (cond2) { ... } }` blocks. This changes
branch structure significantly — early returns generate separate branch targets,
while nested ifs share a common exit point.

Transformations (forward):
    if (!A) return;          ->  if (A) {
    if (!B) return;                if (B) {
    body;                              body;
                                   }
                                 }

    if (!A) return;          ->  if (A) {
    if (!B) return;                if (B) {
    if (C && D) {                      if (C && D) {
        body;                              body;
    }                                  }
                                   } else return;
                                 } else return;

Transformations (reverse — unnest to guards):
    if (A) {                 ->  if (!A) return;
        if (B) {                 if (!B) return;
            body;                body;
        }
    }

Detection signals:
    - Multiple branch opcode mismatches (beq/bne)
    - Large clusters (structural control flow change)
    - 3+ consecutive branch diff_ops
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent
from ..types import Diagnosis, FunctionContext, Variant

_BRANCH_OPCODES = {"beq", "bne", "ble", "bgt", "bge", "blt",
                   "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
                   "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-"}


class GuardToNestedPattern(Pattern):
    name = "guard_to_nested"
    safety_tier = "aggressive"
    structural_domain = "control_flow"
    follow_ups = ("early_return_merge", "branch_polarity")
    cross_unit_modes = ("inline_header",)

    def relevant(self, diagnosis: Diagnosis) -> bool:
        branch_count = sum(
            1 for d in diagnosis.diff_ops
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES
        )
        if branch_count >= 2:
            return True
        if diagnosis.clusters and any(c.size >= 3 for c in diagnosis.clusters):
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        branch_count = sum(
            1 for d in diagnosis.diff_ops
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES
        )
        if branch_count >= 3 and diagnosis.clusters:
            return 0.7
        return 0.4

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source
        stmts = ctx.statements

        # Direction 1: Guards to nested
        for variant in _guards_to_nested(stmts, source, counter):
            yield variant
            counter += 1
            if counter >= 5:
                return

        # Direction 2: Nested to guards (reverse)
        for variant in _nested_to_guards(stmts, source, counter):
            yield variant
            counter += 1
            if counter >= 5:
                return


def _negate_condition(cond_text: bytes) -> bytes:
    """Negate a condition expression intelligently.

    - !(expr) -> expr
    - !var -> var
    - a != b -> a == b
    - a == b -> a != b
    - expr -> !(expr)
    """
    stripped = cond_text.strip()

    # !(expr) -> expr
    if stripped.startswith(b"!(") and stripped.endswith(b")"):
        inner = stripped[2:-1]
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

    # !var -> var (simple identifier, not !=)
    if stripped.startswith(b"!") and not stripped.startswith(b"!="):
        return stripped[1:]

    # a != b -> a == b
    if b"!=" in stripped and b"!==" not in stripped:
        # Simple replacement for top-level != only
        # Find != that's not inside parens
        depth = 0
        for i in range(len(stripped) - 1):
            if stripped[i:i+1] == b"(":
                depth += 1
            elif stripped[i:i+1] == b")":
                depth -= 1
            elif depth == 0 and stripped[i:i+2] == b"!=":
                return stripped[:i] + b"==" + stripped[i+2:]

    # a == b -> a != b
    if b"==" in stripped:
        depth = 0
        for i in range(len(stripped) - 1):
            if stripped[i:i+1] == b"(":
                depth += 1
            elif stripped[i:i+1] == b")":
                depth -= 1
            elif depth == 0 and stripped[i:i+2] == b"==" and (i + 2 >= len(stripped) or stripped[i+2:i+3] != b"="):
                return stripped[:i] + b"!=" + stripped[i+2:]

    # Comparison operators: < -> >=, > -> <=, <= -> >, >= -> <
    _CMP_NEGATIONS = [(b"<=", b">"), (b">=", b"<"), (b"<", b">="), (b">", b"<=")]
    for op, neg_op in _CMP_NEGATIONS:
        if op in stripped:
            depth = 0
            for i in range(len(stripped) - len(op)):
                if stripped[i:i+1] == b"(":
                    depth += 1
                elif stripped[i:i+1] == b")":
                    depth -= 1
                elif depth == 0 and stripped[i:i+len(op)] == op:
                    # Make sure we don't match << or >> or <= when looking for <
                    before = stripped[i-1:i] if i > 0 else b""
                    after = stripped[i+len(op):i+len(op)+1]
                    if before in (b"<", b">") or after in (b"<", b">"):
                        continue
                    # For < and >, don't match <= or >=
                    if len(op) == 1 and after in (b"=",):
                        continue
                    return stripped[:i] + neg_op + stripped[i+len(op):]

    # Simple token (no spaces, no parens) — use !expr
    # Covers: identifiers, member access (a->b, a.b), scoped (A::B)
    if b" " not in stripped and b"(" not in stripped and b")" not in stripped:
        return b"!" + stripped

    # General case: expr -> !(expr)
    return b"!(" + cond_text + b")"


def _extract_guard_info(stmt: Node, source: bytes) -> tuple[bytes, bytes | None] | None:
    """Extract (condition_text, return_value_or_None) from `if (!cond) return [val];`.

    Returns None if stmt is not a guard pattern.
    return_value is None for bare `return;` (void).
    """
    if stmt.type != "if_statement":
        return None

    condition = stmt.child_by_field_name("condition")
    consequence = stmt.child_by_field_name("consequence")
    alternative = stmt.child_by_field_name("alternative")

    if condition is None or consequence is None:
        return None
    if alternative is not None:
        return None

    # Get return statement from consequence
    ret_stmt = None
    if consequence.type == "return_statement":
        ret_stmt = consequence
    elif consequence.type == "compound_statement":
        inner_stmts = [c for c in consequence.named_children if c.type != "comment"]
        if len(inner_stmts) == 1 and inner_stmts[0].type == "return_statement":
            ret_stmt = inner_stmts[0]
    if ret_stmt is None:
        return None

    # Get return value (None for bare return;)
    ret_value = None
    for child in ret_stmt.named_children:
        if child.type != "comment":
            ret_value = source[child.start_byte:child.end_byte]
            break

    # Get condition text
    inner = _get_inner_expr(condition)
    if inner is None:
        return None
    cond_text = source[inner.start_byte:inner.end_byte]

    return cond_text, ret_value


def _get_inner_expr(condition: Node) -> Node | None:
    """Extract inner expression from a condition_clause (parenthesized_expression)."""
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None


def _guards_to_nested(
    stmts: list[Node], source: bytes, counter: int
) -> Iterator[Variant]:
    """Convert consecutive guard returns into nested if blocks."""
    # Find consecutive guards at start of statement list
    guards: list[tuple[bytes, bytes | None]] = []  # (condition, return_value)
    guard_nodes: list[Node] = []

    for stmt in stmts:
        info = _extract_guard_info(stmt, source)
        if info is not None:
            guards.append(info)
            guard_nodes.append(stmt)
        else:
            break

    if len(guards) < 2:
        return

    # Remaining body statements after guards
    body_stmts = stmts[len(guards):]
    if not body_stmts:
        return

    first_guard = guard_nodes[0]
    last_body = body_stmts[-1]
    indent = get_indent(source, first_guard)

    # Build the body text from remaining statements
    body_start = body_stmts[0].start_byte
    body_end = body_stmts[-1].end_byte
    body_text = source[body_start:body_end]

    # Determine return value for else branches
    # Use the return value from the first guard (they should all match for consistency)
    ret_value = guards[0][1]

    # Build nested structure from inside out
    # Start with body, then wrap each guard condition around it
    current = body_text
    for i in range(len(guards) - 1, -1, -1):
        cond_text, guard_ret = guards[i]
        negated = _negate_condition(cond_text)
        depth = i
        inner_indent = indent + b"    " * (depth + 1)
        outer_indent = indent + b"    " * depth

        # Re-indent body/current block
        lines = current.split(b"\n")
        reindented = []
        for line in lines:
            stripped = line.lstrip()
            if stripped:
                reindented.append(inner_indent + stripped)
            else:
                reindented.append(b"")
        body_block = b"\n".join(reindented)

        if guard_ret is not None:
            # Has return value -> add else return X;
            current = (
                outer_indent + b"if (" + negated + b") {\n"
                + body_block + b"\n"
                + outer_indent + b"} else return " + guard_ret + b";"
            )
        else:
            # Void return -> no else needed (falls through)
            current = (
                outer_indent + b"if (" + negated + b") {\n"
                + body_block + b"\n"
                + outer_indent + b"}"
            )

    new_source = (
        source[:first_guard.start_byte]
        + current
        + source[last_body.end_byte:]
    )

    yield Variant(
        name=f"guard_nested_{counter}",
        pattern_name="guard_to_nested",
        description=f"Convert {len(guards)} guard returns to nested if blocks",
        source=new_source,
    )


def _nested_to_guards(
    stmts: list[Node], source: bytes, counter: int
) -> Iterator[Variant]:
    """Convert deeply nested if blocks into guard returns."""
    for stmt in stmts:
        if stmt.type != "if_statement":
            continue

        # Check nesting depth >= 2
        conditions, body_node = _collect_nesting(stmt, source)
        if len(conditions) < 2 or body_node is None:
            continue

        indent = get_indent(source, stmt)

        # Build guard statements
        parts = []
        for cond_text in conditions:
            negated = _negate_condition(cond_text)
            parts.append(indent + b"if (" + negated + b") return;")

        # Get body content (strip outer braces and re-indent)
        if body_node.type == "compound_statement":
            inner_stmts = [c for c in body_node.named_children if c.type != "comment"]
            body_parts = []
            for s in inner_stmts:
                s_text = source[s.start_byte:s.end_byte]
                # Re-indent to base level
                stripped = s_text.lstrip()
                body_parts.append(indent + stripped)
            body_text = b"\n".join(body_parts)
        else:
            body_text = indent + source[body_node.start_byte:body_node.end_byte].lstrip()

        guards = b"\n".join(parts)
        new_source = (
            source[:stmt.start_byte]
            + guards + b"\n"
            + body_text
            + source[stmt.end_byte:]
        )

        yield Variant(
            name=f"guard_nested_{counter}",
            pattern_name="guard_to_nested",
            description=f"Unnest {len(conditions)} levels into guard returns",
            source=new_source,
        )
        counter += 1


def _collect_nesting(
    node: Node, source: bytes
) -> tuple[list[bytes], Node | None]:
    """Recursively collect conditions from nested if structure.

    Returns (list_of_condition_texts, innermost_body_node).
    Stops when:
    - The if has an else clause
    - The consequence is not a single nested if (has other statements / is the body)
    """
    if node.type != "if_statement":
        return [], None

    condition = node.child_by_field_name("condition")
    consequence = node.child_by_field_name("consequence")
    alternative = node.child_by_field_name("alternative")

    if condition is None or consequence is None:
        return [], None

    # Must not have else
    if alternative is not None:
        return [], None

    inner = _get_inner_expr(condition)
    if inner is None:
        return [], None
    cond_text = source[inner.start_byte:inner.end_byte]

    # Check if consequence contains a single nested if
    if consequence.type == "compound_statement":
        inner_stmts = [c for c in consequence.named_children if c.type != "comment"]
        if len(inner_stmts) == 1 and inner_stmts[0].type == "if_statement":
            # Recurse
            deeper_conds, body = _collect_nesting(inner_stmts[0], source)
            return [cond_text] + deeper_conds, body
        else:
            # This is the innermost level — consequence is the body
            return [cond_text], consequence
    else:
        # Bare statement as body
        return [cond_text], consequence
