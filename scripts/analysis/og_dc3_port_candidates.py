#!/usr/bin/env python3
"""Find og-dc3-decomp port candidates: functions where the parallel decomp
(../og-dc3-decomp) matches BETTER than we do.

Both repos target the same binary with the same compiler, so og-dc3% > our%
necessarily means og-dc3's *source differs in a helpful way* — porting it can
raise our match. This is the precise signal (no separate source-diff needed).

Usage:
  python3 scripts/analysis/og_dc3_port_candidates.py            # summary + top worklist
  python3 scripts/analysis/og_dc3_port_candidates.py --json out.json
  python3 scripts/analysis/og_dc3_port_candidates.py --min-gain 5 --our-max 99.99
"""
import argparse, json, os, sqlite3

OURS = "build/373307D9/report.json"
OG = "/home/free/code/milohax/og-dc3-decomp/build/373307D9/report.json"


def norm_pct(f):
    # prefer normalized match; fall back to fuzzy
    v = f.get("match_percent_normalized")
    if v is None:
        v = f.get("fuzzy_match_percent")
    return v if v is not None else 0.0


def load(path):
    r = json.load(open(path))
    out = {}
    for u in r["units"]:
        if tuple(u.get("metadata", {}).get("progress_categories", [])) == ("sdk",):
            continue
        for f in u.get("functions", []):
            if int(f.get("size", 0)) == 0:
                continue
            out[f["name"]] = (norm_pct(f), int(f.get("size", 0)),
                              u["name"].replace("default/", ""),
                              f.get("metadata", {}).get("demangled_name", ""))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", default=OURS)
    ap.add_argument("--og", default=OG)
    ap.add_argument("--min-gain", type=float, default=2.0,
                    help="min (og%% - our%%) to count as a candidate")
    ap.add_argument("--our-max", type=float, default=99.99,
                    help="only consider our functions below this %%")
    ap.add_argument("--json", help="write full candidate list to this path")
    args = ap.parse_args()

    ours = load(args.ours)
    og = load(args.og)
    print(f"ours: {len(ours)} fns | og-dc3: {len(og)} fns")

    buckets = {"og100_we<100 (PRIME)": [], "og>us +{}%".format(int(args.min_gain)): [],
               "og~us (floor: both stuck)": [], "we>=og (we're ahead/equal)": [],
               "og missing": []}
    cands = []
    for sym, (ourp, size, unit, dem) in ours.items():
        if ourp >= args.our_max:
            continue
        if sym not in og:
            buckets["og missing"].append(size); continue
        ogp = og[sym][0]
        gain = ogp - ourp
        if ogp >= 99.99 and ourp < 99.99:
            buckets["og100_we<100 (PRIME)"].append(size)
            cands.append((gain, ourp, ogp, size, unit, dem or sym, sym, "PRIME"))
        elif gain >= args.min_gain:
            buckets["og>us +{}%".format(int(args.min_gain))].append(size)
            cands.append((gain, ourp, ogp, size, unit, dem or sym, sym, "GAIN"))
        elif gain > -args.min_gain:
            buckets["og~us (floor: both stuck)"].append(size)
        else:
            buckets["we>=og (we're ahead/equal)"].append(size)

    print("\n=== Buckets (our fns < %.2f%%) ===" % args.our_max)
    for k, v in buckets.items():
        print(f"  {k:<32} {len(v):5} fns  {sum(v):9,} bytes")

    cands.sort(key=lambda c: (-(c[7] == "PRIME"), -c[0] * c[3]))  # prime first, then gain*size
    print(f"\n=== Port candidates: {len(cands)} (og-dc3 beats us by >= {args.min_gain}%) ===")
    print(f"{'tag':<6}{'our%':>7}{'og%':>7}{'gain':>7}{'size':>7}  unit : fn")
    for gain, ourp, ogp, size, unit, dem, sym, tag in cands[:40]:
        print(f"{tag:<6}{ourp:7.1f}{ogp:7.1f}{gain:7.1f}{size:7}  {unit}: {dem[:42]}")

    if args.json:
        json.dump([dict(symbol=c[6], unit=c[4], demangled=c[5], our=c[1], og=c[2],
                        gain=c[0], size=c[3], tag=c[7]) for c in cands],
                  open(args.json, "w"), indent=1)
        print(f"\nWrote {len(cands)} candidates to {args.json}")


if __name__ == "__main__":
    main()
