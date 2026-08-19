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

COVERAGE (scripts/analysis/coverage.py) -- and the 16,780-row hole
==================================================================
Until 2026-08-19 the row selector read

    fz = F(f.get("fuzzy_match_percent"))          # F() turns None into 0.0
    if fz >= 100.0 or fz <= 0.0 or nm.startswith(("fn_", "lbl_")):
        continue

`fuzzy_match_percent` is a key objdiff emits only for functions WE DEFINE, so a
MISSING field was masked by a default that collides with a real 0.0 and left
through the `<= 0.0` arm.  Measured on this tree: 48,344 function rows, of which
16,920 (35.0%) carry no `fuzzy_match_percent` at all -- 16,780 of them excluding
`fn_`/`lbl_` shapes, worth 5,129,540 B -- against an examined population of
2,238 rows / 1,264,412 B.  The scanner was reporting on 4.6% of the rows and
printed three numerators with no denominator anywhere.

EXCLUDING those rows remains correct: a row we emit no body for has no
relocations to charge, so it cannot carry a relocation-NAME charge.  The defect
was never the exclusion, it was the SILENCE.  They are now a named drop with
their byte total, and the `<= 0.0` arm is a separate branch that (today) fires
on exactly zero rows -- proof that arm only ever existed to swallow the None.

Every other discard is counted too: per-unit objdiff timeouts (which are a
TRUNCATION and now force a non-zero exit), empty objdiff output, per-symbol
`error` records, and symbols we asked about that no record came back for.
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
from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402

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


def select_rows(rep, unit_pattern, cov):
    """`(unit, symbol) -> size` for the rows worth re-diffing, with a denominator.

    Split out of `collect_rows` in 2026-08-19 so the selector -- the part that
    carried the 16,780-row hole -- is testable without an objdiff-cli run.  It
    declares the universe on `cov` and routes EVERY discard through `cov.drop`.
    """
    all_rows = [(u, f) for u in rep["units"] for f in (u.get("functions") or [])]
    cov.universe(len(all_rows), "function rows in report.json")

    want = {}
    skipped_bytes = collections.Counter()
    for u, f in all_rows:
        nm = f.get("name", "")
        size = I(f.get("size"))
        if unit_pattern and unit_pattern not in u.get("name", ""):
            cov.drop("outside--unit-filter", note=f"unit name lacks {unit_pattern!r}")
            skipped_bytes["outside--unit-filter"] += size
            continue
        # >>> the 16,780-row hole: an ABSENT field, not a zero score. <<<
        p = f.get("fuzzy_match_percent")
        if p is None:
            reason = ("no-fuzzy-score-phantom-name" if nm.startswith(("fn_", "lbl_"))
                      else "no-fuzzy-score")
            cov.drop(reason, note="objdiff emits fuzzy_match_percent only for "
                                  "functions we DEFINE; a row with no body of "
                                  "ours has no relocation to charge")
            skipped_bytes[reason] += size
            continue
        if nm.startswith(("fn_", "lbl_")):
            cov.drop("phantom-name-shape", note="dtk fn_/lbl_ placeholder name")
            skipped_bytes["phantom-name-shape"] += size
            continue
        fz = float(p)
        if fz >= 100.0:
            cov.drop("already-100-on-the-graded-ruler")
            skipped_bytes["already-100-on-the-graded-ruler"] += size
            continue
        if fz <= 0.0:
            # A REAL 0.0, now that None can no longer reach here.  Measured
            # 2026-08-19: this fires on 0 rows, i.e. the arm only ever existed
            # to swallow the missing field above.
            cov.drop("graded-score-is-zero")
            skipped_bytes["graded-score-is-zero"] += size
            continue
        want[(u["name"], nm)] = size
    for reason, b in sorted(skipped_bytes.items()):
        cov.extra(f"bytes_{reason.replace('-', '_')}", b)
    return want


