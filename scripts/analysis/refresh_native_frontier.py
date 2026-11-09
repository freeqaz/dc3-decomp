#!/usr/bin/env python3
"""Re-measure unicorn verdicts over the native port's non-100 frontier.

WHY THIS EXISTS. Every stored `unicorn_verdict` in decomp.db was taken on or
before 2026-08-19 while hundreds of source commits landed since, so a verdict
read out of the database describes the source as it was then, not as it is. Two
conclusions were drawn from stale rows before this was noticed: a classifier
class was added to explain four `call_count` rows that no longer diverge at all
(added 5e340626d, retracted in the commit that introduced this script).

WHAT IT DOES. Selects every function under 100% that the native port compiles
(membership via native/build/compile_commands.json), re-runs each through
scripts/unicorn_runner/run.py, and writes one JSON line per row. It NEVER writes
decomp.db: the comparison is recorded so a human or a later ingest can act on it.

WORKERS. Default 4. This box runs other work; a 10-worker run pushed load to 18
on a 32-core machine for no wall-clock benefit worth having. Raise it
deliberately with -j, not by default.

USAGE
    python3 scripts/analysis/refresh_native_frontier.py --out /tmp/frontier.jsonl
    python3 scripts/analysis/refresh_native_frontier.py --out /tmp/f.jsonl -j 8
    python3 scripts/analysis/refresh_native_frontier.py --out /tmp/f.jsonl --detail

`--detail` additionally pulls the full JSON for every DIVERGENT row, which is
what the divergence classes need (the reason alone does not carry the operands).

RESULTS ARE RESUMABLE: rows already present in --out are skipped, so an
interrupted sweep continues rather than restarting.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_TIMEOUT = 180


def native_units() -> set[str]:
    """Unit names the native port compiles, i.e. what 'native frontier' means."""
    path = os.path.join(PROJECT_ROOT, "native", "build", "compile_commands.json")
    with open(path) as fh:
        entries = json.load(fh)
    units = set()
    for e in entries:
        f = e.get("file", "")
        marker = "/src/"
        if marker in f:
            rel = f[f.index(marker) + len(marker):]
            units.add("default/" + rel.rsplit(".", 1)[0])
    return units


def frontier_rows(min_pct=0.0, max_pct=100.0):
    """(unit, symbol, percent, size) for native rows below max_pct."""
    report = os.path.join(PROJECT_ROOT, "build", "373307D9", "report.json")
    with open(report) as fh:
        data = json.load(fh)
    units = native_units()
    rows = []
    for unit in data.get("units", []):
        if unit["name"] not in units:
            continue
        for fn in unit.get("functions", []):
            try:
                pct = float(fn.get("match_percent_normalized"))
            except (TypeError, ValueError):
                continue
            if min_pct <= pct < max_pct:
                rows.append({
                    "unit": unit["name"],
                    "symbol": fn["name"],
                    "pct": pct,
                    "size": int(fn.get("size", 0) or 0),
                })
    rows.sort(key=lambda r: r["pct"])
    return rows


def run_one(target, detail):
    cmd = [
        sys.executable, "-m", "scripts.unicorn_runner.run",
        "--unit", target["unit"],
        "--symbol", target["symbol"],
        "--json",
    ]
    try:
        proc = subprocess.run(
            cmd, cwd=PROJECT_ROOT, capture_output=True, text=True, timeout=DEFAULT_TIMEOUT
        )
    except subprocess.TimeoutExpired:
        return {**target, "verdict": "TIMEOUT", "reason": None, "note": "timeout"}

    start = proc.stdout.find("{")
    if start < 0:
        tail = (proc.stdout + proc.stderr).strip().splitlines()
        return {**target, "verdict": "SKIPPED", "reason": None,
                "note": tail[-1][:200] if tail else "no output"}
    try:
        parsed, _ = json.JSONDecoder().raw_decode(proc.stdout[start:])
    except json.JSONDecodeError as exc:
        return {**target, "verdict": "PARSE_ERROR", "reason": None, "note": str(exc)[:120]}

    details = parsed.get("details") or {}
    row = {
        **target,
        "verdict": parsed.get("verdict"),
        "reason": details.get("reason"),
        "decomp_calls": parsed.get("decomp_call_count"),
        "orig_calls": parsed.get("orig_call_count"),
    }
    if detail:
        row["result"] = parsed
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True, help="JSONL output; resumed if it exists")
    ap.add_argument("-j", "--jobs", type=int, default=4, help="workers (default 4)")
    ap.add_argument("--detail", action="store_true",
                    help="also store the full JSON per row (needed for divergence classes)")
    ap.add_argument("--max-pct", type=float, default=100.0)
    args = ap.parse_args()

    targets = frontier_rows(max_pct=args.max_pct)
    done = set()
    if os.path.exists(args.out):
        with open(args.out) as fh:
            for line in fh:
                try:
                    rec = json.loads(line)
                except json.JSONDecodeError:
                    continue
                done.add((rec.get("unit"), rec.get("symbol")))
    todo = [t for t in targets if (t["unit"], t["symbol"]) not in done]
    print(f"{len(targets)} frontier rows, {len(done)} already done, "
          f"{len(todo)} to run at {args.jobs} workers", flush=True)
    if not todo:
        return 0

    with open(args.out, "a") as fh, ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_one, t, args.detail) for t in todo]
        for n, fut in enumerate(as_completed(futures), 1):
            fh.write(json.dumps(fut.result()) + "\n")
            fh.flush()
            if n % 25 == 0 or n == len(todo):
                print(f"  {n}/{len(todo)}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
