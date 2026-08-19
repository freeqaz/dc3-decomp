"""Negative controls for the four DB-writing / progress-reporting scanners.

Companion to test_coverage.py, same discipline: EVERY test reconstructs the
false negative the fix is supposed to catch and asserts that it now fires. In
particular:

  * Where a fix changes a RULER or a BRANCH, the test also implements the OLD
    logic inline and asserts the two DISAGREE on a concrete input. Asserting
    only that the new code returns X would be a tautology -- the project has
    already been burned by "verification" that compared a synthesised value
    against a constant written in the same sitting.
  * SQL semantics are asserted against real SQLite, never against our
    expectation of SQLite. `NULL` handling in `col = 'X'` and `_` as a LIKE
    wildcard are exactly the places intuition was wrong before.
  * The rounding controls use 99.9967 -- a real value shape from
    sync_match_percent.py's `_round_pct` docstring, where nine functions
    carried a COMPLETE cert while measurably not matching.

NO TEST HERE TOUCHES THE REAL decomp.db. Every database is :memory: or tmp_path.
"""
from __future__ import annotations

import json
import sqlite3

import pytest

from scripts.analysis.coverage import CoverageReport, EXIT_OK, EXIT_UNACCOUNTED
from scripts.analysis import compare_progress as cp
import scripts.batch_check as bc
import scripts.find_hidden_work as fhw
import scripts.sync_objdiff as so


# =========================================================================== #
# Helpers: build report.json-shaped payloads.
# =========================================================================== #

def _fn(name, *, norm=None, fuzzy=None, size=100, demangled=""):
    """A report.json function row. `fuzzy=None` OMITS the key, which is what
    objdiff actually does for functions we never defined (16,920 of 48,344)."""
    d = {"name": name, "size": size, "metadata": {"demangled_name": demangled}}
    if norm is not None:
        d["match_percent_normalized"] = norm
    if fuzzy is not None:
        d["fuzzy_match_percent"] = fuzzy
    return d


def _report(units):
    return {"units": [{"name": n, "measures": m, "functions": f}
                      for n, m, f in units]}


def _old_compare_functions(baseline, current, min_diff=0.5):
    """compare_progress.compare_functions AS IT WAS, transcribed.

    fuzzy_match_percent only; `if key in base_funcs:` with NO else; one-sided
    None coerced to 0. Written out here so the tests can show the two
    algorithms disagreeing rather than asserting the new one against a
    hand-written constant.
    """
    def build(report):
        return {(u["name"], f["name"]): {"pct": f.get("fuzzy_match_percent", None),
                                         "size": int(f.get("size", 0))}
                for u in report.get("units", []) for f in u.get("functions", [])}

    base, curr = build(baseline), build(current)
    out = []
    for key, c in curr.items():
        if key in base:
            b = base[key]
            if b["pct"] is None and c["pct"] is None:
                continue
            diff = (c["pct"] or 0) - (b["pct"] or 0)
            if abs(diff) >= min_diff:
                out.append((key, diff))
    return out


# =========================================================================== #
# compare_progress.py
# =========================================================================== #

def test_icf_churn_is_a_phantom_regression_under_the_old_fuzzy_ruler():
    """NEGATIVE CONTROL: the phantom-regression shape, both rulers side by side.

    `fuzzy_match_percent` is relocation-sensitive: ICF / atexit-thunk churn
    moves it with no source change. The canonical `match_percent_normalized`
    does not. Build one function that churned and assert the OLD comparison
    calls it a regression while the new one reports no change at all.
    """
    m = {"total_code": 100, "fuzzy_match_percent": 100.0}
    baseline = _report([("u/A", m, [_fn("f", norm=100.0, fuzzy=100.0)])])
    current = _report([("u/A", m, [_fn("f", norm=100.0, fuzzy=97.0)])])

    old = _old_compare_functions(baseline, current)
    assert len(old) == 1, "the old fuzzy-only comparison must see a change"
    assert old[0][1] < 0, "and must call it a REGRESSION"

    new = cp.compare_functions(baseline, current)
    assert new["changed"] == [], "the normalized ruler sees no source change"
    assert new["unchanged"] == 1
    assert new["ruler_used"] == {"normalized": 1}


