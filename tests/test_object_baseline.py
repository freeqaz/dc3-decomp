#!/usr/bin/env python3
"""Tests for the per-unit object baselines (schema v18).

The property under test: a pattern scan's findings are about 2,224 object PAIRS,
and the gate must be able to say WHICH units' objects have moved since, and in
which DIRECTION -- target (a `symbols.txt`/split change) or base (a landed
source commit).

Every case carries its negative control inside itself, per this repo's rule that
a guard nobody has watched fail is not a guard.  Two shapes of vacuity are
guarded against specifically:

  * a checker that refuses everything would satisfy every RED assertion, so each
    test also asserts GREEN on the untouched fixture;
  * a checker that named the wrong unit, or the wrong direction, would satisfy a
    bare `raises`, so each RED assertion pins the unit NAME and the DIRECTION.

`tests/sabotage_object_baseline.py` is the second layer: it edits the code under
test one defect at a time and asserts the named case here reddens.

Run:  python3 -m pytest tests/test_object_baseline.py -q
"""
from __future__ import annotations

import json
import sqlite3
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from orchestrator import object_baseline  # noqa: E402
from orchestrator.callee_gate import (  # noqa: E402
    StaleBaseObjectsError, StalePatternScanError, StaleTargetObjectsError,
    UnfingerprintedPatternScanError, ensure_current_scan, installed_objdiff_version,
    raced_units, stale_units)

VERIFY = REPO_ROOT / "scripts" / "verify_pattern_scan_current.py"

UNITS = ("default/alpha", "default/beta", "default/gamma")


# --------------------------------------------------------------------------
# fixture: a tree of objdiff.json + objects, and a DB whose scan measured them
# --------------------------------------------------------------------------

def _rels(unit: str) -> dict[str, str]:
    leaf = unit.split("/")[-1]
    return {"target": f"build/373307D9/obj/{leaf}.obj",
            "base": f"build/373307D9/src/{leaf}.obj"}


def write_tree(root: Path, *, units=UNITS, marker: str = "v1",
               base_missing: tuple[str, ...] = ()) -> None:
    """objdiff.json plus one target and one base object per unit.

    `base_missing` leaves a unit with no base object at all -- 1,234 of the real
    tree's 2,224 units are in that state (target-only library units), and
    "absent then, absent now" must compare EQUAL or the guard is red on more than
    half the binary forever.
    """
    cfg: dict = {"units": []}
    for unit in units:
        rels = _rels(unit)
        for side, rel in rels.items():
            if side == "base" and unit in base_missing:
                continue
            p = root / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(f"{unit}:{side}:{marker}".encode())
        cfg["units"].append({"name": unit, "target_path": rels["target"],
                             "base_path": rels["base"]})
    (root / "objdiff.json").write_text(json.dumps(cfg, indent=1))


SCHEMA = """
CREATE TABLE functions (id INTEGER PRIMARY KEY, symbol TEXT, demangled TEXT,
                        unit TEXT, size INTEGER, current_percent REAL,
                        best_percent REAL, verdict TEXT, verdict_reason TEXT,
                        locked_by TEXT, attempt_count INTEGER, is_stub INTEGER,
                        unicorn_verdict TEXT, unicorn_class TEXT,
                        unicorn_harness_version INTEGER,
                        match_percent_normalized REAL);
CREATE TABLE pattern_scans (id INTEGER PRIMARY KEY, ruler TEXT NOT NULL,
                        tool_version TEXT NOT NULL, project_dir TEXT NOT NULL,
                        build_rev TEXT, tree_verified INTEGER NOT NULL DEFAULT 0,
                        universe INTEGER NOT NULL, examined INTEGER NOT NULL,
                        coverage_json TEXT, patterns_checked TEXT, notes TEXT,
                        started_at TIMESTAMP, finished_at TIMESTAMP);
CREATE TABLE function_patterns (scan_id INTEGER NOT NULL, function_id INTEGER NOT NULL,
                        pattern TEXT NOT NULL, confidence TEXT, fixability TEXT,
                        instruction_count INTEGER, details TEXT,
                        PRIMARY KEY (scan_id, function_id, pattern)) WITHOUT ROWID;
CREATE TABLE pattern_scan_examined (scan_id INTEGER NOT NULL,
                        function_id INTEGER NOT NULL,
                        PRIMARY KEY (scan_id, function_id)) WITHOUT ROWID;
CREATE TABLE pattern_scan_units (scan_id INTEGER NOT NULL, unit TEXT NOT NULL,
                        target_sha256 TEXT, base_sha256 TEXT,
                        raced INTEGER NOT NULL DEFAULT 0,
                        PRIMARY KEY (scan_id, unit)) WITHOUT ROWID;
CREATE VIEW v_latest_pattern_scan AS
    SELECT s.* FROM pattern_scans s
     WHERE s.id = (SELECT MAX(s2.id) FROM pattern_scans s2 WHERE s2.ruler = s.ruler);
"""


