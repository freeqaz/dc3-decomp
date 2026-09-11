"""A database created by `init_database()` must BE the version it claims.

DEFECT THIS PINS (measured 2026-09-11, before the fix):

    >>> init_database(tmp/"fresh.db").execute(
    ...     "SELECT version FROM schema_version").fetchone()[0]
    19
    missing tables/views vs a ladder-built v19:
        function_patterns, patch_queue, pattern_scan_examined,
        pattern_scan_units, pattern_scans, v_function_patterns,
        v_latest_pattern_scan
    missing functions columns: 36, including is_stub, verdict_reason,
        unicorn_verdict, pattern_flags_scan_id

`SCHEMA` is the v1 base shape; `init_database` stamped it `SCHEMA_VERSION` and
ran no migrations.  Because `_run_migrations` only fires for
`version < SCHEMA_VERSION`, the lie was self-sealing: nothing would ever repair
that database.  `query_functions(objdiff_pattern=...)` died on it with
"no such table: pattern_scans".

AND (defect C) two columns the LIVE decomp.db has were created by NO migration
and NO base CREATE -- `has_linker_merged` (from deleted 2026-03 meta-strategy
tooling; database.py has only ever read it) and `match_percent_normalized` (an
ad-hoc ALTER inside scripts/sync_match_percent.py).  Consequence, measured: the
v17 view `v_function_patterns` SELECTs `f.match_percent_normalized`, SQLite
resolves view columns lazily, so the view created fine and every query against
it failed with "no such column" on every database except the live one.

METHOD.  The expected shape is DERIVED, never hand-listed: build a second
database by running the same ladder from v1 explicitly, and diff `sqlite_master`
plus every table's `PRAGMA table_info`.  A hand list would rot at the next
migration and would have been written from the same wrong assumption that
produced the defect.  The derivation is itself controlled -- see
`test_the_expected_shape_is_not_degenerate`, without which an empty expectation
would make every comparison below pass.
"""

import sqlite3
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path

from scripts.orchestrator import database as db


def _ladder_built(path: Path) -> sqlite3.Connection:
    """A database built the way every EXISTING one was: v1 base + migrations.

    Deliberately does not call `init_database` -- that is the code under test.
    """
    conn = db.get_connection(path)
    conn.executescript(db.SCHEMA)
    conn.execute("INSERT INTO schema_version (version) VALUES (1)")
    conn.commit()
    with redirect_stdout(StringIO()):
        db._run_migrations(conn, 1, db.SCHEMA_VERSION)
    return conn


def _objects(conn: sqlite3.Connection) -> set[str]:
    return {r[0] for r in conn.execute(
        "SELECT name FROM sqlite_master WHERE type IN ('table','view','index') "
        "AND name NOT LIKE 'sqlite_%'")}


def _columns(conn: sqlite3.Connection, table: str) -> set[str]:
    return {r[1] for r in conn.execute(f"PRAGMA table_info({table})")}