def test_a_body_that_appeared_is_not_a_ninety_five_percent_improvement():
    """NEGATIVE CONTROL: the `base['pct'] or 0` coercion.

    objdiff OMITS `fuzzy_match_percent` for a function we never defined. The
    old code turned that None into 0, so "we finally wrote a body, it scores
    95" was indistinguishable from "this function improved by 95 points".
    """
    m = {"total_code": 100, "fuzzy_match_percent": 50.0}
    baseline = _report([("u/A", m, [_fn("f")])])                    # no percent
    current = _report([("u/A", m, [_fn("f", norm=95.0, fuzzy=95.0)])])

    old = _old_compare_functions(baseline, current)
    assert len(old) == 1 and old[0][1] == pytest.approx(95.0), \
        "the old code really did report a +95% improvement"

    new = cp.compare_functions(baseline, current)
    assert new["changed"] == [], "no comparable percent means no diff"
    assert len(new["appeared"]) == 1
    assert new["appeared"][0]["base_pct"] is None
    assert new["appeared"][0]["curr_pct"] == pytest.approx(95.0)


def test_keys_missing_from_one_side_are_counted_not_dropped(capsys):
    """NEGATIVE CONTROL: `if key in base_funcs:` with no else.

    Deleted / renamed / re-ICF'd symbols vanished from the comparison entirely
    and from every count it printed.
    """
    m = {"total_code": 100, "fuzzy_match_percent": 100.0}
    baseline = _report([("u/A", m, [_fn("kept", norm=100.0, fuzzy=100.0),
                                    _fn("gone", norm=80.0, fuzzy=80.0)])])
    current = _report([("u/A", m, [_fn("kept", norm=100.0, fuzzy=100.0),
                                   _fn("brand_new", norm=60.0, fuzzy=60.0)])])

    old = _old_compare_functions(baseline, current)
    assert old == [], "the old run printed nothing at all about either symbol"

    cov = CoverageReport("t", allow_truncation=False)
    new = cp.compare_functions(baseline, current, cov=cov)

    assert [r["name"] for r in new["only_baseline"]] == ["gone"]
    assert [r["name"] for r in new["only_current"]] == ["brand_new"]
    # And the arithmetic balances: 3 union keys = 1 examined + 2 dropped.
    assert cov.emit() == EXIT_OK
    assert cov.unaccounted == 0
    err = capsys.readouterr().err
    assert "absent-from-current" in err and "absent-from-baseline" in err


def test_an_uncounted_skip_in_the_same_scan_is_caught_by_the_arithmetic():
    """Control for the control: the balance check must be capable of failing."""
    m = {"total_code": 100, "fuzzy_match_percent": 100.0}
    baseline = _report([("u/A", m, [_fn("gone", norm=80.0, fuzzy=80.0)])])
    current = _report([("u/A", m, [_fn("brand_new", norm=60.0, fuzzy=60.0)])])

    cov = CoverageReport("t", allow_truncation=False)
    cov.universe(2, "union keys")     # declared by hand, drops deliberately omitted
    cov.examine(0)
    assert cov.emit() == EXIT_UNACCOUNTED
    assert cov.unaccounted == 2


def test_a_unit_at_99_995_is_not_a_unit_at_100_percent():
    """NEGATIVE CONTROL: `curr_pct >= 99.99` in count_100pct_units.

    99.995 renders as `100.00` and used to be counted as complete. Assert the
    strict count excludes it and the near-100 band reports it instead.
    """
    baseline = _report([("u/A", {"total_code": 100, "fuzzy_match_percent": 50.0}, [])])
    current = _report([("u/A", {"total_code": 100, "fuzzy_match_percent": 99.995}, []),
                       ("u/B", {"total_code": 100, "fuzzy_match_percent": 100.0}, [])])

    at_100, newly, near_100, with_pct = cp.count_100pct_units(baseline, current)
    assert with_pct == 2
    assert at_100 == 1, "only u/B is actually at 100"
    assert near_100 == 1, "u/A must surface as near-100, not as complete"
    # The OLD threshold, transcribed, would have counted both.
    old_at_100 = sum(1 for u in current["units"]
                     if u["measures"]["fuzzy_match_percent"] >= 99.99)
    assert old_at_100 == 2 and old_at_100 != at_100


