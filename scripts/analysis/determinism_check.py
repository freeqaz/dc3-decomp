#!/usr/bin/env python3
"""determinism_check.py — run each scanner twice and diff itself against itself.

A scanner that disagrees with itself is not a measurement, it is a sample of a
distribution. This repo has already shipped three of them:

  data_symbol_scan.py   a lazily-built linker-map index was published EMPTY and
                        then filled from inside the worker pool, so proven ICF
                        folds were reported as candidate bugs "differently on
                        every run" — a month of candidate counts.
  scope_index_census.py an unsorted `glob(recursive=True)` feeding a
                        last-write-wins dict: 568 of 6,675 (function, static)
                        pairs hold conflicting scope values, so the DIFF VERDICT
                        for those functions flips between runs.
  findarray_receiver_scan.py  set-iteration over strings driving output order —
                        four PYTHONHASHSEED values, four distinct output hashes.

The three usual causes, in order of how often they bite here:
  1. `as_completed` / `imap_unordered` result order used as output order.
  2. `glob`/`os.walk` without `sorted()`, feeding a dict that overwrites.
  3. a `sort` whose key can tie, so the top-N is decided by arrival order.

PYTHONHASHSEED is varied between the two runs on purpose: with it pinned, set
iteration looks stable and the bug hides.

Usage:
    python3 scripts/analysis/determinism_check.py                 # the curated set
    python3 scripts/analysis/determinism_check.py --only home_store_census
    python3 scripts/analysis/determinism_check.py --cmd 'python3 scripts/x.py --foo'
Exit 0 = every scanner agreed with itself; 1 = at least one did not.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Curated: read-only, no DB writes, cheap enough to run twice.  Each entry is
# (label, argv).  Keep this list honest — a scanner absent from it has simply
# never been checked, which is not the same as being deterministic.
CASES: list[tuple[str, list[str]]] = [
    ("remaining_work",        ["python3", "scripts/analysis/remaining_work.py"]),
    ("find_near_complete",    ["python3", "scripts/analysis/find_near_complete_units.py"]),
    ("home_store_census",     ["python3", "scripts/analysis/home_store_census.py"]),
    ("scope_index_census",    ["python3", "scripts/analysis/scope_index_census.py"]),
    # decomp.db is a 0-byte placeholder in a worktree; point at the main repo's
    # copy READ-ONLY so this is a real check and not a traceback.
    ("report_absent_census",  ["python3", "scripts/analysis/report_absent_census.py",
                               "--db", "/home/free/code/milohax/dc3-decomp/decomp.db"]),
    ("frame_deficit_census",  ["python3", "scripts/analysis/frame_deficit_census.py"]),
    ("og_dc3_port_candidates", ["python3", "scripts/analysis/og_dc3_port_candidates.py"]),
    ("honesty_lint",          ["python3", "scripts/analysis/honesty_lint.py", "--json"]),
    ("vtable_dispatch_scan",  ["python3", "scripts/analysis/vtable_dispatch_scan.py",
                               "--min-norm", "99.9"]),
    # Added 2026-08-20 by the frontier lane.  All four are WORK-SELECTION
    # oracles -- the class of tool whose nondeterminism reads as "this class is
    # exhausted" -- and none of them had ever been checked.  All four agreed
    # with themselves on the day they were added.
    ("progress_metrics",      ["python3", "scripts/progress_metrics.py"]),
    ("frontier",              ["python3", "scripts/analysis/frontier.py",
                               "--db", "/home/free/code/milohax/dc3-decomp/decomp.db"]),
    ("function_health",       ["python3", "scripts/analysis/function_health.py",
                               "--db", "/home/free/code/milohax/dc3-decomp/decomp.db",
                               "--min", "99", "--max", "99.99", "--limit", "0", "--json"]),
    ("certify_floor_summary", ["python3", "scripts/certify_floor.py",
                               "--db", "/home/free/code/milohax/dc3-decomp/decomp.db",
                               "--summary"]),
]

# NOT in CASES, and the reason is budget rather than confidence: a single
# uncapped run of ceiling_calculator.py (~1,568 objdiff invocations) or
# batch_pattern_scan.py (~1,751) takes well over ten minutes, so checking either
# one costs half an hour.  They have been spot-checked by hand; they have not
# been checked here.  Absence from CASES means UNCHECKED, never "deterministic".
UNCHECKED_TOO_EXPENSIVE = ["ceiling_calculator", "batch_pattern_scan",
                           "data_symbol_scan", "fake_impl_scan"]

TIMEOUT = 900

# --------------------------------------------------------------------------- #
# THE VACUITY GUARD.
#
# The first version of this harness reported "8/8 scanners agreed with
# themselves" — and three of those eight had produced ZERO BYTES of stdout,
# because `--json` takes a path argument and argparse had exited 2 before any
# scanning happened. sha256("") == sha256(""), so two failures compared equal
# and the harness cheerfully called it determinism.
#
# That is the very bug this file exists to catch, committed inside the catcher.
# A comparison you can pass by doing nothing proves nothing: an empty or
# failed run is INCONCLUSIVE, never SAME.
# --------------------------------------------------------------------------- #
MIN_MEANINGFUL_BYTES = 32
# --------------------------------------------------------------------------- #


def run_once(argv: list[str], seed: str) -> tuple[int, str]:
    env = dict(os.environ)
    env["PYTHONHASHSEED"] = seed
    try:
        p = subprocess.run(argv, cwd=REPO, env=env, capture_output=True,
                           text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return -1, "<TIMEOUT>"
    # stdout AND stderr.  Several scanners here print their whole coverage
    # block to stderr and nothing to stdout, so comparing stdout alone would
    # silently compare two empty strings -- the vacuity trap again.  Any
    # scanner that emits a wall-clock time must be filtered here, not exempted.
    return p.returncode, p.stdout + p.stderr


def check(label: str, argv: list[str], verbose: bool = False) -> str:
    """Return one of 'SAME', 'DIFFERS', 'INCONCLUSIVE'."""
    rc1, o1 = run_once(argv, "1")
    rc2, o2 = run_once(argv, "7")
    h1 = hashlib.sha256(o1.encode()).hexdigest()[:12]
    h2 = hashlib.sha256(o2.encode()).hexdigest()[:12]

    # Vacuity guard — see the comment on MIN_MEANINGFUL_BYTES.
    reasons = []
    if rc1 < 0 or rc2 < 0:
        reasons.append("timed out")
    if rc1 == 2 or rc2 == 2:
        reasons.append("exit 2 (argparse usage error — the command is wrong)")
    if len(o1) < MIN_MEANINGFUL_BYTES or len(o2) < MIN_MEANINGFUL_BYTES:
        reasons.append(f"stdout < {MIN_MEANINGFUL_BYTES}B ({len(o1)}/{len(o2)}) "
                       f"— nothing was compared")
    if reasons:
        print(f"{label:26s} rc={rc1}/{rc2}  {h1} {h2}  "
              f"!! INCONCLUSIVE: {'; '.join(reasons)}")
        return "INCONCLUSIVE"

    if o1 == o2:
        print(f"{label:26s} rc={rc1}/{rc2}  {h1} {h2}  SAME ({len(o1)}B)")
        return "SAME"
    print(f"{label:26s} rc={rc1}/{rc2}  {h1} {h2}  *** DIFFERS ***")
    if verbose:
        for line in list(difflib.unified_diff(
                o1.splitlines(), o2.splitlines(), "run1", "run2", lineterm=""))[:40]:
            print("    " + line)
    return "DIFFERS"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None, help="substring filter on the label")
    ap.add_argument("--cmd", default=None, help="check one ad-hoc command instead")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.cmd:
        cases = [("ad-hoc", args.cmd.split())]
    else:
        cases = [c for c in CASES if not args.only or args.only in c[0]]

    verdicts = {label: check(label, argv, args.verbose) for label, argv in cases}
    same = [k for k, v in verdicts.items() if v == "SAME"]
    diff = [k for k, v in verdicts.items() if v == "DIFFERS"]
    inc = [k for k, v in verdicts.items() if v == "INCONCLUSIVE"]

    print(f"\n{len(same)}/{len(cases)} scanners agreed with themselves on a "
          f"NON-EMPTY output")
    if diff:
        print("NONDETERMINISTIC: " + ", ".join(sorted(diff)))
    if inc:
        print("INCONCLUSIVE (produced nothing to compare — NOT a pass): "
              + ", ".join(sorted(inc)))
    # A green run here covers only what is in CASES. Say what it does not cover,
    # so "the scanners are deterministic" cannot be read off a number that was
    # never about them.
    print("NOT CHECKED (too expensive to run twice here — UNCHECKED, not clean): "
          + ", ".join(UNCHECKED_TOO_EXPENSIVE))
    # An inconclusive run is a failure of the harness, not a clean bill of
    # health for the scanner, so it must not exit 0.
    return 1 if (diff or inc) else 0


if __name__ == "__main__":
    sys.exit(main())
