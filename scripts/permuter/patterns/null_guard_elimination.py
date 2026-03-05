"""Null guard elimination — remove redundant null checks on globals/pointers.

Win rate: untested (new pattern, proven in 4 manual fixes on MetaPanel).

When a global pointer (TheXxx pattern) or other pointer is null-checked
before use, but the target binary doesn't have the check, removing it
eliminates an extra branch and comparison instruction.

Transformations:
    if (ptr) ptr->Method();        -> ptr->Method();
    if (ptr) { ptr->Method(); }    -> ptr->Method();   or   { ptr->Method(); }
    if (A && B) { body }           -> if (B) { body }   (drop leading operand)
    ... || (ptr && ptr->M()) || ...-> ... || ptr->M() || ...

Detection signals:
    - Insert/delete clusters (extra branch/comparison in base)
    - Branch opcode mismatches (beq/bne from null check)
    - Base has more instructions than target
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, get_line_start
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

_BRANCH_OPCODES = {"beq", "bne", "ble", "bgt", "bge", "blt",
                   "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
                   "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-"}


class NullGuardEliminationPattern(Pattern):
    name = "null_guard_elimination"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Branch opcode differences (from extra null check branches)
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True

        # Clusters (insert/delete from extra instructions)
        if diagnosis.clusters:
            return True

        # Compare instruction differences (cmpwi/cmplwi from null check)
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("cmpwi", "cmplwi") or \
               d.base_opcode in ("cmpwi", "cmplwi"):
                return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Higher priority when we see insert clusters (base has extra instructions)
        insert_heavy = any(
            c.inserts > c.deletes for c in diagnosis.clusters
        )
        if insert_heavy:
            return 0.5
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0

        # Ghidra-guided: only remove guards absent in target
        if ctx.ghidra_ast is not None:
            for variant in self._try_ghidra_guided(ctx, counter):
                yield variant
                counter += 1
            if counter > 0:
                return  # Ghidra guided produced candidates, skip blind

        # Walk all statements including nested scopes
        for compound in _find_compound_statements(ctx.body_node):
            if counter >= 10:
                break

            stmts = list(compound.named_children)
            for stmt in stmts:
                if counter >= 10:
                    break

                # Strategy 1: if (ptr) ptr->Method(); -> ptr->Method();
                for variant in _eliminate_guard_call(stmt, source, counter):
                    yield variant
                    counter += 1

                # Strategy 2: if (A && B) { body } -> if (B) { body }
                for variant in _drop_leading_and_operand(stmt, source, counter):
                    yield variant
                    counter += 1

                # Strategy 3: ... || (ptr && expr) || ... -> ... || expr || ...
                for variant in _simplify_or_chain_guard(stmt, source, counter):
                    yield variant
                    counter += 1

    def _try_ghidra_guided(self, ctx: FunctionContext, start_counter: int) -> Iterator[Variant]:
        """Use Ghidra to identify which null checks are absent in the target."""
        if ctx.ghidra_ast is None:
            return

        # Find all null-checked identifiers in the Ghidra output
        ghidra_guards = _extract_ghidra_null_checks(ctx.ghidra_ast)

        # Find all null-checked identifiers in our source
        source_guards = _extract_source_null_checks(ctx.body_node, ctx.file_source)

        # Guards in our source but NOT in Ghidra -> should be removed
        removable = source_guards - ghidra_guards

        if not removable:
            return

        counter = start_counter
        source = ctx.file_source

        for compound in _find_compound_statements(ctx.body_node):
            if counter >= 10:
                break
            stmts = list(compound.named_children)
            for stmt in stmts:
                if counter >= 10:
                    break
                # Only try removal if the guard variable is in the removable set
                if stmt.type == "if_statement":
                    guard_var = _get_guard_variable(stmt, source)
                    if guard_var and guard_var in removable:
                        for variant in _eliminate_guard_call(stmt, source, counter):
                            yield Variant(
                                name=f"ghidra_nullguard_{counter}",
                                pattern_name=variant.pattern_name,
                                description=f"[ghidra] {variant.description}",
                                source=variant.source,
                            )
                            counter += 1
                        for variant in _drop_leading_and_operand(stmt, source, counter):
                            yield Variant(
                                name=f"ghidra_nullguard_{counter}",
                                pattern_name=variant.pattern_name,
                                description=f"[ghidra] {variant.description}",
                                source=variant.source,
                            )
                            counter += 1


def _extract_ghidra_null_checks(ghidra_ast) -> set[str]:
    """Find identifiers that are null-checked in Ghidra output.

    Looks for patterns like:
    - if (var != (TYPE *)0x0)
    - if (var != 0)
    - if (var) (implicit null check)
    """
    from ..ghidra_ast import _walk_all

    if ghidra_ast.body_node is None:
        return set()

    code_bytes = ghidra_ast.code.encode("utf-8")
    guards: set[str] = set()

    for node in _walk_all(ghidra_ast.body_node):
        if node.type != "if_statement":
            continue

        condition = node.child_by_field_name("condition")
        if condition is None:
            continue

        # Get the inner expression
        inner = _get_ghidra_condition_inner(condition)
        if inner is None:
            continue

        # Case 1: if (var != (TYPE *)0x0) or if (var != 0)
        if inner.type == "binary_expression":
            op = inner.child_by_field_name("operator")
            if op is not None and op.text in (b"!=", b"=="):
                left = inner.child_by_field_name("left")
                right = inner.child_by_field_name("right")
                if left is not None and right is not None:
                    # Check if right side is a null literal (0, 0x0, (TYPE *)0x0)
                    if _is_null_literal(right, code_bytes):
                        name = _extract_base_name(left, code_bytes)
                        if name:
                            guards.add(name)
                    # Also check reversed: 0 != var
                    elif _is_null_literal(left, code_bytes):
                        name = _extract_base_name(right, code_bytes)
                        if name:
                            guards.add(name)

            # Case 2: if (var && ...) — conjunction with leading null check
            if op is not None and op.text == b"&&":
                left = inner.child_by_field_name("left")
                if left is not None:
                    # Left operand of && might be a null check itself
                    _collect_null_check_names(left, code_bytes, guards)

        # Case 3: if (var) — implicit null check (identifier used as condition)
        elif inner.type == "identifier":
            name = code_bytes[inner.start_byte:inner.end_byte].decode("utf-8", errors="replace")
            guards.add(_strip_ghidra_prefix(name))

    return guards


def _extract_source_null_checks(body_node: Node, source: bytes) -> set[str]:
    """Find identifiers that are null-checked in our source."""
    guards: set[str] = set()

    for node in walk(body_node):
        if node.type != "if_statement":
            continue

        condition = node.child_by_field_name("condition")
        if condition is None:
            continue

        inner = _get_inner_expr(condition)
        if inner is None:
            continue

        # if (ptr)
        if inner.type == "identifier":
            name = source[inner.start_byte:inner.end_byte].decode("utf-8", errors="replace")
            guards.add(name)

        # if (ptr != nullptr) or if (ptr != 0)
        elif inner.type == "binary_expression":
            op = inner.child_by_field_name("operator")
            if op is not None and op.text in (b"!=", b"=="):
                left = inner.child_by_field_name("left")
                right = inner.child_by_field_name("right")
                if left is not None and right is not None:
                    right_text = source[right.start_byte:right.end_byte].strip()
                    if right_text in (b"nullptr", b"0", b"NULL"):
                        if left.type == "identifier":
                            name = source[left.start_byte:left.end_byte].decode("utf-8", errors="replace")
                            guards.add(name)

            # if (A && B) where A is a simple identifier (null guard)
            if op is not None and op.text == b"&&":
                left = inner.child_by_field_name("left")
                if left is not None and left.type == "identifier":
                    name = source[left.start_byte:left.end_byte].decode("utf-8", errors="replace")
                    guards.add(name)

    return guards


def _get_guard_variable(stmt: Node, source: bytes) -> str | None:
    """Get the variable name being null-checked in an if statement."""
    if stmt.type != "if_statement":
        return None

    condition = stmt.child_by_field_name("condition")
    if condition is None:
        return None

    inner = _get_inner_expr(condition)
    if inner is None:
        return None

    # if (ptr)
    if inner.type == "identifier":
        return source[inner.start_byte:inner.end_byte].decode("utf-8", errors="replace")

    # if (A && B) where A is a simple identifier
    if inner.type == "binary_expression":
        op = inner.child_by_field_name("operator")
        if op is not None and op.text == b"&&":
            left = inner.child_by_field_name("left")
            if left is not None and left.type == "identifier":
                return source[left.start_byte:left.end_byte].decode("utf-8", errors="replace")

    return None


def _get_ghidra_condition_inner(condition: Node) -> Node | None:
    """Get the inner expression from a Ghidra condition (parenthesized_expression)."""
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None


def _is_null_literal(node: Node, code_bytes: bytes) -> bool:
    """Check if a node represents a null/zero literal, including casts like (TYPE *)0x0."""
    text = code_bytes[node.start_byte:node.end_byte].strip()

    # Direct null literals
    if text in (b"0", b"0x0", b"0x00", b"NULL", b"nullptr"):
        return True

    # Cast expression: (TYPE *)0x0
    if node.type == "cast_expression":
        value = node.child_by_field_name("value")
        if value is not None:
            return _is_null_literal(value, code_bytes)

    # Parenthesized: (0)
    if node.type == "parenthesized_expression":
        for child in node.named_children:
            return _is_null_literal(child, code_bytes)

    return False


def _extract_base_name(node: Node, code_bytes: bytes) -> str | None:
    """Extract a base identifier name from a node, stripping Ghidra prefixes."""
    if node.type == "identifier":
        name = code_bytes[node.start_byte:node.end_byte].decode("utf-8", errors="replace")
        return _strip_ghidra_prefix(name)

    # Handle pointer dereference: *var
    if node.type == "pointer_expression":
        for child in node.named_children:
            if child.type == "identifier":
                name = code_bytes[child.start_byte:child.end_byte].decode("utf-8", errors="replace")
                return _strip_ghidra_prefix(name)

    return None


def _strip_ghidra_prefix(name: str) -> str:
    """Strip Ghidra-specific prefixes to get a base name for comparison.

    Ghidra often preserves global names (TheXxx, gXxx) as-is, but may
    rename locals. For globals, the name is typically preserved exactly.
    """
    # Ghidra typically preserves global names like TheMetaMusic as-is
    return name


def _collect_null_check_names(node: Node, code_bytes: bytes, guards: set[str]) -> None:
    """Collect identifier names from null-check sub-expressions."""
    if node.type == "identifier":
        name = code_bytes[node.start_byte:node.end_byte].decode("utf-8", errors="replace")
        guards.add(_strip_ghidra_prefix(name))
    elif node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op is not None and op.text in (b"!=", b"=="):
            left = node.child_by_field_name("left")
            right = node.child_by_field_name("right")
            if left is not None and right is not None:
                if _is_null_literal(right, code_bytes):
                    name = _extract_base_name(left, code_bytes)
                    if name:
                        guards.add(name)
                elif _is_null_literal(left, code_bytes):
                    name = _extract_base_name(right, code_bytes)
                    if name:
                        guards.add(name)


def _find_compound_statements(body: Node) -> list[Node]:
    """Find all compound_statement nodes including nested ones."""
    results = []
    for n in walk(body):
        if n.type == "compound_statement":
            results.append(n)
    return results


def _eliminate_guard_call(
    stmt: Node, source: bytes, counter: int
) -> Iterator[Variant]:
    """Remove `if (ptr) ptr->Method(args);` -> `ptr->Method(args);`.

    Also handles `if (ptr) { ptr->Method(args); }` with single-statement body.
    """
    if stmt.type != "if_statement":
        return

    condition = stmt.child_by_field_name("condition")
    consequence = stmt.child_by_field_name("consequence")
    alternative = stmt.child_by_field_name("alternative")

    if condition is None or consequence is None:
        return

    # Must have no else branch
    if alternative is not None:
        return

    # Get the condition expression (unwrap parenthesized/condition_clause)
    cond_expr = _get_inner_expr(condition)
    if cond_expr is None:
        return

    # Condition must be a simple identifier (the pointer being checked)
    if cond_expr.type != "identifier":
        return
    guard_name = source[cond_expr.start_byte:cond_expr.end_byte]

    # Get the guarded statement(s)
    if consequence.type == "compound_statement":
        # { ptr->Method(); } — must have exactly one statement
        inner_stmts = [c for c in consequence.named_children if c.type != "comment"]
        if len(inner_stmts) != 1:
            return
        guarded_stmt = inner_stmts[0]
    else:
        # ptr->Method(); (no braces)
        guarded_stmt = consequence

    # The guarded statement must use the same pointer
    guarded_text = source[guarded_stmt.start_byte:guarded_stmt.end_byte]
    if guard_name not in guarded_text:
        return

    # Replace the entire if statement with just the guarded statement
    indent = get_indent(source, stmt)
    ed = SourceEditor(source)

    # Include trailing newline in the replacement range
    replace_end = stmt.end_byte
    while replace_end < len(source) and source[replace_end:replace_end + 1] in (b"\n", b"\r"):
        replace_end += 1
    replace_start = stmt.start_byte
    while replace_start > 0 and source[replace_start - 1:replace_start] in (b" ", b"\t"):
        replace_start -= 1

    replacement = indent + guarded_text
    if not replacement.endswith(b";"):
        replacement += b";"
    replacement += b"\n"

    ed.delete_range(replace_start, replace_end)
    ed.insert_at(replace_start, replacement)

    try:
        new_source = ed.apply()
    except ValueError:
        return

    guard_str = guard_name.decode("utf-8", errors="replace")
    yield Variant(
        name=f"nullguard_{counter}",
        pattern_name="null_guard_elimination",
        description=f"Remove null guard: if ({guard_str}) ...",
        source=new_source,
    )


def _drop_leading_and_operand(
    stmt: Node, source: bytes, counter: int
) -> Iterator[Variant]:
    """Transform `if (A && B) { body }` -> `if (B) { body }`.

    Removes the leading operand of an && chain in an if-condition.
    This handles cases where A is a null check that the target doesn't have.
    """
    if stmt.type != "if_statement":
        return

    condition = stmt.child_by_field_name("condition")
    if condition is None:
        return

    cond_expr = _get_inner_expr(condition)
    if cond_expr is None or cond_expr.type != "binary_expression":
        return

    op = cond_expr.child_by_field_name("operator")
    if op is None or op.text != b"&&":
        return

    left = cond_expr.child_by_field_name("left")
    right = cond_expr.child_by_field_name("right")
    if left is None or right is None:
        return

    # Drop the left operand (keep the right)
    right_text = source[right.start_byte:right.end_byte]
    ed = SourceEditor(source)
    ed.replace_range(cond_expr.start_byte, cond_expr.end_byte, right_text)

    try:
        new_source = ed.apply()
    except ValueError:
        return

    left_str = source[left.start_byte:left.end_byte].decode("utf-8", errors="replace")
    if len(left_str) > 30:
        left_str = left_str[:27] + "..."
    yield Variant(
        name=f"nullguard_{counter}",
        pattern_name="null_guard_elimination",
        description=f"Drop && operand: {left_str}",
        source=new_source,
    )

    # Also try dropping the right operand (keep the left)
    counter += 1
    left_text = source[left.start_byte:left.end_byte]
    ed2 = SourceEditor(source)
    ed2.replace_range(cond_expr.start_byte, cond_expr.end_byte, left_text)

    try:
        new_source2 = ed2.apply()
    except ValueError:
        return

    right_str = source[right.start_byte:right.end_byte].decode("utf-8", errors="replace")
    if len(right_str) > 30:
        right_str = right_str[:27] + "..."
    yield Variant(
        name=f"nullguard_{counter}",
        pattern_name="null_guard_elimination",
        description=f"Drop && operand: {right_str}",
        source=new_source2,
    )


def _simplify_or_chain_guard(
    stmt: Node, source: bytes, counter: int
) -> Iterator[Variant]:
    """Simplify `(ptr && ptr->Method())` inside || chains to just `ptr->Method()`.

    Finds binary_expression nodes with && where the left operand is a simple
    identifier and the right uses that identifier, then replaces the entire
    && expression with just the right operand.
    """
    for node in walk(stmt):
        if counter >= 10:
            return

        if node.type != "binary_expression":
            continue

        op = node.child_by_field_name("operator")
        if op is None or op.text != b"&&":
            continue

        left = node.child_by_field_name("left")
        right = node.child_by_field_name("right")
        if left is None or right is None:
            continue

        # Left must be a simple identifier (the null check)
        if left.type != "identifier":
            continue

        guard_name = source[left.start_byte:left.end_byte]
        right_text = source[right.start_byte:right.end_byte]

        # Right must reference the same identifier
        if guard_name not in right_text:
            continue

        # The parent must be part of a larger || expression or condition
        # (not a standalone if-condition — that's handled by _drop_leading_and_operand)
        parent = node.parent
        if parent is not None and parent.type == "binary_expression":
            parent_op = parent.child_by_field_name("operator")
            if parent_op is not None and parent_op.text == b"||":
                # This && is inside a || chain — simplify it
                ed = SourceEditor(source)
                ed.replace_range(node.start_byte, node.end_byte, right_text)

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                guard_str = guard_name.decode("utf-8", errors="replace")
                yield Variant(
                    name=f"nullguard_{counter}",
                    pattern_name="null_guard_elimination",
                    description=f"Remove {guard_str} guard from || chain",
                    source=new_source,
                )
                counter += 1

        # Also handle parenthesized (ptr && ptr->M()) inside || chains
        if parent is not None and parent.type == "parenthesized_expression":
            grandparent = parent.parent
            if grandparent is not None and grandparent.type == "binary_expression":
                gp_op = grandparent.child_by_field_name("operator")
                if gp_op is not None and gp_op.text == b"||":
                    ed = SourceEditor(source)
                    # Replace the parenthesized expression with just the right operand
                    ed.replace_range(parent.start_byte, parent.end_byte, right_text)

                    try:
                        new_source = ed.apply()
                    except ValueError:
                        continue

                    guard_str = guard_name.decode("utf-8", errors="replace")
                    yield Variant(
                        name=f"nullguard_{counter}",
                        pattern_name="null_guard_elimination",
                        description=f"Remove {guard_str} guard from || chain (parenthesized)",
                        source=new_source,
                    )
                    counter += 1


def _get_inner_expr(condition: Node) -> Node | None:
    """Extract the inner expression from a condition_clause or parenthesized_expression."""
    current = condition
    while current.type in ("condition_clause", "parenthesized_expression"):
        children = [c for c in current.named_children if c.type != "comment"]
        if len(children) == 1:
            current = children[0]
        else:
            break
    if current.id == condition.id:
        # Didn't unwrap anything — check named children
        for child in condition.named_children:
            if child.type != "comment":
                return child
        return None
    return current
