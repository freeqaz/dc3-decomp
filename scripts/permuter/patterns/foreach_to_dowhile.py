"""FOREACH to do-while conversion — convert FOREACH macro loops to do-while with pre-guard.

Win rate: untested (new pattern, based on UIListDir::DrawWidgets 71.8->100% fix).

FOREACH(it, container) generates a bottom-check for loop:
    for (auto it = container.begin(); it != container.end(); ++it) { body }

The target compiler sometimes generates a top-check do-while with pre-guard:
    auto it = container.begin();
    if (it != container.end()) {
        do { body; ++it; } while (it != container.end());
    }

This affects:
- Loop entry: bottom-check (branch to condition) vs top-check (pre-guard + fallthrough)
- Register allocation: variables computed before vs inside the guard get different regs
- Instruction scheduling: iterator increment position relative to end-reload

Variants:
- Basic: Convert FOREACH to if+do-while (scoped in outer block)
- Scope narrowing: Move 1-2 preceding declarations into the guard block
  (changes when the compiler "sees" the variable, affecting register assignment)

Detection signals:
- Insert/delete clusters near loop entry (extra `b` instruction, missing lwz/cmplw/beq)
- Callee-saved register swaps (from different loop-invariant placement)

Skips FOREACH bodies that contain `continue;` (semantics change in do-while —
continue skips the increment). Also skips FOREACH_PTR and FOREACH_REVERSE variants.
"""

from __future__ import annotations

import re
from typing import Iterator

from .base import Pattern
from ..types import Diagnosis, FunctionContext, Variant

# Match FOREACH and FOREACH_CONST (the two most common variants).
# Group 1: indentation, Group 2: macro name, Group 3: iterator, Group 4: container
_FOREACH_RE = re.compile(
    rb"^([ \t]*)(FOREACH(?:_CONST)?)\s*\(\s*(\w+)\s*,\s*([^)]+?)\s*\)\s*\{",
    re.MULTILINE,
)

# Lines that look like simple declarations (not control flow or macro calls)
_DECL_LINE_RE = re.compile(
    rb"^[ \t]+"  # indentation
    rb"(?:bool|int|unsigned|float|double|char|auto|const\s+auto|"
    rb"std::\w+(?:<[^>]+>)?(?:::\w+)?)\s+"  # type
    rb"\w+\s*=\s*"  # name = value
    rb"[^;]+;\s*$",  # rest of line with semicolon
    re.MULTILINE,
)


class ForeachToDowhilePattern(Pattern):
    name = "foreach_to_dowhile"
    safety_tier = "conservative"
    structural_domain = "control_flow"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        if diagnosis.clusters:
            return True
        if diagnosis.reg_swap_pairs:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        score = 0.0
        if diagnosis.clusters:
            score = max(score, 0.5)
        if diagnosis.reg_swap_pairs:
            score = max(score, 0.3)
        return score if score > 0 else 0.1

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        func_start, func_end = ctx.func_byte_range
        func_source = source[func_start:func_end]
        counter = 0

        for match in _FOREACH_RE.finditer(func_source):
            if counter >= 8:
                return

            indent = match.group(1)
            macro = match.group(2)
            it_name = match.group(3)
            container = match.group(4).strip()

            # Find the opening brace (last char of match)
            brace_pos = match.end() - 1
            body_end = _find_matching_brace(func_source, brace_pos)
            if body_end is None:
                continue

            # Extract body content (between { and })
            body_inner = func_source[brace_pos + 1 : body_end]

            # Skip if body contains 'continue;' — semantics change in do-while
            if re.search(rb"\bcontinue\s*;", body_inner):
                continue

            # Compute absolute byte positions for replacement
            abs_start = func_start + match.start()
            abs_end = func_start + body_end + 1
            # Consume trailing newline
            if abs_end < len(source) and source[abs_end : abs_end + 1] == b"\n":
                abs_end += 1

            # --- Variant: basic do-while ---
            replacement = _build_dowhile(
                indent, it_name, container, body_inner
            )
            new_source = source[:abs_start] + replacement + source[abs_end:]

            it_str = it_name.decode("utf-8", errors="replace")
            ctr_str = container.decode("utf-8", errors="replace")
            yield Variant(
                name=f"foreach_dowhile_{counter}",
                pattern_name=self.name,
                description=(
                    f"Convert {macro.decode()}({it_str}, {ctr_str}) "
                    f"to do-while with pre-guard"
                ),
                source=new_source,
                func_byte_range=ctx.func_byte_range,
                original_source=source,
            )
            counter += 1

            # --- Scope narrowing variants ---
            # Find declaration-like lines immediately before the FOREACH
            preceding = _find_preceding_decl_lines(source, abs_start)
            for n_move in range(1, min(3, len(preceding) + 1)):
                if counter >= 8:
                    return

                stmts_to_move = preceding[-n_move:]
                # Re-indent moved statements for inside the if-guard
                moved_text = b""
                for _, line_bytes in stmts_to_move:
                    stripped = line_bytes.lstrip()
                    moved_text += indent + b"        " + stripped
                    if not moved_text.endswith(b"\n"):
                        moved_text += b"\n"

                replacement2 = _build_dowhile_scoped(
                    indent, it_name, container, body_inner, moved_text
                )

                # Start replacement from the first moved statement
                first_abs = stmts_to_move[0][0]
                new_source2 = source[:first_abs] + replacement2 + source[abs_end:]

                yield Variant(
                    name=f"foreach_dowhile_scope_{counter}",
                    pattern_name=self.name,
                    description=(
                        f"Convert FOREACH to do-while + move {n_move} "
                        f"decl(s) into guard"
                    ),
                    source=new_source2,
                    func_byte_range=ctx.func_byte_range,
                    original_source=source,
                )
                counter += 1


