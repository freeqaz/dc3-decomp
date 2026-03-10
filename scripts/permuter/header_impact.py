"""Helpers for estimating blast radius of shared-header permuter edits."""

from __future__ import annotations

from dataclasses import dataclass
from collections import deque
from pathlib import Path
import re

_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')
_SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
_HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".inl"}


@dataclass(frozen=True)
class HeaderImpact:
    """Blast-radius summary for a candidate header edit."""

    header: Path
    including_sources: tuple[Path, ...]
    including_headers: tuple[Path, ...]
    affected_sources: tuple[Path, ...] = ()
    affected_headers: tuple[Path, ...] = ()
    max_include_depth: int = 1

    @property
    def total_includers(self) -> int:
        return len(self.including_sources) + len(self.including_headers)

    @property
    def total_affected_files(self) -> int:
        return len(self.affected_sources) + len(self.affected_headers)

    @property
    def risk_tier(self) -> str:
        affected_sources = len(self.affected_sources)
        if affected_sources >= 8 or self.max_include_depth >= 4:
            return "high"
        if affected_sources >= 3 or self.max_include_depth >= 2:
            return "medium"
        return "low"


def estimate_header_impact(
    project_root: Path,
    header: Path,
    search_roots: tuple[Path, ...] | None = None,
) -> HeaderImpact:
    """Estimate how many translation units and headers include *header*."""
    normalized_root = project_root.resolve()
    normalized_header = header.resolve()
    if search_roots is None:
        search_roots = (normalized_root,)

    reverse_graph = _build_reverse_include_graph(search_roots, normalized_root)
    direct_includers = reverse_graph.get(normalized_header, set())
    affected, max_depth = _walk_reverse_include_graph(reverse_graph, normalized_header)

    return HeaderImpact(
        header=normalized_header,
        including_sources=_bucket_files(direct_includers, _SOURCE_SUFFIXES),
        including_headers=_bucket_files(direct_includers, _HEADER_SUFFIXES),
        affected_sources=_bucket_files(affected, _SOURCE_SUFFIXES),
        affected_headers=_bucket_files(affected, _HEADER_SUFFIXES),
        max_include_depth=max_depth,
    )


def resolve_included_files(candidate: Path, project_root: Path) -> tuple[Path, ...]:
    """Resolve include targets from a source/header against the project root."""
    return _iter_resolved_includes(candidate, project_root.resolve())


def _iter_candidate_files(search_roots: tuple[Path, ...]) -> list[Path]:
    """Collect source/header files from the search roots."""
    seen: set[Path] = set()
    results: list[Path] = []
    for root in search_roots:
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix.lower() not in _SOURCE_SUFFIXES | _HEADER_SUFFIXES:
                continue
            resolved = path.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            results.append(path)
    return results


def _build_reverse_include_graph(
    search_roots: tuple[Path, ...],
    project_root: Path,
) -> dict[Path, set[Path]]:
    """Map included file -> files that include it."""
    reverse_graph: dict[Path, set[Path]] = {}
    for candidate in _iter_candidate_files(search_roots):
        for include in _iter_resolved_includes(candidate, project_root):
            reverse_graph.setdefault(include, set()).add(candidate.resolve())
    return reverse_graph


def _walk_reverse_include_graph(
    reverse_graph: dict[Path, set[Path]],
    header: Path,
) -> tuple[set[Path], int]:
    """Collect all direct/transitive includers of a header."""
    affected: set[Path] = set()
    max_depth = 0
    queue: deque[tuple[Path, int]] = deque((path, 1) for path in reverse_graph.get(header, ()))

    while queue:
        current, depth = queue.popleft()
        if current in affected:
            continue
        affected.add(current)
        max_depth = max(max_depth, depth)
        for parent in reverse_graph.get(current, ()):
            queue.append((parent, depth + 1))

    return affected, max_depth


def _bucket_files(paths: set[Path], suffixes: set[str]) -> tuple[Path, ...]:
    """Sort and keep only files matching the requested suffix bucket."""
    return tuple(
        sorted(path for path in paths if path.suffix.lower() in suffixes)
    )


def _iter_resolved_includes(candidate: Path, project_root: Path) -> tuple[Path, ...]:
    """Resolve all includes in *candidate* against known project paths."""
    try:
        text = candidate.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return ()

    resolved: list[Path] = []

    for line in text.splitlines():
        match = _INCLUDE_RE.match(line)
        if not match:
            continue
        include_target = match.group(1)
        include_path = _resolve_include(candidate.parent, include_target, project_root)
        if include_path is not None:
            resolved.append(include_path)

    return tuple(resolved)


def _resolve_include(base_dir: Path, include_target: str, project_root: Path) -> Path | None:
    """Resolve an include path relative to the includer and project root."""
    candidates = [
        (base_dir / include_target).resolve(),
        (project_root / include_target).resolve(),
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None
