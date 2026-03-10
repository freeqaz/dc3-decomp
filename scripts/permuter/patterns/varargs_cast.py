"""Varargs cast insertion — add (char *) casts on Symbol/FilePath args in printf-style calls.

Win rate: untested (proven in 5 manual fixes in HEAD~3 commits).

When Symbol, FilePath, or const char* types are passed to varargs functions
(MILO_NOTIFY, MILO_WARN, MILO_FAIL, MILO_ASSERT, printf), the compiler
may generate different code depending on whether an explicit cast is present.

Transformations:
    MILO_NOTIFY("msg %s", Name());       -> MILO_NOTIFY("msg %s", (char *)Name());
    MILO_WARN("file %s", fp);            -> MILO_WARN("file %s", (String &)fp);
    MILO_FAIL("path %s", PathName(obj)); -> MILO_FAIL("path %s", (char *)PathName(obj));

Detection signals:
    - bl mismatch (different call targets)
    - Replace mismatches
    - Clusters (insert/delete)
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# MILO macros and printf-style functions that take format strings
_MILO_MACROS = {
    b"MILO_NOTIFY", b"MILO_WARN", b"MILO_FAIL",
    b"MILO_ASSERT", b"MILO_ASSERT_FMT", b"MILO_NOTIFY_ONCE",
    b"MILO_LOG", b"printf", b"sprintf",
}

# Cast options to try per argument
_CAST_OPTIONS = [
    b"(char *)",
    b"(String &)",
]

# Regex to count %s placeholders in a format string
_PERCENT_S_RE = re.compile(rb"%s")


class VarargsCastPattern(Pattern):
    name = "varargs_cast"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # bl mismatches suggest different call targets
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
        # Higher priority for bl mismatches (cast differences cause different calls)
        for d in diagnosis.diff_ops:
            if d.target_opcode == "bl" or d.base_opcode == "bl":
                return 0.5
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        for call_node in _find_milo_calls(body, source):
            args_node = call_node.child_by_field_name("arguments")
            if args_node is None:
                continue

            # Get the format string and find %s positions
            fmt_arg, varargs = _split_format_and_args(args_node, source)
            if fmt_arg is None or not varargs:
                continue

            # Find which varargs correspond to %s placeholders
            fmt_text = source[fmt_arg.start_byte:fmt_arg.end_byte]
            s_positions = _get_percent_s_arg_indices(fmt_text)
            if not s_positions:
                continue

            # Collect the argument nodes at %s positions
            target_args = []
            for idx in s_positions:
                if idx < len(varargs):
                    arg = varargs[idx]
                    # Skip if already has a cast
                    if not _already_has_cast(arg, source):
                        target_args.append(arg)

            if not target_args:
                continue

            # Generate variants: one per arg per cast type
            for arg_node in target_args:
                casts = _select_casts_for_arg(arg_node, source, ctx)
                for cast in casts:
                    if counter >= 12:
                        return

                    ed = SourceEditor(source)
                    arg_text = source[arg_node.start_byte:arg_node.end_byte]
                    ed.replace_range(
                        arg_node.start_byte,
                        arg_node.end_byte,
                        cast + arg_text,
                    )

                    try:
                        new_source = ed.apply()
                    except ValueError:
                        continue

                    cast_str = cast.decode("utf-8")
                    arg_str = arg_text.decode("utf-8", errors="replace")
                    yield Variant(
                        name=f"varcast_{counter}",
                        pattern_name=self.name,
                        description=f"Add {cast_str} cast to {arg_str}",
                        source=new_source,
                    )
                    counter += 1

            # Generate "all at once" variant with (char *) if multiple args
            if len(target_args) > 1:
                if counter >= 12:
                    return

                ed = SourceEditor(source)
                for arg_node in target_args:
                    arg_text = source[arg_node.start_byte:arg_node.end_byte]
                    ed.replace_range(
                        arg_node.start_byte,
                        arg_node.end_byte,
                        b"(char *)" + arg_text,
                    )

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                yield Variant(
                    name=f"varcast_{counter}",
                    pattern_name=self.name,
                    description="Add (char *) cast to all %s args in call",
                    source=new_source,
                )
                counter += 1


def _find_milo_calls(node: Node, source: bytes) -> list[Node]:
    """Find call_expression nodes calling MILO macros or printf-style functions."""
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


def _split_format_and_args(
    args_node: Node, source: bytes
) -> tuple[Node | None, list[Node]]:
    """Split argument_list into (format_string, [varargs...]).

    The format string is the first argument that is a string_literal.
    Everything after it is a vararg.
    """
    children = args_node.named_children
    fmt_arg = None
    varargs: list[Node] = []

    for i, child in enumerate(children):
        if fmt_arg is None and child.type == "string_literal":
            fmt_arg = child
            varargs = list(children[i + 1 :])
            break

    return fmt_arg, varargs


def _get_percent_s_arg_indices(fmt_text: bytes) -> list[int]:
    """Return the 0-based vararg indices that correspond to %s placeholders.

    Handles %s specifically; skips %d, %f, etc.
    Scans the format string content for % sequences.
    """
    # Strip outer quotes if present
    inner = fmt_text
    if inner.startswith(b'"') and inner.endswith(b'"'):
        inner = inner[1:-1]

    indices = []
    arg_idx = 0
    i = 0
    while i < len(inner):
        if inner[i:i + 1] == b"%" and i + 1 < len(inner):
            next_ch = inner[i + 1 : i + 2]
            if next_ch == b"%":
                # Escaped %%
                i += 2
                continue
            # This is a format specifier — find the conversion char
            # Simple approach: scan past flags/width/precision to the conversion
            j = i + 1
            while j < len(inner) and inner[j:j + 1] in (
                b"0", b"1", b"2", b"3", b"4", b"5", b"6", b"7", b"8", b"9",
                b"-", b"+", b" ", b"#", b".", b"l", b"h", b"L", b"z",
            ):
                j += 1
            if j < len(inner):
                conv = inner[j:j + 1]
                if conv == b"s":
                    indices.append(arg_idx)
                arg_idx += 1
                i = j + 1
            else:
                i += 1
        else:
            i += 1

    return indices


def _already_has_cast(arg_node: Node, source: bytes) -> bool:
    """Check if an argument already has a C-style cast wrapping it."""
    # tree-sitter parses (char *)expr as a cast_expression
    if arg_node.type == "cast_expression":
        return True
    # Also check parenthesized cast: (Type)expr shows as cast_expression
    return False


def _select_casts_for_arg(
    arg_node: Node, source: bytes, ctx: FunctionContext
) -> list[bytes]:
    """Select appropriate casts for an argument using libclang when available.

    Falls back to the default _CAST_OPTIONS when libclang is unavailable or
    cannot resolve the type.
    """
    try:
        from ..clang_types import is_available, resolve_decl_type
        if is_available() and ctx.file_path.name != "null":
            type_info = resolve_decl_type(str(ctx.file_path), arg_node.start_byte)
            if type_info is not None:
                spelling = type_info.spelling.lower()
                # Already const char* — no cast needed
                if "char" in spelling and type_info.is_pointer:
                    return []
                # Symbol type → prefer (char*)
                if "symbol" in spelling:
                    return [b"(char *)"]
                # String type → prefer (String &)
                if "string" in spelling and not type_info.is_pointer:
                    return [b"(String &)"]
                # Pointer type → only (char*)
                if type_info.is_pointer:
                    return [b"(char *)"]
    except (ImportError, Exception):
        pass

    # Fallback: try both cast options
    return list(_CAST_OPTIONS)