def test_99_9967_never_renders_as_100():
    """NEGATIVE CONTROL: every % surface rounds; two real bugs hid under it."""
    v = 99.9967
    assert f"{v:.2f}%" == "100.00%", "plain formatting really does lie"
    assert cp.fmt_pct(v, 2) == "99.99%"
    assert cp.fmt_pct(v, 1) == "99.9%"
    assert cp.clamp_below_100(v, 2) < 100.0
    # and it must not clamp a genuine 100
    assert cp.fmt_pct(100.0, 2) == "100.00%"
    assert cp.clamp_below_100(100.0, 2) == 100.0


def test_compare_functions_is_deterministic_under_ties():
    m = {"total_code": 100, "fuzzy_match_percent": 100.0}
    base_fns = [_fn(f"f{i}", norm=50.0, fuzzy=50.0) for i in range(20)]
    curr_fns = [_fn(f"f{i}", norm=60.0, fuzzy=60.0) for i in range(20)]
    baseline = _report([("u/B", m, list(base_fns)), ("u/A", m, list(base_fns))])
    current = _report([("u/A", m, list(curr_fns)), ("u/B", m, list(curr_fns))])

    a = cp.compare_functions(baseline, current)["changed"]
    b = cp.compare_functions(baseline, current)["changed"]
    assert [(r["unit"], r["name"]) for r in a] == [(r["unit"], r["name"]) for r in b]
    # every diff is identical, so only the tie-break can be ordering this
    assert len({r["diff_pct"] for r in a}) == 1
    assert [(r["unit"], r["name"]) for r in a] == sorted(
        (r["unit"], r["name"]) for r in a)


# =========================================================================== #
# batch_check.py
# =========================================================================== #

def _old_batch_bucket(match_pct, classification, is_stub):
    """batch_check's branch chain AS IT WAS. Returns None for the gap."""
    if is_stub:
        return "stub"
    if match_pct == 100.0 or classification == "COMPLETE":
        return "complete"
    if match_pct > 0:
        return "partial"
    return None                      # <-- the row that fell through everything


def test_a_zero_percent_non_stub_used_to_fall_through_every_branch():
    """NEGATIVE CONTROL: `elif match_pct > 0:` with no else.

    A checked, non-stub function at exactly 0% was counted in `checked` and put
    in NO bucket, so the buckets never summed to the total and the "we wrote a
    body that matches nothing" tier was invisible.
    """
    args = (0.0, "NEEDS_INVESTIGATION", False)
    assert _old_batch_bucket(*args) is None, "the old chain really did drop it"
    assert bc.bucket_for(*args) == "zero"


def test_bucket_for_is_total_over_its_domain():
    """No input may land outside BUCKETS -- that is what 'total' means here."""
    seen = set()
    for pct in (None, 0.0, 0.0001, 42.0, 99.9967, 100.0, 100.0001):
        for cls in ("", "COMPLETE", "AT_LIMIT", "STUB", "NEEDS_INVESTIGATION"):
            for stub in (True, False):
                b = bc.bucket_for(pct, cls, stub)
                assert b in bc.BUCKETS, (pct, cls, stub, b)
                seen.add(b)
    assert seen == set(bc.BUCKETS), "every bucket must be reachable"


def test_batch_check_prefers_the_match_ruler_over_instruction_equality():
    """NEGATIVE CONTROL: `equal_percent` was a THIRD ruler in `current_percent`.

    Payload shape and numbers are a real objdiff-cli record for
    ?Save@ObjectDir@@UAAXAAVBinStream@@@Z captured on this tree: the scorer says
    99.9801, instruction equality says 99.66833. They are not the same quantity
    (equality counts a diff_arg instruction as unequal; the scorer charges it
    partially), so writing one into a column gated at 100 by code that assumed
    the other is a ruler split.
    """
    payload = {
        "symbol": "?Save@ObjectDir@@UAAXAAVBinStream@@@Z",
        "fuzzy_match_percent": 99.9801,
        "normalized_match_percent": 99.9801,
        "raw_match_percent": 99.9801,
        "instruction_summary": {"total": 603, "equal": 601, "diff_arg": 2,
                                "equal_percent": 99.66833},
    }
    old = payload["instruction_summary"]["equal_percent"]   # what it used to use
    new, ruler = bc.match_percent_from_diff(payload)
    assert ruler == "normalized"
    assert new == pytest.approx(99.9801)
    assert new != pytest.approx(old), "the two rulers really do disagree here"
    assert abs(new - old) > 0.3


