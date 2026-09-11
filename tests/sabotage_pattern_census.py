#!/usr/bin/env python3
"""Mutation harness: break one thing at a time, assert the named test goes RED.

`tests/test_pattern_census_apply.py` carries two in-file negative controls
(`test_a_usable_database_passes_the_preflight`,
`test_the_work_markers_are_strings_the_tool_actually_prints`).  This is the
second layer: it proves those cases can actually FAIL, by editing the code under
test 21 different ways and asserting each named case reddens.  A mutation
nobody's test catches is reported as UNDETECTED and makes this exit non-zero --
including the final control, which re-runs the suite after every restore and
fails if the tree is not back to green.

Same role and same shape as `tests/sabotage_objs_patched.py`.  Restores in
memory rather than via `git checkout --`, so it is safe on a dirty tree; a
non-unique anchor is reported rather than applied.

M1, M10 and M13b are the incident itself: M1 removes the preflight call (the code
as it stood when a lane lost ~7.5 minutes to a refusal it could not see), M13b
moves it back below the sweep, and M10 turns the refusal into exit 0 (the way
the incident was reported).

Run:  python3 tests/sabotage_pattern_census.py     (~5 min)
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CENSUS = ROOT / "scripts" / "analysis" / "pattern_census.py"
DB = ROOT / "scripts" / "orchestrator" / "database.py"
TESTS = ROOT / "tests" / "test_pattern_census_apply.py"

BEFORE_WORK = [
    "test_apply_without_db_exits_6_before_any_work",
    "test_apply_against_the_worktree_tripwire_exits_6_and_names_the_path",
    "test_apply_to_a_missing_database_exits_6_and_does_not_create_it",
    "test_apply_to_a_database_owned_by_another_tree_exits_6",
    "test_apply_to_an_unwritable_database_exits_6",
]

# (label, [(file, old, new), ...], [tests that MUST fail])
# An empty must-fail list marks a mutation that is EXPECTED to survive, because
# another guard covers the same ground.  Those are printed as INFORMATIONAL and
# graded neither way -- a redundant guard is a fact worth recording, and quietly
# dropping such a case would leave the harness looking stronger than it is.
MUTATIONS = [
    ("M1  THE INCIDENT: preflight call removed from main()", [(CENSUS,
     "    if args.apply:\n        rc = preflight_apply(args, project_dir)\n        if rc:\n            return rc",
     "    if args.apply:\n        pass")],
     BEFORE_WORK + ["test_apply_with_limit_is_refused_before_any_work",
                    "test_preflight_is_reached_before_the_patch_check_and_the_universe"]),

    ("M2  preflight always returns 0 (guard disarmed)", [(CENSUS,
     "    # --- flag combinations that cannot produce a valid scan (cheap, exact) ---",
     "    return 0\n    # --- flag combinations that cannot produce a valid scan (cheap, exact) ---")],
     BEFORE_WORK + ["test_apply_with_limit_is_refused_before_any_work",
                    "test_apply_with_negative_control_is_refused_before_any_work",
                    "test_apply_with_no_patterns_is_refused_before_any_work"]),

    ("M3  a missing --db is accepted", [(CENSUS,
     '    if not db_arg:\n        raise ApplyDestinationError(',
     '    if False:\n        raise ApplyDestinationError(')],
     ["test_apply_without_db_exits_6_before_any_work"]),

    ("M4  the shadow/tripwire check alone is skipped", [(CENSUS,
     "        db_mod = _database_module()\n        db_mod.check_not_shadow_db(db_path)",
     "        db_mod = _database_module()")],
     []),   # survives: the SQLite-header check still convicts the tripwire

    ("M5  the SQLite-header check alone is skipped", [(CENSUS,
     '    if not head.startswith(b"SQLite format 3"):',
     '    if False:')],
     ["test_apply_against_the_worktree_tripwire_exits_6_and_names_the_path"]),
     # NOT informational, and the harness is why: this was FILED as expected-to-
     # survive on the reasoning that check_not_shadow_db would still convict a
     # tripwire.  It does not -- `shadow_target()` only fires for a path inside
     # a linked git worktree, and the fixture is a plain tempdir.  The run
     # reported it detected anyway, which is how the wrong assumption surfaced.
     # A harness that only confirms guesses is worth very little.

    ("M4+M5 BOTH tripwire checks skipped at once", [
     (CENSUS, "        db_mod = _database_module()\n        db_mod.check_not_shadow_db(db_path)",
              "        db_mod = _database_module()"),
     (CENSUS, '    if not head.startswith(b"SQLite format 3"):', '    if False:')],
     ["test_apply_against_the_worktree_tripwire_exits_6_and_names_the_path"]),

    ("M6  a missing database is created instead of refused", [(CENSUS,
     '    if not db_path.exists():\n        raise ApplyDestinationError(',
     '    if False:\n        raise ApplyDestinationError(')],
     ["test_apply_to_a_missing_database_exits_6_and_does_not_create_it"]),

    ("M7  the owning-tree check is skipped", [(CENSUS,
     "    if project_dir.resolve() != owner:", "    if False:")],
     ["test_apply_to_a_database_owned_by_another_tree_exits_6"]),

    ("M8  the write probe is removed", [(CENSUS,
     '            conn.execute("BEGIN IMMEDIATE")\n            conn.execute("ROLLBACK")',
     "            pass")],
     []),   # survives: WAL makes the SELECT itself fail on a read-only DB

    ("M9  the destination is never opened at all", [(CENSUS,
     "    try:\n        conn = sqlite3.connect(str(db_path), timeout=10.0)",
     "    if True:\n        return db_path\n    try:\n        conn = sqlite3.connect(str(db_path), timeout=10.0)")],
     ["test_apply_to_an_unwritable_database_exits_6"]),

    ("M10 the refusal exit code becomes 0 (the incident as reported)", [(CENSUS,
     "EXIT_APPLY_DESTINATION = 6", "EXIT_APPLY_DESTINATION = 0")],
     BEFORE_WORK),

    ("M11 the refusal stops naming the database path anywhere", [
     (CENSUS, 'f"  database tried : {self.db_path}\\n"',
              'f"  database tried : (somewhere)\\n"'),
     (CENSUS, 'f"`cat {db_path}` -- if it explains itself, it is the tripwire\\n"',
              'f"`cat` it -- if it explains itself, it is the tripwire\\n"')],
     ["test_apply_against_the_worktree_tripwire_exits_6_and_names_the_path"]),

    ("M12 --limit is no longer refused with --apply", [(CENSUS,
     "    if args.limit:\n        # `callee_gate.latest_scan()` takes the highest-id scan",
     "    if False:\n        # `callee_gate.latest_scan()` takes the highest-id scan")],
     ["test_apply_with_limit_is_refused_before_any_work"]),

    ("M13 preflight made unreachable (call site kept, condition killed)", [(CENSUS,
     "    if args.apply:\n        rc = preflight_apply(args, project_dir)",
     "    if args.apply and False:\n        rc = preflight_apply(args, project_dir)")],
     BEFORE_WORK),   # NOT the source-order test: the CALL is still textually
                     # first, and that test reads source order.  Recorded so the
                     # split of labour between the two kinds of test is explicit.

    ("M13b preflight MOVED below the patch check and the sweep", [(CENSUS,
     "    if args.apply:\n        rc = preflight_apply(args, project_dir)\n        if rc:\n            return rc\n\n    started =",
     "    started =")],
     ["test_preflight_is_reached_before_the_patch_check_and_the_universe"]),

    ("M14 write_scan hardcodes jobs=1", [(CENSUS,
     "         args.notes, started, args.jobs))",
     "         args.notes, started, 1))")],
     ["test_write_scan_records_the_jobs_value_it_was_given",
      "test_jobs_reaches_the_view_the_gate_reads"]),

    ("M15 the jobs column is dropped from the INSERT", [(CENSUS,
     '        " notes, started_at, jobs) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",',
     '        " notes, started_at) VALUES (?,?,?,?,?,?,?,?,?,?,?)",')],
     ["test_write_scan_records_the_jobs_value_it_was_given",
      "test_jobs_reaches_the_view_the_gate_reads"]),

    ("M16 migration v18 never adds the column", [(DB,
     '            conn.execute("ALTER TABLE pattern_scans ADD COLUMN jobs INTEGER")',
     "            pass")],
     ["test_write_scan_records_the_jobs_value_it_was_given",
      "test_jobs_reaches_the_view_the_gate_reads",
      "test_migration_adds_jobs_to_a_v17_database",
      "test_a_row_written_without_jobs_is_NULL_not_a_default"]),

    ("M17 jobs gets a DEFAULT, so NULL stops meaning \'unrecorded\'", [(DB,
     '            conn.execute("ALTER TABLE pattern_scans ADD COLUMN jobs INTEGER")',
     '            conn.execute("ALTER TABLE pattern_scans ADD COLUMN jobs INTEGER DEFAULT 1")')],
     ["test_a_row_written_without_jobs_is_NULL_not_a_default"]),

    ("M18 the view is not rebuilt after the ALTER", [(DB,
     '        conn.execute("DROP VIEW IF EXISTS v_latest_pattern_scan")\n        conn.execute("""\n            CREATE VIEW v_latest_pattern_scan AS\n            SELECT s.* FROM pattern_scans s\n            WHERE s.id = (SELECT MAX(s2.id) FROM pattern_scans s2\n                          WHERE s2.ruler = s.ruler)\n        """)\n\n    # Update schema version',
     "\n    # Update schema version")],
     []),   # survives: SQLite re-expands `SELECT s.*` at query time, so the
            # rebuild is defensive rather than load-bearing.  Measured, not
            # assumed -- which is why the mutation is kept.

    ("M19 a sweep banner is renamed (marker control)", [(CENSUS,
     'print(f"instrument (pre-scan) : {instrument_before}")',
     'print(f"instrument PRE SCAN : {instrument_before}")')],
     ["test_the_work_markers_are_strings_the_tool_actually_prints"]),

    ("M20 check_apply_destination refuses EVERYTHING (control control)", [(CENSUS,
     '    if not db_arg:\n        raise ApplyDestinationError(',
     '    if True:\n        raise ApplyDestinationError(\n            db_arg, "sabotage")\n    if not db_arg:\n        raise ApplyDestinationError(')],
     ["test_a_usable_database_passes_the_preflight"]),
]


def failing_tests():
    p = subprocess.run(
        [sys.executable, "-m", "pytest", str(TESTS), "-q", "--no-header",
         "-p", "no:cacheprovider", "--tb=no"],
        cwd=str(ROOT), capture_output=True, text=True)
    out = p.stdout + p.stderr
    fails = set(re.findall(r"^(?:FAILED|ERROR)(?:\([^)]*\))? [^ ]*::(\S+)", out, re.M))
    return fails, p.returncode, out


def main() -> int:
    base_fails, base_rc, base_out = failing_tests()
    if base_rc != 0 or base_fails:
        print("BASELINE IS NOT GREEN -- refusing to interpret any mutation.")
        print(base_out[-3000:])
        return 1
    print("baseline: green\n")

    bad, informational = [], []
    for label, edits, must_fail in MUTATIONS:
        # `edits` is a list of (file, old, new).  Several of these defects are
        # only real when TWO guards are removed at once -- a single-anchor
        # harness reports such a mutation as "survives", which reads exactly
        # like "the test cannot catch it".
        originals = {}
        broken = None
        for path, old, _new in edits:
            originals.setdefault(path, path.read_text())
            if originals[path].count(old) != 1:
                broken = (f"anchor appears {originals[path].count(old)} "
                          f"times in {path.name}")
                break
        if broken:
            print(f"[{label}] SKIP-ERROR: {broken}")
            bad.append((label, broken))
            continue
        for path, old, new in edits:
            path.write_text(path.read_text().replace(old, new, 1))
        try:
            fails, rc, out = failing_tests()
        finally:
            for path, text in originals.items():
                path.write_text(text)
        if not must_fail:
            # Deliberately-survivable mutations: recorded, never counted as a
            # pass or a failure.  Each one is a fact about redundant coverage,
            # and hiding it would be the "test that cannot fail" defect again.
            informational.append((label, sorted(f.split("::")[-1] for f in fails)))
            print(f"[   ] {label}\n        -> INFORMATIONAL, survives by design; "
                  f"{len(fails)} failing")
            continue
        missed = [t for t in must_fail
                  if not any(t in f for f in fails)]
        status = "OK " if (rc != 0 and not missed) else "BAD"
        if status == "BAD":
            bad.append((label, f"rc={rc} missed={missed}"))
        print(f"[{status}] {label}\n"
              f"        -> {len(fails)} failing: "
              f"{', '.join(sorted(f.split('::')[-1] for f in fails)[:4])}"
              f"{' ...' if len(fails) > 4 else ''}")

    fails, rc, out = failing_tests()
    print(f"\nafter restore: rc={rc}, failing={sorted(fails)}")
    if rc != 0:
        bad.append(("RESTORE", "tree not green after restore"))

    graded = [m for m in MUTATIONS if m[2]]
    print(f"\n{len(graded) - len([b for b in bad if b[0] != 'RESTORE'])}"
          f"/{len(graded)} graded mutations detected "
          f"({len(informational)} informational)")
    for label, seen in informational:
        print(f"  SURVIVES BY DESIGN: {label} -> {seen or 'nothing failed'}")
    for label, why in bad:
        print(f"  UNDETECTED/BROKEN: {label}: {why}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
