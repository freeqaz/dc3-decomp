"""Noreturn attribute pattern — add/remove __declspec(noreturn).

Adding __declspec(noreturn) to function declarations that never return
lets the compiler eliminate dead code after the call site.

Example:
    void Fail(const char* msg);
    ->
    __declspec(noreturn) void Fail(const char* msg);
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..types import Diagnosis, FunctionContext, Variant

# Known never-returning functions in the Milo engine
_NORETURN_FUNCS = {
    b"Fail",
    b"exit",
    b"abort",
    b"_exit",
    b"_Exit",
    b"quick_exit",
    b"terminate",
}

# Patterns to detect __declspec(noreturn) in source
_DECLSPEC_NORETURN_RE = re.compile(
    rb"__declspec\s*\(\s*noreturn\s*\)\s*"
)
_ATTRIBUTE_NORETURN_RE = re.compile(
    rb"__attribute__\s*\(\s*\(\s*noreturn\s*\)\s*\)\s*"
)


class NoreturnAttrPattern(Pattern):
    name = "noreturn_attr"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Relevant when there are clusters (dead code differences)
        if diagnosis.clusters:
            return True
        # Or when there are insert/delete mismatches suggesting extra code
        for d in diagnosis.diff_ops:
            if d.target_opcode == "" or d.base_opcode == "":
                return True
        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0

        # Direction 1: Find calls to known noreturn functions, add attribute
        # to their declarations if visible in the file
        for stmt in ctx.statements:
            for node in walk(stmt):
                if node.type != "call_expression":
                    continue

                func = node.child_by_field_name("function")
                if func is None:
                    continue

                # Get the function name (handle member access like TheDebug.Fail)
                func_name = _get_callee_name(func)
                if func_name is None or func_name not in _NORETURN_FUNCS:
                    continue

                # Try to find and annotate the declaration in the file
                for variant in _add_noreturn_to_decl(source, func_name, counter):
                    yield variant
                    counter += 1

        # Direction 2: Remove existing __declspec(noreturn) from declarations
        for variant in _remove_noreturn(source, counter):
            yield variant
            counter += 1


def _get_callee_name(func_node: Node) -> bytes | None:
    """Get the simple function name from a call target."""
    if func_node.type == "identifier":
        return func_node.text
    if func_node.type == "field_expression":
        # obj.Fail or obj->Fail
        field = func_node.child_by_field_name("field")
        if field is not None:
            return field.text
    return None


def _add_noreturn_to_decl(
    source: bytes, func_name: bytes, counter: int
) -> Iterator[Variant]:
    """Find function declarations for func_name and add __declspec(noreturn)."""
    # Search for declaration patterns like: void Fail(...)
    # This is a text-based search since the declaration may be outside
    # the function body (in a header or earlier in the file)
    pattern = re.compile(
        rb"^([ \t]*)((?:virtual\s+|static\s+|inline\s+)*)"
        rb"(void\s+)" + re.escape(func_name) + rb"\s*\(",
        re.MULTILINE,
    )

    for m in pattern.finditer(source):
        # Check if already has __declspec(noreturn)
        line_start = source.rfind(b"\n", 0, m.start()) + 1
        line = source[line_start:m.start()]
        if b"noreturn" in line:
            continue

        # Insert __declspec(noreturn) before return type
        insert_pos = m.start() + len(m.group(1)) + len(m.group(2))
        new_source = (
            source[:insert_pos]
            + b"__declspec(noreturn) "
            + source[insert_pos:]
        )
        yield Variant(
            name=f"noreturn_{counter}",
            pattern_name="noreturn_attr",
            description=f"Add __declspec(noreturn) to {func_name.decode()}",
            source=new_source,
        )


def _remove_noreturn(source: bytes, counter: int) -> Iterator[Variant]:
    """Remove __declspec(noreturn) or __attribute__((noreturn)) from source."""
    for pattern in (_DECLSPEC_NORETURN_RE, _ATTRIBUTE_NORETURN_RE):
        for m in pattern.finditer(source):
            new_source = source[:m.start()] + source[m.end():]
            yield Variant(
                name=f"noreturn_{counter}",
                pattern_name="noreturn_attr",
                description="Remove noreturn attribute",
                source=new_source,
            )
            counter += 1