def collect_rows(project: Path, report: Path, rk, unit_pattern, cli, timeout, cov):
    """Re-diff every sub-100 row and split its charges into name pairs vs other.

    `cov` is a CoverageReport; `select_rows` declares the universe on it and
    this function routes every further discard through it.  See the COVERAGE
    section of the module docstring.
    """
    rep = json.loads(report.read_text())
    cov.note(f"report: {report}")
    want = select_rows(rep, unit_pattern, cov)

    out_rows = []
    by_unit = collections.defaultdict(list)
    for (uname, sym) in sorted(want):
        by_unit[uname].append(sym)

    n_timed_out = 0
    for n, (uname, syms) in enumerate(sorted(by_unit.items()), 1):
        cmd = [cli, "diff", "-p", str(project), "-u", uname, "--batch",
               "-f", "json", "-o", "-", "--include-instructions"] + rk.args
        try:
            r = subprocess.run(cmd, capture_output=True, text=True,
                               timeout=timeout, input="\n".join(syms) + "\n")
        except subprocess.TimeoutExpired:
            # A whole unit's rows: this is a TRUNCATION of the analysis, so it
            # goes through cov.cap() and makes the run exit non-zero.
            print(f"  ! timeout on {uname} after {timeout}s "
                  f"({len(syms)} rows NEVER EXAMINED)", file=sys.stderr)
            n_timed_out += len(syms)
            continue
        txt = r.stdout.strip()
        if not txt:
            print(f"  ! objdiff-cli emitted nothing for {uname} "
                  f"(rc={r.returncode}, {len(syms)} rows) "
                  f"{(r.stderr or '').strip()[:160]}", file=sys.stderr)
            cov.drop("objdiff-emitted-no-output", len(syms),
                     note="objdiff-cli produced empty stdout for the whole unit")
            continue
        try:
            j = json.loads(txt)
            recs = j if isinstance(j, list) else [j]
        except json.JSONDecodeError:
            recs = [json.loads(x) for x in txt.splitlines() if x.strip()]
        seen = set()
        for rec in recs:
            sym = rec.get("symbol") or rec.get("name") or ""
            if (uname, sym) not in want:
                continue          # not a row we asked about; not in the universe
            if rec.get("error"):
                seen.add(sym)
                cov.drop("objdiff-record-error", note="the diff record for this "
                                                      "symbol carries an `error`")
                continue
            seen.add(sym)
            cov.examine()
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
        # Symbols we asked about that no record came back for.  Before 2026-08-19
        # these were indistinguishable from "examined and found clean".
        absent = [s for s in syms if s not in seen]
        if absent:
            cov.drop("no-diff-record-returned", len(absent),
                     note="we asked objdiff-cli about this symbol and it "
                          "returned no record for it")
        if n % 100 == 0:
            print(f"  ... {n}/{len(by_unit)} units", file=sys.stderr)

    if n_timed_out:
        cov.cap("--timeout", timeout, before=len(want),
                after=len(want) - n_timed_out,
                note="whole units whose objdiff-cli run exceeded --timeout")

    # -size alone ties constantly; symbol keeps the order reproducible.
    out_rows.sort(key=lambda r: (-r["size"], r["unit"], r["symbol"]))
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
    ap.add_argument("--timeout", type=int, default=900,
                    help="per-unit objdiff-cli timeout in seconds (default 900). "
                         "A unit that exceeds it loses ALL its rows; since "
                         "2026-08-19 that is a counted TRUNCATION and the run "
                         "exits non-zero rather than printing a clean total.")
    add_coverage_args(ap)
    args = ap.parse_args(argv)

    project = Path(args.project).resolve()
    report = find_report(project, args.report)
    rk = ruler_mod.resolve_ruler(project)
    # PRESERVED (and the reason this was the only honest scanner of the eight):
    # it discloses which ruler produced its percentages.  A percentage without
    # its ruler is not a measurement.
    print(rk.banner())
    print(f"report: {report}")
    cli = args.objdiff_cli or str(project / "bin" / "objdiff-cli")

    cov = CoverageReport("name_charge_census", args=args)
    cov.note(rk.label())
    rows = collect_rows(project, report, rk, args.unit, cli, args.timeout, cov)
    clean = [r for r in rows if r["other_charges"] == 0]
    dirty = [r for r in rows if r["other_charges"] > 0]

    d = cov.as_dict()
    examined, universe = d["examined"], d["universe"]
    print()
    print(f"DENOMINATOR: {examined} rows re-diffed, of {universe} function rows "
          f"in report.json")
    print(f"             {d['dropped_total']} rows dropped before the re-diff "
          f"(see the COVERAGE block on stderr); "
          f"{d.get('bytes_no_fuzzy_score', 0)} B of that is "
          f"{d['dropped'].get('no-fuzzy-score', 0)} rows we emit NO BODY for")
    print(f"rows with >=1 relocation-NAME charge : {len(rows):>5}  "
          f"{sum(r['size'] for r in rows):>8} B   "
          f"({len(rows)}/{examined} examined)")
    print(f"  ONLY name charges (row crosses)    : {len(clean):>5}  "
          f"{sum(r['size'] for r in clean):>8} B   "
          f"({len(clean)}/{examined} examined)")
    print(f"  other charges too (row will not)   : {len(dirty):>5}  "
          f"{sum(r['size'] for r in dirty):>8} B   "
          f"({len(dirty)}/{examined} examined)")
    print(f"  no relocation-NAME charge at all   : "
          f"{examined - len(rows):>5}          -   "
          f"({examined - len(rows)}/{examined} examined)")

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
    return cov.emit()


if __name__ == "__main__":
    sys.exit(main())
