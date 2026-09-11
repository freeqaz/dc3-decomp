#!/usr/bin/env python3
"""Sabotage tests for the report.json freshness gate.

The defect these lock down
--------------------------
`scripts/measure_progress.sh` gated every measurement on

    ninja -n build/373307D9/report.json | grep "no work to do"

which became unsatisfiable on 2026-08-21 when `report.json` gained an
`always`-dirty implicit (`6e1763aac`).  `restat` keeps a REAL build a no-op on
a current tree, but a dry run cannot apply restat, so the gate read STALE in
every tree -- main checkout included -- and refused with a message blaming a
concurrent build that was not happening.  Lanes routed around it.  An
always-RED guard is as useless as an always-GREEN one, and harder to notice
because it looks conscientious.

Every test below follows this repo's idiom: assert GREEN on a healthy fixture,
break exactly one thing, assert RED *and pin the reason*, restore, assert GREEN
again.  A checker that always refuses fails these as loudly as one that never
does.

The fixtures in `TestClassifier` / `TestCheckVerdicts` are ninja output captured
verbatim from a freshly and fully built worktree (`wt/mp-gate` @ `cbe492813`,
2026-09-11), so `test_freshly_built_tree_reads_current` IS the always-red
regression test and needs no build.  `TestAgainstRealTree` runs the same
question against an actual tree when one is named.

Run:  python3 -m pytest tests/test_report_freshness.py -q
      DC3_FRESHNESS_TREE=/path/to/built/worktree python3 -m pytest tests/test_report_freshness.py -q
"""

from __future__ import annotations

import importlib.util
import os
import subprocess
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CHECKER = REPO_ROOT / "scripts" / "report_freshness.py"
MEASURE = REPO_ROOT / "scripts" / "measure_progress.sh"


def _load_checker():
    spec = importlib.util.spec_from_file_location("_rf_under_test", CHECKER)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


rf = _load_checker()


# --------------------------------------------------------------------------- #
# Captured ninja output.  Both blocks are verbatim from wt/mp-gate @ cbe492813.
# --------------------------------------------------------------------------- #

#: `ninja -n build/373307D9/report.json` on a tree where the immediately
#: preceding full `ninja` reached steady state.  Twelve pending edges, every
#: one of them a guard rooted in the `always` phony.
FRESH_TREE_STDOUT = """\
[1/12] CHECK SPLIT CURRENT
[2/12] GEN data-stub .obj files for lbl_* resolution
[3/12] PATCH anonymous namespace hashes
[4/12] PATCH ??__E dynamic initializers STATIC->EXTERNAL
[5/12] PATCH  guard variables to match ??_B naming
[6/12] PATCH bool parameter back-reference mangling
[7/12] PATCH ??__F atexit scope counters (fuzzy match)
[8/12] NORMALIZE clock-derived .obj build metadata
[9/12] VERIFY every .obj carries the post-compile patches
[10/12] VERIFY no symbol is called but defined nowhere
[11/12] CHECK COMPLETE UNITS
[12/12] REPORT
"""

FRESH_TREE_EXPLAIN = """\
ninja explain: always is dirty
ninja explain: build/373307D9/split_current_checked.stamp is dirty
ninja explain: build/373307D9/data_stubs.stamp is dirty
ninja explain: build/373307D9/anon_ns_patched.stamp is dirty
ninja explain: build/373307D9/dynamic_init_patched.stamp is dirty
ninja explain: build/373307D9/guard_patched.stamp is dirty
ninja explain: build/373307D9/bool_mangle_patched.stamp is dirty
ninja explain: build/373307D9/atexit_scope_patched.stamp is dirty
ninja explain: build/373307D9/build_metadata_normalized.stamp is dirty
ninja explain: build/373307D9/objs_patched_verified.stamp is dirty
ninja explain: always is dirty
ninja explain: build/373307D9/split_current_checked.stamp is dirty
ninja explain: build/373307D9/complete_units_checked.stamp is dirty
ninja explain: post-compile is dirty
"""

#: The same tree after `touch src/system/math/Geo.cpp`.  One extra line, and it
#: is the only one that states a reason.
TOUCHED_SOURCE_EXPLAIN = """\
ninja explain: always is dirty
ninja explain: output build/373307D9/src/system/math/Geo.obj older than most \
recent input src/system/math/Geo.cpp (1789090374606714553 vs 1789090852320777711)
ninja explain: build/373307D9/split_current_checked.stamp is dirty
ninja explain: all_source is dirty
ninja explain: build/373307D9/anon_ns_patched.stamp is dirty
ninja explain: post-compile is dirty
"""

