#!/usr/bin/env python3
"""
LP64 Portability Scanner for DC3 Decomp

Uses tree-sitter to parse C++ and find patterns that break when porting from
Xbox 360 (ILP32, wchar_t=2, big-endian) to x86_64 Linux (LP64, wchar_t=4, little-endian).

Categories:
  WCHAR    - wchar_t size mismatch (2 vs 4 bytes)
  PTRCAST  - Pointer truncation via cast to int-sized type
  PTRDIFF  - Pointer difference stored in int (should be ptrdiff_t)
  STRUCTIO - sizeof(UserType) in binary I/O or hardcoded struct offsets
  PTRCMP   - Pointer compared with integer via relational operator
  INTASPTR - int field used as pointer (cast from int member to pointer type)

Usage:
  python3 scripts/analysis/lp64_scanner.py [--dir src/] [--severity high|medium|low|all]
      [--category WCHAR,PTRCAST,PTRDIFF,STRUCTIO,PTRCMP] [--exclude-guarded] [--json]
"""

from __future__ import annotations

import argparse
import json
import multiprocessing
import os
import re
import sys
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Iterator

import tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser, Node

CPP_LANGUAGE = Language(tscpp.language())
_PARSER = Parser(CPP_LANGUAGE)


@dataclass
class Finding:
    file: str
    line: int
    category: str
    severity: str
    rule_name: str
    text: str
    guarded: bool = False
    suggestion: str = ""


# ── Helpers ──────────────────────────────────────────────────────────────────

SKIP_DIRS = {
    "stlport", "xdk", "curl", ".git", "build", "orig", "tools", "powerpc",
    "__pycache__", "node_modules", ".gemini", "jpeg", "oggvorbis", "zlib",
    "native",      # Native port code is LP64-aware by design
    "rnddx9",      # Xbox 360 DX9 renderer — never compiled for native
    "synth_xbox",   # Xbox 360 synth/DSP — never compiled for native
}

SKIP_FILES = {
    "types.h", "msvc_compat.h", "types_compat.h", "link_glue.cpp",
    "KinectSharePanel.cpp",  # Native uses stub (KinectShare_Stub.cpp)
    "KinectShareJobs.cpp",   # Native uses stub
    "KinectShare.cpp",       # Native uses stub
    "PoolAlloc.cpp",         # Raw memory pool management — offsets are pool headers, not structs
    "DataFlex.c",            # Auto-generated flex lexer — char* buffer comparisons are fine
}

SOURCE_EXTS = {".cpp", ".c", ".h", ".hpp", ".inl"}


def should_scan(path: Path) -> bool:
    if path.suffix not in SOURCE_EXTS:
        return False
    parts = set(path.parts)
    if parts & SKIP_DIRS:
        return False
    if path.name in SKIP_FILES:
        return False
    return True


def walk(node: Node) -> Iterator[Node]:
    """Depth-first walk of all nodes."""
    yield node
    for child in node.children:
        yield from walk(child)


def node_text(node: Node) -> str:
    """Get decoded text of a node."""
    return node.text.decode("utf-8") if node.text else ""


def line_text(source: bytes, node: Node) -> str:
    """Get the full source line containing a node."""
    start = source.rfind(b"\n", 0, node.start_byte) + 1
    end = source.find(b"\n", node.start_byte)
    if end == -1:
        end = len(source)
    return source[start:end].decode("utf-8", errors="replace").strip()


# ── Guard detection ──────────────────────────────────────────────────────────

def detect_guard_regions(source: bytes) -> list[bool]:
    """
    Return per-line bool where True = inside any branch of an HX_NATIVE conditional.

    Lines in EITHER #ifdef HX_NATIVE or its #else are marked guarded, since:
    - #ifdef HX_NATIVE: already has native-specific code
    - #else of #ifdef HX_NATIVE: Xbox-only code, never runs on native

    Only unguarded lines (outside any HX_NATIVE conditional) need scanning.
    """
    lines = source.split(b"\n")
    guarded = [False] * len(lines)

    # Stack tracks: is this conditional an HX_NATIVE conditional?
    # (True = any branch of this conditional is platform-specific)
    ifdef_stack: list[bool] = []
    platform_depth = 0  # depth of HX_NATIVE conditionals we're inside

    for i, line_bytes in enumerate(lines):
        stripped = line_bytes.strip()

        if (stripped.startswith(b"#ifdef HX_NATIVE")
                or stripped.startswith(b"#if defined(HX_NATIVE)")
                or stripped.startswith(b"#ifndef HX_NATIVE")
                or stripped.startswith(b"#if !defined(HX_NATIVE)")):
            ifdef_stack.append(True)  # This is an HX_NATIVE conditional
            platform_depth += 1
        elif stripped.startswith((b"#ifdef", b"#ifndef", b"#if ")):
            ifdef_stack.append(False)  # Not an HX_NATIVE conditional
        elif stripped.startswith(b"#else") or stripped.startswith(b"#elif"):
            pass  # Stay in same conditional — guarded status doesn't change
        elif stripped.startswith(b"#endif"):
            if ifdef_stack:
                was_platform = ifdef_stack.pop()
                if was_platform:
                    platform_depth -= 1

        guarded[i] = platform_depth > 0

    return guarded


def is_line_guarded(guarded: list[bool], byte_offset: int, source: bytes) -> bool:
    """Check if a byte offset falls on a guarded line."""
    line_num = source[:byte_offset].count(b"\n")
    if line_num < len(guarded):
        return guarded[line_num]
    return False


# ── AST-based type analysis ─────────────────────────────────────────────────

# Type specifiers that are int-sized (truncate 8-byte pointers)
_INT_TYPES = {"int", "unsigned int", "unsigned", "u32", "s32", "uint"}

# Type specifiers that indicate a pointer type in a cast
_PTR_SUFFIXES = {"*"}


def _get_cast_type(cast_node: Node) -> str:
    """Extract the type name from a cast_expression's type child."""
    type_desc = cast_node.child_by_field_name("type")
    if type_desc:
        return node_text(type_desc).strip()
    # Fallback: first child after '('
    for child in cast_node.children:
        if child.type == "type_descriptor":
            return node_text(child).strip()
    return ""


def _is_int_type(type_str: str) -> bool:
    """Check if a type string represents an int-sized type."""
    cleaned = type_str.strip().replace("  ", " ")
    return cleaned in _INT_TYPES


def _is_pointer_type(type_str: str) -> bool:
    """Check if a type string represents a pointer type."""
    return type_str.strip().endswith("*")


def _is_wchar_pointer_type(type_str: str) -> bool:
    """Check if type is wchar_t* or const wchar_t*."""
    cleaned = type_str.strip()
    return cleaned in ("wchar_t *", "wchar_t*", "const wchar_t *", "const wchar_t*")


def _expr_is_pointer(node: Node) -> bool:
    """Heuristically determine if an expression yields a pointer value.

    Uses AST structure to identify pointer-returning patterns:
    - 'this' keyword
    - 'new' expressions
    - '&var' address-of
    - Method calls like .data(), .begin(), .end(), .c_str()
    - Cast expressions to pointer types: (char*)x
    - Identifier names ending in common pointer suffixes
    """
    if node is None:
        return False

    txt = node_text(node)

    # this keyword
    if node.type == "this":
        return True

    # new expression
    if node.type == "new_expression":
        return True

    # address-of: &x
    if node.type == "pointer_expression":
        op = node.child_by_field_name("operator")
        if op and node_text(op) == "&":
            return True

    # Call expression: check for known pointer-returning methods
    if node.type == "call_expression":
        fn = node.child_by_field_name("function")
        if fn:
            fn_txt = node_text(fn)
            # .data(), .begin(), .end(), .c_str(), .Buffer()
            if any(fn_txt.endswith(f".{m}") or fn_txt.endswith(f"->{m}")
                   for m in ("data()", "begin()", "end()", "c_str()",
                             "Buffer()", "Str()", "String()")):
                return True

    # Cast to pointer type
    if node.type == "cast_expression":
        cast_type = _get_cast_type(node)
        if _is_pointer_type(cast_type):
            return True

    # Parenthesized expression — unwrap
    if node.type == "parenthesized_expression":
        for child in node.named_children:
            if _expr_is_pointer(child):
                return True

    return False


