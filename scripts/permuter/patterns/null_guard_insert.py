"""Null guard insertion — add missing null checks that the target binary has.

Win rate: proven in 1 manual fix (RndAnimatable::OnAnimate, added && taskPtr).

Complement to null_guard_elimination.py (which removes guards). This pattern
adds null checks where the target binary has them but our source doesn't.

Transformations:
    ptr->Method();              -> if (ptr) ptr->Method();
    if (cond) { ptr->M(); }    -> if (cond && ptr) { ptr->M(); }
    if (local_wait) {           -> if (local_wait && taskPtr) {
        taskPtr->BlendTask();         taskPtr->BlendTask();

Detection signals:
    - Delete clusters with cmplwi + beq/bne (null check in target, missing in source)
    - Ghidra shows `if (ptr != (TYPE *)0x0)` that we don't have

Strategy:
    1. Ghidra-guided: Diff Ghidra null checks vs source null checks, insert missing
    2. Blind: Find pointer dereferences inside if-bodies, try adding && ptr guards
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, node_text, identifiers_in
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

_BRANCH_OPCODES = {"beq", "bne", "ble", "bgt", "bge", "blt",
                   "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
                   "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-"}


class NullGuardInsertPattern(Pattern):
    name = "null_guard_insert"
    # opt_in: 111/120 variants failed compile (93%, 0 wins from 10 runs).
    # Inserted guards corrupt control flow when the original logic already
    # short-circuits, or generated guards reference uninitialized variables.
    opt_in = True

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Delete clusters suggest missing code (target has instructions we don't)
        for c in diagnosis.clusters:
            if c.deletes > 0:
                return True
        # Branch/compare mismatches could indicate missing guard
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
            if d.target_opcode in ("cmpwi", "cmplwi") or \
               d.base_opcode in ("cmpwi", "cmplwi"):
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Higher priority if we see delete-only clusters (our code is shorter)
        delete_heavy = any(
            c.deletes > c.inserts for c in diagnosis.clusters
        )
        if delete_heavy:
            return 0.5
        return 0.2

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0

        # Strategy 1: Ghidra-guided
        if ctx.ghidra_ast is not None:
            for variant in self._try_ghidra_guided(ctx, counter):
                yield variant
                counter += 1
            if counter > 0:
                return  # Ghidra guided produced candidates, skip blind

        # Strategy 2: Find pointer dereferences in if-bodies, add && guard
        for variant in self._add_guards_to_conditions(ctx, counter):
            yield variant
            counter += 1

        # Strategy 3: Wrap unguarded dereferences in if (ptr) blocks
        for variant in self._wrap_dereferences(ctx, counter):
            yield variant
            counter += 1

    def _try_ghidra_guided(self, ctx: FunctionContext, start_counter: int) -> Iterator[Variant]:
        """Use Ghidra to find null checks in target that we're missing."""
        if ctx.ghidra_ast is None:
            return

        # Import Ghidra null check extraction from the elimination pattern
        from .null_guard_elimination import (
            _extract_ghidra_null_checks,
            _extract_source_null_checks,
        )

        ghidra_guards = _extract_ghidra_null_checks(ctx.ghidra_ast)
        source_guards = _extract_source_null_checks(ctx.body_node, ctx.file_source)

        # Guards in Ghidra but NOT in source -> should be added
        missing = ghidra_guards - source_guards
        if not missing:
            return

        source = ctx.file_source
        counter = start_counter

        # For each missing guard, find dereferences of that variable and add guards
        for guard_name in missing:
            if counter >= 10:
                return

            guard_bytes = guard_name.encode("utf-8")

            # Find if-statements that dereference this variable in their body
            for if_stmt in _find_if_statements(ctx.body_node):
                if counter >= 10:
                    return

                consequence = if_stmt.child_by_field_name("consequence")
                if consequence is None:
                    continue

                body_text = source[consequence.start_byte:consequence.end_byte]
                if guard_bytes not in body_text:
                    continue

                # Check the condition doesn't already guard this variable
                condition = if_stmt.child_by_field_name("condition")
                if condition is None:
                    continue
                cond_text = source[condition.start_byte:condition.end_byte]
                if guard_bytes in cond_text:
                    continue

                # Add && guard_name to the condition
                variant = _add_and_guard(
                    source, condition, guard_name, counter,
                    f"[ghidra] Add null guard: && {guard_name}"
                )
                if variant:
                    yield variant
                    counter += 1

    def _add_guards_to_conditions(self, ctx: FunctionContext, start_counter: int) -> Iterator[Variant]:
        """Find if-conditions whose body dereferences pointers, add && ptr."""
        source = ctx.file_source
        counter = start_counter

        for if_stmt in _find_if_statements(ctx.body_node):
            if counter >= 8:
                return

            consequence = if_stmt.child_by_field_name("consequence")
            condition = if_stmt.child_by_field_name("condition")
            if consequence is None or condition is None:
                continue

            # Find pointer dereferences in the body (->)
            deref_vars = _find_arrow_deref_targets(consequence, source)
            if not deref_vars:
                continue

            cond_text = source[condition.start_byte:condition.end_byte]

            for var_name in deref_vars:
                if counter >= 8:
                    return

                var_bytes = var_name.encode("utf-8")

                # Skip if already guarded in condition
                if var_bytes in cond_text:
                    continue

                # Skip if the variable is defined locally in the condition
                # (e.g., if (Type* ptr = GetPtr()) — ptr is always non-null here)
                cond_ids = identifiers_in(condition)
                if var_name not in cond_ids:
                    # Variable isn't referenced in condition at all — good candidate
                    variant = _add_and_guard(
                        source, condition, var_name, counter,
                        f"Add null guard: && {var_name}"
                    )
                    if variant:
                        yield variant
                        counter += 1

    def _wrap_dereferences(self, ctx: FunctionContext, start_counter: int) -> Iterator[Variant]:
        """Wrap standalone pointer dereferences in if (ptr) blocks."""
        source = ctx.file_source
        counter = start_counter

        for compound in _find_compound_statements(ctx.body_node):
            if counter >= 6:
                return

            for stmt in compound.named_children:
                if counter >= 6:
                    return

                if stmt.type != "expression_statement":
                    continue

                # Find arrow dereferences in the statement
                deref_vars = _find_arrow_deref_targets(stmt, source)
                if len(deref_vars) != 1:
                    continue

                var_name = list(deref_vars)[0]

                # Check if already inside an if guard for this variable
                parent = stmt.parent
                if parent and parent.type == "compound_statement":
                    grandparent = parent.parent
                    if grandparent and grandparent.type == "if_statement":
                        cond = grandparent.child_by_field_name("condition")
                        if cond:
                            cond_text = source[cond.start_byte:cond.end_byte]
                            if var_name.encode("utf-8") in cond_text:
                                continue  # Already guarded

                # Wrap in if (var_name)
                indent = get_indent(source, stmt)
                stmt_text = source[stmt.start_byte:stmt.end_byte]

                ed = SourceEditor(source)
                replacement = (
                    indent + f"if ({var_name})\n".encode()
                    + indent + b"    " + stmt_text
                )
                ed.replace_range(stmt.start_byte - len(indent), stmt.end_byte, replacement)

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                yield Variant(
                    name=f"nullins_{counter}",
                    pattern_name=self.name,
                    description=f"Wrap in if ({var_name})",
                    source=new_source,
                )
                counter += 1


