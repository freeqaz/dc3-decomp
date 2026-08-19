"""Tests for scripts/analysis/coverage.py — the scanner-honesty contract.

EVERY test here is written as a NEGATIVE CONTROL: it reconstructs the false
negative the check is supposed to catch and asserts the check now fires. The
worst verification pattern this project has produced was asserting a synthesised
value against a constant written in the same sitting (a tautology — it let a
real error through), so nothing below compares a number to itself. Each test
either

  (a) replays a HISTORICAL defect with its real numbers, or
  (b) builds a scanner that lies, and asserts we catch it.
"""
from __future__ import annotations

import json

import pytest

from scripts.analysis.coverage import (
    CoverageReport,
    TruncationError,
    EXIT_OK,
    EXIT_TRUNCATED,
    EXIT_UNACCOUNTED,
    EXIT_NO_INPUT,
    EXIT_NO_DENOMINATOR,
    like_escape,
    like_prefix_clause,
)


# --------------------------------------------------------------------------- #
# The happy path exists only so the failure paths are meaningful.
# --------------------------------------------------------------------------- #

def test_full_census_is_clean_and_exits_zero(capsys):
    cov = CoverageReport("t", allow_truncation=False)
    cov.universe(100, "widgets")
    cov.examine(90)
    cov.drop("not-selected", 10)
    assert cov.emit() == EXIT_OK
    out = capsys.readouterr().err
    assert "universe            : 100" in out
    assert "TRUNCATED" not in out
    assert "UNACCOUNTED" not in out
    assert cov.as_dict()["complete"] is True


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 1 — the data_symbol_scan cap, with its real numbers.
#
# The historical run examined 4,000 of 18,549 data symbols and printed only
# `scanned=4000`. Nothing in that line was false; it simply never mentioned the
# 14,549 it did not look at, and the resulting counts were quoted as totals for
# a month. Reconstruct that exact run and assert it can no longer pass quietly.
# --------------------------------------------------------------------------- #

def test_max_symbols_cap_is_reported_and_nonzero_exit(capsys):
    ALL, CAP = 18549, 4000               # the numbers from the real incident
    cov = CoverageReport("data_symbol_scan", allow_truncation=False)
    cov.universe(ALL, "data symbols in target .obj files")
    cov.cap("--max-symbols", CAP, before=ALL, after=CAP)
    cov.examine(CAP)

    rc = cov.emit()
    out = capsys.readouterr().err

    assert rc == EXIT_TRUNCATED, "a 22% sample must not exit 0"
    assert "TRUNCATED" in out
    # The banner must name the size of the hole, not merely admit one exists.
    assert "14549" in out
    assert "18549" in out
    assert "SAMPLE, not a census" in out
    d = cov.as_dict()
    assert d["truncated"] is True
    assert d["complete"] is False
    assert d["dropped"]["capped-by-max-symbols"] == 14549
    # 4000/18549 = 21.56% — the "22% sample presented as a total" from the log.
    assert 21.0 < d["coverage_pct"] < 22.0


def test_uncapped_run_of_the_same_scan_is_clean(capsys):
    """Control for the control: the cap machinery must not fire when nothing is cut."""
    ALL = 18549
    cov = CoverageReport("data_symbol_scan")
    cov.universe(ALL, "data symbols")
    cov.cap("--max-symbols", 0, before=ALL, after=ALL)
    cov.examine(ALL)
    assert cov.emit() == EXIT_OK
    assert "TRUNCATED" not in capsys.readouterr().err


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 2 — the fake_impl_scan bare `continue`.
#
# `if pct is None: continue` discarded every row objdiff scored without a
# `fuzzy_match_percent` — i.e. every function we never wrote. ~1,024 rows, and
# four waves reported the pool EXHAUSTED. The point of the arithmetic check is
# that it catches this shape WITHOUT anyone having anticipated it: the author
# does not have to know the field can be missing, only that the numbers must
# balance.
# --------------------------------------------------------------------------- #