def _expr_is_definitely_not_pointer(node: Node) -> bool:
    """Heuristically determine if an expression definitely returns a non-pointer.

    Returns True for patterns like:
    - .size(), .length(), .count() — return size_t
    - Numeric literals
    - Enum-style identifiers
    - Arithmetic operations on non-pointers
    """
    if node is None:
        return False

    # Numeric literal
    if node.type in ("number_literal", "char_literal", "true", "false"):
        return True

    # Call to size/length/count methods
    if node.type == "call_expression":
        fn = node.child_by_field_name("function")
        if fn:
            fn_txt = node_text(fn)
            if any(fn_txt.endswith(f".{m}") or fn_txt.endswith(f"->{m}")
                   for m in ("size()", "length()", "count()", "capacity()",
                             "empty()", "TotalSize()", "Size()", "Num()",
                             "GetMask()", "GetChangedMask()")):
                return True

    # Parenthesized expression — unwrap
    if node.type == "parenthesized_expression":
        for child in node.named_children:
            return _expr_is_definitely_not_pointer(child)

    return False


# ── WCHAR checks ─────────────────────────────────────────────────────────────

# wchar_t string functions
_WCHAR_WRITE_FNS = {"wcscpy", "wcsncpy", "wcscat", "wcsncat", "wmemcpy", "wmemset", "wmemmove"}
_WCHAR_READ_FNS = {"wcschr", "wcsrchr", "wcsstr", "wcspbrk", "wcslen", "wcscmp", "wcsncmp"}
_WCHAR_ALL_FNS = _WCHAR_WRITE_FNS | _WCHAR_READ_FNS


def check_wchar(node: Node, source: bytes) -> list[tuple[str, str, str, str]]:
    """Check for wchar_t portability issues using AST."""
    results: list[tuple[str, str, str, str]] = []

    for n in walk(node):
        # 1. Cast to wchar_t* — the primary dangerous pattern
        if n.type == "cast_expression":
            cast_type = _get_cast_type(n)
            if _is_wchar_pointer_type(cast_type):
                # Get what's being cast
                value = n.child_by_field_name("value")
                if value:
                    val_txt = node_text(value)
                    # Skip if casting a wchar_t expression (same type)
                    if "wchar_t" not in val_txt:
                        results.append((
                            n, "wchar_t_pointer_cast", "high",
                            "Cast to wchar_t* — wchar_t is 4 bytes on Linux, 2 on Xbox. "
                            "Data will be read/written with wrong element size. "
                            "Use char16_t* or manual 2-byte operations."
                        ))

        # 2. Calls to wchar_t string functions with non-wchar arguments
        if n.type == "call_expression":
            fn = n.child_by_field_name("function")
            if fn:
                fn_name = node_text(fn).split("::")[-1]  # strip namespace
                if fn_name in _WCHAR_ALL_FNS:
                    args = n.child_by_field_name("arguments")
                    if args:
                        # Check if any argument has a cast to wchar_t*
                        args_text = node_text(args)
                        if "(wchar_t" in args_text:
                            sev = "high" if fn_name in _WCHAR_WRITE_FNS else "medium"
                            verb = "writes to" if fn_name in _WCHAR_WRITE_FNS else "reads from"
                            results.append((
                                n, f"wchar_fn_{fn_name}", sev,
                                f"{fn_name}() {verb} buffer via wchar_t* cast — "
                                f"element size mismatch (4 vs 2 bytes). "
                                f"Use char16_t-aware equivalent or memcpy with sizeof(u16)."
                            ))

        # 3. L"..." wide string literals
        if n.type == "string_literal":
            txt = node_text(n)
            if txt.startswith("L\""):
                # Check context: is it being used with unsigned short buffers?
                parent = n.parent
                parent_txt = node_text(parent) if parent else ""
                if "unsigned short" in parent_txt or "u16" in parent_txt:
                    results.append((
                        n, "wide_literal_mixed", "high",
                        "L\"...\" literal mixed with unsigned short — "
                        "L chars are 4 bytes on Linux. Use u\"...\" (char16_t)."
                    ))
                else:
                    results.append((
                        n, "wide_literal", "medium",
                        "L\"...\" literal — each char is 4 bytes on Linux, 2 on Xbox. "
                        "If used with unsigned short buffers elsewhere, data will mismatch."
                    ))

        # 4. sizeof(wchar_t)
        if n.type == "sizeof_expression":
            inner = node_text(n)
            if "wchar_t" in inner:
                results.append((
                    n, "sizeof_wchar_t", "medium",
                    "sizeof(wchar_t) = 4 on Linux, 2 on Xbox. "
                    "Use sizeof(unsigned short) or sizeof(char16_t) for 2-byte size."
                ))

    return results


# ── PTRCAST checks ───────────────────────────────────────────────────────────

def check_ptrcast(node: Node, source: bytes) -> list[tuple[str, str, str, str]]:
    """Check for pointer-to-int truncation using AST."""
    results: list[tuple[str, str, str, str]] = []

    for n in walk(node):
        if n.type != "cast_expression":
            continue

        cast_type = _get_cast_type(n)
        if not _is_int_type(cast_type):
            continue

        # Get the value being cast
        value = n.child_by_field_name("value")
        if value is None:
            continue

        # Skip if the value is definitely not a pointer
        if _expr_is_definitely_not_pointer(value):
            continue

        # Check if the value is a pointer expression
        if _expr_is_pointer(value):
            results.append((
                n, "ptr_cast_to_int", "high",
                f"Pointer expression cast to {cast_type} — truncates from 8 to 4 bytes on LP64. "
                "Use (intptr_t) or (uintptr_t)."
            ))

    return results


# ── PTRDIFF checks ───────────────────────────────────────────────────────────

def check_ptrdiff(node: Node, source: bytes) -> list[tuple[str, str, str, str]]:
    """Check for pointer differences stored in int-sized types."""
    results: list[tuple[str, str, str, str]] = []

    for n in walk(node):
        # Look for: int x = expr1 - expr2;  where expr1/expr2 are pointers
        if n.type == "declaration":
            # Get the type
            type_node = n.child_by_field_name("type")
            if type_node is None:
                continue
            type_txt = node_text(type_node).strip()
            if not _is_int_type(type_txt):
                continue

            # Get the declarator
            declarator = n.child_by_field_name("declarator")
            if declarator is None:
                continue

            # Skip pointer declarations: `int *x = ptr - 4;` is type=int,
            # declarator=pointer_declarator — the variable is int*, not int.
            decl_node = declarator
            if decl_node.type == "init_declarator":
                decl_node = decl_node.child_by_field_name("declarator") or decl_node
            if decl_node.type == "pointer_declarator":
                continue

            # Look for init_declarator with a binary subtraction
            for child in walk(declarator):
                if child.type == "binary_expression":
                    op = child.child_by_field_name("operator")
                    if op and node_text(op) == "-":
                        left = child.child_by_field_name("left")
                        right = child.child_by_field_name("right")
                        if left and right:
                            if _expr_is_pointer(left) or _expr_is_pointer(right):
                                # Array index pattern (ptr - &arr[0], ptr - arr.front())
                                # is bounded by array size — effectively safe if array < 2GB.
                                # Downgrade to medium since these are practical non-issues.
                                full_txt = node_text(child)
                                is_bounded = any(k in full_txt for k in (
                                    "front()", ".begin()", "&keys", "&buf",
                                    "- &", ".data()",
                                ))
                                sev = "medium" if is_bounded else "high"
                                results.append((
                                    n, "ptrdiff_stored_as_int", sev,
                                    f"Pointer difference stored as {type_txt} — "
                                    "may overflow on LP64. Use ptrdiff_t or intptr_t."
                                ))
                                break  # One finding per declaration

    return results


# ── STRUCTIO checks ──────────────────────────────────────────────────────────
#
# Detects two patterns that break when struct layout changes between ILP32/LP64:
#
# 1. sizeof(UserType) in binary I/O — If UserType contains pointers or longs,
#    sizeof() is larger on LP64. Reading sizeof() bytes from an ILP32-written
#    file consumes too many bytes, corrupting the stream position.
#
# 2. Hardcoded struct offsets — (char*)ptr + 0xNN assumes ILP32 member offsets.
#    LP64 pointer/padding changes shift members to different offsets.

# Primitive types whose sizeof is the same on both platforms
_PRIMITIVE_TYPES = {
    "int", "unsigned int", "unsigned", "signed", "short", "unsigned short",
    "char", "unsigned char", "signed char",
    "float", "double", "bool", "void",
    "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "f32", "f64",
    "uint", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8_t", "int16_t", "int32_t", "int64_t",
    "size_t", "ptrdiff_t", "intptr_t", "uintptr_t",
    "wchar_t",  # Flagged separately by WCHAR rules
}

