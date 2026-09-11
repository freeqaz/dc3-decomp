#!/usr/bin/env python3
"""A failed build must never read as a clean run.

The defect these lock down
--------------------------
`scripts/measure_progress.sh` builds two trees and compares their
`report.json`s.  Every one of those builds used to report failure the same
way -- a bare `exit 1`, indistinguishable from a usage error -- and one of
them did not report it at all.  Measured 2026-09-11 in `wt/mp-exit` with a
`ninja` shim that fails only for the tree being built::

    $ PATH=/tmp/mp-sabotage/bin:$PATH scripts/measure_progress.sh --allow-stale HEAD~1
    ...
    WARNING (--allow-stale): current (working tree) (...): rebuild of
                             build/373307D9/report.json failed.
    ...
    Overall normalized-weighted: 55.11% -> 55.11% (+0.00%)
    EXIT CODE = 0

A full, authoritative-looking table over a report the failed build never
refreshed, and `$?` said the run was clean.  `--allow-stale` exists to let you
compare numbers that may be out of date; it was also silently downgrading
"the build did not run", which is not a staleness judgement at all.

What is asserted here
---------------------
The *exit contract*, not the arithmetic: 10 = baseline build failed, 11 =
current tree build failed, 12 = comparison failed, 0 = a COMPLETE table was
printed -- and on 10/11/12 no table is printed at all.

How, without a 15-minute build
------------------------------
`_Fixture` builds a throwaway git repo that is shaped like this project from
`measure_progress.sh`'s point of view -- a `build.ninja` naming a `dtk` and an
`objdiff-cli`, a `configure.py`, a `scripts/`, a `report.json` -- with every
expensive part replaced by a stub that reads one environment variable and
either succeeds or fails.  The REAL `measure_progress.sh` is copied in and run
end to end against it; the sabotage knobs live in the fixture, never in
production code.  A whole case runs in ~2 s.

`TestSabotage` is the second layer, and the reason to trust the first: it
rewrites the production script one way at a time -- restoring, verbatim, the
code that shipped before 2026-09-11 -- and requires the matching case above to
go RED.  A mutation nobody's assertion catches is reported UNDETECTED.

Run:  python3 -m pytest tests/test_measure_progress_exit.py -q
      python3 tests/test_measure_progress_exit.py          (unittest main)
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
MEASURE = REPO_ROOT / "scripts" / "measure_progress.sh"

# TestSabotage re-runs a single case in a subprocess with a MUTATED copy of the
# production script.  Swapping MEASURE here, at import, is what makes every
# assertion below -- fixture-driven and header-reading alike -- speak about the
# mutated copy rather than about the real one.  Nothing outside a temp dir is
# ever written.
if os.environ.get("MP_FIXTURE_SCRIPT_OVERRIDE"):
    _ov = Path(tempfile.mkdtemp(prefix="mp-exit-mutant-")) / "measure_progress.sh"
    _ov.write_text(os.environ["MP_FIXTURE_SCRIPT_OVERRIDE"])
    _ov.chmod(0o755)
    MEASURE = _ov

# The contract, in one place.  Kept in step with the header table of
# scripts/measure_progress.sh.
EXIT_OK = 0
EXIT_REFUSAL = 1
EXIT_BASELINE_BUILD = 10
EXIT_CURRENT_BUILD = 11
EXIT_COMPARE = 12

TABLE_MARKER = "FIXTURE COMPARISON TABLE"


# --------------------------------------------------------------------------- #
# Fixture stubs.  Each reads exactly one env var and fails when it says "fail",
# so a test names the step it is breaking instead of arranging a real failure.
# --------------------------------------------------------------------------- #

MKREPORT = '''\
#!/usr/bin/env python3
"""Stand in for the whole report.json build. argv: <env-var-name> <out-path>."""
import os, sys, pathlib
knob, out = sys.argv[1], pathlib.Path(sys.argv[2])
if os.environ.get(knob) == "fail":
    print(f"FIXTURE: {knob}=fail — refusing to build {out}", file=sys.stderr)
    print("ninja: build stopped: subcommand failed", file=sys.stderr)
    sys.exit(1)
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text('{"units": [], "measures": {}}\\n')
print(f"FIXTURE: wrote {out}")
'''

DTK = '''\
#!/usr/bin/env python3
"""Stand in for `dtk xex split`: writes the config.json the split must produce."""
import os, sys, pathlib
if os.environ.get("MP_FIXTURE_SPLIT") == "fail":
    print("FIXTURE: dtk split failed", file=sys.stderr)
    sys.exit(1)
out = pathlib.Path(sys.argv[-1])          # .../build/373307D9
out.mkdir(parents=True, exist_ok=True)
(out / "config.json").write_text("{}\\n")
print("FIXTURE: split ok")
'''

OBJDIFF = '''\
#!/usr/bin/env python3
"""Never executed; it exists so build.ninja can name an objdiff-cli path."""
import sys
print("FIXTURE objdiff-cli 0.0.0")
sys.exit(0)
'''

FRESHNESS = '''\
#!/usr/bin/env python3
"""Stand in for scripts/report_freshness.py (0 current | 1 stale | 2 unknown).

