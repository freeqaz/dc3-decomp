#!/usr/bin/env python3
"""
Type-Punning and Strict Aliasing Scanner for DC3 Decomp

Detects patterns that are undefined behavior under strict aliasing rules,
which can cause corruption on x86_64 native with compiler optimizations.

Categories:
  TYPEPUN     - Float/int type-punning via pointer cast (*(int*)&float_var)
  REINTERPRET - reinterpret_cast to unrelated types (especially STL containers)
  STRTOSYM    - STR_TO_SYM macro usage (Symbol* punning)

Usage:
  python3 scripts/analysis/type_punning_scanner.py [--dir src/] [--severity high|medium|all]
      [--category TYPEPUN,REINTERPRET,STRTOSYM] [--exclude-guarded] [--json]
"""

from __future__ import annotations

import argparse
import json
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
    "rnddx9",       # Xbox 360 DX9 renderer
    "synth_xbox",    # Xbox 360 synth/DSP
}

SKIP_FILES = {
    "types.h", "msvc_compat.h", "types_compat.h", "link_glue.cpp",
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

    ifdef_stack: list[bool] = []
    platform_depth = 0

    for i, line_bytes in enumerate(lines):
        stripped = line_bytes.strip()

        if (stripped.startswith(b"#ifdef HX_NATIVE")
                or stripped.startswith(b"#if defined(HX_NATIVE)")
                or stripped.startswith(b"#ifndef HX_NATIVE")
                or stripped.startswith(b"#if !defined(HX_NATIVE)")):
            ifdef_stack.append(True)
            platform_depth += 1
        elif stripped.startswith((b"#ifdef", b"#ifndef", b"#if ")):
            ifdef_stack.append(False)
        elif stripped.startswith(b"#else") or stripped.startswith(b"#elif"):
            pass
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


# ── AST helpers ──────────────────────────────────────────────────────────────

def _get_cast_type(cast_node: Node) -> str:
    """Extract the type name from a cast_expression's type child."""
    type_desc = cast_node.child_by_field_name("type")
    if type_desc:
        return node_text(type_desc).strip()
    for child in cast_node.children:
        if child.type == "type_descriptor":
            return node_text(child).strip()
    return ""


# Types used in float/int type-punning detection
_FLOAT_PTR_TYPES = {"float *", "float*", "double *", "double*"}
_INT_PTR_TYPES = {
    "int *", "int*",
    "unsigned int *", "unsigned int*",
    "unsigned *", "unsigned*",
    "u32 *", "u32*", "s32 *", "s32*",
    "uint *", "uint*",
    "long *", "long*",
    "unsigned long *", "unsigned long*",
}

# STL container types that are dangerous with reinterpret_cast
_STL_CONTAINERS = {
    "std::map", "std::unordered_map", "std::multimap",
    "std::vector", "std::list", "std::deque",
    "std::set", "std::unordered_set", "std::multiset",
    "std::string", "std::basic_string",
    "std::queue", "std::stack", "std::priority_queue",
}


def _is_float_ptr_type(type_str: str) -> bool:
    """Check if a type string represents a float/double pointer."""
    cleaned = type_str.strip()
    # Handle const: "const float *" -> "float *"
    for prefix in ("const ", "volatile "):
        if cleaned.startswith(prefix):
            cleaned = cleaned[len(prefix):].strip()
    return cleaned in _FLOAT_PTR_TYPES


def _is_int_ptr_type(type_str: str) -> bool:
    """Check if a type string represents an int/unsigned pointer."""
    cleaned = type_str.strip()
    for prefix in ("const ", "volatile "):
        if cleaned.startswith(prefix):
            cleaned = cleaned[len(prefix):].strip()
    return cleaned in _INT_PTR_TYPES


def _is_address_of(node: Node) -> bool:
    """Check if a node is an address-of expression (&x)."""
    if node is None:
        return False
    # Unwrap parens
    inner = node
    while inner.type == "parenthesized_expression" and inner.named_children:
        inner = inner.named_children[0]
    if inner.type == "pointer_expression":
        op = inner.child_by_field_name("operator")
        if op and node_text(op) == "&":
            return True
    return False


def _is_dereference(node: Node) -> bool:
    """Check if a node is a dereference expression (*x)."""
    if node is None:
        return False
    if node.type == "pointer_expression":
        op = node.child_by_field_name("operator")
        if op and node_text(op) == "*":
            return True
    return False


def _has_assignment_context(node: Node) -> bool:
    """Check if a node is on the left side of an assignment (write context)."""
    parent = node.parent
    if parent is None:
        return False
    if parent.type == "assignment_expression":
        left = parent.child_by_field_name("left")
        if left is not None and left.id == node.id:
            return True
    return False


# ── TYPEPUN checks ───────────────────────────────────────────────────────────

def check_typepun(node: Node, source: bytes) -> list[tuple[Node, str, str, str]]:
    """Check for float/int type-punning via pointer cast.

    Detects: *(int*)&float_var, *(float*)&int_var, *(int*)&buf[i]
    where the cast pointer type differs from the source type.
    """
    results: list[tuple[Node, str, str, str]] = []

    for n in walk(node):
        # Pattern: *(CastType*)&expr  or  *(CastType*)(expr)
        # The outer node is a pointer_expression (dereference: *)
        if not _is_dereference(n):
            continue

        argument = n.child_by_field_name("argument")
        if argument is None:
            continue

        # Unwrap parentheses around the cast
        inner = argument
        while inner.type == "parenthesized_expression" and inner.named_children:
            inner = inner.named_children[0]

        # Must be a cast_expression to a pointer type
        if inner.type != "cast_expression":
            continue

        cast_type = _get_cast_type(inner)
        if not cast_type.endswith("*"):
            continue

        # Get the value being cast — should be an address-of (&) expression
        cast_value = inner.child_by_field_name("value")
        if cast_value is None:
            continue

        # The value must involve & (address-of) — either directly or via
        # pointer arithmetic on a buffer (&buf[i], which tree-sitter may
        # parse as just buf + i after & is applied)
        if not _is_address_of(cast_value):
            continue

        # Now we have: *(CastType*)&expr
        # Check if CastType* is int-family and we're punning a float,
        # or CastType* is float-family and we're punning an int.

        is_float_target = _is_float_ptr_type(cast_type)
        is_int_target = _is_int_ptr_type(cast_type)

        if not is_float_target and not is_int_target:
            # Not a float/int punning pattern (e.g. *(Vector3*)&expr is
            # a different kind of reinterpretation, handled elsewhere)
            continue

        # Determine severity based on context
        is_write = _has_assignment_context(n)
        severity = "high" if is_write else "medium"

        target_type = "float" if is_float_target else "int"
        source_type = "int" if is_float_target else "float"

        results.append((
            n, "typepun_ptr_cast", severity,
            f"Type-punning via *({cast_type.strip()})& — reinterprets {source_type} "
            f"bits as {target_type} through pointer cast. This is undefined behavior "
            f"(strict aliasing violation) and may be miscompiled with -O2. "
            f"Use direct assignment (var = 0.0f) or memcpy() for bit-level "
            f"reinterpretation."
        ))

    return results


# ── REINTERPRET checks ──────────────────────────────────────────────────────

def check_reinterpret(node: Node, source: bytes) -> list[tuple[Node, str, str, str]]:
    """Check for reinterpret_cast to unrelated types.

    Detects: reinterpret_cast<std::map<...>*>(&member)->clear()
    and similar patterns where a completely unrelated type is accessed
    through reinterpret_cast.
    """
    results: list[tuple[Node, str, str, str]] = []

    for n in walk(node):
        # tree-sitter parses reinterpret_cast<T>(expr) as a
        # template_function with name "reinterpret_cast"
        # Look for call expressions or field expressions involving reinterpret_cast
        txt = node_text(n)

        # We need to find reinterpret_cast in the text. tree-sitter may
        # parse this differently depending on the template arguments.
        # Use a combined AST + text approach.

        # Strategy: Look for nodes whose text starts with "reinterpret_cast<"
        if n.type not in ("call_expression", "template_function",
                          "field_expression", "parenthesized_expression"):
            # Also check raw text for reinterpret_cast in expression statements
            if n.type == "expression_statement":
                pass  # will check text below
            else:
                continue

        if "reinterpret_cast<" not in txt:
            continue

        # Skip the macro DEFINITION of STR_TO_SYM (in Symbol.h)
        full_line = line_text(source, n)
        if full_line.startswith("#define"):
            continue

        # Check if the target type contains an STL container
        # Extract the type from reinterpret_cast<TYPE>
        m = re.search(r'reinterpret_cast\s*<\s*(.+?)\s*>\s*\(', txt)
        if not m:
            continue

        target_type = m.group(1)

        # Check for STL containers
        has_stl = any(stl in target_type for stl in _STL_CONTAINERS)

        if has_stl:
            # CRITICAL severity for STL container reinterpretation
            stl_name = next(stl for stl in _STL_CONTAINERS if stl in target_type)
            results.append((
                n, "reinterpret_stl_container", "critical",
                f"reinterpret_cast to {stl_name} container type — "
                f"completely undefined behavior. STL containers have "
                f"implementation-defined layout (vtables, allocators, "
                f"internal pointers). This WILL corrupt memory on native. "
                f"Use proper type declarations or a union."
            ))
        elif target_type.endswith("*") and not target_type.startswith("char"):
            # HIGH severity for reinterpret_cast to unrelated class pointer
            # with method call (->method() or .method())
            # Check if the reinterpret_cast result is used with -> or .
            parent = n.parent
            if parent and parent.type == "field_expression":
                results.append((
                    n, "reinterpret_unrelated_class", "high",
                    f"reinterpret_cast to '{target_type}' with method call — "
                    f"accessing methods through reinterpret_cast to an unrelated "
                    f"type is undefined behavior (strict aliasing violation). "
                    f"Use proper type declarations or a union."
                ))

    return results


# ── STRTOSYM checks ─────────────────────────────────────────────────────────

def check_strtosym(node: Node, source: bytes) -> list[tuple[Node, str, str, str]]:
    """Check for STR_TO_SYM macro usage.

    STR_TO_SYM is defined as:
      *reinterpret_cast<Symbol*>(const_cast<char**>(&str))

    This is technically UB (treats char** as Symbol*) but works in practice
    due to Symbol's layout (single char* member). Flag at MEDIUM severity.

    Does NOT flag the macro definition itself (in Symbol.h).
    """
    results: list[tuple[Node, str, str, str]] = []

    # Use line-by-line text search since STR_TO_SYM is a macro that
    # tree-sitter will see as its expansion, not the macro name.
    # We need to search the raw source text.
    lines = source.split(b"\n")

    for i, line_bytes in enumerate(lines):
        line_str = line_bytes.decode("utf-8", errors="replace")
        stripped = line_str.strip()

        # Skip the macro definition line
        if stripped.startswith("#define") and "STR_TO_SYM" in stripped:
            continue

        # Find all STR_TO_SYM occurrences on this line
        idx = 0
        while True:
            pos = line_str.find("STR_TO_SYM", idx)
            if pos == -1:
                break

            # Verify it's a macro call (followed by '(')
            rest = line_str[pos + len("STR_TO_SYM"):].lstrip()
            if not rest.startswith("("):
                idx = pos + 1
                continue

            # Create a synthetic node-like finding using the byte offset
            byte_offset = sum(len(l) + 1 for l in lines[:i]) + pos

            # We need a real AST node for the finding. Find the nearest
            # node at this position.
            target_line = i
            target_col = pos

            # Walk the AST to find a node at approximately this position
            found_node = _find_node_at(node, target_line, target_col)
            if found_node is None:
                # Fallback: use root node but with correct position info
                found_node = node

            results.append((
                found_node, "str_to_sym_usage", "medium",
                "STR_TO_SYM macro — reinterprets char** as Symbol* via "
                "reinterpret_cast. Technically undefined behavior (strict "
                "aliasing violation), though it works in practice due to "
                "Symbol's layout (single char* member). Consider using "
                "Symbol(str) constructor instead."
            ))

            idx = pos + 1

    return results


def _find_node_at(root: Node, target_line: int, target_col: int) -> Node | None:
    """Find the most specific AST node containing the given position."""
    best = None
    for n in walk(root):
        if (n.start_point[0] <= target_line <= n.end_point[0]):
            if n.start_point[0] == target_line:
                if n.start_point[1] <= target_col:
                    best = n
            elif n.start_point[0] < target_line:
                best = n
    return best


# ── Main scanner ─────────────────────────────────────────────────────────────

ALL_CATEGORIES = {"TYPEPUN", "REINTERPRET", "STRTOSYM"}

CHECKERS: dict[str, callable] = {
    "TYPEPUN": check_typepun,
    "REINTERPRET": check_reinterpret,
    "STRTOSYM": check_strtosym,
}

# Severity includes "critical" for this scanner
ALL_SEVERITIES = {"critical", "high", "medium", "low"}


def scan_file(filepath: Path, categories: set[str], severity_filter: set[str],
              exclude_guarded: bool) -> list[Finding]:
    """Scan a single file using tree-sitter."""
    findings = []

    try:
        source = filepath.read_bytes()
    except OSError:
        return findings

    tree = _PARSER.parse(source)
    root = tree.root_node

    guarded = detect_guard_regions(source)
    rel_path = str(filepath)

    for cat, checker in CHECKERS.items():
        if cat not in categories:
            continue

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


def scan_directories(roots: list[Path], categories: set[str],
                     severity_filter: set[str],
                     exclude_guarded: bool) -> list[Finding]:
    all_findings = []
    file_count = 0

    for root in roots:
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]

            for filename in sorted(filenames):
                filepath = Path(dirpath) / filename
                if should_scan(filepath):
                    findings = scan_file(filepath, categories, severity_filter,
                                         exclude_guarded)
                    all_findings.extend(findings)
                    file_count += 1

    print(f"Scanned {file_count} files.", file=sys.stderr)
    return all_findings


