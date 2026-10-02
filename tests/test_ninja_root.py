#!/usr/bin/env python3
"""Sabotage tests for the tree-identity guard (`scripts/verify_ninja_root.py`).

Same rule as `test_split_currency.py`: a guard nobody has watched FAIL is not a
guard. Every test asserts GREEN on a healthy fixture, breaks exactly one thing,
asserts RED *and pins the reason*, then restores and asserts GREEN again.

The failure under test is a `build.ninja` that names another tree. Because every
compile edge is `cd $in_dir && cl.exe /Fo$abs_out` with both absolute while the
ninja node names stay relative, such a manifest compiles the OTHER tree's
sources into the OTHER tree's object dir and leaves this one untouched.

Run:  python3 -m pytest tests/test_ninja_root.py -q
      python3 tests/test_ninja_root.py            (unittest fallback)
"""

from __future__ import annotations

import importlib.util
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CHECKER = REPO_ROOT / "scripts" / "verify_ninja_root.py"


def _load_checker():
    spec = importlib.util.spec_from_file_location("_vnr_under_test", CHECKER)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


vnr = _load_checker()

# A manifest shaped like the real one: relative node names, absolute command
# variables, and the 78-column `$`-continuation ninja_syntax emits.
_MANIFEST = """ninja_required_version = 1.3

configure_args =
python = "/usr/bin/python3"
{root_var}
rule msvc
  command = cd $in_dir && $
      cl.exe $cflags /Fo$abs_out $in_win
  description = MSVC $out

build build/373307D9/src/App.obj: msvc src/App.cpp
  in_dir = {root}/src
  abs_out = $
      {root}/build/373307D9/src/App.obj
"""