# User-defined types known to contain only floats/ints (no pointers/longs).
# sizeof() is identical on ILP32 and LP64. Safe to use in memcpy/Read.
_SAFE_STRUCT_TYPES = {
    # Math types — pure float/int, no pointers
    "Vector2", "Vector3", "Vector4", "Hmx::Vector3", "Hmx::Vector4",
    "Quat", "Hmx::Quat",
    "Matrix3", "Hmx::Matrix3",
    "Transform", "Hmx::Transform",
    "Plane", "Hmx::Plane",
    "Rect", "Hmx::Rect",
    "Color", "Hmx::Color",
    "Sphere", "Hmx::Sphere",
    "Box", "Hmx::Box",
    "Segment",
    "PaddedJointPos",
    # GUID — 16 bytes, no pointers
    "HxGuid", "GUID",
    # Vertex types — floats + small ints
    "RndMesh::Vert",
}

# Functions that read/write raw bytes from binary streams (advance stream position)
# sizeof mismatch here corrupts ALL subsequent reads — critical.
_STREAM_IO_FUNCTIONS = {
    "Read", "ReadEndian", "ReadBytes", "ReadBytesAsync",
    "Write", "WriteEndian", "WriteBytes",
}

# Memory copy functions — sizeof mismatch copies wrong amount but doesn't
# corrupt a stream. Still worth flagging at lower severity.
_MEMCOPY_FUNCTIONS = {
    "memcpy", "memmove",
}

# Methods on stream objects (called as obj.Method or obj->Method)
_STREAM_IO_METHODS = {
    "Read", "ReadEndian", "ReadBytes",
    "Write", "WriteEndian", "WriteBytes",
}

_ALL_IO_FUNCTIONS = _STREAM_IO_FUNCTIONS | _MEMCOPY_FUNCTIONS


def _sizeof_type_name(sizeof_node: Node) -> str | None:
    """Extract the type name from a sizeof_expression, or None if primitive/expr."""
    txt = node_text(sizeof_node)

    # sizeof(Type) — has a parenthesized_type_descriptor child
    for child in sizeof_node.named_children:
        if child.type == "parenthesized_expression":
            inner = node_text(child).strip("() ")
            return inner if inner else None
        if child.type == "type_descriptor":
            return node_text(child).strip()

    # Fallback: parse "sizeof(X)" textually
    m = re.match(r'sizeof\s*\(\s*(.+?)\s*\)', txt)
    if m:
        return m.group(1)
    return None


def _is_user_type(type_name: str) -> bool:
    """Check if a type name is a user-defined type that might differ on LP64.

    Returns False for primitives and known-safe types (no pointers/longs).
    """
    # Strip const, volatile, pointers, references
    cleaned = type_name.strip()
    for prefix in ("const ", "volatile ", "struct ", "class ", "enum "):
        if cleaned.startswith(prefix):
            cleaned = cleaned[len(prefix):].strip()
    # Strip trailing * & []
    cleaned = re.sub(r'[\s\*&\[\]]+$', '', cleaned)
    cleaned = cleaned.strip()

    if not cleaned:
        return False
    if cleaned in _PRIMITIVE_TYPES:
        return False
    if cleaned in _SAFE_STRUCT_TYPES:
        return False

    # Must start with uppercase letter or be a qualified name (Foo::Bar)
    # This filters out variable names in sizeof(var) expressions
    if cleaned[0].isupper() or "::" in cleaned:
        return True

    return False


def _is_in_io_call(node: Node) -> tuple[bool, str, bool]:
    """Check if a node is an argument to a binary I/O function.

    Returns (True, function_name, is_stream) if the node or an ancestor
    argument_list belongs to a call to a known I/O function.
    is_stream=True means it's a BinStream read/write (corrupts stream position).
    is_stream=False means it's a memcpy/memmove (wrong size but no stream corruption).
    """
    current = node.parent
    while current:
        if current.type == "argument_list":
            call = current.parent
            if call and call.type == "call_expression":
                fn = call.child_by_field_name("function")
                if fn:
                    fn_txt = node_text(fn)
                    bare = fn_txt.split("::")[-1]
                    # Direct call
                    if bare in _STREAM_IO_FUNCTIONS:
                        return True, bare, True
                    if bare in _MEMCOPY_FUNCTIONS:
                        return True, bare, False
                    # Method call: stream.Read(...), bs->ReadEndian(...)
                    for method in _STREAM_IO_METHODS:
                        if fn_txt.endswith(f".{method}") or fn_txt.endswith(f"->{method}"):
                            return True, method, True
            break
        current = current.parent
    return False, "", False


def _is_hardcoded_offset(node: Node) -> tuple[bool, str]:
    """Check if a number literal looks like a hardcoded struct offset.

    Returns (True, hex_value) for hex literals >= 0x8 or decimal >= 16
    used in pointer arithmetic context.
    """
    if node.type != "number_literal":
        return False, ""

    txt = node_text(node)

    # Parse the value
    try:
        if txt.startswith("0x") or txt.startswith("0X"):
            val = int(txt, 16)
        elif txt.startswith("0") and len(txt) > 1 and txt[1:].isdigit():
            val = int(txt, 8)
        else:
            val = int(txt)
    except ValueError:
        return False, ""

    # Must be >= 8 (smaller offsets are likely field-level, not struct-level)
    # and must look like an offset (multiple of 4, or common struct sizes)
    if val < 0x8:
        return False, ""

    return True, txt


def _is_byte_ptr_cast(node: Node) -> bool:
    """Check if node is a cast to byte-level pointer (char*, u8*, void*, etc.)."""
    if node.type == "cast_expression":
        cast_type = _get_cast_type(node)
        cleaned = cast_type.strip()
        return any(cleaned.endswith(kw) for kw in
                   ("char *", "char*", "void *", "void*",
                    "u8 *", "u8*", "unsigned char *", "unsigned char*"))
    return False


def _find_byte_arith_offset(node: Node) -> tuple[bool, str, int]:
    """Check if a node is byte-pointer arithmetic: (char*)expr + N.

    Unwraps parenthesized expressions. Returns (found, offset_text, offset_value).
    """
    # Unwrap parens
    inner = node
    while inner.type == "parenthesized_expression" and inner.named_children:
        inner = inner.named_children[0]

    if inner.type != "binary_expression":
        return False, "", 0

    op = inner.child_by_field_name("operator")
    if not op or node_text(op) not in ("+", "-"):
        return False, "", 0

    left = inner.child_by_field_name("left")
    right = inner.child_by_field_name("right")
    if not left or not right:
        return False, "", 0

    # Identify which side is the byte-pointer cast and which is the offset
    byte_side = None
    num_side = None
    if _is_byte_ptr_cast(left) or _expr_is_pointer(left):
        byte_side, num_side = left, right
    elif _is_byte_ptr_cast(right) or _expr_is_pointer(right):
        byte_side, num_side = right, left

    if not byte_side or not num_side:
        return False, "", 0

    # Verify byte_side involves char*/u8*/void* cast (not typed pointer arithmetic)
    byte_txt = node_text(byte_side)
    if not any(kw in byte_txt for kw in
               ("char *", "char*", "void *", "void*",
                "u8 *", "u8*", "unsigned char *", "unsigned char*")):
        return False, "", 0

    # num_side must be a number literal (possibly via macro expansion)
    is_offset, offset_txt = _is_hardcoded_offset(num_side)
    if not is_offset:
        return False, "", 0

    try:
        if offset_txt.startswith(("0x", "0X")):
            val = int(offset_txt, 16)
        elif offset_txt.startswith("0") and len(offset_txt) > 1 and offset_txt[1:].isdigit():
            val = int(offset_txt, 8)
        else:
            val = int(offset_txt)
    except ValueError:
        return False, "", 0

    return True, offset_txt, val


