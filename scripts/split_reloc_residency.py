#!/usr/bin/env python3
"""Split dc3's relocation-name charges by residency in retail's linker map.

The question this answers
-------------------------
`functionRelocDiffs=name_check` charges a site when the relocation at that
instruction names a different symbol in retail's object than in ours.  Two
completely different things produce that charge and they need opposite
treatment:

* Retail's linker FOLDED two byte-identical COMDATs (`/OPT:ICF`), so only one
  spelling survives in the image and retail's object can only ever name the
  survivor.  Ours names our own TU's spelling.  Nothing is wrong with our
  source; the alias map has simply not adjudicated that fold.
* Retail shipped TWO DISTINCT BODIES and we reference the wrong one.  That is a
  genuine source defect, and an alias here would MANUFACTURE a match.

`orig/373307D9/ham_xbox_r.map` -- the shipped MSVC linker map for this exact
build -- separates them, because it states an address for every public in the
image.  Four buckets:

    survivor mapped, ours unmapped   fold the alias map has not adjudicated
    both mapped, SAME address        fold where the map kept both names
    both mapped, DIFFERENT addresses WRONG REFERENCE -- a source defect
    neither mapped                   unclassifiable from the map

Ported from rb3-xenon, where the same split put 4,347 of 11,262 callee charges
on wrong-callee defects including that project's single biggest pair.

A note on what "unmapped" means
-------------------------------
An MSVC map lists PUBLICS.  A static/`internal`-linkage symbol, a symbol the
linker discarded entirely (`/OPT:REF`), and a symbol folded away by ICF are all
absent for different reasons, and the map does not distinguish them.  So
"ours unmapped" is CONSISTENT with a fold and is not a witness of one: it is a
candidate that some other evidence tier has to confirm.  What the map does
state decisively is the opposite direction -- two names at two addresses are
two bodies, and no alias may ever be minted across them.

Usage
-----
    python3 scripts/split_reloc_residency.py --sites <dc3_final_sites.jsonl> \
        --out <dir>
"""
import argparse
import collections
import json
import re
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_MAP = REPO / "orig" / "373307D9" / "ham_xbox_r.map"

# 0005:00001360       ??_GObjRef@@UAAPAXI@Z      82331360 f i App.obj
MAP_RX = re.compile(
    r"^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{8})\s+(.*)$")

# Annotation labels, not aliases -- the same rule gen_icf_alias_map.py applies.
ANNOTATION = re.compile(r"^(__unwind\$|__catch\$|\$L|\?\?_C@)")


def parse_map(path: Path):
    """name -> {addresses}, plus name -> owning object, from the publics table."""
    addrs = collections.defaultdict(set)
    owner = {}
    for line in path.open(errors="replace"):
        m = MAP_RX.match(line)
        if not m:
            continue
        name, addr, rest = m.group(3), m.group(4).lower(), m.group(5)
        addrs[name].add("0x" + addr)
        owner.setdefault(name, rest.split()[-1] if rest.split() else "")
    return addrs, owner


def classify(target, base, addrs):
    """Bucket one (retail name, our name) pair by map residency."""
    ta, ba = addrs.get(target), addrs.get(base)
    if ta and ba:
        if ta & ba:
            return "both_mapped_same_addr"
        if len(ta) > 1 or len(ba) > 1:
            return "ambiguous_multi_addr"
        return "both_mapped_diff_addr"
    if ta and not ba:
        return "survivor_mapped_ours_unmapped"
    if ba and not ta:
        return "ours_mapped_survivor_unmapped"
    return "neither_mapped"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sites", required=True)
    ap.add_argument("--map", default=str(DEFAULT_MAP))
    ap.add_argument("--lanes", default="different_data,different_function")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    lanes = set(args.lanes.split(","))
    addrs, owner = parse_map(Path(args.map))
    print(f"map: {len(addrs)} distinct public names, "
          f"{sum(len(v) for v in addrs.values())} rows, "
          f"{sum(1 for v in addrs.values() if len(v) > 1)} names at >1 address")

    rows = []
    for line in open(args.sites):
        r = json.loads(line)
        if r["lane"] in lanes and r.get("target") and r.get("base"):
            rows.append(r)
    print(f"{len(rows)} charges in lanes {sorted(lanes)}")

    # site-level and pair-level tallies
    bucket_sites = collections.Counter()
    bucket_pairs = collections.defaultdict(set)
    bucket_fns = collections.defaultdict(set)
    pair_rows = collections.defaultdict(list)
    for r in rows:
        b = classify(r["target"], r["base"], addrs)
        r["bucket"] = b
        bucket_sites[b] += 1
        bucket_pairs[b].add((r["target"], r["base"]))
        bucket_fns[b].add((r["unit"], r["func"]))
        pair_rows[(r["target"], r["base"])].append(r)

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    print(f"\n{'bucket':34} {'sites':>7} {'pairs':>7} {'fns':>7}")
    for b, n in bucket_sites.most_common():
        print(f"{b:34} {n:7} {len(bucket_pairs[b]):7} {len(bucket_fns[b]):7}")

    # two-cycles: (a,b) charged AND (b,a) charged
    allpairs = set(pair_rows)
    cycles = sorted({tuple(sorted(p)) for p in allpairs
                     if (p[1], p[0]) in allpairs})
    print(f"\ntwo-cycles: {len(cycles)}")

    with (out / "classified_sites.jsonl").open("w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")

    summary = {
        "buckets": {b: {"sites": n,
                        "pairs": len(bucket_pairs[b]),
                        "functions": len(bucket_fns[b])}
                    for b, n in bucket_sites.most_common()},
        "two_cycles": [{"a": a, "b": b,
                        "a_addr": sorted(addrs.get(a, [])),
                        "b_addr": sorted(addrs.get(b, [])),
                        "sites_ab": len(pair_rows.get((a, b), [])),
                        "sites_ba": len(pair_rows.get((b, a), []))}
                       for a, b in cycles],
    }
    (out / "summary.json").write_text(json.dumps(summary, indent=2))

    # per-bucket pair tables, ranked by charged-function count
    for b in bucket_sites:
        tbl = []
        for p in sorted(bucket_pairs[b]):
            rs = pair_rows[p]
            tbl.append({
                "target": p[0], "base": p[1],
                "sites": len(rs),
                "functions": len({(r["unit"], r["func"]) for r in rs}),
                "units": sorted({r["unit"] for r in rs}),
                "target_addr": sorted(addrs.get(p[0], [])),
                "base_addr": sorted(addrs.get(p[1], [])),
                "target_obj": owner.get(p[0], ""),
                "base_obj": owner.get(p[1], ""),
                "lanes": sorted({r["lane"] for r in rs}),
            })
        tbl.sort(key=lambda d: (-d["functions"], -d["sites"]))
        (out / f"pairs_{b}.json").write_text(json.dumps(tbl, indent=2))

    print(f"\nwrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
