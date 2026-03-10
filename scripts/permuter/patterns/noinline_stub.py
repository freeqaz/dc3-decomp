"""Noinline stub detection — mark trivial same-TU callees as __declspec(noinline).

When MSVC inlines a trivial callee (empty body, single return, forwarding call),
the target binary has no `bl` instruction for that call. Our decomp source has
the explicit call, generating a `bl` that doesn't exist in the target. Adding
`__declspec(noinline)` to the callee's definition prevents inlining and can fix
the caller's match.

This pattern:
1. Finds all function calls in the caller
2. Checks if the callee is defined in the same .cpp file
3. If the callee has a trivial body, generates a variant adding __declspec(noinline)

Trivial bodies:
- Empty: {}
- Return-only: { return expr; }
- 1-2 statements (small functions that MSVC commonly inlines)

Detection signals:
- Insert clusters (extra call setup instructions in base)
- Missing bl in target vs base
- Prologue mismatch (inlined code changes register pressure)
"""

from __future__ import annotations

import re
from typing import Iterator
from pathlib import Path

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, find_calls
from ..editor import SourceEditor
from ..extractor import _PARSER, _find_all_function_defs, _get_function_name
from ..header_impact import estimate_header_impact, resolve_included_files
from ..types import AuxiliaryFile, Diagnosis, FunctionContext, Variant

# Max statements in a callee body to consider it "trivial"
_MAX_TRIVIAL_STMTS = 3


