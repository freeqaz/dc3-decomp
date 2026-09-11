#!/usr/bin/env python3
"""`pattern_census.py --apply` must refuse BEFORE it measures, and record `jobs`.

THE INCIDENT THESE TESTS ARE ABOUT
==================================
A lane was handed `pattern_census.py --ruler name_check --apply` with no `--db`
and ran it from a worktree.  Every `--apply` precondition lived at the BOTTOM of
`main()`, after the sweep, so the tool diffed 48,290 functions for ~6 minutes and
only then declined to write.  The refusal went to stderr while ~40 lines of
stdout sat in a block buffer, so it did not read as the last word, and the stale
scan the run was sent to replace stayed in place reading as current.

WHAT MAKES THESE TESTS ABLE TO FAIL
===================================
Two negative controls are inside the file, because "the tool did not print the
sweep banner" is a vacuous assertion if the banner string is a typo, and "the
destination was refused" is vacuous if the checker refuses everything:

  * `test_the_work_markers_are_strings_the_tool_actually_prints` reads
    `pattern_census.py` and requires every marker this file greps for to be a
    real literal in it.  Rename a banner and THIS test reddens, rather than the
    absence assertions passing for the wrong reason.
  * `test_a_usable_database_passes_the_preflight` drives
    `check_apply_destination()` against a database that is genuinely fine and
    requires it to return.  Without it, `raise ApplyDestinationError` as the
    first line of the function would make every refusal test green.

Run:  python3 -m pytest tests/test_pattern_census_apply.py -q
"""
from __future__ import annotations

import os
import sqlite3
import subprocess
import sys
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CENSUS = ROOT / "scripts" / "analysis" / "pattern_census.py"
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "scripts"))

from scripts.analysis import pattern_census  # noqa: E402
from orchestrator import database as db_mod  # noqa: E402

#: Strings the census prints only once it has STARTED WORKING.  Their absence is
#: the evidence that a refusal came before the measurement, so they are checked
#: against the source by a test of their own -- see the module docstring.
WORK_MARKERS = (
    "instrument (pre-scan)",
    "report.json functions",
    "recorded scan id=",
)

#: Generous: the point is "before a 6-minute sweep", and the measured figure on
#: this box is ~0.45 s.  A bound that only a running sweep can breach is the
#: honest one; the load-bearing assertion is WORK_MARKERS, not the clock.
REFUSAL_DEADLINE_S = 20.0


def run_census(*args, cwd: Path | None = None):
    """Run the census as a subprocess.  Returns (rc, stdout, stderr, seconds)."""
    t0 = time.monotonic()
    p = subprocess.run(
        [sys.executable, str(CENSUS), *args],
        cwd=str(cwd or ROOT), capture_output=True, text=True, timeout=600)
    return p.returncode, p.stdout, p.stderr, time.monotonic() - t0


def make_real_db(path: Path) -> Path:
    """A genuine decomp.db at the current schema, with the pattern tables.

    ⚠ `init_database()` alone is NOT enough, and that is a pre-existing defect
    worth knowing about rather than working around silently: it stamps a fresh
    file at `SCHEMA_VERSION` and creates only the BASE schema, so a brand-new
    decomp.db has `functions`/`attempts`/... and none of the tables introduced
    by migrations -- no `pattern_scans`, no `function_patterns`, no
    `patch_queue`.  The version says 18; the tables are v1's.  The migration
    chain is therefore run explicitly here.
    """
    conn = db_mod.init_database(str(path), allow_shadow=True)
    db_mod._run_migrations(conn, 1, db_mod.SCHEMA_VERSION)
    # ⚠ A SECOND, separate provenance gap, found the same way: these columns
    # exist in the real `decomp.db` and are created by NO migration and by no
    # base CREATE.  They were added ad hoc at some point and the schema chain
    # never learned about them, so a database rebuilt from `database.py` alone
    # cannot run `refresh_legacy_flags()` or the `v_function_patterns` view.
    # Adding them here rather than hiding the fixture behind a copy of the real
    # DB, so the gap stays visible.
    have = {r[1] for r in conn.execute("PRAGMA table_info(functions)")}
    undeclared = [c for c in (
        *pattern_census.LEGACY_FLAGS, *pattern_census.RETIRED_FLAGS,
        "match_percent_normalized") if c not in have]
    for col in undeclared:
        kind = "REAL" if col == "match_percent_normalized" else "INTEGER DEFAULT 0"
        conn.execute(f"ALTER TABLE functions ADD COLUMN {col} {kind}")
    conn.commit()
    conn.close()
    return path