def check_structio(node: Node, source: bytes) -> list[tuple]:
    """Check for struct sizeof in binary I/O and hardcoded struct offsets."""
    results: list[tuple] = []

    # Track lines already flagged by Rule 3 to avoid duplicate Rule 2 reports
    rule3_lines: set[int] = set()

    # ── Rule 3 pre-pass: *(Type*)((char*)expr + N) — full dereference pattern ──
    #
    # Higher confidence than Rule 2 because the outer dereference + cast
    # proves struct member access intent. Flags ANY offset (including small
    # decimal values like 4, 8) since offset 8 was the exact bug in
    # HamDriver::LayerArray::Eval (PPC offset 8 = mWeight, LP64 offset 8 = mBeat).
    for n in walk(node):
        if n.type != "pointer_expression":
            continue
        # Must be a dereference (*), not address-of (&)
        op_node = n.child_by_field_name("operator")
        if not op_node or node_text(op_node) != "*":
            continue

        argument = n.child_by_field_name("argument")
        if argument is None:
            continue

        # Unwrap parens around the cast
        inner = argument
        while inner.type == "parenthesized_expression" and inner.named_children:
            inner = inner.named_children[0]

        # Must be a cast to a non-void pointer type
        if inner.type != "cast_expression":
            continue
        outer_cast_type = _get_cast_type(inner)
        if not _is_pointer_type(outer_cast_type):
            continue
        # Skip casts to void* (not actually accessing a member)
        if outer_cast_type.strip() in ("void *", "void*"):
            continue

        # The cast's value must contain byte-pointer arithmetic
        cast_value = inner.child_by_field_name("value")
        if cast_value is None:
            continue

        found, offset_txt, offset_val = _find_byte_arith_offset(cast_value)
        if not found:
            continue

        # Skip offset 0 (no shift possible)
        if offset_val == 0:
            continue

        # Determine severity based on offset range:
        # - 4-64: high — typical struct member offsets affected by vtable/ptr growth
        # - >= 0x1000: low — likely buffer partition, not struct offset
        # - others: medium
        if offset_val >= 0x1000:
            sev = "low"
        elif 4 <= offset_val <= 256:
            sev = "high"
        else:
            sev = "medium"

        line_num = n.start_point[0] + 1
        rule3_lines.add(line_num)

        results.append((
            n, "raw_byte_member_access", sev,
            f"Raw byte-offset member access at offset {offset_txt} — "
            f"*(…*)((char*)expr + {offset_txt}) reads the wrong field on LP64 "
            f"because vtable pointers grow from 4→8 bytes, shifting all member "
            f"offsets. Use direct member access (obj->member) instead."
        ))

    for n in walk(node):
        # ── Rule 1: sizeof(UserType) in I/O call ─────────────────────────
        if n.type == "sizeof_expression":
            type_name = _sizeof_type_name(n)
            if type_name and _is_user_type(type_name):
                in_io, fn_name, is_stream = _is_in_io_call(n)
                if in_io:
                    if is_stream:
                        results.append((
                            n, "sizeof_struct_in_stream", "high",
                            f"sizeof({type_name}) used in {fn_name}() — "
                            f"if {type_name} contains pointers or long members, "
                            f"sizeof() is larger on LP64. Stream will read/write "
                            f"wrong byte count, corrupting all subsequent data. "
                            f"Use fixed-size constants or per-member serialization."
                        ))
                    else:
                        # memcpy/memmove between in-memory objects — sizeof
                        # is correct for the running platform. Only dangerous
                        # if src/dst was populated from a cross-platform file.
                        results.append((
                            n, "sizeof_struct_in_memcpy", "low",
                            f"sizeof({type_name}) used in {fn_name}() — "
                            f"safe for in-memory copies, but verify the data "
                            f"wasn't read from a file with ILP32 struct layout."
                        ))

        # ── Rule 2: Hardcoded hex offset in byte-level pointer arithmetic ──
        #
        # Pattern: (char*)ptr + 0xNN  or  (Type*)((intptr_t)ptr + 0xNN)
        # Only flag hex offsets >= 0x10 to avoid ObjRef chain noise.
        # Skip lines already caught by Rule 3 (higher-confidence dereference pattern).
        if n.type == "binary_expression":
            line_num = n.start_point[0] + 1
            if line_num in rule3_lines:
                continue
            op = n.child_by_field_name("operator")
            if op and node_text(op) in ("+", "-"):
                left = n.child_by_field_name("left")
                right = n.child_by_field_name("right")
                if left and right:
                    ptr_side = None
                    num_side = None
                    if _expr_is_pointer(left):
                        ptr_side, num_side = left, right
                    elif _expr_is_pointer(right):
                        ptr_side, num_side = right, left

                    if ptr_side and num_side:
                        is_offset, offset_txt = _is_hardcoded_offset(num_side)
                        if is_offset:
                            # Must be a hex literal >= 0x10 (filters out small
                            # constants like 4, 8 used for ObjRef/vtable chains)
                            if not (offset_txt.startswith("0x") or offset_txt.startswith("0X")):
                                continue
                            try:
                                val = int(offset_txt, 16)
                            except ValueError:
                                continue
                            if val < 0x10:
                                continue

                            # Must be byte-level pointer arithmetic (char*/void*)
                            ptr_txt = node_text(ptr_side)
                            if any(kw in ptr_txt for kw in
                                   ("char *", "char*", "void *", "void*",
                                    "intptr_t", "uintptr_t", "u8 *", "u8*")):
                                # Large offsets (>= 0x1000) are almost always
                                # flat memory buffer partitions (DSP arrays,
                                # allocation tracking), not struct member offsets.
                                sev = "low" if val >= 0x1000 else "high"
                                results.append((
                                    n, "hardcoded_struct_offset", sev,
                                    f"Hardcoded offset {offset_txt} in byte-level "
                                    f"pointer arithmetic — struct member offsets "
                                    f"change on LP64 due to pointer size (8 vs 4) "
                                    f"and alignment padding. "
                                    f"Use offsetof() or direct member access."
                                ))

    return results


# ── PTRCMP checks ────────────────────────────────────────────────────────────
#
# Detects pointer-vs-integer comparisons using relational operators.
# On ILP32, (int)ptr fits in 32 bits so `ptr > 0` "works" by accident.
# On LP64, this is undefined behavior (comparing pointer to integer)
# or truncates the pointer (if explicitly cast to int first).
#
# Patterns:
#   ptr > 0         — direct pointer compared to integer literal
#   ptr <= 0        — same, any relational operator
#   (int)ptr > X    — pointer cast to int, then compared
#   (unsigned int)moveDir <= 0 — same with unsigned cast

_RELATIONAL_OPS = {">", "<", ">=", "<="}


def _expr_could_be_pointer(node: Node) -> bool:
    """Broader pointer detection for cast-to-int analysis.

    Like _expr_is_pointer but also recognizes:
    - Call expressions with pointer-returning names (Get*, Find*, Create*)
    - Identifiers with pointer-ish names (ending in Dir, Ptr, Obj, etc.)
    - Arrow/dot member access on common pointer member names
    """
    if _expr_is_pointer(node):
        return True

    txt = node_text(node)

    # Identifiers with pointer-ish names
    if node.type == "identifier":
        lower = txt.lower()
        # Common pointer variable names
        if any(lower.endswith(s) for s in ("dir", "ptr", "obj", "mgr", "buf",
                                            "mem", "node", "list", "impl")):
            return True
        # Common pointer prefixes
        if any(lower.startswith(s) for s in ("p_", "pp_")):
            return True

    # Call expressions that likely return pointers
    if node.type == "call_expression":
        fn = node.child_by_field_name("function")
        if fn:
            fn_txt = node_text(fn)
            bare = fn_txt.split("::")[-1].split("->")[-1].split(".")[-1]
            # Strip trailing () for comparison
            bare_name = bare.rstrip("()")
            # Exclude Get* methods that return counts/sizes/indices
            _NON_PTR_SUFFIXES = (
                "Num", "Count", "Size", "Length", "Index", "Idx",
                "Mask", "Frames", "Time", "Score", "Type", "Rev",
                "Id", "Flags", "State", "Mode", "Level",
            )
            if any(bare_name.endswith(s) for s in _NON_PTR_SUFFIXES):
                return False
            # Common pointer-returning method patterns
            if any(bare_name.startswith(p) for p in ("Get", "Find", "Create",
                                                      "Alloc", "New")):
                return True

    # Member access: obj->mSomethingDir, obj.mPtr
    if node.type in ("field_expression",):
        field = node.child_by_field_name("field")
        if field:
            f_txt = node_text(field).lower()
            if any(f_txt.endswith(s) for s in ("dir", "ptr", "obj", "mgr",
                                                "buf", "mem")):
                return True

    return False


def _expr_is_pointer_or_cast_from_pointer(node: Node) -> tuple[bool, bool]:
    """Check if expression is a pointer or a pointer cast to int.

    Returns (is_pointer_related, is_cast_to_int).
    is_cast_to_int=True means (int)ptr or (unsigned int)ptr pattern.
    """
    if node is None:
        return False, False

    # Direct pointer expression: ptr > 0
    if _expr_is_pointer(node):
        return True, False

    # Cast to int-sized type from a pointer-ish expression
    if node.type == "cast_expression":
        cast_type = _get_cast_type(node)
        if _is_int_type(cast_type):
            value = node.child_by_field_name("value")
            if value and _expr_could_be_pointer(value):
                return True, True

    # Parenthesized: unwrap
    if node.type == "parenthesized_expression":
        for child in node.named_children:
            return _expr_is_pointer_or_cast_from_pointer(child)

    return False, False


