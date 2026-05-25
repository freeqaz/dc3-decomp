"""Value/address caching — swap between reference binding and value caching.

Targets callee-saved register swaps caused by the compiler's choice between
caching an ADDRESS in a GPR (via reference binding) versus caching a VALUE
in a GPR (via accessor call result).

Proven manual fix: UIListDir::Save — changing `auto& ref = member` to
`int val = member.Accessor()` shifted register allocation order, fixing
r29<->r30 swap (92.2->99.8%).

Transformations:
    1. ref-to-value:
       Type& ref = member;         ->  auto val = member;
       auto& ref = obj.mFoo;       ->  auto val = obj.mFoo;
       (only when ref is read-only: no writes through it, no address-of)

    2. value-to-ref:
       Type val = obj.Method();    ->  auto& ref = obj.Method();
       (conservative: only simple .Method() calls)

    3. inline-to-cached:
       obj.Method(); ... obj.Method(); ... obj.Method();
       ->  auto _cached = obj.Method(); _cached; ... _cached; ... _cached;
       (3+ occurrences of same call expression)

Detection signals:
    - Callee-saved GPR swaps (r13-r31)
    - Also fires as a fallback when no diagnosis is available
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent, get_line_start, walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Callee-saved GPR range
_CALLEE_SAVED_RE = re.compile(r"r(1[3-9]|2\d|3[01])")


class ValueAddressCachingPattern(Pattern):
    name = "value_address_caching"
    # Re-enabled (was opt_in due to 79/84 variants failing on mwcc).
    # ref-to-value and value-to-ref strategies now reuse the source's existing
    # `Type` instead of emitting `auto`. The inline-to-cached strategy still
    # requires `auto` so it's msvc-only.
    safety_tier = "normal"
    structural_domain = "register_allocation"
    follow_ups = ("declaration_reorder", "prologue_pressure")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Callee-saved GPR swaps — this pattern directly addresses register
        # allocation order changes from value vs address caching
        for r1, r2 in diagnosis.reg_swap_pairs:
            if _CALLEE_SAVED_RE.match(r1) or _CALLEE_SAVED_RE.match(r2):
                return True

        # Clusters can also indicate load/store reordering from caching changes
        if diagnosis.clusters:
            return True

        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0

        # Strategy 1: ref-to-value — remove reference qualifier
        for variant in _ref_to_value(ctx, counter):
            yield variant
            counter += 1
            if counter >= 8:
                return

        # Strategy 2: value-to-ref — add reference qualifier
        for variant in _value_to_ref(ctx, counter):
            yield variant
            counter += 1
            if counter >= 8:
                return

        # Strategy 3: inline-to-cached — cache repeated call expressions
        for variant in _inline_to_cached(ctx, counter):
            yield variant
            counter += 1
            if counter >= 8:
                return


# ---------------------------------------------------------------------------
# Strategy 1: ref-to-value
# ---------------------------------------------------------------------------


def _ref_to_value(ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Convert reference declarations to value declarations.

    Transforms `Type& ref = expr;` or `auto& ref = expr;` into
    `auto val = expr;` when the reference is only read, never written
    through, and never has its address taken.
    """
    source = ctx.file_source
    body = ctx.body_node

    for compound in _find_compound_statements(body):
        stmts = list(compound.named_children)
        for i, stmt in enumerate(stmts):
            if counter >= 8:
                return

            info = _extract_ref_decl(stmt, source)
            if info is None:
                continue

            var_name, init_expr, is_auto, type_text, decl_start, decl_end = info

            # Collect all uses of this variable in subsequent siblings
            uses = []
            for j in range(i + 1, len(stmts)):
                uses.extend(_find_identifier_uses(stmts[j], var_name))

            if not uses:
                continue

            # Safety: only transform read-only references
            if _has_write_through(uses, source) or _has_address_of(uses):
                continue

            # Build replacement: remove & from declaration
            # Find the reference_declarator in the declaration to replace
            ed = SourceEditor(source)

            # Replace the entire declaration statement
            # Original: "Type& ref = expr;" or "auto& ref = expr;"
            # New: "<type> val = expr;" — for mwcc we MUST use the concrete
            # type; for msvc `auto` is fine. If the original was `auto&` and
            # we're on mwcc, we can't fix it — skip.
            var_str = var_name.decode("utf-8", errors="replace")
            init_str = init_expr.decode("utf-8", errors="replace")
            new_name = f"_val{counter}"
            indent = get_indent(source, stmt)
            if ctx.compiler_dialect == "msvc":
                type_decl = "auto"
            else:
                if is_auto:
                    continue  # Original used `auto&` — no concrete type to reuse.
                type_decl = type_text.decode("utf-8", errors="replace").rstrip()
            new_decl = indent + f"{type_decl} {new_name} = {init_str};\n".encode("utf-8")

            # Delete old line and insert new
            del_start, del_end = _line_range(source, decl_start, decl_end)
            ed.replace_range(del_start, del_end, new_decl)

            # Replace all uses with new name
            new_name_bytes = new_name.encode("utf-8")
            for use_node in sorted(uses, key=lambda n: n.start_byte, reverse=True):
                ed.replace_node(use_node, new_name_bytes)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            yield Variant(
                name=f"ref2val_{counter}",
                pattern_name="value_address_caching",
                description=f"ref-to-value: '{var_str}' -> '{new_name}' (drop &)",
                source=new_source,
            )
            counter += 1