class NinjaRootFixture(unittest.TestCase):
    """Two throwaway trees: one that owns its manifest and one that inherits it."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="ninja-root-")).resolve()
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.origin = self.tmp / "origin"
        self.copy = self.tmp / "copy"
        for d in (self.origin, self.copy):
            (d / "build" / "373307D9").mkdir(parents=True)

    def write_manifest(self, tree: Path, *, generated_for: Path, with_var: bool):
        root_var = f"ninja_root = {generated_for}\n" if with_var else ""
        (tree / "build.ninja").write_text(
            _MANIFEST.format(root=generated_for, root_var=root_var), encoding="utf-8"
        )

    def assert_green(self, tree: Path, msg="expected the guard to pass"):
        try:
            note = vnr.check(tree)
        except vnr.ForeignNinjaRootError as exc:  # pragma: no cover - failure path
            self.fail(f"{msg}, but it raised:\n{exc}")
        self.assertIn("belongs to this tree", note)
        return note

    def assert_red(self, tree: Path, reason_substring: str):
        with self.assertRaises(vnr.ForeignNinjaRootError) as ctx:
            vnr.check(tree)
        self.assertIn(
            reason_substring, str(ctx.exception),
            f"the guard went red, but not for the reason under test "
            f"({reason_substring!r}). Message was:\n{ctx.exception}",
        )
        return str(ctx.exception)


class TestSabotage(NinjaRootFixture):

    def test_foreign_manifest_is_red_and_reconfiguring_makes_it_green(self):
        # CONTROL: the tree that generated its own manifest is fine.
        self.write_manifest(self.origin, generated_for=self.origin, with_var=True)
        self.assert_green(self.origin, "a tree that generated its own manifest")

        # SABOTAGE: the exact scenario -- copy the built tree, keep its manifest.
        shutil.copy(self.origin / "build.ninja", self.copy / "build.ninja")
        msg = self.assert_red(self.copy, "was generated for")
        self.assertIn(str(self.origin), msg, "the message must name the tree it would build")
        self.assertIn(str(self.copy), msg, "the message must name the tree you are in")

        # RESTORE: re-running configure.py is what fixes it.
        self.write_manifest(self.copy, generated_for=self.copy, with_var=True)
        self.assert_green(self.copy, "after reconfiguring in place")

    def test_recovery_works_on_a_manifest_with_no_ninja_root_variable(self):
        """The population that can be wrong TODAY predates the variable.

        A guard that only reads `ninja_root` would pass every legacy manifest by
        finding nothing, which is the failure mode this whole file exists for.
        """
        self.write_manifest(self.origin, generated_for=self.origin, with_var=False)
        note = self.assert_green(self.origin, "legacy manifest, own tree")
        self.assertIn("abs_out", note, "it must say it fell back to abs_out recovery")

        shutil.copy(self.origin / "build.ninja", self.copy / "build.ninja")
        msg = self.assert_red(self.copy, "was generated for")
        self.assertIn("abs_out", msg)

    def test_absent_manifest_is_red_not_silently_ok(self):
        self.assert_red(self.origin, "no build.ninja")

    def test_unvouchable_manifest_is_red(self):
        """No ninja_root and no usable abs_out must refuse, not assume innocence."""
        (self.origin / "build.ninja").write_text(
            "ninja_required_version = 1.3\n\nbuild all: phony\n", encoding="utf-8"
        )
        self.assert_red(self.origin, "cannot establish which tree")

    def test_wrapped_abs_out_is_parsed(self):
        """ninja_syntax breaks lines at 78 columns; an unwrapper that drops the
        continuation reads a truncated path and would silently go red on a
        healthy tree."""
        self.write_manifest(self.origin, generated_for=self.origin, with_var=False)
        raw = (self.origin / "build.ninja").read_text()
        self.assertIn("abs_out = $\n", raw, "fixture must actually exercise wrapping")
        self.assert_green(self.origin)


class TestCli(NinjaRootFixture):
    """The exit codes the ninja edge and the shell scripts depend on."""

    def _run(self, tree: Path, *extra):
        return subprocess.run(
            [sys.executable, str(CHECKER), "--check", "--root", str(tree), *extra],
            capture_output=True, text=True,
        )

    def test_exit_codes_and_stamp_stability(self):
        self.write_manifest(self.origin, generated_for=self.origin, with_var=True)
        stamp = self.origin / "build" / "373307D9" / "ninja_root_checked.stamp"

        proc = self._run(self.origin, "--quiet", "--stamp-out", str(stamp))
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stdout, "", "--quiet must print nothing on success")
        body = stamp.read_text()

        # The ninja edge is `always`-dirty with restat, so a second run on an
        # unmoved tree MUST leave the stamp byte-identical or every `ninja`
        # re-runs REPORT.
        self.assertEqual(self._run(self.origin, "--quiet", "--stamp-out", str(stamp)).returncode, 0)
        self.assertEqual(stamp.read_text(), body, "stamp moved on an unchanged tree")

        # NEGATIVE CONTROL: a foreign manifest must exit 1 and say so on stderr.
        shutil.copy(self.origin / "build.ninja", self.copy / "build.ninja")
        bad = self._run(self.copy, "--quiet")
        self.assertEqual(bad.returncode, 1)
        self.assertIn("was generated for", bad.stderr)

        # A refused tree must not get a stamp at all: a stale stamp left behind
        # by an earlier pass would let restat call the edge unchanged.
        bad_stamp = self.copy / "build" / "373307D9" / "ninja_root_checked.stamp"
        self.assertEqual(self._run(self.copy, "--quiet", "--stamp-out", str(bad_stamp)).returncode, 1)
        self.assertFalse(bad_stamp.exists(), "a refusal wrote a stamp")

    def test_stamp_is_tree_independent(self):
        """A reflinked worktree inherits main's stamp; if the body named the
        root, its first `ninja` would rewrite it and re-fire REPORT for no
        reason. A pass is the only verdict a stamp can record."""
        self.write_manifest(self.origin, generated_for=self.origin, with_var=True)
        self.write_manifest(self.copy, generated_for=self.copy, with_var=True)
        a = self.origin / "a.stamp"
        b = self.copy / "b.stamp"
        self.assertEqual(self._run(self.origin, "--quiet", "--stamp-out", str(a)).returncode, 0)
        self.assertEqual(self._run(self.copy, "--quiet", "--stamp-out", str(b)).returncode, 0)
        self.assertEqual(a.read_text(), b.read_text())


class TestPatchGuardRefusesBeforeBuilding(NinjaRootFixture):
    """`ensure_patched_tree` runs `ninja post-compile` -- the very command that
    writes into the foreign tree -- so the root check has to fire BEFORE it, and
    as an `UnpatchedTreeError` so the existing callers' handlers refuse."""

    def setUp(self):
        super().setUp()
        sys.path.insert(0, str(REPO_ROOT / "scripts"))
        self.addCleanup(sys.path.remove, str(REPO_ROOT / "scripts"))
        from orchestrator import patch_guard
        self.pg = patch_guard
        scripts = self.copy / "scripts"
        scripts.mkdir()
        shutil.copy(CHECKER, scripts / "verify_ninja_root.py")
        (scripts / "verify_objs_patched.py").write_text("import sys; sys.exit(0)\n")
        # A "build tool" that records it was run. If it runs, the guard lost.
        self.marker = self.tmp / "BUILD_RAN"
        make = self.tmp / "fake_make.sh"
        make.write_text(f"#!/bin/sh\ntouch {self.marker}\n")
        make.chmod(0o755)
        (self.copy / "objdiff.json").write_text(
            '{"custom_make": "%s", "custom_args": []}' % make)

    def test_foreign_manifest_refused_before_build(self):
        self.assertTrue(issubclass(self.pg.ForeignNinjaRootError,
                                   self.pg.UnpatchedTreeError))
        self.write_manifest(self.copy, generated_for=self.origin, with_var=True)
        with self.assertRaises(self.pg.ForeignNinjaRootError) as ctx:
            self.pg.ensure_patched_tree(self.copy, build=True)
        self.assertIn("was generated for", str(ctx.exception))
        self.assertFalse(self.marker.exists(),
                         "the build ran before the root check refused")

    def test_own_manifest_passes_the_root_check(self):
        self.write_manifest(self.copy, generated_for=self.copy, with_var=True)
        self.assertIn("belongs to this tree", self.pg.ensure_ninja_root(self.copy))