def _unwrap_parens(node: Node) -> Node:
    """Unwrap parenthesized expressions to get the inner expression."""
    while node and node.type == "parenthesized_expression":
        children = node.named_children
        if children:
            node = children[0]
        else:
            break
    return node


def _is_integer_literal(node: Node) -> bool:
    """Check if a node is a number literal (possibly negated)."""
    node = _unwrap_parens(node)
    if node.type == "number_literal":
        return True
    # Negated literal: -1
    if node.type == "unary_expression":
        op = node.child_by_field_name("operator")
        operand = node.child_by_field_name("operand")
        if op and node_text(op) == "-" and operand:
            return _is_integer_literal(operand)
    return False


def _is_hardcoded_address(node: Node) -> bool:
    """Check if a node is a hardcoded address: (void*)0xNNN or (Type*)0xNNN."""
    node = _unwrap_parens(node)
    if node.type == "cast_expression":
        cast_type = _get_cast_type(node)
        if _is_pointer_type(cast_type):
            value = node.child_by_field_name("value")
            if value:
                return _is_integer_literal(value)
    return False


def check_ptrcmp(node: Node, source: bytes) -> list[tuple]:
    """Check for pointer-vs-integer comparisons using relational operators.

    Only flags when one side is clearly a pointer and the other is clearly
    an integer literal. Skips pointer-to-pointer comparisons (both sides
    are pointers, e.g. loop bounds) since those are valid on LP64.
    """
    results: list[tuple] = []

    for n in walk(node):
        if n.type != "binary_expression":
            continue

        op_node = n.child_by_field_name("operator")
        if op_node is None:
            continue
        op = node_text(op_node)
        if op not in _RELATIONAL_OPS:
            continue

        left = n.child_by_field_name("left")
        right = n.child_by_field_name("right")
        if left is None or right is None:
            continue

        # Case 1: (int)ptr OP literal — pointer cast to int then compared
        # Check both sides for the cast-from-pointer pattern
        left_is_ptr, left_is_cast = _expr_is_pointer_or_cast_from_pointer(left)
        right_is_ptr, right_is_cast = _expr_is_pointer_or_cast_from_pointer(right)

        if left_is_cast:
            results.append((
                n, "ptrcmp_cast_relational", "high",
                f"Pointer cast to int then compared with '{op}' — "
                f"truncates 8-byte pointer to 4 bytes on LP64 before comparison. "
                f"Use intptr_t cast or compare pointer directly (ptr != nullptr)."
            ))
            continue
        if right_is_cast:
            results.append((
                n, "ptrcmp_cast_relational", "high",
                f"Pointer cast to int then compared with '{op}' — "
                f"truncates 8-byte pointer to 4 bytes on LP64 before comparison. "
                f"Use intptr_t cast or compare pointer directly (ptr != nullptr)."
            ))
            continue

        # Case 2: ptr OP integer_literal — direct pointer compared to number
        # Only flag when one side is a recognized pointer and the other is
        # a number literal. This avoids false positives from ptr-to-ptr
        # comparisons where one side is an unrecognized identifier.
        if left_is_ptr and _is_integer_literal(right):
            int_txt = node_text(right).strip()
            results.append((
                n, "ptrcmp_relational", "high",
                f"Pointer compared to integer ({int_txt}) with '{op}' — "
                f"undefined behavior on LP64 (pointer vs integer). "
                f"Use ptr != nullptr or ptr == nullptr instead."
            ))
        elif right_is_ptr and _is_integer_literal(left):
            int_txt = node_text(left).strip()
            results.append((
                n, "ptrcmp_relational", "high",
                f"Pointer compared to integer ({int_txt}) with '{op}' — "
                f"undefined behavior on LP64 (pointer vs integer). "
                f"Use ptr != nullptr or ptr == nullptr instead."
            ))

        # Case 3: expr OP (void*)0xNNN — comparison against hardcoded address
        # Valid on ILP32 but the address is meaningless on LP64.
        elif _is_hardcoded_address(left) or _is_hardcoded_address(right):
            addr_side = left if _is_hardcoded_address(left) else right
            addr_txt = node_text(addr_side).strip()
            results.append((
                n, "ptrcmp_hardcoded_addr", "high",
                f"Comparison against hardcoded address {addr_txt} with '{op}' — "
                f"ILP32 address is meaningless on LP64. "
                f"Guard with #ifdef or use a platform-appropriate check."
            ))

    return results


# ── INTASPTR checks ─────────────────────────────────────────────────────────

# Member prefixes that indicate class/struct fields (not local vars)
_MEMBER_PREFIXES = ("m", "s_", "g", "unk", "pv.")

# Known int-as-pointer field names to suppress (false positives or name collisions)
_INTASPTR_SUPPRESSIONS = {
    "mNonce",       # unsigned char[16] reinterpreted as int for byte swap
    "mData",        # HxGuid: int[4] array; MeshDeform/Cache context-dependent
    "mStart",       # Name collision: CharBones has char* mStart, Timer/MapFile have int mStart
    "mElemDrawState",  # Already has #ifdef HX_NATIVE guard in HamListRibbon.h
}


def _is_member_or_field(txt: str) -> bool:
    """Check if an identifier looks like a class member field."""
    # Direct member access: mFoo, unkFoo, gFoo, s_foo
    if any(txt.startswith(p) for p in ("m", "unk", "g", "s_")):
        return True
    # Struct field access: pv.egParams, state.mFoo
    if "." in txt:
        parts = txt.rsplit(".", 1)
        if len(parts) == 2 and len(parts[1]) > 0:
            return True
    return False


def _collect_int_fields_from_headers(node: Node) -> set[str]:
    """Collect field names declared as int-sized types in struct/class bodies.

    Scans header AST for field_declaration nodes with int-type specifiers
    and extracts the field names. Returns set of field names like
    {'mSourceVoice', 'unk3c', 'mData'}.
    """
    fields: set[str] = set()
    for n in walk(node):
        if n.type != "field_declaration":
            continue
        # Get the type specifier
        type_node = n.child_by_field_name("type")
        if type_node is None:
            continue
        type_txt = node_text(type_node).strip()
        if not _is_int_type(type_txt):
            continue
        # Get the declarator(s) — skip pointer declarators (int *foo is a pointer)
        for child in n.named_children:
            if child.type == "field_identifier":
                fields.add(node_text(child).strip())
            elif child.type == "pointer_declarator":
                continue  # int *field — already a pointer
            elif child.type == "init_declarator":
                decl = child.child_by_field_name("declarator")
                if decl and decl.type == "field_identifier":
                    fields.add(node_text(decl).strip())
    return fields


def check_intasptr(node: Node, source: bytes,
                   global_int_fields: set[str] | None = None,
                   ) -> list[tuple[str, str, str, str]]:
    """Detect int fields cast to pointer types.

    Uses a pre-built set of int-typed field names from headers (global_int_fields)
    plus local header analysis and heuristic fallbacks.

    Catches patterns like:
        int *p = (int *)mSourceVoice;    // mSourceVoice is int, should be pointer
        *(float *)((int *)unk60 + 2)     // unk60 is int, used as pointer

    These work on ILP32 (sizeof(int)==sizeof(void*)==4) but break on LP64
    (sizeof(int)==4, sizeof(void*)==8) — the int truncates the pointer.
    """
    results: list[tuple[str, str, str, str]] = []

    # Collect int fields from this file's own headers (for .h files)
    local_int_fields = _collect_int_fields_from_headers(node)
    all_int_fields = local_int_fields | (global_int_fields or set())

    for n in walk(node):
        if n.type != "cast_expression":
            continue

        cast_type = _get_cast_type(n)
        if not _is_pointer_type(cast_type):
            continue

        # Get the value being cast
        value = n.child_by_field_name("value")
        if value is None:
            continue

        value_txt = node_text(value).strip()

        # Skip if the value is already a pointer expression (legitimate cast)
        if _expr_is_pointer(value):
            continue

        # Skip literals, sizeof, etc.
        if value.type in ("number_literal", "char_literal", "sizeof_expression",
                          "null", "nullptr", "true", "false"):
            continue

        # Skip array subscripts, function calls, complex expressions
        if value.type in ("subscript_expression", "call_expression",
                          "conditional_expression"):
            continue

        # Extract the base field name
        base_name = value_txt
        if "." in value_txt:
            base_name = value_txt.rsplit(".", 1)[-1]

        # Skip known suppressions
        if base_name in _INTASPTR_SUPPRESSIONS:
            continue

        # Check: is this a known int-typed field from headers?
        # Only flag member-like names to avoid false positives from locals
        is_member = _is_member_or_field(value_txt)
        if base_name in all_int_fields and is_member:
            results.append((
                n, "int_field_as_pointer", "high",
                f"Field '{value_txt}' is declared as int but cast to pointer "
                f"type '{cast_type}' — truncates pointer on LP64 "
                f"(int=4 bytes, pointer=8 bytes). "
                f"Declare the field as a pointer type or use intptr_t."
            ))

    return results


