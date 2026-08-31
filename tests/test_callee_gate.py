#!/usr/bin/env python3
"""Tests for the auto-AT_LIMIT callee gate (scripts/orchestrator/callee_gate.py).

Every test here carries its NEGATIVE CONTROL INSIDE ITSELF -- the same assertion
run against a state where the property should NOT hold.  Two traps this repo hit
in the week these were written:

  * a sabotage went red on a *warning message* rather than on the absence of an
    exception, so the "does not raise" assertion was equally true with and
    without the bug;
  * a lane's test was vacuous because the broken state produced the same
    evidence the test asserted on.

So a provenance test never just asserts "raises": it asserts that the SAME
fixture with the version corrected does NOT raise and returns the scan.  A
judging test never just asserts "blocked": it asserts a second, known-different
row is cleared by the same call.

Each test's docstring names the sabotage that must turn it red.
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

from orchestrator.callee_gate import (  # noqa: E402
    CALLEE_PATTERNS, LinkerMapError, StalePatternScanError, build_callee_gate,
    classify_pair, ensure_current_scan, installed_objdiff_version,
    load_linker_map)

REAL_DB = Path("/home/free/code/milohax/dc3-decomp/decomp.db")

#: the row the 4.2.6-scan-read-by-a-4.2.7-binary hole hid: byte-identical but
#: for one relocation naming a function at a DIFFERENT address.  172 B, and it
#: was sitting at COMPLETE with reason "auto: all mismatches unfixable".
FIXABLE_CONTROL = "?Copy@FxSend@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z"
#: the genuine refusal-class row: our side calls a synthesised merged_* stub,
#: which is config work in symbols.txt and no source edit can move.
UNFIXABLE_CONTROL = "?DoVelocity@NgPostProc@@IAAXXZ"


# --------------------------------------------------------------------------
# fixtures: a self-contained DB and linker map, so the logic tests do not
# depend on whatever state decomp.db and the shared objdiff binary are in.
# --------------------------------------------------------------------------

_MAP_ROWS = [
    # name                                       address
    ("?Left@A@@QAAXXZ",                          "82001000"),
    ("?Right@B@@QAAXXZ",                         "82001000"),   # folded with Left
    ("?Elsewhere@C@@QAAXXZ",                     "82002000"),
    ("?OnlyInTarget@D@@QAAXXZ",                  "82003000"),
    ("?Twice@E@@QAAXXZ",                         "82004000"),
    ("?Twice@E@@QAAXXZ",                         "82005000"),   # ambiguous
]


def _write_map(root: Path) -> Path:
    p = root / "orig" / "373307D9" / "ham_xbox_r.map"
    p.parent.mkdir(parents=True, exist_ok=True)
    lines = [" Address         Publics by Value              Rva+Base", ""]
    for name, addr in _MAP_ROWS:
        lines.append(f" 0001:00012345       {name}           {addr}     f   fake.obj")
    p.write_text("\n".join(lines) + "\n")
    return p


def _make_db(path: Path, *, tool_version: str, tree_verified: int = 1,
             ruler: str = "name_check", project_dir: str | None = None,
             patterns: list[tuple[str, str, str, str | None]] = ()) -> None:
    """A minimal v17-shaped DB.  `patterns` = (symbol, pattern, fixability, details).

    `project_dir` defaults to the DB's OWN directory, which is the state
    `check_scan_tree` requires: a scan describes the tree that owns the database
    it lands in.  Pass a different path to build the unmoored fixture.
    """
    project_dir = project_dir or str(Path(path).resolve().parent)
    con = sqlite3.connect(path)
    con.executescript("""
        CREATE TABLE functions (id INTEGER PRIMARY KEY, symbol TEXT, demangled TEXT,
                                unit TEXT, size INTEGER,
                                match_percent_normalized REAL, verdict TEXT);
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
        CREATE VIEW v_latest_pattern_scan AS
            SELECT s.* FROM pattern_scans s
             WHERE s.id = (SELECT MAX(s2.id) FROM pattern_scans s2 WHERE s2.ruler = s.ruler);
    """)
    if tool_version is not None:
        con.execute(
            "INSERT INTO pattern_scans (id, ruler, tool_version, project_dir, build_rev,"
            " tree_verified, universe, examined, finished_at) VALUES (1,?,?,?,?,?,?,?,?)",
            (ruler, tool_version, project_dir, "abc1234", tree_verified,
             10, 10, "2026-08-22 00:00:00"))
    for i, (symbol, pattern, fixability, details) in enumerate(patterns, start=1):
        con.execute("INSERT OR IGNORE INTO functions (id, symbol, unit) VALUES (?,?,?)",
                    (i, symbol, "default/fake"))
        con.execute("SELECT id FROM functions WHERE symbol = ?", (symbol,))
    con.commit()
    # re-resolve ids (a symbol may appear on several pattern rows)
    ids = {s: i for i, s in con.execute("SELECT id, symbol FROM functions")}
    for symbol, pattern, fixability, details in patterns:
        if symbol not in ids:
            con.execute("INSERT INTO functions (symbol, unit) VALUES (?, ?)",
                        (symbol, "default/fake"))
            ids[symbol] = con.execute("SELECT last_insert_rowid()").fetchone()[0]
        con.execute("INSERT OR REPLACE INTO function_patterns"
                    " (scan_id, function_id, pattern, fixability, details)"
                    " VALUES (1, ?, ?, ?, ?)",
                    (ids[symbol], pattern, fixability, details))
    con.commit()
    con.close()


def _callees(*pairs: tuple[str, str]) -> str:
    return json.dumps({"divergent_callees": [
        {"target_symbol": t, "base_symbol": b, "count": 1} for t, b in pairs]})


@pytest.fixture
def fake_tree(tmp_path: Path) -> Path:
    _write_map(tmp_path)
    return tmp_path


# --------------------------------------------------------------------------
# PROBLEM 1 -- the provenance guard
# --------------------------------------------------------------------------

def test_scan_from_a_different_objdiff_raises_and_the_same_scan_corrected_does_not(
        tmp_path: Path, fake_tree: Path):
    """The staleness axis that actually bit: a 4.2.6 scan read by a 4.2.7 binary.

    SABOTAGE: delete the `tool_version != installed` comparison in
    `ensure_current_scan`.  The first half goes red (no exception raised).
    The second half is the negative control that keeps the first half from being
    vacuous -- a guard that raised unconditionally would fail it.
    """
    installed = installed_objdiff_version(REPO_ROOT)

    stale = tmp_path / "stale.db"
    _make_db(stale, tool_version="objdiff-cli 4.2.6 (bf7405e3fe07, xxh3 af689f)")
    con = sqlite3.connect(stale)
    with pytest.raises(StalePatternScanError) as e:
        ensure_current_scan(con, repo_root=REPO_ROOT)
    con.close()
    # the message must name BOTH instruments -- an unattributed refusal is not
    # actionable and was the reason this stayed a comment for a day
    assert "4.2.6" in str(e.value)
    assert installed in str(e.value)

    # NEGATIVE CONTROL, same fixture, only the version corrected
    fresh = tmp_path / "fresh.db"
    _make_db(fresh, tool_version=installed)
    con = sqlite3.connect(fresh)
    scan = ensure_current_scan(con, repo_root=REPO_ROOT)
    con.close()
    assert scan["id"] == 1 and scan["tool_version"] == installed


def test_absence_of_a_scan_raises_rather_than_returning_an_empty_set(
        tmp_path: Path, fake_tree: Path):
    """No scan must not read as "no wrong callees here".

    SABOTAGE: make `ensure_current_scan` `return {}` when `latest_scan` is None.
    The first half goes red.  The control is the same DB with a scan inserted.
    """
    installed = installed_objdiff_version(REPO_ROOT)
    empty = tmp_path / "empty.db"
    _make_db(empty, tool_version=None)
    con = sqlite3.connect(empty)
    with pytest.raises(StalePatternScanError, match="no pattern scan recorded"):
        ensure_current_scan(con, repo_root=REPO_ROOT)
    con.close()

    # NEGATIVE CONTROL
    ok = tmp_path / "ok.db"
    _make_db(ok, tool_version=installed)
    con = sqlite3.connect(ok)
    assert ensure_current_scan(con, repo_root=REPO_ROOT)["id"] == 1
    con.close()


def test_unverified_tree_and_hashless_version_both_refuse(tmp_path: Path,
                                                          fake_tree: Path):
    """tree_verified=0 and a version string carrying no xxh3 are both refusals.

    objdiff's `build_id.rs` says the commit stamp is ADVISORY (cargo re-runs
    build.rs only on declared inputs, so it can lag the bytes) and the xxh3 of
    the executable is AUTHORITATIVE.  A recorded version with no hash therefore
    proves nothing worth a certificate even when the strings match exactly.

    A `-dirty` suffix is deliberately NOT refused -- the xxh3 already identifies
    those exact bytes -- and the last assertion is the control that says so.

    SABOTAGE: drop either refusal.  Its half goes red.  The two controls at the
    end prove the guard is not simply refusing everything.
    """
    import orchestrator.callee_gate as cg
    installed = installed_objdiff_version(REPO_ROOT)

    unverified = tmp_path / "unverified.db"
    _make_db(unverified, tool_version=installed, tree_verified=0)
    con = sqlite3.connect(unverified)
    with pytest.raises(StalePatternScanError, match="tree_verified=0"):
        ensure_current_scan(con, repo_root=REPO_ROOT)
    con.close()

    # A scan whose version string MATCHES the installed one exactly and still
    # cannot identify the instrument.  `installed_objdiff_version` is stubbed so
    # the two sides agree -- otherwise the version comparison would fire first
    # and this refusal would never be reached (that would be a vacuous test).
    hashless = "objdiff-cli 4.2.7 (76c8da87e040, xxh3 unavailable)"
    db = tmp_path / "hashless.db"
    _make_db(db, tool_version=hashless)
    con = sqlite3.connect(db)
    real = cg.installed_objdiff_version
    cg.installed_objdiff_version = lambda *a, **k: hashless
    try:
        with pytest.raises(StalePatternScanError, match="no binary hash"):
            cg.ensure_current_scan(con, repo_root=REPO_ROOT)
    finally:
        cg.installed_objdiff_version = real
    con.close()

    # NEGATIVE CONTROL 1: a DIRTY but hashed version must pass -- dirtiness is
    # the normal state of the shared bin/objdiff-cli and is fully identified.
    dirty = "objdiff-cli 4.2.7 (0a9716466e95-dirty, xxh3 3cb5e58b2e005fb3)"
    db = tmp_path / "dirty.db"
    _make_db(db, tool_version=dirty)
    con = sqlite3.connect(db)
    cg.installed_objdiff_version = lambda *a, **k: dirty
    try:
        assert cg.ensure_current_scan(con, repo_root=REPO_ROOT)["id"] == 1
    finally:
        cg.installed_objdiff_version = real
    con.close()

    # NEGATIVE CONTROL 2: the real installed version, verified tree -> no refusal
    good = tmp_path / "good.db"
    _make_db(good, tool_version=installed, tree_verified=1)
    con = sqlite3.connect(good)
    assert ensure_current_scan(con, repo_root=REPO_ROOT)["tree_verified"] == 1
    con.close()


def test_verify_pattern_scan_current_check_exit_codes(tmp_path: Path):
    """The standalone assertion must exit 1 on a stale scan and 0 on a fresh one.

    SABOTAGE: make `verify_pattern_scan_current.py` return 0 unconditionally.
    The stale half goes red.  The control is the same script on a fresh DB --
    a script that always exited 1 would fail that half.
    """
    installed = installed_objdiff_version(REPO_ROOT)
    script = REPO_ROOT / "scripts" / "verify_pattern_scan_current.py"

    stale = tmp_path / "stale.db"
    _make_db(stale, tool_version="objdiff-cli 1.0.0 (deadbeef, xxh3 0)")
    r = subprocess.run([sys.executable, str(script), "--db", str(stale), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 1, r.stdout + r.stderr
    assert "STALE PATTERN SCAN" in r.stderr

    fresh = tmp_path / "fresh.db"
    _make_db(fresh, tool_version=installed)
    r = subprocess.run([sys.executable, str(script), "--db", str(fresh), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr


TRIPWIRE_TEXT = (
    "This is NOT a database. It is a tripwire, planted by "
    "scripts/setup_worktree.sh.\n"
)


def _fake_worktree(tmp_path: Path, *, main_tool_version: str,
                   shadow: str | None = "tripwire") -> tuple[Path, Path]:
    """A main checkout with a real decomp.db + a linked worktree beside it.

    `shadow` picks what sits at `<worktree>/decomp.db`:
      "tripwire" -- the non-SQLite file setup_worktree.sh plants;
      "sqlite"   -- a VALID but verdict-less shadow, the shape a worktree
                    `ninja` used to grow.  This one opens cleanly and returns
                    no scan rows, so it reaches the same wrong diagnosis with no
                    sqlite error to catch -- which is why the guard also checks
                    by PATH.
    Returns (main_db, worktree_db).
    """
    main = tmp_path / "main"
    (main / ".git").mkdir(parents=True)
    main_db = main / "decomp.db"
    _make_db(main_db, tool_version=main_tool_version)

    wt = tmp_path / "wt"
    wt.mkdir()
    (wt / ".git").write_text(f"gitdir: {main}/.git/worktrees/wt\n")
    wt_db = wt / "decomp.db"
    if shadow == "tripwire":
        wt_db.write_text(TRIPWIRE_TEXT)
    else:
        _make_db(wt_db, tool_version=None)     # valid schema, zero scan rows
    return main_db, wt_db


@pytest.mark.parametrize("shadow", ["tripwire", "sqlite"])
def test_a_worktree_db_is_diagnosed_as_the_wrong_database_not_a_stale_scan(
        tmp_path: Path, shadow: str):
    """The guard must name the condition it is actually in.

    Run from a worktree, `verify_pattern_scan_current.py --check` used to report
    `no pattern scan recorded for ruler='name_check'` -- a STALENESS verdict for
    a WRONG-DIRECTORY condition, with a "re-derive it" command that would have
    changed nothing.  A lane lost time to exactly that on 2026-08-31.

    Both shadow shapes are covered because they fail differently: the tripwire
    raises `file is not a database` on the first statement, while a valid shadow
    answers the query with zero rows and no error at all.

    SABOTAGE (either one turns this red):
      * restore `except sqlite3.Error: return None` in `latest_scan` -- the
        "tripwire" case falls back to "no pattern scan recorded", exit 1;
      * delete the path-based shadow check at the top of `ensure_current_scan`
        -- the "sqlite" case does the same.
    The two negative controls below keep it from being satisfied by a guard that
    simply exits 2 on everything.
    """
    installed = installed_objdiff_version(REPO_ROOT)
    script = REPO_ROOT / "scripts" / "verify_pattern_scan_current.py"
    main_db, wt_db = _fake_worktree(tmp_path, main_tool_version=installed,
                                    shadow=shadow)

    r = subprocess.run([sys.executable, str(script), "--db", str(wt_db), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 2, f"expected the wrong-DB exit code\n{r.stdout}{r.stderr}"
    assert "UNREADABLE DATABASE" in r.stderr
    # it must name the real database, or the diagnosis is not actionable
    assert str(main_db) in r.stderr
    # and it must NOT make the claim that sent the lane to the census
    assert "no pattern scan recorded" not in r.stderr
    assert "STALE PATTERN SCAN" not in r.stderr

    # NEGATIVE CONTROL 1 -- the same script on the MAIN checkout's DB, with a
    # genuinely stale scan, must still give the STALENESS verdict.  Without this
    # half, a guard that returned 2 unconditionally would pass.
    stale_main, _ = _fake_worktree(tmp_path / "b", main_tool_version=
                                   "objdiff-cli 1.0.0 (deadbeef, xxh3 0)")
    r = subprocess.run([sys.executable, str(script), "--db", str(stale_main), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 1, r.stdout + r.stderr
    assert "STALE PATTERN SCAN" in r.stderr
    assert "UNREADABLE DATABASE" not in r.stderr

    # NEGATIVE CONTROL 2 -- main checkout, current scan: still a clean pass.
    r = subprocess.run([sys.executable, str(script), "--db", str(main_db), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr


def test_a_scan_written_from_another_tree_is_refused_and_named(tmp_path: Path):
    """A census run from a WORKTREE must not read green from main.

    `pattern_census.py --apply` from a worktree records `project_dir` = that
    worktree.  The row lands in the shared main `decomp.db`, outlives the
    directory and the branch, and becomes the latest scan for its ruler.
    Measured in the real DB on 2026-08-31: four of eleven scans are of that
    shape, three of those directories are gone, and scan 9 was latest for
    fifteen minutes while the guard reported green.

    SABOTAGE: delete the `check_scan_tree(...)` call in `ensure_current_scan`.
    The first two halves go red.  The third half is the negative control: a scan
    recorded against the DB's own tree must still pass, so a guard that refused
    every scan would fail here.
    """
    from orchestrator.callee_gate import UnmooredPatternScanError
    installed = installed_objdiff_version(REPO_ROOT)
    script = REPO_ROOT / "scripts" / "verify_pattern_scan_current.py"

    # A worktree path that does not exist -- the common real state (three of the
    # four recorded worktree scans name directories that are already gone).
    ghost = tmp_path / "wt-callee5"
    db = tmp_path / "decomp_main.db"
    _make_db(db, tool_version=installed, project_dir=str(ghost))

    con = sqlite3.connect(db)
    with pytest.raises(UnmooredPatternScanError) as e:
        ensure_current_scan(con, repo_root=REPO_ROOT)
    con.close()
    assert str(ghost) in str(e.value)              # names the scan's tree
    assert str(tmp_path.resolve()) in str(e.value)  # and the DB's tree
    assert "no longer exists" in str(e.value)
    # it must remain catchable as staleness -- sync_objdiff catches that type
    assert isinstance(e.value, StalePatternScanError)

    r = subprocess.run([sys.executable, str(script), "--db", str(db), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 3, r.stdout + r.stderr
    assert "UNMOORED PATTERN SCAN" in r.stderr

    # NEGATIVE CONTROL: same fixture, scan recorded against the DB's own tree.
    ok = tmp_path / "ok" / "decomp.db"
    ok.parent.mkdir()
    _make_db(ok, tool_version=installed)
    con = sqlite3.connect(ok)
    assert ensure_current_scan(con, repo_root=REPO_ROOT)["id"] == 1
    con.close()
    r = subprocess.run([sys.executable, str(script), "--db", str(ok), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr


def test_unreadable_database_is_not_a_stale_scan_error(tmp_path: Path):
    """The two conditions must be two exception TYPES, not two strings.

    `sync_objdiff.py` branches on the type name when it reports why the gate is
    unavailable, and a caller must be able to tell "wrong database" from
    "re-run the census" without parsing prose.

    SABOTAGE: make `UnreadableDatabaseError` subclass `StalePatternScanError`.
    The first assertion goes red.  The second is the control: a real staleness
    refusal must NOT be an `UnreadableDatabaseError`.
    """
    from orchestrator.callee_gate import UnreadableDatabaseError

    assert not issubclass(UnreadableDatabaseError, StalePatternScanError)
    assert not issubclass(StalePatternScanError, UnreadableDatabaseError)

    junk = tmp_path / "not-a-db.sqlite"
    junk.write_text("plainly not SQLite\n")
    con = sqlite3.connect(junk)
    with pytest.raises(UnreadableDatabaseError, match="NOT a stale scan"):
        ensure_current_scan(con, repo_root=REPO_ROOT)
    con.close()

    # NEGATIVE CONTROL: a readable DB with an aged scan raises the OTHER type.
    stale = tmp_path / "stale.db"
    _make_db(stale, tool_version="objdiff-cli 1.0.0 (deadbeef, xxh3 0)")
    con = sqlite3.connect(stale)
    with pytest.raises(StalePatternScanError):
        ensure_current_scan(con, repo_root=REPO_ROOT)
    con.close()


# --------------------------------------------------------------------------
# the adjudicator
# --------------------------------------------------------------------------

def test_classify_pair_truth_table(fake_tree: Path):
    """Every disposition, including the two that must never be reached silently.

    SABOTAGE: change `ICF_FOLD` to be returned whenever both names are in the
    map (drop the `at == ab` test).  The REAL_OTHER_ADDR row goes red.  The
    ICF_FOLD row is the control that keeps it from being satisfied by a
    classifier that never returns ICF_FOLD at all.
    """
    m = load_linker_map(fake_tree)
    assert m.address("?Left@A@@QAAXXZ") == "82001000"
    assert set(m.group("82001000")) == {"?Left@A@@QAAXXZ", "?Right@B@@QAAXXZ"}

    cases = {
        ("?Left@A@@QAAXXZ",       "?Right@B@@QAAXXZ"):      "ICF_FOLD",
        ("?Left@A@@QAAXXZ",       "?Elsewhere@C@@QAAXXZ"):  "REAL_OTHER_ADDR",
        ("?Left@A@@QAAXXZ",       "?NotInMap@@QAAXXZ"):     "BASE_NOT_IN_MAP",
        ("?NotInMap@@QAAXXZ",     "?Left@A@@QAAXXZ"):       "TARGET_NOT_IN_MAP",
        ("?Left@A@@QAAXXZ",       "?Twice@E@@QAAXXZ"):      "AMBIGUOUS",
        ("?Left@A@@QAAXXZ",       "?merged_Whatever@@YAXXZ"): "MERGED_STUB",
    }
    got = {k: classify_pair(m, *k) for k in cases}
    assert got == cases


def test_a_map_that_parses_to_nothing_refuses_instead_of_adjudicating_blind(
        tmp_path: Path):
    """An empty map would classify EVERY pair TARGET_NOT_IN_MAP -- i.e. block
    everything -- which looks safe and is actually an instrument failure.

    SABOTAGE: delete the `if not addr: raise` in `load_linker_map`.  Goes red.
    Control: the well-formed map in the same test still loads.
    """
    bad = tmp_path / "bad"
    p = bad / "orig" / "373307D9" / "ham_xbox_r.map"
    p.parent.mkdir(parents=True)
    p.write_text("nothing that matches the map line format at all\n")
    with pytest.raises(LinkerMapError, match="parsed 0 symbols"):
        load_linker_map(bad)

    good = tmp_path / "good"
    _write_map(good)
    assert len(load_linker_map(good).addr) == 5     # ?Twice@E@@ counted once


# --------------------------------------------------------------------------
# PROBLEM 2 -- the judging gate
# --------------------------------------------------------------------------

def _gate(tmp_path: Path, fake_tree: Path, patterns) -> "object":
    db = tmp_path / "gate.db"
    _make_db(db, tool_version=installed_objdiff_version(REPO_ROOT), patterns=patterns)
    con = sqlite3.connect(db)
    try:
        return build_callee_gate(con, repo_root=REPO_ROOT, project_dir=fake_tree)
    finally:
        con.close()


def test_gate_judges_folds_and_pairing_artifacts_but_blocks_a_different_address(
        tmp_path: Path, fake_tree: Path):
    """The whole disposition table in one call, so no row's verdict can be a
    constant the test happens to agree with.

    SABOTAGE: make `_PAIR_DISPOSITION["REAL_OTHER_ADDR"]` non-blocking.  `real`
    goes red.  SABOTAGE 2: make every pair block.  `fold`, `guessed` and
    `merged` go red.  Neither sabotage can satisfy both halves.
    """
    g = _gate(tmp_path, fake_tree, [
        ("fold",    "WRONG_CALLEE", "likely_fixable",
         _callees(("?Left@A@@QAAXXZ", "?Right@B@@QAAXXZ"))),
        ("real",    "WRONG_CALLEE", "likely_fixable",
         _callees(("?Left@A@@QAAXXZ", "?Elsewhere@C@@QAAXXZ"))),
        ("guessed", "WRONG_CALLEE", "unverifiable",
         _callees(("?Left@A@@QAAXXZ", "?Elsewhere@C@@QAAXXZ"))),
        ("merged",  "WRONG_CALLEE", "likely_fixable",
         _callees(("?Left@A@@QAAXXZ", "?merged_Thing@@YAXXZ"))),
        ("ambig",   "WRONG_CALLEE", "likely_fixable",
         _callees(("?Left@A@@QAAXXZ", "?Twice@E@@QAAXXZ"))),
        ("garbage", "WRONG_CALLEE", "likely_fixable", "not json at all"),
    ])
    assert g.cleared == {"fold": "icf_fold", "guessed": "unverifiable_pairing",
                         "merged": "merged_stub"}
    assert g.blocked == {"real": "real_other_address", "ambig": "unresolved",
                         "garbage": "no_evidence"}
    assert g.blocks("real") and not g.blocks("fold")


def test_one_blocking_pair_among_folds_still_withholds_the_certificate(
        tmp_path: Path, fake_tree: Path):
    """A function is cleared only when EVERY pair is non-actionable.

    This is the sub-case the 2026-08-21 `fix/callee-rest` lane makes dangerous:
    a fold group proves the printed NAME is unreliable, not that our call is
    right, and the lane fixed twelve rows by expanding the group at the target
    address.  Those rows are the `real_other_address` shape, and one of them
    sitting beside a genuine fold must not be averaged away.

    SABOTAGE: change the `blocking` test from `any` to `all` (i.e. clear a
    function when ANY pair folds).  `mixed` goes red.  The control is
    `all_folds` in the same call, which must stay cleared.
    """
    g = _gate(tmp_path, fake_tree, [
        ("mixed", "WRONG_CALLEE", "likely_fixable",
         _callees(("?Left@A@@QAAXXZ", "?Right@B@@QAAXXZ"),
                  ("?Left@A@@QAAXXZ", "?Elsewhere@C@@QAAXXZ"))),
        ("all_folds", "WRONG_CALLEE", "likely_fixable",
         _callees(("?Left@A@@QAAXXZ", "?Right@B@@QAAXXZ"),
                  ("?Right@B@@QAAXXZ", "?Left@A@@QAAXXZ"))),
    ])
    assert g.blocked == {"mixed": "real_other_address"}
    assert g.cleared == {"all_folds": "icf_fold"}


def test_unverifiable_pairing_clears_only_when_every_finding_is_unverifiable(
        tmp_path: Path, fake_tree: Path):
    """A guessed pair beside a real one must not launder the real one.

    SABOTAGE: clear a function when ANY row is `unverifiable` instead of all.
    `both` goes red.  Control: `only_guessed` must stay cleared.
    """
    g = _gate(tmp_path, fake_tree, [
        ("both", "WRONG_CALLEE", "unverifiable",
         _callees(("?Left@A@@QAAXXZ", "?Elsewhere@C@@QAAXXZ"))),
        ("both", "TEMPLATE_INSTANTIATION_MISMATCH", "likely_fixable",
         _callees(("?Left@A@@QAAXXZ", "?Elsewhere@C@@QAAXXZ"))),
        ("only_guessed", "WRONG_CALLEE", "unverifiable",
         _callees(("?Left@A@@QAAXXZ", "?Elsewhere@C@@QAAXXZ"))),
    ])
    assert g.blocked == {"both": "real_other_address"}
    assert g.cleared == {"only_guessed": "unverifiable_pairing"}


# --------------------------------------------------------------------------
# the required positive control, on the real evidence
# --------------------------------------------------------------------------

@pytest.mark.skipif(not REAL_DB.exists(), reason="main checkout's decomp.db absent")
def test_positive_control_on_the_real_population_both_directions(tmp_path: Path):
    """Certify a row that is genuinely unfixable; refuse one that is fixable.

    `?Copy@FxSend@@` is the canonical fixable row (it crossed to 100.0 the
    moment the callee was corrected, af06c725b) and MUST be blocked.
    `?DoVelocity@NgPostProc@@` calls a synthesised `merged_*` stub -- #112's
    refusal class, config work rather than source work -- and MUST be cleared.
    Asserting both in one call is the negative control: a gate that returned a
    constant fails one half whichever constant it picks.

    The scan's `tool_version` is rewritten to the installed one in a COPY of the
    database, deliberately and only here: this test is about the adjudication,
    and the provenance guard has its own tests above.  Without that the test
    would silently start skipping whenever the shared objdiff binary moves --
    which it did, mid-session, on 2026-08-22.

    SABOTAGE: make `classify_pair` return `ICF_FOLD` whenever both names are in
    the map.  The FxSend half goes red.  SABOTAGE 2: drop the `merged_*` case.
    The DoVelocity half goes red.
    """
    db = tmp_path / "real.db"
    db.write_bytes(REAL_DB.read_bytes())
    con = sqlite3.connect(db)
    con.execute("UPDATE pattern_scans SET tool_version = ?, tree_verified = 1 "
                " WHERE id = (SELECT MAX(id) FROM pattern_scans WHERE ruler='name_check')",
                (installed_objdiff_version(REPO_ROOT),))
    # Copying the DB out of its checkout is itself an unmoored state (see
    # `check_scan_tree`); re-point the scan at the copy so the axis under test
    # stays the adjudication, not the provenance.
    con.execute("UPDATE pattern_scans SET project_dir = ? "
                " WHERE id = (SELECT MAX(id) FROM pattern_scans WHERE ruler='name_check')",
                (str(tmp_path.resolve()),))
    con.commit()

    present = {r[0] for r in con.execute(
        "SELECT f.symbol FROM function_patterns p JOIN functions f ON f.id=p.function_id"
        " WHERE p.pattern IN (%s)" % ",".join("?" * len(CALLEE_PATTERNS)),
        CALLEE_PATTERNS)}
    assert FIXABLE_CONTROL in present, \
        "the fixable control is not in the scan -- the test would be vacuous"
    assert UNFIXABLE_CONTROL in present, \
        "the unfixable control is not in the scan -- the test would be vacuous"

    gate = build_callee_gate(con, repo_root=REPO_ROOT, project_dir=REPO_ROOT)
    con.close()

    assert gate.blocks(FIXABLE_CONTROL), \
        f"{FIXABLE_CONTROL} is fixable and must NOT be certifiable"
    assert gate.blocked[FIXABLE_CONTROL] == "real_other_address"

    assert not gate.blocks(UNFIXABLE_CONTROL), \
        f"{UNFIXABLE_CONTROL} is #112's refusal class and should be certifiable"
    assert gate.cleared[UNFIXABLE_CONTROL] == "merged_stub"

    # and the population is the one the 2026-08-21 re-adjudication measured
    assert len(gate.blocked) + len(gate.cleared) == 138
    assert sorted(gate.counts().items()) == [
        ("block:real_other_address", 63),
        ("block:unresolved", 3),
        ("clear:icf_fold", 13),
        ("clear:merged_stub", 1),
        ("clear:unverifiable_pairing", 58),
    ]


@pytest.mark.skipif(not REAL_DB.exists(), reason="main checkout's decomp.db absent")
def test_sync_objdiff_refuses_to_certify_from_a_stale_scan_end_to_end(tmp_path: Path):
    """`sync_objdiff.py --auto-at-limit` must EXIT 5, not warn and carry on.

    This is the wiring test.  The unit tests above prove `ensure_current_scan`
    raises; nothing there proves `sync_objdiff` surfaces it rather than
    swallowing it into the `except sqlite3.Error` that used to sit at this call
    site and turn any failure into an empty, permissive set.

    The assertion is on the EXIT CODE, deliberately not on the message: a
    sabotage that leaves the warning printed while dropping `sys.exit(5)` -- the
    exact shape that fooled a lane this week -- must still turn this red.

    SABOTAGE: replace `sys.exit(5)` with `pass`.  The first half goes red.
    Control: the same command against a DB whose scan version has been corrected
    must NOT exit 5, so a script that always exited 5 fails too.
    """
    script = REPO_ROOT / "scripts" / "sync_objdiff.py"
    tiny_unit = "default/system/jpeg/jfdctflt"      # exactly 1 function
    argv = ["--dry-run", "--all", "--unit", tiny_unit, "--skip-patch-check",
            "--auto-at-limit"]

    stale = tmp_path / "stale.db"
    stale.write_bytes(REAL_DB.read_bytes())
    con = sqlite3.connect(stale)
    con.execute("UPDATE pattern_scans SET tool_version = 'objdiff-cli 0.0.0 "
                "(deadbeef, xxh3 0000000000000000)' WHERE ruler = 'name_check'")
    con.commit(); con.close()
    r = subprocess.run([sys.executable, str(script), "--db", str(stale), *argv],
                       capture_output=True, text=True)
    assert r.returncode == 5, f"rc={r.returncode}\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}"
    assert "REFUSING to issue auto-AT_LIMIT" in r.stderr

    # NEGATIVE CONTROL: same command, scan version corrected
    fresh = tmp_path / "fresh.db"
    fresh.write_bytes(REAL_DB.read_bytes())
    con = sqlite3.connect(fresh)
    con.execute("UPDATE pattern_scans SET tool_version = ?, tree_verified = 1 "
                " WHERE ruler = 'name_check'", (installed_objdiff_version(REPO_ROOT),))
    # The copy moved the database out of the checkout it describes, which
    # `check_scan_tree` correctly refuses.  Re-point the scan at the copy's own
    # directory so the ONE axis under test here stays the tool version.
    con.execute("UPDATE pattern_scans SET project_dir = ? WHERE ruler = 'name_check'",
                (str(tmp_path.resolve()),))
    con.commit(); con.close()
    r = subprocess.run([sys.executable, str(script), "--db", str(fresh), *argv],
                       capture_output=True, text=True)
    assert r.returncode != 5, f"{r.stdout[-2000:]}\n{r.stderr[-2000:]}"
    assert "REFUSING to issue auto-AT_LIMIT" not in r.stderr
