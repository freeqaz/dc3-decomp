"""Accessor outline — detect inlined accessors and generate noinline wrappers.

When the target binary calls an accessor via `bl` but our compiler inlines it
(direct member load), the codegen diverges. The real fix is moving the accessor
body from the header to the .cpp file, which the permuter can't do automatically.

This pattern detects likely inlined accessor patterns and generates variants that
either:
1. Wrap the accessor call in a __declspec(noinline) local forwarding function
2. Use volatile function pointer indirection to prevent inlining

Context: Proven on UIListSlot::Draw (96.6->100%) by moving DisabledAlphaScale()
and ParentList() from UIListWidget.h to UIListWidget.cpp.

Detection signals:
    - Replace clusters where target has bl (function call) that base lacks
    - Prologue mismatches (inlined accessor changes register pressure)
    - Clusters (inlined code vs call instruction)
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, find_calls
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Member access patterns: obj->member or obj.member (simple accessor loads)
_ACCESSOR_RE = re.compile(
    rb"(\w+)\s*->\s*(m[A-Z]\w*)"  # ptr->mMember
    rb"|"
    rb"(\w+)\s*\.\s*(m[A-Z]\w*)"  # obj.mMember
)

# Callee-saved register range
_CALLEE_SAVED_RE = re.compile(r"r(1[3-9]|2\d|3[01])")

# Max variants to generate
_MAX_VARIANTS = 5


class AccessorOutlinePattern(Pattern):
    name = "accessor_outline"
    # opt_in: 71/71 variants failed compile historically (100% fail rate, 0 wins).
    # Generated wrappers conflict with existing inline accessors. Needs rework.
    opt_in = True
    safety_tier = "experimental"
    structural_domain = "cross_unit"
    follow_ups = ("declaration_reorder", "value_address_caching")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Replace clusters where target has bl (function call) pattern
        for d in diagnosis.diff_ops:
            if d.target_opcode == "bl" and d.base_opcode != "bl":
                return True

        # Clusters suggest structural differences (inlined vs outlined)
        if diagnosis.clusters:
            return True

        # Prologue mismatch — inlined code changes callee-saved pressure
        if diagnosis.has_prologue_mismatch:
            return True

        # Real structural replaces
        if diagnosis.replace_real > 0:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        score = 0.4
        # Strong signal: target has bl that base doesn't (accessor outlined in target)
        for d in diagnosis.diff_ops:
            if d.target_opcode == "bl" and d.base_opcode != "bl":
                score = 0.9
                break
        # Prologue mismatch boosts
        if diagnosis.has_prologue_mismatch:
            score = min(1.0, score + 0.2)
        return score

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        # Strategy 1: Find method calls on objects that look like simple
        # accessors (no arguments) and wrap them with __declspec(noinline)
        # forwarding functions
        for variant in _noinline_wrapper_variants(ctx, counter):
            yield variant
            counter += 1
            if counter >= _MAX_VARIANTS:
                return

        # Strategy 2: Find direct member access expressions (obj->mMember)
        # that appear where an accessor call might be expected, and wrap
        # them in a volatile function pointer call to force outline
        for variant in _volatile_indirection_variants(ctx, counter):
            yield variant
            counter += 1
            if counter >= _MAX_VARIANTS:
                return


def _find_no_arg_method_calls(body: Node, source: bytes) -> list[tuple[str, str, Node]]:
    """Find method calls with no arguments: obj.Method() or obj->Method().

    Returns list of (receiver_text, method_name, call_node) tuples.
    Only includes calls that look like simple accessor invocations.
    """
    results: list[tuple[str, str, Node]] = []

    for call_node in find_calls(body):
        func = call_node.child_by_field_name("function")
        args = call_node.child_by_field_name("arguments")
        if func is None or args is None:
            continue

        # Must be a field_expression (obj.Method or obj->Method)
        if func.type != "field_expression":
            continue

        # Check no arguments (only parens, no named children)
        arg_children = [c for c in args.named_children if c.type != "comment"]
        if arg_children:
            continue

        receiver = func.child_by_field_name("argument")
        method = func.child_by_field_name("field")
        if receiver is None or method is None:
            continue

        receiver_text = source[receiver.start_byte:receiver.end_byte].decode(
            "utf-8", errors="replace"
        )
        method_name = source[method.start_byte:method.end_byte].decode(
            "utf-8", errors="replace"
        )

        # Skip if method name starts with lowercase common non-accessor prefixes
        # (e.g., begin, end, size, empty — these are STL, not Hmx accessors)
        if method_name in ("begin", "end", "size", "empty", "data", "clear"):
            continue

        results.append((receiver_text, method_name, call_node))

    return results


def _find_repeated_member_accesses(body: Node, source: bytes) -> dict[str, list[Node]]:
    """Find direct member access expressions (obj->mMember) used 2+ times.

    These are candidates for what might be accessor calls in the target —
    the compiler inlined the accessor, producing a direct member load instead
    of a function call.

    Returns dict mapping "receiver->mMember" -> list of field_expression nodes.
    """
    accesses: dict[str, list[Node]] = {}

    for node in walk(body):
        if node.type != "field_expression":
            continue

        receiver = node.child_by_field_name("argument")
        field = node.child_by_field_name("field")
        if receiver is None or field is None:
            continue

        field_name = source[field.start_byte:field.end_byte].decode(
            "utf-8", errors="replace"
        )
        # Only match Hmx member naming: mFoo
        if not re.match(r"^m[A-Z]", field_name):
            continue

        # Check it's a pointer dereference (->), not a call result
        # The operator should be -> between receiver and field
        op_text = source[receiver.end_byte:field.start_byte]
        if b"->" not in op_text and b"." not in op_text:
            continue

        # Skip if the parent is already a call expression (method call, not member access)
        parent = node.parent
        if parent is not None and parent.type == "call_expression":
            parent_func = parent.child_by_field_name("function")
            if parent_func is not None and parent_func.id == node.id:
                continue

        key = source[node.start_byte:node.end_byte].decode("utf-8", errors="replace")
        accesses.setdefault(key, []).append(node)

    return {k: v for k, v in accesses.items() if len(v) >= 2}


def _get_containing_stmt(node: Node, body: Node) -> Node | None:
    """Walk up from node to find the direct child statement of body."""
    current = node
    while current is not None:
        if current.parent is not None and current.parent.id == body.id:
            return current
        current = current.parent
    return None


def _noinline_wrapper_variants(
    ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate noinline wrapper variants for no-argument method calls.

    For each accessor-like call (obj.Method() with no args), generates a
    local __declspec(noinline) forwarding function before the caller, then
    replaces the call site to use the wrapper.

    This forces the compiler to emit `bl` to the wrapper instead of inlining
    the accessor body.
    """
    source = ctx.file_source
    body = ctx.body_node
    func_node = ctx.func_node

    calls = _find_no_arg_method_calls(body, source)
    if not calls:
        return

    # Group by method name to avoid duplicate wrappers
    seen_methods: set[str] = set()

    for receiver_text, method_name, call_node in calls:
        if counter >= _MAX_VARIANTS:
            return
        if method_name in seen_methods:
            continue
        seen_methods.add(method_name)

        # Build a __declspec(noinline) wrapper function.
        # We use a template to handle unknown return/receiver types.
        # The wrapper has the form:
        #   template <class T>
        #   __declspec(noinline) auto _outline_MethodName(T* obj) -> decltype(obj->MethodName()) {
        #       return obj->MethodName();
        #   }
        wrapper_name = f"_outline_{method_name}"
        wrapper = (
            f"template <class _T>\n"
            f"__declspec(noinline) auto {wrapper_name}(_T* _obj)"
            f" -> decltype(_obj->{method_name}()) {{\n"
            f"    return _obj->{method_name}();\n"
            f"}}\n\n"
        ).encode("utf-8")

        # Insert wrapper before the function definition
        ed = SourceEditor(source)
        ed.insert_at(func_node.start_byte, wrapper)

        # Replace call sites: obj.Method() -> _outline_MethodName(&obj)
        # or obj->Method() -> _outline_MethodName(obj)
        matching_calls = [
            (rt, mn, cn) for rt, mn, cn in calls if mn == method_name
        ]

        for recv, _, cn in sorted(
            matching_calls, key=lambda x: x[2].start_byte, reverse=True
        ):
            # Determine if receiver is pointer (->)  or value (.)
            func_expr = cn.child_by_field_name("function")
            if func_expr is None:
                continue
            func_text = source[func_expr.start_byte:func_expr.end_byte]
            if b"->" in func_text:
                # ptr->Method() -> _outline_MethodName(ptr)
                replacement = f"{wrapper_name}({recv})".encode("utf-8")
            else:
                # obj.Method() -> _outline_MethodName(&obj)
                replacement = f"{wrapper_name}(&{recv})".encode("utf-8")
            ed.replace_node(cn, replacement)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        yield Variant(
            name=f"accessor_outline_{counter}",
            pattern_name="accessor_outline",
            description=(
                f"Outline accessor {method_name}() via noinline wrapper "
                f"({len(matching_calls)} call sites)"
            ),
            source=new_source,
            tags=frozenset({"outlined_accessor"}),
        )
        counter += 1


