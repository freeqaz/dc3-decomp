"""Apply selected local permuter patterns to directly included inline headers."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .extractor import extract_function
from .header_impact import HeaderImpact, estimate_header_impact, resolve_included_files
from .patterns import get_pattern
from .types import AuxiliaryFile, Variant

_SUPPORTED_HEADER_PATTERNS = frozenset({
    "return_call_merge",
    "switch_if_convert",
    "early_return_merge",
    "branch_polarity",
})


@dataclass(frozen=True)
class HeaderPatternVariant:
    """A wrapped header-backed variant produced by a local pattern."""

    variant: Variant
    impact: HeaderImpact
    header_function: str
    header_path: Path
    base_pattern: str


def supported_header_patterns() -> frozenset[str]:
    """Return local patterns supported in header-backed mode."""
    return _SUPPORTED_HEADER_PATTERNS


def discover_header_pattern_variants(
    source_path: Path,
    function_name: str,
    base_pattern_name: str,
    max_variants: int = 8,
) -> list[HeaderPatternVariant]:
    """Run a selected local pattern on directly included inline header functions."""
    if base_pattern_name not in _SUPPORTED_HEADER_PATTERNS:
        raise KeyError(
            f"Unsupported header pattern bridge '{base_pattern_name}'. "
            f"Supported: {', '.join(sorted(_SUPPORTED_HEADER_PATTERNS))}"
        )

    caller_ctx = extract_function(source_path, function_name)
    called_names = _called_function_names(caller_ctx.body_node, caller_ctx.file_source)
    if not called_names:
        return []

    project_root = _project_root_for(source_path)
    pattern = get_pattern(base_pattern_name)
    variants: list[HeaderPatternVariant] = []
    seen_updates: set[tuple[Path, bytes]] = set()

    for header_path in resolve_included_files(source_path, project_root):
        if header_path.suffix.lower() not in {".h", ".hh", ".hpp", ".hxx", ".inl"}:
            continue
        impact = estimate_header_impact(project_root, header_path)
        if impact.risk_tier == "high":
            continue
        for candidate_name in _matching_header_function_names(header_path, called_names):
            if len(variants) >= max_variants:
                return variants
            try:
                header_ctx = extract_function(header_path, candidate_name)
            except ValueError:
                continue
            if not _looks_inline(header_ctx.func_node, header_ctx.file_source):
                continue

            for local_variant in pattern.generate(header_ctx):
                key = (header_path.resolve(), local_variant.source)
                if key in seen_updates:
                    continue
                seen_updates.add(key)
                wrapped = Variant(
                    name=f"header_{base_pattern_name}:{local_variant.name}",
                    pattern_name=f"header_{base_pattern_name}",
                    description=(
                        f"{local_variant.description} "
                        f"in header {candidate_name}"
                    ),
                    source=caller_ctx.file_source,
                    auxiliary_files=(
                        AuxiliaryFile(path=header_path.resolve(), content=local_variant.source),
                    ),
                    tags=local_variant.tags,
                )
                variants.append(
                    HeaderPatternVariant(
                        variant=wrapped,
                        impact=impact,
                        header_function=candidate_name,
                        header_path=header_path.resolve(),
                        base_pattern=base_pattern_name,
                    )
                )
                if len(variants) >= max_variants:
                    return variants

    return variants


def _called_function_names(body_node, source: bytes) -> set[str]:
    names: set[str] = set()
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


def _matching_header_function_names(header_path: Path, called_names: set[str]) -> list[str]:
    """Return qualified/unqualified header function names matching caller-side calls."""
    source = header_path.read_bytes()
    from .extractor import _PARSER, _find_all_function_defs, _get_function_name

    tree = _PARSER.parse(source)
    all_names: list[str] = []
    for func_node in _find_all_function_defs(tree.root_node):
        name = _get_function_name(func_node)
        if name:
            all_names.append(name)

    matches: list[str] = []
    seen: set[str] = set()
    for name in all_names:
        candidates = {name}
        for sep in ("::", "->", "."):
            if sep in name:
                candidates.add(name.rsplit(sep, 1)[-1])
        if candidates & called_names and name not in seen:
            seen.add(name)
            matches.append(name)
    return matches


def _looks_inline(func_node, source: bytes) -> bool:
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


def _walk(node):
    yield node
    for child in node.children:
        yield from _walk(child)