def _scan(rows, count_the_skip: bool):
    """A miniature scanner. `count_the_skip=False` reproduces the 2026 bug."""
    cov = CoverageReport("fake_impl_scan", allow_truncation=False)
    cov.universe(len(rows), "authorable functions in report.json")
    kept = []
    for r in rows:
        if r.get("fuzzy_match_percent") is None:
            if count_the_skip:
                cov.drop("missing-fuzzy-percent", note="no base body — the tier-2 pool")
            continue                       # <-- the historical bare continue
        cov.examine()
        kept.append(r)
    return cov, kept


BUGGY_ROWS = (
    [{"fuzzy_match_percent": 42.0}] * 500          # tier 1: we wrote a body
    + [{"match_percent_normalized": 0.0}] * 1024   # tier 2: we wrote nothing
)


def test_uncounted_continue_is_caught_by_the_arithmetic(capsys):
    cov, kept = _scan(BUGGY_ROWS, count_the_skip=False)
    rc = cov.emit()
    out = capsys.readouterr().err

    assert len(kept) == 500, "the buggy scanner really did see only tier 1"
    assert rc == EXIT_UNACCOUNTED
    assert cov.unaccounted == 1024
    assert "UNACCOUNTED" in out
    assert "1024" in out
    # And nothing about the run may look like a completed census.
    assert cov.as_dict()["complete"] is False
    with pytest.raises(TruncationError):
        cov.assert_complete()


def test_the_same_scan_with_the_skip_counted_balances(capsys):
    cov, kept = _scan(BUGGY_ROWS, count_the_skip=True)
    assert cov.emit() == EXIT_OK
    err = capsys.readouterr().err
    assert len(kept) == 500
    assert cov.unaccounted == 0
    # The 1,024 are still not examined — but now they are VISIBLE, which is the
    # whole difference between "exhausted" and "never looked at".
    assert cov.as_dict()["dropped"]["missing-fuzzy-percent"] == 1024
    assert "missing-fuzzy-percent" in err
    assert "UNACCOUNTED" not in err


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 3 — a scanner that never declares a denominator at all.
# "12 candidates" with no universe is the shape that starts every false
# exhaustion claim, so it must not render as a clean result.
# --------------------------------------------------------------------------- #

def test_missing_universe_is_admitted_not_assumed(capsys):
    cov = CoverageReport("nameless")
    cov.examine(12)
    cov.emit()
    out = capsys.readouterr().err
    assert "universe            : UNKNOWN" in out
    assert "NO DENOMINATOR" in out
    assert cov.as_dict()["complete"] is False
    assert cov.as_dict()["coverage_pct"] is None
    with pytest.raises(TruncationError):
        cov.assert_complete()


# --------------------------------------------------------------------------- #
# --allow-truncation is an escape hatch for TRUNCATION only. An unbalanced
# denominator is a scanner bug, so no flag may silence it.
# --------------------------------------------------------------------------- #

def test_allow_truncation_downgrades_a_cap_but_not_an_imbalance():
    cov = CoverageReport("t", allow_truncation=True)
    cov.universe(100)
    cov.cap("--limit", 10, before=100, after=10)
    cov.examine(90)          # 10 examined + 90 capped would balance; this does not
    assert cov.emit() == EXIT_UNACCOUNTED

    ok = CoverageReport("t", allow_truncation=True)
    ok.universe(100)
    ok.cap("--limit", 10, before=100, after=10)
    ok.examine(10)
    assert ok.emit() == EXIT_OK
    # ...but the JSON still confesses.
    assert ok.as_dict()["truncated"] is True
    assert ok.as_dict()["complete"] is False


def test_coverage_json_is_written(tmp_path):
    p = tmp_path / "cov.json"

    class A:
        allow_truncation = False
        coverage_json = str(p)

    cov = CoverageReport("t", args=A())
    cov.universe(5)
    cov.examine(5)
    cov.emit()
    d = json.loads(p.read_text())
    assert d["scanner"] == "t" and d["universe"] == 5 and d["complete"] is True