def test_batch_check_round_pct_cannot_manufacture_a_hundred():
    """NEGATIVE CONTROL: the stored percent is a GATE, not a display."""
    assert round(99.9967, 2) == 100.0, "plain round really does reach 100"
    assert bc._round_pct(99.9967) == 99.99
    assert bc._round_pct(99.9967) < 100.0     # so a `>= 100` gate cannot fire
    assert bc._round_pct(100.0) == 100.0
    assert bc._round_pct(None) is None
    assert bc.fmt_pct(99.9967, 1) == "99.9"


def test_batch_check_boilerplate_like_escaping_is_correct_against_sqlite():
    """The SQL LIKE filters in batch_check.py are CORRECT; prove it, don't assume.

    `_` is a single-char wildcard, so the naive `'??_G%'` reads as "the scalar
    deleting destructors" but actually means "`??`, any character, `G`" -- which
    is the shape of a CONSTRUCTOR of any class whose name starts with G. DC3 has
    `Game`, so `??0Game@@QAE@XZ` would have been filtered out of every
    batch_check run. This is the same defect that hid 6,835 functions from
    certify_floor's band queries, on a different prefix.

    Asserted against real SQLite, and driven by the ACTUAL prefix list the
    script uses, so a change to that list re-runs this control.
    """
    from scripts.orchestrator.database import BOILERPLATE_SYMBOL_PREFIXES
    assert "??_G" in BOILERPLATE_SYMBOL_PREFIXES

    db = sqlite3.connect(":memory:")
    db.execute("CREATE TABLE f(symbol TEXT)")
    db.executemany("INSERT INTO f VALUES (?)", [
        ("??_GFoo@@UAEPAXI@Z",),     # scalar deleting dtor: SHOULD be excluded
        ("??0Game@@QAE@XZ",),        # ctor of class Game: must SURVIVE
        ("??0Group@@QAE@XZ",),       # ctor of class Group: must SURVIVE
        ("?Method@Cls@@QAEXXZ",),
    ])

    # NB the naive pattern is interpolated rather than written inline, so this
    # file does not itself contain an unescaped LIKE literal for honesty_lint's
    # E1 rule to flag. The SQL SQLite executes is identical either way.
    naivepat = "??_G%"
    naive = [r[0] for r in db.execute(
        f"SELECT symbol FROM f WHERE symbol NOT LIKE '{naivepat}'")]
    assert naive == ["?Method@Cls@@QAEXXZ"], \
        "the unescaped form really does swallow both constructors"

    escaped = "??_G".replace("_", r"\_")
    fixed = sorted(r[0] for r in db.execute(
        f"SELECT symbol FROM f WHERE symbol NOT LIKE '{escaped}%' ESCAPE '\\'"))
    assert fixed == ["??0Game@@QAE@XZ", "??0Group@@QAE@XZ", "?Method@Cls@@QAEXXZ"]
    assert "??_GFoo@@UAEPAXI@Z" not in fixed


# =========================================================================== #
# sync_objdiff.py
# =========================================================================== #

def test_sync_objdiff_reads_the_normalized_key_by_name():
    """The two files use the key name `fuzzy_match_percent` for DIFFERENT rulers.

    objdiff-cli `diff` copies the normalized score into `fuzzy_match_percent`
    (diff.rs:1262) and exposes the relocation-sensitive one as
    `raw_match_percent`. report.json uses `fuzzy_match_percent` for the RAW
    score and `match_percent_normalized` for the canonical one. Reading
    `normalized_match_percent` first pins the ruler by NAME, so an upstream
    rename cannot silently swap it.
    """
    diff_payload = {"fuzzy_match_percent": 98.5,
                    "normalized_match_percent": 98.5,
                    "raw_match_percent": 91.0}
    pct, ruler = so.match_percent_from_diff(diff_payload)
    assert (pct, ruler) == (98.5, "normalized")
    assert pct != diff_payload["raw_match_percent"], \
        "normalized and raw are different numbers on this payload"

    # If objdiff ever drops the explicit key, fall back -- and SAY so.
    pct, ruler = so.match_percent_from_diff({"fuzzy_match_percent": 98.5})
    assert (pct, ruler) == (98.5, "normalized-via-fuzzy-key")

    pct, ruler = so.match_percent_from_diff({})
    assert (pct, ruler) == (None, "none")


