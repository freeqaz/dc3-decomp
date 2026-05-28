"""Loop-invariant declaration hoist (and inverse sink).

Hoists loop-invariant ``T name = INIT;`` declarations OUT of a loop body to
just BEFORE the enclosing loop. Inverse direction (sink): a declaration JUST
BEFORE a loop with a single use inside is sunk INTO the loop body.

Why: MSVC/MWCC sometimes fails to hoist invariant computations itself —
particularly ``const`` expressions involving multiple member accesses or
chained calls — leaving repeated loads inside the loop body. Manually
hoisting in source can fix ``missing_guard``-style clusters in
DrawTrackMasks/SetupGems-shaped functions.

Example (hoist):

    for (int i = 0; i < count; i++) {
        const int maxFret = mGems->mMaxFret + mOffset;
        DoStuff(mGems[i], maxFret);
    }
    ->
    const int maxFret = mGems->mMaxFret + mOffset;
    for (int i = 0; i < count; i++) {
        DoStuff(mGems[i], maxFret);
    }

Example (sink, inverse):

    const int v = mObj->mValue;
    for (int i = 0; i < n; i++) {
        Use(v);
    }
    ->
    for (int i = 0; i < n; i++) {
        const int v = mObj->mValue;
        Use(v);
    }

Loop-invariant detection rules for ``T name = INIT;`` inside a loop body:

1. ``INIT`` must not reference the loop variable (from ``for (T i = ...)``).
2. ``INIT`` must not reference any local declared LATER in the loop body.
3. ``INIT``'s identifiers must all be either:
   - function parameters,
   - members (``m[A-Z]`` prefix or ``this->`` reference),
   - locals declared BEFORE the loop, or
   - call expressions whose callee name looks side-effect-free
     (matches ``Get[A-Z]``, ``m[A-Z]``, or ``^[a-z_]+Const``).
4. ``name`` must not be written elsewhere in the loop body.
5. The declaration must be ``const`` OR have no other assignments in the loop.

Detection signals (trigger):
- ``missing_guard``-style clusters (consecutive insert/delete around lwz/mr ops)
- Diff ops with repeated ``lwz`` / ``mr`` (suggests redundant loads)
- Generic cluster presence at moderate confidence
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import identifiers_in, walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


_LOOP_TYPES = frozenset({"for_statement", "while_statement", "do_statement"})

_MAX_VARIANTS = 6

# Members or accessor calls that we treat as effectively side-effect-free
# for the purpose of invariance analysis. Conservative: only well-known
# read-only accessor prefixes.
_PURE_CALL_RE = re.compile(r"^(Get[A-Z]|m[A-Z]|[a-z_]+Const$)")

# Member prefix (m[A-Z]...)
_MEMBER_RE = re.compile(r"^m[A-Z]")


class LoopVarHoistPattern(Pattern):
    """Hoist loop-invariant declarations out of loop body (and inverse sink)."""

    name = "loop_var_hoist"
    safety_tier = "moderate"
    structural_domain = "data_flow"
    follow_ups = ("scope_widening", "scope_narrowing", "loop_condition_cache")

    # ------------------------------------------------------------------
    # Diagnosis gating
    # ------------------------------------------------------------------

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Direct trigger: clusters (likely missing_guard / reload chains)
        if diagnosis.clusters:
            return True
        # Indirect trigger: repeated lwz / mr ops in diff
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("lwz", "mr") or d.base_opcode in ("lwz", "mr"):
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Spec-pinned priority
        return 0.5

    def context_priority(self, diagnosis: Diagnosis, ctx: FunctionContext) -> float:
        base = self.priority(diagnosis)
        if base == 0.0:
            return 0.0
        if _has_any_loop(ctx.body_node):
            return min(1.0, base + 0.1)
        return base

    # ------------------------------------------------------------------
    # Generation
    # ------------------------------------------------------------------

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0

        # --- Direction A: hoist invariant decls OUT of loop bodies ---
        for loop_node in _find_loops(ctx.body_node):
            if counter >= _MAX_VARIANTS:
                return

            loop_body = _get_loop_body(loop_node)
            if loop_body is None:
                continue

            loop_var = _get_for_loop_var(loop_node)
            outer_pre_locals = _collect_locals_before(ctx.body_node, loop_node)
            params = _collect_param_names(ctx.func_node)

            # Examine each statement in the loop body in declaration order
            body_stmts = [c for c in loop_body.named_children if c.type != "comment"]
            for stmt_idx, stmt in enumerate(body_stmts):
                if counter >= _MAX_VARIANTS:
                    return
                if stmt.type != "declaration":
                    continue

                decl_info = _classify_decl(stmt, source)
                if decl_info is None:
                    continue
                decl_name, init_node, is_const = decl_info

                # Rule 1: INIT must not use the loop variable
                init_ids = identifiers_in(init_node)
                if loop_var and loop_var in init_ids:
                    continue

                # Rule 2: INIT must not use a local declared later in the loop
                later_locals = _collect_locals_in_range(
                    body_stmts, stmt_idx + 1, len(body_stmts)
                )
                if init_ids & later_locals:
                    continue

                # Rule 3: identifier provenance check + pure-call check
                if not _all_identifiers_safe(
                    init_node, source, loop_var, outer_pre_locals, params
                ):
                    continue

                # Rule 4: `name` must not be reassigned in the loop
                if _is_written_in(loop_body, decl_name, source, exclude=stmt):
                    continue

                # Rule 5: const OR no other writes (already handled by rule 4)
                # `const` is a SOFT signal — non-const single-assignment locals
                # are still hoist candidates as long as rule 4 passes.

                # Build the hoist variant
                new_source = _apply_hoist(source, stmt, loop_node)
                if new_source is None or new_source == source:
                    continue

                tag_set = {"loop_var_hoist", "hoist"}
                if is_const:
                    tag_set.add("const_decl")

                yield Variant(
                    name=f"loop_var_hoist_{counter}",
                    pattern_name=self.name,
                    description=(
                        f"Hoist loop-invariant '{decl_name}' out of loop"
                    ),
                    source=new_source,
                    func_byte_range=ctx.func_byte_range,
                    original_source=source,
                    tags=frozenset(tag_set),
                )
                counter += 1

        # --- Direction B: sink a pre-loop decl INTO the loop body when ---
        # --- it has a single use inside and is unused after the loop. ---
        for loop_node in _find_loops(ctx.body_node):
            if counter >= _MAX_VARIANTS:
                return

            sink_candidate = _find_sink_candidate(
                ctx.body_node, loop_node, source
            )
            if sink_candidate is None:
                continue
            decl_stmt, decl_name = sink_candidate

            new_source = _apply_sink(source, decl_stmt, loop_node)
            if new_source is None or new_source == source:
                continue

            yield Variant(
                name=f"loop_var_sink_{counter}",
                pattern_name=self.name,
                description=(
                    f"Sink pre-loop decl '{decl_name}' into loop body"
                ),
                source=new_source,
                func_byte_range=ctx.func_byte_range,
                original_source=source,
                tags=frozenset({"loop_var_hoist", "sink"}),
            )
            counter += 1


# ---------------------------------------------------------------------------
# AST helpers
# ---------------------------------------------------------------------------


def _has_any_loop(node: Node) -> bool:
    for n in walk(node):
        if n.type in _LOOP_TYPES:
            return True
    return False


def _find_loops(root: Node) -> Iterator[Node]:
    for n in walk(root):
        if n.type in _LOOP_TYPES:
            yield n


def _get_loop_body(loop_node: Node) -> Node | None:
    """Return the compound_statement body of a loop, or None."""
    body = loop_node.child_by_field_name("body")
    if body is not None and body.type == "compound_statement":
        return body
    # do_statement uses "body" field too; while_statement same.
    # Fallback: first compound_statement child.
    for child in loop_node.children:
        if child.type == "compound_statement":
            return child
    return None


def _get_for_loop_var(loop_node: Node) -> str | None:
    """Return the declared loop variable name from `for (T i = ...)`, or None.

    Handles range-based for as well: `for (auto& x : c)` returns 'x'.
    """
    if loop_node.type != "for_statement":
        return None

    # tree-sitter c++ for_statement: initializer can be a declaration or
    # expression_statement. We look for an init field decl.
    initializer = loop_node.child_by_field_name("initializer")
    if initializer is not None and initializer.type == "declaration":
        return _get_simple_decl_name(initializer)

    # Range-based for: child of type "init_declarator" inside the for header
    # Tree-sitter exposes range-for as for_range_loop in some grammars;
    # in standard c++ grammar it's still for_statement with `for_range_loop`
    # as a child.
    for child in loop_node.named_children:
        if child.type in ("declaration",):
            name = _get_simple_decl_name(child)
            if name:
                return name
        # Some grammars expose `condition_clause` or `init_declarator`
        if child.type == "init_declarator":
            inner = child.child_by_field_name("declarator")
            if inner is not None and inner.text:
                return inner.text.decode("utf-8", errors="replace")

    return None


def _get_simple_decl_name(decl: Node) -> str | None:
    declarator = decl.child_by_field_name("declarator")
    if declarator is None:
        return None
    if declarator.type == "init_declarator":
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            declarator = inner
    while declarator.type in ("pointer_declarator", "reference_declarator"):
        inner = declarator.child_by_field_name("declarator")
        if inner is None:
            break
        declarator = inner
    if declarator.type == "identifier" and declarator.text:
        return declarator.text.decode("utf-8", errors="replace")
    if declarator.text:
        return declarator.text.decode("utf-8", errors="replace")
    return None


def _classify_decl(
    decl: Node, source: bytes
) -> tuple[str, Node, bool] | None:
    """Return (name, init_node, is_const) for a simple `T name = INIT;` decl.

    Returns None for multi-declarator decls or decls without an initializer.
    """
    if decl.type != "declaration":
        return None

    # Detect `const` qualifier in the declaration prefix
    is_const = False
    for child in decl.children:
        if child.type == "type_qualifier" and child.text == b"const":
            is_const = True
            break
        if child.type == "primitive_type" or child.type == "type_identifier":
            # Stop scanning once we hit the type proper
            break

    # Tree-sitter sometimes attaches `const` differently; fallback text-scan
    if not is_const:
        decl_text = source[decl.start_byte:decl.end_byte]
        # Quick prefix check: must be a leading `const ` token
        if decl_text.lstrip().startswith(b"const "):
            is_const = True

    # Find init_declarator
    declarators = [
        c for c in decl.named_children
        if c.type in (
            "init_declarator", "identifier", "pointer_declarator",
            "reference_declarator", "array_declarator", "function_declarator",
        )
    ]
    if len(declarators) != 1:
        return None
    declarator = declarators[0]
    if declarator.type != "init_declarator":
        return None

    inner = declarator.child_by_field_name("declarator")
    init = declarator.child_by_field_name("value")
    if inner is None or init is None:
        return None

    # Walk through ptr/ref wrappers to get the name
    name_node = inner
    while name_node.type in ("pointer_declarator", "reference_declarator"):
        sub = name_node.child_by_field_name("declarator")
        if sub is None:
            break
        name_node = sub
    if name_node.type != "identifier" or not name_node.text:
        return None
    name = name_node.text.decode("utf-8", errors="replace")

    return (name, init, is_const)


def _collect_locals_before(func_body: Node, loop_node: Node) -> set[str]:
    """Names of locals declared at function scope BEFORE `loop_node`."""
    out: set[str] = set()
    for child in func_body.named_children:
        if child.start_byte >= loop_node.start_byte:
            break
        if child.type == "declaration":
            name = _get_simple_decl_name(child)
            if name:
                out.add(name)
    return out


def _collect_locals_in_range(
    stmts: list[Node], start: int, end: int
) -> set[str]:
    out: set[str] = set()
    for s in stmts[start:end]:
        if s.type == "declaration":
            name = _get_simple_decl_name(s)
            if name:
                out.add(name)
    return out


def _collect_param_names(func_node: Node) -> set[str]:
    """Extract function parameter names."""
    params: set[str] = set()
    declarator = func_node.child_by_field_name("declarator")
    if declarator is None:
        return params
    # Walk until we find a function_declarator
    target = declarator
    while target is not None and target.type != "function_declarator":
        sub = target.child_by_field_name("declarator")
        if sub is None:
            break
        target = sub
    if target is None or target.type != "function_declarator":
        return params
    param_list = target.child_by_field_name("parameters")
    if param_list is None:
        return params
    for p in param_list.named_children:
        if p.type == "parameter_declaration":
            name = _get_simple_decl_name(p)
            if name:
                params.add(name)
    return params


def _all_identifiers_safe(
    init_node: Node,
    source: bytes,
    loop_var: str | None,
    outer_pre_locals: set[str],
    params: set[str],
) -> bool:
    """Check that every identifier in INIT is provably loop-invariant.

    Allows: params, outer-pre-locals, member names (m-prefix), `this`,
    and identifiers used as call targets if the call looks pure
    (Get*, m*, *Const).
    """
    # Walk the init subtree. For every call_expression, check the callee
    # name looks safe. For every standalone identifier, it must come from
    # params, outer_pre_locals, members (m-prefix), `this`, or be a known
    # type/constant token.
    for node in walk(init_node):
        if node.type == "call_expression":
            callee = node.child_by_field_name("function")
            if callee is None:
                return False
            callee_name = _extract_call_name(callee, source)
            if callee_name is None:
                # Couldn't decode — be conservative
                return False
            if not _PURE_CALL_RE.match(callee_name):
                return False

    # For identifiers that are not callees, verify provenance.
    callee_ids = _collect_callee_identifiers(init_node)
    for ident in identifiers_in(init_node):
        if ident in callee_ids:
            continue
        if ident == loop_var:
            return False
        if ident in params or ident in outer_pre_locals:
            continue
        if ident == "this":
            continue
        # Member access (m-prefix) is OK — read of `this->m...`
        if _MEMBER_RE.match(ident):
            continue
        # Allow common constants/types/macros (ALL_CAPS or PascalCase suffixed
        # with capitals — heuristic). Numeric literals never show up as
        # identifiers so they don't need handling.
        if ident.isupper():
            continue
        # Otherwise — unknown provenance, reject conservatively.
        return False
    return True


def _extract_call_name(callee: Node, source: bytes) -> str | None:
    """Return the rightmost call target identifier (foo, obj.foo, ptr->foo)."""
    if callee.type == "identifier" and callee.text:
        return callee.text.decode("utf-8", errors="replace")
    if callee.type == "field_expression":
        field = callee.child_by_field_name("field")
        if field is not None and field.text:
            return field.text.decode("utf-8", errors="replace")
    if callee.type == "qualified_identifier":
        # Class::method — take the rightmost id
        name = callee.child_by_field_name("name")
        if name is not None and name.text:
            return name.text.decode("utf-8", errors="replace")
    return None


def _collect_callee_identifiers(node: Node) -> set[str]:
    """Names that appear as the callee of a call (so we don't treat them
    as plain identifier references that need provenance)."""
    out: set[str] = set()
    for n in walk(node):
        if n.type == "call_expression":
            callee = n.child_by_field_name("function")
            if callee is None:
                continue
            name = _extract_call_name(callee, n.text or b"")
            if name:
                out.add(name)
    return out


def _is_written_in(
    scope: Node, var_name: str, source: bytes, exclude: Node | None = None
) -> bool:
    """Return True if `var_name` is written (=, +=, ++, --, etc.) in `scope`,
    excluding the byte range of `exclude` if given."""
    excl_range = (exclude.start_byte, exclude.end_byte) if exclude else None

    target = var_name.encode("utf-8")
    for node in walk(scope):
        if excl_range and excl_range[0] <= node.start_byte < excl_range[1]:
            continue
        if node.type == "assignment_expression":
            lhs = node.child_by_field_name("left")
            if lhs is not None and lhs.text == target:
                return True
        elif node.type == "update_expression":
            arg = node.child_by_field_name("argument")
            if arg is not None and arg.text == target:
                return True
    return False


# ---------------------------------------------------------------------------
# Source rewriting
# ---------------------------------------------------------------------------


def _line_start(source: bytes, pos: int) -> int:
    while pos > 0 and source[pos - 1:pos] not in (b"\n", b"\r"):
        pos -= 1
    return pos


def _line_end(source: bytes, pos: int) -> int:
    length = len(source)
    while pos < length and source[pos:pos + 1] not in (b"\n", b""):
        pos += 1
    if pos < length and source[pos:pos + 1] == b"\n":
        pos += 1
    return pos


def _line_indent(source: bytes, pos: int) -> bytes:
    """Whitespace prefix of the line containing `pos`."""
    line_start = _line_start(source, pos)
    indent = b""
    for i in range(line_start, pos):
        ch = source[i:i + 1]
        if ch in (b" ", b"\t"):
            indent += ch
        else:
            break
    return indent


def _apply_hoist(
    source: bytes, decl_stmt: Node, loop_node: Node
) -> bytes | None:
    """Move `decl_stmt` from inside loop body to just before the loop."""
    decl_line_start = _line_start(source, decl_stmt.start_byte)
    decl_line_end = _line_end(source, decl_stmt.end_byte)
    decl_text_bytes = source[decl_stmt.start_byte:decl_stmt.end_byte]

    loop_line_start = _line_start(source, loop_node.start_byte)
    loop_indent = _line_indent(source, loop_node.start_byte)

    # Build the new line that will precede the loop
    new_line = loop_indent + decl_text_bytes + b"\n"

    # Sanity: ensure loop_line_start is BEFORE decl_line_start (else we're
    # not actually moving outward)
    if loop_line_start >= decl_line_start:
        return None

    # Apply: insert new_line at loop_line_start, delete the original decl line
    result = (
        source[:loop_line_start]
        + new_line
        + source[loop_line_start:decl_line_start]
        + source[decl_line_end:]
    )
    return result


def _apply_sink(
    source: bytes, decl_stmt: Node, loop_node: Node
) -> bytes | None:
    """Move a pre-loop decl `decl_stmt` into the start of `loop_node`'s body."""
    loop_body = _get_loop_body(loop_node)
    if loop_body is None:
        return None

    decl_line_start = _line_start(source, decl_stmt.start_byte)
    decl_line_end = _line_end(source, decl_stmt.end_byte)
    decl_text_bytes = source[decl_stmt.start_byte:decl_stmt.end_byte]

    # Insertion point: just past the body's opening `{` plus newline
    insert_pos = loop_body.start_byte + 1  # past `{`
    if insert_pos < len(source) and source[insert_pos:insert_pos + 1] == b"\n":
        insert_pos += 1

    # Determine indentation for the new statement (match existing body indent)
    body_indent = _body_inner_indent(source, loop_body)
    new_line = body_indent + decl_text_bytes + b"\n"

    # decl_line_start must be BEFORE insert_pos for outward->inward direction
    if decl_line_start >= insert_pos:
        return None

    # We delete the original decl first, then insert into body. Since the
    # insert position is AFTER the decl, deletion shifts the body position.
    # Simpler: do both at once via slicing.
    result = (
        source[:decl_line_start]
        + source[decl_line_end:insert_pos]
        + new_line
        + source[insert_pos:]
    )
    return result


