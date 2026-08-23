#!/usr/bin/env python3
"""Sabotage tests for the complete-unit guard (task #145).

objdiff's `metadata.complete: true` substitutes 100% for a measurement on every
function of a unit that has no BASE object.  It is a sanctioned upstream hatch
and is deliberately not being closed; `scripts/verify_complete_units.py` is this
repo's assertion that it is not firing here.

Same discipline as `tests/test_split_currency.py`: a guard nobody has watched
FAIL is not a guard.  Every test asserts GREEN on a healthy fixture, breaks
exactly one thing, asserts RED **pinning the reason**, restores, and asserts
GREEN again.  Pinning the reason matters because several distinct defects all
raise -- a guard that goes red for the wrong reason must fail these, not pass.

Task #149 added the second half: the guard used to read objects that were zero
bytes because `cl.exe` was mid-write, and fail a parallel build.  The
`MidWriteRaceTest` cases below construct that exact condition -- an empty object
with a REAL process named `ninja` alive in the tree -- and assert the guard goes
INDETERMINATE rather than red, while the same fixture without the build stays
red.  Nothing is monkey-patched: `ninja_builds_in_flight` runs unmodified
against a real `/proc`.

Run:  python3 -m pytest tests/test_complete_units.py -q
      python3 tests/test_complete_units.py            (unittest fallback)
"""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CHECKER = REPO_ROOT / "scripts" / "verify_complete_units.py"


def _load_checker():
    spec = importlib.util.spec_from_file_location("_vcu_under_test", CHECKER)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


vcu = _load_checker()


def _fixture(tmp: Path, units: list[dict], objects: dict[str, bytes]) -> Path:
    for rel, data in objects.items():
        p = tmp / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    (tmp / "objdiff.json").write_text(json.dumps({"units": units}))
    return tmp


def _unit(name: str, base_path: str | None, complete: bool) -> dict:
    u: dict = {"name": name, "target_path": "t/x.obj",
               "metadata": {"complete": complete}}
    if base_path is not None:
        u["base_path"] = base_path
    return u


HEALTHY_UNITS = [
    _unit("default/alpha", "build/src/alpha.obj", True),
    _unit("default/beta", "build/src/beta.obj", True),
    _unit("default/gamma", "build/src/gamma.obj", False),
]
HEALTHY_OBJECTS = {
    "build/src/alpha.obj": b"\x00\x01",
    "build/src/beta.obj": b"\x00\x02",
    "build/src/gamma.obj": b"\x00\x03",
}