class TestWiredIntoGeneratedManifest(unittest.TestCase):
    """The check enforces nothing unless the GENERATED build.ninja runs it.

    It landed on 2026-09-13 with no caller at all; this reads the real manifest
    of the tree the test runs in (skipped if unconfigured or unsplit, i.e. no
    compile edges) and asserts every compile edge is ordered after it.
    """

    STAMP = "build/373307D9/ninja_root_checked.stamp"
    # The three rules that run `cd $in_dir && cl.exe /Fo$abs_out` (msvc_link
    # consumes objects that are already ordered after pre-compile).
    COMPILE_RULES = ("msvc", "msvc_pch", "msvc_pch_create")

    @classmethod
    def setUpClass(cls):
        bn = REPO_ROOT / "build.ninja"
        if not bn.exists():
            raise unittest.SkipTest("no build.ninja in this tree")
        cls.text = vnr._unwrap(bn.read_text(encoding="utf-8", errors="replace"))
        cls.edges = {}  # output -> (rule, inputs-line)
        import re
        for m in re.finditer(r"^build (.+?): (\S+)(.*)$", cls.text, re.MULTILINE):
            for out in m.group(1).split(" | ")[0].split():
                cls.edges[out] = (m.group(2), m.group(3))
        if not any(r in cls.COMPILE_RULES for r, _ in cls.edges.values()):
            raise unittest.SkipTest("manifest has no compile edges (unsplit tree)")

    def test_check_edge_is_always_dirty(self):
        self.assertIn(self.STAMP, self.edges, "no edge produces the root stamp")
        rule, rest = self.edges[self.STAMP]
        self.assertEqual(rule, "ninja_root_check")
        self.assertIn("always", rest)
        self.assertRegex(self.text, r"(?m)^ninja_root = /")

    def test_pre_compile_requires_the_check(self):
        rule, rest = self.edges["pre-compile"]
        self.assertEqual(rule, "phony")
        self.assertIn(self.STAMP, rest.split("||")[0])

    def test_every_compile_edge_is_ordered_after_pre_compile(self):
        compiles = {o: r for o, (rule, r) in self.edges.items() if rule in self.COMPILE_RULES}
        self.assertGreater(len(compiles), 100)
        missing = [o for o, r in compiles.items()
                   if "||" not in r or "pre-compile" not in r.split("||", 1)[1].split()]
        self.assertEqual(missing, [], f"{len(missing)} compile edges skip pre-compile")

    def test_reports_depend_on_the_check(self):
        for rep in ("report.json", "report_raw.json", "baseline.json"):
            out = f"build/373307D9/{rep}"
            self.assertIn(out, self.edges)
            self.assertIn(self.STAMP, self.edges[out][1], f"{rep} does not depend on the check")


if __name__ == "__main__":
    unittest.main(verbosity=2)