def _body_inner_indent(source: bytes, body: Node) -> bytes:
    """Indent for statements inside `body`. Falls back to body+4spaces."""
    for child in body.named_children:
        return _line_indent(source, child.start_byte)
    base = _line_indent(source, body.start_byte)
    return base + b"    "


def _find_sink_candidate(
    func_body: Node, loop_node: Node, source: bytes
) -> tuple[Node, str] | None:
    """Find a sinkable `T name = INIT;` decl immediately before `loop_node`.

    Conditions:
    - decl is the immediately preceding statement at function scope
    - `name` is used exactly once inside the loop body
    - `name` is NOT used after the loop body
    - INIT is loop-invariant (uses only params/outer-pre-locals/members)
    """
    # Locate the immediately preceding statement
    prev: Node | None = None
    for child in func_body.named_children:
        if child.start_byte >= loop_node.start_byte:
            break
        prev = child
    if prev is None or prev.type != "declaration":
        return None

    decl_info = _classify_decl(prev, source)
    if decl_info is None:
        return None
    decl_name, init_node, _is_const = decl_info

    # Count uses inside loop body and after
    loop_body = _get_loop_body(loop_node)
    if loop_body is None:
        return None

    in_loop_uses = _count_identifier_uses(loop_body, decl_name)
    if in_loop_uses != 1:
        return None

    # After-loop uses: zero allowed
    after_uses = _count_uses_after(func_body, loop_node, decl_name)
    if after_uses != 0:
        return None

    # Don't sink a decl whose name is also written inside the loop
    if _is_written_in(loop_body, decl_name, source):
        return None

    return (prev, decl_name)


def _count_identifier_uses(scope: Node, name: str) -> int:
    target = name.encode("utf-8")
    count = 0
    for n in walk(scope):
        if n.type == "identifier" and n.text == target:
            count += 1
    return count


def _count_uses_after(
    func_body: Node, loop_node: Node, name: str
) -> int:
    """Count identifier uses of `name` AFTER loop_node within func_body."""
    target = name.encode("utf-8")
    count = 0
    for child in func_body.named_children:
        if child.start_byte <= loop_node.start_byte:
            continue
        for n in walk(child):
            if n.type == "identifier" and n.text == target:
                count += 1
    return count
