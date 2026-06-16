#!/usr/bin/env python3
"""Find hidden decomp work: stale verdicts and missing implementations.

Identifies functions that need work but are hidden behind stale COMPLETE/AT_LIMIT
verdicts, plus functions in the target binary with no decomp implementation at all.

Usage:
    python3 scripts/find_hidden_work.py                  # report only
    python3 scripts/find_hidden_work.py --demote 80      # demote COMPLETE < 80% to workable
    python3 scripts/find_hidden_work.py --demote 80 --dry-run  # preview demotion
"""
from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from collections import Counter
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_REPORT = REPO_ROOT / "build" / "373307D9" / "report.json"
DEFAULT_DB = REPO_ROOT / "decomp.db"

# Units that are third-party / SDK code (not our decomp targets)
SDK_UNIT_PATTERNS = [
    "%bink%", "%xdk/%", "%xnet/%", "%xapilib%", "%libcmt%", "%libcpmt%",
    "%d3d/%", "%xaudio/%", "%tomcrypt/%", "%zlib/%", "%oggvorbis/%",
    "%nui_core%", "%auto_%", "%sapi/%", "%srdrv%", "%x2voice%", "%x2engine%",
    "%c30sw%", "%femanager%", "%textnorm%", "%filter%", "%shader%",
    "%cfglexicon%", "%phone/%", "%msasrx%", "%rtresults%", "%ccodec%",
    "%xspeech%", "%spphrase%", "%ctransducer%", "%crtvmx%", "%crtgpr%",
    "%crtfpr%", "%cconstant%", "%irinst%", "%import/%", "%constreg%",
    "%srrecomaster%", "%keygen%", "%xonline%", "%curl/%",
]


def is_sdk_unit(unit_name: str) -> bool:
    """Check if a unit is SDK/third-party code."""
    return any(p.strip("%") in unit_name for p in SDK_UNIT_PATTERNS)


def find_stale_verdicts(conn: sqlite3.Connection, threshold: float = 80.0) -> list[dict]:
    """Find functions marked COMPLETE but below threshold match%."""
    rows = conn.execute("""
        SELECT id, symbol, demangled, current_percent, unit, size, verdict
        FROM functions
        WHERE verdict = 'COMPLETE' AND excluded = 0
        AND current_percent IS NOT NULL AND current_percent < ?
        AND symbol NOT LIKE 'merged\\_%' ESCAPE '\\'
        ORDER BY current_percent ASC
    """, (threshold,)).fetchall()
    return [dict(r) for r in rows]


def find_stale_at_limit(conn: sqlite3.Connection, threshold: float = 60.0) -> list[dict]:
    """Find functions marked AT_LIMIT but below threshold (likely gave up too early)."""
    rows = conn.execute("""
        SELECT id, symbol, demangled, current_percent, unit, size, verdict
        FROM functions
        WHERE verdict = 'AT_LIMIT' AND excluded = 0
        AND current_percent IS NOT NULL AND current_percent < ?
        AND symbol NOT LIKE 'merged\\_%' ESCAPE '\\'
        ORDER BY current_percent ASC
    """, (threshold,)).fetchall()
    return [dict(r) for r in rows]


def find_missing_implementations(report_path: Path) -> list[dict]:
    """Find functions in target .obj with no decomp implementation.

    These show up in report.json with fuzzy_match_percent == null.
    """
    with open(report_path) as f:
        data = json.load(f)

    missing = []
    for unit in data.get("units", []):
        unit_name = unit["name"]
        if is_sdk_unit(unit_name):
            continue

        for fn in unit.get("functions", []):
            mp = fn.get("fuzzy_match_percent")
            if mp is None:
                missing.append({
                    "unit": unit_name.split("/")[-1],
                    "unit_full": unit_name,
                    "symbol": fn["name"],
                    "demangled": fn.get("metadata", {}).get("demangled_name", fn["name"]),
                    "size": int(fn.get("size", "0")),
                })

    return missing


def demote_functions(conn: sqlite3.Connection, func_ids: list[int], dry_run: bool) -> int:
    """Reset verdict from COMPLETE to NULL (workable) for given function IDs."""
    if dry_run or not func_ids:
        return len(func_ids)

    conn.executemany(
        """UPDATE functions
           SET verdict = NULL, verdict_reason = 'demoted: stale COMPLETE below threshold',
               updated_at = CURRENT_TIMESTAMP
           WHERE id = ?""",
        [(fid,) for fid in func_ids],
    )
    conn.commit()
    return len(func_ids)