# ── Main scanner ─────────────────────────────────────────────────────────────

ALL_CATEGORIES = {"WCHAR", "PTRCAST", "PTRDIFF", "STRUCTIO", "PTRCMP", "INTASPTR"}

CHECKERS = {
    "WCHAR": check_wchar,
    "PTRCAST": check_ptrcast,
    "PTRDIFF": check_ptrdiff,
    "STRUCTIO": check_structio,
    "PTRCMP": check_ptrcmp,
    "INTASPTR": check_intasptr,
}


def _collect_global_int_fields(root_dir: Path) -> set[str]:
    """Pre-scan all header files to collect int-typed field names.

    Returns a set of field names declared as int-sized types in
    struct/class bodies across all headers.
    """
    all_fields: set[str] = set()
    for dirpath, dirnames, filenames in os.walk(root_dir):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for filename in sorted(filenames):
            if not filename.endswith((".h", ".hpp", ".inl")):
                continue
            filepath = Path(dirpath) / filename
            if not should_scan(filepath):
                continue
            try:
                source = filepath.read_bytes()
            except OSError:
                continue
            tree = _PARSER.parse(source)
            all_fields |= _collect_int_fields_from_headers(tree.root_node)
    return all_fields


def scan_file(filepath: Path, categories: set[str], severity_filter: set[str],
              exclude_guarded: bool,
              global_int_fields: set[str] | None = None) -> list[Finding]:
    """Scan a single file using tree-sitter."""
    findings = []

    try:
        source = filepath.read_bytes()
    except OSError:
        return findings

    # Parse with tree-sitter
    tree = _PARSER.parse(source)
    root = tree.root_node

    # Detect HX_NATIVE guard regions
    guarded = detect_guard_regions(source)
    rel_path = str(filepath)

    for cat, checker in CHECKERS.items():
        if cat not in categories:
            continue

        # INTASPTR needs the global int fields set
        if cat == "INTASPTR":
            check_results = checker(root, source, global_int_fields=global_int_fields)
        else:
            check_results = checker(root, source)

        for result_node, rule_name, severity, suggestion in check_results:
            if severity not in severity_filter:
                continue

            is_guarded = is_line_guarded(guarded, result_node.start_byte, source)
            if exclude_guarded and is_guarded:
                continue

            line_num = result_node.start_point[0] + 1
            text = line_text(source, result_node)

            # Dedup
            if any(f.line == line_num and f.rule_name == rule_name for f in findings):
                continue

            findings.append(Finding(
                file=rel_path,
                line=line_num,
                category=cat,
                severity=severity,
                rule_name=rule_name,
                text=text[:140],
                guarded=is_guarded,
                suggestion=suggestion,
            ))

    return findings


def scan_directory(root: Path, categories: set[str], severity_filter: set[str],
                   exclude_guarded: bool) -> list[Finding]:
    all_findings = []
    file_count = 0

    # Pre-collect int-typed fields from all headers for INTASPTR cross-file analysis
    global_int_fields: set[str] | None = None
    if "INTASPTR" in categories:
        global_int_fields = _collect_global_int_fields(root)

    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]

        for filename in sorted(filenames):
            filepath = Path(dirpath) / filename
            if should_scan(filepath):
                findings = scan_file(filepath, categories, severity_filter,
                                     exclude_guarded, global_int_fields)
                all_findings.extend(findings)
                file_count += 1

    print(f"Scanned {file_count} files.", file=sys.stderr)
    return all_findings


def print_findings(findings: list[Finding], as_json: bool = False):
    if as_json:
        print(json.dumps([asdict(f) for f in findings], indent=2))
        return

    if not findings:
        print("No LP64 portability issues found.")
        return

    by_cat: dict[str, list[Finding]] = {}
    for f in findings:
        by_cat.setdefault(f.category, []).append(f)

    total = len(findings)
    unguarded = sum(1 for f in findings if not f.guarded)
    print(f"\n{'='*72}")
    print(f"LP64 PORTABILITY SCAN RESULTS")
    print(f"{'='*72}")
    print(f"Total findings: {total}  (unguarded: {unguarded}, guarded: {total - unguarded})")
    print()

    sev_counts: dict[str, int] = {}
    for f in findings:
        sev_counts[f.severity] = sev_counts.get(f.severity, 0) + 1
    for sev in ["high", "medium", "low"]:
        if sev in sev_counts:
            marker = "!!!" if sev == "high" else ("! " if sev == "medium" else "  ")
            print(f"  {marker} {sev.upper():8s}: {sev_counts[sev]}")
    print()

    for cat in sorted(by_cat.keys()):
        cat_findings = by_cat[cat]
        print(f"{'─'*72}")
        print(f"  {cat} ({len(cat_findings)} findings)")
        print(f"{'─'*72}")

        sev_order = {"high": 0, "medium": 1, "low": 2}
        cat_findings.sort(key=lambda f: (f.guarded, sev_order.get(f.severity, 9), f.file, f.line))

        prev_file = None
        for f in cat_findings:
            if f.file != prev_file:
                print()
                prev_file = f.file

            guard_tag = " [GUARDED]" if f.guarded else ""
            sev_tag = f"[{f.severity.upper()}]"
            print(f"  {sev_tag:8s} {f.file}:{f.line}  [{f.rule_name}]{guard_tag}")
            print(f"           {f.text}")
            if f.suggestion and not f.guarded:
                print(f"           → {f.suggestion}")

    print(f"\n{'─'*72}")
    print("  TOP FILES WITH UNGUARDED ISSUES")
    print(f"{'─'*72}")
    file_counts: dict[str, int] = {}
    for f in findings:
        if not f.guarded:
            file_counts[f.file] = file_counts.get(f.file, 0) + 1
    if file_counts:
        for filepath, count in sorted(file_counts.items(), key=lambda x: -x[1])[:20]:
            print(f"  {count:4d}  {filepath}")
    else:
        print("  (none)")
    print()


# ── libclang-based deep analysis ─────────────────────────────────────────────
#
# When --clang is passed, uses libclang for type-aware analysis.
# This catches patterns that tree-sitter cannot:
#   - Plain identifier `ptr > 0` where `ptr` is typed as a pointer
#   - `(int)expr` where `expr` resolves to a pointer type
#   - Pointer arithmetic stored in int-sized variables
#
# Requires: pip install libclang
#           compile_commands.json from the native CMake build

CLANG_CATEGORIES = {"PTRCMP", "PTRCAST", "PTRDIFF"}


def _clang_available() -> bool:
    try:
        import clang.cindex
        return True
    except ImportError:
        return False


def _get_clang_resource_dir() -> str | None:
    """Get the system clang resource dir for header resolution."""
    import subprocess
    for cmd in ['clang++', 'clang']:
        try:
            return subprocess.check_output(
                [cmd, '-print-resource-dir'], stderr=subprocess.DEVNULL
            ).decode().strip()
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
    return None


def _filter_compile_args(raw_args: list[str], filepath: str) -> list[str]:
    """Filter compile_commands args for libclang parsing."""
    filtered = []
    skip_next = False
    for a in raw_args[1:]:  # skip compiler path
        if skip_next:
            skip_next = False
            continue
        if a in ('-o', '-c'):
            skip_next = True
            continue
        if a == filepath:
            continue
        if a.startswith('--driver-mode'):
            continue
        filtered.append(a)
    # Suppress warnings (we only care about type info)
    filtered.append('-w')
    # Remove HX_NATIVE — we want to analyze PPC code path (where bugs live)
    filtered = [a for a in filtered if a != '-DHX_NATIVE=1']
    return filtered