class PreflightRefusesBeforeMeasuringTest(unittest.TestCase):
    """Each refusal must arrive with NO work done and a distinct exit code."""

    def assertRefusedWithoutWorking(self, rc, out, err, expect_rc, elapsed):
        blob = out + err
        started = [m for m in WORK_MARKERS if m in blob]
        self.assertEqual(
            rc, expect_rc,
            f"expected exit {expect_rc}, got {rc}\n--- stdout ---\n{out}"
            f"\n--- stderr ---\n{err}")
        self.assertEqual(
            started, [],
            f"the refusal came AFTER work started (markers seen: {started}). "
            f"That is the whole defect: the sweep must not run.")
        self.assertLess(elapsed, REFUSAL_DEADLINE_S,
                        f"refusal took {elapsed:.1f}s -- it measured something")

    def test_apply_without_db_exits_6_before_any_work(self):
        """The reproduced incident, exactly: --apply, no --db."""
        with TempTree() as t:
            rc, out, err, dt = run_census(
                "--project-dir", str(t.project), "--ruler", "name_check",
                "--apply", "--skip-patch-check")
            self.assertRefusedWithoutWorking(rc, out, err, 6, dt)
            self.assertIn("--apply needs --db", err)
            self.assertIn("DESTINATION UNUSABLE", err)

    def test_apply_against_the_worktree_tripwire_exits_6_and_names_the_path(self):
        """`<worktree>/decomp.db` is a deliberate NON-SQLite file."""
        with TempTree() as t:
            trip = t.project / "decomp.db"
            trip.write_text("This is NOT a database. It is a tripwire.\n")
            rc, out, err, dt = run_census(
                "--project-dir", str(t.project), "--ruler", "name_check",
                "--apply", "--db", str(trip), "--skip-patch-check")
            self.assertRefusedWithoutWorking(rc, out, err, 6, dt)
            self.assertIn("not a SQLite database", err)
            # "it did not write" without "here is where it looked" is not a
            # diagnosis.  The path must be in the message.
            self.assertIn(str(trip), err)

    def test_apply_to_a_missing_database_exits_6_and_does_not_create_it(self):
        with TempTree() as t:
            gone = t.project / "decomp.db"
            rc, out, err, dt = run_census(
                "--project-dir", str(t.project), "--ruler", "name_check",
                "--apply", "--db", str(gone), "--skip-patch-check")
            self.assertRefusedWithoutWorking(rc, out, err, 6, dt)
            self.assertIn("no such file", err)
            self.assertFalse(gone.exists(),
                             "a census must never CREATE its destination")

    def test_apply_to_a_database_owned_by_another_tree_exits_6(self):
        """The unmoored-scan class, refused at write time instead of read time."""
        with TempTree() as owner, TempTree() as measured:
            db = make_real_db(owner.project / "decomp.db")
            rc, out, err, dt = run_census(
                "--project-dir", str(measured.project), "--ruler", "name_check",
                "--apply", "--db", str(db), "--skip-patch-check")
            self.assertRefusedWithoutWorking(rc, out, err, 6, dt)
            self.assertIn(str(owner.project), err)
            self.assertIn(str(measured.project), err)

    @unittest.skipIf(os.geteuid() == 0, "root ignores the permission bits")
    def test_apply_to_an_unwritable_database_exits_6(self):
        """Existing, valid SQLite, right tree -- and still not writable."""
        with TempTree() as t:
            db = make_real_db(t.project / "decomp.db")
            # WAL needs the directory too, which is the realistic read-only case.
            os.chmod(db, 0o444)
            os.chmod(t.project, 0o555)
            try:
                rc, out, err, dt = run_census(
                    "--project-dir", str(t.project), "--ruler", "name_check",
                    "--apply", "--db", str(db), "--skip-patch-check")
            finally:
                os.chmod(t.project, 0o755)
                os.chmod(db, 0o644)
            self.assertRefusedWithoutWorking(rc, out, err, 6, dt)
            self.assertIn("not writable", err)

    def test_apply_with_limit_is_refused_before_any_work(self):
        """Exit 3 kept, but taken on the FLAG rather than after the sweep."""
        with TempTree() as t:
            db = make_real_db(t.project / "decomp.db")
            rc, out, err, dt = run_census(
                "--project-dir", str(t.project), "--ruler", "name_check",
                "--apply", "--limit", "20", "--db", str(db),
                "--skip-patch-check")
            self.assertRefusedWithoutWorking(rc, out, err, 3, dt)
            self.assertIn("--limit 20", err)

    def test_apply_with_negative_control_is_refused_before_any_work(self):
        with TempTree() as t:
            db = make_real_db(t.project / "decomp.db")
            rc, out, err, dt = run_census(
                "--project-dir", str(t.project), "--ruler", "none", "--apply",
                "--negative-control", "--db", str(db), "--skip-patch-check")
            self.assertRefusedWithoutWorking(rc, out, err, 2, dt)

    def test_apply_with_no_patterns_is_refused_before_any_work(self):
        with TempTree() as t:
            db = make_real_db(t.project / "decomp.db")
            rc, out, err, dt = run_census(
                "--project-dir", str(t.project), "--ruler", "none", "--apply",
                "--no-patterns", "--db", str(db), "--skip-patch-check")
            self.assertRefusedWithoutWorking(rc, out, err, 2, dt)


