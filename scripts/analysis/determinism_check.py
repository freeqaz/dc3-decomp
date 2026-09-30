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

WHAT THIS HARNESS IS NOT ABOUT: BUILD OUTPUTS
---------------------------------------------
Issue #150 ("`OptionsPanel.obj` is byte-nondeterministic") was reported against
a green run of this file, and this file was rated SOUND by the cannot-fail
audit.  Both are true and neither is a contradiction: **the subject of every
case below is a Python scanner's STDOUT, and no case in this file has ever
looked at a byte of `build/`.**  It could not have caught #150 at any sample
size, under any PYTHONHASHSEED, because it does not read the artifact.

That is the sound-but-checking-something-else failure, and it is worth naming
because a "determinism check" that is green reads as "this project's outputs
are deterministic" when it means "these thirteen scanners' text output is".

The real number, measured 2026-08-31 in a worktree, two full rebuilds of
identical source in the same tree: **980 of 989 objects differed**, the other
nine only because ninja did not rebuild them.  Cause: MSVC's clock-derived
COFF `TimeDateStamp` and CodeView `S_OBJNAME` signature -- not a patcher, and
not one object.  Masking those two fields made all 980 compare equal with zero
residual bytes.

Build-output determinism is now covered where it belongs -- inside the build:
`scripts/obj_build_metadata_patcher.py` zeroes both fields as the last
post-compile pass, and `scripts/verify_objs_patched.py --check` (a default
build edge) runs its `--check` and fails the build if any object still carries
them.  Do NOT add a 2x-full-rebuild case here; this harness is read-only and
must stay cheap.

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
    # Added 2026-09-15.  Reads 990 COFF objects and a 12 MB linker map, so it is
    # the slowest entry here (~40 s/run); it earns the seat because its output
    # is a WORK LIST -- an access divergence it drops silently reads as "that
    # class is clean".  Agreed with itself, and across PYTHONHASHSEED, on the
    # day it was added.
    ("access_specifier_scan", ["python3", "scripts/analysis/access_specifier_scan.py"]),
    # Added 2026-09-16.  Cheap (~8 s/run, 980 COFF object pairs).  It earns the
    # seat for the same reason: its output is a WORK LIST whose ZERO was read as
    # an exhaustion proof for a month, and two real bugs went through it.  Its
    # coverage block is now the load-bearing part of the output, so a
    # nondeterministic denominator would be worse than a nondeterministic hit
    # list.  Agreed with itself, and across PYTHONHASHSEED, on the day it was added.
    ("bss_initializer_scan",  ["python3", "scripts/analysis/bss_initializer_scan.py"]),
    # Added 2026-09-16 after its our-side walker was rewritten to follow
    # materialised base registers.  ~5 s/run.  Same rationale as the two
    # above: the output is a WORK LIST whose DISAGREE bucket reads 0 today,
    # and a 0 that moves between runs reads as "this class is clean".  Both
    # globs were unsorted into a first-write-wins setdefault until today, so
    # this entry is guarding a fragility that was real rather than a
    # hypothetical one.  Agreed with itself, and across PYTHONHASHSEED, on
    # the day it was added (12,419 B).
    ("mutable_float_audit",   ["python3", "scripts/analysis/mutable_float_audit.py"]),
    # Added 2026-09-16.  ~12 s/run over 989 COFF object pairs.  Same rationale
    # as the three above: the output is a WORK LIST for taxonomy class 7
    # (Save/Load field-order desync), it reads 0-1 findings on the current
    # tree, and a zero that moves between runs reads as "this class is clean".
    # Its coverage block carries the load-bearing part of the result -- which
    # bodies it could NOT follow -- so a nondeterministic denominator would be
    # worse than a nondeterministic hit list.  Both globs are sorted and both
    # per-object loops iterate a sorted list.
    ("serializer_field_trace", ["python3", "scripts/analysis/serializer_field_trace.py"]),
    # Added 2026-09-16.  ~6 s/run over 979 COFF object pairs (30,832 function
    # bodies).  Same rationale as the three above, with one extra: its output is
    # a CANDIDATE list for a bug class that scores 100%, so a row that appears
    # on one run and not the next would read as "adjudicated and gone".  Its
    # dict iteration order feeds the reported pair order directly.  Agreed with
    # itself, and across PYTHONHASHSEED, on the day it was added.
    ("this_offset_scan",      ["python3", "scripts/analysis/this_offset_scan.py"]),
    # Added 2026-09-30.  ~15 s/run: ONE `objdiff-cli diff --batch` over every
    # sub-100% function (840 today) plus a block pairing per function.  Same
    # rationale as the entries above -- a WORK LIST for taxonomy classes 2/3
    # whose finding buckets read small, so a row that moves between runs would
    # read as "adjudicated and gone".  Every dict it iterates to produce output
    # is walked in sorted order and the batch's own row order is fixed by
    # objdiff (BTreeMap by unit position).  Agreed with itself, and across
    # PYTHONHASHSEED, on non-empty output on the day it was added.
    ("cond_semantics_scan",   ["python3", "scripts/analysis/cond_semantics_scan.py"]),
    # Added 2026-09-30.  ~20 s/run over 979 COFF object pairs (30,832 function
    # bodies, 5,536 displacement-only rows).  Its candidate list feeds bug
    # hunting for taxonomy class 1 off NON-`this` bases, and its value-flow
    # check hash-conses expressions into ids whose numbering follows
    # evaluation order -- a set iterated into that numbering would move rows
    # between the reordered and candidate buckets from run to run.  No globs;
    # units are walked sorted and every dict it iterates for output is built
    # from a sorted source.  Agreed with itself, and across PYTHONHASHSEED, on
    # the day it was added.
    ("pointer_disp_scan",     ["python3", "scripts/analysis/pointer_disp_scan.py"]),
    # Added 2026-09-30 (det-arith).  The slowest entry: one sharded objdiff
    # --batch pass over all 48,365 report functions, ~2-3 min/run with 12
    # workers.  It earns the seat because its output is a CANDIDATE list for a
    # bug class the canonical ruler forgives (register-only rows), and its
    # shard results are merged from a process pool -- `ex.map` preserves
    # submission order, and the per-function pass iterates `sorted(universe)`;
    # a nondeterministic merge would move rows between runs.  The watchdog
    # (--idle-timeout) is the one timing-dependent input: a symbol it gives up
    # on is NAMED in the coverage block, so a flake is visible, not silent.
    ("arith_semantics_scan",  ["python3", "scripts/analysis/arith_semantics_scan.py"]),
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
    print("OUT OF SCOPE: build outputs. This harness compares SCANNER STDOUT and "
          "reads no byte of build/. Object-byte reproducibility is enforced by "
          "scripts/obj_build_metadata_patcher.py --check, via "
          "verify_objs_patched.py in the default build (see #150).")
    # An inconclusive run is a failure of the harness, not a clean bill of
    # health for the scanner, so it must not exit 0.
    return 1 if (diff or inc) else 0


if __name__ == "__main__":
    sys.exit(main())