def _clang_scan_file(
    filepath: str,
    idx,  # clang.cindex.Index
    compdb,  # clang.cindex.CompilationDatabase
    resource_dir: str | None,
    categories: set[str],
    exclude_guarded: bool,
) -> list[Finding]:
    """Scan a single file using libclang for type-aware analysis."""
    from clang.cindex import CursorKind, TypeKind

    findings: list[Finding] = []

    cmds = compdb.getCompileCommands(filepath)
    if not cmds:
        return findings

    args = _filter_compile_args(list(cmds[0].arguments), filepath)
    if resource_dir:
        args.extend(['-resource-dir', resource_dir])

    try:
        tu = idx.parse(filepath, args=args)
    except Exception:
        return findings

    # Read source for guard detection and line text extraction
    try:
        source = Path(filepath).read_bytes()
    except OSError:
        return findings

    guarded = detect_guard_regions(source)
    lines = source.split(b"\n")

    _RELATIONAL = {'>', '<', '>=', '<='}
    # Int-sized types that truncate pointers
    _INT_TYPES_CLANG = {TypeKind.INT, TypeKind.UINT, TypeKind.SHORT,
                        TypeKind.USHORT, TypeKind.LONG, TypeKind.ULONG}
    # Type kinds that indicate parse errors / template misparsing.
    # When we see these, '<' is likely a template bracket, not comparison.
    _BAD_TYPE_KINDS = {TypeKind.DEPENDENT, TypeKind.INVALID,
                       TypeKind.OVERLOAD, TypeKind.UNEXPOSED}

    def _type_is_valid(canon_kind, spelling):
        """Return False if the type indicates a parse artifact."""
        if canon_kind in _BAD_TYPE_KINDS:
            return False
        if spelling in ('<dependent type>', '<bound member function type>',
                        '<overloaded function type>'):
            return False
        return True

    def _get_line_text(line_num: int) -> str:
        if 0 < line_num <= len(lines):
            return lines[line_num - 1].decode("utf-8", errors="replace").strip()
        return ""

    def _is_guarded(line_num: int) -> bool:
        idx_0 = line_num - 1
        if 0 <= idx_0 < len(guarded):
            return guarded[idx_0]
        return False

    def _get_op_from_tokens(cursor) -> str | None:
        """Extract relational operator from cursor tokens."""
        try:
            for t in cursor.get_tokens():
                if t.spelling in _RELATIONAL:
                    return t.spelling
        except Exception:
            pass
        return None

    def _is_integer_literal_cursor(cursor) -> bool:
        """Check if cursor is an integer literal (possibly cast to pointer)."""
        if cursor.kind == CursorKind.INTEGER_LITERAL:
            return True
        # (void*)0xNNN — cast of integer to pointer
        if cursor.kind == CursorKind.CSTYLE_CAST_EXPR:
            children = list(cursor.get_children())
            if children and children[0].kind == CursorKind.INTEGER_LITERAL:
                return True
        # Implicit cast wrapping an integer literal
        if cursor.kind == CursorKind.UNEXPOSED_EXPR:
            children = list(cursor.get_children())
            if children:
                return _is_integer_literal_cursor(children[0])
        return False

    def _walk(cursor):
        loc = cursor.location
        if loc.file and loc.file.name != filepath:
            return

        line_num = loc.line if loc.file else 0

        # ── PTRCMP: pointer vs integer comparisons ──────────────────
        if "PTRCMP" in categories:
            is_binop = cursor.kind == CursorKind.BINARY_OPERATOR
            # Also check UNEXPOSED_EXPR — clang uses this for dependent
            # or error-recovery expressions (e.g., ptr > 0 when headers
            # have parse errors)
            is_unexposed = cursor.kind == CursorKind.UNEXPOSED_EXPR

            if is_binop or is_unexposed:
                children = list(cursor.get_children())
                if len(children) == 2:
                    left, right = children
                    op = _get_op_from_tokens(cursor)
                    if op:
                        left_canon = left.type.get_canonical()
                        right_canon = right.type.get_canonical()

                        # Skip if either side has unresolvable types
                        # (template misparsing: reinterpret_cast<T*> → T* < ...)
                        if (not _type_is_valid(left_canon.kind, left.type.spelling)
                                or not _type_is_valid(right_canon.kind, right.type.spelling)):
                            pass  # skip this node
                        else:
                            left_is_ptr = left_canon.kind == TypeKind.POINTER
                            right_is_ptr = right_canon.kind == TypeKind.POINTER

                            # Case 1: Cast from pointer to int, then compared
                            for side, other in [(left, right), (right, left)]:
                                if side.kind == CursorKind.CSTYLE_CAST_EXPR:
                                    sc = list(side.get_children())
                                    if sc:
                                        inner_canon = sc[0].type.get_canonical()
                                        cast_canon = side.type.get_canonical()
                                        if (inner_canon.kind == TypeKind.POINTER
                                                and cast_canon.kind in _INT_TYPES_CLANG):
                                            inner_type = sc[0].type.spelling
                                            cast_type = side.type.spelling
                                            _emit(findings, filepath, line_num,
                                                  "PTRCMP", "high",
                                                  "clang_ptrcmp_cast", guarded,
                                                  exclude_guarded, lines,
                                                  f"Pointer ({inner_type}) cast to "
                                                  f"{cast_type} then compared with "
                                                  f"'{op}' — truncates 8->4 bytes on "
                                                  f"LP64. Use intptr_t or compare "
                                                  f"pointer directly.")
                                            break  # don't double-report

                            # Case 2: Direct pointer vs non-pointer
                            if (left_is_ptr or right_is_ptr) and not (left_is_ptr and right_is_ptr):
                                ptr_side = left if left_is_ptr else right
                                int_side = right if left_is_ptr else left

                                # Only flag if the integer side is a literal
                                if _is_integer_literal_cursor(int_side):
                                    # Skip template misparsing: reinterpret_cast<T*>
                                    # shows up as T* < ... when clang can't resolve templates
                                    src_line = _get_line_text(line_num)
                                    if ('reinterpret_cast<' in src_line
                                            or 'static_cast<' in src_line
                                            or 'dynamic_cast<' in src_line
                                            or 'const_cast<' in src_line):
                                        pass  # template misparse
                                    else:
                                        ptr_type = ptr_side.type.spelling
                                        _emit(findings, filepath, line_num,
                                              "PTRCMP", "high",
                                              "clang_ptrcmp_direct", guarded,
                                              exclude_guarded, lines,
                                              f"Pointer ({ptr_type}) compared to "
                                              f"integer with '{op}' — undefined "
                                              f"behavior on LP64. Use != nullptr.")

                            # Case 3: Both pointers, but one is a hardcoded address
                            # e.g., mem >= (void*)0xA0000000
                            if left_is_ptr and right_is_ptr:
                                for side in [left, right]:
                                    if side.kind == CursorKind.CSTYLE_CAST_EXPR:
                                        sc = list(side.get_children())
                                        if sc and sc[0].kind == CursorKind.INTEGER_LITERAL:
                                            _emit(findings, filepath, line_num,
                                                  "PTRCMP", "high",
                                                  "clang_ptrcmp_hardcoded_addr",
                                                  guarded, exclude_guarded, lines,
                                                  f"Comparison against hardcoded "
                                                  f"address with '{op}' — ILP32 "
                                                  f"address is meaningless on LP64.")

        # ── PTRCAST: pointer truncation via cast ────────────────────
        if "PTRCAST" in categories:
            if cursor.kind == CursorKind.CSTYLE_CAST_EXPR:
                cast_canon = cursor.type.get_canonical()
                if cast_canon.kind in _INT_TYPES_CLANG:
                    children = list(cursor.get_children())
                    if children:
                        inner_canon = children[0].type.get_canonical()
                        if inner_canon.kind == TypeKind.POINTER:
                            inner_type = children[0].type.spelling
                            cast_type = cursor.type.spelling
                            # Skip if this is inside a PTRCMP comparison
                            # (already reported there)
                            parent = cursor.semantic_parent
                            _emit(findings, filepath, line_num,
                                  "PTRCAST", "high",
                                  "clang_ptr_to_int", guarded,
                                  exclude_guarded, lines,
                                  f"Pointer ({inner_type}) cast to "
                                  f"{cast_type} — truncates from 8 to "
                                  f"4 bytes on LP64. Use intptr_t/uintptr_t.")

        # ── PTRDIFF: pointer subtraction stored in int ──────────────
        if "PTRDIFF" in categories:
            if cursor.kind == CursorKind.VAR_DECL:
                var_canon = cursor.type.get_canonical()
                if var_canon.kind in _INT_TYPES_CLANG:
                    children = list(cursor.get_children())
                    for child in children:
                        if child.kind == CursorKind.BINARY_OPERATOR:
                            op = _get_op_from_tokens(child)
                            if op == '-':
                                gc = list(child.get_children())
                                if len(gc) == 2:
                                    l_canon = gc[0].type.get_canonical()
                                    r_canon = gc[1].type.get_canonical()
                                    if (l_canon.kind == TypeKind.POINTER
                                            and r_canon.kind == TypeKind.POINTER):
                                        _emit(findings, filepath, line_num,
                                              "PTRDIFF", "high",
                                              "clang_ptrdiff_in_int", guarded,
                                              exclude_guarded, lines,
                                              f"Pointer difference stored as "
                                              f"{cursor.type.spelling} — may "
                                              f"overflow on LP64. Use "
                                              f"ptrdiff_t.")

        for child in cursor.get_children():
            _walk(child)

    _walk(tu.cursor)
    return findings