def print_findings(findings: list[Finding], as_json: bool = False):
    if as_json:
        print(json.dumps([asdict(f) for f in findings], indent=2))
        return

    if not findings:
        print("No type-punning / strict aliasing issues found.")
        return

    by_cat: dict[str, list[Finding]] = {}
    for f in findings:
        by_cat.setdefault(f.category, []).append(f)

    total = len(findings)
    unguarded = sum(1 for f in findings if not f.guarded)
    print(f"\n{'='*72}")
    print(f"TYPE-PUNNING / STRICT ALIASING SCAN RESULTS")
    print(f"{'='*72}")
    print(f"Total findings: {total}  (unguarded: {unguarded}, guarded: {total - unguarded})")
    print()

    sev_counts: dict[str, int] = {}
    for f in findings:
        sev_counts[f.severity] = sev_counts.get(f.severity, 0) + 1
    for sev in ["critical", "high", "medium", "low"]:
        if sev in sev_counts:
            if sev == "critical":
                marker = "***"
            elif sev == "high":
                marker = "!!!"
            elif sev == "medium":
                marker = "! "
            else:
                marker = "  "
            print(f"  {marker} {sev.upper():10s}: {sev_counts[sev]}")
    print()

    for cat in sorted(by_cat.keys()):
        cat_findings = by_cat[cat]
        print(f"{'─'*72}")
        print(f"  {cat} ({len(cat_findings)} findings)")
        print(f"{'─'*72}")

        sev_order = {"critical": 0, "high": 1, "medium": 2, "low": 3}
        cat_findings.sort(key=lambda f: (f.guarded, sev_order.get(f.severity, 9), f.file, f.line))

        prev_file = None
        for f in cat_findings:
            if f.file != prev_file:
                print()
                prev_file = f.file

            guard_tag = " [GUARDED]" if f.guarded else ""
            sev_tag = f"[{f.severity.upper()}]"
            print(f"  {sev_tag:10s} {f.file}:{f.line}  [{f.rule_name}]{guard_tag}")
            print(f"             {f.text}")
            if f.suggestion and not f.guarded:
                print(f"             -> {f.suggestion}")

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