#: After appending to config/373307D9/symbols.txt (measured the same day).
TOUCHED_SYMBOLS_EXPLAIN = """\
ninja explain: always is dirty
ninja explain: output build/373307D9/config.json older than most recent input \
config/373307D9/symbols.txt (1789090352854638940 vs 1789091158221643283)
ninja explain: build/373307D9/split_current_checked.stamp is dirty
ninja explain: post-compile is dirty
"""

#: Every explain format the installed ninja can emit, enumerated from the
#: binary (`strings -a $(command -v ninja) | grep -E "is dirty|doesn't exist"`).
#: Six root causes, one propagation.  If a future classifier files any of the
#: six as benign, a real staleness becomes invisible.
ROOT_CAUSE_FORMATS = [
    "command line changed for build/373307D9/src/App.obj",
    "deps for 'build/373307D9/src/App.obj' are missing",
    "output build/373307D9/src/App.obj doesn't exist",
    "output build/373307D9/src/App.obj older than most recent input src/App.cpp (1 vs 2)",
    "recorded mtime of build/373307D9/report.json older than most recent input x (1 vs 2)",
    "output post-compile of phony edge with no inputs doesn't exist",
]


class TestClassifier(unittest.TestCase):
    """The propagation/root split, with no build tree in sight."""

    def test_fresh_tree_has_no_root_causes(self):
        roots, prop, always = rf.classify_explain(FRESH_TREE_EXPLAIN)
        self.assertEqual(roots, [], "a freshly built tree produced root causes")
        self.assertTrue(always, "the benign `always` root was not recognised")
        self.assertEqual(len(prop), 12)

    def test_touched_source_produces_exactly_one_root(self):
        roots, _, always = rf.classify_explain(TOUCHED_SOURCE_EXPLAIN)
        self.assertEqual(len(roots), 1, f"expected one root, got {roots}")
        self.assertIn("Geo.obj", roots[0])
        self.assertTrue(always)

    def test_touched_symbols_produces_exactly_one_root(self):
        roots, _, _ = rf.classify_explain(TOUCHED_SYMBOLS_EXPLAIN)
        self.assertEqual(len(roots), 1, f"expected one root, got {roots}")
        self.assertIn("symbols.txt", roots[0])

    def test_every_ninja_root_format_is_a_root_cause(self):
        """SABOTAGE TARGET: a classifier that widens the propagation rule."""
        for line in ROOT_CAUSE_FORMATS:
            with self.subTest(line=line):
                roots, _, _ = rf.classify_explain(f"ninja explain: {line}\n")
                self.assertEqual(
                    roots, [line],
                    f"ninja's `{line.split()[0]} ...` format was not treated as a "
                    f"reason to rebuild — real staleness would read as clean",
                )

    def test_the_always_phony_is_the_only_benign_root(self):
        """`post-compile`'s phony line is a root; only `always`'s is benign."""
        roots, _, always = rf.classify_explain(
            "ninja explain: output always of phony edge with no inputs doesn't exist\n"
        )
        self.assertEqual(roots, [])
        self.assertTrue(always)

    def test_propagation_alone_is_not_evidence_of_health(self):
        roots, prop, always = rf.classify_explain(
            "ninja explain: post-compile is dirty\nninja explain: all_source is dirty\n"
        )
        self.assertEqual(roots, [])
        self.assertEqual(len(prop), 2)
        self.assertFalse(always, "a rootless graph must not look explained")

    def test_sabotage_widened_propagation_rule_is_caught(self):
        """Break the one rule the classifier turns on; require the RED."""
        import re
        healthy, _, _ = rf.classify_explain(TOUCHED_SOURCE_EXPLAIN)
        self.assertEqual(len(healthy), 1)          # GREEN control

        original = rf._PROPAGATION_RE
        try:
            rf._PROPAGATION_RE = re.compile(r"^(?P<node>.*)$")   # swallow everything
            broken, _, _ = rf.classify_explain(TOUCHED_SOURCE_EXPLAIN)
        finally:
            rf._PROPAGATION_RE = original

        self.assertEqual(
            broken, [],
            "sabotage did not change the classification — "
            "test_touched_source_produces_exactly_one_root is vacuous",
        )
        restored, _, _ = rf.classify_explain(TOUCHED_SOURCE_EXPLAIN)
        self.assertEqual(len(restored), 1)         # GREEN again


class _FakeNinja:
    """Stand in for `ninja -n -d explain` without a build tree."""

    def __init__(self, rc=0, stdout="", explain=""):
        self.rc, self.stdout, self.explain = rc, stdout, explain

    def __call__(self, project_dir, target, timeout=300):
        return self.rc, self.stdout, self.stdout + self.explain