def make_scan(tree: Path, *, tool_version: str | None = None,
              record_units: bool = True, raced: tuple[str, ...] = ()) -> Path:
    """A DB in `tree` whose one scan measured `tree`'s objects.  Returns its path."""
    db = tree / "decomp.db"
    con = sqlite3.connect(db)
    con.executescript(SCHEMA)
    con.execute(
        "INSERT INTO pattern_scans (id, ruler, tool_version, project_dir, build_rev,"
        " tree_verified, universe, examined, finished_at) VALUES (1,?,?,?,?,1,3,3,?)",
        ("name_check", tool_version or installed_objdiff_version(REPO_ROOT),
         str(tree.resolve()), "abc1234", "2026-09-10 00:00:00"))
    if record_units:
        for unit, h in object_baseline.fingerprint_units(tree).items():
            con.execute("INSERT INTO pattern_scan_units VALUES (1,?,?,?,?)",
                        (unit, h["target"], h["base"], 1 if unit in raced else 0))
    con.commit()
    con.close()
    return db


@pytest.fixture
def tree(tmp_path: Path) -> Path:
    root = tmp_path / "main"
    root.mkdir()
    (root / ".git").mkdir()
    write_tree(root)
    return root


def _mutate(tree: Path, unit: str, side: str) -> None:
    """Rewrite one object -- the whole sabotage, in one line."""
    p = tree / _rels(unit)[side]
    p.write_bytes(p.read_bytes() + b"-moved")


def _check(db: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(VERIFY), "--db", str(db), "--check",
         "--repo-root", str(REPO_ROOT), *args],
        capture_output=True, text=True)


# --------------------------------------------------------------------------
# (d) nothing moved -- the control every other case leans on
# --------------------------------------------------------------------------

def test_an_untouched_tree_is_current_in_both_directions(tree: Path):
    """The negative control for the whole file.

    If this ever goes red the other three cases prove nothing: a checker that
    called every tree stale would pass all of them.

    SABOTAGE: make `compare_fingerprints` return a non-empty dict
    unconditionally.  This goes red.
    """
    db = make_scan(tree)
    con = sqlite3.connect(db)
    assert stale_units(con, 1, project_dir=tree) == {}
    assert ensure_current_scan(con, repo_root=REPO_ROOT)["id"] == 1
    con.close()
    r = _check(db)
    assert r.returncode == 0, r.stdout + r.stderr


def test_a_unit_with_no_base_object_is_not_stale(tmp_path: Path):
    """Absent-then, absent-now must compare EQUAL, or 1,234 units are red forever.

    SABOTAGE: treat a NULL recorded hash as a mismatch (e.g. compare with `is not
    None` semantics).  This goes red while the mutation control below stays
    green, so it cannot be satisfied by a checker that ignores the base side.
    """
    root = tmp_path / "main"
    root.mkdir()
    (root / ".git").mkdir()
    write_tree(root, base_missing=("default/gamma",))
    db = make_scan(root)
    con = sqlite3.connect(db)
    assert stale_units(con, 1, project_dir=root) == {}

    # CONTROL: the same unit's TARGET object still participates.
    _mutate(root, "default/gamma", "target")
    assert stale_units(con, 1, project_dir=root) == {"default/gamma": "target"}
    con.close()


# --------------------------------------------------------------------------
# (a) the target side
# --------------------------------------------------------------------------

