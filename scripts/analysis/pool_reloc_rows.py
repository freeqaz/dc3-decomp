#!/usr/bin/env python3
"""Real vs phantom MISMATCH ROW counts for the pool-reloc population.

A percentage does not adjudicate a verdict; a row count does.  `AT_LIMIT` with
reason "auto: all mismatches unfixable" was decided by walking the mismatch rows
`objdiff-cli diff` printed -- under the config that SYNTHESIZED pool
relocations out of each object's own symbol table.  This counts, per function,
how many charged rows that config produced and how many survive the corrected
one, so a verdict can be re-decided on the rows that actually exist.

`instruction_summary` from `diff --batch -f json` is the row census:
`diff_arg + diff_op + replace + delete + insert`.  The two runs differ in
exactly one config key, so the drop is attributable.

⚠ This does NOT itself prove a row is phantom in the "two identical
instructions" sense -- it proves the row exists only under the synthesized-
relocation config, which is the config the pattern doc established as wrong.
Spot-check individual rows with `run_diff_inspect(mode='mismatches')`.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from collections import Counter
from pathlib import Path

OLD = "ppc.calculatePoolRelocations=true"
ROW_KEYS = ("diff_arg", "diff_op", "replace", "delete", "insert")


def batch(repo: Path, symbols: list[str], extra: list[str]) -> dict:
    cmd = [str(repo / "bin" / "objdiff-cli"), "diff", "--batch", "-p", str(repo), "-f", "json"]
    for e in extra:
        cmd += ["-c", e]
    proc = subprocess.run(cmd, input="\n".join(symbols) + "\n",
                          capture_output=True, text=True, cwd=str(repo))
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr[-4000:])
        raise SystemExit(f"batch failed ({proc.returncode})")
    out = {}
    for line in proc.stdout.splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if "error" not in row:
            out[row["symbol"]] = row
    return out


def rowcount(row: dict) -> tuple[int, dict]:
    s = row.get("instruction_summary") or {}
    per = {k: int(s.get(k, 0) or 0) for k in ROW_KEYS}
    return sum(per.values()), per


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", required=True)
    ap.add_argument("--population", required=True)
    ap.add_argument("--out")
    args = ap.parse_args()

    repo = Path(args.repo).resolve()
    pop = json.load(open(args.population))["population"]
    syms = [p["symbol"] for p in pop]

    now = batch(repo, syms, [])
    old = batch(repo, syms, [OLD])

    results, skipped = [], 0
    for p in pop:
        rn, ro = now.get(p["symbol"]), old.get(p["symbol"])
        if rn is None or ro is None:
            skipped += 1
            continue
        cn, pern = rowcount(rn)
        co, _ = rowcount(ro)
        v = rn.get("verdict") or {}
        results.append(dict(
            **{k: p[k] for k in ("symbol", "unit", "size", "report_mpn",
                                 "old_diff_pct", "now_diff_pct", "delta_pp",
                                 "real_gap_bytes", "verdict", "verdict_reason")},
            rows_old=co, rows_now=cn, rows_phantom=co - cn, breakdown_now=pern,
            objdiff_class=v.get("classification"),
            objdiff_expl=v.get("explanation"),
            patterns=[q.get("pattern") for q in (rn.get("analysis") or {}).get("patterns", [])],
        ))

    results.sort(key=lambda r: (r["rows_now"], -r["real_gap_bytes"]))
    zero = [r for r in results if r["rows_now"] == 0]
    print(f"# {len(results)} functions examined ({skipped} unresolved by batch)")
    print(f"# {sum(r['rows_phantom'] for r in results)} phantom rows removed | "
          f"{sum(r['rows_now'] for r in results)} real rows remain "
          f"(was {sum(r['rows_old'] for r in results)})")
    print(f"# entire mismatch set was phantom on {len(zero)} functions "
          f"({sum(r['size'] for r in zero)} B)")
    print("# real-row histogram: " + ", ".join(
        f"{k}->{v}" for k, v in sorted(Counter(r["rows_now"] for r in results).items())[:14]))
    print()
    print("%8s %8s %7s %6s %9s %-10s %-20s %s"
          % ("rows_now", "rows_old", "phantom", "size", "report", "verdict", "objdiff_class", "symbol"))
    for r in results:
        print("%8d %8d %7d %6d %9.4f %-10s %-20s %s"
              % (r["rows_now"], r["rows_old"], r["rows_phantom"], r["size"],
                 r["report_mpn"], str(r["verdict"]), str(r["objdiff_class"])[:20],
                 r["symbol"][:64]))
    if args.out:
        Path(args.out).write_text(json.dumps(results, indent=2))
        print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
