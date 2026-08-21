#!/usr/bin/env python3
"""Sabotage tests for the split-currency guard.

The rule this project keeps relearning: a guard nobody has watched FAIL is not a
guard.  Every test below therefore carries its own negative control -- it
asserts GREEN on a healthy fixture first, then breaks exactly one thing and
asserts RED, then restores and asserts GREEN again.  A checker that always
raises, or one that raises for the wrong reason, fails these.

Note on the trap a sibling lane hit: `assertRaises` alone is not enough when the
buggy and fixed code both raise.  Each RED assertion below pins the *reason*
(the substring naming which of the three conditions fired), so a guard that goes
red on the wrong condition -- or a message that stops naming the condition --
fails rather than passes.

Run:  python3 -m pytest tests/test_split_currency.py -q
      python3 tests/test_split_currency.py            (unittest fallback)
"""

from __future__ import annotations

import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CHECKER = REPO_ROOT / "scripts" / "verify_split_current.py"


def _load_checker():
    spec = importlib.util.spec_from_file_location("_vsc_under_test", CHECKER)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


vsc = _load_checker()


class SplitCurrencyFixture(unittest.TestCase):
    """A minimal project tree: the three split config inputs plus a stamp."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="split-currency-"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        cfg = self.tmp / "config" / vsc.VERSION
        cfg.mkdir(parents=True)
        (cfg / "symbols.txt").write_text(
            "?Load@UIList@@UAAXAAVDataArray@@@Z = .text:0x82341C48; "
            "// type:function size:0x78\n"
        )
        (cfg / "splits.txt").write_text("src/system/ui/UIList.cpp:\n\t.text start:0x1\n")
        (cfg / "config.yml").write_text("version: test\n")
        orig = self.tmp / "orig" / vsc.VERSION
        orig.mkdir(parents=True)
        (orig / "default.xex").write_bytes(b"XEX2" + b"\0" * 64)
        (self.tmp / "build" / vsc.VERSION).mkdir(parents=True)

    def split(self):
        """Stand in for a successful `dtk xex split`."""
        vsc.write_stamp(self.tmp, vsc.STATE_RUNNING)
        vsc.write_stamp(self.tmp, vsc.STATE_COMPLETE)

    def assert_green(self, msg="expected the guard to pass"):
        try:
            note = vsc.check(self.tmp)
        except vsc.StaleSplitError as exc:  # pragma: no cover - failure path
            self.fail(f"{msg}, but it raised:\n{exc}")
        self.assertIn("split current", note)

    def assert_red(self, reason_substring):
        with self.assertRaises(vsc.StaleSplitError) as ctx:
            vsc.check(self.tmp)
        self.assertIn(
            reason_substring, str(ctx.exception),
            f"the guard went red, but not for the reason under test "
            f"({reason_substring!r}). Message was:\n{ctx.exception}",
        )
        return str(ctx.exception)


class TestSabotage(SplitCurrencyFixture):

    def test_no_stamp_is_red_and_a_split_makes_it_green(self):
        # NEGATIVE CONTROL first: a tree that never split must NOT read as fine.
        self.assert_red("is missing or unreadable")
        self.split()
        self.assert_green("a freshly split tree")

    def test_symbols_txt_edit_is_red_until_the_split_reruns(self):
        self.split()
        self.assert_green("control: healthy before sabotage")

        # SABOTAGE: exactly the change that produced the 341-function gap --
        # rename one symbol, do not re-split.
        cfg = self.tmp / "config" / vsc.VERSION / "symbols.txt"
        cfg.write_text(cfg.read_text().replace("?Load@UIList", "?Unload@UIList"))
        msg = self.assert_red("split from a DIFFERENT config")
        self.assertIn("symbols.txt", msg, "the message must name the file that moved")

        # RESTORE: re-splitting is what fixes it, and it must go green again.
        self.split()
        self.assert_green("after re-splitting")

    def test_split_in_flight_is_red_even_when_the_config_matches(self):
        self.split()
        self.assert_green("control: healthy before sabotage")

        # SABOTAGE: a split that is running right now, with the SAME config it
        # last completed with. The input hashes cannot see this -- only the
        # state can -- and this is the exact shape of the reproduced incident:
        # a report that overlapped a split reading the objects it was rewriting.
        vsc.write_stamp(self.tmp, vsc.STATE_RUNNING)
        self.assert_red("not `complete`")

        vsc.write_stamp(self.tmp, vsc.STATE_COMPLETE)
        self.assert_green("after the split completes")

    def test_a_crashed_split_stays_red(self):
        # --begin ran, dtk died, --complete never ran. The tree is half-renamed
        # and must not be measurable.
        vsc.write_stamp(self.tmp, vsc.STATE_RUNNING)
        self.assert_red("not `complete`")

    def test_unreadable_stamp_is_red_not_silently_ok(self):
        self.split()
        self.assert_green("control")
        (self.tmp / vsc.STAMP_REL).write_text("{ this is not json")
        self.assert_red("is missing or unreadable")

    def test_splits_txt_and_config_yml_are_also_gated(self):
        # Each input gets its own red, so a future refactor that drops one from
        # SPLIT_CONFIG_INPUTS fails here instead of shipping a partial gate.
        for rel in ("splits.txt", "config.yml"):
            with self.subTest(input=rel):
                self.split()
                self.assert_green(f"control before touching {rel}")
                p = self.tmp / "config" / vsc.VERSION / rel
                p.write_text(p.read_text() + "\n# drift\n")
                msg = self.assert_red("split from a DIFFERENT config")
                self.assertIn(rel, msg)
                self.split()
                self.assert_green(f"after re-splitting for {rel}")


class TestCliContract(SplitCurrencyFixture):
    """The ninja edge reads the EXIT CODE, so the exit code is under test too."""

    def _run(self, *args):
        return subprocess.run(
            [sys.executable, str(CHECKER), "--project-dir", str(self.tmp), *args],
            capture_output=True, text=True,
        )

    def test_check_exit_codes(self):
        # NEGATIVE CONTROL: must be able to fail. A checker that exits 0 on a
        # tree with no stamp at all is the failure mode this whole file exists
        # for -- see rustfmt --check reading stdin, which cannot fail.
        self.assertEqual(self._run("--check", "--quiet").returncode, 1)

        self.assertEqual(self._run("--begin", "--quiet").returncode, 0)
        self.assertEqual(self._run("--check", "--quiet").returncode, 1,
                         "a running split must not pass --check")

        self.assertEqual(self._run("--complete", "--quiet").returncode, 0)
        self.assertEqual(self._run("--check", "--quiet").returncode, 0)

        cfg = self.tmp / "config" / vsc.VERSION / "symbols.txt"
        cfg.write_text(cfg.read_text().replace("0x82341C48", "0x82341C50"))
        red = self._run("--check", "--quiet")
        self.assertEqual(red.returncode, 1)
        self.assertIn("DIFFERENT config", red.stderr)


class TestNinjaWiring(unittest.TestCase):
    """The guard has to be reachable from the build, not merely present."""

    def test_report_edges_depend_on_the_split_check(self):
        ninja = REPO_ROOT / "build.ninja"
        if not ninja.exists():
            self.skipTest("build.ninja not generated in this tree")
        text = ninja.read_text()
        stamp = f"build/{vsc.VERSION}/split_current_checked.stamp"
        self.assertTrue(stamp in text,
                        "configure.py did not emit the split_current_check edge")
        # Every edge that produces a progress report must list it.
        for out in (f"build/{vsc.VERSION}/report.json",
                    f"build/{vsc.VERSION}/report_raw.json",
                    f"build/{vsc.VERSION}/baseline.json"):
            idx = text.find(f"build {out}:")
            self.assertNotEqual(idx, -1, f"no edge produces {out}")
            edge = text[idx:text.find("\nbuild ", idx + 1)]
            self.assertTrue(stamp in edge, f"{out} does not depend on {stamp}")

    def test_split_rule_brackets_dtk_with_begin_and_complete(self):
        ninja = REPO_ROOT / "build.ninja"
        if not ninja.exists():
            self.skipTest("build.ninja not generated in this tree")
        text = ninja.read_text()
        idx = text.find("\nrule split\n")
        self.assertNotEqual(idx, -1)
        rule = text[idx:text.find("\nbuild ", idx + 1)]
        self.assertTrue("--begin" in rule, "split rule has no --begin")
        self.assertTrue("--complete" in rule, "split rule has no --complete")
        self.assertLess(rule.find("--begin"), rule.find("xex split"),
                        "--begin must run BEFORE dtk touches the objects")
        self.assertLess(rule.find("xex split"), rule.find("--complete"),
                        "--complete must run AFTER a successful split")


class TestPatchGuardIntegration(unittest.TestCase):
    def test_patch_guard_exports_and_delegates(self):
        sys.path.insert(0, str(REPO_ROOT / "scripts"))
        try:
            from orchestrator import patch_guard
        finally:
            sys.path.remove(str(REPO_ROOT / "scripts"))
        self.assertIn("ensure_split_current", patch_guard.__all__)
        self.assertIn("StaleSplitError", patch_guard.__all__)
        # An absent checker must degrade to a NOTE, never to a silent pass that
        # looks identical to a real verification.
        with tempfile.TemporaryDirectory() as td:
            note = patch_guard.ensure_split_current(td)
            self.assertIn("NOT checked", note)


class TestConvergence(SplitCurrencyFixture):
    """The guard must not make every build re-run the report.

    The check edge is `always`-dirty on purpose, so its OUTPUT is what keeps
    report.json clean. `touch $out` there costs ~14 s of REPORT + REPORT RAW on
    every ninja and rewrites report.json on a tree where nothing moved -- which
    then re-fires SYNC DB against decomp.db. Write-if-changed + restat is the
    fix, and this is the test that would have caught shipping it without them.
    """

    def _check(self, out):
        return subprocess.run(
            [sys.executable, str(CHECKER), "--project-dir", str(self.tmp),
             "--check", "--quiet", "--stamp-out", str(out)],
            capture_output=True, text=True,
        )

    def test_stamp_out_does_not_move_when_nothing_moved(self):
        self.split()
        out = self.tmp / "checked.stamp"
        self.assertEqual(self._check(out).returncode, 0)
        first = out.stat().st_mtime_ns
        body = out.read_text()
        time.sleep(0.05)
        self.assertEqual(self._check(out).returncode, 0)
        self.assertEqual(out.stat().st_mtime_ns, first,
                         "the check rewrote its output on an unchanged tree; "
                         "restat cannot save report.json from that")
        self.assertEqual(out.read_text(), body)

        # NEGATIVE CONTROL: it must still move when a split actually happens,
        # or the edge would be inert and report.json would never re-run.
        cfg = self.tmp / "config" / vsc.VERSION / "symbols.txt"
        cfg.write_text(cfg.read_text() + "\n?extra@@YAXXZ = .text:0x82341D00;\n")
        self.assertEqual(self._check(out).returncode, 1, "drift must still fail")
        self.split()
        self.assertEqual(self._check(out).returncode, 0)
        self.assertNotEqual(out.read_text(), body,
                            "the digest did not move across a real re-split")


if __name__ == "__main__":
    unittest.main(verbosity=2)