def test_a_moved_target_object_is_named_with_direction_target(tree: Path):
    """`dtk xex split` rewrote a target object: exit 4, the unit named.

    SABOTAGE: drop "target" from `object_baseline.SIDES`, or stop calling
    `check_scan_objects` from `ensure_current_scan`.  Both turn this red.

    The direction is pinned, not just the raising: a guard that reported every
    change as base-side would satisfy a bare `pytest.raises`.
    """
    db = make_scan(tree)
    _mutate(tree, "default/beta", "target")

    con = sqlite3.connect(db)
    assert stale_units(con, 1, project_dir=tree) == {"default/beta": "target"}
    with pytest.raises(StaleTargetObjectsError) as e:
        ensure_current_scan(con, repo_root=REPO_ROOT)
    con.close()
    assert "default/beta" in str(e.value)
    assert e.value.stale == {"default/beta": "target"}
    # it must stay catchable as staleness -- sync_objdiff catches that type
    assert isinstance(e.value, StalePatternScanError)

    r = _check(db)
    assert r.returncode == 4, r.stdout + r.stderr
    assert "STALE TARGET OBJECTS" in r.stderr
    assert "default/beta" in r.stderr
    assert "1 unit(s)" in r.stderr and "1 on the TARGET side" in r.stderr
    # the OTHER units must not be swept in -- a whole-tree verdict is the design
    # this replaces
    assert "default/alpha" not in r.stderr


# --------------------------------------------------------------------------
# (b) the base side
# --------------------------------------------------------------------------

def test_a_rebuilt_base_object_is_named_with_direction_base(tree: Path):
    """A landed source commit rebuilt a base object: exit 5, the unit named.

    This is the ordinary steady state of an active repo, and it is a DIFFERENT
    exit code from the target side on purpose: the two have different causes and
    different remedies.

    SABOTAGE: drop "base" from `object_baseline.SIDES`, or make
    `check_scan_objects` raise `StaleTargetObjectsError` for every direction.
    Both turn this red; the target case above is the control that keeps a
    "raise base for everything" fix from passing.
    """
    db = make_scan(tree)
    _mutate(tree, "default/alpha", "base")

    con = sqlite3.connect(db)
    assert stale_units(con, 1, project_dir=tree) == {"default/alpha": "base"}
    with pytest.raises(StaleBaseObjectsError) as e:
        ensure_current_scan(con, repo_root=REPO_ROOT)
    con.close()
    assert "default/alpha" in str(e.value)
    assert not isinstance(e.value, StaleTargetObjectsError)

    r = _check(db)
    assert r.returncode == 5, r.stdout + r.stderr
    assert "STALE BASE OBJECTS" in r.stderr
    assert "default/alpha" in r.stderr
    assert "1 on the BASE side" in r.stderr


# --------------------------------------------------------------------------
# (c) both
# --------------------------------------------------------------------------

def test_both_sides_moving_is_reported_as_both_and_exits_on_the_target_code(
        tree: Path):
    """Both directions at once: direction `both`, and both counts in the message.

    Exit 4 takes precedence -- a target-side change means the baseline the whole
    diff is measured against was rewritten, which subsumes "and we also
    recompiled".  The message still has to name the base-side count, or the
    precedence rule silently hides half the finding.

    SABOTAGE: report only the first side found (return after the target check),
    or drop the base-side count from the message.  This goes red; the two
    single-direction tests above stay green, so the precedence cannot be
    satisfied by collapsing all three cases into one.
    """
    db = make_scan(tree)
    _mutate(tree, "default/gamma", "target")
    _mutate(tree, "default/gamma", "base")
    _mutate(tree, "default/alpha", "base")

    con = sqlite3.connect(db)
    assert stale_units(con, 1, project_dir=tree) == {
        "default/gamma": "both", "default/alpha": "base"}
    with pytest.raises(StaleTargetObjectsError) as e:
        ensure_current_scan(con, repo_root=REPO_ROOT)
    con.close()
    assert e.value.stale == {"default/gamma": "both", "default/alpha": "base"}

    r = _check(db)
    assert r.returncode == 4, r.stdout + r.stderr
    assert "2 unit(s)" in r.stderr
    assert "1 on the TARGET side" in r.stderr
    assert "2 on the BASE side" in r.stderr      # `both` counts on both sides
    assert "1 base, 1 both" in r.stderr          # the per-direction breakdown


# --------------------------------------------------------------------------
# per-unit scoping: the property that makes the guard usable at all
# --------------------------------------------------------------------------