def _find_matching_brace(source: bytes, open_pos: int) -> int | None:
    """Find position of the matching closing brace, handling nesting and literals."""
    depth = 0
    i = open_pos
    in_line_comment = False
    in_block_comment = False
    while i < len(source):
        ch = source[i : i + 1]

        if in_line_comment:
            if ch == b"\n":
                in_line_comment = False
            i += 1
            continue

        if in_block_comment:
            if source[i : i + 2] == b"*/":
                in_block_comment = False
                i += 2
                continue
            i += 1
            continue

        if source[i : i + 2] == b"//":
            in_line_comment = True
            i += 2
            continue

        if source[i : i + 2] == b"/*":
            in_block_comment = True
            i += 2
            continue

        if ch == b'"':
            i += 1
            while i < len(source) and source[i : i + 1] != b'"':
                if source[i : i + 1] == b"\\":
                    i += 1
                i += 1
            i += 1
            continue

        if ch == b"'":
            i += 1
            while i < len(source) and source[i : i + 1] != b"'":
                if source[i : i + 1] == b"\\":
                    i += 1
                i += 1
            i += 1
            continue

        if ch == b"{":
            depth += 1
        elif ch == b"}":
            depth -= 1
            if depth == 0:
                return i

        i += 1

    return None


def _build_dowhile(
    indent: bytes,
    it_name: bytes,
    container: bytes,
    body_inner: bytes,
) -> bytes:
    """Build do-while replacement without scope narrowing.

    The body_inner keeps its original indentation. The wrapping
    structure doesn't need perfect formatting — it just needs to compile.
    """
    bi = indent + b"    "  # one level in
    inc_expr = b"++" + it_name

    parts = [
        indent + b"{\n",
        bi + b"auto " + it_name + b" = " + container + b".begin();\n",
        bi + b"if (" + it_name + b" != " + container + b".end()) {\n",
        bi + b"    do {",
        body_inner,
        b"\n" + bi + b"        " + inc_expr + b";\n",
        bi + b"    } while (" + it_name + b" != " + container + b".end());\n",
        bi + b"}\n",
        indent + b"}\n",
    ]
    return b"".join(parts)


def _build_dowhile_scoped(
    indent: bytes,
    it_name: bytes,
    container: bytes,
    body_inner: bytes,
    moved_stmts: bytes,
) -> bytes:
    """Build do-while with preceding statements moved into the guard block."""
    bi = indent + b"    "
    inc_expr = b"++" + it_name

    parts = [
        indent + b"{\n",
        bi + b"auto " + it_name + b" = " + container + b".begin();\n",
        bi + b"if (" + it_name + b" != " + container + b".end()) {\n",
        moved_stmts,
        bi + b"    do {",
        body_inner,
        b"\n" + bi + b"        " + inc_expr + b";\n",
        bi + b"    } while (" + it_name + b" != " + container + b".end());\n",
        bi + b"}\n",
        indent + b"}\n",
    ]
    return b"".join(parts)


def _find_preceding_decl_lines(
    source: bytes, foreach_abs_start: int
) -> list[tuple[int, bytes]]:
    """Find declaration-like lines immediately before the FOREACH.

    Returns list of (abs_line_start, line_bytes_with_newline) for lines
    that look like simple declarations. Stops at blank lines, control flow,
    or non-declaration statements.
    """
    results = []
    pos = foreach_abs_start

    for _ in range(5):  # Check up to 5 preceding lines
        # Skip back over any trailing newline
        if pos > 0 and source[pos - 1 : pos] == b"\n":
            line_end_excl = pos  # exclusive end (includes the \n)
            pos -= 1
        else:
            break

        # Find start of this line
        line_start = pos
        while line_start > 0 and source[line_start - 1 : line_start] != b"\n":
            line_start -= 1

        line_content = source[line_start:pos]
        stripped = line_content.strip()

        # Stop at blank lines
        if not stripped:
            break

        # Stop at control flow, braces, macros, labels
        if stripped.startswith(
            (
                b"if ", b"if(", b"else", b"for ", b"for(",
                b"while ", b"while(", b"do ", b"switch",
                b"return", b"goto ", b"FOREACH", b"{", b"}",
                b"case ", b"default:", b"#",
            )
        ):
            break

        # Must end with semicolon
        if not stripped.endswith(b";"):
            break

        # Skip macro calls (MILO_ASSERT, etc.)
        if re.match(rb"^[A-Z_]{2,}\s*\(", stripped):
            break

        # Accept: looks like a declaration or assignment
        results.append((line_start, source[line_start:line_end_excl]))
        pos = line_start

    results.reverse()  # Restore source order
    return results
