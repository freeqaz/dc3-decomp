#!/usr/bin/env python3
"""Census every relocation-NAME charge in the build, with the row size it gates.

Ported into dc3-decomp from rb3-xenon's `tools/w25_pair_dump.py` on 2026-08-17.
The original hardcoded `build/45410914/report.json` -- rb3-xenon's title id --
which is precisely the kind of constant three sibling decomp trees must not
share, so here the report path is derived from `--project` (or given outright
with `--report`) and the ruler is read from that report's own
`provenance.diff_config` via `scripts/analysis/ruler.py`.  A percentage without
its ruler is not a measurement.

WHAT IT PRODUCES
================
For every function row that objdiff scores below 100 on the GRADED ruler, the
tool re-diffs with `--include-instructions` and separates the charges into:

  * relocation-NAME charges -- a `diff_arg` mismatch whose ONLY differing typed
    argument is a `Symbol`.  These are pairs (target_name, our_name) that the
    `name_check` ruler charges purely because the two relocations name different
    symbols.  Each is a candidate ICF fold OR a wrong callee in our source.
  * everything else ("other charges") -- real instruction differences.

⚠ A ROW ONLY PAYS WHEN EVERY CHARGE ON IT IS CLOSED.  `matched_code` is
all-or-nothing per row, so a name pair is only worth acting on when the rest of
its row is already clean.  That is why `--clean-only` exists and why the summary
separates `other_charges == 0` rows from the rest: on those rows, and only
those, closing the name pairs alone crosses the row and pays its FULL size.

FEEDING THE FOLD PROOF
======================
`--pairs-json` writes the charged pairs in the shape
`scripts/analysis/fold_proof.py --pairs-json` consumes, so the two chain:

    python3 scripts/analysis/name_charge_census.py --project . \\
        --clean-only --pairs-json /tmp/pairs.json
    python3 scripts/analysis/fold_proof.py --objects build/373307D9/src \\
        --pairs-json /tmp/pairs.json --equiv-json scripts/symbol_aliases.json \\
        --json-out /tmp/verdicts.json

Optionally `--map` cross-checks each pair against the shipped MSVC linker map
first: /OPT:ICF folds byte-identical COMDATs, so two names sharing an address
there ARE the fold set, stated by the linker that made the image.  Map silence
about a name is evidence AGAINST a fold -- but only if the name is spelled the
way the target spells it.  (dc3-decomp, 2026-08-17: `?Handle@HamDirector@@`'s
9,004 B rode on a charge naming `?OnSaveFaceAnims@HamDirector@@`, absent from
the map, which looked like decisive evidence against a fold.  The target spells
it `OnSaveFaceanims` and co-lists it with twelve other names at 0x82901c78.  The
map was never silent; our source had a capitalisation typo.)
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from scripts.analysis import ruler as ruler_mod  # noqa: E402

#: `<sect>:<offset> <name> <address> ...` -- parse_msvc_map's shape.
MAP_LINE = re.compile(r"^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+(\S+)\s+([0-9A-Fa-f]{8})\b")


def I(x, d=0):
    return d if x is None else int(x)


def F(x, d=0.0):
    return d if x is None else float(x)


def find_report(project: Path, explicit: str | None) -> Path:
    if explicit:
        return Path(explicit)
    builds = sorted((project / "build").glob("*/report.json"),
                    key=lambda p: p.stat().st_mtime, reverse=True)
    if not builds:
        raise SystemExit(f"no build/<title>/report.json under {project}")
    return builds[0]


def load_map_index(path):
    """symbol name -> sorted list of addresses it occupies in the linker map."""
    idx = collections.defaultdict(set)
    with open(path, errors="replace") as fh:
        for line in fh:
            m = MAP_LINE.match(line)
            if m:
                idx[m.group(1)].add(m.group(2).lower())
    return {k: sorted(v) for k, v in idx.items()}


def map_verdict(index, target_name, base_name):
    """MAP_CONFIRMS_FOLD / MAP_REFUTES_FOLD / NOT_IN_MAP for one charged pair."""
    ta = index.get(target_name) or []
    ba = index.get(base_name) or []
    if not ta or not ba:
        return "NOT_IN_MAP"
    return "MAP_CONFIRMS_FOLD" if set(ta) & set(ba) else "MAP_REFUTES_FOLD"


def collect_rows(project: Path, report: Path, rk, unit_pattern, cli, timeout):
    """Re-diff every sub-100 row and split its charges into name pairs vs other."""
    rep = json.loads(report.read_text())
    units = [u for u in rep["units"]
             if not unit_pattern or unit_pattern in u.get("name", "")]

    want = {}
    for u in units:
        for f in (u.get("functions") or []):
            fz = F(f.get("fuzzy_match_percent"))
            nm = f.get("name", "")
            if fz >= 100.0 or fz <= 0.0 or nm.startswith(("fn_", "lbl_")):
                continue
            want[(u["name"], nm)] = I(f.get("size"))

    out_rows = []
    by_unit = collections.defaultdict(list)
    for (uname, sym) in want:
        by_unit[uname].append(sym)

    for n, (uname, syms) in enumerate(sorted(by_unit.items()), 1):
        cmd = [cli, "diff", "-p", str(project), "-u", uname, "--batch",
               "-f", "json", "-o", "-", "--include-instructions"] + rk.args
        try:
            r = subprocess.run(cmd, capture_output=True, text=True,
                               timeout=timeout, input="\n".join(syms) + "\n")
        except subprocess.TimeoutExpired:
            print(f"  ! timeout on {uname}", file=sys.stderr)
            continue
        txt = r.stdout.strip()
        if not txt:
            continue
        try:
            j = json.loads(txt)
            recs = j if isinstance(j, list) else [j]
        except json.JSONDecodeError:
            recs = [json.loads(x) for x in txt.splitlines() if x.strip()]
        for rec in recs:
            sym = rec.get("symbol") or rec.get("name") or ""
            if (uname, sym) not in want or rec.get("error"):
                continue
            namepairs = []
            other = 0
            for i in rec.get("instructions", []) or []:
                mt = i.get("match_type")
                if mt == "equal":
                    continue
                if mt != "diff_arg":
                    other += 1
                    continue
                t = i.get("target") or {}
                b = i.get("base") or {}
                kinds, sp = set(), None
                for x, y in zip(t.get("typed_args", []) or [],
                                b.get("typed_args", []) or []):
                    if x.get("value") != y.get("value"):
                        kinds.add(x.get("type"))
                        if x.get("type") == "Symbol":
                            sp = (x.get("value"), y.get("value"))
                if kinds == {"Symbol"} and sp:
                    namepairs.append(sp)
                else:
                    other += 1
            if namepairs:
                out_rows.append({
                    "unit": uname, "symbol": sym, "size": want[(uname, sym)],
                    "fuzzy": F(rec.get("fuzzy_match_percent")),
                    "other_charges": other,
                    "pairs": sorted({tuple(p) for p in namepairs}),
                })
        if n % 100 == 0:
            print(f"  ... {n}/{len(by_unit)} units", file=sys.stderr)
    out_rows.sort(key=lambda r: -r["size"])
    return out_rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--project", default=".", help="project dir (default: cwd)")
    ap.add_argument("--report", help="explicit report.json (default: newest "
                                     "build/<title>/report.json under --project)")
    ap.add_argument("--objdiff-cli", default=None,
                    help="objdiff-cli path (default: <project>/bin/objdiff-cli)")
    ap.add_argument("--unit", default="", help="substring filter on unit name")
    ap.add_argument("--map", help="MSVC linker map to adjudicate pairs against "
                                  "(dc3: orig/373307D9/ham_xbox_r.map)")
    ap.add_argument("--clean-only", action="store_true",
                    help="keep only rows whose ONLY charges are relocation names")
    ap.add_argument("--json-out", help="write the full row records here")
    ap.add_argument("--pairs-json", help="write charged pairs in fold_proof's "
                                         "--pairs-json shape")
    ap.add_argument("--timeout", type=int, default=900)
    args = ap.parse_args(argv)

    project = Path(args.project).resolve()
    report = find_report(project, args.report)
    rk = ruler_mod.resolve_ruler(project)
    print(rk.banner())
    print(f"report: {report}")
    cli = args.objdiff_cli or str(project / "bin" / "objdiff-cli")

    rows = collect_rows(project, report, rk, args.unit, cli, args.timeout)
    clean = [r for r in rows if r["other_charges"] == 0]
    dirty = [r for r in rows if r["other_charges"] > 0]
    print()
    print(f"rows with >=1 relocation-NAME charge : {len(rows):>5}  "
          f"{sum(r['size'] for r in rows):>8} B")
    print(f"  ONLY name charges (row crosses)    : {len(clean):>5}  "
          f"{sum(r['size'] for r in clean):>8} B")
    print(f"  other charges too (row will not)   : {len(dirty):>5}  "
          f"{sum(r['size'] for r in dirty):>8} B")

    kept = clean if args.clean_only else rows

    if args.map:
        index = load_map_index(args.map)
        print(f"\nlinker map: {args.map} ({len(index)} names)")
        counts, byb = collections.Counter(), collections.Counter()
        for r in kept:
            verdicts = {map_verdict(index, t, b) for t, b in r["pairs"]}
            # A row is only as adjudicated as its WEAKEST pair: one refuted pair
            # means the row cannot cross on aliases alone.
            for k in ("MAP_REFUTES_FOLD", "NOT_IN_MAP", "MAP_CONFIRMS_FOLD"):
                if k in verdicts:
                    r["map_verdict"] = k
                    break
            counts[r["map_verdict"]] += 1
            byb[r["map_verdict"]] += r["size"]
        print(f"{'verdict':<20}{'rows':>7}{'bytes':>9}")
        for k, v in counts.most_common():
            print(f"{k:<20}{v:>7}{byb[k]:>9}")

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(kept, indent=1))
        print(f"\nwrote {args.json_out}")
    if args.pairs_json:
        pairs = []
        for r in kept:
            for t, b in r["pairs"]:
                pairs.append({"target": t, "base": b, "unit": r["unit"],
                              "row": r["symbol"], "row_size": r["size"],
                              "row_other_charges": r["other_charges"],
                              "map_verdict": r.get("map_verdict")})
        Path(args.pairs_json).write_text(json.dumps(pairs, indent=1))
        print(f"wrote {args.pairs_json} ({len(pairs)} pairs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
