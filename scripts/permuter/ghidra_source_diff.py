"""Ghidra vs source structural diff — semantic comparison beyond instruction matching.

Compares Ghidra's decompilation of the target binary against our C++ source to
find structural differences: missing/extra calls, different null guards, control
flow divergence.

This provides higher-level diagnostic information than objdiff's instruction-level
diff, helping identify *what* needs to change rather than just *where* instructions
differ.

Usage:
    from scripts.permuter.ghidra_source_diff import diff_ghidra_vs_source

    result = diff_ghidra_vs_source(ghidra_code, source_bytes, body_node)
    print(format_source_diff(result))
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Optional

from tree_sitter import Node

from .ghidra_ast import (
    GhidraAST,
    parse_ghidra,
    extract_control_flow_skeleton,
    _walk_all,
)
from .ast_queries import walk, find_calls, node_text


@dataclass
class CallDiff:
    """A function call present in one side but not the other."""
    name: str
    side: str  # "ghidra_only" or "source_only"
    count_ghidra: int = 0
    count_source: int = 0


@dataclass
class GuardDiff:
    """A null guard present in one side but not the other."""
    variable: str
    side: str  # "ghidra_only" or "source_only"


@dataclass
class ControlFlowDiff:
    """A difference in control flow structure."""
    description: str
    ghidra_skeleton: list[str]
    source_skeleton: list[str]


@dataclass
class SourceDiff:
    """Complete structural diff between Ghidra decompilation and source."""
    missing_calls: list[CallDiff] = field(default_factory=list)
    extra_calls: list[CallDiff] = field(default_factory=list)
    guard_diffs: list[GuardDiff] = field(default_factory=list)
    control_flow_diff: Optional[ControlFlowDiff] = None
    ghidra_call_count: int = 0
    source_call_count: int = 0


# Keywords that look like calls but aren't
_NOT_CALLS = frozenset({
    "if", "while", "for", "switch", "return", "sizeof", "typeof", "do",
    "int", "long", "short", "char", "void", "float", "double",
    "uint", "ulong", "ushort", "uchar", "undefined", "undefined4",
    "undefined8", "undefined2", "undefined1", "bool", "byte",
    "CONCAT44", "CONCAT22", "CONCAT11", "ZEXT48", "SEXT48",
    "SUB41", "SUB42", "SUB84",
})

# Ghidra compiler helpers to skip
_HELPER_RE = re.compile(
    r"^(?:__savegprlr_\d+|__restgprlr_\d+|__savefpr_\d+|__restfpr_\d+|"
    r"CONCAT\d+|ZEXT\d+|SEXT\d+|SUB\d+|__ctr\w*)$"
)

# Source-side macros/builtins to normalize
_SOURCE_MACROS = frozenset({
    "MILO_ASSERT", "MILO_WARN", "MILO_FAIL", "MILO_LOG",
    "MILO_NOTIFY", "MILO_ASSERT_FMT",
})


def diff_ghidra_vs_source(
    ghidra_code: str,
    source_bytes: bytes,
    body_node: Node,
) -> SourceDiff:
    """Compare Ghidra decompilation against our C++ source.

    Args:
        ghidra_code: Raw Ghidra decompilation text
        source_bytes: Full source file bytes
        body_node: tree-sitter body node of our function

    Returns:
        SourceDiff with all structural differences
    """
    result = SourceDiff()

    # Parse Ghidra output
    ghidra_ast = parse_ghidra(ghidra_code)

    # 1. Call diff
    ghidra_calls = _extract_ghidra_calls(ghidra_ast)
    source_calls = _extract_source_calls(body_node, source_bytes)

    result.ghidra_call_count = sum(ghidra_calls.values())
    result.source_call_count = sum(source_calls.values())

    # Normalize names for comparison
    ghidra_norm = _normalize_call_counts(ghidra_calls)
    source_norm = _normalize_call_counts(source_calls)

    all_names = set(ghidra_norm.keys()) | set(source_norm.keys())
    for name in sorted(all_names):
        gc = ghidra_norm.get(name, 0)
        sc = source_norm.get(name, 0)
        if gc > sc:
            result.missing_calls.append(CallDiff(
                name=name, side="ghidra_only",
                count_ghidra=gc, count_source=sc,
            ))
        elif sc > gc:
            result.extra_calls.append(CallDiff(
                name=name, side="source_only",
                count_ghidra=gc, count_source=sc,
            ))

    # 2. Null guard diff
    ghidra_guards = _extract_ghidra_guards(ghidra_ast)
    source_guards = _extract_source_guards(body_node, source_bytes)

    for var in sorted(ghidra_guards - source_guards):
        result.guard_diffs.append(GuardDiff(variable=var, side="ghidra_only"))
    for var in sorted(source_guards - ghidra_guards):
        result.guard_diffs.append(GuardDiff(variable=var, side="source_only"))

    # 3. Control flow skeleton diff
    ghidra_cf = extract_control_flow_skeleton(ghidra_ast)
    source_cf = _extract_source_control_flow(body_node)

    if ghidra_cf != source_cf:
        result.control_flow_diff = ControlFlowDiff(
            description=_describe_cf_diff(ghidra_cf, source_cf),
            ghidra_skeleton=ghidra_cf,
            source_skeleton=source_cf,
        )

    return result


def format_source_diff(diff: SourceDiff) -> str:
    """Format a SourceDiff as a human-readable report."""
    lines = []

    lines.append(f"Calls: Ghidra={diff.ghidra_call_count}, Source={diff.source_call_count}")

    if diff.missing_calls:
        lines.append("Missing calls (in target, not in source):")
        for c in diff.missing_calls:
            lines.append(f"  - {c.name} (ghidra={c.count_ghidra}, source={c.count_source})")

    if diff.extra_calls:
        lines.append("Extra calls (in source, not in target):")
        for c in diff.extra_calls:
            lines.append(f"  - {c.name} (ghidra={c.count_ghidra}, source={c.count_source})")

    if diff.guard_diffs:
        lines.append("Guard differences:")
        for g in diff.guard_diffs:
            side = "target only" if g.side == "ghidra_only" else "source only"
            lines.append(f"  - {g.variable}: {side}")

    if diff.control_flow_diff:
        lines.append(f"Control flow: {diff.control_flow_diff.description}")
        g_cf = " ".join(diff.control_flow_diff.ghidra_skeleton[:20])
        s_cf = " ".join(diff.control_flow_diff.source_skeleton[:20])
        lines.append(f"  Ghidra: {g_cf}")
        lines.append(f"  Source: {s_cf}")

    if not diff.missing_calls and not diff.extra_calls and \
       not diff.guard_diffs and not diff.control_flow_diff:
        lines.append("No structural differences found")

    return "\n".join(lines)


# -- Call extraction --------------------------------------------------------

def _extract_ghidra_calls(ast: GhidraAST) -> dict[str, int]:
    """Extract function call names and counts from Ghidra AST."""
    counts: dict[str, int] = {}
    if ast.body_node is None:
        return counts

    code_bytes = ast.code.encode("utf-8")
    for node in _walk_all(ast.body_node):
        if node.type != "call_expression":
            continue
        func = node.child_by_field_name("function")
        if func is None:
            continue
        name = code_bytes[func.start_byte:func.end_byte].decode("utf-8", errors="replace")
        if name in _NOT_CALLS or _HELPER_RE.match(name):
            continue
        counts[name] = counts.get(name, 0) + 1

    return counts


def _extract_source_calls(body_node: Node, source: bytes) -> dict[str, int]:
    """Extract function call names and counts from our source AST."""
    counts: dict[str, int] = {}
    for call in find_calls(body_node):
        func = call.child_by_field_name("function")
        if func is None:
            continue
        name = source[func.start_byte:func.end_byte].decode("utf-8", errors="replace")
        if name in _SOURCE_MACROS:
            name = "Fail"  # MILO_ASSERT etc. expand to Fail() at runtime
        counts[name] = counts.get(name, 0) + 1

    return counts


def _normalize_call_counts(calls: dict[str, int]) -> dict[str, int]:
    """Normalize call names for comparison.

    - Strip namespaces/qualifiers: std::fabs -> fabs, this->Method -> Method
    - Strip Ghidra demangling artifacts
    """
    result: dict[str, int] = {}
    for name, count in calls.items():
        # Extract last component after :: -> .
        parts = re.split(r'::|->|\.', name)
        normalized = parts[-1].strip()
        if not normalized or normalized in _NOT_CALLS:
            continue
        result[normalized] = result.get(normalized, 0) + count
    return result


# -- Guard extraction -------------------------------------------------------

def _extract_ghidra_guards(ast: GhidraAST) -> set[str]:
    """Extract variables null-checked in Ghidra decompilation."""
    guards: set[str] = set()
    if ast.body_node is None:
        return guards

    code_bytes = ast.code.encode("utf-8")
    for node in _walk_all(ast.body_node):
        if node.type != "if_statement":
            continue
        condition = node.child_by_field_name("condition")
        if condition is None:
            continue
        _collect_guard_names(condition, code_bytes, guards)

    return guards


def _extract_source_guards(body_node: Node, source: bytes) -> set[str]:
    """Extract variables null-checked in our source."""
    guards: set[str] = set()
    for node in walk(body_node):
        if node.type != "if_statement":
            continue
        condition = node.child_by_field_name("condition")
        if condition is None:
            continue
        _collect_guard_names(condition, source, guards)

    return guards


def _collect_guard_names(condition: Node, code: bytes, guards: set[str]) -> None:
    """Collect variable names used as null checks in a condition."""
    # Unwrap parenthesized_expression / condition_clause
    inner = condition
    while inner.type in ("condition_clause", "parenthesized_expression"):
        children = [c for c in inner.named_children if c.type != "comment"]
        if len(children) == 1:
            inner = children[0]
        else:
            break

    # if (ptr)
    if inner.type == "identifier":
        name = code[inner.start_byte:inner.end_byte].decode("utf-8", errors="replace")
        guards.add(name)

    # if (ptr != 0) / if (ptr != nullptr) / if (ptr != (TYPE*)0x0)
    elif inner.type == "binary_expression":
        op = inner.child_by_field_name("operator")
        if op and op.text in (b"!=", b"=="):
            left = inner.child_by_field_name("left")
            right = inner.child_by_field_name("right")
            if left and right:
                r_text = code[right.start_byte:right.end_byte].strip()
                if _is_null_value(r_text):
                    if left.type == "identifier":
                        guards.add(code[left.start_byte:left.end_byte].decode("utf-8", errors="replace"))
                l_text = code[left.start_byte:left.end_byte].strip()
                if _is_null_value(l_text):
                    if right.type == "identifier":
                        guards.add(code[right.start_byte:right.end_byte].decode("utf-8", errors="replace"))

        # if (A && B) — left operand as guard
        if op and op.text == b"&&":
            left = inner.child_by_field_name("left")
            if left and left.type == "identifier":
                guards.add(code[left.start_byte:left.end_byte].decode("utf-8", errors="replace"))


def _is_null_value(text: bytes) -> bool:
    """Check if text represents a null/zero value."""
    stripped = text.strip()
    if stripped in (b"0", b"0x0", b"0x00", b"NULL", b"nullptr"):
        return True
    # Ghidra cast: (TYPE *)0x0
    if stripped.startswith(b"(") and b"0x0" in stripped:
        return True
    return False


# -- Control flow extraction ------------------------------------------------

def _extract_source_control_flow(body_node: Node) -> list[str]:
    """Extract control flow skeleton from our source AST."""
    result: list[str] = []
    for child in body_node.named_children:
        _walk_cf(child, result)
    return result


def _walk_cf(node: Node, result: list[str]) -> None:
    """Walk source AST collecting control flow nodes."""
    if node.type == "if_statement":
        result.append("if")
        alt = node.child_by_field_name("alternative")
        if alt:
            result.append("else")
    elif node.type == "for_statement":
        result.append("for")
    elif node.type == "while_statement":
        result.append("while")
    elif node.type == "do_statement":
        result.append("do_while")
    elif node.type == "switch_statement":
        result.append("switch")
    elif node.type == "return_statement":
        result.append("return")

    # Recurse into compound statements and other containers
    for child in node.named_children:
        _walk_cf(child, result)


def _describe_cf_diff(ghidra_cf: list[str], source_cf: list[str]) -> str:
    """Generate a brief description of control flow differences."""
    g_counts = {}
    s_counts = {}
    for item in ghidra_cf:
        g_counts[item] = g_counts.get(item, 0) + 1
    for item in source_cf:
        s_counts[item] = s_counts.get(item, 0) + 1

    diffs = []
    all_types = set(g_counts.keys()) | set(s_counts.keys())
    for t in sorted(all_types):
        gc = g_counts.get(t, 0)
        sc = s_counts.get(t, 0)
        if gc != sc:
            diffs.append(f"{t}: ghidra={gc} source={sc}")

    if diffs:
        return "; ".join(diffs)

    # Same counts but different order
    return "same structure types, different order"
