#!/usr/bin/env python3
"""Refuse to measure a target object tree that did not come from the current split.

Why this exists
---------------
`report.json` is produced by diffing 2,223 pairs of objects.  The BASE side
(`build/<v>/src/**.obj`) is compiled by ninja edges, so ninja knows about it and
`patch_guard.ensure_patched_tree()` asserts it.  The TARGET side
(`build/<v>/obj/**.obj`) is different in kind: it is written by
`dtk xex split`, whose only DECLARED ninja output is `build/<v>/config.json`.
The 2,223 target objects are undeclared side effects.  No ninja edge names them,
nothing stats them, and nothing in the report's `provenance` block describes
them.

That matters because their CONTENT depends on `config/373307D9/symbols.txt`:
dtk writes each function under the name symbols.txt gives its address, so a
symbols.txt edit rewrites the COFF symbol tables of every unit it touches.  A
report taken against target objects split from a DIFFERENT symbols.txt is a
different measurement, and it is silent -- the mispaired functions read 0.0%
rather than erroring.

Reproduced here 2026-08-21, in one worktree, with one objdiff-cli
(4.2.7 / 76c8da87e040), with the report cache COLD on both runs:

    symbols.txt on disk   report started       matched_functions
    ------------------    ------------------   -----------------
    e5b1e3ce7 (new)       2 s into the split   29,497
    e5b1e3ce7 (new)       after it finished    29,838

A 341-function gap, from the same tree and the same command, discriminated only
by WHEN it ran.  The first run neither failed nor warned.  That number was
initially attributed to a stale objdiff report cache; the cache was tested
directly and exonerated (its key covers the target obj bytes -- mutating one
byte of a symbol name in a target .obj turns a hit into a miss).  The real
discriminator was the split.

What this checks
----------------
`--begin` / `--complete` bracket the split, writing
`build/<v>/split_inputs.stamp`.  `--check` passes only if:

  * the stamp exists (a tree that never recorded a split cannot be vouched for);
  * the stamp says `complete`, not `running` (nobody is mid-rewrite, and the
    last split did not die halfway); and
  * the hashes of the split's config inputs still equal the ones the stamp
    recorded (symbols.txt / splits.txt / config.yml have not moved since).

The `running` state is not decoration.  The reproduction above is a report that
overlapped a split which was rewriting the very objects it was reading, and the
input hashes ALONE do not catch it: a split re-run with an unchanged symbols.txt
matches its own stamp the whole time it is running.

What this deliberately does NOT check
-------------------------------------
The target objects' own bytes.  Digesting 71 MB on every measurement is
affordable (~0.2 s) but it would be a *different* assertion -- "nobody edited a
target object" -- and this project does not edit them.  The failure that has
actually happened is a config/objects mismatch, and that is what is gated.  If a
hand-edited target object ever shows up, extend this rather than replacing it.

Exit codes
----------
    0   the objects on disk correspond to the current split config
    1   they do not, or the state cannot be established (message says which)
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
VERSION = "373307D9"

#: The files whose CONTENT decides what dtk writes into build/<v>/obj/**.obj.
#: `orig/<v>/default.xex` is deliberately absent: it is the immutable input, it
#: is 40 MB, and hashing it on every measurement would buy nothing this project
#: can act on.  Its size is recorded for the record, not gated on.
SPLIT_CONFIG_INPUTS = (
    Path("config") / VERSION / "symbols.txt",
    Path("config") / VERSION / "splits.txt",
    Path("config") / VERSION / "config.yml",
)

XEX_PATH = Path("orig") / VERSION / "default.xex"

STAMP_REL = Path("build") / VERSION / "split_inputs.stamp"

STATE_RUNNING = "running"
STATE_COMPLETE = "complete"


class StaleSplitError(RuntimeError):
    """The target object tree does not correspond to the current split config."""


#: Why `--complete` also asserts a fixed point, on a repo where it has never
#: fired.
#:
#: CLAUDE.md states the contract: "`dtk xex split` must not modify its own
#: inputs -- its output has to be a fixed point of its input, or the depfile
#: edge self-refires on every build."  Nothing enforced it.  dc3's
#: `config/373307D9/splits.txt` IS a fixed point today (measured: four
#: consecutive full builds, byte-identical, git-clean), so this guard is
#: prevention, not repair.
#:
#: The sibling repo rb3-xenon (title 45410914) is the case for enforcing it.
#: Its committed splits.txt was not a fixed point, and the symptom was not a
#: build failure -- it was a WORKING-TREE MODIFICATION, which in a shared
#: checkout reads as somebody's work in progress.  Three lanes were told to
#: leave it alone.  It was generated churn the whole time: dtk re-derives each
#: `.pdata` split from the `.text` split that owns the function the entry
#: describes, and four hand-written `.pdata` ranges named the wrong TU.  dtk
#: was right every build and said so only by rewriting the file (it does log
#: `Writing updated .../splits.txt` at INFO; that was not enough).
#:
#: Recovery is one build: the split has already written the corrected file, so
#: the retry is a fixed point and passes.  Commit what it wrote -- do not hand-
#: revert it, and do not silence this with --no-fixed-point-check unless you
#: are deliberately re-deriving generated config in a single build.


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def current_inputs(project_dir: Path) -> dict:
    """Hash the split's config inputs as they are on disk right now."""
    out: dict[str, object] = {}
    for rel in SPLIT_CONFIG_INPUTS:
        p = project_dir / rel
        out[str(rel)] = _sha256(p) if p.exists() else None
    xex = project_dir / XEX_PATH
    out["xex_size"] = xex.stat().st_size if xex.exists() else None
    return out