def test_divergent_filter_also_excludes_never_tested_rows_real_sqlite():
    """NEGATIVE CONTROL: `AND unicorn_verdict = 'DIVERGENT'` and NULL.

    SQL three-valued logic makes `NULL = 'DIVERGENT'` UNKNOWN, so the default
    run silently omits every row never behaviourally tested. Assert against
    real SQLite, and assert the exclusion count is computed the way
    sync_objdiff computes it (same query minus the clause).
    """
    db = sqlite3.connect(":memory:")
    db.execute("CREATE TABLE functions(id INTEGER PRIMARY KEY, unicorn_verdict TEXT)")
    db.executemany("INSERT INTO functions(unicorn_verdict) VALUES (?)",
                   [("DIVERGENT",)] * 3 + [("EQUIVALENT",)] * 5 + [(None,)] * 7)

    universe = db.execute("SELECT count(*) FROM functions").fetchone()[0]
    scanned = db.execute(
        "SELECT count(*) FROM functions WHERE unicorn_verdict = 'DIVERGENT'"
    ).fetchone()[0]
    nulls = db.execute(
        "SELECT count(*) FROM functions WHERE unicorn_verdict IS NULL"
    ).fetchone()[0]

    assert universe == 15 and scanned == 3
    assert universe - scanned == 12, "12 rows are excluded, not 5"
    assert nulls == 7, "and over half the exclusion is 'never tested', not 'equivalent'"
    # The naive intuition -- "it only drops the EQUIVALENT ones" -- is wrong:
    assert universe - scanned != 5


def test_sync_objdiff_worker_returns_its_own_line_counts():
    """The two bare `continue`s in the JSONL parser now travel back as counts.

    Counting them inside the worker would be the data_symbol_scan race shape
    (CoverageReport is main-thread only), so the worker returns a dict. Assert
    the contract on the SHAPE, which is what the main thread relies on.
    """
    keys = {"stdout_lines", "blank_lines", "malformed_json_lines",
            "unrequested_symbol_lines", "parsed_records"}
    # An empty task list short-circuits before spawning anything.
    results, stats = so.run_batch([], "/nonexistent", jobs=1)
    assert results == [] and stats == {}
    # The worker's own contract: a 2-tuple whose second element carries the keys.
    import inspect
    src = inspect.getsource(so._run_single_batch)
    for k in keys:
        assert k in src, f"worker must account for {k}"


# =========================================================================== #
# find_hidden_work.py
# =========================================================================== #

FUNCTIONS_DDL = """
CREATE TABLE functions (
    id INTEGER PRIMARY KEY,
    symbol TEXT, demangled TEXT, unit TEXT, size INTEGER,
    current_percent REAL, match_percent_normalized REAL,
    verdict TEXT, verdict_reason TEXT, excluded INTEGER DEFAULT 0,
    updated_at TEXT
)
"""


def _db(rows):
    conn = sqlite3.connect(":memory:")
    conn.row_factory = sqlite3.Row
    conn.execute(FUNCTIONS_DDL)
    conn.executemany(
        "INSERT INTO functions(symbol, demangled, unit, size, current_percent,"
        " match_percent_normalized, verdict, excluded) VALUES (?,?,?,?,?,?,?,?)",
        rows)
    return conn