MP_FIXTURE_FRESHNESS_ONCE=1 makes the FIRST call stale and every later call
current, which is the "report was stale, the rebuild fixed it" shape.
"""
import os, pathlib, sys
rc = int(os.environ.get("MP_FIXTURE_FRESHNESS", "0"))
if os.environ.get("MP_FIXTURE_FRESHNESS_ONCE") == "1":
    seen = pathlib.Path(os.environ["MP_FIXTURE_STATE"]) / "freshness_seen"
    if seen.exists():
        rc = 0
    else:
        seen.write_text("1")
print(f"FIXTURE freshness: exit {rc}", file=sys.stderr)
sys.exit(rc)
'''

COMPARE = '''\
#!/usr/bin/env python3
"""Stand in for scripts/analysis/compare_progress.py."""
import os, sys
print("=" * 70)
print("FIXTURE COMPARISON TABLE")
print("| Subsystem | Baseline | Current |")
if os.environ.get("MP_FIXTURE_COMPARE") == "fail":
    print("| partial   | 1.0      | ...")       # a TRUNCATED table, on stdout
    print("FIXTURE: compare_progress blew up", file=sys.stderr)
    sys.exit(3)
print("| all       | 1.0      | 1.0     |")
print("=" * 70)
'''

CONFIGURE = '''\
#!/usr/bin/env python3
"""Stand in for configure.py: writes the baseline worktree's build.ninja."""
import os, pathlib, sys
if os.environ.get("MP_FIXTURE_CONFIGURE") == "fail":
    print("FIXTURE: configure.py failed", file=sys.stderr)
    sys.exit(1)
here = pathlib.Path.cwd()
(here / "_fixture_mkreport.py").write_text(
    pathlib.Path(os.environ["MP_FIXTURE_MKREPORT"]).read_text())
(here / "build.ninja").write_text(
    "rule mkreport\\n"
    "  command = python3 _fixture_mkreport.py MP_FIXTURE_BASELINE_BUILD $out\\n"
    "  description = REPORT\\n"
    "\\n"
    "build build/373307D9/report.json: mkreport\\n")
