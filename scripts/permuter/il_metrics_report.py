#!/usr/bin/env python3
"""Summarize persisted IL analysis metrics from permuter_cache.db."""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from collections import Counter, defaultdict
from pathlib import Path

try:
    from .repo_paths import get_cache_db_path
except ImportError:
    _PROJECT_ROOT = Path(__file__).resolve().parents[2]
    if str(_PROJECT_ROOT) not in sys.path:
        sys.path.insert(0, str(_PROJECT_ROOT))
    from scripts.permuter.repo_paths import get_cache_db_path

DEFAULT_DB = get_cache_db_path()


def _connect(db_path: Path) -> sqlite3.Connection:
    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    return conn


def _rows_with_il(conn: sqlite3.Connection) -> list[sqlite3.Row]:
    cols = {
        row["name"]
        for row in conn.execute("PRAGMA table_info(improvement_runs)").fetchall()
    }
    pattern_col = ", il_pattern_metrics" if "il_pattern_metrics" in cols else ""
    return conn.execute(
        f"""
        SELECT id, timestamp, symbol, function_name, delta, stopped_reason,
               il_analyzed_variants, il_unique_buckets, il_duplicate_buckets,
               winning_rounds{pattern_col}
        FROM improvement_runs
        WHERE il_analyzed_variants > 0
        ORDER BY id DESC
        """
    ).fetchall()


def build_report(conn: sqlite3.Connection) -> dict:
    rows = _rows_with_il(conn)

    totals = conn.execute(
        """
        SELECT COUNT(*) as total_rows,
               SUM(CASE WHEN il_analyzed_variants > 0 THEN 1 ELSE 0 END) as il_rows,
               SUM(COALESCE(il_analyzed_variants, 0)) as analyzed,
               SUM(COALESCE(il_unique_buckets, 0)) as uniq,
               SUM(COALESCE(il_duplicate_buckets, 0)) as dup
        FROM improvement_runs
        """
    ).fetchone()

    top_high_dup = [
        {
            "id": row["id"],
            "function": row["function_name"],
            "delta": row["delta"],
            "stopped_reason": row["stopped_reason"],
            "analyzed": row["il_analyzed_variants"],
            "unique": row["il_unique_buckets"],
            "duplicate": row["il_duplicate_buckets"],
        }
        for row in sorted(
            (r for r in rows if r["il_duplicate_buckets"] >= 4),
            key=lambda r: r["delta"],
            reverse=True,
        )[:10]
    ]

    by_dup: dict[int, list[sqlite3.Row]] = defaultdict(list)
    for row in rows:
        by_dup[row["il_duplicate_buckets"]].append(row)

    dup_buckets = []
    for dup in sorted(by_dup):
        subset = by_dup[dup]
        reason_counts = Counter(r["stopped_reason"] for r in subset)
        dup_buckets.append(
            {
                "duplicate_buckets": dup,
                "runs": len(subset),
                "avg_delta": round(sum(r["delta"] for r in subset) / len(subset), 4),
                "max_delta": round(max(r["delta"] for r in subset), 4),
                "perfects": reason_counts.get("perfect", 0),
                "depth_exhausted": reason_counts.get("depth_exhausted", 0),
                "stopped_reason_counts": dict(sorted(reason_counts.items())),
            }
        )

    pattern_stats: dict[str, dict[str, float | int]] = defaultdict(
        lambda: {"runs": 0, "dup_sum": 0, "delta_sum": 0.0}
    )
    il_pattern_stats: dict[str, dict[str, int]] = defaultdict(
        lambda: {"rows": 0, "analyzed_variants": 0, "unique_buckets": 0, "duplicate_buckets": 0}
    )
    for row in rows:
        seen = set()
        for item in json.loads(row["winning_rounds"] or "[]"):
            pattern = item.get("pattern")
            if not pattern or pattern in seen:
                continue
            seen.add(pattern)
            entry = pattern_stats[pattern]
            entry["runs"] += 1
            entry["dup_sum"] += row["il_duplicate_buckets"]
            entry["delta_sum"] += row["delta"]
        raw_il_pattern_metrics = row["il_pattern_metrics"] if "il_pattern_metrics" in row.keys() else None
        if raw_il_pattern_metrics:
            for pattern, metrics in json.loads(raw_il_pattern_metrics).items():
                entry = il_pattern_stats[pattern]
                entry["rows"] += 1
                entry["analyzed_variants"] += int(metrics.get("analyzed_variants", 0) or 0)
                entry["unique_buckets"] += int(metrics.get("unique_buckets", 0) or 0)
                entry["duplicate_buckets"] += int(metrics.get("duplicate_buckets", 0) or 0)

    top_patterns = [
        {
            "pattern": pattern,
            "runs": int(data["runs"]),
            "dup_sum": int(data["dup_sum"]),
            "delta_sum": round(float(data["delta_sum"]), 4),
        }
        for pattern, data in sorted(
            pattern_stats.items(),
            key=lambda item: (-item[1]["dup_sum"], -item[1]["runs"], item[0]),
        )[:15]
    ]

    function_stats: dict[str, dict[str, float | int]] = defaultdict(
        lambda: {"runs": 0, "dup_sum": 0, "delta_sum": 0.0}
    )
    for row in rows:
        entry = function_stats[row["function_name"]]
        entry["runs"] += 1
        entry["dup_sum"] += row["il_duplicate_buckets"]
        entry["delta_sum"] += row["delta"]

    top_functions = [
        {
            "function": function,
            "runs": int(data["runs"]),
            "dup_sum": int(data["dup_sum"]),
            "delta_sum": round(float(data["delta_sum"]), 4),
        }
        for function, data in sorted(
            function_stats.items(),
            key=lambda item: (-item[1]["dup_sum"], -item[1]["runs"], item[0]),
        )[:15]
    ]

    top_il_patterns = [
        {
            "pattern": pattern,
            "rows": int(data["rows"]),
            "analyzed_variants": int(data["analyzed_variants"]),
            "unique_buckets": int(data["unique_buckets"]),
            "duplicate_buckets": int(data["duplicate_buckets"]),
        }
        for pattern, data in sorted(
            il_pattern_stats.items(),
            key=lambda item: (
                -item[1]["duplicate_buckets"],
                -item[1]["analyzed_variants"],
                item[0],
            ),
        )[:15]
    ]

    return {
        "totals": {
            "rows": totals["total_rows"] or 0,
            "il_rows": totals["il_rows"] or 0,
            "analyzed": totals["analyzed"] or 0,
            "unique": totals["uniq"] or 0,
            "duplicate": totals["dup"] or 0,
        },
        "top_high_duplicate_runs": top_high_dup,
        "duplicate_bucket_summary": dup_buckets,
        "top_patterns_by_duplicate_buckets": top_patterns,
        "top_functions_by_duplicate_buckets": top_functions,
        "top_il_patterns_by_duplicate_buckets": top_il_patterns,
    }


