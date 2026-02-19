#!/usr/bin/env python3
"""Run objdiff on every function and sync results to decomp.db.

Unlike sync_match_percent.py (which reads report.json for just match%),
this script runs `objdiff-cli diff` per-function to get instruction-level
diffs, then detects patterns and updates enrichment columns:

  - current_percent, best_percent, size, demangled
  - has_linker_merged, has_bool_mask, primary_pattern, reachable_100
  - verdict (COMPLETE for 100%, optionally AT_LIMIT for flagged patterns)

Usage:
    python3 scripts/sync_objdiff.py                         # full scan
    python3 scripts/sync_objdiff.py --unit 'system/char/*'  # filter by unit
    python3 scripts/sync_objdiff.py --dry-run               # preview
    python3 scripts/sync_objdiff.py -j8                     # parallel workers
    python3 scripts/sync_objdiff.py --skip-100              # skip already-COMPLETE
"""
from __future__ import annotations

import argparse
import json
import re
import sqlite3
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
OBJDIFF_CLI = REPO_ROOT / "bin" / "objdiff-cli"
DEFAULT_DB = REPO_ROOT / "decomp.db"

SDK_UNIT_PREFIXES = [
    "default/xdk/",
]

# Itanium ABI mangled name pattern: MethodName__<N><ClassName><params>
_ITANIUM_PATTERN = re.compile(r'^(.+?)__(\d+)(\w+)')


def demangle_itanium(symbol: str) -> str | None:
    """Demangle Itanium-style name to ClassName::MethodName."""
    if symbol.startswith("?") or "::" in symbol:
        return None
    m = _ITANIUM_PATTERN.match(symbol)
    if not m:
        return None
    method, class_len_str, rest = m.group(1), m.group(2), m.group(3)
    class_len = int(class_len_str)
    if class_len > len(rest) or class_len == 0:
        return None
    class_name = rest[:class_len]
    if method == "__ct":
        method = class_name
    elif method == "__dt":
        method = f"~{class_name}"
    return f"{class_name}::{method}"


@dataclass
class FunctionResult:
    """Result of running objdiff on a single function."""
    db_id: int
    symbol: str
    match_percent: float | None = None
    size: int | None = None
    demangled: str | None = None
    has_merged: bool = False
    has_bool_mask: bool = False
    primary_pattern: str | None = None
    error: str | None = None


def run_objdiff_for_function(db_id: int, symbol: str, project_dir: str) -> FunctionResult:
    """Run objdiff-cli diff on a single function and extract results."""
    result = FunctionResult(db_id=db_id, symbol=symbol)

    lookup = symbol
    demangled = demangle_itanium(symbol)
    if demangled is not None:
        lookup = demangled

    try:
        proc = subprocess.run(
            [str(OBJDIFF_CLI), "diff", "-p", project_dir,
             lookup, "--include-instructions", "-f", "json"],
            capture_output=True, text=True, timeout=60,
        )

        if proc.returncode != 0 or "Symbol not found" in proc.stdout:
            result.error = "not_found"
            return result

        data = json.loads(proc.stdout)
    except subprocess.TimeoutExpired:
        result.error = "timeout"
        return result
    except json.JSONDecodeError:
        result.error = "bad_json"
        return result
    except Exception as e:
        result.error = str(e)
        return result

    result.match_percent = data.get("fuzzy_match_percent")
    result.size = data.get("target_size") or data.get("base_size")
    result.demangled = data.get("demangled")

    # Skip unimplemented (no base object)
    if data.get("base_size", 0) == 0:
        result.error = "unimplemented"
        return result

    # Analyze instructions for patterns
    instructions = data.get("instructions", [])
    if instructions:
        _analyze_instructions(result, instructions)

    return result