class CompleteUnitGuardTest(unittest.TestCase):

    def assert_green(self, root: Path, expect_in_note: str = ""):
        note = vcu.check(root)
        if expect_in_note:
            self.assertIn(expect_in_note, note)
        return note

    def assert_red(self, root: Path, exc_type, reason_substring: str):
        with self.assertRaises(exc_type) as cm:
            vcu.check(root)
        self.assertIn(reason_substring, str(cm.exception),
                      f"went red, but not for the pinned reason "
                      f"{reason_substring!r}: {cm.exception}")

    # -- the two shapes the hatch takes -------------------------------------

    def test_deleted_object_is_caught_and_restoring_clears_it(self):
        with tempfile.TemporaryDirectory() as td:
            root = _fixture(Path(td), HEALTHY_UNITS, dict(HEALTHY_OBJECTS))
            self.assert_green(root, "2/2 `complete: true` units")

            victim = root / "build/src/alpha.obj"
            saved = victim.read_bytes()
            victim.unlink()
            self.assert_red(root, vcu.UncreditedCompleteUnitError,
                            "does not exist on disk")
            # ...and it must NAME the unit, not just count offenders.
            with self.assertRaises(vcu.UncreditedCompleteUnitError) as cm:
                vcu.check(root)
            self.assertIn("default/alpha", str(cm.exception))
            self.assertNotIn("default/beta", str(cm.exception))

            victim.write_bytes(saved)
            self.assert_green(root, "2/2 `complete: true` units")

    def test_missing_base_path_key_is_caught(self):
        """The SILENT shape: no base_path at all is what objdiff credits 100%."""
        with tempfile.TemporaryDirectory() as td:
            units = [_unit("default/alpha", None, True),
                     _unit("default/gamma", "build/src/gamma.obj", False)]
            root = _fixture(Path(td), units,
                            {"build/src/gamma.obj": b"\x00"})
            self.assert_red(root, vcu.UncreditedCompleteUnitError,
                            "no `base_path`")

    def test_zero_byte_object_is_caught(self):
        """Task #142's shape: a .cpp that stops emitting an object."""
        with tempfile.TemporaryDirectory() as td:
            root = _fixture(Path(td), HEALTHY_UNITS, dict(HEALTHY_OBJECTS))
            (root / "build/src/alpha.obj").write_bytes(b"")
            self.assert_red(root, vcu.UncreditedCompleteUnitError, "ZERO BYTES")

    def test_a_non_complete_unit_missing_its_object_is_NOT_an_offence(self):
        """The negative control on the guard's own scope.

        1,244 of dc3's units legitimately have no base object (the xdk stubs).
        A guard that reddened on those would be unusable and would be quietly
        disabled, which is worse than not having it.
        """
        with tempfile.TemporaryDirectory() as td:
            root = _fixture(Path(td), HEALTHY_UNITS, dict(HEALTHY_OBJECTS))
            (root / "build/src/gamma.obj").unlink()
            self.assert_green(root, "2/2 `complete: true` units")

    # -- vacuity: nothing to check is not the same as clean -----------------

    def test_no_complete_units_is_a_refusal(self):
        with tempfile.TemporaryDirectory() as td:
            root = _fixture(Path(td),
                            [_unit("default/gamma", "build/src/gamma.obj", False)],
                            {"build/src/gamma.obj": b"\x00"})
            self.assert_red(root, vcu.EmptyPopulationError,
                            "carry `metadata.complete: true`")

    def test_zero_units_is_a_refusal(self):
        with tempfile.TemporaryDirectory() as td:
            root = _fixture(Path(td), [], {})
            self.assert_red(root, vcu.EmptyPopulationError, "declares ZERO")

    def test_typod_units_key_is_a_refusal(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "objdiff.json").write_text(json.dumps({"unit": []}))
            self.assert_red(root, vcu.MissingConfigError, "has no `units` array")

    def test_absent_config_is_a_refusal(self):
        with tempfile.TemporaryDirectory() as td:
            self.assert_red(Path(td), vcu.MissingConfigError, "is absent")

    # -- exit codes, read without a pipe ------------------------------------

    def test_cli_exit_codes_are_distinct(self):
        """1 (offender), 5 (nothing to check) and 4 (no config) must differ.

        A disarmed tripwire has to exit differently from a passing one, or
        deleting objdiff.json reads as success.
        """
        cases = [
            (HEALTHY_UNITS, dict(HEALTHY_OBJECTS), 0),
            ([_unit("a", None, True)], {}, 1),
            ([_unit("a", "b/a.obj", False)], {"b/a.obj": b"\x00"}, 5),
        ]
        for units, objects, expected in cases:
            with tempfile.TemporaryDirectory() as td:
                root = _fixture(Path(td), units, dict(objects))
                # No pipe: `subprocess.run` reports the checker's own status.
                proc = subprocess.run(
                    [sys.executable, str(CHECKER), "--check",
                     "--project-dir", str(root)],
                    capture_output=True, text=True)
                self.assertEqual(proc.returncode, expected,
                                 f"units={units}\n{proc.stdout}{proc.stderr}")

        with tempfile.TemporaryDirectory() as td:
            proc = subprocess.run(
                [sys.executable, str(CHECKER), "--check", "--project-dir", td],
                capture_output=True, text=True)
            self.assertEqual(proc.returncode, 4)

    def test_stamp_is_write_if_changed(self):
        """The stamp must not move on a re-check, or every `ninja` re-runs REPORT.

        And it MUST move when the complete-unit set changes, or the guard's
        verdict could go stale behind an unmoved stamp.
        """
        with tempfile.TemporaryDirectory() as td:
            root = _fixture(Path(td), HEALTHY_UNITS, dict(HEALTHY_OBJECTS))
            stamp = root / "stamp.txt"
            args = [sys.executable, str(CHECKER), "--check", "--quiet",
                    "--project-dir", str(root), "--stamp-out", str(stamp)]
            self.assertEqual(subprocess.run(args).returncode, 0)
            first = stamp.read_bytes()
            mtime = stamp.stat().st_mtime_ns

            # An ordinary recompile changes object CONTENT. The stamp must not
            # notice -- that is what keeps the `always` edge cheap.
            (root / "build/src/alpha.obj").write_bytes(b"\xff" * 64)
            self.assertEqual(subprocess.run(args).returncode, 0)
            self.assertEqual(stamp.read_bytes(), first)
            self.assertEqual(stamp.stat().st_mtime_ns, mtime,
                             "stamp was rewritten with identical content; "
                             "restat cannot save REPORT from re-firing")

            # Adding a complete unit MUST move it.
            units = HEALTHY_UNITS + [_unit("default/delta", "build/src/delta.obj", True)]
            (root / "build/src/delta.obj").write_bytes(b"\x00\x04")
            (root / "objdiff.json").write_text(json.dumps({"units": units}))
            self.assertEqual(subprocess.run(args).returncode, 0)
            self.assertNotEqual(stamp.read_bytes(), first)

    def test_selftest_negative_control_passes(self):
        """The tool's own `--selftest` must run and must pass here."""
        proc = subprocess.run([sys.executable, str(CHECKER), "--selftest"],
                              capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertIn("must FAIL the checker", proc.stdout)

    def test_this_repo_is_currently_green(self):
        """The live assertion, with its denominator on the record.

        968 of 2,224 units on 2026-08-23. If this fails, either an object went
        missing or configure.py's unit table changed -- both are things to look
        at, not to relax the test for.

        This one runs against the REAL tree, so it is the one test here that can
        collide with a build another lane is running in it. That is reported as
        an explicit skip naming the pid -- never as a pass, and never as the
        failure it used to be.
        """
        try:
            note = vcu.check(REPO_ROOT)
        except vcu.BuildInFlightError as exc:
            self.skipTest(f"a ninja is building {REPO_ROOT} right now, so "
                          f"missing/empty objects cannot be told from mid-write "
                          f"ones: {str(exc).splitlines()[0]}")
        self.assertIn("`complete: true` units have a non-empty base object", note)
        head, _, _ = note.partition(" ")
        got, _, total = head.partition("/")
        self.assertEqual(got, total, f"some complete units lack an object: {note}")
        self.assertGreater(int(total), 0, "empty population must not read as clean")


class MidWriteRaceTest(unittest.TestCase):
    """Task #149: an object that is empty because it is BEING WRITTEN.

    Measured 2026-08-23 in a worktree, one incremental `ninja -j 16` over 220
    units: 169 zero-byte windows, median 58 ms, max 552 ms; polling the pre-fix
    checker through the same build gave 85 build-failing exit-1s in 1,699 runs.
    Post-fix, over the same build: 0 exit-1s and 83 exit-6s.
    """

    def _zero_byte_fixture(self, tmp: Path) -> Path:
        root = _fixture(Path(tmp), HEALTHY_UNITS, dict(HEALTHY_OBJECTS))
        (root / "build/src/alpha.obj").write_bytes(b"")   # mid-write shape
        return root

    def _require_control(self, pid, root):
        if pid is None:
            self.skipTest(
                "cannot host the in-flight control on this platform (no /proc, "
                "or no `sleep` to copy) -- reporting a skip rather than a pass")

    # -- the race itself ----------------------------------------------------

    def test_empty_object_during_a_build_is_indeterminate_not_red(self):
        with tempfile.TemporaryDirectory() as td:
            root = self._zero_byte_fixture(Path(td))
            with vcu.fake_ninja_build(root) as pid:
                self._require_control(pid, root)
                with self.assertRaises(vcu.BuildInFlightError) as cm:
                    vcu.check(root)
            msg = str(cm.exception)
            self.assertIn("CANNOT ESTABLISH STATE", msg)
            self.assertIn(str(pid), msg)
            self.assertIn("default/alpha", msg)
            self.assertNotIn("REFUSING TO VOUCH", msg)

    def test_the_same_fixture_is_red_once_the_build_is_quiescent(self):
        """The other half. Tolerance that never ends is just a disabled guard."""
        with tempfile.TemporaryDirectory() as td:
            root = self._zero_byte_fixture(Path(td))
            with vcu.fake_ninja_build(root) as pid:
                self._require_control(pid, root)
                self.assertRaises(vcu.BuildInFlightError, vcu.check, root)
            # ...control gone, nothing else changed on disk:
            with self.assertRaises(vcu.UncreditedCompleteUnitError) as cm:
                vcu.check(root)
            self.assertIn("ZERO BYTES", str(cm.exception))
            self.assertIn("default/alpha", str(cm.exception))

    def test_deleted_object_during_a_build_is_also_indeterminate(self):
        with tempfile.TemporaryDirectory() as td:
            root = _fixture(Path(td), HEALTHY_UNITS, dict(HEALTHY_OBJECTS))
            (root / "build/src/alpha.obj").unlink()
            with vcu.fake_ninja_build(root) as pid:
                self._require_control(pid, root)
                self.assertRaises(vcu.BuildInFlightError, vcu.check, root)
            self.assertRaises(vcu.UncreditedCompleteUnitError, vcu.check, root)

    # -- and the four ways it must NOT become a licence to go quiet ---------

    def test_missing_base_path_KEY_is_red_even_during_a_build(self):
        """No compiler is ever halfway through writing a config key."""
        with tempfile.TemporaryDirectory() as td:
            root = _fixture(Path(td), [_unit("default/alpha", None, True)], {})
            with vcu.fake_ninja_build(root) as pid:
                self._require_control(pid, root)
                with self.assertRaises(vcu.UncreditedCompleteUnitError) as cm:
                    vcu.check(root)
            self.assertIn("no `base_path`", str(cm.exception))

    def test_a_mix_of_shapes_is_red_not_indeterminate(self):
        """One unexcusable offender makes the whole verdict definite."""
        with tempfile.TemporaryDirectory() as td:
            units = HEALTHY_UNITS + [_unit("default/delta", None, True)]
            root = _fixture(Path(td), units, dict(HEALTHY_OBJECTS))
            (root / "build/src/alpha.obj").write_bytes(b"")
            with vcu.fake_ninja_build(root) as pid:
                self._require_control(pid, root)
                with self.assertRaises(vcu.UncreditedCompleteUnitError) as cm:
                    vcu.check(root)
            self.assertIn("default/delta", str(cm.exception))
            self.assertIn("default/alpha", str(cm.exception))

    def test_ordered_after_compile_is_red_even_during_a_build(self):
        """What the ninja edge passes, having bought quiescence with deps."""
        with tempfile.TemporaryDirectory() as td:
            root = self._zero_byte_fixture(Path(td))
            with vcu.fake_ninja_build(root) as pid:
                self._require_control(pid, root)
                with self.assertRaises(vcu.UncreditedCompleteUnitError) as cm:
                    vcu.check(root, ordered_after_compile=True)
            self.assertIn("ZERO BYTES", str(cm.exception))
            self.assertIn("--ordered-after-compile", str(cm.exception))

    def test_a_build_in_ANOTHER_tree_excuses_nothing(self):
        """Six lanes build here at once. Only THIS tree's build is an excuse."""
        with tempfile.TemporaryDirectory() as td, \
                tempfile.TemporaryDirectory() as elsewhere:
            root = self._zero_byte_fixture(Path(td))
            with vcu.fake_ninja_build(Path(elsewhere)) as pid:
                if pid is None:
                    self.skipTest("no in-flight control on this platform")
                # The control is genuinely alive -- just not here.
                self.assertTrue(vcu.ninja_builds_in_flight(Path(elsewhere)))
                self.assertEqual(vcu.ninja_builds_in_flight(root), [])
                self.assertRaises(vcu.UncreditedCompleteUnitError, vcu.check, root)

    def test_detector_is_silent_on_a_quiescent_tree(self):
        """Negative control on the detector itself.

        A detector stuck at 'yes' would turn every real offence into a skip,
        and would pass every other test in this class.
        """
        with tempfile.TemporaryDirectory() as td:
            self.assertEqual(vcu.ninja_builds_in_flight(Path(td)), [])

    # -- exit codes, read without a pipe ------------------------------------

    def test_indeterminate_has_its_own_exit_code(self):
        """6 must differ from 0 AND from 1, or the build cannot act on it."""
        with tempfile.TemporaryDirectory() as td:
            root = self._zero_byte_fixture(Path(td))
            base = [sys.executable, str(CHECKER), "--check", "--quiet",
                    "--project-dir", str(root)]
            with vcu.fake_ninja_build(root) as pid:
                self._require_control(pid, root)
                self.assertEqual(subprocess.run(base, capture_output=True).returncode, 6)
                self.assertEqual(
                    subprocess.run(base + ["--ordered-after-compile"],
                                   capture_output=True).returncode, 1)
            self.assertEqual(subprocess.run(base, capture_output=True).returncode, 1)


class NinjaEdgeWiringTest(unittest.TestCase):
    """The primary fix lives in the build graph, so assert the build graph.

    `--ordered-after-compile` disables the mid-write tolerance. It is only
    sound because the edge is order-only after every compile edge and every
    post-compile patcher. Either half without the other is a defect:

      * deps without the flag  -> a real task-#142 object reads as exit 6
        "indeterminate" during the build that would have caught it;
      * flag without the deps  -> the task-#149 race is back, with the
        tolerance explicitly switched off.

    Asserted on TWO surfaces, because neither alone is enough:

      * `tools/project.py`, which is what review and git see; and
      * the GENERATED `build.ninja`, which is what ninja actually obeys and
        which is gitignored, so a correct generator with a stale generated file
        means the protection is not installed yet.

    A stale `build.ninja` FAILS rather than skips.  Skipping would be a
    laundering path: delete the deps from the generator, and the generated file
    goes stale, and a skip-on-stale test turns that into green.
    """

    EDGE = "build/373307D9/complete_units_checked.stamp"
    GENERATOR = REPO_ROOT / "tools" / "project.py"

    def _build_ninja(self) -> str:
        p = REPO_ROOT / "build.ninja"
        if not p.exists():
            self.fail(f"{p} is absent -- run `python3 configure.py` in "
                      f"{REPO_ROOT} first. Skipping here would make this test "
                      f"unable to fail.")
        if p.stat().st_mtime < self.GENERATOR.stat().st_mtime:
            self.fail(
                f"{p} is OLDER than {self.GENERATOR}, so the edge this test "
                f"reads is not the edge ninja would run. Re-run `ninja` (or "
                f"`python3 configure.py`) in {REPO_ROOT}. This is a failure and "
                f"not a skip on purpose: 'stale generated file' is exactly the "
                f"state a removed order-only dep would produce.")
        # Un-wrap ninja's `$`-continuations so each edge is one line.
        return p.read_text().replace("$\n", "")

    def test_generator_source_pairs_the_deps_with_the_flag(self):
        """The surface review sees. Independent of any build having been run."""
        src = self.GENERATOR.read_text()
        self.assertIn("--ordered-after-compile", src,
                      "tools/project.py no longer passes the flag")
        self.assertIn('complete_units_order_only: List[str] = ["all_source"]', src,
                      "tools/project.py no longer seeds the guard's order-only "
                      "deps with all_source -- the task-#149 race is back")
        self.assertIn('complete_units_order_only.append("post-compile")', src,
                      "tools/project.py no longer orders the guard after the "
                      "post-compile patchers, which rewrite the very objects "
                      "objdiff reads")
        self.assertIn("order_only=complete_units_order_only", src,
                      "the order-only list is computed but never attached to "
                      "the complete_units_check edge")

    def _edge_line(self, text: str) -> str:
        for line in text.splitlines():
            if line.startswith(f"build {self.EDGE}:"):
                return " ".join(line.split())
        self.fail(f"no edge produces {self.EDGE} in build.ninja")

    def test_edge_is_order_only_after_all_source_and_post_compile(self):
        line = self._edge_line(self._build_ninja())
        self.assertIn("||", line,
                      f"the complete-unit guard has NO order-only deps, so "
                      f"ninja may schedule it concurrently with cl.exe and read "
                      f"a half-written .obj (task #149): {line}")
        order_only = line.split("||", 1)[1].split()
        self.assertIn("all_source", order_only, line)
        self.assertIn("post-compile", order_only, line)

    def test_rule_passes_ordered_after_compile(self):
        text = self._build_ninja()
        rule = ""
        grab = False
        for line in text.splitlines():
            if line.startswith("rule complete_units_check"):
                grab = True
                continue
            if grab:
                if line.startswith((" ", "\t")):
                    rule += line
                else:
                    break
        self.assertIn("--ordered-after-compile", rule,
                      "the edge has earned quiescence with order-only deps but "
                      "does not spend it, so a genuine uncredited unit would be "
                      "reported as indeterminate instead of as an offence")

    def test_flag_and_deps_are_both_present_or_this_test_says_which(self):
        """One assertion that names the pairing, so a partial revert is loud."""
        text = self._build_ninja()
        has_deps = "||" in self._edge_line(text)
        has_flag = "--ordered-after-compile" in text
        self.assertEqual(
            has_deps, has_flag,
            f"order-only deps ({has_deps}) and --ordered-after-compile "
            f"({has_flag}) must travel together; see the comment on the "
            f"complete_units_check edge in tools/project.py")


if __name__ == "__main__":
    unittest.main(verbosity=2)
