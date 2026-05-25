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

Historical note (May 2026):
    Earlier versions produced 93% compile failures (111/120 variants). Failure
    modes were:
      a) Local variables declared inside the if-body were chosen as outer-condition
         guards (out-of-scope use of `str`, `arr`, etc.).
      b) Non-pointer identifiers — references / value-typed members (`mFilename` as
         String, `TheUI` / `TheTaskMgr` accessed via `.`) — were used as guards.
      c) `field_expression` parses both `a->b` AND `c.d`; the latter were being
         collected as `->` dereferences.
      d) Multi-line MILO_LOG / MILO_ASSERT macro calls were wrap-targets.
    The current implementation requires every candidate guard to be a verified
    pointer (declared `Type *name` in this function, OR seen as the left side of a
    real `->` somewhere). Out-of-scope locals are filtered per-condition. Multi-
    line statements are not wrap candidates.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, identifiers_in
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

_BRANCH_OPCODES = {"beq", "bne", "ble", "bgt", "bge", "blt",
                   "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
                   "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-"}


class NullGuardInsertPattern(Pattern):
    name = "null_guard_insert"

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
        # Build the per-function "known pointer" set up front. Every emitted
        # guard must be in this set; otherwise we'd generate code referencing
        # locals declared inside an inner scope (out-of-scope at the guard
        # site) or non-pointer values (`String mFilename`, `TheUI` reference).
        known_pointers = _collect_known_pointer_names(ctx)
        counter = 0

        # Strategy 1: Ghidra-guided
        if ctx.ghidra_ast is not None:
            for variant in self._try_ghidra_guided(ctx, counter, known_pointers):
                yield variant
                counter += 1
            if counter > 0:
                return  # Ghidra guided produced candidates, skip blind

        # Strategy 2: Find pointer dereferences in if-bodies, add && guard
        for variant in self._add_guards_to_conditions(ctx, counter, known_pointers):
            yield variant
            counter += 1

        # Strategy 3: Wrap unguarded dereferences in if (ptr) blocks
        for variant in self._wrap_dereferences(ctx, counter, known_pointers):
            yield variant
            counter += 1

    def _try_ghidra_guided(
        self, ctx: FunctionContext, start_counter: int, known_pointers: set[str],
    ) -> Iterator[Variant]:
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

            # Even Ghidra-guided candidates must be real pointers — otherwise we
            # emit broken C (e.g. Ghidra may surface a stack-variable name that
            # doesn't exist verbatim in our source).
            if guard_name not in known_pointers:
                continue

            guard_bytes = guard_name.encode("utf-8")

            # Find if-statements that dereference this variable in their body
            for if_stmt in _find_if_statements(ctx.body_node):
                if counter >= 10:
                    return

                consequence = if_stmt.child_by_field_name("consequence")
                if consequence is None:
                    continue

                # Out-of-scope check: skip if guard_name is declared inside the
                # if-body (would reference an uninitialized local in the
                # outer condition).
                if _is_name_declared_inside(consequence, guard_name):
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

    def _add_guards_to_conditions(
        self, ctx: FunctionContext, start_counter: int, known_pointers: set[str],
    ) -> Iterator[Variant]:
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

            # Find DIRECT pointer dereferences in the body (->) — limit search
            # to the body's immediate scope (skip nested if-bodies; their
            # dereferences are guarded by their own conditions, and adding the
            # guard at the outer level is usually redundant noise).
            deref_vars = _find_arrow_deref_targets_shallow(consequence, source)
            if not deref_vars:
                continue

            cond_text = source[condition.start_byte:condition.end_byte]

            for var_name in deref_vars:
                if counter >= 8:
                    return

                # Must be a verified pointer in this function.
                if var_name not in known_pointers:
                    continue

                # Must not be a local declared inside the if-body (out of scope
                # at the condition site).
                if _is_name_declared_inside(consequence, var_name):
                    continue

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

    def _wrap_dereferences(
        self, ctx: FunctionContext, start_counter: int, known_pointers: set[str],
    ) -> Iterator[Variant]:
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

                # Multi-line statements (likely MILO_LOG / MILO_ASSERT macros
                # spanning several lines) are skipped — wrapping them produces
                # ugly diffs and the if-body would have to brace the whole
                # thing anyway. We only wrap statements that fit on one line.
                line_start = source.rfind(b"\n", 0, stmt.start_byte) + 1
                line_end = source.find(b"\n", stmt.end_byte)
                if line_end == -1:
                    line_end = len(source)
                if line_end != source.find(b"\n", stmt.start_byte):
                    # Statement spans more than one source line — skip.
                    continue

                # Find arrow dereferences in the statement
                deref_vars = _find_arrow_deref_targets_shallow(stmt, source)
                if len(deref_vars) != 1:
                    continue

                var_name = list(deref_vars)[0]

                # Must be a verified pointer.
                if var_name not in known_pointers:
                    continue

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


def _find_arrow_deref_targets_shallow(node: Node, source: bytes) -> set[str]:
    """Find variables dereferenced via `->` in *node* (not nested if-bodies).

    The earlier helper used `walk()` and tree-sitter's `field_expression` type,
    which matches BOTH `a->b` and `a.b`. We now filter by the literal operator
    text and additionally stop descending into nested `if_statement`/`for`/
    `while` bodies, so a guard derived from a deeply nested dereference doesn't
    get spliced into an outer condition where it's redundant (already inner-
    guarded) or even out of scope.
    """
    results: set[str] = set()

    def visit(n: Node) -> None:
        if n.type == "field_expression":
            op = n.child_by_field_name("operator")
            if op is not None and op.text == b"->":
                obj = n.child_by_field_name("argument")
                if obj is not None and obj.type == "identifier":
                    name = source[obj.start_byte:obj.end_byte].decode(
                        "utf-8", errors="replace"
                    )
                    results.add(name)
        # Don't descend into nested control-flow bodies — their derefs are
        # the responsibility of their own guards.
        if n.type in ("if_statement", "for_statement", "while_statement",
                      "do_statement", "switch_statement"):
            # Visit only the condition / init parts, not the body.
            for child_name in ("condition", "init", "update"):
                ch = n.child_by_field_name(child_name)
                if ch is not None:
                    visit(ch)
            return
        for ch in n.children:
            visit(ch)

    visit(node)
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