def main():
    p = argparse.ArgumentParser(description="Find hidden decomp work")
    p.add_argument("--db", type=Path, default=DEFAULT_DB)
    p.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    p.add_argument("--demote", type=float, default=None, metavar="PCT",
                   help="Demote COMPLETE functions below this %% to workable")
    p.add_argument("--dry-run", action="store_true",
                   help="Preview changes without writing")
    p.add_argument("--verbose", "-v", action="store_true")
    args = p.parse_args()

    conn = sqlite3.connect(str(args.db))
    conn.row_factory = sqlite3.Row

    # === Part 1: Stale COMPLETE verdicts ===
    print("=" * 70)
    print("  STALE COMPLETE VERDICTS (marked COMPLETE but far from 100%)")
    print("=" * 70)

    ranges = [(0, 10), (10, 30), (30, 50), (50, 70), (70, 80), (80, 90), (90, 95)]
    for lo, hi in ranges:
        cnt = conn.execute("""
            SELECT count(*) FROM functions
            WHERE verdict = 'COMPLETE' AND excluded = 0
            AND current_percent >= ? AND current_percent < ?
            AND symbol NOT LIKE 'merged\\_%' ESCAPE '\\'
        """, (lo, hi)).fetchone()[0]
        if cnt > 0:
            print(f"  {lo:3d}-{hi:3d}%: {cnt:5d} functions")

    # Show the worst ones
    stale = find_stale_verdicts(conn, threshold=50.0)
    game_stale = [s for s in stale if not is_sdk_unit(s["unit"] or "")]
    if game_stale:
        print(f"\n  Worst offenders (COMPLETE < 50%, game code): {len(game_stale)}")
        for s in game_stale[:20]:
            name = (s["demangled"] or s["symbol"])[:55]
            unit = (s["unit"] or "").split("/")[-1]
            print(f"    {s['current_percent']:5.1f}% | {unit:20s} | {name}")
        if len(game_stale) > 20:
            print(f"    ... and {len(game_stale) - 20} more")

    # === Part 2: Stale AT_LIMIT verdicts ===
    print()
    print("=" * 70)
    print("  STALE AT_LIMIT VERDICTS (gave up too early?)")
    print("=" * 70)
    stale_al = find_stale_at_limit(conn, threshold=60.0)
    game_stale_al = [s for s in stale_al if not is_sdk_unit(s["unit"] or "")]
    print(f"  AT_LIMIT < 60% (game code): {len(game_stale_al)}")
    for s in game_stale_al[:15]:
        name = (s["demangled"] or s["symbol"])[:55]
        unit = (s["unit"] or "").split("/")[-1]
        print(f"    {s['current_percent']:5.1f}% | {unit:20s} | {name}")

    # === Part 3: Missing implementations ===
    if args.report.exists():
        print()
        print("=" * 70)
        print("  MISSING IMPLEMENTATIONS (in target, no decomp source)")
        print("=" * 70)

        missing = find_missing_implementations(args.report)
        if missing:
            # Group by unit
            by_unit = Counter(m["unit"] for m in missing)
            print(f"  {len(missing)} functions across {len(by_unit)} units")
            print()
            for unit, cnt in by_unit.most_common(20):
                total_size = sum(m["size"] for m in missing if m["unit"] == unit)
                print(f"    {cnt:4d} | {total_size:7d}B | {unit}")

            # Show individual missing functions
            if args.verbose:
                print()
                non_trivial = [m for m in missing if m["size"] > 20
                               and "??_9" not in m["symbol"]
                               and "??_G" not in m["symbol"]]
                print(f"  Non-trivial missing ({len(non_trivial)}):")
                for m in sorted(non_trivial, key=lambda x: x["size"], reverse=True)[:30]:
                    print(f"    {m['size']:5d}B | {m['unit']:20s} | {m['demangled'][:55]}")
        else:
            print("  No missing implementations found!")
    else:
        print(f"\n  Warning: report.json not found at {args.report}")
        print("  Run: ninja build/373307D9/report.json")

    # === Part 4: Demote if requested ===
    if args.demote is not None:
        print()
        print("=" * 70)
        print(f"  DEMOTION: COMPLETE < {args.demote}% -> workable")
        print("=" * 70)

        to_demote = find_stale_verdicts(conn, threshold=args.demote)
        game_demote = [s for s in to_demote if not is_sdk_unit(s["unit"] or "")]

        if game_demote:
            func_ids = [s["id"] for s in game_demote]
            mode = " (DRY RUN)" if args.dry_run else ""
            count = demote_functions(conn, func_ids, args.dry_run)
            print(f"  Demoted {count} functions{mode}")
        else:
            print("  Nothing to demote.")

    conn.close()


if __name__ == "__main__":
    main()
