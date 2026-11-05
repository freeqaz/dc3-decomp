#!/usr/bin/env python3
"""Find native rows whose CALL TARGETS really differ from the image.

The trap this exists for: objdiff shows callee names, and the linker folds
identical functions (ICF), so the image's name for a call can differ from ours
while the call is to the very same address. SongSortNode::Text was flagged as
calling CampaignEra::GetIntroMovie where our source calls
SongMetadata::GameOrigin, and both are at 0x82919ef0 in ham_xbox_r.map.

So this census resolves every call target through the map to an ADDRESS and
compares address multisets. A row only appears here if the calls differ by
address, which ICF cannot explain.

Read-only: objdiff-cli on built objects plus the map. No ninja, no db writes.

Usage:
    python3 scripts/analysis/callee_divergence_census.py [--min-percent P] [--limit N]
"""
import argparse
import collections
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
MAP = os.path.join(REPO, "orig", "373307D9", "ham_xbox_r.map")

CALL_OPS = {"bl", "bla", "bctrl", "bclrl"}

# " 0005:005e9ef0       ?GameOrigin@SongMetadata@@QBA?AVSymbol@@XZ 82919ef0 f   meta:SongMetadata.obj"
MAP_LINE = re.compile(r"^\s+[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s")


def load_map():
    name_to_addr = {}
    if not os.path.exists(MAP):
        return name_to_addr
    with open(MAP, errors="replace") as fh:
        for line in fh:
            m = MAP_LINE.match(line)
            if m:
                name_to_addr.setdefault(m.group(1), int(m.group(2), 16))
    return name_to_addr


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


def calls(inst):
    """Symbol names of call targets in one instruction, across arg list."""
    if (inst.get("opcode") or "") not in CALL_OPS:
        return []
    out = []
    for arg in inst.get("typed_args") or []:
        if arg.get("type") == "Symbol":
            out.append(arg.get("value"))
        elif arg.get("type") == "Relocation":
            out.append(arg.get("value"))
    return out


def diff_one(job, name_to_addr):
    unit, name = job
    tgt, base = obj_paths(unit)
    if not os.path.exists(tgt) or not os.path.exists(base):
        return None
    cmd = [
        OBJDIFF, "diff", "-1", tgt, "-2", base,
        "--include-instructions", "-f", "json",
        "-c", "functionRelocDiffs=name_check",
        "--map-file", MAP, name,
    ]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    except Exception:
        return None
    if out.returncode != 0:
        return None
    # objdiff-cli prints "Loaded N ICF equivalence entries ..." on stdout
    # before the JSON body, so the payload does not start at column 0.
    body = out.stdout.lstrip()
    if not body.startswith("{"):
        idx = body.find("{")
        if idx < 0:
            return None
        body = body[idx:]
    try:
        data = json.loads(body)
    except Exception:
        return None

    # objdiff has already applied the ICF map, so a call row that is still
    # diff_arg is a call objdiff could NOT fold. That is the thing worth
    # reporting; anything it folded is not a divergence.
    pairs = []
    for row in data.get("instructions") or []:
        if row.get("match_type") == "equal":
            continue
        t = row.get("target") or {}
        b = row.get("base") or {}
        if (t.get("opcode") or "") not in CALL_OPS:
            continue
        tsym = calls(t)
        bsym = calls(b)
        # Only a SUBSTITUTION is evidence: both sides call something, and
        # objdiff's ICF map could not reconcile them. A one-sided row is an
        # insert/delete pair from alignment, which says nothing about callees.
        if tsym and bsym:
            pairs.append((", ".join(tsym), ", ".join(bsym)))
    if not pairs:
        return None
    return (unit, name, pairs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--min-percent", type=float, default=0.0)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--workers", type=int, default=12)
    args = ap.parse_args()

    name_to_addr = load_map()
    print("map symbols: %d" % len(name_to_addr), file=sys.stderr)
    units = native_units()
    report = json.load(open(os.path.join(BUILD, "report.json")))
    meta = {}
    jobs = []
    for u in report["units"]:
        if u["name"] not in units:
            continue
        for f in u.get("functions", []):
            pct = f.get("match_percent_normalized")
            if pct is None or pct >= 100.0 or pct < args.min_percent:
                continue
            key = (u["name"], f["name"])
            meta[key] = (int(f.get("size") or 0), pct)
            jobs.append(key)
    if args.limit:
        jobs = jobs[: args.limit]
    print("examining %d native functions" % len(jobs), file=sys.stderr)

    rows = []
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        for result in pool.map(lambda j: diff_one(j, name_to_addr), jobs):
            if result:
                rows.append(result)

    rows.sort(key=lambda r: (-len(r[2]), -meta[(r[0], r[1])][1]))
    print("rows with a call objdiff could NOT fold through the ICF map: %d of %d examined"
          % (len(rows), len(jobs)))
    for unit, name, pairs in rows:
        size, pct = meta[(unit, name)]
        print("%-9.3f %-50s [%s]  %d call row(s)" % (
            pct, name[:50], unit.split("/", 1)[1], len(pairs)))
        for tdesc, bdesc in pairs[:4]:
            print("      image: %s" % tdesc)
            print("      ours : %s" % bdesc)


if __name__ == "__main__":
    main()