class NoinlineStubPattern(Pattern):
    name = "noinline_stub"
    opt_in = True  # No batch wins found; use explicitly via --patterns noinline_stub
    safety_tier = "aggressive"
    structural_domain = "cross_unit"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Any clusters — inserts or deletes indicate structural differences
        # that could be caused by inlining
        if diagnosis.clusters:
            return True

        # Prologue mismatch — inlined code changes callee-saved pressure
        if diagnosis.has_prologue_mismatch:
            return True

        # Diff ops with branch instructions
        for d in diagnosis.diff_ops:
            if "bl" in d.base_opcode or "bl" in d.target_opcode:
                return True

        # Replace mismatches (real, not noise) — structural differences
        if diagnosis.replace_real > 0:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        score = 0.5
        # Strong signal: base has bl that target doesn't
        for d in diagnosis.diff_ops:
            if d.base_opcode == "bl" and d.target_opcode != "bl":
                score = 1.0
                break
        # Prologue mismatch boosts priority
        if diagnosis.has_prologue_mismatch:
            score = min(1.0, score + 0.2)
        return score

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source

        # Parse the full file to find all function definitions
        local_defs = _collect_function_defs(source)
        header_defs = _collect_header_function_defs(ctx.file_path)

        # Find all call expressions in the caller
        callee_names = set()
        for call_node in find_calls(ctx.body_node):
            callee = _extract_callee_name(call_node, source)
            if callee:
                callee_names.add(callee)

        # For each callee defined in the same file with a trivial body,
        # generate a variant adding __declspec(noinline)
        counter = 0
        seen_callees = set()

        for callee_name in sorted(callee_names):
            if counter >= 8:
                break

            # Try both the bare name and qualified variants
            candidates = _resolve_callee_candidates(callee_name, local_defs)
            header_candidates = _resolve_callee_candidates(callee_name, header_defs)

            for resolved_name in candidates:
                if resolved_name in seen_callees:
                    continue
                seen_callees.add(resolved_name)

                func_node, body_node = local_defs[resolved_name]

                # Skip if already has __declspec(noinline)
                func_text = source[func_node.start_byte:func_node.end_byte]
                if b"__declspec(noinline)" in func_text:
                    continue
                # Also check the line before (attribute might be on prior line)
                line_start = func_node.start_byte
                while line_start > 0 and source[line_start - 1:line_start] != b"\n":
                    line_start -= 1
                prefix = source[line_start:func_node.start_byte]
                if b"__declspec(noinline)" in prefix:
                    continue

                # Check if the callee has a trivial body
                stmt_count = _count_statements(body_node)
                if stmt_count > _MAX_TRIVIAL_STMTS:
                    continue

                # Generate variant: add __declspec(noinline) before the function
                ed = SourceEditor(source)
                insert_pos = func_node.start_byte
                ed.insert_at(insert_pos, b"__declspec(noinline) ")

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                body_desc = _describe_body(body_node, source)
                yield Variant(
                    name=f"noinline_{counter}",
                    pattern_name=self.name,
                    description=(
                        f"Add __declspec(noinline) to {resolved_name} "
                        f"({body_desc})"
                    ),
                    source=new_source,
                )
                counter += 1

            for resolved_name in header_candidates:
                if counter >= 8:
                    break
                header_def = header_defs[resolved_name]
                callee_key = f"{header_def.path}:{resolved_name}"
                if callee_key in seen_callees:
                    continue
                seen_callees.add(callee_key)

                if header_def.impact.risk_tier == "high":
                    continue
                if not _is_trivial_header_candidate(
                    header_def.func_node,
                    header_def.body_node,
                    header_def.source,
                ):
                    continue

                try:
                    new_header_source = _apply_noinline_insert(
                        header_def.source,
                        header_def.func_node,
                    )
                except ValueError:
                    continue

                body_desc = _describe_body(header_def.body_node, header_def.source)
                yield Variant(
                    name=f"noinline_{counter}",
                    pattern_name=self.name,
                    description=(
                        f"Add __declspec(noinline) to header {resolved_name} "
                        f"in {header_def.path.name} ({body_desc}, "
                        f"{header_def.impact.risk_tier} risk)"
                    ),
                    source=source,
                    auxiliary_files=(
                        AuxiliaryFile(
                            path=header_def.path,
                            content=new_header_source,
                        ),
                    ),
                )
                counter += 1

        # Strategy 2: Try marking multiple trivial callees at once
        if counter > 1:
            trivial_callees = []
            for callee_name in sorted(callee_names):
                candidates = _resolve_callee_candidates(callee_name, local_defs)
                for resolved_name in candidates:
                    func_node, body_node = local_defs[resolved_name]
                    func_text = source[func_node.start_byte:func_node.end_byte]
                    if b"__declspec(noinline)" in func_text:
                        continue
                    if _count_statements(body_node) <= _MAX_TRIVIAL_STMTS:
                        trivial_callees.append((resolved_name, func_node, body_node))

            if len(trivial_callees) >= 2 and counter < 8:
                ed = SourceEditor(source)
                descs = []
                for name, func_node, body_node in trivial_callees:
                    ed.insert_at(func_node.start_byte, b"__declspec(noinline) ")
                    descs.append(name)

                try:
                    new_source = ed.apply()
                    yield Variant(
                        name=f"noinline_{counter}",
                        pattern_name=self.name,
                        description=(
                            f"Add __declspec(noinline) to {len(descs)} callees: "
                            f"{', '.join(descs)}"
                        ),
                        source=new_source,
                    )
                    counter += 1
                except ValueError:
                    pass


class _FunctionDef:
    def __init__(
        self,
        path: Path,
        source: bytes,
        func_node: Node,
        body_node: Node,
        impact=None,
    ) -> None:
        self.path = path
        self.source = source
        self.func_node = func_node
        self.body_node = body_node
        self.impact = impact


def _collect_function_defs(source: bytes) -> dict[str, tuple[Node, Node]]:
    """Build a name -> (func_node, body_node) map for a parsed file."""
    tree = _PARSER.parse(source)
    all_funcs = _find_all_function_defs(tree.root_node)

    func_map: dict[str, tuple[Node, Node]] = {}
    for func_node in all_funcs:
        name = _get_function_name(func_node)
        if name is None:
            continue
        body = func_node.child_by_field_name("body")
        if body is None:
            continue
        func_map[name] = (func_node, body)
    return func_map


def _collect_header_function_defs(source_path: Path) -> dict[str, _FunctionDef]:
    """Collect conservative direct-header function definitions for noinline."""
    project_root = _project_root_for(source_path)
    header_map: dict[str, _FunctionDef] = {}
    for header_path in resolve_included_files(source_path, project_root):
        if header_path.suffix.lower() not in {".h", ".hh", ".hpp", ".hxx", ".inl"}:
            continue
        try:
            header_source = header_path.read_bytes()
        except OSError:
            continue
        defs = _collect_function_defs(header_source)
        if not defs:
            continue
        impact = estimate_header_impact(project_root, header_path)
        for name, (func_node, body_node) in defs.items():
            header_map.setdefault(
                name,
                _FunctionDef(header_path, header_source, func_node, body_node, impact),
            )
    return header_map


