#!/usr/bin/env python3
"""Run this repo's Python test suites — the population that had no caller.

Why this exists
---------------
Measured 2026-08-17 on branch point ``37614e655``: **nothing in this repo runs
any Python test.** 37 tracked test files, zero invocations. There is no
``[tool.pytest]`` section, no ``testpaths``, no tox/nox/Makefile target, and no
shell script or Python entry point that shells out to pytest. CI runs a native
GTest suite and no Python tests at all.

That is the same defect class as a vacuous assertion, approached from the other
side: the assertions here are fine, nobody ever asks them anything. This script
is the caller. Ported from decomp-synth's ``scripts/test_tools.py`` (landed
there as ``0bda4538``) and matching its contract, with one addition the game
repos need — see "Script mode" below.

Why a separate runner instead of a ``testpaths`` pin
----------------------------------------------------
1. **The baseline is not green.** Making a bare ``pytest`` red for every peer on
   day one gets the change reverted, not the tests fixed. This runner holds a
   checked-in list of the known-bad entries
   (``scripts/test_tools_known_bad.txt``) and fails only on a failure that is
   NOT on it, so pre-existing breakage is visible and inert while a NEW failure
   is loud.
2. **One pytest process per collection root.** Test directories that are not
   packages put each root on ``sys.path``, and same-named modules across roots
   then shadow each other. ``testpaths`` cannot express per-root processes.
3. **Some of these files are not pytest tests at all** (script mode, below).

Script mode
-----------
The decomp-synth original assumes every ``test_*.py`` is a pytest module. In the
game repos that is false: several files named ``test_*.py`` / ``*_test.py`` are
``main()``-style self-checks that pytest collects **zero** tests from (exit code
5). A naive lane calls those BROKEN, which is wrong — some of them are real
checks (``scripts/test_certify_floor.py`` in dc3 builds a synthetic
``decomp.db`` and asserts against it). So there are three arms, not one:

* **pytest roots** — one pytest process each, red list diffed against the
  manifest.
* **SCRIPT_ARM** — run as ``python3 <file>``; a nonzero exit is the failure.
* **EXCLUDED** — files this lane deliberately does not run, each with a written
  reason, printed on every run. This is the escape hatch that must never be
  quiet: an excluded file is a file with no caller, which is the thing this lane
  exists to abolish.

Anti-staleness
--------------
The roots and the two tables are described by hand. Any tracked test file that
matches no root, no script-arm entry and no exclusion is reported as UNCOVERED
and exits non-zero — so adding a test directory is a two-line change here, not a
silent escape. A manifest entry that now PASSES is reported as STALE, so the
manifest cannot rot in the other direction either.

Usage
-----
    python3 scripts/test_tools.py                 # whole lane
    python3 scripts/test_tools.py --root tools
    python3 scripts/test_tools.py --list
    python3 scripts/test_tools.py --strict-manifest   # stale entries are fatal
    python3 scripts/test_tools.py --no-script-arm     # pytest roots only

Exit codes: 0 = no NEW failure; 1 = new failure, timeout, broken root, script
failure, or uncovered file.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
MANIFEST = REPO_ROOT / "scripts" / "test_tools_known_bad.txt"

# ── collection roots ──────────────────────────────────────────────────────────
# One pytest process each. ``ignore`` keeps a parent root from swallowing a
# child root (or a script-arm file). ``timeout`` is a per-root wall-clock
# ceiling in seconds, sized generously against the baseline measured
# 2026-08-17 on this box; a root that hits it is reported as TIMEOUT, a FAILURE
# outcome distinct from a red test, because a hang is exactly the shape this
# lane must not silently absorb.
STATIC_ROOTS: list[dict] = [
    {"path": "tests", "timeout": 600},
    {"path": "msvc-src/tools", "timeout": 600,
     "deselect": [
         # DESTROYS A TRACKED FIXTURE. test_stable_json_export re-exports
         # bundle.json IN PLACE over msvc-src/analysis/il-fixtures/<name>/ via
         # `il_parser.py export-json <bundle_dir>`, and never restores it. The
         # re-export drops the `debug` block, so running the lane deleted 476
         # lines from il_type_control_cast_vs_and/bundle.json and left the
         # working tree dirty (measured 2026-08-17).
         #
         # It cannot be handled by the known-bad manifest: a manifested entry
         # still RUNS, and running this one corrupts the repo. Same category as
         # a hang — hence a deselect, printed on every run.
         #
         # It also took a second test down with it.
         # test_il_permuter.py::TestFixtures::test_cast_vs_and_hashes_differ
         # failed with `KeyError: '?cast_shift@@YAII@Z'` in every whole-root run
         # and in the pre-lane per-file audit (which ran the files alphabetically
         # in one tree, so test_il_fixtures had already gutted the fixture).
         # Verified: on a clean fixture, test_il_permuter.py is 6 passed. That
         # red was pollution, not a defect.
         #
         # The fix is small and belongs to whoever owns msvc-src/tools: copy the
         # bundle dir to a tempdir, export THERE, and compare. The determinism
         # assertion is correct; only its choice of scratch space is not.
         "msvc-src/tools/test_il_fixtures.py::"
         "TestCrossBundleConsistency::test_stable_json_export",
     ]},
    {"path": "scripts/analysis/tests", "timeout": 600},
    # test_context_collector.py measured ~80 s.
    {"path": "scripts/orchestrator/tests", "timeout": 900},
    {"path": "scripts/unicorn_runner/tests", "timeout": 600},
    # tools/ top level only — the two test packages below are their own roots.
    {"path": "tools", "timeout": 600,
     "ignore": ["tools/compiler_trace", "tools/state_diff"]},
    {"path": "tools/compiler_trace/tests", "timeout": 600},
    {"path": "tools/state_diff/tests", "timeout": 600},
]

# NOTE: there is deliberately no ``scripts`` root. All three tracked
# scripts/test_*.py files are main()-style, so a pytest process over that
# directory would collect zero tests and report BROKEN. They are covered by
# SCRIPT_ARM and EXCLUDED below; a NEW scripts/test_*.py would show up as
# UNCOVERED, which is the intended prompt to decide which arm it belongs in.

# ── script mode ───────────────────────────────────────────────────────────────
# ``main()``-style self-checks: pytest collects 0 tests (rc=5), but running them
# as a program is a real check. Run as ``python3 <path>``; rc 0 is pass.
SCRIPT_ARM: list[dict] = [
    # Real, self-contained checks. Both build a synthetic decomp.db in a temp
    # dir, assert against it, print [PASS]/[FAIL] per case and return 0/1 from
    # main(). They say "no pytest required" in their own docstrings and they
    # mean it. Converting them to pytest modules would be a rewrite of two
    # working 200-450 line harnesses for no gain; giving them a caller is the
    # gain. Both measured rc=0 on 2026-08-17.
    {"path": "scripts/test_certify_floor.py", "timeout": 300},
    {"path": "scripts/test_measurement_sync.py", "timeout": 300},
    # 12 real unittest cases (10 pass, 2 skip). It CANNOT run under pytest, and
    # not for a fixable reason: pytest names it `unicorn.test_refresh` (basedir
    # scripts/, package unicorn), which collides with the third-party emulator
    # package of the same name -- once anything imports the real bindings,
    # `ModuleNotFoundError: No module named 'unicorn.test_refresh'`. Ordering
    # cannot fix a module-NAME collision; only renaming scripts/unicorn or
    # adding scripts/__init__.py would, and both change import semantics for
    # every test tree under scripts/. Script mode is the honest answer.
    {"path": "scripts/unicorn/test_refresh.py", "timeout": 300},
]

# ── deliberate exclusions ─────────────────────────────────────────────────────
# Every entry needs a reason. Printed on every run.
EXCLUDED: list[dict] = [
    {"path": "msvc-src/tools/diff_test.py",
     "why": "Not a test: a 1908-line differential-testing HARNESS for the MSVC "
            "c2.dll compiler. Requires --suite (argparse errors without it), "
            "needs the wibo/MSVC toolchain, and writes results JSON. Named "
            "*_test.py by accident of convention."},
    {"path": "scripts/build/link_test.py",
     "why": "Not a test: a 'standalone proof-of-concept' that links split .obj "
            "files back into a PE. Needs build/373307D9/config.json, wibo and "
            "X360 link.exe. Measured rc=1 on a clean tree ('Build config not "
            "found. Run ninja first'), so script mode would ship red on every "
            "checkout without a built target."},
    {"path": "scripts/test_burnxfm_variations.py",
     "why": "A compile-sweep research script, and running it would be a "
            "VACUOUS control: measured 2026-08-17 it exits 0 on a clean tree "
            "while reporting 0.00% for every single variation, because there "
            "is no build to measure. rc=0 with no signal is exactly the shape "
            "this campaign keeps finding; do not wire it up as a check."},
    {"path": "scripts/analysis/mogg_unicorn_test.py",
     "why": "Not a test: a one-off research script that emulates setupCypher "
            "out of orig-assets/debug.xex (a large binary asset this box does "
            "not track) and PRINTS a comparison rather than asserting one. Its "
            "own source comments still say the entry addresses are unresolved "
            "('WRONG, this isn't setupCypher'). Its unicorn path setup was "
            "repaired anyway so it does not hard-fail on import."},
]

TEST_FILE_RE = re.compile(r"(^|/)(test_[^/]*\.py|[^/]*_test\.py)$")

# This runner is itself named test_tools.py, so it matches TEST_FILE_RE. Once it
# was committed the lane reported ITSELF as an UNCOVERED tracked test file and
# exited 1 (measured 2026-08-17 — the anti-staleness check working correctly on
# the wrong input). It is also handed to pytest by any root that contains it,
# which collects zero tests from it. Excluded from both.
SELF = Path(__file__).resolve().relative_to(REPO_ROOT).as_posix()

# pytest short-summary lines. The id runs to the first " - " (pytest's separator
# before the exception's first line); parametrized ids contain spaces, so
# ``\S+`` would truncate them.
SUMMARY_RE = re.compile(r"^(FAILED|ERROR)\s+(.+?)(?:\s+-\s.*)?$")
COUNT_RE = re.compile(
    r"(\d+) (passed|failed|error|errors|skipped|xfailed|xpassed|deselected)")


def _strip_ansi(text: str) -> str:
    return re.sub(r"\x1b\[[0-9;]*m", "", text)


# ── discovery ─────────────────────────────────────────────────────────────────

def discover_roots() -> list[dict]:
    return [dict(r) for r in STATIC_ROOTS]


def tracked_test_files() -> list[str]:
    """Every TRACKED test file in the repo (untracked WIP is out of scope)."""
    try:
        out = subprocess.run(
            ["git", "-C", str(REPO_ROOT), "ls-files"],
            capture_output=True, text=True, check=True).stdout
        files = out.splitlines()
    except (subprocess.CalledProcessError, FileNotFoundError):
        files = [p.relative_to(REPO_ROOT).as_posix()
                 for p in REPO_ROOT.rglob("*.py")]
    return sorted(f for f in files if TEST_FILE_RE.search(f) and f != SELF)


def _claimed_by_a_root(f: str, roots: list[dict]) -> bool:
    owners = [r for r in roots
              if f == r["path"] or f.startswith(r["path"].rstrip("/") + "/")]
    for r in owners:
        ignored = any(f == i or f.startswith(i.rstrip("/") + "/")
                      for i in r.get("ignore", []))
        if not ignored:
            return True
    return False


def coverage_gaps(roots: list[dict]) -> list[str]:
    """Tracked test files that nothing in this lane would run."""
    handled = ({e["path"] for e in SCRIPT_ARM} | {e["path"] for e in EXCLUDED})
    return [f for f in tracked_test_files()
            if f not in handled and not _claimed_by_a_root(f, roots)]


# ── known-bad manifest ────────────────────────────────────────────────────────

def load_manifest() -> list[str]:
    if not MANIFEST.is_file():
        return []
    entries = []
    for line in MANIFEST.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            entries.append(line)
    return entries


def manifest_match(entry: str, observed: str) -> bool:
    """An entry matches a failure id exactly, or covers a whole file/class.

    ``a/test_b.py`` covers ``a/test_b.py::test_c`` and its parametrizations;
    ``...::test_c`` covers ``...::test_c[case-1]``.
    """
    return (observed == entry
            or observed.startswith(entry + "::")
            or observed.startswith(entry + "["))


# ── running ───────────────────────────────────────────────────────────────────

def run_root(root: dict, python: str, extra: list[str], log_dir: Path) -> dict:
    cmd = [python, "-m", "pytest", "-q", "--tb=no", "-rEf",
           "--continue-on-collection-errors", "-p", "no:cacheprovider"]
    ignores = list(root.get("ignore", []))
    if SELF.startswith(root["path"].rstrip("/") + "/"):
        ignores.append(SELF)
    # A script-arm or excluded file inside this root must not be collected:
    # pytest would report it as an error or as "no tests ran".
    for entry in SCRIPT_ARM + EXCLUDED:
        p = entry["path"]
        if p.startswith(root["path"].rstrip("/") + "/") and p not in ignores:
            ignores.append(p)
    for ig in ignores:
        cmd.append(f"--ignore={ig}")
    for ds in root.get("deselect", []):
        cmd.extend(["--deselect", ds])
    cmd.append(root["path"])
    cmd.extend(extra)

    # Spool pytest's output to a FILE, never to a pipe the runner buffers: a
    # test that emits enough captured stdout can MemoryError pytest's own
    # capture buffer, and a pipe-buffering parent would die alongside it.
    log_path = log_dir / (root["path"].replace("/", "_") + ".log")
    started = time.time()
    with log_path.open("wb") as fh:
        proc = subprocess.Popen(cmd, cwd=REPO_ROOT, stdout=fh,
                                stderr=subprocess.STDOUT)
        try:
            rc = proc.wait(timeout=root["timeout"])
            timed_out = False
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            rc = -1
            timed_out = True

    out = _strip_ansi(log_path.read_text(errors="replace")[-2_000_000:])

    observed = []
    for line in out.splitlines():
        m = SUMMARY_RE.match(line.strip())
        if m:
            observed.append(m.group(2))
    tail = [ln for ln in out.splitlines() if COUNT_RE.search(ln)]

    # A root that neither passed cleanly (rc 0) nor produced a parseable red
    # list (rc 1 + at least one FAILED/ERROR line) has gone wrong in a way the
    # known-bad manifest cannot express: internal error, usage error, nothing
    # collected (rc 5 — the script-mode shape), OOM, killed. That must be
    # FATAL, otherwise a crashed root reports as "no new failures".
    broken = (not timed_out
              and (rc not in (0, 1) or (rc == 1 and not observed)))
    return {
        "root": root["path"],
        "rc": rc,
        "timed_out": timed_out,
        "broken": broken,
        "observed": sorted(set(observed)),
        "counts": tail[-1].strip() if tail else "(no count line)",
        "elapsed": time.time() - started,
        "log": log_path,
    }


def run_script(entry: dict, python: str, log_dir: Path) -> dict:
    """Run a main()-style self-check as a program. rc 0 is pass."""
    log_path = log_dir / ("script_" + entry["path"].replace("/", "_") + ".log")
    started = time.time()
    with log_path.open("wb") as fh:
        proc = subprocess.Popen([python, entry["path"]], cwd=REPO_ROOT,
                                stdin=subprocess.DEVNULL, stdout=fh,
                                stderr=subprocess.STDOUT)
        try:
            rc = proc.wait(timeout=entry.get("timeout", 300))
            timed_out = False
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            rc = -1
            timed_out = True
    tail = _strip_ansi(log_path.read_text(errors="replace")).strip().splitlines()
    return {
        "path": entry["path"],
        "rc": rc,
        "timed_out": timed_out,
        "last": tail[-1][:80] if tail else "(no output)",
        "elapsed": time.time() - started,
        "log": log_path,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", action="append", default=None,
                    help="only run this collection root (repeatable)")
    ap.add_argument("--list", action="store_true",
                    help="list the roots, the script arm and the exclusions")
    ap.add_argument("--python", default=sys.executable,
                    help="interpreter used to run pytest (default: this one)")
    ap.add_argument("--strict-manifest", action="store_true",
                    help="a known-bad entry that now PASSES is fatal too")
    ap.add_argument("--no-script-arm", action="store_true",
                    help="skip the main()-style self-checks")
    ap.add_argument("--verbose-failures", action="store_true",
                    help="dump each failing root's output")
    ap.add_argument("--log-dir", default=None,
                    help="where per-root logs are spooled "
                         "(default: a temp dir, path printed)")
    ap.add_argument("pytest_args", nargs="*",
                    help="extra args forwarded to every pytest invocation")
    args = ap.parse_args()

    roots = discover_roots()
    if args.list:
        for r in roots:
            ig = f"  (ignore: {', '.join(r['ignore'])})" if r.get("ignore") else ""
            print(f"pytest  {r['path']}{ig}   timeout={r['timeout']}s")
        for e in SCRIPT_ARM:
            print(f"script  {e['path']}   timeout={e.get('timeout', 300)}s")
        for e in EXCLUDED:
            print(f"EXCL    {e['path']}   {e['why']}")
        return 0
    if args.root:
        wanted = {p.rstrip("/") for p in args.root}
        roots = [r for r in roots if r["path"] in wanted]
        if not roots:
            print(f"no such root: {args.root}", file=sys.stderr)
            return 1

    known_bad = load_manifest()
    gaps = coverage_gaps(discover_roots())

    log_dir = Path(args.log_dir) if args.log_dir else Path(
        tempfile.mkdtemp(prefix="test-tools-"))
    log_dir.mkdir(parents=True, exist_ok=True)

    script_arm = [] if args.no_script_arm else SCRIPT_ARM
    print(f"test lane — {len(roots)} pytest root(s), {len(script_arm)} "
          f"script-mode file(s), {len(known_bad)} known-bad entr(ies)")
    print(f"logs: {log_dir}\n")

    results = []
    for r in roots:
        res = run_root(r, args.python, args.pytest_args, log_dir)
        results.append(res)
        state = ("TIMEOUT" if res["timed_out"]
                 else "BROKEN" if res["broken"]
                 else "ok" if res["rc"] == 0 else "red")
        print(f"  [{state:>7}] {res['root']:<44} {res['counts']} "
              f"({res['elapsed']:.0f}s)", flush=True)

    script_results = []
    for e in script_arm:
        res = run_script(e, args.python, log_dir)
        script_results.append(res)
        state = ("TIMEOUT" if res["timed_out"]
                 else "ok" if res["rc"] == 0 else "FAILED")
        print(f"  [{state:>7}] {res['path']:<44} rc={res['rc']} "
              f"{res['last']} ({res['elapsed']:.0f}s)", flush=True)

    print()
    if EXCLUDED:
        print(f"EXCLUDED — tracked test-named files this lane does NOT run "
              f"({len(EXCLUDED)}). Each is a file with no caller; shrink this "
              f"list, do not grow it:")
        for e in EXCLUDED:
            print(f"    {e['path']}\n        {e['why']}")
        print()

    deselected = [(r["path"], d) for r in roots for d in r.get("deselect", [])]
    if deselected:
        print(f"DESELECTED — the known-bad manifest cannot cover these, "
              f"because a manifested test still RUNS (these hang, or corrupt "
              f"the tree). Reasons are in {Path(__file__).name}, STATIC_ROOTS "
              f"({len(deselected)}):")
        for _root, d in deselected:
            print(f"    {d}")
        print()

    new_failures: list[str] = []
    expected_hit: set[str] = set()
    for res in results:
        for obs in res["observed"]:
            hit = next((e for e in known_bad if manifest_match(e, obs)), None)
            if hit:
                expected_hit.add(hit)
            else:
                new_failures.append(obs)

    # A manifest entry for a root we did not run this time is not stale, and
    # neither is one inside a root's ``ignore`` subtree.
    def _in_scope(entry: str) -> bool:
        for r in roots:
            if not (entry == r["path"]
                    or entry.startswith(r["path"].rstrip("/") + "/")):
                continue
            if any(entry == i or entry.startswith(i.rstrip("/") + "/")
                   for i in r.get("ignore", [])):
                continue
            return True
        return False

    stale = [e for e in known_bad if e not in expected_hit and _in_scope(e)]

    timeouts = [r["root"] for r in results if r["timed_out"]]
    timeouts += [r["path"] for r in script_results if r["timed_out"]]
    broken = [(r["root"], r["rc"]) for r in results if r["broken"]]
    script_failed = [(r["path"], r["rc"]) for r in script_results
                     if r["rc"] != 0 and not r["timed_out"]]

    if expected_hit:
        print(f"KNOWN-BAD, reported not failed ({len(expected_hit)}):")
        for e in sorted(expected_hit):
            print(f"    {e}")
        print()
    if stale:
        print(f"STALE known-bad entries — these PASSED, drop them from "
              f"{MANIFEST.relative_to(REPO_ROOT)} ({len(stale)}):")
        for e in sorted(stale):
            print(f"    {e}")
        print()
    if gaps:
        print(f"UNCOVERED tracked test files — no root, no script-arm entry "
              f"and no exclusion claims them; add one in "
              f"{Path(__file__).name} ({len(gaps)}):")
        for g in gaps:
            print(f"    {g}")
        print()
    if timeouts:
        print(f"TIMEOUT ({len(timeouts)}): " + ", ".join(timeouts) + "\n")
    if broken:
        print(f"BROKEN roots — pytest exited unusably (internal error, no "
              f"tests collected, OOM, killed); the manifest cannot cover these "
              f"({len(broken)}):")
        for root, rc in broken:
            print(f"    {root}  (pytest rc={rc})")
        print()
    if script_failed:
        print(f"SCRIPT-MODE FAILURES ({len(script_failed)}):")
        for path, rc in script_failed:
            print(f"    {path}  (rc={rc})")
        print()
    if new_failures:
        print(f"NEW FAILURES ({len(new_failures)}):")
        for f in sorted(new_failures):
            print(f"    {f}")
        print()

    if args.verbose_failures:
        for res in results:
            if res["rc"] != 0 or res["timed_out"]:
                body = res["log"].read_text(errors="replace")[-40_000:]
                print(f"───── {res['root']} ─────\n{_strip_ansi(body)}\n")
        for res in script_results:
            if res["rc"] != 0:
                body = res["log"].read_text(errors="replace")[-40_000:]
                print(f"───── {res['path']} ─────\n{_strip_ansi(body)}\n")

    bad = bool(new_failures or gaps or timeouts or broken or script_failed
               or (args.strict_manifest and stale))
    print("RESULT: " + ("FAIL" if bad else "PASS")
          + f"  (new={len(new_failures)} timeout={len(timeouts)} "
            f"broken={len(broken)} script-fail={len(script_failed)} "
            f"uncovered={len(gaps)} stale={len(stale)} "
            f"known-bad-hit={len(expected_hit)})")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