class TestCheckVerdicts(unittest.TestCase):
    """`check()` end to end, with ninja and the two sub-guards faked."""

    def setUp(self):
        self.tmp = Path(os.environ.get("TMPDIR", "/tmp")) / f"rf-fixture-{os.getpid()}"
        self.tmp.mkdir(parents=True, exist_ok=True)
        (self.tmp / "build.ninja").write_text("# fixture\n")
        self.addCleanup(lambda: __import__("shutil").rmtree(self.tmp, ignore_errors=True))

        self._saved = (rf.run_ninja_explain, rf.check_split_current, rf.check_patched_tree)
        rf.check_split_current = lambda d: (True, "split current (fixture)")
        rf.check_patched_tree = lambda d: (True, "fixed point (fixture)")
        self.addCleanup(self._restore)

    def _restore(self):
        rf.run_ninja_explain, rf.check_split_current, rf.check_patched_tree = self._saved

    def _verdict(self, rc=0, stdout="", explain=""):
        rf.run_ninja_explain = _FakeNinja(rc, stdout, explain)
        return rf.check(self.tmp)

    # -- the regression this whole file exists for --------------------------- #

    def test_freshly_built_tree_reads_current(self):
        """THE always-red regression: 12 pending guard edges is not staleness.

        An implementation that asks `ninja -n | grep "no work to do"` returns
        STALE here, which is exactly what shipped between 2026-08-21 and
        2026-09-11.
        """
        v = self._verdict(stdout=FRESH_TREE_STDOUT, explain=FRESH_TREE_EXPLAIN)
        self.assertEqual(v.status, rf.CURRENT, v.detail)
        self.assertEqual(v.exit_code, 0)
        self.assertNotIn("no work to do", FRESH_TREE_STDOUT,
                         "fixture no longer exercises the regression")

    def test_touched_source_reads_stale_and_names_the_file(self):
        v = self._verdict(stdout=FRESH_TREE_STDOUT, explain=TOUCHED_SOURCE_EXPLAIN)
        self.assertEqual(v.status, rf.STALE)
        self.assertEqual(v.exit_code, 1)
        self.assertTrue(any("Geo.cpp" in r for r in v.reasons),
                        f"the refusal did not name the stale input: {v.reasons}")

    def test_touched_symbols_reads_stale_and_names_the_file(self):
        v = self._verdict(stdout=FRESH_TREE_STDOUT, explain=TOUCHED_SYMBOLS_EXPLAIN)
        self.assertEqual(v.status, rf.STALE)
        self.assertTrue(any("symbols.txt" in r for r in v.reasons), v.reasons)

    def test_nothing_pending_is_current(self):
        v = self._verdict(stdout="ninja: no work to do.\n")
        self.assertEqual(v.status, rf.CURRENT)

    # -- anti-vacuity: the ways this checker must refuse to answer ----------- #

    def test_pending_work_with_no_explanation_cannot_be_verified(self):
        """Work pending and nothing roots it: refuse, never pass.

        A dirty graph cannot be pure propagation, so this state means the
        parser has lost ninja's output (format change, -d explain dropped).
        Reading it as CURRENT is the always-green failure.
        """
        v = self._verdict(stdout=FRESH_TREE_STDOUT, explain="")
        self.assertEqual(v.status, rf.CANNOT_VERIFY)
        self.assertEqual(v.exit_code, 2)
        self.assertIn("no root cause", v.detail)

    def test_propagation_without_a_root_cannot_be_verified(self):
        v = self._verdict(stdout=FRESH_TREE_STDOUT,
                          explain="ninja explain: post-compile is dirty\n")
        self.assertEqual(v.status, rf.CANNOT_VERIFY)

    def test_broken_graph_cannot_be_verified(self):
        v = self._verdict(rc=1, stdout="ninja: error: loading 'build.ninja'\n")
        self.assertEqual(v.status, rf.CANNOT_VERIFY)
        self.assertIn("build graph is broken", v.detail)

    def test_missing_build_ninja_cannot_be_verified(self):
        (self.tmp / "build.ninja").unlink()
        rf.run_ninja_explain = _FakeNinja(0, FRESH_TREE_STDOUT, FRESH_TREE_EXPLAIN)
        v = rf.check(self.tmp)
        self.assertEqual(v.status, rf.CANNOT_VERIFY)
        self.assertIn("build.ninja", v.detail)

    def test_cannot_verify_is_never_exit_zero(self):
        self.assertNotEqual(rf._STATUS_EXIT[rf.CANNOT_VERIFY], 0)
        self.assertNotEqual(rf._STATUS_EXIT[rf.STALE], 0)

    # -- the two sub-guards must each be able to veto a settled graph -------- #

    def test_split_guard_vetoes_a_settled_graph(self):
        """The mtime-invisible case: symbols.txt content moved, mtime did not.

        Measured on wt/mp-gate 2026-09-11 -- ninja read CURRENT, the split
        stamp caught it.
        """
        rf.check_split_current = lambda d: (False, "REFUSING TO VOUCH FOR ...: symbols.txt")
        v = self._verdict(stdout=FRESH_TREE_STDOUT, explain=FRESH_TREE_EXPLAIN)
        self.assertEqual(v.status, rf.STALE)
        self.assertIn("split", v.detail)

    def test_patch_guard_vetoes_a_settled_graph(self):
        """The always-rooted patcher edges are benign only while they no-op.

        Measured on the main checkout 2026-09-11: ninja's only root was
        `always`, and 4 of 6 patchers had pending work over those objects.
        """
        rf.check_patched_tree = lambda d: (False, "post-compile patchers have pending work")
        v = self._verdict(stdout=FRESH_TREE_STDOUT, explain=FRESH_TREE_EXPLAIN)
        self.assertEqual(v.status, rf.STALE)
        self.assertIn("fixed point", v.detail)

    def test_sabotage_disabling_a_subguard_is_visible(self):
        """Turning a sub-guard off must change the verdict, or it guards nothing."""
        rf.check_patched_tree = lambda d: (False, "pending work")
        red = self._verdict(stdout=FRESH_TREE_STDOUT, explain=FRESH_TREE_EXPLAIN)
        self.assertEqual(red.status, rf.STALE)          # RED with the guard on
        rf.run_ninja_explain = _FakeNinja(0, FRESH_TREE_STDOUT, FRESH_TREE_EXPLAIN)
        green = rf.check(self.tmp, check_patched=False)
        self.assertEqual(green.status, rf.CURRENT,
                         "--no-patch-check did not actually skip the guard")


