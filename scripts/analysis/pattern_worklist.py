#!/usr/bin/env python3
"""Rank the LikelyFixable pattern rows by the bytes closing them would pay.

WHY A RANK AND NOT A LIST
=========================
`WRONG_CALLEE` and `TEMPLATE_INSTANTIATION_MISMATCH` are the two classes objdiff
4.2.6 marks `LikelyFixable`, and they are not equally worth fixing.  A wrong
callee on a function that is otherwise byte-perfect pays that function's ENTIRE
size the moment it is closed -- the row crosses into the matched set.  The same
bug on a row scoring 84% pays a fraction of a percentage point and leaves the
row exactly where it was.  Sorting the class by size, or by count, mixes those
two together and puts the second kind at the top.

THE DISCRIMINATOR
=================
Three sweeps over the same objects:

    name_check   the graded ruler, and report.json's.  Charges relocation NAMES
                 but forgives the ~2,992 adjudicated /OPT:ICF folds.
    all          charges every name difference AND addends.  Its extra rows over
                 name_check are the folds the project has already proven.
    none         relocation-BLIND.  Forgives every name difference.

`none` is useless for PATTERNS (the detectors cannot fire) but it is exactly
right for this question, because the gap between the blind score and the graded
score IS the price of the names:

    blind fuzzy == 100  ->  the name is the ONLY thing wrong with the function.
                            Closing it pays `size` bytes.  This is the prize.
    blind fuzzy <  100  ->  the function has structural mismatches too. Closing
                            the name is still a real fix (it is a call to a
                            different function, which no other oracle here can
                            see) but it does not cross the row.

The old `norm >= 100.0` test for this slice is no longer correct: since the
2026-08-20 objdiff fix a vetted relocation-name disagreement stays in
`diff_score` and DOES reach `match_percent_normalized` under name_check, so an
otherwise-perfect row now reads 99.9-something rather than exactly 100.  A test
written against the old behaviour silently reports the prize slice as empty.

Usage:
    python3 scripts/analysis/pattern_worklist.py \\
        --namecheck /tmp/census-name_check.jsonl \\
        --blind /tmp/blind-none.jsonl \\
        [--all /tmp/census-all.jsonl] [--json-out /tmp/worklist.json]
"""
from __future__ import annotations

import argparse
import collections
import json
import sys

import re

LIKELY_FIXABLE = ("WRONG_CALLEE", "TEMPLATE_INSTANTIATION_MISMATCH")

#: A concurrent lane owns the ">= 95%" slice of WRONG_CALLEE, seeded from
#: ?Set@PlayBack@CharLipSync@@.  That seed measures 94.421 normalized / 93.158
#: graded-fuzzy / 93.16 current_percent in this census, so it is >= 95 on NO
#: ruler available here and the boundary as stated does not close.  Rather than
#: guess, the band is opened DOWNWARD to include the seed and every row in it is
#: labelled -- never dropped.  A worklist that silently removes rows cannot be
#: reconciled against the census it came from.
OTHER_LANE_SEED = "?Set@PlayBack@CharLipSync@@QAAXPAV2@V?$ObjPtr@VObjectDir@@@@@Z"
OTHER_LANE_MIN_NORM = 93.0

#: objdiff pairs MSVC EH funclets BY BYTE SIGNATURE, so the funclet on the other
#: side routinely belongs to a DIFFERENT PARENT FUNCTION.  Its `bl` then names a
#: different symbol for a reason that has nothing to do with our source, and
#: `detect_callee_divergences` calls that WRONG_CALLEE: the classifier checks
#: whether the CALLEE is a splitter placeholder (that is UNVERIFIABLE_CALLEE_NAME)
#: but never whether the ENCLOSING symbol is one.  53 of the 59 rows that would
#: otherwise head this worklist are such funclets, at 40-44 bytes each.  They are
#: reported as their own tier, not mixed into the fixable ones.
_PLACEHOLDER_RE = re.compile(
    r"^_?(fn_|lbl_|jumptable_|code_|data_|bss_|rdata_|vftable_)[0-9A-Fa-f_]+$")


def is_placeholder_symbol(name: str) -> bool:
    return bool(_PLACEHOLDER_RE.match(name or ""))


def load_jsonl(path):
    scan, rows = None, {}
    with open(path) as fh:
        for line in fh:
            d = json.loads(line)
            if "_scan" in d:
                scan = d["_scan"]
                continue
            rows[d["symbol"]] = d
    return scan, rows