def _add_and_guard(
    source: bytes, condition: Node, guard_name: str, counter: int,
    description: str,
) -> Variant | None:
    """Add && guard_name to an existing condition."""
    # Find the inner expression
    inner = _get_inner_expr(condition)
    if inner is None:
        return None

    ed = SourceEditor(source)
    guard_bytes = guard_name.encode("utf-8")

    # Insert && guard_name before the existing condition expression
    new_cond = guard_bytes + b" && " + source[inner.start_byte:inner.end_byte]
    ed.replace_range(inner.start_byte, inner.end_byte, new_cond)

    try:
        new_source = ed.apply()
    except ValueError:
        return None

    return Variant(
        name=f"nullins_{counter}",
        pattern_name="null_guard_insert",
        description=description,
        source=new_source,
    )


def _find_if_statements(node: Node) -> Iterator[Node]:
    """Find all if_statement nodes recursively."""
    for n in walk(node):
        if n.type == "if_statement":
            yield n


def _find_compound_statements(body: Node) -> list[Node]:
    """Find all compound_statement nodes including nested ones."""
    results = []
    for n in walk(body):
        if n.type == "compound_statement":
            results.append(n)
    return results


def _find_arrow_deref_targets(node: Node, source: bytes) -> set[str]:
    """Find variables that are dereferenced via -> in the given subtree."""
    results = set()
    for n in walk(node):
        if n.type == "field_expression":
            # ptr->member
            obj = n.child_by_field_name("argument")
            if obj is not None and obj.type == "identifier":
                name = source[obj.start_byte:obj.end_byte].decode("utf-8", errors="replace")
                results.add(name)
    return results


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
        for child in condition.named_children:
            if child.type != "comment":
                return child
        return None
    return current