class NegativeControlsTest(unittest.TestCase):
    """Without these two, every test above could be green for a wrong reason."""

    def test_a_usable_database_passes_the_preflight(self):
        """The checker must ACCEPT a database that is genuinely fine.

        `raise ApplyDestinationError(...)` as the first line of
        `check_apply_destination` would make every refusal test in this file
        pass.  This is the assertion that forbids it.
        """
        with TempTree() as t:
            db = make_real_db(t.project / "decomp.db")
            got = pattern_census.check_apply_destination(
                str(db), t.project, "name_check")
            self.assertEqual(got, db.resolve())

    def test_the_work_markers_are_strings_the_tool_actually_prints(self):
        """`WORK_MARKERS` must be real literals, or their absence proves nothing."""
        src = CENSUS.read_text()
        for marker in WORK_MARKERS:
            self.assertIn(
                marker, src,
                f"{marker!r} is not printed by pattern_census.py any more, so "
                f"asserting its ABSENCE is vacuous. Fix the marker list.")

    def test_preflight_is_reached_before_the_patch_check_and_the_universe(self):
        """Source-order assertion: the guard is only a guard if it is FIRST.

        The subprocess tests all pass `--skip-patch-check`, so on their own they
        cannot see a preflight that sits after `ensure_patched_tree` -- which is
        a full `ninja`, and the more expensive half of the incident.
        """
        src = CENSUS.read_text()
        body = src[src.index("def main()"):]
        call = body.index("preflight_apply(")
        for later in ("ensure_patched_tree(", "report_universe(",
                      "sweep_functions(", "read_instrument("):
            self.assertLess(
                call, body.index(later),
                f"preflight_apply() must run before {later} -- otherwise "
                f"--apply is still validated after the expensive work.")