def read_stamp(project_dir: Path) -> dict | None:
    p = project_dir / STAMP_REL
    if not p.exists():
        return None
    try:
        return json.loads(p.read_text())
    except (OSError, json.JSONDecodeError):
        return None


def write_stamp(project_dir: Path, state: str) -> Path:
    p = project_dir / STAMP_REL
    p.parent.mkdir(parents=True, exist_ok=True)
    doc = {
        "state": state,
        "inputs": current_inputs(project_dir),
        "pid": os.getpid(),
        "unix_time": time.time(),
        "note": (
            "Written by scripts/verify_split_current.py around `dtk xex split`. "
            "`state` is `running` between --begin and --complete; a check that "
            "sees `running` is looking at a tree mid-rewrite."
        ),
    }
    p.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n")
    return p


def _describe_drift(stamp_inputs: dict, live_inputs: dict) -> list[str]:
    drift = []
    for key in sorted(set(stamp_inputs) | set(live_inputs)):
        was, now = stamp_inputs.get(key), live_inputs.get(key)
        if was != now:
            drift.append(f"    {key}\n      split with: {was}\n      on disk now: {now}")
    return drift


def check(project_dir: Path) -> str:
    """Return a one-line note, or raise `StaleSplitError` naming the drift."""
    project_dir = Path(project_dir).resolve()
    stamp = read_stamp(project_dir)
    stamp_path = project_dir / STAMP_REL

    if stamp is None:
        raise StaleSplitError(
            f"REFUSING TO VOUCH FOR {project_dir}: {stamp_path} is missing or "
            f"unreadable, so there is no record of which config/{VERSION}/"
            f"symbols.txt produced build/{VERSION}/obj/**.obj.\n\n"
            f"The target objects are not a declared ninja output, so their age "
            f"cannot be inferred from mtimes either. Run `ninja` in that "
            f"directory (the split edge writes the stamp) and retry."
        )

    state = stamp.get("state")
    if state != STATE_COMPLETE:
        raise StaleSplitError(
            f"REFUSING TO VOUCH FOR {project_dir}: the split is recorded as "
            f"`{state}`, not `{STATE_COMPLETE}`.\n\n"
            f"Either `dtk xex split` is running right now and is rewriting "
            f"build/{VERSION}/obj/**.obj underneath you, or the last one died "
            f"partway and left a mixed tree. A report taken over a half-split "
            f"tree does not fail -- it silently reads the PRE-split number "
            f"(measured 2026-08-21: 29,497 instead of 29,838, a 341-function "
            f"gap, no warning). Wait for the build to finish, or re-run `ninja`."
        )

    live = current_inputs(project_dir)
    drift = _describe_drift(stamp.get("inputs") or {}, live)
    if drift:
        raise StaleSplitError(
            f"REFUSING TO VOUCH FOR {project_dir}: build/{VERSION}/obj/**.obj "
            f"were split from a DIFFERENT config than the one on disk.\n\n"
            + "\n".join(drift)
            + f"\n\ndtk writes each function under the name symbols.txt gives "
            f"its address, so the target objects currently name functions this "
            f"config does not. Every such function reads 0.0% and NOTHING "
            f"errors. Run `ninja` to re-split, then retry."
        )

    return f"split current ({len(SPLIT_CONFIG_INPUTS)} config inputs match {STAMP_REL})"