def _analyze_instructions(result: FunctionResult, instructions: list[dict]) -> None:
    """Detect patterns from instruction diff."""
    has_merged = False
    has_bool_mask = False
    has_regswap = False
    has_fma = False
    has_offset_mismatch = False

    mismatches = [i for i in instructions if i.get("match_type") != "equal"]
    if not mismatches:
        return

    for ins in instructions:
        match_type = ins.get("match_type", "equal")
        if match_type == "equal":
            continue

        tgt = ins.get("target", {}) or {}
        base = ins.get("base", {}) or {}
        tgt_op = (tgt.get("opcode") or "").strip()
        base_op = (base.get("opcode") or "").strip()
        tgt_args = (tgt.get("args") or "").strip()
        base_args = (base.get("args") or "").strip()

        # Merged symbol detection
        if "merged_" in tgt_args or "merged_" in base_args:
            has_merged = True

        # Bool mask detection (clrlwi r, r, 24)
        if match_type in ("insert", "delete"):
            side = base if match_type == "insert" else tgt
            op = (side.get("opcode") or "").strip() if side else ""
            args = (side.get("args") or "").strip() if side else ""
            if op in ("clrlwi", "clrlwi."):
                parts = [p.strip() for p in args.split(",")]
                if len(parts) >= 3:
                    try:
                        if int(parts[2], 0) in (24, 31):
                            has_bool_mask = True
                    except ValueError:
                        pass
            # rlwinm form of bool mask
            if op in ("rlwinm", "rlwinm."):
                parts = [p.strip() for p in args.split(",")]
                if len(parts) >= 5:
                    try:
                        sh, mb, me = int(parts[2], 0), int(parts[3], 0), int(parts[4], 0)
                        if sh == 0 and ((mb == 24 and me == 31) or (mb == 31 and me == 31)):
                            has_bool_mask = True
                    except ValueError:
                        pass

        # Register swap detection (same opcode, different registers)
        if match_type == "diff_arg" and tgt_op == base_op:
            has_regswap = True

        # FMA mismatch
        fma_ops = {"fmadds", "fmsubs", "fnmadds", "fnmsubs", "fmadd", "fmsub", "fnmadd", "fnmsub"}
        fmul_ops = {"fmuls", "fmul"}
        fadd_ops = {"fadds", "fsubs", "fadd", "fsub"}
        if match_type == "replace":
            if (tgt_op in fma_ops and base_op in (fmul_ops | fadd_ops)) or \
               (base_op in fma_ops and tgt_op in (fmul_ops | fadd_ops)):
                has_fma = True

        # Offset mismatch (same opcode, different offsets in memory operands)
        if match_type == "diff_arg":
            if tgt_op == base_op and tgt_op in ("lwz", "stw", "lbz", "stb", "lhz", "sth",
                                                  "lfs", "stfs", "lfd", "stfd", "lwzx", "stwx",
                                                  "addi", "subi"):
                has_offset_mismatch = True

    # Set flags
    result.has_merged = has_merged
    result.has_bool_mask = has_bool_mask

    # Determine primary pattern (most impactful)
    if has_merged:
        result.primary_pattern = "LINKER_MERGED"
    elif has_fma:
        result.primary_pattern = "FMA_MISMATCH"
    elif has_bool_mask:
        result.primary_pattern = "BOOL_MASK"
    elif has_offset_mismatch:
        result.primary_pattern = "OFFSET_SWAP"
    elif has_regswap:
        result.primary_pattern = "REGISTER_SWAP"


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Run objdiff on all functions and sync to DB")
    p.add_argument("--db", type=Path, default=DEFAULT_DB)
    p.add_argument("--dry-run", action="store_true",
                   help="Preview changes without writing to DB")
    p.add_argument("--unit", type=str, default=None,
                   help="Only process functions in units matching this glob")
    p.add_argument("--min", type=float, default=None,
                   help="Minimum current_percent to include (e.g. 0, 50, 90)")
    p.add_argument("--max", type=float, default=None,
                   help="Maximum current_percent to include (e.g. 0, 99.9, 100)")
    p.add_argument("--skip-100", action="store_true",
                   help="Skip functions already marked COMPLETE")
    p.add_argument("--promote", action="store_true", default=True,
                   help="Promote 100%% matches to COMPLETE (default: true)")
    p.add_argument("--no-promote", action="store_false", dest="promote",
                   help="Don't promote 100%% matches")
    p.add_argument("-j", "--jobs", type=int, default=4,
                   help="Number of parallel workers (default: 4)")
    p.add_argument("-v", "--verbose", action="store_true")
    return p.parse_args()