def callee_pairs(row, wanted):
    """(target, base, count) for each divergent call site in the wanted classes."""
    out = []
    for p in row.get("patterns") or []:
        if p["pattern"] not in wanted:
            continue
        det = p.get("details") or {}
        for c in det.get("divergent_callees") or []:
            out.append((c.get("target_symbol"), c.get("base_symbol"), c.get("count", 1)))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--namecheck", required=True)
    ap.add_argument("--blind", required=True,
                    help="a functionRelocDiffs=none sweep. Used ONLY for its "
                         "fuzzy percentages -- its pattern list is starved.")
    ap.add_argument("--all", dest="all_jsonl", default=None)
    ap.add_argument("--patterns", default=",".join(LIKELY_FIXABLE))
    ap.add_argument("--json-out", default=None)
    ap.add_argument("--top", type=int, default=40)
    args = ap.parse_args()

    wanted = set(args.patterns.split(","))
    nscan, N = load_jsonl(args.namecheck)
    bscan, B = load_jsonl(args.blind)
    A = load_jsonl(args.all_jsonl)[1] if args.all_jsonl else {}

    if nscan and nscan.get("ruler") != "name_check":
        print(f"WARNING: --namecheck was measured under ruler="
              f"{nscan.get('ruler')}", file=sys.stderr)
    if bscan and bscan.get("ruler") != "none":
        print(f"WARNING: --blind was measured under ruler={bscan.get('ruler')}, "
              f"not `none`. The 'name is the only defect' test needs the blind "
              f"ruler; this run's prize slice is not that test.", file=sys.stderr)

    items, no_blind = [], 0
    for sym, r in N.items():
        pats = {p["pattern"] for p in (r.get("patterns") or [])}
        if not (pats & wanted):
            continue
        b = B.get(sym)
        if b is None:
            no_blind += 1
            continue
        blind_fuzzy = b.get("fuzzy")
        graded_fuzzy = r.get("fuzzy")
        size = int(r.get("size") or 0)
        norm = r.get("norm")
        pays_full = blind_fuzzy is not None and blind_fuzzy >= 100.0
        charge = 0.0
        if blind_fuzzy is not None and graded_fuzzy is not None:
            charge = max(0.0, (blind_fuzzy - graded_fuzzy)) * size / 100.0
        items.append({
            "symbol": sym,
            "demangled": r.get("demangled"),
            "unit": r.get("unit"),
            "size": size,
            "norm": norm,
            "graded_fuzzy": graded_fuzzy,
            "blind_fuzzy": blind_fuzzy,
            "classes": sorted(pats & wanted),
            "pays_full": pays_full,
            "recoverable_bytes": size if pays_full else 0,
            "name_charge_bytes": round(charge, 1),
            "forgiven_under_all_only": sym in A and not (
                {p["pattern"] for p in (A[sym].get("patterns") or [])} & wanted),
            "callees": callee_pairs(r, wanted),
            "enclosing_placeholder": is_placeholder_symbol(sym),
            "owner": ("wrong-callee-lane"
                      if (sym == OTHER_LANE_SEED
                          or (norm is not None and norm >= OTHER_LANE_MIN_NORM
                              and "WRONG_CALLEE" in pats
                              and not is_placeholder_symbol(sym)))
                      else "unowned"),
        })

    items.sort(key=lambda d: (not d["pays_full"], -d["recoverable_bytes"],
                              -d["name_charge_bytes"]))

    funclets = [d for d in items if d["enclosing_placeholder"]]
    real = [d for d in items if not d["enclosing_placeholder"]]
    prize = [d for d in real if d["pays_full"]]
    rest = [d for d in real if not d["pays_full"]]
    by_class = collections.Counter(c for d in items for c in d["classes"])

    print(f"# LikelyFixable worklist -- {len(items)} functions "
          f"({', '.join(f'{k}={v}' for k, v in by_class.most_common())})")
    print("Ruler: functionRelocDiffs=name_check. `norm` is "
          "report.json.match_percent_normalized (canonical, unrounded).")
    if no_blind:
        print(f"  {no_blind} rows dropped: no blind-ruler measurement, so the "
              f"'name is the only defect' test could not be applied")
    print(f"\n## Tier 1 -- the name is the ONLY defect ({len(prize)} functions, "
          f"{sum(d['recoverable_bytes'] for d in prize):,} bytes recoverable)")
    print("Named enclosing symbol AND blind fuzzy == 100.0, so closing the "
          "callee crosses the row and pays its full size.\n")
    _table(prize, args.top)
    print(f"\n## Tier 2 -- real, but does not cross the row ({len(rest)} "
          f"functions, {sum(d['name_charge_bytes'] for d in rest):,.0f} bytes "
          f"of score charged to names)")
    print("The call IS to a different function -- a class neither "
          "match_percent_normalized (pre-4.2.4) nor unicorn can see -- but the "
          "row has structural mismatches too, so closing it does not cross it.\n")
    _table(rest, args.top)
    print(f"\n## Tier 3 -- ARTIFACT, do not work ({len(funclets)} functions, "
          f"{sum(d['recoverable_bytes'] for d in funclets):,} bytes that a naive "
          f"rank would have put at the TOP of tier 1)")
    print("The enclosing symbol is a splitter placeholder: an MSVC EH funclet "
          "objdiff paired BY BYTE SIGNATURE, so its counterpart routinely "
          "belongs to a different parent function and the differing `bl` is a "
          "pairing artifact, not a call. detect_callee_divergences checks the "
          "CALLEE for a placeholder name (-> UNVERIFIABLE_CALLEE_NAME) but not "
          "the ENCLOSING symbol; this is that gap.\n")
    _table(funclets, min(args.top, 8))

    if args.json_out:
        with open(args.json_out, "w") as fh:
            json.dump({"tier1": prize, "tier2": rest, "tier3_artifact": funclets,
                       "namecheck_scan": nscan, "blind_scan": bscan}, fh, indent=1)
        print(f"\nwrote {args.json_out}")
    return 0


def _table(rows, top):
    print(f"| {'norm':>8} | {'size':>6} | {'pay B':>6} | class | owner | symbol | target <- base |")
    print("|---:|---:|---:|---|---|---|---|")
    for d in rows[:top]:
        cal = d["callees"][0] if d["callees"] else (None, None, 0)
        cls = "+".join(c.replace("_MISMATCH", "").replace("TEMPLATE_INSTANTIATION", "TEMPLATE")
                       for c in d["classes"])
        pay = d["recoverable_bytes"] or int(d["name_charge_bytes"])
        norm = f"{d['norm']:.3f}" if d["norm"] is not None else "-"
        print(f"| {norm:>8} | {d['size']:>6} | {pay:>6} | {cls} | {d['owner']} | "
              f"`{d['symbol'][:78]}` | `{str(cal[0])[:44]}` <- `{str(cal[1])[:44]}` |")
    if len(rows) > top:
        print(f"| ... | | | | | +{len(rows) - top} more (see --json-out) | |")


if __name__ == "__main__":
    sys.exit(main())
