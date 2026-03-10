"""Discover header-inline tail-call reorder variants for a caller."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from tree_sitter import Node

from .control_flow import is_bare_return_statement, noncomment_named_children, trailing_run
from .editor import SourceEditor
from .extractor import _PARSER, _find_all_function_defs, _get_function_name, extract_function
from .header_impact import estimate_header_impact, resolve_included_files, HeaderImpact
from .patterns.tail_call_reorder import _are_independent_calls, _is_call_statement
from .statement_effects import StatementEffectAnalyzer
from .types import AuxiliaryFile, Variant


@dataclass(frozen=True)
class HeaderTailCallVariant:
    """A generated header-inline tail-call reorder candidate."""

    variant: Variant
    impact: HeaderImpact
    header_function: str
    header_path: Path


@dataclass(frozen=True)
class _HeaderFunction:
    path: Path
    source: bytes
    name: str
    func_node: Node
    body_node: Node


def discover_header_tail_call_variants(
    source_path: Path,
    function_name: str,
    max_variants: int = 8,
) -> list[HeaderTailCallVariant]:
    """Find header-inline call-order variants relevant to the given caller."""
    ctx = extract_function(source_path, function_name)
    called_names = _called_function_names(ctx.body_node, ctx.file_source)
    if not called_names:
        return []

    project_root = _project_root_for(source_path)
    header_defs = _collect_header_functions(source_path, project_root)
    analyzer_cache: dict[Path, StatementEffectAnalyzer] = {}
    variants: list[HeaderTailCallVariant] = []
    seen_updates: set[tuple[Path, bytes]] = set()

    for called_name in sorted(called_names):
        for header_func in _resolve_header_candidates(called_name, header_defs):
            if len(variants) >= max_variants:
                return variants
            if not _looks_inline(header_func.func_node, header_func.source):
                continue

            analyzer = analyzer_cache.setdefault(
                header_func.path,
                StatementEffectAnalyzer(header_func.source),
            )
            impact = estimate_header_impact(project_root, header_func.path)

            for variant in _variants_for_header_function(
                header_func,
                analyzer,
                primary_source=ctx.file_source,
            ):
                key = (header_func.path.resolve(), variant.auxiliary_files[0].content)
                if key in seen_updates:
                    continue
                seen_updates.add(key)
                variants.append(
                    HeaderTailCallVariant(
                        variant=variant,
                        impact=impact,
                        header_function=header_func.name,
                        header_path=header_func.path,
                    )
                )
                if len(variants) >= max_variants:
                    return variants

    return variants


def _collect_header_functions(
    source_path: Path,
    project_root: Path,
) -> dict[str, list[_HeaderFunction]]:
    """Parse directly included headers and collect inline-capable functions."""
    header_defs: dict[str, list[_HeaderFunction]] = {}
    for header_path in resolve_included_files(source_path, project_root):
        if header_path.suffix.lower() not in {".h", ".hh", ".hpp", ".hxx", ".inl"}:
            continue
        try:
            source = header_path.read_bytes()
        except OSError:
            continue
        tree = _PARSER.parse(source)
        for func_node in _find_all_function_defs(tree.root_node):
            name = _get_function_name(func_node)
            if not name:
                continue
            body = func_node.child_by_field_name("body")
            if body is None:
                continue
            header_defs.setdefault(name, []).append(
                _HeaderFunction(
                    path=header_path.resolve(),
                    source=source,
                    name=name,
                    func_node=func_node,
                    body_node=body,
                )
            )
    return header_defs


def _called_function_names(body_node: Node, source: bytes) -> set[str]:
    """Collect bare or qualified call names referenced by the caller."""
    names: set[str] = set()
    for node in body_node.named_children:
        for inner in node.named_children:
            _ = inner
    for node in _walk(body_node):
        if node.type != "call_expression":
            continue
        func = node.child_by_field_name("function")
        if func is None:
            continue
        text = source[func.start_byte:func.end_byte].decode("utf-8", errors="replace").strip()
        if not text:
            continue
        names.add(text)
        for sep in ("::", "->", "."):
            if sep in text:
                names.add(text.rsplit(sep, 1)[-1])
    return names


def _walk(node: Node):
    yield node
    for child in node.children:
        yield from _walk(child)


def _resolve_header_candidates(
    called_name: str,
    header_defs: dict[str, list[_HeaderFunction]],
) -> list[_HeaderFunction]:
    """Resolve a caller-side function name to direct-header definitions."""
    matches: list[_HeaderFunction] = []
    seen: set[tuple[Path, str]] = set()

    def _add(items: list[_HeaderFunction]) -> None:
        for item in items:
            key = (item.path, item.name)
            if key in seen:
                continue
            seen.add(key)
            matches.append(item)

    if called_name in header_defs:
        _add(header_defs[called_name])

    suffix = "::" + called_name
    for name, items in header_defs.items():
        if name.endswith(suffix):
            _add(items)

    return matches


def _variants_for_header_function(
    header_func: _HeaderFunction,
    analyzer: StatementEffectAnalyzer,
    primary_source: bytes,
):
    """Generate safe tail-call reorder swaps for one inline header function."""
    children = noncomment_named_children(header_func.body_node)
    call_runs: list[list[Node]] = []

    trailing_calls = trailing_run(children, _is_call_statement)
    if len(trailing_calls) >= 2:
        call_runs.append(trailing_calls)

    if len(children) >= 3 and is_bare_return_statement(children[-1], header_func.source):
        before_return = trailing_run(children[:-1], _is_call_statement)
        if len(before_return) >= 2:
            call_runs.append(before_return)

    counter = 0
    for call_run in call_runs:
        for i in range(len(call_run) - 1, 0, -1):
            if counter >= 4:
                return
            a = call_run[i - 1]
            b = call_run[i]
            if not _are_independent_calls(a, b, header_func.source, analyzer):
                continue
            ed = SourceEditor(header_func.source)
            _swap_statement_ranges(ed, header_func.source, a, b)
            try:
                new_header_source = ed.apply()
            except ValueError:
                continue
            a_name = _call_name(a, header_func.source)
            b_name = _call_name(b, header_func.source)
            yield Variant(
                name=f"header_tailcall_{counter}",
                pattern_name="header_tail_call",
                description=(
                    f"Swap {a_name}() and {b_name}() in header {header_func.name} "
                    f"for tail-call reorder"
                ),
                source=primary_source,
                auxiliary_files=(
                    AuxiliaryFile(path=header_func.path, content=new_header_source),
                ),
                tags=frozenset({"reordered_tail_calls"}),
            )
            counter += 1


def _swap_statement_ranges(ed: SourceEditor, source: bytes, a: Node, b: Node) -> None:
    """Swap two statement line ranges in a header source buffer."""
    a_start = _line_start(source, a.start_byte)
    a_end = _line_end(source, a.end_byte)
    b_start = _line_start(source, b.start_byte)
    b_end = _line_end(source, b.end_byte)

    text_a = source[a_start:a_end]
    text_b = source[b_start:b_end]
    ed.replace_range(b_start, b_end, text_a)
    ed.replace_range(a_start, a_end, text_b)


def _line_start(source: bytes, pos: int) -> int:
    while pos > 0 and source[pos - 1:pos] not in (b"\n", b"\r"):
        pos -= 1
    return pos


def _line_end(source: bytes, pos: int) -> int:
    while pos < len(source) and source[pos:pos + 1] not in (b"\n", b"\r"):
        pos += 1
    if pos < len(source):
        pos += 1
    return pos


def _call_name(stmt: Node, source: bytes) -> str:
    for node in _walk(stmt):
        if node.type != "call_expression":
            continue
        func = node.child_by_field_name("function")
        if func is None:
            continue
        text = source[func.start_byte:func.end_byte].decode("utf-8", errors="replace").strip()
        for sep in ("::", "->", "."):
            if sep in text:
                text = text.rsplit(sep, 1)[-1]
        return text or "?"
    return "?"


def _looks_inline(func_node: Node, source: bytes) -> bool:
    """Detect obvious inline-style header definitions worth considering."""
    head = source[func_node.start_byte:func_node.end_byte].split(b"{", 1)[0]
    return (
        b"inline" in head
        or b"__inline" in head
        or b"__forceinline" in head
    )


def _project_root_for(path: Path) -> Path:
    resolved = path.resolve()
    for candidate in (resolved.parent, *resolved.parents):
        if (candidate / ".git").exists():
            return candidate
    return resolved.parent
