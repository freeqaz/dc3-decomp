"""Iterator dereference style swapping: (*it).member <-> it->member.

Win rate: untested (new pattern).

The compiler may generate different code for (*it).member vs it->member
when the iterator's operator* returns by reference vs operator-> returns
a pointer. Swapping between these styles can fix register allocation and
instruction ordering differences.

Transformations:
    (*it).mTarget   -> it->mTarget
    it->mWeight     -> (*it).mWeight

Detection signals:
    - Replace mismatches (instruction substitutions)
    - Register swaps involving iterator temporaries
    - Clusters near iterator dereferences
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


class IteratorDerefStylePattern(Pattern):
    name = "iterator_deref_style"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Broad relevance — cheap pattern, worth trying when there are real diffs
        if diagnosis.replace_real > 0:
            return True
        if diagnosis.clusters:
            return True
        for (r1, r2) in diagnosis.reg_swap_pairs:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        return 0.15  # Low priority, cheap to generate

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        # Strategy 1: Find (*it).member -> it->member
        for variant in _deref_dot_to_arrow(body, source, counter):
            yield variant
            counter += 1
            if counter >= 6:
                return

        # Strategy 2: Find it->member -> (*it).member
        for variant in _arrow_to_deref_dot(body, source, counter):
            yield variant
            counter += 1
            if counter >= 6:
                return


def _deref_dot_to_arrow(
    body: Node, source: bytes, counter: int
) -> Iterator[Variant]:
    """Convert (*it).member to it->member."""
    # Pattern: field_expression where argument is parenthesized(pointer_expression(*it))
    sites = []
    for n in walk(body):
        if n.type != "field_expression":
            continue

        arg = n.child_by_field_name("argument")
        field = n.child_by_field_name("field")
        if arg is None or field is None:
            continue

        # Check for (*it) pattern: parenthesized_expression > pointer_expression > identifier
        if arg.type != "parenthesized_expression":
            continue
        if arg.named_child_count != 1:
            continue

        inner = arg.named_children[0]
        if inner.type != "pointer_expression":
            continue

        # Get the iterator variable name
        operand = inner.child_by_field_name("argument")
        if operand is None:
            continue

        it_name = source[operand.start_byte:operand.end_byte]
        field_name = source[field.start_byte:field.end_byte]
        # Check it uses . (not ->)
        op = n.child_by_field_name("operator")
        full_text = source[n.start_byte:n.end_byte]
        if b"->" in full_text[:full_text.find(field_name)]:
            continue  # Already using ->

        sites.append((n, it_name, field_name))

    # Generate one variant per site
    for node, it_name, field_name in sites:
        if counter >= 6:
            break

        ed = SourceEditor(source)
        replacement = it_name + b"->" + field_name
        ed.replace_node(node, replacement)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        it_str = it_name.decode("utf-8", errors="replace")
        field_str = field_name.decode("utf-8", errors="replace")
        yield Variant(
            name=f"itderef_{counter}",
            pattern_name="iterator_deref_style",
            description=f"(*{it_str}).{field_str} -> {it_str}->{field_str}",
            source=new_source,
        )
        counter += 1

    # Also try converting ALL sites at once
    if len(sites) >= 2 and counter < 6:
        ed = SourceEditor(source)
        for node, it_name, field_name in sites:
            replacement = it_name + b"->" + field_name
            ed.replace_node(node, replacement)

        try:
            new_source = ed.apply()
            yield Variant(
                name=f"itderef_all_{counter}",
                pattern_name="iterator_deref_style",
                description=f"Convert all {len(sites)} (*it).member to it->member",
                source=new_source,
            )
        except ValueError:
            pass


def _arrow_to_deref_dot(
    body: Node, source: bytes, counter: int
) -> Iterator[Variant]:
    """Convert it->member to (*it).member for iterator-like variables."""
    # Find field_expression with -> operator where the argument looks like an iterator
    sites = []
    for n in walk(body):
        if n.type != "field_expression":
            continue

        arg = n.child_by_field_name("argument")
        field = n.child_by_field_name("field")
        if arg is None or field is None:
            continue

        # Must use -> operator
        full_text = source[n.start_byte:n.end_byte]
        field_name = source[field.start_byte:field.end_byte]
        # Find -> before the field name
        arrow_pos = full_text.find(b"->")
        if arrow_pos < 0:
            continue

        # Only for iterator-like variables (named 'it', 'iter', single letter, etc.)
        arg_text = source[arg.start_byte:arg.end_byte]
        arg_str = arg_text.decode("utf-8", errors="replace")
        if not _looks_like_iterator(arg_str):
            continue

        sites.append((n, arg_text, field_name))

    for node, it_name, field_name in sites:
        if counter >= 6:
            break

        ed = SourceEditor(source)
        replacement = b"(*" + it_name + b")." + field_name
        ed.replace_node(node, replacement)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        it_str = it_name.decode("utf-8", errors="replace")
        field_str = field_name.decode("utf-8", errors="replace")
        yield Variant(
            name=f"itarrow_{counter}",
            pattern_name="iterator_deref_style",
            description=f"{it_str}->{field_str} -> (*{it_str}).{field_str}",
            source=new_source,
        )
        counter += 1


def _looks_like_iterator(name: str) -> bool:
    """Heuristic: does this variable name look like an iterator?"""
    # Common iterator names
    if name in ("it", "iter", "itr", "i", "j", "k"):
        return True
    if name.startswith("it") and (len(name) <= 4 or name[2].isupper() or name[2] == '_'):
        return True
    if "iter" in name.lower() or "itr" in name.lower():
        return True
    return False
