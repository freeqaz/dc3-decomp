"""Missing function call detection — find bl instructions in target absent from source.

Win rate: proven in 1 manual fix (SystemInit missing GlitchFinder::Init()).

When the target binary has a `bl <symbol>` that our source doesn't emit, it shows
as a delete cluster in objdiff. This pattern detects such clusters and reports the
missing call as a diagnostic hint.

This is an opt-in diagnostic pattern — it reports findings but doesn't blindly
insert calls (too risky). Use it to identify what's missing, then fix manually.

Detection signals:
    - Delete clusters containing bl instructions
    - Ghidra decompilation shows calls absent from source
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, find_calls, node_text
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


class MissingCallPattern(Pattern):
    name = "missing_call"
    opt_in = True  # Diagnostic only — don't run in batch sweeps

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Delete clusters suggest target has code we don't
        for c in diagnosis.clusters:
            if c.deletes > 0:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Small delete clusters (1-3 instructions) are likely single missing calls
        small_deletes = sum(
            1 for c in diagnosis.clusters
            if c.deletes <= 3 and c.inserts == 0
        )
        if small_deletes > 0:
            return 0.6
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        """Ghidra-guided: find calls in Ghidra output missing from source.

        For each missing call, try uncommenting commented-out calls in the source
        that match the missing function name.
        """
        source = ctx.file_source
        counter = 0

        # Strategy 1: Ghidra-guided call diff
        if ctx.ghidra_code:
            ghidra_calls = _extract_ghidra_call_names(ctx.ghidra_code)
            source_calls = _extract_source_call_names(ctx.body_node, source)

            missing = ghidra_calls - source_calls
            if missing:
                # Try uncommenting commented-out calls matching missing names
                for variant in _try_uncomment_calls(source, ctx.func_byte_range, missing, counter):
                    yield variant
                    counter += 1

        # Strategy 2: Find commented-out function calls and uncomment them
        for variant in _try_uncomment_any_calls(source, ctx.func_byte_range, counter):
            if counter >= 10:
                return
            yield variant
            counter += 1


def _extract_ghidra_call_names(ghidra_code: str) -> set[str]:
    """Extract function names called in Ghidra decompilation."""
    names: set[str] = set()
    # Match function calls: FuncName( or Class::Method(
    # Ghidra uses demangled names
    for m in re.finditer(r'(\w[\w:~]*)\s*\(', ghidra_code):
        name = m.group(1)
        # Skip control flow keywords
        if name in ("if", "while", "for", "switch", "return", "sizeof", "do"):
            continue
        # Extract just the method name (last component after ::)
        parts = name.split("::")
        names.add(parts[-1])
    return names


def _extract_source_call_names(body_node: Node, source: bytes) -> set[str]:
    """Extract function names called in our source code."""
    names: set[str] = set()
    for call in find_calls(body_node):
        func = call.child_by_field_name("function")
        if func is None:
            continue
        func_text = source[func.start_byte:func.end_byte].decode("utf-8", errors="replace")
        # Extract just the method name (last :: component, or after -> or .)
        # Handle: obj->Method, Class::Method, Method
        parts = re.split(r'::|->|\.', func_text)
        names.add(parts[-1].strip())
    return names


def _try_uncomment_calls(
    source: bytes, func_range: tuple[int, int],
    missing_names: set[str], start_counter: int,
) -> Iterator[Variant]:
    """Find commented-out calls matching missing function names and uncomment them."""
    counter = start_counter
    start, end = func_range
    func_source = source[start:end]

    # Find // commented lines containing function calls
    for m in re.finditer(rb'([ \t]*)//\s*(.+)', func_source):
        if counter >= 10:
            return

        indent = m.group(1)
        commented = m.group(2).strip()

        # Check if the commented text looks like a function call
        call_match = re.match(rb'(\w[\w:~]*(?:::\w+)*)\s*\(', commented)
        if not call_match:
            continue

        call_name = call_match.group(1).decode("utf-8", errors="replace")
        # Extract last component
        last_part = call_name.split("::")[-1]

        if last_part not in missing_names:
            continue

        # Uncomment this line
        comment_start = start + m.start()
        comment_end = start + m.end()

        # Find the full line (including the //)
        line_start = comment_start
        while line_start > 0 and source[line_start - 1:line_start] not in (b"\n", b"\r"):
            line_start -= 1

        ed = SourceEditor(source)
        # Replace the commented line with the uncommented version
        uncommented = indent + commented
        if not uncommented.rstrip().endswith(b";"):
            uncommented = uncommented.rstrip() + b";"
        ed.replace_range(line_start, comment_end, uncommented)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        yield Variant(
            name=f"misscall_{counter}",
            pattern_name="missing_call",
            description=f"Uncomment call: {call_name}",
            source=new_source,
        )
        counter += 1


def _try_uncomment_any_calls(
    source: bytes, func_range: tuple[int, int], start_counter: int,
) -> Iterator[Variant]:
    """Find any commented-out function calls and try uncommenting them."""
    counter = start_counter
    start, end = func_range
    func_source = source[start:end]

    for m in re.finditer(rb'([ \t]*)//\s*(.+)', func_source):
        if counter >= 6:
            return

        indent = m.group(1)
        commented = m.group(2).strip()

        # Must look like a function call (name followed by parentheses)
        if not re.match(rb'\w[\w:~]*(?:::\w+)*\s*\(', commented):
            continue

        # Skip if it's clearly a comment, not commented-out code
        if commented.startswith(b"TODO") or commented.startswith(b"FIXME"):
            continue
        if commented.startswith(b"NOTE") or commented.startswith(b"HACK"):
            continue

        # Must end with ); or similar to look like a statement
        if not re.search(rb'\)\s*;?\s*$', commented):
            continue

        comment_start = start + m.start()
        comment_end = start + m.end()

        line_start = comment_start
        while line_start > 0 and source[line_start - 1:line_start] not in (b"\n", b"\r"):
            line_start -= 1

        ed = SourceEditor(source)
        uncommented = indent + commented
        if not uncommented.rstrip().endswith(b";"):
            uncommented = uncommented.rstrip() + b";"
        ed.replace_range(line_start, comment_end, uncommented)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        call_text = commented.decode("utf-8", errors="replace")
        if len(call_text) > 50:
            call_text = call_text[:47] + "..."

        yield Variant(
            name=f"misscall_{counter}",
            pattern_name="missing_call",
            description=f"Uncomment: {call_text}",
            source=new_source,
        )
        counter += 1
