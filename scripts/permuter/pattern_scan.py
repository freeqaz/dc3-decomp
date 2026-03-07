"""Pattern scan — Semgrep-like AST scanning for permuter pattern matches.

Scans source files for functions where specific permuter patterns would
generate variants, WITHOUT building or scoring. Pure tree-sitter analysis.

This is much faster than batch_triage+batch_sweep because it skips:
- objdiff builds (~1s per function)
- Diagnosis from instruction-level diffs
- Variant scoring (compilation)

Instead, it just parses C++ and runs pattern generators to see which
functions produce variants.

Usage:
    # Scan specific patterns across all decomp source files
    python -m scripts.permuter.pattern_scan --patterns null_guard_elimination

    # Scan a specific unit
    python -m scripts.permuter.pattern_scan --patterns null_guard_elimination --unit "meta_ham/*"

    # Scan multiple patterns
    python -m scripts.permuter.pattern_scan --patterns null_guard_elimination,reference_elimination

    # Show variant details (what exactly would change)
    python -m scripts.permuter.pattern_scan --patterns null_guard_elimination --show-variants

    # Only show functions that aren't already 100%
    python -m scripts.permuter.pattern_scan --patterns null_guard_elimination --incomplete-only

    # JSON output for piping to other tools
    python -m scripts.permuter.pattern_scan --patterns null_guard_elimination --json

    # Limit to functions with match < 99%
    python -m scripts.permuter.pattern_scan --patterns null_guard_elimination --max-pct 99
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import sqlite3
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

from .extractor import (
    _PARSER,
    _find_all_function_defs,
    _find_function_preproc_regions,
    _get_function_name,
)
from .patterns import get_pattern, list_patterns
from .patterns.base import Pattern
from .types import FunctionContext, Variant

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DECOMP_DB = REPO_ROOT / "decomp.db"
OBJDIFF_JSON = REPO_ROOT / "objdiff.json"


@dataclass
class ScanHit:
    """A function where a pattern generated at least one variant."""
    source_path: str
    function_name: str
    pattern_name: str
    variant_count: int
    variants: list[dict] = field(default_factory=list)  # name, description
    # Optional: match info from decomp.db
    symbol: str = ""
    match_percent: float | None = None
    unit: str = ""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m scripts.permuter.pattern_scan",
        description="Scan codebase for functions matching permuter patterns (no build required).",
    )
    parser.add_argument(
        "--patterns",
        help="Comma-separated pattern names to scan for (omit to list available patterns)",
    )
    parser.add_argument(
        "--unit",
        help="Unit glob pattern (e.g. 'meta_ham/*', 'system/obj/*')",
    )
    parser.add_argument(
        "--source",
        type=Path,
        help="Scan a single source file instead of the whole codebase",
    )
    parser.add_argument(
        "--show-variants", action="store_true",
        help="Show variant details (descriptions of what would change)",
    )
    parser.add_argument(
        "--incomplete-only", action="store_true",
        help="Only show functions that aren't already 100%% matched",
    )
    parser.add_argument(
        "--max-pct", type=float, default=100.0,
        help="Only show functions with match%% below this threshold (default: 100)",
    )
    parser.add_argument(
        "--min-pct", type=float, default=0.0,
        help="Only show functions with match%% above this threshold (default: 0)",
    )
    parser.add_argument(
        "--json", action="store_true", dest="json_output",
        help="Output results as JSON",
    )
    parser.add_argument(
        "--limit", type=int, default=0,
        help="Max hits to report (0 = unlimited)",
    )
    parser.add_argument(
        "--scan", action="store_true",
        help="When listing patterns (no --patterns), scan codebase and show hit counts (~30s)",
    )
    return parser.parse_args()


def _load_source_files(unit_glob: str | None) -> list[tuple[str, str]]:
    """Load source file paths from objdiff.json, optionally filtered by unit glob.

    Returns list of (unit_name, source_path).
    """
    if not OBJDIFF_JSON.exists():
        print(f"Error: {OBJDIFF_JSON} not found", file=sys.stderr)
        sys.exit(1)

    with open(OBJDIFF_JSON) as f:
        data = json.load(f)

    results = []
    for unit in data.get("units", []):
        name = unit.get("name", "")
        source = unit.get("metadata", {}).get("source_path")
        if not source:
            continue
        if unit_glob and not fnmatch.fnmatch(name, f"*{unit_glob}*"):
            continue
        results.append((name, source))

    return results


def _load_match_info() -> dict[str, tuple[float, str]]:
    """Load function match percentages from decomp.db.

    Returns dict mapping qualified_name -> (match_percent, symbol).
    """
    if not DECOMP_DB.exists():
        return {}

    try:
        conn = sqlite3.connect(str(DECOMP_DB))
        conn.row_factory = sqlite3.Row
        rows = conn.execute(
            "SELECT symbol, demangled, match_percent FROM functions "
            "WHERE match_percent IS NOT NULL"
        ).fetchall()
        conn.close()

        from .types import extract_qualified_name
        result = {}
        for row in rows:
            qname = extract_qualified_name(row["demangled"])
            if qname:
                result[qname] = (row["match_percent"], row["symbol"])
        return result
    except Exception:
        return {}


def _scan_file(
    source_path: Path,
    patterns: list[Pattern],
    unit_name: str,
    match_info: dict[str, tuple[float, str]],
    show_variants: bool,
    max_variants_per_func: int = 10,
) -> list[ScanHit]:
    """Scan a single source file for pattern matches."""
    if not source_path.exists():
        return []

    try:
        source = source_path.read_bytes()
    except OSError:
        return []

    tree = _PARSER.parse(source)
    hits: list[ScanHit] = []

    for func_node in _find_all_function_defs(tree.root_node):
        name = _get_function_name(func_node)
        if not name:
            continue

        body = func_node.child_by_field_name("body")
        if body is None:
            continue

        statements = list(body.named_children)
        func_range = (func_node.start_byte, func_node.end_byte)
        ctx = FunctionContext(
            file_path=source_path,
            file_source=source,
            func_node=func_node,
            body_node=body,
            statements=statements,
            func_byte_range=func_range,
            preproc_regions=_find_function_preproc_regions(source, func_range),
        )

        for pattern in patterns:
            try:
                variants = []
                for v in pattern.generate(ctx):
                    variants.append({"name": v.name, "description": v.description})
                    if len(variants) >= max_variants_per_func:
                        break

                if variants:
                    info = match_info.get(name, (None, ""))
                    hit = ScanHit(
                        source_path=str(source_path),
                        function_name=name,
                        pattern_name=pattern.name,
                        variant_count=len(variants),
                        variants=variants if show_variants else [],
                        match_percent=info[0],
                        symbol=info[1],
                        unit=unit_name,
                    )
                    hits.append(hit)
            except Exception:
                # Don't let one function's parse error kill the scan
                continue

    return hits


def main():
    args = parse_args()

    # No patterns specified — show available and exit
    if not args.patterns:
        from .scan_and_permute import _print_pattern_table, _scan_all_counts
        counts = None
        if getattr(args, 'scan', False):
            print("Scanning codebase for all patterns...", file=sys.stderr)
            scan_start = time.time()
            counts = _scan_all_counts(args.unit)
            print(f"  Done in {time.time() - scan_start:.1f}s", file=sys.stderr)
        _print_pattern_table(counts)
        sys.exit(0)

    # Parse pattern names
    default_available = list_patterns()
    all_available = list_patterns(include_opt_in=True)
    if args.patterns.strip() == "all":
        # Keep historical behavior: `all` excludes opt-in patterns.
        pattern_names = default_available
    else:
        pattern_names = [p.strip() for p in args.patterns.split(",")]

    patterns = []
    for name in pattern_names:
        if name not in all_available:
            print(f"Error: unknown pattern '{name}'", file=sys.stderr)
            from .scan_and_permute import _print_pattern_table
            _print_pattern_table()
            sys.exit(1)
        patterns.append(get_pattern(name))

    # Determine files to scan
    if args.source:
        files = [("(single file)", str(args.source))]
    else:
        files = _load_source_files(args.unit)

    if not files:
        print("No source files found.", file=sys.stderr)
        sys.exit(0)

    # Load match info for filtering
    match_info = _load_match_info()

    # Scan
    start = time.time()
    all_hits: list[ScanHit] = []
    files_scanned = 0
    pattern_str = ", ".join(p.name for p in patterns)

    print(
        f"Scanning {len(files)} files for patterns: {pattern_str}",
        file=sys.stderr,
    )

    for unit_name, source_path in files:
        hits = _scan_file(
            Path(source_path), patterns, unit_name,
            match_info, args.show_variants,
        )

        # Apply filters
        for hit in hits:
            if args.incomplete_only and hit.match_percent is not None and hit.match_percent >= 100.0:
                continue
            if hit.match_percent is not None and hit.match_percent >= args.max_pct:
                continue
            if hit.match_percent is not None and hit.match_percent < args.min_pct:
                continue
            all_hits.append(hit)

        files_scanned += 1

        if args.limit and len(all_hits) >= args.limit:
            all_hits = all_hits[:args.limit]
            break

    elapsed = time.time() - start

    # Output
    if args.json_output:
        data = {
            "metadata": {
                "patterns": pattern_names,
                "files_scanned": files_scanned,
                "elapsed_seconds": round(elapsed, 2),
            },
            "hits": [
                {
                    "source_path": h.source_path,
                    "function_name": h.function_name,
                    "pattern": h.pattern_name,
                    "variant_count": h.variant_count,
                    "match_percent": h.match_percent,
                    "symbol": h.symbol,
                    "unit": h.unit,
                    **({"variants": h.variants} if h.variants else {}),
                }
                for h in all_hits
            ],
            "summary": {
                "total_hits": len(all_hits),
                "by_pattern": _count_by(all_hits, "pattern_name"),
            },
        }
        print(json.dumps(data, indent=2))
    else:
        _print_text(all_hits, elapsed, files_scanned, args.show_variants)


def _count_by(hits: list[ScanHit], attr: str) -> dict[str, int]:
    from collections import Counter
    return dict(Counter(getattr(h, attr) for h in hits).most_common())


def _print_text(
    hits: list[ScanHit],
    elapsed: float,
    files_scanned: int,
    show_variants: bool,
):
    if not hits:
        print(f"\nNo hits found ({files_scanned} files scanned in {elapsed:.1f}s).")
        return

    # Group by source file for readability
    by_file: dict[str, list[ScanHit]] = {}
    for h in hits:
        by_file.setdefault(h.source_path, []).append(h)

    print(f"\n{'=' * 70}")
    print(f"PATTERN SCAN RESULTS ({len(hits)} hits in {files_scanned} files, {elapsed:.1f}s)")
    print(f"{'=' * 70}")

    for source_path, file_hits in sorted(by_file.items()):
        print(f"\n  {source_path}")
        for h in file_hits:
            pct_str = f" ({h.match_percent:.1f}%)" if h.match_percent is not None else ""
            print(f"    [{h.pattern_name}] {h.function_name}{pct_str} — {h.variant_count} variant(s)")
            if show_variants:
                for v in h.variants:
                    print(f"      - {v['description']}")

    # Summary
    by_pattern = _count_by(hits, "pattern_name")
    print(f"\n  Summary: {len(hits)} functions across {len(by_file)} files")
    for pattern, count in by_pattern.items():
        print(f"    {pattern}: {count}")


if __name__ == "__main__":
    main()