def test_a_caller_can_ask_about_one_unit_while_the_tree_churns(tree: Path):
    """`--unit X` answers about X, and says how much else moved.

    A tree-wide verdict on a repo several lanes land into is permanently red and
    is therefore routed around -- this is the property that stops that.

    SABOTAGE: ignore the `units=` argument in `check_scan_objects`.  The first
    half goes red.  The second half is the control: the scoped check must still
    REFUSE when the named unit is the one that moved.
    """
    db = make_scan(tree)
    _mutate(tree, "default/alpha", "base")

    r = _check(db, "--unit", "default/beta")
    assert r.returncode == 0, r.stdout + r.stderr

    r = _check(db, "--unit", "default/alpha")
    assert r.returncode == 5, r.stdout + r.stderr
    assert "default/alpha" in r.stderr
    # scoping must not hide the rest of the tree
    assert "1 unit(s) in the whole tree have moved" in r.stderr


def test_the_stale_list_is_truncated_but_the_count_never_is(tmp_path: Path):
    """A truncated list must never read as a total.

    SABOTAGE: print only `len(listed[:limit])` as the count.  This goes red.
    """
    root = tmp_path / "main"
    root.mkdir()
    (root / ".git").mkdir()
    many = tuple(f"default/u{i:02d}" for i in range(12))
    write_tree(root, units=many)
    db = make_scan(root)
    for unit in many:
        _mutate(root, unit, "base")

    r = _check(db, "--list-stale", "3")
    assert r.returncode == 5, r.stdout + r.stderr
    assert "12 unit(s)" in r.stderr              # the summary line's count
    # ...and the LIST's own header count, which is the one that can lie: it sits
    # directly above a truncated list, where "3" would read as the total.
    assert "12 of the scan's units have moved" in r.stderr
    assert "... and 9 more" in r.stderr
    assert r.stderr.count("default/u") <= 6      # 3 listed, not 12


# --------------------------------------------------------------------------
# a scan with NO baseline: "cannot say" is a refusal, not a pass
# --------------------------------------------------------------------------

def test_a_scan_with_no_recorded_baseline_is_refused_not_believed(tree: Path):
    """Every pre-v18 scan is of this shape and none of them can be backfilled.

    SABOTAGE: `return {}` instead of raising when `recorded_units` is empty --
    which reads as "no units are stale", i.e. green.  The first half goes red.
    The control is the same fixture WITH a baseline.
    """
    db = make_scan(tree, record_units=False)
    con = sqlite3.connect(db)
    with pytest.raises(UnfingerprintedPatternScanError, match="NO per-unit object"):
        stale_units(con, 1, project_dir=tree)
    with pytest.raises(UnfingerprintedPatternScanError):
        ensure_current_scan(con, repo_root=REPO_ROOT)
    con.close()
    r = _check(db)
    assert r.returncode == 1, r.stdout + r.stderr
    assert "STALE PATTERN SCAN" in r.stderr

    # NEGATIVE CONTROL: identical fixture, baseline recorded -> green
    (tree / "decomp.db").unlink()
    db2 = make_scan(tree)            # same fixture, baseline recorded
    assert _check(db2).returncode == 0


def test_the_object_check_can_be_switched_off_and_says_so(tree: Path):
    """`--no-check-objects` must be visibly a narrower question, not a pass.

    The pre-v18 behaviour still has a use ("was this scan taken by the right
    binary?"), but a run that skipped the object check and printed a bare "is
    current" would be the old lie with a new flag.

    SABOTAGE: drop the "NOT CHECKED" line from the describe output.  The last
    assertion goes red.
    """
    db = make_scan(tree)
    _mutate(tree, "default/beta", "target")
    assert _check(db).returncode == 4
    assert _check(db, "--no-check-objects").returncode == 0

    r = subprocess.run([sys.executable, str(VERIFY), "--db", str(db),
                        "--repo-root", str(REPO_ROOT), "--no-check-objects"],
                       capture_output=True, text=True)
    assert r.returncode == 0
    assert "NOT CHECKED" in r.stdout


# --------------------------------------------------------------------------
# raced: measured, and not laundered into either verdict
# --------------------------------------------------------------------------