class TestSelftestAndCli(unittest.TestCase):
    def test_selftest_passes(self):
        p = subprocess.run([sys.executable, str(CHECKER), "--selftest"],
                           capture_output=True, text=True, timeout=120)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)

    def test_measure_progress_routes_through_this_checker(self):
        """Regression lock: the shell gate must call the checker, not grep ninja.

        Asserted positively (the invocation is present) rather than as the
        absence of a string, because this file's own prose quotes the old
        idiom and a whole-file substring check would fail on the comment that
        explains why it is gone.
        """
        text = MEASURE.read_text()
        self.assertIn(
            'python3 "${MAIN_REPO}/scripts/report_freshness.py" '
            '--project-dir "${dir}" --quiet',
            text,
            "report_is_current() no longer invokes the freshness checker",
        )
        self.assertIn('report_is_current "${dir}"', text,
                      "require_fresh_report() no longer calls report_is_current()")
        self.assertIn("--check-freshness", text)
        self.assertNotIn("ninja_is_clean", text,
                         "the old dry-run gate is still wired up")


@unittest.skipUnless(os.environ.get("DC3_FRESHNESS_TREE"),
                     "set DC3_FRESHNESS_TREE=<fully built tree> to run the "
                     "integration half")
class TestAgainstRealTree(unittest.TestCase):
    """The same three questions, against a real ninja graph.

    A skip is not a pass, so when the variable IS set every precondition is
    asserted rather than skipped: a tree that was never built fails here.
    """

    @classmethod
    def setUpClass(cls):
        cls.tree = Path(os.environ["DC3_FRESHNESS_TREE"]).resolve()
        assert (cls.tree / "build.ninja").is_file(), \
            f"{cls.tree} has no build.ninja — build it before running this"
        assert (cls.tree / rf.REPORT_REL).is_file(), \
            f"{cls.tree}/{rf.REPORT_REL} does not exist — run `ninja` there first"

    def _run(self, *extra):
        return subprocess.run(
            [sys.executable, str(CHECKER), "--project-dir", str(self.tree), *extra],
            capture_output=True, text=True, timeout=1800,
        )

    def test_freshly_built_tree_exits_zero(self):
        p = self._run()
        self.assertEqual(p.returncode, 0,
                         "a fully built tree must pass the gate:\n" + p.stdout + p.stderr)
        self.assertIn("CURRENT", p.stdout)

    def test_staled_source_exits_nonzero(self):
        src = self.tree / "src" / "system" / "math" / "Geo.cpp"
        self.assertTrue(src.is_file(), f"{src} is missing; pick another probe file")
        st = src.stat()
        try:
            os.utime(src, None)                      # forward its mtime only
            p = self._run("--no-patch-check")
            self.assertNotEqual(p.returncode, 0,
                                "the gate passed on a tree with a stale object")
            self.assertIn("STALE", p.stdout + p.stderr)
            self.assertIn("Geo", p.stdout + p.stderr)
        finally:
            os.utime(src, (st.st_atime, st.st_mtime))   # reversible: content untouched
        p = self._run()
        self.assertEqual(p.returncode, 0,
                         "restoring the mtime did not restore the verdict:\n"
                         + p.stdout + p.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
