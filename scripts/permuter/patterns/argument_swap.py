"""Argument swap pattern — swap arguments of 2-argument function calls.

Targets the documented right-to-left argument evaluation order
(TECHNICAL_NOTES line 322-334): swapping 2-arg function call arguments
changes load instruction order.

Transformations:
    func(a, b) -> func(b, a) for 2-argument calls only

Only swaps when arguments are simple expressions (identifiers, member
access, method calls) — skips if args have side effects.

Example:
    strcmp(name, "foo")
    ->
    strcmp("foo", name)
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import find_calls
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Argument types considered "safe" to swap (no side effects)
_SAFE_ARG_TYPES = {
    "identifier",
    "field_expression",
    "subscript_expression",
    "number_literal",
    "string_literal",
    "char_literal",
    "true",
    "false",
    "null",
    "this",
    "parenthesized_expression",
    "cast_expression",
    "unary_expression",  # &x, *x, !x, -x etc (no ++ or --)
}

# Unary operators with side effects — skip these
_SIDE_EFFECT_UNARY_OPS = {b"++", b"--"}


class ArgumentSwapPattern(Pattern):
    name = "argument_swap"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Relevant if there are insert/delete clusters or diff_arg mismatches
        if diagnosis.clusters:
            return True
        if diagnosis.reg_swap_pairs:
            return True
        # diff_arg with register mismatches suggests argument order issues
        if diagnosis.noise_total > diagnosis.noise_explained:
            return True
        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            for call_node in find_calls(stmt):
                args_node = call_node.child_by_field_name("arguments")
                if args_node is None:
                    continue

                named_args = list(args_node.named_children)
                if len(named_args) != 2:
                    continue

                arg_a, arg_b = named_args

                # Skip if either argument has side effects
                if not _is_safe_arg(arg_a) or not _is_safe_arg(arg_b):
                    continue

                # Skip if args are likely type-incompatible (54.5% build failure root cause)
                if not _args_type_compatible(arg_a, arg_b, ctx):
                    continue

                # Skip if args are identical text (swap would be no-op)
                a_text = arg_a.text
                b_text = arg_b.text
                if a_text == b_text:
                    continue

                # Swap arguments using SourceEditor
                ed = SourceEditor(ctx.file_source)
                ed.swap_nodes(arg_a, arg_b)
                new_source = ed.apply()

                a_str = a_text.decode("utf-8", errors="replace") if a_text else "?"
                b_str = b_text.decode("utf-8", errors="replace") if b_text else "?"
                yield Variant(
                    name=f"argswap_{counter}",
                    pattern_name=self.name,
                    description=f"Swap arguments: ({a_str}, {b_str}) -> ({b_str}, {a_str})",
                    source=new_source,
                )
                counter += 1


def _arg_category(node: Node) -> str:
    """Categorize an argument for type-compatibility checking."""
    if node.type == "string_literal":
        return "string"
    if node.type == "char_literal":
        return "char"
    if node.type == "number_literal":
        return "number"
    if node.type in ("true", "false"):
        return "bool"
    if node.type == "null":
        return "null"
    if node.type == "this":
        return "this"
    if node.type == "cast_expression":
        return "cast"
    # identifiers, field_expression, call_expression, subscript_expression
    # are all "expr" — likely compatible with each other
    return "expr"


def _cast_type_name(node: Node) -> str | None:
    """Extract the type name string from a cast_expression node, e.g. '(VShaderConstant)0' -> 'VShaderConstant'."""
    if node.type != "cast_expression":
        return None
    type_node = node.child_by_field_name("type")
    if type_node is None:
        return None
    text = type_node.text
    if text is None:
        return None
    return text.decode("utf-8", errors="replace").strip()


def _ident_is_pointer_like(ident: str, ctx: FunctionContext) -> bool:
    """Heuristic: True when *ident* is declared or used as a pointer in the TU.

    Mirrors the guard in signed_unsigned.py to detect pointer types syntactically.
    Two signals: arrow usage (``ident->``) and a pointer declaration
    (``Type *ident`` / ``Type* ident``). False positives only cost a skipped
    variant, so the bias toward detecting pointers is intentional.
    """
    src = ctx.file_source.decode("utf-8", errors="replace")
    esc = re.escape(ident)
    # Used as a pointer: ident->member
    if re.search(rf"\b{esc}\s*->", src):
        return True
    # Declared as a pointer: `Type *ident` or `Type* ident`
    if re.search(rf"[\w>\)]\s*\*\s*{esc}\b", src):
        return True
    return False


def _args_type_compatible(arg_a: Node, arg_b: Node, ctx: FunctionContext | None = None) -> bool:
    """Heuristic check if two args are likely type-compatible for swapping.

    Prevents build failures from swapping e.g. a Symbol and an int.
    """
    cat_a = _arg_category(arg_a)
    cat_b = _arg_category(arg_b)

    # Never swap `this` with anything
    if cat_a == "this" or cat_b == "this":
        return False

    # String literals are rarely interchangeable with other types
    if cat_a == "string" != cat_b:
        return False
    if cat_b == "string" != cat_a:
        return False

    # Number literal vs non-literal expr — likely different types
    if cat_a == "number" and cat_b == "expr":
        return False
    if cat_b == "number" and cat_a == "expr":
        return False

    # Bool literal vs non-bool — likely different types
    if cat_a == "bool" and cat_b not in ("bool", "number"):
        return False
    if cat_b == "bool" and cat_a not in ("bool", "number"):
        return False

    # null vs non-pointer-like — risky
    if cat_a == "null" and cat_b not in ("null", "expr"):
        return False
    if cat_b == "null" and cat_a not in ("null", "expr"):
        return False

    # cast_expression carries a typed annotation: (SomeType)val.
    # Swapping a typed cast with a differently-typed arg almost always causes
    # a build failure (54.5% of all argument_swap compile errors trace here).
    # Allow cast+cast only when both sides cast to the same type name.
    if cat_a == "cast" or cat_b == "cast":
        if cat_a == "cast" and cat_b == "cast":
            # Both casts — allow only if they name the same target type
            type_a = _cast_type_name(arg_a)
            type_b = _cast_type_name(arg_b)
            if type_a is None or type_b is None or type_a != type_b:
                return False
        else:
            # One cast, one non-cast — incompatible by construction
            return False

    # For expr+expr: reject when one identifier is pointer-like and the other
    # is not a pointer (e.g. RemoveSink(mObj*, gNullStr) → Symbol vs Object*).
    if cat_a == "expr" and cat_b == "expr" and ctx is not None:
        text_a = arg_a.text
        text_b = arg_b.text
        if text_a and text_b:
            str_a = text_a.decode("utf-8", errors="replace")
            str_b = text_b.decode("utf-8", errors="replace")
            # Only check single identifiers (not complex expressions)
            if re.match(r"^\w+$", str_a) and re.match(r"^\w+$", str_b):
                ptr_a = _ident_is_pointer_like(str_a, ctx)
                ptr_b = _ident_is_pointer_like(str_b, ctx)
                if ptr_a != ptr_b:
                    return False

    return True


def _is_safe_arg(node: Node) -> bool:
    """Check if an argument node is safe to swap (no side effects)."""
    if node.type in _SAFE_ARG_TYPES:
        # Extra check for unary: reject ++ and --
        if node.type == "unary_expression":
            op = node.child_by_field_name("operator")
            if op is not None and op.text in _SIDE_EFFECT_UNARY_OPS:
                return False
        return True

    # Allow call_expression (method calls) — they technically have side
    # effects but we want to try swapping them
    if node.type == "call_expression":
        return True

    return False