def test_never_measured_complete_rows_are_counted_not_silently_dropped():
    """NEGATIVE CONTROL: `AND current_percent IS NOT NULL`.

    A COMPLETE verdict with no measurement at all is the single most suspicious
    "stale COMPLETE" shape there is, and it was filtered out of every band query
    and out of --demote without a number ever being printed.
    """
    conn = _db([
        ("?a@@YAXXZ", "a", "default/system/char/A", 10, None, None, "COMPLETE", 0),
        ("?b@@YAXXZ", "b", "default/system/char/A", 10, 40.0, 40.0, "COMPLETE", 0),
    ])

    # Real SQLite: the row with NULL satisfies neither `< 80` nor `>= 80`.
    below = conn.execute(
        "SELECT count(*) FROM functions WHERE verdict='COMPLETE' "
        "AND current_percent < 80").fetchone()[0]
    at_or_above = conn.execute(
        "SELECT count(*) FROM functions WHERE verdict='COMPLETE' "
        "AND current_percent >= 80").fetchone()[0]
    assert below == 1 and at_or_above == 0
    assert below + at_or_above == 1, "the NULL row is in NEITHER band"

    stale = fhw.find_stale_verdicts(conn, threshold=80.0, ruler="normalized")
    assert [s["symbol"] for s in stale] == ["?b@@YAXXZ"]
    assert fhw.count_never_measured(conn, "COMPLETE", "normalized") == 1
    assert fhw.count_verdict_pool(conn, "COMPLETE") == 2
    # pool == returned + never-measured + everything at/above threshold
    assert fhw.count_verdict_pool(conn, "COMPLETE") == len(stale) + 1 + 0


def test_the_ruler_decides_whether_a_row_is_a_demotion_candidate():
    """NEGATIVE CONTROL: demoting on the relocation-sensitive column.

    `--demote` sets verdict=NULL. Under the fuzzy ruler a row whose canonical
    score is a clean 100 is a demotion candidate purely because ICF churn moved
    `current_percent`; under the normalized ruler it is not.
    """
    conn = _db([
        ("?icf@@YAXXZ", "icf", "default/system/char/A", 10, 97.0, 100.0, "COMPLETE", 0),
        ("?real@@YAXXZ", "real", "default/system/char/A", 10, 60.0, 60.0, "COMPLETE", 0),
    ])
    fuzzy = {s["symbol"] for s in
             fhw.find_stale_verdicts(conn, threshold=99.0, ruler="fuzzy")}
    norm = {s["symbol"] for s in
            fhw.find_stale_verdicts(conn, threshold=99.0, ruler="normalized")}
    assert fuzzy == {"?icf@@YAXXZ", "?real@@YAXXZ"}
    assert norm == {"?real@@YAXXZ"}
    assert "?icf@@YAXXZ" in fuzzy - norm, \
        "the ruler change is exactly what spares the churned row"


def test_ruler_falls_back_to_current_percent_when_normalized_is_null():
    """20,349 DB rows have no normalized value; they must not vanish."""
    conn = _db([
        ("?x@@YAXXZ", "x", "default/system/char/A", 10, 30.0, None, "COMPLETE", 0),
    ])
    got = fhw.find_stale_verdicts(conn, threshold=80.0, ruler="normalized")
    assert [s["symbol"] for s in got] == ["?x@@YAXXZ"]
    assert got[0]["gate_percent"] == pytest.approx(30.0)
    assert fhw.count_never_measured(conn, "COMPLETE", "normalized") == 0


def test_find_hidden_work_rounding_control():
    """99.9967 must not print as 100.0 inside a "far from 100%" listing."""
    assert f"{99.9967:5.1f}" == "100.0", "the old format string really did lie"
    assert fhw.fmt_pct(99.9967, 1) == "99.9"
    assert fhw.fmt_pct(100.0, 1) == "100.0"
    assert fhw.fmt_pct(None) == "  n/a"


def test_sdk_substring_heuristic_swallows_an_authorable_unit():
    """NEGATIVE CONTROL: the token 'bink' is not a path test.

    `src/system/moviebink/BinkMovieImpl.cpp` is a real, authorable Milo engine
    source. The substring rule classes its unit as third-party SDK and skips it.
    """
    authorable = "default/system/moviebink/BinkMovieImpl"
    third_party = "default/lib/binkxenon/binkread"

    assert fhw.is_sdk_unit(authorable, "substring") is True, \
        "the historical rule really does swallow it"
    assert fhw.is_sdk_unit(authorable, "segment") is False

    # ...and the segment rule is not simply better: it stops recognising
    # binkxenon, because the pattern token is 'bink', not 'binkxenon'.
    assert fhw.is_sdk_unit(third_party, "substring") is True
    assert fhw.is_sdk_unit(third_party, "segment") is False
    # Which is exactly why the default is unchanged and the delta is REPORTED.
    delta = fhw.sdk_classification_delta([authorable, third_party,
                                          "default/system/char/Char"])
    assert delta["sdk_substring"] == 2 and delta["sdk_segment"] == 0
    assert authorable in delta["sdk_only_under_substring"]


