#!/usr/bin/env python3
"""Rank native non-matching functions by reload-vs-forward mismatch count.

A "reload-vs-forward" row is a diff_arg row where the image reads a value from
memory (a member or a stack slot) and our side passes a register it already
holds. The image reloading a value we forward is the signature of a source that
kept something in a local. Reading it at the use site fixes it. Two rows in the
2026-11 wave, BoxMapLighting::CacheData and NgLight::SphereConeTest, moved 14.6
and 1.3 points on exactly this, which is why it is worth a census.

Read-only: runs objdiff-cli on already-built objects. No ninja, no db writes.

Usage:
    python3 scripts/analysis/reload_census.py [--min-percent P] [--limit N]
"""
import argparse
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.abspath(__file__)
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
OBJDIFF = os.path.join(REPO, "bin", "objdiff-cli")
BUILD = os.path.join(REPO, "build", "373307D9")


def native_units():
    path = os.path.join(REPO, "native", "build", "compile_commands.json")
    units = set()
    for entry in json.load(open(path)):
        match = re.search(r"/src/(.+)\.(cpp|cc|c)$", entry["file"])
        if match:
            units.add("default/" + match.group(1))
    return units


def obj_paths(unit):
    rel = unit.split("/", 1)[1]
    return (
        os.path.join(BUILD, "obj", rel + ".obj"),
        os.path.join(BUILD, "src", rel + ".obj"),
    )


LOADS = {
    "lwz", "lwzu", "lwzx", "lbz", "lbzu", "lbzx", "lhz", "lhzu", "lhzx",
    "lha", "lhau", "lhax", "lfs", "lfsu", "lfsx", "lfd", "lfdu", "lfdx",
    "lvx", "lvxl", "lvsl", "lvsr", "ld", "ldu", "ldx", "lwarx",
}


def is_load(inst):
    return (inst.get("opcode") or "") in LOADS


def diff_one(job):
    unit, name = job
    tgt, base = obj_paths(unit)
    if not os.path.exists(tgt) or not os.path.exists(base):
        return None
    cmd = [
        OBJDIFF, "diff", "-1", tgt, "-2", base,
        "--include-instructions", "-f", "json",
        "-c", "functionRelocDiffs=name_check", name,
    ]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    except Exception:
        return None
    if out.returncode != 0:
        return None
    try:
        data = json.loads(out.stdout)
    except Exception:
        return None
    ins = data.get("instructions") or []
    diff = 0
    tgt_loads = 0
    base_loads = 0
    for row in ins:
        if is_load(row.get("target") or {}):
            tgt_loads += 1
        if is_load(row.get("base") or {}):
            base_loads += 1
        if row.get("match_type") == "equal":
            continue
        diff += 1
    return (unit, name, len(ins), diff, tgt_loads, base_loads)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--min-percent", type=float, default=0.0)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--workers", type=int, default=12)
    args = ap.parse_args()

    units = native_units()
    report = json.load(open(os.path.join(BUILD, "report.json")))
    meta = {}
    jobs = []
    for u in report["units"]:
        if u["name"] not in units:
            continue
        for f in u.get("functions", []):
            pct = f.get("match_percent_normalized")
            if pct is None:
                continue
            if pct >= 100.0 or pct < args.min_percent:
                continue
            key = (u["name"], f["name"])
            meta[key] = (int(f.get("size") or 0), pct)
            jobs.append(key)
    if args.limit:
        jobs = jobs[: args.limit]
    print("examining %d native functions" % len(jobs), file=sys.stderr)

    rows = []
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        for result in pool.map(diff_one, jobs):
            if result:
                rows.append(result)

    rows.sort(key=lambda r: (-(r[4] - r[5]), -meta[(r[0], r[1])][0]))

    header = "%-4s %-4s %-5s %-6s %-6s %-9s %s" % (
        "extra", "tgt", "base", "diff", "ins", "pct", "symbol")
    print(header)
    shown = 0
    for unit, name, nin, diff, tl, bl in rows:
        surplus = tl - bl
        if surplus <= 0:
            continue
        size, pct = meta[(unit, name)]
        print("%-4d %-4d %-5d %-6d %-6d %-9.3f %-52s [%s]" % (
            surplus, tl, bl, diff, nin, pct, name[:52], unit.split("/", 1)[1]))
        shown += 1
    print("functions where the image loads MORE than we do: %d of %d examined"
          % (shown, len(jobs)), file=sys.stderr)


if __name__ == "__main__":
    main()