def test_a_raced_unit_is_recorded_and_reported_separately_from_staleness(
        tree: Path):
    """`raced` is un-attributability, not staleness, and is reported as such.

    SABOTAGE: write `raced = 0` unconditionally in `pattern_census.write_scan`,
    or fold raced units into the stale set.  This goes red.
    """
    db = make_scan(tree, raced=("default/beta",))
    con = sqlite3.connect(db)
    assert raced_units(con, 1) == ["default/beta"]
    # a raced unit whose objects have NOT since moved is not stale
    assert stale_units(con, 1, project_dir=tree) == {}
    con.close()

    r = subprocess.run([sys.executable, str(VERIFY), "--db", str(db),
                        "--repo-root", str(REPO_ROOT)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "1 raced DURING the scan" in r.stdout
    assert "default/beta" in r.stdout


def test_census_write_scan_records_the_baseline_it_measured(tmp_path: Path):
    """The WRITE side: `pattern_census.write_scan` must store hashes and races.

    The read-side tests above are all fed by a hand-built fixture, so without
    this one the census could write nothing at all and every one of them would
    still pass -- the exact shape of a guard that is green because it is never
    reached.

    `ruler="all"` on purpose: the `name_check` path additionally refreshes the
    legacy `has_*` columns, which this minimal fixture does not carry, and that
    is a different subsystem.

    SABOTAGE: write `raced = 0` unconditionally, or drop the
    `pattern_scan_units` executemany.  This goes red.
    """
    import argparse
    sys.path.insert(0, str(REPO_ROOT))
    from scripts.analysis import pattern_census

    root = tmp_path / "main"
    root.mkdir()
    write_tree(root)
    db = root / "decomp.db"
    con = sqlite3.connect(db)
    con.executescript(SCHEMA)
    con.execute("CREATE TABLE schema_version (version INTEGER)")
    con.execute("INSERT INTO schema_version VALUES (18)")
    con.commit()
    con.close()

    measured = object_baseline.fingerprint_units(root)
    args = argparse.Namespace(ruler="all", notes="fixture", db=str(db))
    res = {"objdiff_version": "objdiff-cli 4.2.8 (deadbeef, xxh3 0123456789abcdef)",
           "_coverage": {"examined": 0}, "patterns_checked": []}
    pattern_census.write_scan(db, args, res, [], {}, 3, 3, 1, root,
                              "2026-09-10T00:00:00+00:00",
                              measured, {"default/beta"})

    con = sqlite3.connect(db)
    rows = {u: (t, b, r) for u, t, b, r in con.execute(
        "SELECT unit, target_sha256, base_sha256, raced FROM pattern_scan_units")}
    con.close()
    assert set(rows) == set(measured)
    for unit, h in measured.items():
        assert rows[unit][:2] == (h["target"], h["base"])
    assert rows["default/beta"][2] == 1
    assert rows["default/alpha"][2] == 0


def test_race_units_compares_both_reads(tmp_path: Path):
    """The census's before/after bracket must notice a mid-sweep rewrite.

    SABOTAGE: make `race_units` return an empty set.  This goes red; the
    unchanged control keeps it from being satisfied by "everything raced".
    """
    root = tmp_path / "main"
    root.mkdir()
    write_tree(root)
    before = object_baseline.fingerprint_units(root)
    assert object_baseline.race_units(before, before) == set()

    _mutate(root, "default/beta", "base")
    after = object_baseline.fingerprint_units(root)
    assert object_baseline.race_units(before, after) == {"default/beta"}


# --------------------------------------------------------------------------
# set differences, and the refusal to fingerprint nothing
# --------------------------------------------------------------------------

def test_added_and_removed_units_are_named_rather_than_ignored(tree: Path):
    """A unit in one list and not the other cannot be vouched for either way.

    SABOTAGE: iterate only over the recorded units (`for unit in recorded`).
    The `unrecorded` half goes red -- and that is the dangerous direction, since
    a source file added since the scan is a unit the scan never looked at.
    """
    db = make_scan(tree)
    write_tree(tree, units=(*UNITS, "default/delta"))   # a new unit appears
    con = sqlite3.connect(db)
    assert stale_units(con, 1, project_dir=tree) == {"default/delta": "unrecorded"}

    write_tree(tree, units=UNITS[:2])                   # and one disappears
    assert stale_units(con, 1, project_dir=tree) == {"default/gamma": "removed"}
    con.close()


def test_an_empty_unit_list_is_refused_rather_than_matching_everything(
        tmp_path: Path):
    """A baseline of zero units would match every future tree, forever.

    SABOTAGE: delete the `if not units:` refusal in `unit_paths`.  This goes red.
    """
    root = tmp_path / "empty"
    root.mkdir()
    (root / "objdiff.json").write_text(json.dumps({"units": []}))
    with pytest.raises(object_baseline.ObjectBaselineError, match="ZERO units"):
        object_baseline.unit_paths(root)


# --------------------------------------------------------------------------
# the consumer: query_functions must not serve a stale row as current
# --------------------------------------------------------------------------

def test_query_functions_flags_rows_whose_unit_moved(tree: Path):
    """`objdiff_pattern=` rows carry their unit's currency, or they are a claim.

    SABOTAGE: drop the `_attach_unit_currency` call in `query_functions`.  The
    flagged half goes red.  The control is the untouched tree, where the same
    query must flag None -- so a function that marked everything stale fails too.
    """
    from orchestrator.database import query_functions

    db = make_scan(tree)
    con = sqlite3.connect(db)
    con.executemany(
        "INSERT INTO functions (id, symbol, demangled, unit, current_percent,"
        " attempt_count) VALUES (?,?,?,?,?,0)",
        [(1, "?A@@QAAXXZ", "A::A(void)", "default/alpha", 90.0),
         (2, "?B@@QAAXXZ", "B::B(void)", "default/beta", 91.0)])
    con.executemany("INSERT INTO function_patterns (scan_id, function_id, pattern)"
                    " VALUES (1,?, 'WRONG_CALLEE')", [(1,), (2,)])
    con.commit()
    con.close()

    # `_attach_unit_currency` fingerprints the module's OWN project root (the
    # MCP server's tree), so point it at the fixture for the length of the test.
    import orchestrator.database as dbmod
    real = dbmod._attach_unit_currency

    def patched(conn, scan_id, rows, *, drop_stale):
        recorded = {u: {"target": t, "base": b} for u, t, b in conn.execute(
            "SELECT unit, target_sha256, base_sha256 FROM pattern_scan_units "
            "WHERE scan_id = ?", (scan_id,))}
        stale = object_baseline.compare_fingerprints(
            recorded, object_baseline.fingerprint_units(tree))
        for r in rows:
            r["unit_objects_stale"] = stale.get(r.get("unit"))
        return [r for r in rows if not r["unit_objects_stale"]] if drop_stale else rows

    dbmod._attach_unit_currency = patched
    try:
        rows = query_functions(db_path=db, objdiff_pattern="WRONG_CALLEE",
                               exclude_complete=False)
        assert {r["symbol"]: r["unit_objects_stale"] for r in rows} == {
            "?A@@QAAXXZ": None, "?B@@QAAXXZ": None}

        _mutate(tree, "default/beta", "base")
        rows = query_functions(db_path=db, objdiff_pattern="WRONG_CALLEE",
                               exclude_complete=False)
        assert {r["symbol"]: r["unit_objects_stale"] for r in rows} == {
            "?A@@QAAXXZ": None, "?B@@QAAXXZ": "base"}

        # 'exclude' is opt-in precisely because a shrunken set reads like an
        # exhausted class; when asked for, it must actually drop the row.
        rows = query_functions(db_path=db, objdiff_pattern="WRONG_CALLEE",
                               exclude_complete=False, stale_units="exclude")
        assert [r["symbol"] for r in rows] == ["?A@@QAAXXZ"]
    finally:
        dbmod._attach_unit_currency = real


def test_query_functions_flags_a_pre_v18_scan_rather_than_reporting_clean(
        tree: Path):
    """No baseline must not read as an unflagged row.

    SABOTAGE: leave `unit_objects_stale` unset when the table is empty.  This
    goes red -- and this is the state EVERY existing scan is in, so a silent
    None here would have been the whole subsystem's blind spot re-opened.
    """
    from orchestrator.database import query_functions

    db = make_scan(tree, record_units=False)
    con = sqlite3.connect(db)
    con.execute("INSERT INTO functions (id, symbol, demangled, unit, "
                "current_percent, attempt_count) "
                "VALUES (1,'?A@@QAAXXZ','A::A(void)','default/alpha',90.0,0)")
    con.execute("INSERT INTO function_patterns (scan_id, function_id, pattern)"
                " VALUES (1,1,'WRONG_CALLEE')")
    con.commit()
    con.close()

    rows = query_functions(db_path=db, objdiff_pattern="WRONG_CALLEE",
                           exclude_complete=False)
    assert [r["unit_objects_stale"] for r in rows] == ["unfingerprinted"]


if __name__ == "__main__":
    raise SystemExit(subprocess.call(
        [sys.executable, "-m", "pytest", str(Path(__file__)), "-q"]))