# ----------------------------------------------------------------------------
# Pointer-name discovery + scope checks
# ----------------------------------------------------------------------------

def _collect_known_pointer_names(ctx: FunctionContext) -> set[str]:
    """Return the set of identifiers we believe are pointer-typed in this function.

    Conservative — we'd rather skip a real win than emit a broken variant. The
    set is the UNION of:

      1. Local variables declared with `Type *name` (or `Type *name = ...`) in
         the function body, at any nesting depth.
      2. Parameter declarations of the form `Type *name`.
      3. Identifiers seen as the LHS of a real `->` field_expression anywhere
         in the function. (`mFoo->Bar()` proves `mFoo` is a pointer; if the
         field-access uses `.` instead, this signal is absent so the identifier
         is rejected — exactly the `TheUI` / `mFilename` failure we saw.)

    The `.` filter is what makes (3) safe: tree-sitter's `field_expression`
    matches both `->` and `.`, so without an operator check we'd accept
    references and value-typed members as pointers.
    """
    source = ctx.file_source
    out: set[str] = set()

    # (1) + (2): scan declarations and parameters for `*` declarators.
    for n in walk(ctx.func_node):
        if n.type in ("declaration", "parameter_declaration", "field_declaration"):
            _collect_pointer_declarator_names(n, source, out)

    # (3): any identifier that appears as `ident->...` anywhere in the body.
    for n in walk(ctx.body_node):
        if n.type == "field_expression":
            op = n.child_by_field_name("operator")
            if op is None or op.text != b"->":
                continue
            arg = n.child_by_field_name("argument")
            if arg is not None and arg.type == "identifier":
                name = source[arg.start_byte:arg.end_byte].decode(
                    "utf-8", errors="replace"
                )
                out.add(name)

    return out


def _collect_pointer_declarator_names(
    decl_node: Node, source: bytes, out: set[str],
) -> None:
    """Walk a declaration/parameter and collect names declared as `Type *name`."""
    for child in decl_node.children:
        _walk_pointer_declarator(child, source, out, saw_pointer=False)


def _walk_pointer_declarator(
    node: Node, source: bytes, out: set[str], saw_pointer: bool,
) -> None:
    """Recursive: if we pass a pointer_declarator, the inner identifier is a ptr."""
    if node.type == "pointer_declarator":
        saw_pointer = True
        # Pointer-to-array or pointer-to-pointer chains: keep recursing.
        for ch in node.children:
            _walk_pointer_declarator(ch, source, out, saw_pointer)
        return

    if node.type == "init_declarator":
        for ch in node.children:
            _walk_pointer_declarator(ch, source, out, saw_pointer)
        return

    if node.type == "reference_declarator":
        # Type &name — NOT a pointer, abandon this subtree.
        return

    if node.type == "function_declarator":
        # Type (*name)(...) — first child may be a pointer_declarator for fn ptrs.
        for ch in node.children:
            _walk_pointer_declarator(ch, source, out, saw_pointer)
        return

    if node.type == "array_declarator":
        # Type name[N] — not a pointer for our purposes (arrays decay, but the
        # name as written doesn't take `->`). Skip.
        return

    if node.type == "identifier" and saw_pointer:
        out.add(source[node.start_byte:node.end_byte].decode("utf-8", errors="replace"))
        return

    # Descend through declarators we don't recognize specifically.
    for ch in node.children:
        if ch.type in ("pointer_declarator", "init_declarator",
                       "reference_declarator", "function_declarator",
                       "array_declarator", "identifier"):
            _walk_pointer_declarator(ch, source, out, saw_pointer)


def _is_name_declared_inside(scope: Node, name: str) -> bool:
    """Return True if *name* is declared at any nesting level inside *scope*.

    Used to reject guards that would reference a variable that doesn't exist
    yet at the parent if-condition (the local was declared inside the body).
    """
    needle = name.encode("utf-8")
    for n in walk(scope):
        if n.type not in ("declaration", "init_declarator", "pointer_declarator",
                          "reference_declarator", "array_declarator",
                          "function_declarator"):
            continue
        # Cheap pre-filter: name must appear as a token in this declaration.
        if needle not in n.text:
            continue
        # Confirm the name is actually a declarator's identifier, not just a
        # name appearing in the initializer.
        if _declares_identifier(n, name):
            return True
    return False


def _declares_identifier(decl: Node, name: str) -> bool:
    """True if *decl* introduces *name* (as opposed to mentioning it in an init)."""
    needle = name.encode("utf-8")

    def visit(n: Node, in_initializer: bool) -> bool:
        if in_initializer:
            return False
        if n.type == "identifier" and n.text == needle:
            return True
        # Recurse into declarator-shaped children; mark initializer subtrees so
        # we don't false-positive on `Foo *p = q;` thinking `q` is declared.
        for ch in n.children:
            child_in_init = in_initializer
            if n.type == "init_declarator":
                # tree-sitter init_declarator: declarator = value
                # The "value" field is the initializer.
                if n.child_by_field_name("value") is ch:
                    child_in_init = True
            if visit(ch, child_in_init):
                return True
        return False

    return visit(decl, False)
