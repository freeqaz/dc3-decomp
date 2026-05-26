"""Replace reads of an arg with reads of the member it was just stored to.

Win rate: untested (new pattern, proven in BandCharacter::StartLoad 99.3->100%).

After a store `member = arg;` (where `member` is a class data member and `arg`
is a parameter or simple local), a SUBSEQUENT read of `arg` — especially in a
bool test like `if (!arg)` — can be replaced with `member`.

MWCC then emits a record-form byte test (`clrlwi.` on the stored byte from
memory) instead of `cmpwi` on the parameter register, matching the target.

Safety: Only fires when `member` is provably still equal to `arg`.  The
pattern requires:
  1. The store is an assignment_expression `member = arg` where `member` is a
     simple identifier matching the m[A-Z] Hmx naming convention and `arg` is
     an identifier (no calls, no complex expressions).
  2. No intervening reassignment of `member` or `arg` between the store and the
     read.
  3. The read appears in a boolean context (condition_clause / if_statement
     condition / unary `!`).

Detection signals:
    - cmpwi vs clrlwi. replace mismatch (MWCC byte-test form)
    - diff_ops with clrlwi in target
    - cmpwi in base where target has nothing (store-and-retest optimization)
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Hmx/Milo member naming: starts with m + uppercase letter
_MEMBER_RE = re.compile(rb"^m[A-Z]")

# Callee-saved GPR range
_CALLEE_SAVED_RE = re.compile(r"r(1[3-9]|2\d|3[01])")


class MemberReadbackPattern(Pattern):
    name = "member_readback"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # clrlwi. / cmpwi target-vs-base mismatch is the primary signal
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("clrlwi.", "clrlwi") and d.base_opcode == "cmpwi":
                return True
            if d.target_opcode == "cmpwi" and d.base_opcode in ("clrlwi.", "clrlwi"):
                return True
            # Generic compare-type mismatch
            if d.target_opcode in ("clrlwi.", "lbz") and d.base_opcode in ("cmpwi", "cmplwi"):
                return True
        # replace_real hints at structural bool test difference
        if diagnosis.replace_real > 0:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("clrlwi.", "clrlwi") and d.base_opcode in ("cmpwi", "cmplwi"):
                return 0.8
        return 0.3 if self.relevant(diagnosis) else 0.0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        stmts = ctx.statements
        counter = 0

        for i, stmt in enumerate(stmts):
            if counter >= 8:
                break

            # Find assignment_expression statements: member = arg;
            store_info = _extract_member_store(stmt, source)
            if store_info is None:
                continue

            member_bytes, arg_bytes, store_node = store_info

            # Scan subsequent statements for reads of arg in boolean contexts
            for j in range(i + 1, len(stmts)):
                if counter >= 8:
                    break

                next_stmt = stmts[j]

                # Stop if arg or member is reassigned in an intervening statement
                if _is_reassigned(next_stmt, source, member_bytes):
                    break
                if _is_reassigned(next_stmt, source, arg_bytes):
                    break

                # Find bool-context reads of arg
                for read_node in _find_bool_reads(next_stmt, source, arg_bytes):
                    if counter >= 8:
                        break

                    ed = SourceEditor(source)
                    ed.replace_node(read_node, member_bytes)

                    try:
                        new_source = ed.apply()
                    except ValueError:
                        continue

                    arg_str = arg_bytes.decode("utf-8", errors="replace")
                    mem_str = member_bytes.decode("utf-8", errors="replace")
                    yield Variant(
                        name=f"memread_{counter}",
                        pattern_name=self.name,
                        description=f"Replace bool read of '{arg_str}' with member '{mem_str}'",
                        source=new_source,
                    )
                    counter += 1


def _extract_member_store(
    stmt: Node, source: bytes
) -> tuple[bytes, bytes, Node] | None:
    """Find a simple member = arg; assignment expression statement.

    Returns (member_bytes, arg_bytes, assignment_node) or None.
    Only matches when:
    - The statement is a single expression_statement
    - The top-level expression is assignment_expression  (LHS = RHS)
    - LHS is an identifier matching the m[A-Z] Hmx member convention
    - RHS is a plain identifier (param or local)
    """
    if stmt.type != "expression_statement":
        return None

    # expression_statement may have a semicolon child; find the expression
    expr = None
    for child in stmt.named_children:
        if child.type == "assignment_expression":
            expr = child
            break
    if expr is None:
        return None

    left = expr.child_by_field_name("left")
    right = expr.child_by_field_name("right")
    if left is None or right is None:
        return None

    # LHS must be a plain member identifier (mFoo)
    if left.type != "identifier":
        return None
    member_bytes = source[left.start_byte:left.end_byte]
    if not _MEMBER_RE.match(member_bytes):
        return None

    # RHS must be a plain identifier (param or local)
    if right.type != "identifier":
        return None
    arg_bytes = source[right.start_byte:right.end_byte]

    # Operator must be plain =, not +=, etc.
    op = expr.child_by_field_name("operator")
    if op is None:
        return None
    op_text = source[op.start_byte:op.end_byte]
    if op_text != b"=":
        return None

    return member_bytes, arg_bytes, expr


def _is_reassigned(stmt: Node, source: bytes, name: bytes) -> bool:
    """Return True if `name` appears as the LHS of any assignment in stmt,
    or if it's used in an update_expression (++/--), to detect intervening mutation.
    """
    for node in walk(stmt):
        if node.type == "assignment_expression":
            left = node.child_by_field_name("left")
            if left is not None and source[left.start_byte:left.end_byte] == name:
                return True
        if node.type == "update_expression":
            arg = node.child_by_field_name("argument")
            if arg is not None and source[arg.start_byte:arg.end_byte] == name:
                return True
    return False


def _find_bool_reads(stmt: Node, source: bytes, name: bytes) -> list[Node]:
    """Find identifier nodes named `name` in boolean test contexts within stmt.

    Boolean contexts:
    - condition_clause / parenthesized_expression that is the condition of
      an if_statement, while_statement, for_statement
    - Direct operand of unary `!` (logical_not_expression)
    - Standalone in a condition (the lone expression is a bool test)
    """
    results: list[Node] = []

    for node in walk(stmt):
        if node.type != "identifier":
            continue
        if source[node.start_byte:node.end_byte] != name:
            continue

        # Walk up to see if we're in a boolean context
        parent = node.parent
        if parent is None:
            continue

        # Direct condition in if/while/for
        if _in_condition_context(node):
            results.append(node)

    return results


def _in_condition_context(node: Node) -> bool:
    """Return True if this identifier node is in a boolean-test context."""
    current = node
    while current is not None:
        parent = current.parent
        if parent is None:
            break

        ptype = parent.type

        # Unary logical_not: !arg
        if ptype == "unary_expression":
            op = parent.child_by_field_name("operator")
            if op is not None and op.type == "!" :
                return True

        # condition_clause or parenthesized_expression used as if/while condition
        if ptype in ("condition_clause", "parenthesized_expression"):
            gp = parent.parent
            if gp is not None and gp.type in (
                "if_statement", "while_statement", "for_statement",
                "do_statement",
            ):
                return True

        # Direct child of if_statement condition field
        if ptype == "if_statement":
            cond = parent.child_by_field_name("condition")
            if cond is not None and cond.id == current.id:
                return True

        current = parent

    return False
