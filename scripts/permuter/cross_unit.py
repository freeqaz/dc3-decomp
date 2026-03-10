"""Helpers for mapping cross-unit file impact to concrete functions."""

from __future__ import annotations

import sqlite3
from dataclasses import dataclass
from pathlib import Path

from .header_impact import HeaderImpact
from .types import extract_qualified_name


@dataclass(frozen=True)
class AffectedFunction:
    """A function that would need scoring for a cross-unit/header edit."""

    symbol: str
    function_name: str
    unit: str
    source_path: Path
    current_percent: float | None
    verdict: str | None


def lookup_functions_for_sources(
    db_path: Path,
    source_paths: tuple[Path, ...],
    project_root: Path,
    exclude_complete: bool = False,
) -> tuple[AffectedFunction, ...]:
    """Resolve source files to functions recorded in decomp.db."""
    normalized = {_normalize_source_path(project_root, path) for path in source_paths}
    if not normalized:
        return ()

    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    try:
        rows = conn.execute(
            "SELECT symbol, demangled, unit, current_percent, verdict FROM functions"
        ).fetchall()
    finally:
        conn.close()

    matches: list[AffectedFunction] = []
    for row in rows:
        unit = row["unit"] or ""
        if _normalize_unit(unit) not in normalized:
            continue
        verdict = row["verdict"]
        if exclude_complete and verdict == "COMPLETE":
            continue
        demangled = row["demangled"] or ""
        function_name = extract_qualified_name(demangled) or demangled or row["symbol"]
        matches.append(
            AffectedFunction(
                symbol=row["symbol"],
                function_name=function_name,
                unit=unit,
                source_path=project_root / _normalize_unit(unit),
                current_percent=row["current_percent"],
                verdict=verdict,
            )
        )

    matches.sort(key=lambda item: (str(item.source_path), item.function_name, item.symbol))
    return tuple(matches)


def lookup_functions_for_header_impact(
    db_path: Path,
    impact: HeaderImpact,
    project_root: Path,
    exclude_complete: bool = False,
) -> tuple[AffectedFunction, ...]:
    """Resolve a header impact summary to affected functions."""
    return lookup_functions_for_sources(
        db_path,
        impact.affected_sources,
        project_root=project_root,
        exclude_complete=exclude_complete,
    )


def _normalize_source_path(project_root: Path, path: Path) -> str:
    """Normalize a source path into the form stored in decomp.db."""
    resolved = path.resolve()
    try:
        rel = resolved.relative_to(project_root.resolve())
        return rel.as_posix()
    except ValueError:
        return resolved.as_posix()


def _normalize_unit(unit: str) -> str:
    """Drop known prefixes from stored DB unit paths."""
    if unit.startswith("default/"):
        return unit[len("default/"):]
    return unit
