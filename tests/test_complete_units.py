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
        """
        note = vcu.check(REPO_ROOT)
        self.assertIn("`complete: true` units have a non-empty base object", note)
        head, _, _ = note.partition(" ")
        got, _, total = head.partition("/")
        self.assertEqual(got, total, f"some complete units lack an object: {note}")
        self.assertGreater(int(total), 0, "empty population must not read as clean")


if __name__ == "__main__":
    unittest.main(verbosity=2)
