#!/usr/bin/env python3
"""Dump the FULL detail payload of the reloc-sensitive objdiff patterns.

`scripts/backfill_reloc_patterns.py` populates the `has_*` boolean columns from
a `functionRelocDiffs=all` pass, but it throws away everything the detectors
actually said -- which merged symbol, which `__savegprlr_N` pair, which
`MakeString<>` instantiation. Triaging the resulting buckets by hand, one
`run_objdiff` at a time, is not affordable at 1,594 rows.

This is the same pass with the payload kept. It writes one JSON object per
function:

    {"symbol": ..., "unit": ..., "fuzzy": ..., "normalized": ...,
     "patterns": [ <the raw objdiff pattern objects> ]}

so a bucket can be classified in aggregate before anything is worked by hand.

Nothing here writes decomp.db.

Usage:
    python3 scripts/analysis/reloc_pattern_census.py \
        --db /path/to/main/decomp.db --project-dir . \
        --flag has_prologue_mismatch --out /tmp/prologue.jsonl
    # or an explicit symbol list on stdin with --stdin
"""
from __future__ import annotations

import argparse
import json
import math
import sqlite3
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
OBJDIFF_CLI = REPO_ROOT / "bin" / "objdiff-cli"

FLAGS = {
    "has_linker_merged": "LINKER_MERGED",
    "has_prologue_mismatch": "PROLOGUE_MISMATCH",
    "has_makestring_mismatch": "MAKESTRING_TEMPLATE_MISMATCH",
    "has_scope_counter_mismatch": "SCOPE_COUNTER_MISMATCH",
}


def _batch(symbols: list[str], project_dir: str, reloc: str) -> list[dict]:
    stdin_data = "\n".join(symbols) + "\n"
    try:
        proc = subprocess.run(
            [str(OBJDIFF_CLI), "diff", "-p", project_dir,
             "-c", f"functionRelocDiffs={reloc}", "--batch"],
            input=stdin_data, capture_output=True, text=True, timeout=1800)
    except Exception as e:  # noqa: BLE001
        return [{"symbol": s, "error": str(e)} for s in symbols]

    out = []
    seen = set()
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            data = json.loads(line)
        except json.JSONDecodeError:
            continue
        sym = data.get("symbol", "")
        seen.add(sym)
        analysis = data.get("analysis") or {}
        out.append({
            "symbol": sym,
            "unit": data.get("unit"),
            "demangled": data.get("demangled"),
            "fuzzy": data.get("fuzzy_match_percent"),
            "normalized": data.get("match_percent_normalized"),
            "target_size": data.get("target_size"),
            "base_size": data.get("base_size"),
            "patterns": analysis.get("patterns", []),
            "verdict": (data.get("verdict") or {}).get("classification"),
        })
    for s in symbols:
        if s not in seen:
            out.append({"symbol": s, "error": "not_found"})
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", default=str(REPO_ROOT / "decomp.db"))
    ap.add_argument("--project-dir", default=str(REPO_ROOT))
    ap.add_argument("--flag", choices=sorted(FLAGS), default=None)
    ap.add_argument("--stdin", action="store_true",
                    help="read symbols from stdin instead of the DB")
    ap.add_argument("--reloc", default="all")
    ap.add_argument("-j", "--jobs", type=int, default=8)
    ap.add_argument("--limit", type=int, default=None)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    if args.stdin:
        symbols = [ln.strip() for ln in sys.stdin if ln.strip()]
    else:
        if not args.flag:
            ap.error("--flag or --stdin required")
        conn = sqlite3.connect(args.db)
        symbols = [r[0] for r in conn.execute(
            f"SELECT symbol FROM functions WHERE {args.flag}=1 AND excluded=0 "
            f"ORDER BY symbol")]
    # The count printed below used to be the count AFTER the slice, so
    # `--limit 500` silently rewrote the denominator of everything downstream.
    # Keep the universe and say when the census is a sample.
    universe = len(symbols)
    truncated = bool(args.limit) and args.limit < universe
    if args.limit:
        symbols = symbols[:args.limit]
    if truncated:
        print(f"TRUNCATED by --limit: {len(symbols)} of {universe} symbols, "
              f"functionRelocDiffs={args.reloc} -- this run is a SAMPLE, "
              f"not a census")
    else:
        print(f"{len(symbols)} of {universe} symbols, "
              f"functionRelocDiffs={args.reloc}")

    chunk = max(1, math.ceil(len(symbols) / args.jobs))
    chunks = [symbols[i:i + chunk] for i in range(0, len(symbols), chunk)]
    results: list[dict] = []
    if len(chunks) == 1:
        results = _batch(chunks[0], args.project_dir, args.reloc)
    else:
        with ProcessPoolExecutor(max_workers=len(chunks)) as pool:
            futs = {pool.submit(_batch, c, args.project_dir, args.reloc): i
                    for i, c in enumerate(chunks)}
            for f in as_completed(futs):
                results.extend(f.result())
                print(f"  worker {futs[f] + 1}/{len(chunks)} done")

    with open(args.out, "w") as fh:
        for r in sorted(results, key=lambda r: r["symbol"]):
            fh.write(json.dumps(r) + "\n")
    print(f"wrote {len(results)} rows of {universe} symbols -> {args.out}")
    # Exit 3 == TRUNCATED, matching scripts/analysis/coverage.py.  A caller that
    # asked for a sample can ignore it; a caller that thought it got a census
    # cannot miss it.
    return 3 if truncated else 0


if __name__ == "__main__":
    sys.exit(main())