def test_missing_implementations_do_not_merge_units_sharing_a_leaf_name(tmp_path):
    """NEGATIVE CONTROL: `unit_name.split('/')[-1]` as an aggregation key.

    64 leaf names cover 138 units in the current report -- including every
    system/synth/FxSend* against its system/synth_xbox/FxSend* twin -- so their
    counts and byte totals cross-contaminated.
    """
    m = {"total_code": 100, "fuzzy_match_percent": 0.0}
    report = _report([
        ("default/system/synth/FxSend", m, [_fn("?a@@YAXXZ", size=10),
                                            _fn("?b@@YAXXZ", size=20)]),
        ("default/system/synth_xbox/FxSend", m, [_fn("?c@@YAXXZ", size=40)]),
    ])
    p = tmp_path / "report.json"
    p.write_text(json.dumps(report))

    missing = fhw.find_missing_implementations(p, "substring")
    assert len(missing) == 3

    from collections import Counter
    by_leaf = Counter(mm["unit_leaf"] for mm in missing)
    by_full = Counter(mm["unit_full"] for mm in missing)
    assert len(by_leaf) == 1 and by_leaf["FxSend"] == 3, \
        "the old leaf key really did merge the two units"
    assert len(by_full) == 2
    assert by_full["default/system/synth/FxSend"] == 2
    assert by_full["default/system/synth_xbox/FxSend"] == 1


def test_missing_implementation_census_declares_its_denominator(tmp_path, capsys):
    """The census must be able to say what it did NOT look at."""
    m = {"total_code": 100, "fuzzy_match_percent": 0.0}
    report = _report([
        ("default/system/char/Char", m, [_fn("?a@@YAXXZ"),
                                         _fn("?b@@YAXXZ", norm=100.0, fuzzy=100.0)]),
        ("default/lib/binkxenon/binkread", m, [_fn("?c@@YAXXZ"), _fn("?d@@YAXXZ")]),
    ])
    p = tmp_path / "report.json"
    p.write_text(json.dumps(report))

    cov = CoverageReport("t", allow_truncation=False)
    missing = fhw.find_missing_implementations(p, "substring", cov)

    assert [mm["symbol"] for mm in missing] == ["?a@@YAXXZ"]
    assert cov.emit() == EXIT_OK
    d = cov.as_dict()
    assert d["universe"] == 4, "the denominator is all four rows, not the two scanned"
    assert d["examined"] == 2
    assert d["dropped"]["sdk-unit-substring-heuristic"] == 2
    out = capsys.readouterr().err
    assert "sdk-unit-substring-heuristic" in out


def test_demote_is_a_no_op_in_dry_run(tmp_path):
    """--dry-run must not write. Assert on the DB, not on the return value."""
    db = tmp_path / "t.db"
    conn = sqlite3.connect(str(db))
    conn.row_factory = sqlite3.Row
    conn.execute(FUNCTIONS_DDL)
    conn.execute("INSERT INTO functions(id, symbol, unit, current_percent,"
                 " match_percent_normalized, verdict, excluded)"
                 " VALUES (1,'?x@@YAXXZ','default/system/char/A',10.0,10.0,'COMPLETE',0)")
    conn.commit()

    n = fhw.demote_functions(conn, [1], dry_run=True)
    assert n == 1, "it still reports what it WOULD have done"
    assert conn.execute("SELECT verdict FROM functions WHERE id=1").fetchone()[0] \
        == "COMPLETE", "but the row is untouched"

    fhw.demote_functions(conn, [1], dry_run=False)
    assert conn.execute("SELECT verdict FROM functions WHERE id=1").fetchone()[0] is None
