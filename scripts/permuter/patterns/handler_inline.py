"""Handler body inlining — convert named/temporary Message vars and inline wrappers.

Win rate: proven on HamNavProvider::Handle (98.4->100%) and Automator::Handle
(98.8->100%).

When a HANDLE_ACTION handler macro calls a wrapper function but the target
binary inlines the wrapper body directly at the call site, replacing the
wrapper call with the wrapper's body fixes the mismatch.  Also applies to
Message temporary construction -- using ``Message(_msg)`` as a temporary vs
``Message msg(_msg)`` as a named variable changes destructor timing (end of
expression vs end of block).

Transformations:
    named-to-temporary:
        Message msg(sym);
        HandleMessage(msg);
        ->
        HandleMessage(Message(sym));

    temporary-to-named:
        HandleMessage(Message(sym));
        ->
        Message msg(sym);
        HandleMessage(msg);

    call-to-body:
        HANDLE_ACTION(append_nav_item, AddNavItem())
        ->
        HANDLE_ACTION(append_nav_item, mNavItems.push_back(NavItem()))

    handle-message-macro:
        OnMsg(SomeMsg) { HandleMessage(msg.Data()->Sym(1)); }
        -> HANDLE_MESSAGE(SomeMsg)

Detection signals:
    - Function name contains "Handle" or "handler"
    - Source contains HANDLE_ACTION / HANDLE / _HANDLE_CHECKED macros
    - Diagnosis shows frame size differences (clusters)
    - Diagnosis shows prologue mismatch
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, get_line_start, find_by_type
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Macro names that wrap handler dispatch
_HANDLER_MACRO_RE = re.compile(
    rb"\b(HANDLE_ACTION|HANDLE|_HANDLE_CHECKED|HANDLE_EXPR|HANDLE_ACTION_CHECKED)\b"
)

# Message constructor pattern: ``Message varname(args)``
_MESSAGE_DECL_RE = re.compile(
    rb"Message\s+(\w+)\s*\("
)

# Detect ``OnMsg(Type)`` pattern
_ON_MSG_RE = re.compile(
    rb"\bOnMsg\s*\(\s*(\w+)\s*\)"
)


class HandlerInlinePattern(Pattern):
    name = "handler_inline"
    safety_tier = "normal"
    structural_domain = "handler"
    follow_ups = ("temp_elimination", "declaration_reorder")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Clusters suggest instruction reordering from inlining differences
        if diagnosis.clusters:
            return True
        # Frame size / prologue mismatch from inlined code
        if diagnosis.has_prologue_mismatch:
            return True
        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        stmts = ctx.statements
        counter = 0

        # Check source-level signals: function name or macro presence
        func_text = source[ctx.func_node.start_byte:ctx.func_node.end_byte]
        has_handler_macros = bool(_HANDLER_MACRO_RE.search(func_text))
        func_name = _get_func_name(ctx.func_node, source)
        is_handler_func = func_name is not None and (
            "Handle" in func_name or "handler" in func_name.lower()
        )

        # Strategy 1: Named Message variable -> temporary
        for v in _named_to_temporary(ctx, source, stmts, counter):
            yield v
            counter += 1
            if counter >= 8:
                return

        # Strategy 2: Temporary Message() -> named variable
        for v in _temporary_to_named(ctx, source, stmts, counter):
            yield v
            counter += 1
            if counter >= 8:
                return

        # Strategy 3: Inline simple wrapper calls in HANDLE_ACTION macros
        if has_handler_macros or is_handler_func:
            for v in _call_to_body(ctx, source, body, counter):
                yield v
                counter += 1
                if counter >= 12:
                    return

        # Strategy 4: OnMsg handler -> HANDLE_MESSAGE macro
        if is_handler_func:
            for v in _handle_message_macro(ctx, source, body, counter):
                yield v
                counter += 1
                if counter >= 12:
                    return


def _get_func_name(func_node: Node, source: bytes) -> str | None:
    """Extract the function name from a function_definition node."""
    declarator = func_node.child_by_field_name("declarator")
    if declarator is None:
        return None
    # Walk into nested declarators to find the identifier
    current = declarator
    while current is not None:
        if current.type == "identifier":
            return source[current.start_byte:current.end_byte].decode("utf-8", errors="replace")
        if current.type == "qualified_identifier":
            # e.g. Class::Handle -- return the last name
            name = current.child_by_field_name("name")
            if name is not None:
                return source[name.start_byte:name.end_byte].decode("utf-8", errors="replace")
        inner = current.child_by_field_name("declarator")
        if inner is None:
            # Try named children
            for child in current.named_children:
                if child.type in ("identifier", "qualified_identifier", "function_declarator"):
                    current = child
                    break
            else:
                break
        else:
            current = inner
    return None


def _find_message_declarations(
    stmts: list[Node], source: bytes
) -> list[tuple[int, bytes, bytes, Node]]:
    """Find ``Message varname(args);`` declarations.

    Returns list of (stmt_index, var_name, ctor_args_text, stmt_node).
    """
    results = []
    for i, stmt in enumerate(stmts):
        if stmt.type != "declaration":
            continue
        stmt_text = source[stmt.start_byte:stmt.end_byte]
        m = _MESSAGE_DECL_RE.search(stmt_text)
        if m is None:
            continue

        var_name = m.group(1)

        # Extract the constructor arguments -- find the parenthesized part
        # The text looks like: "Message varname(arg1, arg2)"
        # Find the opening paren after the var name
        paren_start = stmt_text.find(b"(", m.end() - 1)
        if paren_start < 0:
            continue
        # Find matching close paren
        depth = 0
        paren_end = -1
        for j in range(paren_start, len(stmt_text)):
            if stmt_text[j:j + 1] == b"(":
                depth += 1
            elif stmt_text[j:j + 1] == b")":
                depth -= 1
                if depth == 0:
                    paren_end = j
                    break
        if paren_end < 0:
            continue

        args_text = stmt_text[paren_start + 1:paren_end]
        results.append((i, var_name, args_text, stmt))
    return results


def _find_identifier_uses(node: Node, name: bytes) -> list[Node]:
    """Find all uses of an identifier in a subtree (excluding declarations)."""
    results: list[Node] = []
    for n in walk(node):
        if n.type == "identifier" and n.text == name:
            parent = n.parent
            if parent is not None and parent.type == "init_declarator":
                decl = parent.child_by_field_name("declarator")
                if decl is not None and decl.id == n.id:
                    continue
            results.append(n)
    return results


def _named_to_temporary(
    ctx: FunctionContext, source: bytes, stmts: list[Node], counter: int
) -> Iterator[Variant]:
    """Convert ``Message msg(args); Use(msg);`` to ``Use(Message(args));``."""
    if counter >= 8:
        return

    decls = _find_message_declarations(stmts, source)
    for stmt_idx, var_name, args_text, decl_stmt in decls:
        if counter >= 8:
            break

        # Find all uses of var_name in subsequent statements
        uses: list[Node] = []
        for j in range(stmt_idx + 1, len(stmts)):
            uses.extend(_find_identifier_uses(stmts[j], var_name))

        # Only handle single-use case for safety
        if len(uses) != 1:
            continue

        use_node = uses[0]
        temporary_expr = b"Message(" + args_text + b")"

        # Build edit: delete declaration, replace use with temporary
        ed = SourceEditor(source)

        # Delete the declaration line (including whitespace and newline)
        del_start = decl_stmt.start_byte
        del_end = decl_stmt.end_byte
        while del_end < len(source) and source[del_end:del_end + 1] in (b"\n", b"\r"):
            del_end += 1
        while del_start > 0 and source[del_start - 1:del_start] in (b" ", b"\t"):
            del_start -= 1

        ed.delete_range(del_start, del_end)
        ed.replace_node(use_node, temporary_expr)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        var_str = var_name.decode("utf-8", errors="replace")
        yield Variant(
            name=f"named_to_temp_{counter}",
            pattern_name="handler_inline",
            description=f"Convert named Message '{var_str}' to temporary (destructor at end-of-expression)",
            source=new_source,
        )
        counter += 1


def _find_temporary_message_calls(
    stmts: list[Node], source: bytes
) -> list[tuple[int, Node, bytes, Node]]:
    """Find ``Use(Message(args))`` patterns in statements.

    Returns list of (stmt_index, call_expr_node, args_text, message_arg_node).
    The message_arg_node is the ``Message(args)`` call_expression within the outer call.
    """
    results = []
    for i, stmt in enumerate(stmts):
        for call_node in find_by_type(stmt, "call_expression"):
            # Check if any argument is a Message() constructor call
            args_node = call_node.child_by_field_name("arguments")
            if args_node is None:
                continue
            for arg in args_node.named_children:
                if arg.type != "call_expression":
                    continue
                func = arg.child_by_field_name("function")
                if func is None:
                    continue
                func_text = source[func.start_byte:func.end_byte]
                if func_text == b"Message":
                    # Extract the Message constructor args
                    inner_args = arg.child_by_field_name("arguments")
                    if inner_args is None:
                        continue
                    # Get text between parens
                    inner_args_text = source[inner_args.start_byte + 1:inner_args.end_byte - 1]
                    results.append((i, call_node, inner_args_text, arg))
    return results


def _temporary_to_named(
    ctx: FunctionContext, source: bytes, stmts: list[Node], counter: int
) -> Iterator[Variant]:
    """Convert ``Use(Message(args))`` to ``Message msg(args); Use(msg);``."""
    if counter >= 8:
        return

    temporaries = _find_temporary_message_calls(stmts, source)
    for stmt_idx, outer_call, args_text, msg_arg_node in temporaries:
        if counter >= 8:
            break

        stmt = stmts[stmt_idx]
        var_name = b"msg"

        # Check that 'msg' is not already in use
        existing_uses = _find_identifier_uses(ctx.body_node, var_name)
        if existing_uses:
            var_name = b"_msg_tmp"

        indent = get_indent(source, stmt)
        line_start = get_line_start(source, stmt)

        # Insert declaration before the statement
        decl_line = indent + b"Message " + var_name + b"(" + args_text + b");\n"

        ed = SourceEditor(source)
        ed.insert_at(line_start, decl_line)
        # Replace the Message(args) temporary with the variable name
        ed.replace_node(msg_arg_node, var_name)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        yield Variant(
            name=f"temp_to_named_{counter}",
            pattern_name="handler_inline",
            description=f"Convert temporary Message() to named variable (destructor at end-of-block)",
            source=new_source,
        )
        counter += 1


# Common wrapper -> body expansions for HANDLE_ACTION macros
# Maps wrapper call patterns to likely body expansions
_WRAPPER_HEURISTICS: list[tuple[re.Pattern, list[bytes]]] = [
    # push_back wrappers: AddFoo() -> mFoos.push_back(Foo())
    (re.compile(rb"Add(\w+)\(\)"), [
        b"m{0}s.push_back({0}())",
    ]),
    # Clear wrappers: ClearFoo() -> mFoos.clear()
    (re.compile(rb"Clear(\w+)s?\(\)"), [
        b"m{0}s.clear()",
        b"m{0}.clear()",
    ]),
    # Reset wrappers: ResetFoo() -> mFoo.Reset() or mFoo = Foo()
    (re.compile(rb"Reset(\w+)\(\)"), [
        b"m{0}.Reset()",
        b"m{0} = {0}()",
    ]),
]


def _call_to_body(
    ctx: FunctionContext, source: bytes, body: Node, counter: int
) -> Iterator[Variant]:
    """Replace wrapper function calls in HANDLE_ACTION with inlined bodies."""
    if counter >= 12:
        return

    body_text = source[body.start_byte:body.end_byte]

    # Find HANDLE_ACTION(name, expr) macro invocations
    # Look for patterns like HANDLE_ACTION(sym_name, CallExpr())
    for m in re.finditer(
        rb"(HANDLE_ACTION|HANDLE_ACTION_CHECKED)\s*\(\s*(\w+)\s*,\s*",
        body_text,
    ):
        if counter >= 12:
            break

        macro_start = body.start_byte + m.start()
        after_comma = body.start_byte + m.end()

        # Find the closing paren of the macro, accounting for nested parens
        depth = 1  # We're inside the outer (
        pos = after_comma
        while pos < body.end_byte and depth > 0:
            ch = source[pos:pos + 1]
            if ch == b"(":
                depth += 1
            elif ch == b")":
                depth -= 1
            pos += 1

        if depth != 0:
            continue

        # The expression between the comma and the closing paren
        expr_text = source[after_comma:pos - 1].strip()

        # Check if expr_text is a simple function call: FuncName() or FuncName(args)
        call_match = re.match(rb"(\w+)\s*\(([^)]*)\)", expr_text)
        if call_match is None:
            continue

        func_name = call_match.group(1)

        # Try heuristic expansions
        for pattern, templates in _WRAPPER_HEURISTICS:
            pm = pattern.match(expr_text)
            if pm is None:
                continue

            captured = pm.group(1)
            for template in templates:
                replacement = template.replace(b"{0}", captured)

                ed = SourceEditor(source)
                ed.replace_range(after_comma, pos - 1, b" " + replacement)

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                func_str = func_name.decode("utf-8", errors="replace")
                repl_str = replacement.decode("utf-8", errors="replace")
                yield Variant(
                    name=f"call_to_body_{counter}",
                    pattern_name="handler_inline",
                    description=f"Inline wrapper {func_str}() -> {repl_str}",
                    source=new_source,
                )
                counter += 1
                if counter >= 12:
                    return


def _handle_message_macro(
    ctx: FunctionContext, source: bytes, body: Node, counter: int
) -> Iterator[Variant]:
    """Detect OnMsg handler patterns and suggest HANDLE_MESSAGE macro usage.

    Looks for patterns like:
        static Symbol _msg = "sym";
        DataNode _ret = OnMsg(MsgType);
        if (_ret.Type() != kDataUnhandled) return _ret;
    and replaces with:
        HANDLE_MESSAGE(MsgType)
    """
    if counter >= 12:
        return

    body_text = source[body.start_byte:body.end_byte]

    # Find OnMsg(Type) patterns
    for m in _ON_MSG_RE.finditer(body_text):
        if counter >= 12:
            break

        msg_type = m.group(1)

        # Find the containing statement -- search for the full block
        # Look for the pattern: { ... OnMsg(Type) ... kDataUnhandled ... }
        # This is complex to do with regex; just suggest the macro replacement
        # for the OnMsg call itself
        on_msg_start = body.start_byte + m.start()
        on_msg_end = body.start_byte + m.end()

        # Find the statement containing this OnMsg
        containing_stmt = None
        for stmt in ctx.statements:
            if stmt.start_byte <= on_msg_start and stmt.end_byte >= on_msg_end:
                containing_stmt = stmt
                break

        if containing_stmt is None:
            continue

        # Check if the full block has a HandleMessage call pattern
        # Look for HandleMessage in surrounding context
        context_start = max(body.start_byte, on_msg_start - 200)
        context_end = min(body.end_byte, on_msg_end + 200)
        context = source[context_start:context_end]

        if b"HandleMessage" in context or b"kDataUnhandled" in context:
            # This looks like a manual OnMsg handler that could use HANDLE_MESSAGE
            indent = get_indent(source, containing_stmt)
            macro_line = indent + b"HANDLE_MESSAGE(" + msg_type + b")\n"

            # Find the full block to replace (from OnMsg to the kDataUnhandled check)
            # For safety, just add the macro as an alternative before the block
            ed = SourceEditor(source)
            line_start = get_line_start(source, containing_stmt)
            ed.insert_at(line_start, macro_line)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            type_str = msg_type.decode("utf-8", errors="replace")
            yield Variant(
                name=f"handle_msg_{counter}",
                pattern_name="handler_inline",
                description=f"Add HANDLE_MESSAGE({type_str}) macro for OnMsg handler",
                source=new_source,
            )
            counter += 1
