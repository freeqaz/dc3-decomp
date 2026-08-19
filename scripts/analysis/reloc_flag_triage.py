#!/usr/bin/env python3
"""Partition the reloc-sensitive pattern buckets into actionable vs artifact.

The `has_linker_merged` / `has_prologue_mismatch` / `has_makestring_mismatch`
columns are populated from a `functionRelocDiffs=all` pass, which charges EVERY
relocation-name difference -- including the thousands the project has already
adjudicated as legitimate `/OPT:ICF` folds and registered in
`build/373307D9/icf_aliases.map`.

The discriminator is to re-run the same detectors under the project's GRADED
ruler, `functionRelocDiffs=name_check`, which consults that map and applies the
placeholder/counter/anchor exemptions. A pattern that survives `name_check` is
charged against the score; one that vanishes is a fold the project has already
proven and is artifact for triage purposes.

    class A  flag under `all`, gone under `name_check`   -> forgiven fold
    class B  flag survives, but match_percent_normalized == 100
                -> the ONLY thing wrong is a name; either an unregistered fold
                   or a wrong callee. THE PRIZE.
    class C  flag survives and normalized < 100
                -> the function has structural mismatches too; the flag is a
                   co-symptom, not the limiting factor.

Usage:
    reloc_flag_triage.py --all-jsonl a.jsonl --namecheck-jsonl n.jsonl \\
        --report build/373307D9/report.json --pattern LINKER_MERGED
"""
from __future__ import annotations

import argparse
import collections
import json
import sys


def load(path):
    out = {}
    with open(path) as fh:
        for line in fh:
            r = json.loads(line)
            out[r["symbol"]] = r
    return out


def pats(row):
    return {p["pattern"] for p in (row.get("patterns") or [])}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--all-jsonl", required=True)
    ap.add_argument("--namecheck-jsonl", required=True)
    ap.add_argument("--pattern", required=True)
    ap.add_argument("--list", choices=["A", "B", "C"], default=None)
    ap.add_argument("--json-out", default=None)
    args = ap.parse_args()

    # objdiff spells MakeString two ways; accept both.
    wanted = {args.pattern}
    if args.pattern.startswith("MAKESTRING"):
        wanted.add("MAKE_STRING_TEMPLATE_MISMATCH")

    A = load(args.all_jsonl)
    N = load(args.namecheck_jsonl)

    cls = collections.Counter()
    rows = {"A": [], "B": [], "C": []}
    for sym, ra in A.items():
        if not (pats(ra) & wanted):
            continue
        rn = N.get(sym)
        if rn is None:
            cls["missing_namecheck"] += 1
            continue
        survives = bool(pats(rn) & wanted)
        norm = rn.get("normalized")
        if norm is None:
            norm = 0.0
        rec = {"symbol": sym, "unit": rn.get("unit"),
               "size": rn.get("target_size") or 0,
               "norm": norm, "fuzzy": rn.get("fuzzy"),
               "patterns_namecheck": sorted(pats(rn)),
               "detail": [p for p in (rn.get("patterns") or [])
                          if p["pattern"] in wanted]}
        if not survives:
            cls["A forgiven fold (gone under name_check)"] += 1
            rows["A"].append(rec)
        elif norm >= 100.0:
            cls["B charged, normalized==100 (PRIZE)"] += 1
            rows["B"].append(rec)
        else:
            cls["C charged, normalized<100 (co-symptom)"] += 1
            rows["C"].append(rec)

    total = sum(cls.values())
    print(f"=== {args.pattern}: {total} rows carrying the flag under "
          f"functionRelocDiffs=all")
    for k, v in sorted(cls.items()):
        print(f"  {v:5d}  {100.0 * v / max(total, 1):5.1f}%  {k}")

    if args.list:
        for r in sorted(rows[args.list], key=lambda r: -r["size"]):
            print(f"  norm={r['norm']:6.2f} {r['size']:6d} {r['unit']} :: "
                  f"{r['symbol']}")
    if args.json_out:
        with open(args.json_out, "w") as fh:
            json.dump(rows, fh, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
