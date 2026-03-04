"""Float vs double literal suffix swapping.

Win rate: untested (new pattern).

In MSVC for PowerPC:
- `0.001` (no suffix) is a double literal -> generates lfd (64-bit float load)
- `0.001f` (f suffix) is a float literal -> generates lfs (32-bit float load)

The wrong literal type generates different load instructions and can cascade
into register width mismatches.

Transformations:
    0.001   -> 0.001f    (double to float)
    0.001f  -> 0.001     (float to double)
    0.0     -> 0.0f
    1.0f    -> 1.0

Detection signals:
    - lfd vs lfs mismatches
    - fcfid/frsp (double-to-float conversion) differences
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


class FloatDoubleLiteralPattern(Pattern):
    name = "float_double_literal"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            # lfd vs lfs — float width mismatch
            if (d.target_opcode == "lfd" and d.base_opcode == "lfs") or \
               (d.target_opcode == "lfs" and d.base_opcode == "lfd"):
                return True
            # frsp (round to single precision) differences
            if d.target_opcode == "frsp" or d.base_opcode == "frsp":
                return True

        # Also if there are replace mismatches (broad trigger, but cheap)
        if diagnosis.replace_real > 0:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        for d in diagnosis.diff_ops:
            if (d.target_opcode == "lfd" and d.base_opcode == "lfs") or \
               (d.target_opcode == "lfs" and d.base_opcode == "lfd"):
                return 0.8
        return 0.2

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        # Find all float/double literals in the function body
        literals = _find_float_literals(body, source)

        for lit_node, lit_text, has_f_suffix in literals:
            if counter >= 10:
                break

            ed = SourceEditor(source)

            if has_f_suffix:
                # Remove f suffix: 0.001f -> 0.001
                new_text = lit_text[:-1]  # strip trailing 'f' or 'F'
            else:
                # Add f suffix: 0.001 -> 0.001f
                new_text = lit_text + b"f"

            ed.replace_node(lit_node, new_text)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            old = lit_text.decode("utf-8", errors="replace")
            new = new_text.decode("utf-8", errors="replace")
            yield Variant(
                name=f"fltlit_{counter}",
                pattern_name=self.name,
                description=f"Swap literal {old} -> {new}",
                source=new_source,
            )
            counter += 1

        # Strategy 2: Swap ALL literals at once (common when a whole function
        # uses the wrong suffix convention)
        if len(literals) >= 2 and counter < 10:
            # Group by suffix type
            with_f = [(n, t) for n, t, has_f in literals if has_f]
            without_f = [(n, t) for n, t, has_f in literals if not has_f]

            for group, add_suffix in [(with_f, False), (without_f, True)]:
                if not group or counter >= 10:
                    continue

                ed = SourceEditor(source)
                for lit_node, lit_text in group:
                    if add_suffix:
                        ed.replace_node(lit_node, lit_text + b"f")
                    else:
                        ed.replace_node(lit_node, lit_text[:-1])

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                action = "Add f suffix to" if add_suffix else "Remove f suffix from"
                yield Variant(
                    name=f"fltlit_all_{counter}",
                    pattern_name=self.name,
                    description=f"{action} {len(group)} literals",
                    source=new_source,
                )
                counter += 1


def _find_float_literals(
    node: Node, source: bytes
) -> list[tuple[Node, bytes, bool]]:
    """Find float/double number literals in the AST.

    Returns [(node, text_bytes, has_f_suffix), ...]
    Only includes literals that look like floats (contain '.' or 'e/E').
    """
    results = []
    for n in walk(node):
        if n.type != "number_literal":
            continue

        text = source[n.start_byte:n.end_byte]
        text_str = text.decode("utf-8", errors="replace").lower()

        # Must be a float literal (has decimal point or exponent)
        if "." not in text_str and "e" not in text_str:
            continue

        # Skip hex floats, long doubles, etc.
        if text_str.startswith("0x"):
            continue
        if text_str.endswith("l") or text_str.endswith("L"):
            continue

        has_f = text_str.endswith("f")
        results.append((n, text, has_f))

    return results
