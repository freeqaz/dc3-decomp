"""Append .Str() to Symbol-returning args in MILO macros.

Win rate: untested (new pattern).

ClassName(), Name(), StaticClassName(), TypeDef() return Symbol. When passed
directly to MILO_NOTIFY / MILO_WARN / MILO_FAIL / MILO_ASSERT, the compiler
instantiates MakeString<...,Symbol,...> instead of MakeString<...,const char*,...>.
The mangled template name differs, causing a bl target mismatch.

Appending .Str() converts Symbol -> const char*, fixing the template.

Transformations:
    ClassName()           -> ClassName().Str()
    obj->Name()           -> obj->Name().Str()
    StaticClassName()     -> StaticClassName().Str()

Detection signals:
    - bl mismatches (different MakeString specialization)
    - replace_real > 0 (function call target differs)
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# MILO macros that take format strings
_MILO_MACROS = {
    b"MILO_NOTIFY", b"MILO_WARN", b"MILO_LOG", b"MILO_FAIL",
    b"MILO_ASSERT", b"MILO_ASSERT_FMT", b"MILO_NOTIFY_ONCE",
}

# Method names known to return Symbol
_SYMBOL_METHODS = {
    b"ClassName", b"Name", b"StaticClassName", b"TypeDef",
}


class MiloStrConvPattern(Pattern):
    name = "milo_str_conv"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # bl mismatches suggest different call targets (MakeString specializations)
        for d in diagnosis.diff_ops:
            if d.target_opcode == "bl" or d.base_opcode == "bl":
                return True
        if diagnosis.replace_real > 0:
            return True
        if diagnosis.clusters:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        return 0.4

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        # Find all MILO macro call sites and their Symbol-returning args
        for call_node in _find_milo_calls(body, source):
            args_node = call_node.child_by_field_name("arguments")
            if args_node is None:
                continue

            # Collect Symbol-returning argument nodes
            symbol_args = _find_symbol_args(args_node, source)
            if not symbol_args:
                continue

            # Generate one variant per individual arg
            for arg_node in symbol_args:
                if counter >= 8:
                    return

                ed = SourceEditor(source)
                ed.insert_at(arg_node.end_byte, b".Str()")

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                arg_text = source[arg_node.start_byte:arg_node.end_byte].decode(
                    "utf-8", errors="replace"
                )
                yield Variant(
                    name=f"strconv_{counter}",
                    pattern_name=self.name,
                    description=f"Add .Str() to {arg_text}",
                    source=new_source,
                )
                counter += 1

            # Generate "all at once" variant if multiple args
            if len(symbol_args) > 1:
                if counter >= 8:
                    return

                ed = SourceEditor(source)
                for arg_node in symbol_args:
                    ed.insert_at(arg_node.end_byte, b".Str()")

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                yield Variant(
                    name=f"strconv_{counter}",
                    pattern_name=self.name,
                    description="Add .Str() to all Symbol args in call",
                    source=new_source,
                )
                counter += 1


def _find_milo_calls(node: Node, source: bytes) -> list[Node]:
    """Find call_expression nodes calling MILO macros."""
    results = []
    for n in walk(node):
        if n.type != "call_expression":
            continue
        func = n.child_by_field_name("function")
        if func is None:
            continue
        func_text = source[func.start_byte:func.end_byte]
        if func_text in _MILO_MACROS:
            results.append(n)
    return results


def _find_symbol_args(args_node: Node, source: bytes) -> list[Node]:
    """Find arguments that are calls to Symbol-returning methods.

    Handles both bare calls (ClassName()) and member access (obj->Name()).
    Returns the call_expression nodes.
    """
    results = []
    for child in args_node.named_children:
        # Walk the argument expression to find call_expressions
        for n in walk(child):
            if n.type != "call_expression":
                continue
            func = n.child_by_field_name("function")
            if func is None:
                continue

            # Get the actual method name (last identifier)
            method_name = _extract_method_name(func, source)
            if method_name in _SYMBOL_METHODS:
                # Make sure this call isn't already followed by .Str()
                if not _already_has_str(n, source):
                    results.append(n)
                break  # Only match the outermost call per argument
    return results


def _extract_method_name(func_node: Node, source: bytes) -> bytes:
    """Extract the method name from a function node.

    Handles:
    - ClassName           (identifier)
    - obj->Name           (field_expression)
    - Foo::StaticClassName (qualified_identifier)
    """
    if func_node.type == "identifier":
        return source[func_node.start_byte:func_node.end_byte]
    elif func_node.type == "field_expression":
        field = func_node.child_by_field_name("field")
        if field is not None:
            return source[field.start_byte:field.end_byte]
    elif func_node.type == "qualified_identifier":
        name = func_node.child_by_field_name("name")
        if name is not None:
            return source[name.start_byte:name.end_byte]
    return b""


def _already_has_str(call_node: Node, source: bytes) -> bool:
    """Check if a call_expression is already followed by .Str().

    Looks at the parent to see if this call is the object in a member access
    calling Str().
    """
    parent = call_node.parent
    if parent is None:
        return False

    # Pattern: call_node is the "argument" of a field_expression -> .Str()
    if parent.type == "field_expression":
        field = parent.child_by_field_name("field")
        if field is not None:
            field_text = source[field.start_byte:field.end_byte]
            if field_text == b"Str":
                return True

    # Pattern: parent is a call_expression whose function is a field_expression
    # pointing to .Str on our call
    if parent.type == "call_expression":
        func = parent.child_by_field_name("function")
        if func is not None and func.type == "field_expression":
            field = func.child_by_field_name("field")
            if field is not None:
                field_text = source[field.start_byte:field.end_byte]
                if field_text == b"Str":
                    return True

    return False
