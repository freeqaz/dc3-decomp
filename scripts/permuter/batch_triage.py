"""Batch triage — diagnose and classify near-match functions.

Queries decomp.db for functions at 90-99.9% match, runs objdiff with
diagnosis on each, and classifies by mismatch type (REGSWAP_ONLY,
REGSWAP_PLUS, STRUCTURAL, NOISE_ONLY, UNFIXABLE, MIXED).

Usage:
    python -m scripts.permuter.batch_triage --min-pct 90 --max-pct 99.9 -o report.json
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import subprocess
import sys
import time
from pathlib import Path

from .diagnosis import diagnose_baseline, is_all_noise
from .types import Diagnosis, TriageResult

# Repo root (script lives in scripts/permuter/)
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
OBJDIFF_JSON = REPO_ROOT / "objdiff.json"
DECOMP_DB = REPO_ROOT / "decomp.db"

# Regex to extract qualified C++ name from demangled signature
import re

QUALIFIED_NAME_RE = re.compile(r"([\w~][\w:~]*(?:::[\w~]+)+)\s*\(")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Triage near-match functions by mismatch category.",
    )
    parser.add_argument(
        "--min-pct", type=float, default=90,
        help="Minimum match percentage (default: 90)",
    )
    parser.add_argument(
        "--max-pct", type=float, default=99.9,
        help="Maximum match percentage (default: 99.9)",
    )
    parser.add_argument(
        "--include-at-limit", action="store_true",
        help="Include functions marked AT_LIMIT (default: exclude)",
    )
    parser.add_argument(
        "--limit", type=int, default=0,
        help="Max functions to process (0 = unlimited)",
    )
    parser.add_argument(
        "-o", "--output", type=Path, default=None,
        help="Output JSON file (default: stdout)",
    )
    parser.add_argument(
        "--json", action="store_true", dest="json_output",
        help="Force JSON output to stdout (ignored if -o is set)",
    )
    return parser.parse_args()


def load_unit_source_map() -> dict[str, str]:
    """Load objdiff.json and return unit name -> source_path mapping."""
    with open(OBJDIFF_JSON) as f:
        data = json.load(f)

    mapping = {}
    for unit in data.get("units", []):
        name = unit.get("name", "")
        source_path = unit.get("metadata", {}).get("source_path")
        if name and source_path:
            mapping[name] = source_path
    return mapping


def query_candidates(
    unit_source_map: dict[str, str],
    min_pct: float,
    max_pct: float,
    include_at_limit: bool,
    limit: int,
) -> list[dict]:
    """Query decomp.db for candidate functions."""
    conn = sqlite3.connect(str(DECOMP_DB))
    conn.row_factory = sqlite3.Row

    if include_at_limit:
        verdict_clause = "AND (verdict IS NULL OR verdict NOT IN ('COMPLETE'))"
    else:
        verdict_clause = "AND (verdict IS NULL OR verdict NOT IN ('AT_LIMIT', 'COMPLETE'))"

    rows = conn.execute(
        f"""
        SELECT symbol, demangled, unit, current_percent, verdict
        FROM functions
        WHERE current_percent >= ? AND current_percent <= ?
          {verdict_clause}
          AND symbol NOT LIKE 'merged_%'
          AND symbol NOT LIKE 'fn_%'
          AND demangled NOT LIKE '%stlpmtx_std::%'
        ORDER BY current_percent DESC
        """,
        (min_pct, max_pct),
    ).fetchall()
    conn.close()

    candidates = []
    for row in rows:
        row_dict = dict(row)
        unit = row_dict["unit"]
        demangled = row_dict.get("demangled", "")

        source_path = unit_source_map.get(unit)
        if not source_path:
            continue

        if not Path(REPO_ROOT / source_path).exists():
            continue

        m = QUALIFIED_NAME_RE.search(demangled or "")
        if not m:
            continue

        qualified_name = m.group(1)
        row_dict["source_path"] = source_path
        row_dict["qualified_name"] = qualified_name
        candidates.append(row_dict)

    if limit > 0:
        candidates = candidates[:limit]

    return candidates


def classify(diagnosis: Diagnosis) -> str:
    """Classify a diagnosis into a mismatch category."""
    if is_all_noise(diagnosis):
        return "NOISE_ONLY"

    has_gpr = any(
        p[0].startswith("r") or p[1].startswith("r")
        for p in diagnosis.reg_swap_pairs
    )

    if has_gpr and not diagnosis.diff_ops and not diagnosis.clusters:
        return "REGSWAP_ONLY"

    if has_gpr and len(diagnosis.diff_ops) <= 3 and len(diagnosis.clusters) <= 1:
        return "REGSWAP_PLUS"

    if len(diagnosis.clusters) >= 3 or len(diagnosis.diff_ops) >= 5:
        return "STRUCTURAL"

    # No GPR swaps, no diff_ops, no clusters, no real replaces — only unexplained noise
    if not has_gpr and not diagnosis.diff_ops and not diagnosis.clusters and diagnosis.replace_real == 0:
        return "UNFIXABLE"

    return "MIXED"


def build_object(source_path: str) -> bool:
    """Build the object file for a source path."""
    obj_target = f"build/373307D9/{Path(source_path).with_suffix('.obj')}"
    result = subprocess.run(
        ["ninja", obj_target],
        capture_output=True,
        text=True,
        cwd=str(REPO_ROOT),
    )
    return result.returncode == 0


def run_objdiff(symbol: str) -> tuple[float, dict | None]:
    """Run objdiff-cli with --include-instructions and return (match%, json)."""
    cmd = [
        "./bin/objdiff-cli", "diff", "-p", ".", symbol,
        "-f", "json", "--include-instructions",
    ]
    result = subprocess.run(
        cmd, capture_output=True, text=True, cwd=str(REPO_ROOT),
    )
    try:
        data = json.loads(result.stdout)
        match_pct = data.get("fuzzy_match_percent", 0.0)
        return match_pct, data
    except (json.JSONDecodeError, KeyError):
        return 0.0, None


def triage_function(candidate: dict) -> TriageResult:
    """Diagnose and classify a single function."""
    symbol = candidate["symbol"]
    source_path = candidate["source_path"]

    # Build
    if not build_object(source_path):
        return TriageResult(
            symbol=symbol,
            demangled=candidate.get("demangled", ""),
            unit=candidate["unit"],
            source_path=source_path,
            qualified_name=candidate["qualified_name"],
            current_percent=candidate["current_percent"],
            category="ERROR",
            gpr_swap_pairs=[],
            diff_op_count=0,
            cluster_count=0,
            total_instructions=0,
            error="build failed",
        )

    # Run objdiff with instructions
    match_pct, objdiff_data = run_objdiff(symbol)

    if not objdiff_data or not objdiff_data.get("instructions"):
        return TriageResult(
            symbol=symbol,
            demangled=candidate.get("demangled", ""),
            unit=candidate["unit"],
            source_path=source_path,
            qualified_name=candidate["qualified_name"],
            current_percent=match_pct or candidate["current_percent"],
            category="ERROR",
            gpr_swap_pairs=[],
            diff_op_count=0,
            cluster_count=0,
            total_instructions=0,
            error="no instruction data from objdiff",
        )

    # Diagnose
    diagnosis = diagnose_baseline(objdiff_data)
    category = classify(diagnosis)

    # Extract GPR swap pair info
    gpr_pairs = []
    for (r0, r1), info in diagnosis.reg_swap_pairs.items():
        if r0.startswith("r") or r1.startswith("r"):
            gpr_pairs.append({"pair": [r0, r1], "count": info.count})

    return TriageResult(
        symbol=symbol,
        demangled=candidate.get("demangled", ""),
        unit=candidate["unit"],
        source_path=source_path,
        qualified_name=candidate["qualified_name"],
        current_percent=match_pct or candidate["current_percent"],
        category=category,
        gpr_swap_pairs=gpr_pairs,
        diff_op_count=len(diagnosis.diff_ops),
        cluster_count=len(diagnosis.clusters),
        total_instructions=diagnosis.total_instructions,
    )


def main():
    args = parse_args()

    print("Loading objdiff.json...", file=sys.stderr)
    unit_source_map = load_unit_source_map()
    print(f"  {len(unit_source_map)} units with source paths", file=sys.stderr)

    print(
        f"Querying decomp.db ({args.min_pct}-{args.max_pct}%, "
        f"at_limit={'included' if args.include_at_limit else 'excluded'})...",
        file=sys.stderr,
    )
    candidates = query_candidates(
        unit_source_map, args.min_pct, args.max_pct,
        args.include_at_limit, args.limit,
    )
    print(f"  {len(candidates)} candidates", file=sys.stderr)

    if not candidates:
        print("No candidates found.", file=sys.stderr)
        sys.exit(0)

    # Triage each function
    results: list[TriageResult] = []
    start_time = time.time()

    for i, candidate in enumerate(candidates):
        func = candidate["qualified_name"]
        pct = candidate["current_percent"]
        print(
            f"[{i + 1}/{len(candidates)}] {func} ({pct:.1f}%) ... ",
            end="", flush=True, file=sys.stderr,
        )

        result = triage_function(candidate)
        results.append(result)

        if result.error:
            print(f"ERROR: {result.error}", file=sys.stderr)
        else:
            gpr_str = ""
            if result.gpr_swap_pairs:
                pairs = [f"{p['pair'][0]}<->{p['pair'][1]}" for p in result.gpr_swap_pairs[:3]]
                gpr_str = f" [{', '.join(pairs)}]"
            print(f"{result.category}{gpr_str}", file=sys.stderr)

    elapsed = time.time() - start_time

    # Build report
    from dataclasses import asdict
    report = {
        "metadata": {
            "min_pct": args.min_pct,
            "max_pct": args.max_pct,
            "include_at_limit": args.include_at_limit,
            "total_candidates": len(candidates),
            "elapsed_seconds": round(elapsed, 1),
        },
        "summary": _build_summary(results),
        "results": [asdict(r) for r in results],
    }

    # Output
    output_text = json.dumps(report, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output_text)
        print(f"\nReport written to: {args.output}", file=sys.stderr)
    else:
        print(output_text)

    # Print summary to stderr
    _print_summary(report["summary"])


def _build_summary(results: list[TriageResult]) -> dict:
    """Build category summary from triage results."""
    from collections import Counter
    cats = Counter(r.category for r in results)
    total = len(results)
    return {
        "total": total,
        "by_category": {
            cat: {"count": count, "pct": round(100 * count / total, 1) if total else 0}
            for cat, count in cats.most_common()
        },
    }


def _print_summary(summary: dict):
    """Print human-readable category summary."""
    print(f"\n{'=' * 50}", file=sys.stderr)
    print("TRIAGE SUMMARY", file=sys.stderr)
    print(f"{'=' * 50}", file=sys.stderr)
    print(f"  Total: {summary['total']}", file=sys.stderr)
    for cat, info in summary["by_category"].items():
        print(f"  {cat:20s}: {info['count']:4d} ({info['pct']:.1f}%)", file=sys.stderr)


if __name__ == "__main__":
    main()