class TestFreshDatabaseMatchesTheLadder(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)
        db._migrated_dbs.clear()
        with redirect_stdout(StringIO()):
            self.fresh = db.init_database(tmp / "fresh.db")
        self.ladder = _ladder_built(tmp / "ladder.db")

    # ------------------------------------------------------------- the control

    def test_the_expected_shape_is_not_degenerate(self):
        """NEGATIVE CONTROL for every comparison below.

        If `_ladder_built` silently produced an empty or v1-shaped database,
        every set difference would be empty and this file would pass while
        asserting nothing.  So assert the expectation contains objects that
        ONLY the ladder creates, and a column count far above the v1 base.
        """
        objs = _objects(self.ladder)
        for late in ("pattern_scans", "function_patterns", "pattern_scan_units",
                     "patch_queue", "v_function_patterns"):
            self.assertIn(late, objs,
                          "expectation is degenerate: the ladder-built DB is "
                          "missing an object only a migration creates")
        base_cols = {"id", "symbol", "demangled", "unit", "size",
                     "current_percent", "best_percent", "verdict"}
        ladder_cols = _columns(self.ladder, "functions")
        self.assertGreater(len(ladder_cols), len(base_cols) + 40,
                           "expectation is degenerate: functions has barely "
                           "more columns than the v1 base")

    # --------------------------------------------------------------- defect B

    def test_fresh_database_has_every_table_view_and_index(self):
        """SABOTAGE: restore `INSERT INTO schema_version VALUES (SCHEMA_VERSION)`
        with no `_run_migrations` call -> 7 objects go missing."""
        missing = _objects(self.ladder) - _objects(self.fresh)
        self.assertEqual(missing, set(),
                         f"fresh DB is missing {len(missing)} schema objects "
                         f"the migration ladder creates: {sorted(missing)}")

    def test_fresh_database_has_every_column_of_every_table(self):
        """SABOTAGE: as above -> 36 `functions` columns go missing."""
        tables = {r[0] for r in self.ladder.execute(
            "SELECT name FROM sqlite_master WHERE type='table' "
            "AND name NOT LIKE 'sqlite_%'")}
        problems = {}
        for t in sorted(tables):
            missing = _columns(self.ladder, t) - _columns(self.fresh, t)
            if missing:
                problems[t] = sorted(missing)
        self.assertEqual(problems, {},
                         f"fresh DB is missing columns: {problems}")

    def test_recorded_version_is_the_version_that_actually_ran(self):
        """The stamp must be earned.  SABOTAGE: stamp SCHEMA_VERSION without
        running the ladder -- caught by the two tests above, and this one pins
        that the stamp itself did not regress in the other direction."""
        v = self.fresh.execute("SELECT version FROM schema_version").fetchone()[0]
        self.assertEqual(v, db.SCHEMA_VERSION)

    def test_reopening_a_fresh_database_runs_no_migrations(self):
        """Idempotence: a second open must not re-run the ladder or fail.

        SABOTAGE: stamp 1 instead of SCHEMA_VERSION after the ladder -> the
        second open re-runs every migration (this test still passes, but the
        version assertion above goes red), or drop a duplicate-column guard in
        any migration -> this raises.
        """
        path = self.fresh.execute("PRAGMA database_list").fetchone()[2]
        db._migrated_dbs.clear()
        with redirect_stdout(StringIO()) as out:
            again = db.init_database(path)
        self.assertNotIn("Running database migrations", out.getvalue())
        self.assertEqual(
            again.execute("SELECT version FROM schema_version").fetchone()[0],
            db.SCHEMA_VERSION)

    # --------------------------------------------------------------- defect C

    def test_the_two_orphan_columns_are_in_the_ladder(self):
        """`has_linker_merged` and `match_percent_normalized` exist in the live
        decomp.db and were created by no migration and no base CREATE.

        Named explicitly rather than derived, because the derivation above
        cannot see them: they were missing from BOTH sides.

        SABOTAGE: delete the v20 migration block.
        """
        for col in ("has_linker_merged", "match_percent_normalized"):
            self.assertIn(col, _columns(self.fresh, "functions"),
                          f"{col} is in the live DB but no migration creates it")
            self.assertIn(col, _columns(self.ladder, "functions"))

    def test_v_function_patterns_is_queryable(self):
        """The v17 view SELECTs `f.match_percent_normalized`; SQLite resolves
        that lazily, so the defect only ever surfaced at query time.

        SABOTAGE: delete the v20 migration block -> OperationalError
        "no such column: f.match_percent_normalized".
        """
        for conn, label in ((self.fresh, "fresh"), (self.ladder, "ladder")):
            with self.subTest(db=label):
                conn.execute("SELECT * FROM v_function_patterns LIMIT 1").fetchall()
                conn.execute("SELECT * FROM v_latest_pattern_scan LIMIT 1").fetchall()

    def test_the_pattern_query_works_on_a_fresh_database(self):
        """End-to-end: the query CLAUDE.md tells lanes to use must reach its own
        refusal instead of dying on a missing table.

        Before the fix: OperationalError "no such table: pattern_scans".
        After: ValueError naming pattern_census.py -- the refusal that
        distinguishes "not measured" from "class is empty".

        SABOTAGE: restore the old `init_database` fresh-DB branch.
        """
        path = self.fresh.execute("PRAGMA database_list").fetchone()[2]
        with self.assertRaises(ValueError) as cm:
            db.query_functions(db_path=path, objdiff_pattern="WRONG_CALLEE")
        self.assertIn("pattern_census", str(cm.exception))


if __name__ == "__main__":
    unittest.main()
