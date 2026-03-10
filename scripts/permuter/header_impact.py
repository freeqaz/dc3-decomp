"""Helpers for estimating blast radius of shared-header permuter edits."""

from __future__ import annotations

from dataclasses import dataclass
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

    @property
    def total_includers(self) -> int:
        return len(self.including_sources) + len(self.including_headers)


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

    sources: list[Path] = []
    headers: list[Path] = []

    for candidate in _iter_candidate_files(search_roots):
        if candidate.resolve() == normalized_header:
            continue
        if not _includes_header(candidate, normalized_header, normalized_root):
            continue
        if candidate.suffix.lower() in _SOURCE_SUFFIXES:
            sources.append(candidate)
        else:
            headers.append(candidate)

    return HeaderImpact(
        header=normalized_header,
        including_sources=tuple(sorted(sources)),
        including_headers=tuple(sorted(headers)),
    )


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


def _includes_header(candidate: Path, header: Path, project_root: Path) -> bool:
    """Return True if *candidate* includes *header* via a resolvable include."""
    try:
        text = candidate.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return False

    for line in text.splitlines():
        match = _INCLUDE_RE.match(line)
        if not match:
            continue
        include_target = match.group(1)
        resolved = _resolve_include(candidate.parent, include_target, project_root)
        if resolved is not None and resolved == header:
            return True

    return False


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