def test_drop_reasons_are_stable_and_sorted():
    """Two identical runs must produce byte-identical coverage blocks."""
    def run():
        c = CoverageReport("t")
        c.universe(6)
        c.examine(1)
        for r in ("zebra", "alpha", "mid", "alpha", "zebra"):
            c.drop(r)
        return c.render(), json.dumps(c.as_dict(), sort_keys=False)

    a_txt, a_json = run()
    b_txt, b_json = run()
    assert a_txt == b_txt
    assert a_json == b_json


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 4 — the certify_floor SQL LIKE wildcard.
#
# `symbol NOT LIKE '??_%'` reads as "not starting with ??_" but `_` matches ANY
# single character, so it excluded every '??'-prefixed symbol: 6,835 functions.
# Assert against real SQLite semantics, not against our own expectation string.
# --------------------------------------------------------------------------- #

def test_like_escape_against_real_sqlite_semantics():
    import sqlite3
    db = sqlite3.connect(":memory:")
    db.execute("CREATE TABLE f(symbol TEXT)")
    rows = [
        ("??_Gfoo@@",),      # genuinely ??_-prefixed: SHOULD be excluded
        ("??0Bar@@QAE@XZ",),  # a ctor — '??' then '0'. Must SURVIVE the filter.
        ("??1Bar@@QAE@XZ",),  # a dtor
        ("?Method@Cls@@",),   # ordinary method
    ]
    db.executemany("INSERT INTO f VALUES (?)", rows)

    naive = list(db.execute("SELECT symbol FROM f WHERE symbol NOT LIKE '??_%'"))
    # The historical bug, demonstrated: the ctor and dtor are gone too.
    assert len(naive) == 1, f"expected the naive filter to over-exclude, got {naive}"
    assert naive[0][0] == "?Method@Cls@@"

    fixed = list(db.execute(
        f"SELECT symbol FROM f WHERE {like_prefix_clause('symbol', '??_', negate=True)}"))
    got = sorted(r[0] for r in fixed)
    assert got == ["??0Bar@@QAE@XZ", "??1Bar@@QAE@XZ", "?Method@Cls@@"]
    assert "??_Gfoo@@" not in got


def test_like_escape_handles_percent_and_backslash():
    assert like_escape("a_b") == r"a\_b"
    assert like_escape("a%b") == r"a\%b"
    assert like_escape("a\\b") == "a\\\\b"
    # escaping must be idempotent-safe under SQLite, not merely string-equal
    import sqlite3
    db = sqlite3.connect(":memory:")
    db.execute("CREATE TABLE f(s TEXT)")
    db.executemany("INSERT INTO f VALUES (?)", [("100%",), ("100x",)])
    got = [r[0] for r in db.execute(
        f"SELECT s FROM f WHERE {like_prefix_clause('s', '100%')}")]
    assert got == ["100%"]


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL — the exit-4 BYPASS.
#
# Exit 4 is the tripwire with teeth: it fires whenever a bare `continue` skipped
# drop(), so it catches the NEXT instance of this bug class without anyone
# having anticipated the field involved.  But `unaccounted` is
# `universe - (examined + drops)`, which is None when no universe was declared,
# and None is falsy.  So DELETING THE `cov.universe(...)` LINE -- one line --
# disarmed the check entirely: emit() returned 0 while stdout said "out of None
# rows" and the banner said NO DENOMINATOR.
#
# Everything below asserts the disarmed form and the armed form now exit
# DIFFERENTLY, and that the honest "I cannot know" case is still allowed.
# --------------------------------------------------------------------------- #

class _A:
    allow_truncation = False
    coverage_json = None


def _lying_scanner(rows, declare_universe):
    """A scanner with a bare `continue` -- the defect -- built two ways."""
    cov = CoverageReport("lying_scanner", args=_A())
    if declare_universe:
        cov.universe(len(rows), "rows")
    for r in rows:
        if r is None:
            continue                       # THE BUG: no cov.drop()
        cov.examine()
    return cov


