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

Exit codes:
    0   the recorded scan was taken by the installed objdiff-cli
    1   STALE PATTERN SCAN     -- a real staleness/provenance refusal
    2   UNREADABLE DATABASE    -- wrong DB: worktree shadow/tripwire, or not SQLite
    3   UNMOORED PATTERN SCAN  -- the scan was taken from a different tree

Usage:
    python3 scripts/verify_pattern_scan_current.py --check          # exit 0/1/2
    python3 scripts/verify_pattern_scan_current.py                  # describe
    python3 scripts/verify_pattern_scan_current.py --ruler all --check
"""
from __future__ import annotations

import argparse
import sqlite3
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from orchestrator.callee_gate import (DEFAULT_RULER,  # noqa: E402
                                      StalePatternScanError,
                                      UnmooredPatternScanError,
                                      UnreadableDatabaseError,
                                      ensure_current_scan,
                                      installed_objdiff_version)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--db", default=str(REPO_ROOT / "decomp.db"),
                    help="the MAIN checkout's decomp.db -- a worktree has a tripwire")
    ap.add_argument("--ruler", default=DEFAULT_RULER)
    ap.add_argument("--repo-root", default=str(REPO_ROOT),
                    help="tree whose bin/objdiff-cli is the reference instrument")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 with the reason on stderr instead of describing")
    a = ap.parse_args(argv)

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
        scan = ensure_current_scan(con, ruler=a.ruler, repo_root=a.repo_root)
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
    except StalePatternScanError as e:
        print(f"STALE PATTERN SCAN ({a.ruler}): {e}", file=sys.stderr)
        return 1
    finally:
        con.close()

    if not a.check:
        print(f"pattern scan id={scan['id']} ruler={scan['ruler']} is current")
        print(f"  tool      : {scan['tool_version']}")
        print(f"  installed : {installed_objdiff_version(a.repo_root)}")
        print(f"  tree      : {scan['project_dir']} @ {scan['build_rev']} "
              f"(tree_verified={scan['tree_verified']})")
        print(f"  examined  : {scan['examined']} of {scan['universe']}")
        print(f"  finished  : {scan['finished_at']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
