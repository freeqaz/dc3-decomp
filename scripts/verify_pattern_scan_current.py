#!/usr/bin/env python3
"""Assert that decomp.db's pattern scan was taken by the INSTALLED objdiff-cli.

The ~0.1 s assertion any measuring script should run before it believes a
`v_function_patterns` / `v_latest_pattern_scan` answer -- the same shape as
`scripts/verify_split_current.py --check`.

WHY THIS IS A CHECK AND NOT A NINJA EDGE THAT RE-DERIVES
========================================================
`v_latest_pattern_scan` is written only by a hand-run
`scripts/analysis/pattern_census.py --apply`.  The obvious repair is a ninja
edge, and it does not work here:

  * The staleness axis that actually bit (2026-08-21: a 4.2.6 scan read by a
    4.2.7 binary, with a real 172 B wrong callee missing from the 4.2.6 set) is
    the TOOL BINARY, and `bin/objdiff-cli` has no ninja edge at all -- the cargo
    rule is deliberately depfile-less and this repo uses a prebuilt binary.  An
    edge keyed on build inputs would have re-run the census repeatedly that day
    and still handed the gate a 4.2.6 scan.
  * `bin/objdiff-cli` is a symlink shared with `../rb3` and `../rb3-xenon`, so
    declaring it an input re-fires a ~30 s whole-binary sweep in three repos.
  * This repo was bitten on 2026-08-21 by an `always` edge re-running a 14 s
    report on every steady-state build.

So the refresh stays manual and the READ refuses.  A stale scan is an exception
naming both version strings, never a smaller result set -- and a smaller result
set is exactly what "no wrong callees here" looks like.

RUN IT FROM THE MAIN CHECKOUT -- AND IT NOW SAYS SO ITSELF
==========================================================
`decomp.db` deliberately does not exist in a worktree.  Until 2026-08-31 this
script, run from one, opened the tripwire `setup_worktree.sh` plants, swallowed
sqlite's `file is not a database`, and printed

    STALE PATTERN SCAN (name_check): no pattern scan recorded for ruler=...

-- a *stale-scan* diagnosis for a *wrong-directory* condition, complete with a
"re-derive it" command that would have done nothing.  That is this project's
recurring defect class: an instrument reporting a plausible wrong thing instead
of refusing.  The two conditions are now separate exceptions with separate exit
codes, and the message names the real one.

A SECOND, MIRROR-IMAGE FAILURE: A SCAN *WRITTEN* FROM A WORKTREE
================================================================
`pattern_census.py --apply` run from a worktree records `project_dir` = that
worktree.  The row lands in the MAIN checkout's shared `decomp.db`, outlives the
directory it names and the branch it was taken on, becomes the latest scan for
its ruler, and reads GREEN from main.  Four of this database's eleven scans are
of that shape (ids 1, 5, 6, 9) and three of those four directories are already
gone; scan 9 was the latest `name_check` scan for fifteen minutes on 2026-08-31
and nothing surfaced it.  That is now exit 3.

AND A THIRD BLINDNESS: NEITHER OBJECT BASELINE WAS EVER CHECKED
===============================================================
Everything above is about the INSTRUMENT (which objdiff) and the BOOKKEEPING
(which tree, which directory).  Until 2026-09-10 nothing looked at the 2,224
object PAIRS that were the measurement, and both sides move under a scan:

  * TARGET, `build/373307D9/obj/**.obj` -- `dtk xex split`'s undeclared output,
    rewritten by any `config/373307D9/symbols.txt` edit;
  * BASE, `build/373307D9/src/**.obj` -- rebuilt by any landed source commit.

`build_rev` is not a stand-in.  Scan 16 was taken across a landing merge: it
recorded `b91fc0cb5`, repaired 3 rows that were stale against deleted base
objects and INTRODUCED 6 more.  You cannot re-run your way to currency on an
active repo, so the scan has to carry what it measured -- since schema v18 it
does, per unit, in `pattern_scan_units`.

The verdict is deliberately a LIST OF UNITS and not a tree-wide boolean.  A
tree-wide boolean is permanently red here and would be routed around within a
day; a per-unit answer lets a lane ask about the one unit it is working on
(`--unit default/system/obj/Dir`) while the rest of the tree churns.

Exit codes:
    0   the recorded scan was taken by the installed objdiff-cli, and both
        object baselines still hash to what it recorded
    1   STALE PATTERN SCAN     -- a real staleness/provenance refusal.  Also the
                                  code for a scan that recorded NO object
                                  baseline at all (every pre-v18 scan): "cannot
                                  say" is a refusal, not a pass, and there is no
                                  honest way to backfill it.
    2   UNREADABLE DATABASE    -- wrong DB: worktree shadow/tripwire, or not SQLite
    3   UNMOORED PATTERN SCAN  -- the scan was taken from a different tree
    4   STALE TARGET OBJECTS   -- at least one unit's target object differs from
                                  the one the scan diffed (a `symbols.txt` /
                                  split change).  Takes precedence over 5 when
                                  both directions are dirty; the message names
                                  both counts either way.
    5   STALE BASE OBJECTS     -- base objects moved and no target object did
                                  (the ordinary "somebody landed a commit" case)

Usage:
    python3 scripts/verify_pattern_scan_current.py --check          # exit 0/1/2/3/4/5
    python3 scripts/verify_pattern_scan_current.py                  # describe
    python3 scripts/verify_pattern_scan_current.py --ruler all --check
    python3 scripts/verify_pattern_scan_current.py --check \\
        --unit default/system/obj/Dir       # is MY unit current?
    python3 scripts/verify_pattern_scan_current.py --check --list-stale 200
    python3 scripts/verify_pattern_scan_current.py --no-check-objects --check
"""
from __future__ import annotations