print("FIXTURE: configured", " ".join(sys.argv[1:]))
'''

MAIN_BUILD_NINJA = """\
# Fixture manifest for the MAIN side.  The `split` and `report` rules exist so
# measure_progress.sh's tool_from_ninja() can read which dtk / objdiff-cli this
# tree builds with; only the mkreport edge is ever executed.
rule split
  command = {dtk} xex split config/373307D9/config.yml build/373307D9
  description = SPLIT

rule report
  command = {objdiff} report generate -o $out
  description = REPORT

rule mkreport
  command = python3 {mkreport} MP_FIXTURE_CURRENT_BUILD $out
  description = REPORT

build build/373307D9/report.json: mkreport
"""


class _Fixture:
    """A throwaway repo shaped like dc3-decomp, with every build step stubbed."""

    def __init__(self, script_text: str | None = None):
        self.root = Path(tempfile.mkdtemp(prefix="mp-exit-fixture-"))
        self.state = self.root / ".fixture-state"
        self.state.mkdir()
        self.baseline_wt = self.root.parent / (self.root.name + "-baseline")

        def w(rel: str, text: str, mode: int = 0o644) -> Path:
            p = self.root / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text)
            p.chmod(mode)
            return p

        # -- tracked: what a `git worktree add` + reset must reproduce -------- #
        w("scripts/measure_progress.sh",
          script_text if script_text is not None else MEASURE.read_text(), 0o755)
        w("scripts/report_freshness.py", FRESHNESS, 0o755)
        w("scripts/analysis/compare_progress.py", COMPARE, 0o755)
        w("configure.py", CONFIGURE, 0o755)
        w("config/373307D9/config.yml", "fixture: true\n")
        w(".gitignore", "build/\nbin/\norig\n")

        # -- untracked: tools + build outputs --------------------------------- #
        self.mkreport = w("bin/_fixture_mkreport.py", MKREPORT, 0o755)
        self.dtk = w("bin/dtk", DTK, 0o755)
        self.objdiff = w("bin/objdiff-cli", OBJDIFF, 0o755)
        w("build/tools/.keep", "")
        (self.root / "orig" / "373307D9").mkdir(parents=True)
        w("build.ninja", MAIN_BUILD_NINJA.format(
            dtk=self.dtk, objdiff=self.objdiff, mkreport=self.mkreport))
        w("build/373307D9/report.json", '{"units": [], "measures": {}}\n')

        env = {"GIT_AUTHOR_NAME": "f", "GIT_AUTHOR_EMAIL": "f@x",
               "GIT_COMMITTER_NAME": "f", "GIT_COMMITTER_EMAIL": "f@x"}
        for args in (["init", "-q", "-b", "main"],
                     ["add", "-A"],
                     ["commit", "-q", "-m", "baseline"],
                     ["commit", "-q", "--allow-empty", "-m", "head"]):
            subprocess.run(["git", *args], cwd=self.root, check=True,
                           env={**os.environ, **env})

    def script(self) -> Path:
        return self.root / "scripts" / "measure_progress.sh"

    def rewrite_script(self, text: str) -> None:
        self.script().write_text(text)
        self.script().chmod(0o755)

    def run(self, *args: str, **knobs: str) -> subprocess.CompletedProcess:
        env = {
            **os.environ,
            "MP_FIXTURE_MKREPORT": str(self.mkreport),
            "MP_FIXTURE_STATE": str(self.state),
            # `git worktree add` inside a repo whose parent is a repo: keep git
            # from wandering upward out of the fixture.
            "GIT_CEILING_DIRECTORIES": str(self.root.parent),
        }
        env.update(knobs)
        return subprocess.run(
            [str(self.script()), "--worktree", str(self.baseline_wt), *args],
            cwd=self.root, capture_output=True, text=True, timeout=300, env=env)

    def cleanup(self) -> None:
        shutil.rmtree(self.root, ignore_errors=True)
        shutil.rmtree(self.baseline_wt, ignore_errors=True)


class _FixtureCase(unittest.TestCase):
    def setUp(self):
        self.fx = _Fixture()
        self.addCleanup(self.fx.cleanup)

    def assertNoTable(self, p: subprocess.CompletedProcess, why: str):
        self.assertNotIn(
            TABLE_MARKER, p.stdout,
            f"{why}: a comparison table was printed on a failing run\n{p.stdout}")


class TestHealthyRun(_FixtureCase):
    """GREEN control.  Without it every RED below could be an always-red gate."""

    def test_exits_zero_and_prints_a_complete_table(self):
        p = self.fx.run("HEAD~1")
        self.assertEqual(p.returncode, EXIT_OK, p.stdout + p.stderr)
        self.assertIn(TABLE_MARKER, p.stdout, p.stdout + p.stderr)
        self.assertIn("end of comparison", p.stdout,
                      "a complete run must say so; otherwise a truncated "
                      "capture cannot be told from a complete one")

    def test_the_baseline_really_was_built(self):
        """Anti-vacuity: the healthy case must exercise the path it is a control for."""
        p = self.fx.run("HEAD~1")
        self.assertIn("Building baseline report", p.stdout, p.stdout + p.stderr)
        self.assertIn("Generating baseline split config", p.stdout)


class TestBaselineBuildFailure(_FixtureCase):
    def test_failing_baseline_ninja_exits_10_with_a_banner(self):
        p = self.fx.run("HEAD~1", MP_FIXTURE_BASELINE_BUILD="fail")
        self.assertEqual(p.returncode, EXIT_BASELINE_BUILD, p.stdout + p.stderr)
        self.assertIn("BASELINE BUILD FAILED", p.stderr)
        self.assertNoTable(p, "baseline ninja failed")

    def test_the_banner_names_the_worktree_and_the_step(self):
        p = self.fx.run("HEAD~1", MP_FIXTURE_BASELINE_BUILD="fail")
        self.assertIn(str(self.fx.baseline_wt), p.stderr,
                      "the banner does not say WHICH tree failed")
        self.assertIn("ninja build/373307D9/report.json", p.stderr,
                      "the banner does not say WHICH step failed")
        self.assertIn("FIXTURE: MP_FIXTURE_BASELINE_BUILD=fail", p.stderr,
                      "the build log was discarded instead of being shown")

    def test_the_verdict_is_the_last_line(self):
        """A refusal buried under 'Removing temporary worktree...' is not a refusal."""
        p = self.fx.run("HEAD~1", MP_FIXTURE_BASELINE_BUILD="fail")
        last = [l for l in p.stderr.splitlines() if l.strip()][-1]
        self.assertIn("measure_progress.sh FAILED", last, p.stderr)
        self.assertIn(f"exit {EXIT_BASELINE_BUILD}", last)

    def test_failing_split_exits_10(self):
        p = self.fx.run("HEAD~1", MP_FIXTURE_SPLIT="fail")
        self.assertEqual(p.returncode, EXIT_BASELINE_BUILD, p.stdout + p.stderr)
        self.assertIn("dtk xex split", p.stderr)
        self.assertNoTable(p, "dtk split failed")

    def test_failing_configure_exits_10_and_keeps_its_output(self):
        p = self.fx.run("HEAD~1", MP_FIXTURE_CONFIGURE="fail")
        self.assertEqual(p.returncode, EXIT_BASELINE_BUILD, p.stdout + p.stderr)
        self.assertIn("configure.py", p.stderr)
        self.assertIn("FIXTURE: configure.py failed", p.stderr,
                      "configure.py's output went to /dev/null again")
        self.assertNoTable(p, "configure.py failed")

    def test_allow_stale_does_not_downgrade_a_baseline_build_failure(self):
        p = self.fx.run("--allow-stale", "HEAD~1", MP_FIXTURE_BASELINE_BUILD="fail")
        self.assertEqual(p.returncode, EXIT_BASELINE_BUILD, p.stdout + p.stderr)
        self.assertNoTable(p, "--allow-stale + failing baseline build")


class TestCurrentBuildFailure(_FixtureCase):
    def test_failing_initial_build_of_the_current_tree_exits_11(self):
        (self.fx.root / "build" / "373307D9" / "report.json").unlink()
        p = self.fx.run("--current-dir", str(self.fx.root), "HEAD~1",
                        MP_FIXTURE_CURRENT_BUILD="fail")
        self.assertEqual(p.returncode, EXIT_CURRENT_BUILD, p.stdout + p.stderr)
        self.assertIn("CURRENT TREE BUILD FAILED", p.stderr)
        self.assertNoTable(p, "initial current-tree build failed")

    def test_failing_stale_rebuild_exits_11(self):
        p = self.fx.run("HEAD~1",
                        MP_FIXTURE_FRESHNESS="1", MP_FIXTURE_CURRENT_BUILD="fail")
        self.assertEqual(p.returncode, EXIT_CURRENT_BUILD, p.stdout + p.stderr)
        self.assertIn("CURRENT TREE BUILD FAILED", p.stderr)
        self.assertNoTable(p, "stale-report rebuild failed")

    def test_allow_stale_does_not_downgrade_a_failing_rebuild(self):
        """THE regression: this run exited 0 with a full table before 2026-09-11."""
        p = self.fx.run("--allow-stale", "HEAD~1",
                        MP_FIXTURE_FRESHNESS="1", MP_FIXTURE_CURRENT_BUILD="fail")
        self.assertEqual(
            p.returncode, EXIT_CURRENT_BUILD,
            "--allow-stale downgraded a BUILD FAILURE, not a staleness "
            "judgement\n" + p.stdout + p.stderr)
        self.assertNoTable(p, "--allow-stale + failing rebuild")

    def test_a_stale_report_whose_rebuild_SUCCEEDS_still_compares(self):
        """Anti-overfire: the fix must not turn every rebuild into a refusal."""
        p = self.fx.run("HEAD~1",
                        MP_FIXTURE_FRESHNESS="1", MP_FIXTURE_FRESHNESS_ONCE="1")
        self.assertEqual(p.returncode, EXIT_OK, p.stdout + p.stderr)
        self.assertIn("Rebuilding...", p.stdout, "the rebuild path was not taken")
        self.assertIn(TABLE_MARKER, p.stdout)

    def test_allow_stale_still_downgrades_actual_staleness(self):
        """--allow-stale semantics are intact: a STALE verdict is still a warning."""
        p = self.fx.run("--allow-stale", "HEAD~1", MP_FIXTURE_FRESHNESS="2")
        self.assertEqual(p.returncode, EXIT_OK, p.stdout + p.stderr)
        self.assertIn("WARNING (--allow-stale)", p.stderr, p.stderr)
        self.assertIn(TABLE_MARKER, p.stdout)

    def test_without_allow_stale_an_unverifiable_report_refuses(self):
        p = self.fx.run("HEAD~1", MP_FIXTURE_FRESHNESS="2")
        self.assertEqual(p.returncode, EXIT_REFUSAL, p.stdout + p.stderr)
        self.assertNoTable(p, "freshness gate could not verify")


class TestComparisonFailure(_FixtureCase):
    def test_a_dying_comparison_exits_12_and_its_partial_table_is_withheld(self):
        p = self.fx.run("HEAD~1", MP_FIXTURE_COMPARE="fail")
        self.assertEqual(p.returncode, EXIT_COMPARE, p.stdout + p.stderr)
        self.assertNoTable(p, "compare_progress.py died")
        self.assertIn("COMPARISON FAILED", p.stderr)
        self.assertIn("SUPPRESSED", p.stderr,
                      "the partial output must be labelled as incomplete")


class TestHeaderContract(unittest.TestCase):
    """The documented table and the implemented codes must not drift apart."""

    def setUp(self):
        self.text = MEASURE.read_text()

    def test_every_code_is_both_documented_and_assigned(self):
        for name, code in (("EXIT_BASELINE_BUILD", EXIT_BASELINE_BUILD),
                           ("EXIT_CURRENT_BUILD", EXIT_CURRENT_BUILD),
                           ("EXIT_COMPARE", EXIT_COMPARE)):
            with self.subTest(name=name):
                self.assertIn(f"{name}={code}", self.text,
                              f"{name} is not assigned {code} in the script")
                self.assertRegex(self.text, rf"#\s+{code}\s+[A-Z]",
                                 f"exit code {code} is not in the header table")

    def test_help_prints_the_whole_header(self):
        """`sed -n '2,31p'` silently truncated this header when it grew."""
        p = subprocess.run([str(MEASURE), "--help"], capture_output=True,
                           text=True, timeout=60)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("EXIT CODES", p.stdout, p.stdout)
        self.assertIn("BASELINE BUILD FAILED", p.stdout)

    def test_pipefail_is_on(self):
        self.assertIn("set -euo pipefail", self.text)


# --------------------------------------------------------------------------- #
# Layer 2: prove the cases above can fail, by putting the old code back.
# --------------------------------------------------------------------------- #

#: (label, anchor, replacement, [test method names that MUST fail])
MUTATIONS = [
    ("S1  baseline ninja failure back to a bare `exit 1` (pre-2026-09-11)",
     '        build_fail "${EXIT_BASELINE_BUILD}" "ninja ${REPORT_REL} (baseline build)" \\\n'
     '                   "${WORKTREE}" "${BUILD_LOG}"',
     '        tail -100 "${BUILD_LOG}" || true\n        exit 1',
     ["TestBaselineBuildFailure.test_failing_baseline_ninja_exits_10_with_a_banner",
      "TestBaselineBuildFailure.test_the_banner_names_the_worktree_and_the_step",
      "TestBaselineBuildFailure.test_the_verdict_is_the_last_line",
      "TestBaselineBuildFailure.test_allow_stale_does_not_downgrade_a_baseline_build_failure"]),

    ("S2  THE defect: a failing rebuild goes back through stale_fail()",
     '        build_fail "${code}" "ninja ${REPORT_REL} (stale-report rebuild of the ${label} side)" \\\n'
     '                   "${dir}" "${log}"',
     '        stale_fail "${label} (${dir}): rebuild of ${REPORT_REL} failed."\n'
     '        return 0',
     ["TestCurrentBuildFailure.test_failing_stale_rebuild_exits_11",
      "TestCurrentBuildFailure.test_allow_stale_does_not_downgrade_a_failing_rebuild"]),

    ("S3  the current tree's initial build back to `ninja ... | tail -1`",
     '        CUR_BUILD_LOG="$(mktemp -t measure_progress_current.XXXXXX.log)"',
     '        ninja -C "${CURRENT_DIR}" "${REPORT_REL}" -j"$(nproc)" 2>&1 | tail -1\n'
     '        CUR_BUILD_LOG="$(mktemp -t measure_progress_current.XXXXXX.log)"',
     ["TestCurrentBuildFailure.test_failing_initial_build_of_the_current_tree_exits_11"]),

    ("S4  the comparison streams straight to the terminal again",
     '    "${CURRENT_REPORT}" >"${COMPARE_OUT}" 2>&1 || COMPARE_RC=$?',
     '    "${CURRENT_REPORT}" || COMPARE_RC=$?',
     ["TestComparisonFailure.test_a_dying_comparison_exits_12_and_its_partial_table_is_withheld"]),

    ("S5  the failure verdict is no longer the last line",
     '        echo "measure_progress.sh FAILED — exit ${rc}',
     '        : "measure_progress.sh FAILED — exit ${rc}',
     ["TestBaselineBuildFailure.test_the_verdict_is_the_last_line"]),

    ("S6  --help goes back to a hardcoded line range",
     "            usage\n            exit 0",
     "            sed -n '2,31p' \"$0\"\n            exit 0",
     ["TestHeaderContract.test_help_prints_the_whole_header"]),
]


class TestSabotage(unittest.TestCase):
    """Each mutation must redden its named cases.  Nothing is written to disk
    outside a temp fixture: the mutated script is only ever the fixture's copy,
    so this is safe to run on a dirty tree and alongside other lanes."""

    def _run_case(self, dotted: str, script_text: str) -> bool:
        """Run one test method against a fixture built from `script_text`.

        Returns True if it PASSED -- i.e. the mutation went undetected.
        """
        p = subprocess.run(
            [sys.executable, "-m", "unittest", "-v",
             f"{Path(__file__).stem}.{dotted}"],
            cwd=str(REPO_ROOT / "tests"), capture_output=True, text=True,
            timeout=900, env={**os.environ, "MP_FIXTURE_SCRIPT_OVERRIDE": script_text})
        return p.returncode == 0

    @unittest.skipIf(os.environ.get("MP_FIXTURE_SCRIPT_OVERRIDE"),
                     "inner run: mutations are driven by the outer process")
    def test_every_mutation_is_detected(self):
        original = (REPO_ROOT / "scripts" / "measure_progress.sh").read_text()
        undetected, unanchored = [], []
        for label, old, new, cases in MUTATIONS:
            if original.count(old) != 1:
                unanchored.append(f"{label}  (anchor occurs {original.count(old)}x)")
                continue
            mutated = original.replace(old, new, 1)
            self.assertNotEqual(mutated, original, label)
            caught = [c for c in cases if not self._run_case(c, mutated)]
            if not caught:
                undetected.append(f"{label}  — none of {cases} reddened")
        self.assertEqual(unanchored, [], "mutation anchors no longer match the script")
        self.assertEqual(undetected, [],
                         "these deliberate defects were NOT caught; the matching "
                         "assertions are vacuous")


if __name__ == "__main__":
    unittest.main(verbosity=2)