def _project_root_for(path: Path) -> Path:
    """Find a reasonable project root for include resolution."""
    resolved = path.resolve()
    for candidate in (resolved.parent, *resolved.parents):
        if (candidate / ".git").exists():
            return candidate
    return resolved.parent


def _apply_noinline_insert(source: bytes, func_node: Node) -> bytes:
    """Insert __declspec(noinline) before a function definition."""
    ed = SourceEditor(source)
    ed.insert_at(func_node.start_byte, b"__declspec(noinline) ")
    return ed.apply()


def _is_trivial_header_candidate(func_node: Node, body_node: Node, source: bytes) -> bool:
    """Return True for a conservative header function eligible for noinline."""
    if _count_statements(body_node) > _MAX_TRIVIAL_STMTS:
        return False
    if not _looks_inline(func_node, source):
        return False
    func_text = source[func_node.start_byte:func_node.end_byte]
    if b"__declspec(noinline)" in func_text:
        return False
    line_start = func_node.start_byte
    while line_start > 0 and source[line_start - 1:line_start] != b"\n":
        line_start -= 1
    prefix = source[line_start:func_node.start_byte]
    if b"__declspec(noinline)" in prefix:
        return False
    return True


def _looks_inline(func_node: Node, source: bytes) -> bool:
    """Detect obvious inline-style header definitions."""
    signature = source[func_node.start_byte:func_node.end_byte]
    head = signature.split(b"{", 1)[0]
    return (
        b"inline" in head
        or b"__forceinline" in head
        or b"__inline" in head
    )


def _extract_callee_name(call_node: Node, source: bytes) -> str | None:
    """Extract the function name from a call_expression node.

    Handles:
    - Simple calls: Foo(args)
    - Method calls: obj.Method(args)  -> Method
    - Qualified calls: Class::Method(args)  -> Class::Method
    - Scoped calls: obj->Method(args)  -> Method
    """
    func = call_node.child_by_field_name("function")
    if func is None:
        return None

    if func.type == "identifier":
        return func.text.decode("utf-8") if func.text else None

    if func.type == "qualified_identifier":
        return func.text.decode("utf-8") if func.text else None

    if func.type == "field_expression":
        field = func.child_by_field_name("field")
        if field and field.text:
            return field.text.decode("utf-8")

    if func.type == "template_function":
        name_node = func.child_by_field_name("name")
        if name_node and name_node.text:
            return name_node.text.decode("utf-8")

    return None


def _resolve_callee_candidates(
    callee_name: str, func_map: dict[str, tuple[Node, Node]]
) -> list[str]:
    """Resolve a callee name to matching function definitions.

    Handles:
    - Exact match: "Foo" -> "Foo"
    - Unqualified method: "Method" -> "Class::Method" (any class)
    - Qualified: "Class::Method" -> "Class::Method"
    """
    results = []

    # Exact match
    if callee_name in func_map:
        results.append(callee_name)
        return results

    # Unqualified name might match Class::Method
    suffix = "::" + callee_name
    for name in func_map:
        if name.endswith(suffix) or name == callee_name:
            if name not in results:
                results.append(name)

    return results


def _count_statements(body_node: Node) -> int:
    """Count the number of named children (statements) in a compound_statement."""
    return len([c for c in body_node.named_children if c.type != "comment"])


def _describe_body(body_node: Node, source: bytes) -> str:
    """Describe a function body for variant descriptions."""
    stmts = [c for c in body_node.named_children if c.type != "comment"]
    if not stmts:
        return "empty body"
    if len(stmts) == 1:
        stmt = stmts[0]
        if stmt.type == "return_statement":
            ret_text = source[stmt.start_byte:stmt.end_byte].decode(
                "utf-8", errors="replace"
            )
            if len(ret_text) > 50:
                ret_text = ret_text[:47] + "..."
            return ret_text
        return f"1 statement"
    return f"{len(stmts)} statements"