def main():
    args = parse_args()

    if not OBJDIFF_CLI.exists():
        print(f"Error: objdiff-cli not found at {OBJDIFF_CLI}", file=sys.stderr)
        sys.exit(1)

    if not args.db.exists():
        print(f"Error: Database not found: {args.db}", file=sys.stderr)
        sys.exit(1)

    # Query functions from DB
    conn = sqlite3.connect(str(args.db))
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode = WAL")

    query = "SELECT id, symbol, unit, current_percent, verdict FROM functions WHERE 1=1"
    params: list = []

    # Exclude SDK
    for prefix in SDK_UNIT_PREFIXES:
        query += " AND unit NOT LIKE ?"
        params.append(f"{prefix}%")

    # Exclude merged symbols
    query += " AND symbol NOT LIKE 'merged_%'"

    if args.unit:
        pattern = args.unit
        if pattern.startswith("src/"):
            pattern = "default/" + pattern[4:]
        elif not pattern.startswith("default/") and not pattern.startswith("*"):
            pattern = "default/" + pattern
        query += " AND unit GLOB ?"
        params.append(pattern)

    if args.min is not None:
        if args.min == 0:
            query += " AND (current_percent IS NULL OR current_percent >= ?)"
        else:
            query += " AND current_percent >= ?"
        params.append(args.min)

    if args.max is not None:
        query += " AND current_percent <= ?"
        params.append(args.max)

    if args.skip_100:
        query += " AND (verdict IS NULL OR verdict != 'COMPLETE')"

    rows = conn.execute(query, params).fetchall()
    functions = [(row["id"], row["symbol"]) for row in rows]
    conn.close()

    print(f"Functions to scan: {len(functions)}")
    print(f"Workers: {args.jobs}")
    if args.dry_run:
        print("Mode: DRY RUN")
    print()

    # Run objdiff on each function
    project_dir = str(REPO_ROOT)
    results: list[FunctionResult] = []
    start_time = time.time()
    completed = 0

    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        futures = {
            pool.submit(run_objdiff_for_function, db_id, symbol, project_dir): symbol
            for db_id, symbol in functions
        }

        for future in as_completed(futures):
            completed += 1
            result = future.result()
            results.append(result)

            if completed % 500 == 0:
                elapsed = time.time() - start_time
                rate = completed / elapsed
                eta = (len(functions) - completed) / rate if rate > 0 else 0
                print(f"  [{completed}/{len(functions)}] {rate:.0f}/s, ETA {eta:.0f}s")

            if args.verbose and result.error:
                print(f"  SKIP {result.symbol}: {result.error}")

    elapsed = time.time() - start_time
    print(f"\nScan complete: {len(results)} functions in {elapsed:.1f}s ({len(results)/elapsed:.0f}/s)")

    # Compute stats
    stats = {
        "scanned": len(results),
        "matched": 0,
        "not_found": 0,
        "unimplemented": 0,
        "errors": 0,
        "pct_updated": 0,
        "promoted": 0,
        "merged_flagged": 0,
        "bool_mask_flagged": 0,
        "patterns_set": 0,
    }

    pct_updates: list[tuple] = []
    enrich_updates: list[tuple] = []
    promotions: list[int] = []

    for r in results:
        if r.error == "not_found":
            stats["not_found"] += 1
            continue
        if r.error == "unimplemented":
            stats["unimplemented"] += 1
            continue
        if r.error:
            stats["errors"] += 1
            continue

        stats["matched"] += 1

        if r.match_percent is not None:
            reachable = 1
            if r.has_merged or r.has_bool_mask:
                # Functions with unfixable patterns are not reachable to 100%
                # unless they're already at 100%
                if r.match_percent < 100.0:
                    reachable = 0

            pct_updates.append((
                r.match_percent,
                r.match_percent,
                r.size,
                r.demangled,
                r.db_id,
            ))
            enrich_updates.append((
                1 if r.has_merged else 0,
                1 if r.has_bool_mask else 0,
                r.primary_pattern,
                reachable,
                r.db_id,
            ))
            stats["pct_updated"] += 1

            if r.has_merged:
                stats["merged_flagged"] += 1
            if r.has_bool_mask:
                stats["bool_mask_flagged"] += 1
            if r.primary_pattern:
                stats["patterns_set"] += 1

            if args.promote and r.match_percent == 100.0:
                promotions.append(r.db_id)
                stats["promoted"] += 1

    # Apply to DB
    if not args.dry_run and (pct_updates or enrich_updates or promotions):
        conn = sqlite3.connect(str(args.db))
        conn.execute("PRAGMA journal_mode = WAL")

        if pct_updates:
            conn.executemany(
                """UPDATE functions
                   SET current_percent = ?,
                       best_percent = MAX(COALESCE(best_percent, 0), ?),
                       size = COALESCE(?, size),
                       demangled = COALESCE(?, demangled),
                       updated_at = CURRENT_TIMESTAMP
                   WHERE id = ?""",
                pct_updates,
            )

        if enrich_updates:
            conn.executemany(
                """UPDATE functions
                   SET has_linker_merged = ?,
                       has_bool_mask = ?,
                       primary_pattern = ?,
                       reachable_100 = ?,
                       updated_at = CURRENT_TIMESTAMP
                   WHERE id = ?""",
                enrich_updates,
            )

        if promotions:
            conn.executemany(
                """UPDATE functions
                   SET verdict = 'COMPLETE',
                       updated_at = CURRENT_TIMESTAMP
                   WHERE id = ?""",
                [(fid,) for fid in promotions],
            )

        conn.commit()
        conn.close()

    # Print summary
    mode = " (DRY RUN)" if args.dry_run else ""
    print(f"\n--- Sync Results{mode} ---")
    print(f"  Scanned:         {stats['scanned']}")
    print(f"  Matched:         {stats['matched']}")
    print(f"  Not found:       {stats['not_found']}")
    print(f"  Unimplemented:   {stats['unimplemented']}")
    print(f"  Errors:          {stats['errors']}")
    print(f"  Percent updated: {stats['pct_updated']}")
    print(f"  Promoted:        {stats['promoted']} (-> COMPLETE)")
    print(f"  Merged flagged:  {stats['merged_flagged']}")
    print(f"  Bool mask:       {stats['bool_mask_flagged']}")
    print(f"  Patterns set:    {stats['patterns_set']}")


if __name__ == "__main__":
    main()