def _report_self_rewrite(project_dir: Path, was: dict | None) -> int:
    """Exit 1 if the split changed a file it reads.  Called from ``--complete``.

    ``was`` is the ``running`` record written by ``--begin``, read before the
    ``complete`` record overwrote it.
    """
    if not isinstance(was, dict) or was.get("state") != STATE_RUNNING:
        # No bracket to compare against (a fresh tree's first split, or a split
        # not run through --begin).  Returning 0 here is "nothing to say", not
        # "verified" -- which is why this is a tripwire on top of the existing
        # checks and not a replacement for any of them.
        return 0
    now = read_stamp(project_dir) or {}
    drift = _describe_drift(was.get("inputs") or {}, now.get("inputs") or {})
    if not drift:
        return 0
    print(
        "[split-guard] THE SPLIT REWROTE ITS OWN INPUT -- its output is not a "
        "fixed point of its input.\n"
        + "\n".join(drift)
        + "\n\nCLAUDE.md: `dtk xex split` must not modify its own inputs. The "
          "corrected file is on disk NOW -- inspect it and commit it; the next "
          "build passes. Do not hand-revert generated config to work around a "
          "generator bug, and do not assume the committed file was right: in "
          "rb3-xenon this exact churn was four hand-written `.pdata` ranges "
          "attributed to the wrong TU, and dtk's re-derivation was correct.\n"
          "  --no-fixed-point-check opts out for a deliberate re-derivation.",
        file=sys.stderr,
    )
    return 1


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--project-dir", default=str(REPO_ROOT),
                    help="Project root to operate on (default: this repo)")
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--begin", action="store_true",
                      help="Record `running` before `dtk xex split`")
    mode.add_argument("--complete", action="store_true",
                      help="Record `complete` after a successful split")
    mode.add_argument("--check", action="store_true",
                      help="Exit 1 if the target objects do not match the config")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--no-fixed-point-check", action="store_true",
                    help="With --complete: do not fail when the split rewrote "
                         "one of its own inputs. For deliberately re-deriving "
                         "generated config in one build instead of two.")
    ap.add_argument("--stamp-out", default=None,
                    help="With --check: write a digest of the verified state to "
                         "this path, but ONLY when it differs. The ninja edge is "
                         "`always`-dirty by design (both failure modes are "
                         "mtime-invisible), so without write-if-changed + restat "
                         "every build would re-run REPORT and REPORT RAW -- ~14 s "
                         "and a rewritten report.json on a tree where nothing "
                         "moved, which then churns the decomp.db metadata sync.")
    args = ap.parse_args(argv)

    project_dir = Path(args.project_dir).resolve()

    if args.begin or args.complete:
        state = STATE_RUNNING if args.begin else STATE_COMPLETE
        # Read the `running` record BEFORE overwriting it: the difference
        # between it and the `complete` record is --complete's only
        # opportunity to notice an input the split rewrote.
        was = read_stamp(project_dir) if args.complete else None
        p = write_stamp(project_dir, state)
        if not args.quiet:
            print(f"[split-guard] {state}: {p}")
        if args.complete and not args.no_fixed_point_check:
            rc = _report_self_rewrite(project_dir, was)
            if rc:
                return rc
        return 0

    try:
        note = check(project_dir)
    except StaleSplitError as exc:
        print(f"[split-guard] {exc}", file=sys.stderr)
        return 1

    if args.stamp_out:
        # The digest is of the STAMP, so it moves exactly when a split does and
        # not one build sooner. A passing check on an unmoved tree leaves the
        # file byte-identical and (with restat=True) ninja re-stats, sees the
        # old mtime, and leaves report.json clean.
        stamp_bytes = (project_dir / STAMP_REL).read_bytes()
        digest = hashlib.sha256(stamp_bytes).hexdigest() + "\n"
        out = Path(args.stamp_out)
        out.parent.mkdir(parents=True, exist_ok=True)
        if not out.exists() or out.read_text() != digest:
            out.write_text(digest)

    if not args.quiet:
        print(f"[split-guard] {note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
