"""Math return type cast — wrap math function calls in (float) cast.

Win rate: untested (new pattern).

Math library functions (ceil, floor, sqrt, sin, cos, etc.) return double on PPC.
When the result is cast to int, MSVC sometimes needs an explicit (float) intermediate
cast to generate `frsp` (round-to-single-precision) before `fctiwz` (float-to-int).

Transformations:
    (int)ceil(x)       -> (int)(float)ceil(x)
    (int)(float)ceil(x) -> (int)ceil(x)     (reverse)

Detection signals:
    - frsp in target but not base (or vice versa)
    - fctiwz differences near math calls
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, node_text
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Math functions that return double
_MATH_FUNCS = {
    b"ceil", b"floor", b"round", b"trunc",
    b"sqrt", b"sin", b"cos", b"tan",
    b"asin", b"acos", b"atan", b"atan2",
    b"fabs", b"fmod", b"pow", b"exp", b"log", b"log10",
    b"std::ceil", b"std::floor", b"std::round", b"std::trunc",
    b"std::sqrt", b"std::sin", b"std::cos", b"std::tan",
    b"std::asin", b"std::acos", b"std::atan", b"std::atan2",
    b"std::fabs", b"std::fmod", b"std::pow", b"std::exp",
    b"std::log", b"std::log10",
}

# Integer types we expect to see as outer cast targets
_INT_TYPES = {b"int", b"unsigned int", b"long", b"unsigned long", b"short",
              b"unsigned short", b"char", b"unsigned char"}


class MathReturnCastPattern(Pattern):
    name = "math_return_cast"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            # frsp present in target but not base (need to add float cast)
            if d.target_opcode == "frsp" and d.base_opcode != "frsp":
                return True
            # frsp present in base but not target (need to remove float cast)
            if d.base_opcode == "frsp" and d.target_opcode != "frsp":
                return True

        # Also trigger on replace mismatches (frsp can show as replace)
        if diagnosis.replace_real > 0:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        for d in diagnosis.diff_ops:
            if d.target_opcode == "frsp" or d.base_opcode == "frsp":
                return 0.8
        return 0.2

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        # Strategy 1: Find (int)math_call(...) and add (float) cast
        for variant in _add_float_casts(source, body, counter):
            yield variant
            counter += 1
            if counter >= 8:
                return

        # Strategy 2: Find (int)(float)math_call(...) and remove (float) cast
        for variant in _remove_float_casts(source, body, counter):
            yield variant
            counter += 1
            if counter >= 8:
                return


def _add_float_casts(source: bytes, body: Node, counter: int) -> Iterator[Variant]:
    """Find (type)math_call(...) and wrap with (float) cast."""
    for cast_node in walk(body):
        if cast_node.type != "cast_expression":
            continue

        # Get the type being cast to
        type_node = cast_node.child_by_field_name("type")
        value_node = cast_node.child_by_field_name("value")
        if type_node is None or value_node is None:
            continue

        type_text = node_text(source, type_node).strip()
        if type_text not in _INT_TYPES:
            continue

        # Check if value is a math call (possibly in parens)
        call = _unwrap_to_call(value_node)
        if call is None:
            continue

        func_node = call.child_by_field_name("function")
        if func_node is None:
            continue

        func_text = node_text(source, func_node).strip()
        if func_text not in _MATH_FUNCS:
            continue

        # Check it's not already (int)(float)call(...)
        # That would be: cast to int, where value is cast to float
        if value_node.type == "cast_expression":
            inner_type = value_node.child_by_field_name("type")
            if inner_type is not None:
                inner_type_text = node_text(source, inner_type).strip()
                if inner_type_text == b"float":
                    continue  # Already has (float) cast

        # Also check parenthesized: (int)(float)call
        if value_node.type == "parenthesized_expression":
            inner = value_node.named_children[0] if value_node.named_children else None
            if inner and inner.type == "cast_expression":
                inner_type = inner.child_by_field_name("type")
                if inner_type is not None and node_text(source, inner_type).strip() == b"float":
                    continue

        # Insert (float) before the math call
        call_text = node_text(source, value_node)
        new_text = b"(float)" + call_text

        ed = SourceEditor(source)
        ed.replace_node(value_node, new_text)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        func_name = func_text.decode("utf-8", errors="replace")
        yield Variant(
            name=f"mathcast_add_{counter}",
            pattern_name="math_return_cast",
            description=f"Add (float) cast to {func_name}() return",
            source=new_source,
        )
        counter += 1


def _remove_float_casts(source: bytes, body: Node, counter: int) -> Iterator[Variant]:
    """Find (type)(float)math_call(...) and remove the (float) cast."""
    for cast_node in walk(body):
        if cast_node.type != "cast_expression":
            continue

        type_node = cast_node.child_by_field_name("type")
        value_node = cast_node.child_by_field_name("value")
        if type_node is None or value_node is None:
            continue

        type_text = node_text(source, type_node).strip()
        if type_text not in _INT_TYPES:
            continue

        # Value should be another cast to float
        if value_node.type != "cast_expression":
            continue

        inner_type = value_node.child_by_field_name("type")
        inner_value = value_node.child_by_field_name("value")
        if inner_type is None or inner_value is None:
            continue

        inner_type_text = node_text(source, inner_type).strip()
        if inner_type_text != b"float":
            continue

        # Inner value should be a math call
        call = _unwrap_to_call(inner_value)
        if call is None:
            continue

        func_node = call.child_by_field_name("function")
        if func_node is None:
            continue

        func_text = node_text(source, func_node).strip()
        if func_text not in _MATH_FUNCS:
            continue

        # Remove the (float) cast — replace value_node with inner_value
        inner_text = node_text(source, inner_value)

        ed = SourceEditor(source)
        ed.replace_node(value_node, inner_text)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        func_name = func_text.decode("utf-8", errors="replace")
        yield Variant(
            name=f"mathcast_rm_{counter}",
            pattern_name="math_return_cast",
            description=f"Remove (float) cast from {func_name}() return",
            source=new_source,
        )
        counter += 1


def _unwrap_to_call(node: Node) -> Node | None:
    """Unwrap parenthesized expressions to find a call_expression."""
    current = node
    for _ in range(3):  # Max unwrap depth
        if current.type == "call_expression":
            return current
        if current.type == "parenthesized_expression":
            children = current.named_children
            if len(children) == 1:
                current = children[0]
                continue
        break
    return None