def main():
    parser = argparse.ArgumentParser(
        description="Type-Punning and Strict Aliasing Scanner (tree-sitter)")
    parser.add_argument("--dir", action="append", default=None,
                        help="Directory to scan (may be specified multiple times; default: src/)")
    parser.add_argument("--severity", default="all",
                        help="Filter: critical, high, medium, low, or all (default: all)")
    parser.add_argument("--category", default="all",
                        help="Comma-separated: TYPEPUN,REINTERPRET,STRTOSYM or all")
    parser.add_argument("--exclude-guarded", action="store_true",
                        help="Exclude findings inside #ifdef HX_NATIVE blocks")
    parser.add_argument("--unguarded-only", action="store_true",
                        help="Shorthand for --exclude-guarded")
    parser.add_argument("--json", action="store_true",
                        help="Output as JSON")

    args = parser.parse_args()

    dirs = args.dir if args.dir else ["src/"]
    roots = []
    for d in dirs:
        root = Path(d)
        if not root.is_dir():
            print(f"Error: {root} is not a directory", file=sys.stderr)
            sys.exit(1)
        roots.append(root)

    severity_filter = (ALL_SEVERITIES if args.severity == "all"
                       else set(args.severity.split(",")) & ALL_SEVERITIES)

    categories = (ALL_CATEGORIES if args.category == "all"
                  else set(args.category.upper().split(",")) & ALL_CATEGORIES)

    exclude_guarded = args.exclude_guarded or args.unguarded_only

    findings = scan_directories(roots, categories, severity_filter, exclude_guarded)
    print_findings(findings, as_json=args.json)

    unguarded_critical_or_high = sum(
        1 for f in findings
        if not f.guarded and f.severity in ("critical", "high")
    )
    sys.exit(1 if unguarded_critical_or_high > 0 else 0)


if __name__ == "__main__":
    main()
