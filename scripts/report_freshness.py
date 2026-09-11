#!/usr/bin/env python3
"""Is `build/<v>/report.json` current with respect to the objects and config?

Why this exists
---------------
`scripts/measure_progress.sh` gated both halves of its comparison on

    ninja -n build/373307D9/report.json   |   grep "no work to do"

and treated anything else as "the report is stale".  That was correct on
2026-08-04 when it was written (`dca4a6ca0`).  It stopped being correct on
2026-08-21, when `6e1763aac` gave `report.json` an `always`-dirty implicit --
the split-currency guard -- because the target objects are an undeclared build
output that no mtime can describe.  `7b4044fb7` then made that edge
always-DIRTY without being always-CHANGED (`--stamp-out` + `restat`), so a real
`ninja` on a current tree is a ~0.4 s no-op again.

`restat` is the crucial detail: it is applied **while the build runs**.  A dry
run cannot apply it, so `ninja -n` reports the whole downstream chain as
pending on a tree where a real build would do nothing.  Measured in a freshly
and fully built worktree (`wt/mp-gate` @ `cbe492813`, 2026-09-11), immediately
after a `ninja` that reached steady state::

    $ ninja -n build/373307D9/report.json
    [1/12] CHECK SPLIT CURRENT
    [2/12] GEN data-stub .obj files for lbl_* resolution
    ... 8 more guard/patcher stamp edges ...
    [12/12] REPORT

Nine further `always`-rooted edges were added between 2026-08-21 and now, so
`ninja -n` can no longer print `no work to do` for `report.json` in ANY tree,
main checkout included.  The gate was therefore permanently red, and its
message blamed a race that was not happening ("another process is probably
building there concurrently").  A guard that is always red is a guard nobody
runs -- the same failure shape as one that is always green.

The question this asks instead
------------------------------
Not *"does ninja have nothing to run"* but *"would running it change
report.json"* -- i.e. is every pending edge one of the deliberately
always-dirty guards, and are the target objects the ones the current split
config produces?

Two independent checks, each of which can fail on its own:

1. **Ninja root causes.**  `ninja -n -d explain <report>` explains every dirty
   node.  Exactly one of its message formats -- ``"%s is dirty"`` -- is pure
   PROPAGATION: it names a node without saying why, because the why is reported
   separately by one of the root-cause formats.  Filter the propagation lines
   out and what remains is the set of real reasons.  On a current tree the only
   root is the `always` phony; anything else (a recompiled object, a missing
   output, a changed command line, missing deps) is genuine staleness and is
   printed verbatim.

   The complete set of explain formats in the installed ninja, enumerated from
   the binary rather than assumed::

       $ strings -a $(command -v ninja) | grep -E "is dirty|doesn't exist|..."
       command line changed for %s
       deps for '%s' are missing
       output %s doesn't exist
       output %s of phony edge with no inputs doesn't exist
       output %s older than most recent input %s (%ld vs %ld)
       recorded mtime of %s older than most recent input %s (%ld vs %ld)
       %s is dirty

   Six root-cause formats, one propagation format.  `ninja --version` is
   recorded in the failure text so a format change shows up as an unexplained
   line rather than as silence.

2. **Split currency.**  `scripts/verify_split_current.py --check` -- the one
   input ninja genuinely cannot see, since `dtk xex split`'s 2,223 target
   objects are undeclared outputs.

3. **Post-compile fixed point.**  `scripts/verify_objs_patched.py --check`.
   The six patcher edges are always-rooted, so check 1 classifies them as
   benign -- which is only true while they are *no-ops*.  On a tree whose
   objects were compiled but not yet post-processed they do real work, the
   objects change, and REPORT re-runs: the report is stale and check 1 alone
   would call it current.  Measured on the main checkout 2026-09-11 while a
   sibling lane was mid-build: 20 pending `atexit_scope` patches under a
   ninja graph whose only roots were `always`.  ~8 s over 989 objects; pass
   ``--no-patch-check`` for the ~0.2 s graph-only view.

Anti-vacuity
------------
The dangerous failure of check 1 is that it degenerates into "no explain lines,
therefore nothing wrong".  So: if ninja reports pending work and *nothing*
roots it -- no root-cause line at all, not even the `always` phony's -- that is
`CANNOT_VERIFY`, not `CURRENT`.  It is impossible for a dirty graph to consist
purely of propagation, so that state means the parser has lost track of ninja's
output and must say so instead of passing.  `--selftest` exercises both
directions.

Exit codes
----------
    0   report.json is current (a real build would not rewrite it)
    1   STALE -- real pending work, or the split config moved.  Reasons printed.
    2   CANNOT VERIFY -- no build.ninja, ninja failed, or unparseable explain
        output.  Never conflated with 0.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from project_root import project_root  # noqa: E402

#: Default tree AND the place the two sibling guards are loaded from.  Derived
#: from the invocation, not `Path(__file__).resolve()` -- see
#: scripts/project_root.py.  (The guard CALLS below always pass the project dir
#: explicitly, so this file was never the one that measured the wrong tree; it
#: matters for `--project-dir`'s default and for loading a worktree's own copy
#: of the guards when it has one.)
REPO_ROOT = project_root(__file__)
VERSION = "373307D9"
REPORT_REL = f"build/{VERSION}/report.json"

EXPLAIN_PREFIX = "ninja explain: "

#: The ONE propagation format: ninja naming a dirty node without a reason.
#: Every genuine reason arrives as one of the six root-cause formats listed in
#: the module docstring, so stripping this leaves exactly the root causes.
_PROPAGATION_RE = re.compile(r"^(?P<node>.+) is dirty$")

#: The `always` phony (tools/project.py: "Phony edge that will always be
#: considered dirty by ninja") is dirty BY CONSTRUCTION on every build.  It is
#: the designed root of every guard edge, so it is the one benign root.  Both
#: spellings ninja can use for it are matched.
_ALWAYS_NODE = "always"
_ALWAYS_ROOT_RE = re.compile(
    r"^output always of phony edge with no inputs doesn't exist$"
)

CURRENT = "current"
STALE = "stale"
CANNOT_VERIFY = "cannot_verify"

_STATUS_EXIT = {CURRENT: 0, STALE: 1, CANNOT_VERIFY: 2}


class Verdict:
    """What the checker concluded, and every line it based that on."""

    def __init__(self, status, reasons=None, benign=None, detail=""):
        self.status = status
        self.reasons = list(reasons or [])
        self.benign = list(benign or [])
        self.detail = detail

    @property
    def exit_code(self) -> int:
        return _STATUS_EXIT[self.status]

    def __repr__(self):  # pragma: no cover - debugging aid
        return f"<Verdict {self.status} reasons={len(self.reasons)}>"


def classify_explain(text: str) -> tuple[list[str], list[str], bool]:
    """Split ninja's `-d explain` output into root causes and propagation.

    Returns ``(root_causes, propagation, saw_always_root)``.

    ``root_causes`` are the lines that state an actual reason and are not the
    `always` phony.  ``saw_always_root`` records whether the benign root was
    seen at all -- its absence on a graph with pending work is what makes a
    verdict unverifiable rather than clean.
    """
    roots: list[str] = []
    propagation: list[str] = []
    saw_always = False
    for raw in text.splitlines():
        line = raw.strip()
        if not line.startswith(EXPLAIN_PREFIX):
            continue
        body = line[len(EXPLAIN_PREFIX):].strip()
        m = _PROPAGATION_RE.match(body)
        if m:
            if m.group("node") == _ALWAYS_NODE:
                # `always is dirty` -- ninja's usual spelling for the phony.
                saw_always = True
            else:
                propagation.append(body)
            continue
        if _ALWAYS_ROOT_RE.match(body):
            saw_always = True
            continue
        roots.append(body)
    return roots, propagation, saw_always


def _load_split_checker():
    path = REPO_ROOT / "scripts" / "verify_split_current.py"
    spec = importlib.util.spec_from_file_location("_vsc_for_freshness", path)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def check_split_current(project_dir: Path) -> tuple[bool, str]:
    """The half of the question ninja cannot answer: are the TARGET objects
    the ones the config on disk produces?  Returns ``(ok, message)``."""
    try:
        vsc = _load_split_checker()
    except Exception as exc:  # pragma: no cover - import failure is infra
        return False, f"could not load verify_split_current.py: {exc}"
    try:
        return True, vsc.check(project_dir)
    except vsc.StaleSplitError as exc:
        return False, str(exc)


def check_patched_tree(project_dir: Path, timeout: int = 900) -> tuple[bool, str]:
    """Are `project_dir`'s objects a fixed point of the post-compile patchers?

    The patcher edges are `always`-rooted, so `classify_explain` files them as
    benign -- correct only while they no-op.  An unpatched tree makes them do
    real work, which rewrites the objects REPORT reads.  Returns ``(ok, msg)``.
    """
    verify = REPO_ROOT / "scripts" / "verify_objs_patched.py"
    if not verify.is_file():  # pragma: no cover - infra
        return False, f"{verify} is missing; cannot check the patch fixed point"
    try:
        proc = subprocess.run(
            [sys.executable, str(verify), "--repo", str(project_dir), "--check", "--quiet"],
            capture_output=True, text=True, timeout=timeout,
        )
    except (OSError, subprocess.SubprocessError) as exc:  # pragma: no cover
        return False, f"could not run verify_objs_patched.py: {exc}"
    if proc.returncode == 0:
        return True, (proc.stdout.strip().splitlines() or ["post-compile fixed point"])[-1]
    body = (proc.stdout + proc.stderr).strip()
    return False, (
        f"post-compile patchers have pending work in {project_dir} "
        f"(verify_objs_patched.py --check exit {proc.returncode}). The objects "
        f"report.json was measured from are about to change:\n" + body[-1500:]
    )


def run_ninja_explain(project_dir: Path, target: str, timeout: int = 300):
    """`ninja -n -d explain <target>`.  Returns ``(rc, stdout, combined)``."""
    proc = subprocess.run(
        ["ninja", "-n", "-d", "explain", target],
        cwd=str(project_dir),
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return proc.returncode, proc.stdout, proc.stdout + proc.stderr


def ninja_version(project_dir: Path) -> str:
    try:
        p = subprocess.run(["ninja", "--version"], cwd=str(project_dir),
                           capture_output=True, text=True, timeout=30)
        return p.stdout.strip() or "?"
    except (OSError, subprocess.SubprocessError):  # pragma: no cover
        return "?"


def check(project_dir, target: str = REPORT_REL, check_split: bool = True,
          check_patched: bool = True) -> Verdict:
    """Answer "is `target` current?" for `project_dir`."""
    project_dir = Path(project_dir).resolve()

    if not (project_dir / "build.ninja").is_file():
        return Verdict(
            CANNOT_VERIFY,
            detail=f"{project_dir}/build.ninja does not exist — there is no "
                   f"build graph to ask. Run configure.py / ninja there first.",
        )

    try:
        rc, stdout, combined = run_ninja_explain(project_dir, target)
    except (OSError, subprocess.SubprocessError) as exc:
        return Verdict(CANNOT_VERIFY, detail=f"could not run ninja in {project_dir}: {exc}")
    if rc != 0:
        return Verdict(
            CANNOT_VERIFY,
            detail=f"`ninja -n {target}` failed (rc={rc}) in {project_dir} — the "
                   f"build graph is broken:\n" + combined.strip()[-2000:],
        )

    roots, propagation, saw_always = classify_explain(combined)

    if "no work to do" in stdout:
        # No always-dirty edge reaches this target (or they were all removed).
        # Nothing pending at all is unambiguously current.
        verdict = Verdict(CURRENT, benign=propagation,
                          detail="ninja has no pending work for this target")
    elif roots:
        verdict = Verdict(
            STALE, reasons=roots, benign=propagation,
            detail=f"{len(roots)} real reason(s) to rebuild {target}",
        )
    elif saw_always:
        verdict = Verdict(
            CURRENT, benign=propagation,
            detail="the only pending edges are the always-dirty guards "
                   "(restat makes a real build a no-op for this target)",
        )
    else:
        # Work is pending and NOTHING explains it.  A dirty graph cannot be
        # pure propagation, so the parser has lost ninja's output.  Refuse.
        verdict = Verdict(
            CANNOT_VERIFY,
            detail=(
                f"`ninja -n {target}` reports pending work but `-d explain` "
                f"produced no root cause — not even the `always` phony.\n"
                f"This checker classifies ninja's explain messages, so it "
                f"cannot tell a real staleness from a guard edge here.\n"
                f"ninja: {ninja_version(project_dir)}; "
                f"{len(propagation)} propagation line(s) seen.\n"
                f"Re-read scripts/report_freshness.py's docstring against "
                f"`strings -a $(command -v ninja) | grep dirty`."
            ),
        )

    if check_split and verdict.status == CURRENT:
        ok, msg = check_split_current(project_dir)
        if not ok:
            return Verdict(STALE, reasons=[msg],
                           detail="ninja is settled but the split is not")
        verdict.detail += f"; {msg}"

    if check_patched and verdict.status == CURRENT:
        ok, msg = check_patched_tree(project_dir)
        if not ok:
            return Verdict(STALE, reasons=[msg],
                           detail="ninja is settled but the objects are not "
                                  "a post-compile fixed point")
        verdict.detail += f"; {msg}"
    return verdict


def format_verdict(project_dir, target: str, v: Verdict) -> str:
    head = {
        CURRENT: f"[report-freshness] CURRENT: {target} in {project_dir}",
        STALE: f"[report-freshness] STALE: {target} in {project_dir}",
        CANNOT_VERIFY: f"[report-freshness] CANNOT VERIFY: {target} in {project_dir}",
    }[v.status]
    lines = [head]
    if v.detail:
        lines.append(f"  {v.detail}")
    for r in v.reasons:
        for i, sub in enumerate(str(r).splitlines()):
            lines.append(("  - " if i == 0 else "    ") + sub)
    return "\n".join(lines)


# --------------------------------------------------------------------------- #
# Self-test: the classifier, in both directions, with no build tree required.
# --------------------------------------------------------------------------- #

_CLEAN_SAMPLE = """\
ninja explain: always is dirty
ninja explain: build/373307D9/split_current_checked.stamp is dirty
ninja explain: build/373307D9/data_stubs.stamp is dirty
ninja explain: post-compile is dirty
"""

_STALE_SAMPLE = """\
ninja explain: always is dirty
ninja explain: output build/373307D9/src/system/math/Geo.obj older than most \
recent input src/system/math/Geo.cpp (1789090374606714553 vs 1789090852320777711)
ninja explain: build/373307D9/split_current_checked.stamp is dirty
ninja explain: all_source is dirty
ninja explain: post-compile is dirty
"""


def selftest() -> int:
    failures = []

    roots, prop, always = classify_explain(_CLEAN_SAMPLE)
    if roots:
        failures.append(f"clean sample produced root causes: {roots}")
    if not always:
        failures.append("clean sample: the `always` root was not recognised")
    if len(prop) != 3:
        failures.append(f"clean sample: expected 3 propagation lines, got {len(prop)}")

    roots, prop, always = classify_explain(_STALE_SAMPLE)
    if len(roots) != 1 or "Geo.obj" not in roots[0]:
        failures.append(f"stale sample: expected the Geo.obj root, got {roots}")
    if not always:
        failures.append("stale sample: the `always` root was not recognised")

    # Anti-vacuity: propagation with no root at all must not read as clean.
    roots, prop, always = classify_explain(
        "ninja explain: post-compile is dirty\nninja explain: all_source is dirty\n"
    )
    if roots or always:
        failures.append("rootless sample was mis-parsed")

    # A format ninja does not use must not be silently swallowed.
    roots, _, _ = classify_explain("ninja explain: deps for 'x.obj' are missing\n")
    if len(roots) != 1:
        failures.append("`deps are missing` was not treated as a root cause")

    if failures:
        for f in failures:
            print(f"[report-freshness] SELFTEST FAIL: {f}", file=sys.stderr)
        return 1
    print("[report-freshness] selftest ok (4 classifier cases, both directions)")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project-dir", default=str(REPO_ROOT),
                    help="Tree to ask about (default: this repo)")
    ap.add_argument("--target", default=REPORT_REL,
                    help=f"Ninja target to gate (default: {REPORT_REL})")
    ap.add_argument("--no-split-check", action="store_true",
                    help="Skip verify_split_current.py (ninja graph only)")
    ap.add_argument("--no-patch-check", action="store_true",
                    help="Skip verify_objs_patched.py --check (~8 s over 989 "
                         "objects). Leaves the always-rooted patcher edges "
                         "ASSUMED to be no-ops rather than measured.")
    ap.add_argument("--quiet", action="store_true",
                    help="Print nothing when the verdict is CURRENT")
    ap.add_argument("--selftest", action="store_true",
                    help="Run the classifier self-test; needs no build tree")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    project_dir = Path(args.project_dir).resolve()
    v = check(project_dir, args.target,
              check_split=not args.no_split_check,
              check_patched=not args.no_patch_check)
    if v.status != CURRENT or not args.quiet:
        stream = sys.stdout if v.status == CURRENT else sys.stderr
        print(format_verdict(project_dir, args.target, v), file=stream)
    return v.exit_code


if __name__ == "__main__":
    sys.exit(main())