import argparse
import sqlite3
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from orchestrator import object_baseline  # noqa: E402
from orchestrator.callee_gate import (DEFAULT_RULER,  # noqa: E402
                                      StaleBaseObjectsError,
                                      StalePatternScanError,
                                      StaleTargetObjectsError,
                                      UnfingerprintedPatternScanError,
                                      UnmooredPatternScanError,
                                      UnreadableDatabaseError,
                                      ensure_current_scan,
                                      installed_objdiff_version,
                                      raced_units, stale_units)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--db", default=str(REPO_ROOT / "decomp.db"),
                    help="the MAIN checkout's decomp.db -- a worktree has a tripwire")
    ap.add_argument("--ruler", default=DEFAULT_RULER)
    ap.add_argument("--repo-root", default=str(REPO_ROOT),
                    help="tree whose bin/objdiff-cli is the reference instrument")
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero with the reason on stderr instead of describing")
    ap.add_argument("--unit", action="append", default=None, metavar="NAME",
                    help="only refuse if THIS unit's objects moved (repeatable). "
                         "Drift elsewhere is still counted in the message. This is "
                         "what makes the object check usable on an active repo.")
    ap.add_argument("--list-stale", type=int, default=20, metavar="N",
                    help="name the first N stale units (default 20). The COUNT is "
                         "always printed in full -- a truncated list is never "
                         "presented as a total.")
    ap.add_argument("--no-check-objects", dest="check_objects",
                    action="store_false",
                    help="skip the per-unit object baselines and check only the "
                         "instrument/tree provenance (the pre-v18 behaviour). For "
                         "asking 'was this scan taken by the right binary?' alone.")
    a = ap.parse_args(argv)
    units = set(a.unit) if a.unit else None

    # `sqlite3.connect(..., mode=ro)` on a non-database SUCCEEDS -- the header is
    # not read until the first statement -- so there is nothing to catch here.
    # The discrimination lives in `ensure_current_scan`, which knows the
    # connection's path and the shape of the failure.
    try:
        con = sqlite3.connect(f"file:{a.db}?mode=ro", uri=True)
    except sqlite3.Error as e:
        print(f"UNREADABLE DATABASE: cannot open {a.db}: {e}", file=sys.stderr)
        return 2
    try:
        try:
            scan = ensure_current_scan(con, ruler=a.ruler, repo_root=a.repo_root,
                                       check_objects=a.check_objects, units=units,
                                       list_limit=a.list_stale)
        except UnreadableDatabaseError as e:
            # NOT a staleness verdict.  Wrong database, most often because the
            # command was run from a worktree.  Exit code differs so a caller can
            # branch without parsing prose.
            print(f"UNREADABLE DATABASE ({a.ruler}): {e}", file=sys.stderr)
            return 2
        except UnmooredPatternScanError as e:
            # Must be caught BEFORE StalePatternScanError -- it is a subclass.
            print(f"UNMOORED PATTERN SCAN ({a.ruler}): {e}", file=sys.stderr)
            return 3
        except StaleTargetObjectsError as e:
            # Also a StalePatternScanError subclass, so it must be caught first.
            # Its own message carries the counts and the first --list-stale units.
            print(f"STALE TARGET OBJECTS ({a.ruler}): {e}", file=sys.stderr)
            return 4
        except StaleBaseObjectsError as e:
            print(f"STALE BASE OBJECTS ({a.ruler}): {e}", file=sys.stderr)
            return 5
        except UnfingerprintedPatternScanError as e:
            # A refusal, not a pass: the scan predates `pattern_scan_units` and
            # its baseline cannot be reconstructed.  Same exit code as the other
            # provenance refusals because the remedy is the same command.
            print(f"STALE PATTERN SCAN ({a.ruler}): {e}", file=sys.stderr)
            return 1
        except StalePatternScanError as e:
            print(f"STALE PATTERN SCAN ({a.ruler}): {e}", file=sys.stderr)
            return 1

        if not a.check:
            print(f"pattern scan id={scan['id']} ruler={scan['ruler']} is current")
            print(f"  tool      : {scan['tool_version']}")
            print(f"  installed : {installed_objdiff_version(a.repo_root)}")
            print(f"  tree      : {scan['project_dir']} @ {scan['build_rev']} "
                  f"(tree_verified={scan['tree_verified']})")
            print(f"  examined  : {scan['examined']} of {scan['universe']}")
            print(f"  finished  : {scan['finished_at']}")
            if not a.check_objects:
                print("  objects   : NOT CHECKED (--no-check-objects): this says "
                      "nothing about whether the scan's findings are about the "
                      "objects on disk")
            else:
                stale = stale_units(con, scan["id"],
                                    project_dir=scan["project_dir"])
                raced = raced_units(con, scan["id"])
                print(f"  objects   : {len(object_baseline.unit_paths(scan['project_dir']))} "
                      f"unit pairs content-hashed; "
                      f"{'all current' if not stale else f'{len(stale)} stale'}"
                      + (f", {len(raced)} raced DURING the scan" if raced else ""))
                if stale:
                    print(object_baseline.render_stale(stale, limit=a.list_stale,
                                                       indent="    "))
                if raced:
                    print(f"    raced (baseline not attributable): "
                          f"{', '.join(raced[:a.list_stale])}")
        return 0
    finally:
        con.close()


if __name__ == "__main__":
    raise SystemExit(main())
