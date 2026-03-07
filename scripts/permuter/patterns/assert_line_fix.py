"""MILO_ASSERT / MILO_WARN / MILO_FAIL line number correction.

Win rate: proven in 1 manual fix (ChunkStream::WriteChunk, 4 asserts off by +6).

Hardcoded __LINE__ values in MILO_ASSERT(cond, LINE) are baked into the binary
as `li rN, <value>`. When lines are added or removed above the assert, the
number drifts by a uniform delta.

Strategy:
    1. Find all MILO_ASSERT/MILO_WARN/MILO_FAIL calls via tree-sitter
    2. Extract the numeric line argument from each
    3. Generate variants applying uniform deltas (-10..+10) to ALL asserts at once
    4. Scorer picks the winning delta (if any)

Detection signals:
    - offset_deltas has entries (li immediates are captured there)
    - Any diff_arg mismatches exist (MILO_ASSERT li shows as diff_arg)
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Macros that take a line number as their last argument
_LINE_MACROS = {b"MILO_ASSERT", b"MILO_WARN", b"MILO_FAIL"}


class AssertLineFixPattern(Pattern):
    name = "assert_line_fix"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # li immediates show up as diff_arg, which feeds offset_deltas
        if diagnosis.offset_deltas:
            return True
        # Any noise at all could include li mismatches
        if diagnosis.noise_total > diagnosis.noise_explained:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Check if offset_deltas has a dominant small delta (line numbers
        # typically differ by 1-20 lines, not large struct offsets like 0x40)
        for delta, count in diagnosis.offset_deltas.items():
            if 1 <= abs(delta) <= 30 and count >= 2:
                return 0.8  # Strong signal
        return 0.2

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node

        # Find all MILO_ASSERT/MILO_WARN/MILO_FAIL calls
        macro_calls = _find_line_macro_calls(body, source)
        if not macro_calls:
            return

        counter = 0

        # Strategy 1: Uniform delta applied to ALL asserts at once
        # Try deltas from -10 to +10 (skip 0)
        for delta in _prioritized_deltas(ctx.diagnosis):
            if counter >= 20:
                break

            ed = SourceEditor(source)
            any_change = False

            for call_node, line_arg_node, old_val in macro_calls:
                new_val = old_val + delta
                if new_val <= 0:
                    continue
                ed.replace_node(line_arg_node, str(new_val).encode())
                any_change = True

            if not any_change:
                continue

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            yield Variant(
                name=f"assertline_{counter}",
                pattern_name=self.name,
                description=f"Shift {len(macro_calls)} assert line numbers by {delta:+d}",
                source=new_source,
            )
            counter += 1

        # Strategy 2: Per-assert individual deltas (for non-uniform cases)
        # Only if we have few asserts (otherwise too many variants)
        if len(macro_calls) <= 4:
            for call_node, line_arg_node, old_val in macro_calls:
                for delta in (1, -1, 2, -2, 3, -3, 6, -6):
                    if counter >= 30:
                        return
                    new_val = old_val + delta
                    if new_val <= 0:
                        continue

                    ed = SourceEditor(source)
                    ed.replace_node(line_arg_node, str(new_val).encode())

                    try:
                        new_source = ed.apply()
                    except ValueError:
                        continue

                    yield Variant(
                        name=f"assertline_{counter}",
                        pattern_name=self.name,
                        description=f"Shift assert line {old_val} -> {new_val} ({delta:+d})",
                        source=new_source,
                    )
                    counter += 1


def _prioritized_deltas(diagnosis: Diagnosis | None) -> list[int]:
    """Return deltas to try, prioritizing those matching offset_deltas histogram."""
    # Default order: small deltas first, both directions
    all_deltas = []

    # If diagnosis has offset_deltas, try those first (sorted by count descending)
    if diagnosis and diagnosis.offset_deltas:
        by_count = sorted(
            diagnosis.offset_deltas.items(),
            key=lambda x: x[1],
            reverse=True,
        )
        for delta, count in by_count:
            if 1 <= abs(delta) <= 30:
                all_deltas.append(delta)

    # Then fill in remaining small deltas
    for d in range(1, 11):
        if d not in all_deltas:
            all_deltas.append(d)
        if -d not in all_deltas:
            all_deltas.append(-d)

    return all_deltas


def _find_line_macro_calls(
    node: Node, source: bytes
) -> list[tuple[Node, Node, int]]:
    """Find MILO_ASSERT/MILO_WARN/MILO_FAIL calls and extract line number args.

    Returns [(call_node, line_arg_node, line_number_int), ...]
    """
    results = []
    for n in walk(node):
        if n.type != "call_expression":
            continue

        func = n.child_by_field_name("function")
        if func is None:
            continue

        func_text = source[func.start_byte:func.end_byte]
        if func_text not in _LINE_MACROS:
            continue

        args = n.child_by_field_name("arguments")
        if args is None:
            continue

        # Get the last argument (line number)
        arg_nodes = [c for c in args.named_children if c.type != "comment"]
        if not arg_nodes:
            continue

        line_arg = arg_nodes[-1]
        line_text = source[line_arg.start_byte:line_arg.end_byte].strip()

        try:
            line_val = int(line_text)
        except ValueError:
            continue

        # Sanity: line numbers should be reasonable
        if 1 <= line_val <= 99999:
            results.append((n, line_arg, line_val))

    return results