# ---------------------------------------------------------------------------
# Strategy 2: value-to-ref
# ---------------------------------------------------------------------------


def _value_to_ref(ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Convert value declarations initialized by accessor calls to references.

    Transforms `Type val = obj.Method();` into `auto& ref = obj.Method();`
    when the initializer is a simple `.Method()` call with no arguments.
    """
    source = ctx.file_source
    body = ctx.body_node

    for compound in _find_compound_statements(body):
        stmts = list(compound.named_children)
        for i, stmt in enumerate(stmts):
            if counter >= 8:
                return

            info = _extract_value_decl_with_call(stmt, source)
            if info is None:
                continue

            var_name, init_expr, type_text, decl_start, decl_end = info

            # Collect uses
            uses = []
            for j in range(i + 1, len(stmts)):
                uses.extend(_find_identifier_uses(stmts[j], var_name))

            if not uses:
                continue

            # Build replacement: add & to declaration. For mwcc, reuse the
            # original `Type` (must be a real type, not `auto`).
            var_str = var_name.decode("utf-8", errors="replace")
            init_str = init_expr.decode("utf-8", errors="replace")
            new_name = f"_ref{counter}"
            indent = get_indent(source, stmt)
            if ctx.compiler_dialect == "msvc":
                ref_decl = "auto&"
            else:
                type_str = type_text.decode("utf-8", errors="replace").rstrip()
                if type_str == "auto":
                    continue
                ref_decl = f"{type_str} &"
            new_decl = indent + f"{ref_decl} {new_name} = {init_str};\n".encode("utf-8")

            ed = SourceEditor(source)

            del_start, del_end = _line_range(source, decl_start, decl_end)
            ed.replace_range(del_start, del_end, new_decl)

            # Replace all uses
            new_name_bytes = new_name.encode("utf-8")
            for use_node in sorted(uses, key=lambda n: n.start_byte, reverse=True):
                ed.replace_node(use_node, new_name_bytes)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            yield Variant(
                name=f"val2ref_{counter}",
                pattern_name="value_address_caching",
                description=f"value-to-ref: '{var_str}' -> 'auto& {new_name}'",
                source=new_source,
            )
            counter += 1


# ---------------------------------------------------------------------------
# Strategy 3: inline-to-cached
# ---------------------------------------------------------------------------


def _inline_to_cached(ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Cache repeated call expressions into a local variable.

    When the same `obj.Method()` call (no arguments) appears 3+ times
    in the function body, introduce `auto _cached = obj.Method();`
    before the first use and replace all occurrences.
    """
    source = ctx.file_source
    body = ctx.body_node

    # Find all call expressions that are simple no-arg method calls
    call_groups: dict[str, list[Node]] = {}
    for node in walk(body):
        if node.type != "call_expression":
            continue
        call_text = source[node.start_byte:node.end_byte]
        # Only match simple no-arg calls: expr.Method() or expr->Method()
        func_node = node.child_by_field_name("function")
        args_node = node.child_by_field_name("arguments")
        if func_node is None or args_node is None:
            continue
        if func_node.type != "field_expression":
            continue
        # Check no arguments (just parentheses)
        arg_children = [c for c in args_node.named_children]
        if arg_children:
            continue
        key = call_text.decode("utf-8", errors="replace")
        call_groups.setdefault(key, []).append(node)

    for call_text, nodes in call_groups.items():
        if counter >= 8:
            return
        if len(nodes) < 3:
            continue

        # Find the first use and its containing top-level statement
        first_node = nodes[0]
        containing_stmt = _get_containing_stmt(first_node, body)
        if containing_stmt is None:
            continue

        var_name = f"_cached{counter}"
        var_bytes = var_name.encode("utf-8")
        call_bytes = call_text.encode("utf-8")
        indent = get_indent(source, containing_stmt)
        line_start = get_line_start(source, containing_stmt)

        # mwcc can't deduce the return type via `auto` — skip this strategy
        # entirely. Caching call results without knowing the return type is
        # too risky (we'd need libclang or a parsed header to find it).
        if ctx.compiler_dialect != "msvc":
            continue
        decl_line = indent + b"auto " + var_bytes + b" = " + call_bytes + b";\n"

        ed = SourceEditor(source)
        ed.insert_at(line_start, decl_line)

        # Replace all occurrences (reverse order for stable offsets)
        for node in sorted(nodes, key=lambda n: n.start_byte, reverse=True):
            ed.replace_node(node, var_bytes)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        short = call_text if len(call_text) <= 40 else call_text[:37] + "..."
        yield Variant(
            name=f"cache_{counter}",
            pattern_name="value_address_caching",
            description=f"inline-to-cached: {short} ({len(nodes)} uses) -> {var_name}",
            source=new_source,
        )
        counter += 1


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _find_compound_statements(body: Node) -> list[Node]:
    """Find all compound_statement nodes (including the body itself)."""
    results = []
    for n in walk(body):
        if n.type == "compound_statement":
            results.append(n)
    return results


def _extract_ref_decl(
    stmt: Node, source: bytes
) -> tuple[bytes, bytes, bool, bytes, int, int] | None:
    """Extract info from a reference declaration.

    Returns (var_name, init_expr, is_auto, type_text, stmt_start, stmt_end)
    or None if the statement is not a matching reference declaration.

    Matches:
        auto& ref = expr;
        Type& ref = expr;
        const auto& ref = expr;
        const Type& ref = expr;
    """
    if stmt.type != "declaration":
        return None

    init_decls = [c for c in stmt.named_children if c.type == "init_declarator"]
    if len(init_decls) != 1:
        return None

    init_decl = init_decls[0]
    declarator = init_decl.child_by_field_name("declarator")
    value = init_decl.child_by_field_name("value")

    if declarator is None or value is None:
        return None

    # Must be a reference declarator
    if declarator.type != "reference_declarator":
        return None

    # Get the identifier name
    name_node = declarator
    while name_node.type == "reference_declarator":
        inner = name_node.child_by_field_name("declarator")
        if inner is None:
            inner = name_node.named_children[-1] if name_node.named_children else None
        if inner is None:
            break
        name_node = inner

    if name_node.type != "identifier" or name_node.text is None:
        return None

    var_name = name_node.text

    # Don't transform if initializer has side-effect calls (other than simple accessors)
    # We allow calls here since the value is captured once — the key transformation
    # is just removing the & qualifier
    init_expr = source[value.start_byte:value.end_byte]

    # Get the type specifier text
    type_node = stmt.child_by_field_name("type")
    type_text = source[type_node.start_byte:type_node.end_byte] if type_node else b""
    is_auto = type_text.rstrip() in (b"auto", b"const auto")

    return var_name, init_expr, is_auto, type_text, stmt.start_byte, stmt.end_byte


def _extract_value_decl_with_call(
    stmt: Node, source: bytes
) -> tuple[bytes, bytes, bytes, int, int] | None:
    """Extract info from a value declaration initialized by a method call.

    Returns (var_name, init_expr, type_text, stmt_start, stmt_end) or None.

    Matches:
        Type val = obj.Method();
        int val = obj.GetValue();
        auto val = obj.Method();
    But NOT:
        Type& ref = ...;  (already a reference)
        Type* ptr = ...;  (pointer)
    """
    if stmt.type != "declaration":
        return None

    init_decls = [c for c in stmt.named_children if c.type == "init_declarator"]
    if len(init_decls) != 1:
        return None

    init_decl = init_decls[0]
    declarator = init_decl.child_by_field_name("declarator")
    value = init_decl.child_by_field_name("value")

    if declarator is None or value is None:
        return None

    # Must NOT be a reference or pointer declarator
    if declarator.type in ("reference_declarator", "pointer_declarator"):
        return None

    # Declarator must be a plain identifier
    if declarator.type != "identifier" or declarator.text is None:
        return None

    # Initializer must be a call expression (accessor-like)
    if value.type != "call_expression":
        return None

    # The call must be a simple method call: obj.Method() or obj->Method()
    func_node = value.child_by_field_name("function")
    args_node = value.child_by_field_name("arguments")
    if func_node is None or args_node is None:
        return None

    if func_node.type != "field_expression":
        return None

    # No arguments
    if [c for c in args_node.named_children]:
        return None

    var_name = declarator.text
    init_expr = source[value.start_byte:value.end_byte]

    type_node = stmt.child_by_field_name("type")
    type_text = source[type_node.start_byte:type_node.end_byte] if type_node else b""

    return var_name, init_expr, type_text, stmt.start_byte, stmt.end_byte


def _find_identifier_uses(node: Node, name: bytes) -> list[Node]:
    """Find all uses of an identifier in a subtree (excluding declarations)."""
    results = []
    for n in walk(node):
        if n.type == "identifier" and n.text == name:
            parent = n.parent
            if parent is not None and parent.type == "init_declarator":
                decl = parent.child_by_field_name("declarator")
                if decl is not None:
                    # Walk through reference_declarator to find the actual id
                    check = decl
                    while check.type in ("reference_declarator", "pointer_declarator"):
                        inner = check.child_by_field_name("declarator")
                        if inner is None:
                            inner = check.named_children[-1] if check.named_children else None
                        if inner is None:
                            break
                        check = inner
                    if check.id == n.id:
                        continue  # Declaration site, not a use
            results.append(n)
    return results


def _has_write_through(uses: list[Node], source: bytes) -> bool:
    """Check if any use of the reference is written through.

    Detects:
    - Assignment: `ref = ...;` (ref on LHS of assignment)
    - Compound assignment: `ref += ...;`, `ref *= ...;`, etc.
    - Increment/decrement: `ref++`, `--ref`
    """
    for use_node in uses:
        parent = use_node.parent
        if parent is None:
            continue

        # Assignment expression: check if use is on the left side
        if parent.type == "assignment_expression":
            left = parent.child_by_field_name("left")
            if left is not None and left.id == use_node.id:
                return True

        # Compound assignment (+=, -=, etc.)
        if parent.type in ("augmented_assignment_expression",):
            left = parent.child_by_field_name("left")
            if left is not None and left.id == use_node.id:
                return True

        # Increment/decrement
        if parent.type in ("update_expression",):
            return True

    return False


def _has_address_of(uses: list[Node]) -> bool:
    """Check if any use has its address taken (&ref)."""
    for use_node in uses:
        parent = use_node.parent
        if parent is not None and parent.type == "pointer_expression":
            # Check for & operator
            op = parent.child_by_field_name("operator")
            if op is not None and op.text == b"&":
                return True
    return False


def _get_containing_stmt(node: Node, body: Node) -> Node | None:
    """Walk up from node to find the direct child statement of body."""
    current = node
    while current is not None:
        if current.parent is not None and current.parent.id == body.id:
            return current
        current = current.parent
    return None


def _line_range(source: bytes, start: int, end: int) -> tuple[int, int]:
    """Expand a byte range to include leading whitespace and trailing newline."""
    del_start = start
    while del_start > 0 and source[del_start - 1:del_start] in (b" ", b"\t"):
        del_start -= 1
    del_end = end
    while del_end < len(source) and source[del_end:del_end + 1] in (b"\n", b"\r"):
        del_end += 1
    return del_start, del_end