def test_omitting_universe_no_longer_silences_the_unaccounted_check():
    rows = [1, None, 2, None, None]

    # (b) With the denominator declared, the tripwire fires: this is the
    # behaviour the module is FOR.
    armed = _lying_scanner(rows, declare_universe=True)
    assert armed.unaccounted == sum(1 for r in rows if r is None)
    assert armed.emit() == EXIT_UNACCOUNTED

    # The bypass: the SAME scanner with the SAME bug, minus one line.
    disarmed = _lying_scanner(rows, declare_universe=False)
    assert disarmed.unaccounted is None, (
        "the mechanism must really be disarmed, or this is not a control")

    # Historically this returned EXIT_OK -- indistinguishable from a clean
    # census.  The interesting claim is the DISAGREEMENT with EXIT_OK, not the
    # specific code.
    rc = disarmed.emit()
    assert rc != EXIT_OK, (
        "a scanner whose own arithmetic check could not run must not exit like "
        "one that passed it")
    assert rc == EXIT_NO_DENOMINATOR


def test_a_declared_unknown_denominator_is_still_allowed():
    """Control for the control: the rule must not punish an honest admission."""
    cov = CoverageReport("honest", args=_A())
    cov.universe_unknown("streaming input; the producer never states a total")
    cov.examine(7)
    assert cov.emit() == EXIT_OK
    assert "UNKNOWN, DECLARED" in cov.render()
    assert cov.as_dict()["universe_unknown_reason"]


def test_universe_unknown_demands_a_reason():
    """An unexplained missing denominator is the bug, not the escape hatch."""
    cov = CoverageReport("shrug", args=_A())
    with pytest.raises(ValueError):
        cov.universe_unknown("")


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL — balanced, and yet it looked at nothing.
#
# The residue of the DTA defect: the corpus gate keys on "the corpus was
# empty", not on "this run checked nothing".  Drop every row for perfectly good
# reasons and `universe == examined + drops` holds at examined == 0 -- clean
# arithmetic, empty epistemics, exit 0.
# --------------------------------------------------------------------------- #

def test_a_balanced_run_that_examined_nothing_is_not_a_clean_verdict():
    def build(require):
        cov = CoverageReport("empty_run", args=_A())
        cov.universe(100, "rows")
        if require:
            cov.require_examined("every row was dropped")
        cov.drop("no-resolved-context", 100)
        return cov

    lax, strict = build(False), build(True)

    # (a) The arithmetic identity holds on BOTH -- that is the whole point.
    assert lax.unaccounted == 0 and strict.unaccounted == 0

    # (b) ...and yet they must not exit the same way.
    assert lax.emit() == EXIT_OK
    assert strict.emit() == EXIT_NO_INPUT
    assert strict.emit() != lax.emit()
    assert "EXAMINED NOTHING" in strict.render()
    assert not strict.is_clean()
    assert strict.as_dict()["examined_nothing"] is True
    with pytest.raises(TruncationError):
        strict.assert_complete()


def test_require_examined_is_quiet_when_something_was_examined():
    """Control for the control: it must not fire on a run that did work."""
    cov = CoverageReport("real_run", args=_A())
    cov.universe(100, "rows")
    cov.require_examined("every row was dropped")
    cov.examine(1)
    cov.drop("filtered", 99)
    assert cov.emit() == EXIT_OK
    assert "EXAMINED NOTHING" not in cov.render()
    assert cov.is_clean()


def test_require_examined_does_not_fire_on_an_empty_universe():
    """universe == 0 is the CORPUS-empty case, which has its own gate and its
    own exit code; this check must not shadow it with a different one."""
    cov = CoverageReport("no_corpus", args=_A())
    cov.universe(0, "rows")
    cov.require_examined("every row was dropped")
    assert cov.examined_nothing is False
    assert cov.emit() == EXIT_OK