def _volatile_indirection_variants(
    ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate volatile indirection variants for repeated member accesses.

    When obj->mMember appears multiple times, it may be an inlined accessor.
    This generates a variant that wraps the access in a volatile-qualified
    function pointer call, preventing the compiler from inlining.

    This is a more aggressive strategy for when the noinline wrapper approach
    doesn't cover the case (e.g., the access is a direct member load, not
    a method call).
    """
    source = ctx.file_source
    body = ctx.body_node
    func_node = ctx.func_node

    repeated = _find_repeated_member_accesses(body, source)
    if not repeated:
        return

    for access_key, nodes in repeated.items():
        if counter >= _MAX_VARIANTS:
            return

        # Extract receiver and field from the first node
        first = nodes[0]
        receiver = first.child_by_field_name("argument")
        field = first.child_by_field_name("field")
        if receiver is None or field is None:
            continue

        field_name = source[field.start_byte:field.end_byte].decode(
            "utf-8", errors="replace"
        )

        # Create a getter-style accessor name from the member name.
        # mFoo -> GetFoo, mAlpha -> GetAlpha
        if field_name.startswith("m") and len(field_name) > 1:
            getter_name = f"Get{field_name[1:]}"
        else:
            getter_name = f"Get_{field_name}"

        # Build a local volatile wrapper that returns the member value.
        # This forces the compiler to NOT inline the access.
        #
        # auto _get_mFoo = [](auto* _self) __declspec(noinline) {
        #     return _self->mFoo;
        # };
        # Then replace obj->mFoo with _get_mFoo(obj)
        #
        # Actually, lambdas with __declspec(noinline) aren't well-supported.
        # Instead, use a template function before the caller:
        wrapper_func_name = f"_get_{field_name}"
        receiver_text = source[receiver.start_byte:receiver.end_byte]
        op_text = source[receiver.end_byte:field.start_byte]

        if b"->" in op_text:
            # Pointer access
            wrapper = (
                f"template <class _T>\n"
                f"__declspec(noinline) auto {wrapper_func_name}(_T* _obj)"
                f" -> decltype(_obj->{field_name}) {{\n"
                f"    return _obj->{field_name};\n"
                f"}}\n\n"
            ).encode("utf-8")
            call_prefix = b""
        else:
            # Value/reference access
            wrapper = (
                f"template <class _T>\n"
                f"__declspec(noinline) auto {wrapper_func_name}(_T& _obj)"
                f" -> decltype(_obj.{field_name}) {{\n"
                f"    return _obj.{field_name};\n"
                f"}}\n\n"
            ).encode("utf-8")
            call_prefix = b""

        ed = SourceEditor(source)
        ed.insert_at(func_node.start_byte, wrapper)

        # Replace all uses
        for node in sorted(nodes, key=lambda n: n.start_byte, reverse=True):
            recv_node = node.child_by_field_name("argument")
            if recv_node is None:
                continue
            recv_text = source[recv_node.start_byte:recv_node.end_byte]
            node_op = source[recv_node.end_byte:node.start_byte + len(source[node.start_byte:node.end_byte])]
            if b"->" in source[node.start_byte:node.end_byte]:
                replacement = f"{wrapper_func_name}({recv_text.decode('utf-8', errors='replace')})".encode("utf-8")
            else:
                replacement = f"{wrapper_func_name}({recv_text.decode('utf-8', errors='replace')})".encode("utf-8")
            ed.replace_node(node, replacement)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        yield Variant(
            name=f"accessor_outline_{counter}",
            pattern_name="accessor_outline",
            description=(
                f"Outline member access {access_key} via noinline getter "
                f"({len(nodes)} uses)"
            ),
            source=new_source,
            tags=frozenset({"outlined_accessor"}),
        )
        counter += 1