class JobsIsRecordedTest(unittest.TestCase):
    """`pattern_scans.jobs` -- without it a scan's duration means nothing."""

    def test_write_scan_records_the_jobs_value_it_was_given(self):
        with TempTree() as t:
            db = make_real_db(t.project / "decomp.db")
            _write_stub_scan(db, t.project, jobs=5)
            con = sqlite3.connect(str(db))
            got = con.execute("SELECT jobs FROM pattern_scans "
                              "ORDER BY id DESC LIMIT 1").fetchone()[0]
            con.close()
            # 5, not 1 and not the argparse default of 8: a hardcoded constant
            # anywhere in the write path fails here.
            self.assertEqual(got, 5)

    def test_jobs_reaches_the_view_the_gate_reads(self):
        with TempTree() as t:
            db = make_real_db(t.project / "decomp.db")
            _write_stub_scan(db, t.project, jobs=12)
            con = sqlite3.connect(str(db))
            con.row_factory = sqlite3.Row
            row = con.execute("SELECT * FROM v_latest_pattern_scan "
                              "WHERE ruler = 'name_check'").fetchone()
            con.close()
            self.assertIn("jobs", row.keys(),
                          "v_latest_pattern_scan must expose the new column -- "
                          "verify_pattern_scan_current.py reads the view, not "
                          "the table.")
            self.assertEqual(row["jobs"], 12)

    def test_a_row_written_without_jobs_is_NULL_not_a_default(self):
        """NULL must stay distinguishable from `-j 1`.

        A pre-v18 scan does not record its shard count.  If the column defaulted
        to anything, every historical row would start asserting a value nobody
        measured -- the exact failure `pattern_flags_scan_id` was added for.
        """
        with TempTree() as t:
            db = make_real_db(t.project / "decomp.db")
            con = sqlite3.connect(str(db))
            con.execute(
                "INSERT INTO pattern_scans (ruler, tool_version, project_dir,"
                " tree_verified, universe, examined) VALUES (?,?,?,?,?,?)",
                ("name_check", "objdiff-cli 0.0.0", str(t.project), 1, 1, 1))
            con.commit()
            got = con.execute("SELECT jobs FROM pattern_scans "
                              "ORDER BY id DESC LIMIT 1").fetchone()[0]
            con.close()
            self.assertIsNone(got)

    def test_migration_adds_jobs_to_a_v17_database(self):
        """A DB that already exists at v17 must gain the column, not error."""
        with TempTree() as t:
            db = make_real_db(t.project / "decomp.db")
            con = sqlite3.connect(str(db))
            # Asserted BEFORE the drop, so that a migration which never adds the
            # column fails HERE.  Without this the test reached `DROP COLUMN
            # jobs`, got "no such column", and took the skipTest branch -- a
            # skip, which ctest and pytest both score as not-a-failure.
            cols = {r[1] for r in con.execute("PRAGMA table_info(pattern_scans)")}
            self.assertIn("jobs", cols,
                          "migration v18 did not add the column at all")
            try:
                con.execute("ALTER TABLE pattern_scans DROP COLUMN jobs")
            except sqlite3.OperationalError as e:  # pragma: no cover
                self.skipTest(f"sqlite too old for DROP COLUMN: {e}")
            con.execute("UPDATE schema_version SET version = 17")
            con.commit()
            cols = {r[1] for r in con.execute("PRAGMA table_info(pattern_scans)")}
            self.assertNotIn("jobs", cols, "control: the column really was gone")

            db_mod._run_migrations(con, 17, db_mod.SCHEMA_VERSION)
            cols = {r[1] for r in con.execute("PRAGMA table_info(pattern_scans)")}
            con.close()
            self.assertIn("jobs", cols)


def _write_stub_scan(db: Path, project: Path, *, jobs: int) -> None:
    """Drive the real `write_scan()` with a minimal, sweep-free payload."""
    class Args:
        ruler = "name_check"
        notes = "unit test"
    Args.jobs = jobs
    res = {
        "objdiff_version": "objdiff-cli 4.2.8 (deadbeef, xxh3 0123456789abcdef)",
        "_coverage": {"examined": 0},
        "patterns_checked": ["WRONG_CALLEE"],
    }
    pattern_census.write_scan(db, Args(), res, [], {}, 0, 0, 1, project,
                              "2026-09-10T00:00:00+00:00")


class TempTree:
    """A throwaway directory that looks enough like a checkout to be a --project-dir."""

    def __enter__(self):
        import tempfile
        self._td = tempfile.TemporaryDirectory()
        self.project = Path(self._td.name).resolve()
        return self

    def __exit__(self, *exc):
        self._td.cleanup()
        return False


if __name__ == "__main__":
    unittest.main()
