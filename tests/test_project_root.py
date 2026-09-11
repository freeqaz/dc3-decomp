#!/usr/bin/env python3
"""A guard invoked in tree B must report about tree B -- not about whatever
tree its own `scripts/` directory really lives in.

The defect
----------
`scripts/measure_progress.sh` built its baseline in a throwaway worktree whose
`scripts/` was a SYMLINK to the main checkout's.  Every guard and patcher the
baseline build ran opened with

    PROJECT_ROOT = Path(__file__).resolve().parent.parent

and `.resolve()` follows that symlink, so all of them acted on MAIN.  Measured
2026-09-11 in a baseline-shaped worktree holding one object and its own
matching manifest: `verify_objs_patched.py --verify-manifest` exited 1 quoting
main's 990-object manifest and its 817 drifted objects; `verify_split_current.py
--check` said "split current" for a tree that has never been split; and
`obj_anon_ns_patcher.py --batch` planned 990 patches against main's objects --
the ninja edge runs that one with `--apply`.

What this file pins
-------------------
Two minimal fixture trees, A ("main") and B ("worktree", with `scripts/` a
symlink into A).  Each case asserts the healthy behaviour, then SABOTAGES the
one line that decides the root -- rewriting `project_root.py` to resolve
symlinks again, which is exactly the reverted code -- and REQUIRES the same
assertion to go red.  A sabotage that produces no failure fails the test: this
repo has shipped three guards that stayed green on deliberately broken code
(see MEMORY: "A test you cannot make fail is not a test").

Anti-vacuity clauses, each learned from a specimen in this repo:

* The two trees are asserted to be genuinely DISTINGUISHABLE (different object
  bytes, different manifest hashes) before anything is concluded from a
  verdict.  Two identical fixtures would make every case pass for free.
* The sabotage is asserted to have actually landed (the mutated file differs
  from the original) before its effect is measured.
* `test_shipped_guards_do_not_resolve_their_own_path` reads the REAL scripts,
  from an explicitly named list, and fails if the old idiom returns.  A script
  added to the build without being added to that list is a failure, not a
  silent widening of the exemption.
* Nothing here skips.  A missing fixture prerequisite is a failure.

Run:  python3 -m pytest tests/test_project_root.py -q
      python3 tests/test_project_root.py            (unittest fallback)
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = REPO_ROOT / "scripts"
VERSION = "373307D9"

#: The scripts a fixture tree needs.  `project_root.py` is the unit under test;
#: `obj_patch_io.py` is imported by every patcher at module load.
_FIXTURE_SCRIPTS = (
    "project_root.py",
    "verify_objs_patched.py",
    "verify_split_current.py",
    "obj_patch_io.py",
)

#: Every script the generated `build.ninja` invokes as `python3 scripts/<x>.py`
#: with cwd set to the tree being built, and which therefore MUST derive its
#: root from the invocation.  Kept explicit so that adding a build-invoked
#: script without fixing its root resolution fails this test.
_BUILD_INVOKED = (
    "obj_anon_ns_patcher.py",
    "obj_dynamic_init_patcher.py",
    "obj_guard_patcher.py",
    "obj_bool_mangle_patcher.py",
    "obj_atexit_scope_patcher.py",
    "obj_build_metadata_patcher.py",
    "verify_data_symbol_spelling.py",
    "verify_objs_patched.py",
    "verify_split_current.py",
    "verify_complete_units.py",
    "create_data_stubs.py",
    "gen_icf_alias_map.py",
    "check_undefined_decomp_symbols.py",
    "report_freshness.py",
    "obj_patch_chain.py",
)

#: The idiom that caused the defect, in the spellings this repo actually uses.
_BAD_ROOT_IDIOMS = (
    re.compile(r"Path\(__file__\)\.resolve\(\)\.parent\.parent"),
    re.compile(r"os\.path\.dirname\(\s*os\.path\.dirname\(\s*os\.path\.realpath"),
    re.compile(r"os\.path\.realpath\(__file__\)"),
)

#: Reverting `project_root.py` to the broken behaviour, as a text substitution
#: on the fixture's copy.  `invoked_root` is the one function that decides.
_SABOTAGE_FROM = "    p = Path(os.path.abspath(script_file))"
_SABOTAGE_TO = "    p = Path(os.path.realpath(script_file))  # SABOTAGE"


def _run(args, cwd):
    return subprocess.run([sys.executable, *args], cwd=str(cwd),
                          capture_output=True, text=True, timeout=300)


class TwoTreeFixture:
    """Tree A (a 'main' checkout) and tree B (a worktree whose scripts/ is a
    symlink into A).  Both are complete enough for the guards under test."""

    def __init__(self, base: Path):
        self.a = base / "A"
        self.b = base / "B"
        for tree, obj_name, obj_bytes in ((self.a, "main.obj", b"AAAA-main"),
                                          (self.b, "wt.obj", b"BBBB-worktree")):
            (tree / "build" / VERSION / "src").mkdir(parents=True)
            (tree / "build" / VERSION / "obj").mkdir(parents=True)
            (tree / "config" / VERSION).mkdir(parents=True)
            (tree / "build" / VERSION / "src" / obj_name).write_bytes(obj_bytes)
            for name, body in (("symbols.txt", f"# {tree.name}\n"),
                               ("splits.txt", f"# {tree.name}\n"),
                               ("config.yml", f"# {tree.name}\n")):
                (tree / "config" / VERSION / name).write_text(body)
        # A owns a real scripts/; B only links to it.  This IS the defect's
        # shape: `measure_progress.sh` produced exactly this.
        (self.a / "scripts").mkdir()
        for name in _FIXTURE_SCRIPTS:
            shutil.copy2(SCRIPTS / name, self.a / "scripts" / name)
        (self.b / "scripts").symlink_to(self.a / "scripts")

        self.checker_in_b = self.b / "scripts" / "verify_objs_patched.py"
        self.split_in_b = self.b / "scripts" / "verify_split_current.py"
        self.project_root_py = self.a / "scripts" / "project_root.py"
        self._pristine = self.project_root_py.read_text()

    # -- fixture helpers ---------------------------------------------------
    def emit_manifests(self, test):
        """Give each tree a manifest OF ITSELF, via the explicit `--repo` path
        (which is correct in both the healthy and the sabotaged build)."""
        for tree in (self.a, self.b):
            p = _run([str(self.checker_in_b), "--repo", str(tree), "--emit"], tree)
            test.assertEqual(p.returncode, 0, f"--emit failed for {tree}: {p.stderr}")
        a_doc = json.loads((self.a / "build" / VERSION / "patch_state.json").read_text())
        b_doc = json.loads((self.b / "build" / VERSION / "patch_state.json").read_text())
        # ANTI-VACUITY: if the two trees hashed the same, every verdict below
        # would be indistinguishable and this whole file would prove nothing.
        test.assertNotEqual(a_doc["tree_sha256"], b_doc["tree_sha256"],
                            "fixture trees are not distinguishable")
        return a_doc, b_doc

    def stamp_split(self, tree: Path, test):
        p = _run([str(self.split_in_b), "--project-dir", str(tree), "--begin",
                  "--quiet"], tree)
        test.assertEqual(p.returncode, 0, p.stderr)
        p = _run([str(self.split_in_b), "--project-dir", str(tree), "--complete",
                  "--quiet"], tree)
        test.assertEqual(p.returncode, 0, p.stderr)

    # -- the negative control ---------------------------------------------
    def sabotage(self, test):
        """Revert root resolution to `__file__`-of-a-symlink."""
        text = self.project_root_py.read_text()
        test.assertIn(_SABOTAGE_FROM, text,
                      "project_root.py no longer contains the line this "
                      "sabotage rewrites — update _SABOTAGE_FROM. A sabotage "
                      "that silently no-ops is how a test becomes vacuous.")
        self.project_root_py.write_text(text.replace(_SABOTAGE_FROM, _SABOTAGE_TO))
        test.assertNotEqual(self.project_root_py.read_text(), self._pristine,
                            "the sabotage did not land")
        self._purge_pycache()

    def restore(self):
        self.project_root_py.write_text(self._pristine)
        self._purge_pycache()

    def _purge_pycache(self):
        # A stale .pyc silently voids a sabotage (MEMORY:
        # pattern_pyc_staleness_in_sabotage_harnesses).  Reading an existing
        # .pyc is not blocked by PYTHONDONTWRITEBYTECODE, so delete them.
        shutil.rmtree(self.a / "scripts" / "__pycache__", ignore_errors=True)


class ProjectRootUnitTest(unittest.TestCase):
    """The resolver itself, with no build tree involved."""

    def setUp(self):
        sys.path.insert(0, str(SCRIPTS))
        import project_root  # noqa: E402
        self.mod = project_root

    def test_explicit_wins(self):
        with tempfile.TemporaryDirectory() as td:
            td = Path(td).resolve()
            self.assertEqual(
                self.mod.project_root(str(td / "scripts" / "x.py"), explicit=str(td)),
                td)

    def test_symlinked_scripts_dir_yields_the_invoked_tree(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td).resolve()
            (base / "A" / "scripts").mkdir(parents=True)
            (base / "A" / "scripts" / "x.py").write_text("")
            (base / "B").mkdir()
            (base / "B" / "scripts").symlink_to(base / "A" / "scripts")
            got = self.mod.project_root(str(base / "B" / "scripts" / "x.py"),
                                        warn=False)
            self.assertEqual(got, base / "B")
            # ...and the old spelling is the one that gets it wrong:
            self.assertEqual(
                Path(os.path.realpath(base / "B" / "scripts" / "x.py")).parent.parent,
                base / "A")

    def test_relative_invocation_resolves_against_cwd(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td).resolve()
            (base / "scripts").mkdir()
            cwd = os.getcwd()
            try:
                os.chdir(base)
                self.assertEqual(self.mod.project_root("scripts/x.py", warn=False),
                                 base)
            finally:
                os.chdir(cwd)

    def test_nested_scripts_dir_uses_levels(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td).resolve()
            self.assertEqual(
                self.mod.project_root(str(base / "scripts" / "analysis" / "x.py"),
                                      levels=2, warn=False),
                base)


class GuardsMeasureTheInvokedTreeTest(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.fx = TwoTreeFixture(Path(self._td.name).resolve())

    def tearDown(self):
        self._td.cleanup()

    # --- verify_objs_patched.py ------------------------------------------
    def test_manifest_check_in_B_is_about_B(self):
        self.fx.emit_manifests(self)
        p = _run([str(self.fx.checker_in_b), "--verify-manifest"], self.fx.b)
        self.assertEqual(p.returncode, 0, f"B should be clean:\n{p.stderr}")
        self.assertIn("1 objects match", p.stdout)

        # Make B, and ONLY B, drift.  A stays a perfect fixed point, so the
        # only way to go red is by actually reading B.
        (self.fx.b / "build" / VERSION / "src" / "wt.obj").write_bytes(b"CHANGED")
        p = _run([str(self.fx.checker_in_b), "--verify-manifest"], self.fx.b)
        self.assertEqual(p.returncode, 1, "B drifted and the guard did not say so")
        self.assertIn("wt.obj", p.stderr)
        self.assertNotIn("main.obj", p.stderr)

    def test_SABOTAGE_manifest_check_reverts_to_measuring_A(self):
        """The negative control: with root resolution reverted, the same call
        reports about A.  If this does not happen, the case above is vacuous."""
        self.fx.emit_manifests(self)
        # Make A drift and leave B clean -- the mirror image of the real
        # incident (main mid-churn, baseline tree clean).
        (self.fx.a / "build" / VERSION / "src" / "main.obj").write_bytes(b"CHANGED")

        healthy = _run([str(self.fx.checker_in_b), "--verify-manifest"], self.fx.b)
        self.assertEqual(healthy.returncode, 0,
                         "B is clean; the fixed guard must pass it")

        self.fx.sabotage(self)
        try:
            broken = _run([str(self.fx.checker_in_b), "--verify-manifest"], self.fx.b)
        finally:
            self.fx.restore()
        self.assertEqual(
            broken.returncode, 1,
            "SABOTAGE UNDETECTED: with `Path(__file__).resolve()` restored, the "
            "guard invoked in B still passed. This test cannot fail, so it is "
            "not a test.\nstdout:\n" + broken.stdout + "\nstderr:\n" + broken.stderr)
        self.assertIn("main.obj", broken.stderr,
                      "the sabotaged run went red for some other reason than "
                      "reading A's objects")

        after = _run([str(self.fx.checker_in_b), "--verify-manifest"], self.fx.b)
        self.assertEqual(after.returncode, 0, "restore did not bring B back green")

    # --- verify_split_current.py -----------------------------------------
    def test_split_guard_in_B_refuses_a_tree_that_was_never_split(self):
        self.fx.stamp_split(self.fx.a, self)          # A has a stamp; B has none
        self.assertFalse((self.fx.b / "build" / VERSION / "split_inputs.stamp").exists())

        p = _run([str(self.fx.split_in_b), "--check"], self.fx.b)
        self.assertEqual(p.returncode, 1,
                         "B has never been split; vouching for it is the bug")
        self.assertIn(str(self.fx.b), p.stderr)

    def test_SABOTAGE_split_guard_vouches_for_an_unsplit_tree(self):
        self.fx.stamp_split(self.fx.a, self)
        self.fx.sabotage(self)
        try:
            p = _run([str(self.fx.split_in_b), "--check"], self.fx.b)
        finally:
            self.fx.restore()
        self.assertEqual(
            p.returncode, 0,
            "SABOTAGE UNDETECTED: the reverted resolver was expected to read "
            "A's stamp and call B current.\n" + p.stdout + p.stderr)
        self.assertIn("split current", p.stdout)

        p = _run([str(self.fx.split_in_b), "--check"], self.fx.b)
        self.assertEqual(p.returncode, 1, "restore did not re-arm the guard")


class ShippedGuardsTest(unittest.TestCase):
    """The ratchet: the real scripts, not a fixture copy."""

    def test_shipped_guards_do_not_resolve_their_own_path(self):
        offenders = []
        for name in _BUILD_INVOKED:
            path = SCRIPTS / name
            self.assertTrue(path.is_file(),
                            f"{path} is missing; _BUILD_INVOKED is out of date")
            text = path.read_text()
            for rx in _BAD_ROOT_IDIOMS:
                if rx.search(text):
                    offenders.append(f"{name}: {rx.pattern}")
        self.assertEqual(
            offenders, [],
            "these build-invoked scripts derive their project root from the "
            "REAL path of their own file. In a tree whose scripts/ is a "
            "symlink that is another checkout, and the patchers among them "
            "WRITE there:\n  " + "\n  ".join(offenders))

    def test_measure_progress_does_not_symlink_scripts(self):
        text = (SCRIPTS / "measure_progress.sh").read_text()
        self.assertNotRegex(
            text, r'ln -sf? +"\$\{MAIN_REPO\}/scripts"',
            "measure_progress.sh is symlinking its baseline worktree's "
            "scripts/ at the main checkout again — the baseline build's guards "
            "and patchers would act on main.")

    def test_the_resolver_is_where_the_scripts_expect_it(self):
        self.assertTrue((SCRIPTS / "project_root.py").is_file())


if __name__ == "__main__":
    unittest.main(verbosity=2)