def _emit(
    findings: list[Finding], filepath: str, line_num: int,
    category: str, severity: str, rule_name: str,
    guarded: list[bool], exclude_guarded: bool,
    lines: list[bytes], suggestion: str,
):
    """Add a finding, handling guard checks and dedup."""
    is_guarded = False
    idx_0 = line_num - 1
    if 0 <= idx_0 < len(guarded):
        is_guarded = guarded[idx_0]

    if exclude_guarded and is_guarded:
        return

    # Get line text
    text = ""
    if 0 < line_num <= len(lines):
        text = lines[line_num - 1].decode("utf-8", errors="replace").strip()[:140]

    # Dedup
    if any(f.line == line_num and f.rule_name == rule_name for f in findings):
        return

    findings.append(Finding(
        file=filepath,
        line=line_num,
        category=category,
        severity=severity,
        rule_name=rule_name,
        text=text,
        guarded=is_guarded,
        suggestion=suggestion,
    ))


def _clang_worker_init(compdb_dir: str, resource_dir_val: str | None,
                       categories_val: set[str], exclude_guarded_val: bool):
    """Per-process initializer for multiprocessing pool."""
    import clang.cindex
    global _worker_idx, _worker_compdb, _worker_resource_dir
    global _worker_categories, _worker_exclude_guarded
    _worker_compdb = clang.cindex.CompilationDatabase.fromDirectory(compdb_dir)
    _worker_idx = clang.cindex.Index.create()
    _worker_resource_dir = resource_dir_val
    _worker_categories = categories_val
    _worker_exclude_guarded = exclude_guarded_val


def _clang_worker_scan(filepath: str) -> list[Finding]:
    """Worker function: scan one file using per-process clang state."""
    return _clang_scan_file(
        filepath, _worker_idx, _worker_compdb, _worker_resource_dir,
        _worker_categories, _worker_exclude_guarded,
    )


def clang_scan_directory(
    compdb_dir: str,
    scan_dir: str,
    categories: set[str],
    severity_filter: set[str],
    exclude_guarded: bool,
    jobs: int = 0,
) -> list[Finding]:
    """Scan files using libclang, guided by compile_commands.json.

    Uses multiprocessing for parallel parsing. jobs=0 means auto (cpu_count).
    """
    print(f"Loading compilation database from {compdb_dir}...", file=sys.stderr)
    resource_dir = _get_clang_resource_dir()
    if resource_dir:
        print(f"Using clang resource dir: {resource_dir}", file=sys.stderr)

    # Get all files from compile_commands.json that are under scan_dir
    import json as json_mod
    compdb_path = Path(compdb_dir) / "compile_commands.json"
    with open(compdb_path) as f:
        entries = json_mod.load(f)

    scan_root = str(Path(scan_dir).resolve())
    files = []
    for entry in entries:
        fpath = entry["file"]
        if not fpath.startswith(scan_root):
            continue
        # Apply same skip logic as tree-sitter scanner
        p = Path(fpath)
        parts = set(p.parts)
        if parts & SKIP_DIRS:
            continue
        if p.name in SKIP_FILES:
            continue
        if p.suffix not in SOURCE_EXTS:
            continue
        files.append(fpath)

    # Deduplicate (compile_commands.json can have dupes)
    files = sorted(set(files))
    total = len(files)

    if jobs <= 0:
        jobs = min(os.cpu_count() or 1, total)
    jobs = min(jobs, total)

    print(f"Scanning {total} files with libclang ({jobs} workers)...",
          file=sys.stderr)

    all_findings: list[Finding] = []

    if jobs == 1:
        # Single-process fallback (useful for debugging)
        import clang.cindex
        compdb = clang.cindex.CompilationDatabase.fromDirectory(compdb_dir)
        idx = clang.cindex.Index.create()
        for i, filepath in enumerate(files):
            if (i + 1) % 100 == 0 or i == 0:
                print(f"  [{i+1}/{total}] {Path(filepath).name}...",
                      file=sys.stderr)
            file_findings = _clang_scan_file(
                filepath, idx, compdb, resource_dir,
                categories, exclude_guarded,
            )
            file_findings = [f for f in file_findings
                             if f.severity in severity_filter]
            all_findings.extend(file_findings)
    else:
        with multiprocessing.Pool(
            processes=jobs,
            initializer=_clang_worker_init,
            initargs=(compdb_dir, resource_dir, categories, exclude_guarded),
        ) as pool:
            done = 0
            for file_findings in pool.imap_unordered(_clang_worker_scan,
                                                     files, chunksize=4):
                done += 1
                file_findings = [f for f in file_findings
                                 if f.severity in severity_filter]
                all_findings.extend(file_findings)
                if done % 100 == 0:
                    print(f"  [{done}/{total}] ...", file=sys.stderr)

    print(f"Done. Scanned {total} files.", file=sys.stderr)
    return all_findings


def main():
    parser = argparse.ArgumentParser(
        description="LP64 Portability Scanner (tree-sitter + optional libclang)")
    parser.add_argument("--dir", default="src/",
                        help="Directory to scan (default: src/)")
    parser.add_argument("--severity", default="all",
                        help="Filter: high, medium, low, or all (default: all)")
    parser.add_argument("--category", default="all",
                        help="Comma-separated: WCHAR,PTRCAST,PTRDIFF,STRUCTIO,PTRCMP or all")
    parser.add_argument("--exclude-guarded", action="store_true",
                        help="Exclude findings inside #ifdef HX_NATIVE blocks")
    parser.add_argument("--unguarded-only", action="store_true",
                        help="Shorthand for --exclude-guarded")
    parser.add_argument("--json", action="store_true",
                        help="Output as JSON")
    parser.add_argument("--clang", action="store_true",
                        help="Use libclang for type-aware analysis (PTRCMP, PTRCAST, PTRDIFF)")
    parser.add_argument("--compdb",
                        default="native/build",
                        help="Path to compile_commands.json directory (default: native/build)")
    parser.add_argument("-j", "--jobs", type=int, default=0,
                        help="Number of parallel workers for --clang (default: auto = cpu_count)")

    args = parser.parse_args()

    root = Path(args.dir)
    if not root.is_dir():
        print(f"Error: {root} is not a directory", file=sys.stderr)
        sys.exit(1)

    all_severities = {"high", "medium", "low"}
    severity_filter = (all_severities if args.severity == "all"
                       else set(args.severity.split(",")) & all_severities)

    categories = (ALL_CATEGORIES if args.category == "all"
                  else set(args.category.upper().split(",")) & ALL_CATEGORIES)

    exclude_guarded = args.exclude_guarded or args.unguarded_only

    if args.clang:
        if not _clang_available():
            print("Error: libclang not available. Install with: pip install libclang",
                  file=sys.stderr)
            sys.exit(1)

        clang_cats = categories & CLANG_CATEGORIES
        ts_cats = categories - CLANG_CATEGORIES

        all_findings: list[Finding] = []

        # Run libclang for supported categories
        if clang_cats:
            clang_findings = clang_scan_directory(
                args.compdb, str(root.resolve()),
                clang_cats, severity_filter, exclude_guarded,
                jobs=args.jobs,
            )
            all_findings.extend(clang_findings)

        # Fall back to tree-sitter for unsupported categories (WCHAR, STRUCTIO)
        if ts_cats:
            ts_findings = scan_directory(root, ts_cats, severity_filter, exclude_guarded)
            all_findings.extend(ts_findings)

        print_findings(all_findings, as_json=args.json)
        unguarded_high = sum(1 for f in all_findings if not f.guarded and f.severity == "high")
        sys.exit(1 if unguarded_high > 0 else 0)
    else:
        findings = scan_directory(root, categories, severity_filter, exclude_guarded)
        print_findings(findings, as_json=args.json)

        unguarded_high = sum(1 for f in findings if not f.guarded and f.severity == "high")
        sys.exit(1 if unguarded_high > 0 else 0)


if __name__ == "__main__":
    main()