def print_report(report: dict) -> None:
    totals = report["totals"]
    print(
        f"Rows: {totals['rows']} total, {totals['il_rows']} with IL data | "
        f"analyzed={totals['analyzed']} unique={totals['unique']} dup={totals['duplicate']}"
    )
    print("\nDuplicate bucket summary:")
    for item in report["duplicate_bucket_summary"]:
        print(
            f"  dup={item['duplicate_buckets']}: runs={item['runs']} "
            f"avg_delta={item['avg_delta']:.4f} max_delta={item['max_delta']:.4f} "
            f"perfects={item['perfects']} depth_exhausted={item['depth_exhausted']}"
        )

    print("\nTop high-duplicate runs:")
    for item in report["top_high_duplicate_runs"]:
        print(
            f"  {item['function']}: delta={item['delta']:.4f} "
            f"stop={item['stopped_reason']} analyzed={item['analyzed']} "
            f"unique={item['unique']} dup={item['duplicate']}"
        )

    print("\nTop patterns by duplicate buckets:")
    for item in report["top_patterns_by_duplicate_buckets"]:
        print(
            f"  {item['pattern']}: runs={item['runs']} "
            f"dup_sum={item['dup_sum']} delta_sum={item['delta_sum']:.4f}"
        )

    print("\nTop functions by duplicate buckets:")
    for item in report["top_functions_by_duplicate_buckets"]:
        print(
            f"  {item['function']}: runs={item['runs']} "
            f"dup_sum={item['dup_sum']} delta_sum={item['delta_sum']:.4f}"
        )

    if report["top_il_patterns_by_duplicate_buckets"]:
        print("\nTop IL patterns by duplicate buckets:")
        for item in report["top_il_patterns_by_duplicate_buckets"]:
            print(
                f"  {item['pattern']}: rows={item['rows']} "
                f"dup={item['duplicate_buckets']} "
                f"uniq={item['unique_buckets']} "
                f"an={item['analyzed_variants']}"
            )


def main() -> None:
    parser = argparse.ArgumentParser(description="Summarize persisted IL metrics")
    parser.add_argument("--db", type=Path, default=DEFAULT_DB, help="Path to permuter_cache.db")
    parser.add_argument("--json", action="store_true", help="Emit JSON")
    args = parser.parse_args()

    conn = _connect(args.db)
    try:
        report = build_report(conn)
    finally:
        conn.close()

    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print_report(report)


if __name__ == "__main__":
    main()
