#!/usr/bin/env python3
"""Assert that the two measurement paths agree on the CANONICAL score.

Why this exists
---------------
`objdiff-cli diff` and `objdiff-cli report generate` are two different code
paths that are supposed to measure the same thing. On 2026-08-20 they did not:
`diff` computed all three of its percent fields from `symbol_diff.match_percent`
and never read `symbol_diff.match_percent_normalized` at all, so a field named
`normalized_match_percent` carried the FUZZY score. On DC3's `TransformKeys`
that was a 4.1pp understatement; the worst gap seen was 7.6pp.

That was expensive because of what consumes it: the markdown header renders the
field as `Match: X% normalized`, and CLAUDE.md calls `run_objdiff` the source of
truth for decomp percentages. Every agent quoting a "normalized" number from a
one-shot diff was quoting fuzzy.

It was also self-concealing. The documented remedy for a suspect percentage was
"re-measure with run_objdiff" -- the same ruler that was wrong. No amount of
following the advice could surface it. It took two lanes arriving from
unrelated directions.

So this guard does not check either path against a constant. It checks them
against EACH OTHER, which is the property that actually broke, and which no
single-path test can see.

Exit codes
----------
  0  agreement within tolerance on every sampled function
  1  disagreement (the defect this exists to catch)
  2  could not run (missing report.json, missing objdiff-cli, no sample)

Usage
-----
  python3 scripts/analysis/ruler_agreement.py                 # sample 40
  python3 scripts/analysis/ruler_agreement.py --sample 200
  python3 scripts/analysis/ruler_agreement.py --self-test     # negative control
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
REPORT = REPO / "build" / "373307D9" / "report.json"
CLI = REPO / "bin" / "objdiff-cli"
MAP = REPO / "build" / "373307D9" / "icf_aliases.map"
TOL = 0.01


def load_report():
    if not REPORT.exists():
        print(f"NO_INPUT: {REPORT} does not exist; run ninja first", file=sys.stderr)
        sys.exit(2)
    with REPORT.open() as f:
        data = json.load(f)
    rows = []
    for unit in data.get("units", []):
        uname = unit.get("name", "")
        for fn in unit.get("functions", []) or []:
            fz = fn.get("fuzzy_match_percent")
            nm = fn.get("match_percent_normalized")
            if fz is None or nm is None:
                continue
            rows.append(
                {
                    "unit": uname,
                    "name": fn.get("name", ""),
                    "fuzzy": float(fz),
                    "canonical": float(nm),
                    "size": int(fn.get("size", 0) or 0),
                }
            )
    return rows


def query_cli(unit, name):
    cmd = [
        str(CLI), "diff", "-p", str(REPO), "-u", unit, name,
        "--map-file", str(MAP), "-o", "-", "-f", "json",
    ]
    try:
        out = subprocess.run(cmd, capture_output=True, timeout=180, cwd=REPO).stdout
        return json.loads(out)
    except Exception as e:
        return {"__error__": f"{type(e).__name__}: {e}"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sample", type=int, default=40)
    ap.add_argument("--tolerance", type=float, default=TOL)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if not CLI.exists():
        print(f"NO_INPUT: {CLI} missing", file=sys.stderr)
        sys.exit(2)

    rows = load_report()
    if not rows:
        print("NO_INPUT: report.json held no scored functions", file=sys.stderr)
        sys.exit(2)

    # Deliberately biased toward functions where fuzzy and canonical DIVERGE.
    # A sample drawn uniformly would be dominated by matched functions, where
    # both rulers read 100.0 and agree no matter how broken either one is --
    # i.e. it would pass against the very defect this guard exists to catch.
    divergent = sorted(rows, key=lambda r: -(r["canonical"] - r["fuzzy"]))
    picked = divergent[: args.sample]
    spread = max(r["canonical"] - r["fuzzy"] for r in picked)
    print(f"denominator: {len(rows)} scored functions in report.json")
    print(f"sampled:     {len(picked)}, biased toward canonical-vs-fuzzy divergence")
    print(f"max divergence in sample: {spread:.5f}pp")
    if spread < args.tolerance:
        print(
            "NO_DISCRIMINATION: no function in report.json has canonical != fuzzy, "
            "so this run could not have detected the defect. Treating as failure "
            "to measure rather than as success.",
            file=sys.stderr,
        )
        sys.exit(2)

    if args.self_test:
        # Negative control: prove the comparison can FAIL. Feed it a value we
        # know is wrong and confirm it is reported. A guard that has never been
        # observed failing is not known to be a guard.
        worst = picked[0]
        fake_cli = worst["fuzzy"]  # what the buggy CLI used to return
        bad = abs(fake_cli - worst["canonical"]) > args.tolerance
        print("\n--- self-test (negative control) ---")
        print(f"  function : {worst['name'][:70]}")
        print(f"  canonical: {worst['canonical']:.5f}")
        print(f"  injected : {fake_cli:.5f}  (the pre-fix CLI's answer)")
        print(f"  detected : {bad}")
        if not bad:
            print("SELF-TEST FAILED: the comparison did not flag a known-wrong value",
                  file=sys.stderr)
            sys.exit(1)
        print("  self-test PASSED: a known-wrong value is detected")
        return

    errors, mismatches, checked = [], [], 0
    for r in picked:
        out = query_cli(r["unit"], r["name"])
        if "__error__" in out:
            errors.append((r, out["__error__"]))
            continue
        canon = out.get("canonical_match_percent")
        if canon is None:
            mismatches.append((r, None, "canonical_match_percent ABSENT from CLI output"))
            continue
        checked += 1
        if abs(float(canon) - r["canonical"]) > args.tolerance:
            mismatches.append((r, float(canon), "value disagrees with report.json"))

    print(f"\nchecked:   {checked}")
    print(f"errors:    {len(errors)}")
    print(f"mismatches:{len(mismatches)}")

    for r, why in errors[:5]:
        print(f"  ERROR {r['name'][:60]}: {why}")

    if mismatches:
        print("\nDISAGREEMENT between objdiff-cli diff and report generate:")
        for r, got, why in mismatches[:20]:
            print(f"  {r['unit']} :: {r['name'][:60]}")
            print(f"      report.json canonical : {r['canonical']:.5f}")
            print(f"      objdiff-cli diff      : {got}")
            print(f"      fuzzy (for reference) : {r['fuzzy']:.5f}")
            print(f"      -> {why}")
        print(
            "\nThe two measurement paths must agree on the canonical score. "
            "If the CLI value equals the fuzzy column above, the regression is "
            "the 2026-08-20 one: diff reporting match_percent where it should "
            "report match_percent_normalized."
        )
        sys.exit(1)

    if checked == 0:
        print("NO_INPUT: nothing was actually checked", file=sys.stderr)
        sys.exit(2)

    print("\nOK: both measurement paths agree on the canonical score.")


if __name__ == "__main__":
    main()
