"""Negative controls for the five census scanners audited on 2026-08-19.

Written to the standard of `test_coverage.py`: every test RECONSTRUCTS the false
negative and asserts the check now fires.  The rule that governs this file is
the one that let a real error through once already --

    never assert a synthesised value against a constant you wrote in the same
    sitting

-- so nothing below compares a number to a literal that only this file knows.
Each assertion is one of:

  (a) an ARITHMETIC IDENTITY the scanner must satisfy (universe == examined +
      drops), which no fixture value can make vacuously true;
  (b) a DISAGREEMENT between two independent implementations (the historical
      selector vs the current one, or the same input fed in two orders), where
      the interesting claim is that they differ / no longer differ;
  (c) a cross-check against a REAL artifact on disk (build/373307D9/report.json,
      the built objects), recomputed here by a different expression than the one
      under test.

The scanners and the defect each control covers:

  name_charge_census   `F(f.get("fuzzy_match_percent"))` turned an ABSENT field
                       into 0.0 and the next line dropped it as `fz <= 0.0`.
                       16,780 rows / 5,129,540 B, against 2,238 examined.
  scope_index_census   `our[fn][name] = scope` was last-write-wins over an
                       unsorted glob, so a name declared in several scopes kept
                       one arbitrary reading.
  scope_index_census   `subprocess.run(['strings', ...])` with no returncode
                       test: a broken `strings` reads as "no skew".
  frame_deficit_census `p > prev[0]` raised TypeError when a duplicate symbol's
                       first occurrence had no percent; `--limit` truncated
                       BEFORE the row count and the histogram.
  report_absent_census rows whose UNIT is absent from report.json were dropped
                       by the join guard and never counted as their own class.
"""
from __future__ import annotations

import json
import os
import shutil
import sqlite3
import subprocess
import sys

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))))
if REPO not in sys.path:
    sys.path.insert(0, REPO)

from scripts.analysis.coverage import EXIT_OK, EXIT_UNACCOUNTED  # noqa: E402
from scripts.analysis import frame_deficit_census as fdc  # noqa: E402
from scripts.analysis import name_charge_census as ncc  # noqa: E402
from scripts.analysis import scope_index_census as sic  # noqa: E402

REPORT_JSON = os.path.join(REPO, "build", "373307D9", "report.json")
OBJ_SRC_DIR = os.path.join(REPO, "build", "373307D9", "src")
OBJDIFF_JSON = os.path.join(REPO, "objdiff.json")

needs_report = pytest.mark.skipif(
    not os.path.exists(REPORT_JSON),
    reason="needs a built tree: build/373307D9/report.json")
needs_objects = pytest.mark.skipif(
    not os.path.isdir(OBJ_SRC_DIR) or not os.path.exists(OBJDIFF_JSON),
    reason="needs a built tree: build/373307D9/src + objdiff.json")


# =========================================================================== #
# NEGATIVE CONTROL 1 — name_charge_census, the missing-field-as-zero fold.
#
# The historical selector, verbatim in shape:
#
#     def F(x, d=0.0): return d if x is None else float(x)
#     fz = F(f.get("fuzzy_match_percent"))
#     if fz >= 100.0 or fz <= 0.0 or nm.startswith(("fn_", "lbl_")):
#         continue
#
# A row that objdiff never scored (because we define no body for it) has NO
# `fuzzy_match_percent` key at all. F() made it 0.0, the `<= 0.0` arm ate it,
# and the summary printed three numerators with no denominator.
# =========================================================================== #

def _legacy_select(units):
    """The pre-2026-08-19 selector. Returns the kept keys AND nothing else --
    which is precisely the problem: it has no way to report what it dropped."""
    def F(x, d=0.0):
        return d if x is None else float(x)

    want = {}
    for u in units:
        for f in (u.get("functions") or []):
            fz = F(f.get("fuzzy_match_percent"))
            nm = f.get("name", "")
            if fz >= 100.0 or fz <= 0.0 or nm.startswith(("fn_", "lbl_")):
                continue
            want[(u["name"], nm)] = int(f.get("size") or 0)
    return want


def _fixture_report():
    """Rows in the four shapes report.json really contains."""
    return {"units": [{"name": "default/u1", "functions": [
        # tier 1: we wrote a body and it scores
        {"name": "?a@@YAXXZ", "fuzzy_match_percent": 42.0, "size": 100},
        {"name": "?b@@YAXXZ", "fuzzy_match_percent": 99.5, "size": 200},
        {"name": "?done@@YAXXZ", "fuzzy_match_percent": 100.0, "size": 300},
        # tier 2: NO body of ours -> objdiff emits no fuzzy_match_percent.
        # Only `match_percent_normalized` is present, exactly as on disk.
        {"name": "?nobody@@YAXXZ", "match_percent_normalized": 0.0, "size": 400},
        {"name": "?nobody2@@YAXXZ", "match_percent_normalized": 0.0, "size": 500},
        # a genuine 0.0 on the graded ruler -- distinct from "absent"
        {"name": "?zero@@YAXXZ", "fuzzy_match_percent": 0.0, "size": 600},
        # dtk placeholder names, both with and without a score
        {"name": "fn_82001234", "fuzzy_match_percent": 12.0, "size": 700},
        {"name": "lbl_82005678", "match_percent_normalized": 0.0, "size": 800},
    ]}]}


class _Args:
    allow_truncation = False
    coverage_json = None


def test_absent_fuzzy_field_is_counted_not_folded_into_zero():
    rep = _fixture_report()
    rows = rep["units"][0]["functions"]

    # (b) The two implementations must disagree about the SAME input, and the
    # size of the disagreement is the population the old one could not name.
    cov = ncc.CoverageReport("name_charge_census", args=_Args())
    want = ncc.select_rows(rep, "", cov)
    legacy = _legacy_select(rep["units"])
    assert want == legacy, ("the fix must not change WHICH rows are examined -- "
                            "only whether the discards are counted")

    # (a) The arithmetic identity. No fixture value can make this vacuous.
    # `select_rows` only SELECTS; `collect_rows` calls cov.examine() per row it
    # actually re-diffs, so the selector's own identity is
    #     universe == selected + dropped.
    d = cov.as_dict()
    assert d["universe"] == len(rows)
    assert d["universe"] == len(want) + d["dropped_total"]
    cov.examine(len(want))            # stand in for the objdiff pass
    assert cov.unaccounted == 0
    assert cov.emit() == EXIT_OK

    # (c) The counter, not the output text: rows with the key genuinely absent,
    # recomputed here by a different expression than the one under test.
    absent_named = sum(1 for f in rows
                       if "fuzzy_match_percent" not in f
                       and not f["name"].startswith(("fn_", "lbl_")))
    absent_phantom = sum(1 for f in rows
                         if "fuzzy_match_percent" not in f
                         and f["name"].startswith(("fn_", "lbl_")))
    assert d["dropped"]["no-fuzzy-score"] == absent_named
    assert d["dropped"]["no-fuzzy-score-phantom-name"] == absent_phantom
    assert absent_named > 0, "the fixture must actually contain the shape"

    # And the real 0.0 is now a class of its own rather than sharing a bucket
    # with "the field was missing" -- the collision that hid the 16,780.
    assert d["dropped"]["graded-score-is-zero"] == sum(
        1 for f in rows if f.get("fuzzy_match_percent") == 0.0)


def test_the_legacy_selector_could_not_have_reported_the_hole():
    """Reconstruct the false negative: the old code has no counter to check.

    Run the old selector inside a CoverageReport that declares the same
    universe and counts only what the old code kept. The arithmetic check fires
    on its own, without anyone having anticipated the missing-key shape --
    which is the whole point of the convention.
    """
    rep = _fixture_report()
    rows = rep["units"][0]["functions"]
    cov = ncc.CoverageReport("name_charge_census/legacy", args=_Args())
    cov.universe(len(rows), "function rows in report.json")
    cov.examine(len(_legacy_select(rep["units"])))
    assert cov.emit() == EXIT_UNACCOUNTED
    assert cov.unaccounted == len(rows) - len(_legacy_select(rep["units"]))
    assert cov.unaccounted > 0


@needs_report
def test_missing_fuzzy_population_matches_the_real_report():
    """(c) Cross-check the counter against the artifact, not against a literal.

    The count is recomputed here straight off report.json with an expression
    that shares no code with the scanner's selector.
    """
    rep = json.loads(open(REPORT_JSON).read())
    cov = ncc.CoverageReport("name_charge_census", args=_Args())
    want = ncc.select_rows(rep, "", cov)
    d = cov.as_dict()

    all_fns = [f for u in rep["units"] for f in (u.get("functions") or [])]
    independently = sum(1 for f in all_fns
                        if f.get("fuzzy_match_percent") is None
                        and not f.get("name", "").startswith(("fn_", "lbl_")))
    assert d["dropped"]["no-fuzzy-score"] == independently
    assert d["universe"] == len(all_fns) == len(want) + d["dropped_total"]
    # The hole must be large enough to matter; if this ever reads 0 the tree has
    # changed shape and the docstring numbers need re-measuring, not the test.
    assert independently > 1000, (
        f"only {independently} unscored rows -- re-measure the docstring")


# =========================================================================== #
# NEGATIVE CONTROL 2 — scope_index_census, last-write-wins over an unsorted glob.
#
# One enclosing function legitimately declares the same generated name (`_s`,
# `_t`, `$S3`) in many scopes: every MILO_ASSERT contributes another `_s`.
# `our[fn][name] = scope` kept ONE of them, chosen by enumeration order.
# =========================================================================== #

def _mangle(name, scope_digit, enclosing):
    """`?<name>@?<d>??<enclosing>@4<type>A` -- the shape scope_index parses."""
    return f"?{name}@?{scope_digit}??{enclosing}@4VSymbol@@A"


ENCLOSING = "?Handle@ObjectDir@@UAA?AVDataNode@@PAVDataArray@@_N@Z"
# Two readings of the SAME (function, name) at different scopes -- the real
# shape: `_s` at scope 3 and again at scope 7, two asserts apart.
READING_A = _mangle("_s", "2", ENCLOSING)     # digit 2 decodes to 3
READING_B = _mangle("_s", "6", ENCLOSING)     # digit 6 decodes to 7
# The map key is whatever the parser itself calls the enclosing function -- taken
# FROM the parser rather than re-spelled here, so the test cannot pass by
# agreeing with a constant this file invented.
FN_KEY = sic.parse(READING_A)[2]
assert sic.parse(READING_B)[2] == FN_KEY
assert sic.parse(READING_A)[1] != sic.parse(READING_B)[1]


def _last_write_wins(order):
    """The pre-2026-08-19 storage, verbatim in shape."""
    import collections
    m = collections.defaultdict(dict)
    for text in order:
        r = sic.parse(text)
        if r:
            m[r[2]][r[0]] = r[1]
    return {fn: dict(v) for fn, v in m.items()}


def test_conflicting_scopes_were_order_dependent_and_now_are_not():
    forward = _last_write_wins([READING_A, READING_B])
    reverse = _last_write_wins([READING_B, READING_A])

    # (b) Reconstruct the false negative: the SAME two facts, two orders, two
    # different answers. Nothing here is compared to a constant.
    assert forward != reverse, (
        "the historical storage must be shown to be order-dependent, or this "
        "test is not a control")
    assert forward[FN_KEY]["_s"] != reverse[FN_KEY]["_s"]

    # The fix: order-independent, and lossless.
    fwd = sic.ScopeIndex("t")
    for t in (READING_A, READING_B):
        fwd.feed(t)
    rev = sic.ScopeIndex("t")
    for t in (READING_B, READING_A):
        rev.feed(t)
    assert fwd.as_json() == rev.as_json()
    assert fwd.map[FN_KEY]["_s"] == {sic.parse(READING_A)[1],
                                    sic.parse(READING_B)[1]}

    # ...and the collision is REPORTED rather than absorbed.
    assert fwd.multivalued == 1
    assert fwd.readings == 2 and fwd.pairs == 1
    assert fwd.readings_beyond_first == 1, (
        "the count of readings a scalar map would have discarded")
    assert fwd.stats()["multivalued_pairs"] == 1


def test_a_name_with_one_scope_is_not_reported_as_a_collision():
    """Control for the control: the collision counter must not fire on clean input."""
    idx = sic.ScopeIndex("t")
    idx.feed(READING_A)
    idx.feed(_mangle("_t", "2", ENCLOSING))
    assert idx.multivalued == 0
    assert idx.readings_beyond_first == 0
    assert idx.pairs == 2


def test_an_unrecognised_local_static_shape_is_counted_not_dropped():
    """A future mangling must not vanish: it lands in shaped_but_unparsed."""
    idx = sic.ScopeIndex("t")
    assert idx.feed(READING_A) is True
    # local-static SHAPED (`?name@?...??enclosing`) but with a scope token the
    # strict NAME regex rejects -- the shape a new compiler spelling would take.
    assert idx.feed("?_s@?ZZZ@??Foo@@YAXXZ@4VSymbol@@A") is False
    assert idx.feed("?NotALocalStatic@Cls@@QAEXXZ") is False   # not shaped at all
    assert idx.unparsed_shaped == 1, "the unknown shape must be COUNTED"
    assert idx.stats()["shaped_but_unparsed"] == 1
    assert idx.considered == 3 and idx.parsed == 1


def test_glob_results_are_sorted_before_use(tmp_path):
    """The unsorted-glob half of the same defect, on a directory readdir does
    not hand back in order."""
    objdir = tmp_path / "obj"
    (objdir / "z").mkdir(parents=True)
    (objdir / "a").mkdir(parents=True)
    for sub, n in (("z", "z1.obj"), ("a", "a1.obj"), ("z", "z0.obj")):
        (objdir / sub / n).write_text(READING_A + "\n")
    idx = sic.ScopeIndex("ours")
    objs, failures = sic.load_ours(str(objdir), idx, shutil.which("strings"))
    assert failures == []
    assert objs == sorted(objs), "load_ours must not inherit readdir order"


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 3 — a broken `strings` must not read as "no skew".
# --------------------------------------------------------------------------- #

def _tiny_project(tmp_path):
    """A project with one target local static and one object that holds it."""
    cfg = tmp_path / "config" / "373307D9"
    cfg.mkdir(parents=True)
    (cfg / "symbols.txt").write_text(f"{READING_A} = .text:0x82000000;\n")
    src = tmp_path / "build" / "373307D9" / "src"
    src.mkdir(parents=True)
    # `strings` on a text file returns its text, so this stands in for a COFF
    # object without needing one.
    (src / "u.obj").write_text(READING_A + "\n")
    return tmp_path


def _run_scope(tmp_path, *extra):
    return subprocess.run(
        [sys.executable, os.path.join(REPO, "scripts", "analysis",
                                      "scope_index_census.py"),
         "--project", str(tmp_path), *extra],
        capture_output=True, text=True)


def test_healthy_strings_run_reports_a_match(tmp_path):
    r = _run_scope(_tiny_project(tmp_path))
    assert r.returncode == EXIT_OK, r.stderr
    assert "match=1 diff=0 target-only=0" in r.stdout, r.stdout


def test_failing_strings_is_loud_and_nonzero(tmp_path):
    """Reconstruct the false negative: with `strings` broken the tool used to
    print `match=0 diff=0 target-only=N` and exit 0, which reads as no skew."""
    proj = _tiny_project(tmp_path)
    healthy = _run_scope(proj)
    broken = _run_scope(proj, "--strings-bin", "false")

    assert healthy.returncode == EXIT_OK
    assert broken.returncode == sic.EXIT_TOOL_FAILURE
    assert broken.returncode != healthy.returncode, (
        "a run that read NOTHING must not exit like a run that read everything")
    assert "FAILED" in broken.stderr
    # the count of failed objects must be stated, not merely admitted
    assert "1 of 1 objects" in broken.stderr, broken.stderr


def test_missing_strings_binary_is_fatal(tmp_path):
    r = _run_scope(_tiny_project(tmp_path), "--strings-bin", "no_such_bin_xyz")
    assert r.returncode == sic.EXIT_TOOL_FAILURE
    assert "not found on PATH" in r.stderr


# =========================================================================== #
# NEGATIVE CONTROL 4 — frame_deficit_census.
# =========================================================================== #

def test_duplicate_symbol_whose_first_percent_is_none_no_longer_raises(tmp_path):
    """`p > prev[0]` raised TypeError whenever the FIRST occurrence of a
    duplicated symbol carried no percent. Latent, and ordering-dependent."""
    rep = {"units": [
        {"name": "u1", "functions": [
            {"name": "?dup@@YAXXZ", "match_percent_normalized": None,
             "fuzzy_match_percent": None, "size": 10}]},
        {"name": "u2", "functions": [
            {"name": "?dup@@YAXXZ", "match_percent_normalized": 55.0,
             "size": 10}]},
    ]}
    build = tmp_path / "build" / "373307D9"
    build.mkdir(parents=True)
    (build / "report.json").write_text(json.dumps(rep))

    # The historical comparison, reconstructed: it must really blow up, or this
    # is not a control.
    with pytest.raises(TypeError):
        prev = (None, "u1", 10)
        _ = 55.0 > prev[0]

    pct = fdc.load_report(str(tmp_path))
    # A real number wins over None regardless of which came first.
    assert pct["?dup@@YAXXZ"][0] == 55.0
    rev = {"units": list(reversed(rep["units"]))}
    (build / "report.json").write_text(json.dumps(rev))
    assert fdc.load_report(str(tmp_path))["?dup@@YAXXZ"][0] == 55.0


def test_unreadable_report_is_fatal_unless_opted_into(tmp_path):
    """It used to `except Exception: return pct`, silently turning
    --min-percent/--max-percent into no filter at all."""
    with pytest.raises(SystemExit) as e:
        fdc.load_report(str(tmp_path))
    assert "--allow-missing-report" in str(e.value)
    assert fdc.load_report(str(tmp_path), allow_missing=True) == {}


@needs_objects
def test_limit_does_not_rewrite_the_counts_it_prints_above():
    """`rows = rows[:args.limit]` ran BEFORE the `len(rows)` line and before the
    histogram, so --limit silently changed the totals it appeared beneath."""
    def run(*extra):
        return subprocess.run(
            [sys.executable, os.path.join(REPO, "scripts", "analysis",
                                          "frame_deficit_census.py"),
             "--project-dir", REPO, "--all", *extra],
            capture_output=True, text=True, cwd=REPO)

    full = run()
    capped = run("--limit", "3")
    assert full.returncode == EXIT_OK, full.stderr[-2000:]
    assert capped.returncode == EXIT_OK

    import re as _re
    HIST = _re.compile(r"^\s*[+-]\d+: \d+$")

    def summary(out):
        """The row COUNT line and the histogram -- the two things --limit used
        to rewrite from beneath. Deliberately excludes the row listing."""
        return [ln for ln in out.splitlines()
                if ln.startswith("# scanned") or HIST.match(ln)]

    assert summary(full.stdout) == summary(capped.stdout), (
        "the row count and the delta histogram must be identical with and "
        "without --limit")
    # ...and the LIST really is shorter, or the test proves nothing.
    def n_rows(out):
        return sum(1 for ln in out.splitlines() if "0x" in ln and "[" in ln)
    assert n_rows(capped.stdout) == 3
    assert n_rows(full.stdout) > 3
    assert "showing 3 of" in capped.stdout


@needs_objects
def test_frame_census_coverage_balances_on_the_real_tree(tmp_path):
    """(a) The arithmetic identity, on the real population rather than a fixture."""
    cov_path = tmp_path / "cov.json"
    r = subprocess.run(
        [sys.executable, os.path.join(REPO, "scripts", "analysis",
                                      "frame_deficit_census.py"),
         "--project-dir", REPO, "--coverage-json", str(cov_path), "--all"],
        capture_output=True, text=True, cwd=REPO)
    assert r.returncode == EXIT_OK, r.stderr[-2000:]
    d = json.loads(cov_path.read_text())
    assert d["universe"] == d["examined"] + d["dropped_total"]
    assert d["unaccounted"] == 0
    assert d["complete"] is True
    # The populations the tool never looked at must be NAMED, and large.
    assert d["dropped"]["our-frame-unreadable"] > 0
    assert d["dropped"]["no-target-counterpart"] > 0
    assert d["units_coverage"]["universe"] > d["units_coverage"]["examined"]


# =========================================================================== #
# NEGATIVE CONTROL 5 — report_absent_census: a DB row whose UNIT is not in
# report.json at all was dropped by `runits.get(r[1]) is not None` and counted
# nowhere. It is a different class from "the unit is there, the symbol is not",
# and it is the one that would indicate a real build/pairing defect.
# =========================================================================== #

def _absent_fixture(tmp_path, unit_in_report):
    db_path = tmp_path / ("d_in.db" if unit_in_report else "d_out.db")
    db = sqlite3.connect(db_path)
    db.execute("CREATE TABLE functions(id INTEGER PRIMARY KEY, unit TEXT, "
               "symbol TEXT, size INT, verdict TEXT, current_percent REAL, "
               "attempt_count INT, excluded INT)")
    db.executemany(
        "INSERT INTO functions VALUES (?,?,?,?,?,?,?,?)",
        [(1, "default/known", "?present@@YAXXZ", 4, "COMPLETE", 100.0, 1, 0),
         (2, "default/vanished", "?orphan@@YAXXZ", 8, "AT_LIMIT", 90.0, 3, 0)])
    db.commit()
    db.close()

    units = [{"name": "default/known",
              "functions": [{"name": "?present@@YAXXZ"}]}]
    if unit_in_report:
        units.append({"name": "default/vanished",
                      "functions": [{"name": "?orphan@@YAXXZ"}]})
    (tmp_path / "report.json").write_text(json.dumps({"units": units}))
    (tmp_path / "objdiff.json").write_text(json.dumps({"units": [
        {"name": u["name"]} for u in units]}))
    return db_path


def _run_absent(tmp_path, db_path):
    return subprocess.run(
        [sys.executable, os.path.join(REPO, "scripts", "analysis",
                                      "report_absent_census.py"),
         "--db", str(db_path),
         "--report", str(tmp_path / "report.json"),
         "--objdiff", str(tmp_path / "objdiff.json"),
         "--project-dir", REPO,
         "--coverage-json", str(tmp_path / "cov.json")],
        capture_output=True, text=True, cwd=REPO)


def test_row_whose_unit_is_absent_from_the_report_is_its_own_counted_class(tmp_path):
    db_path = _absent_fixture(tmp_path, unit_in_report=False)
    r = _run_absent(tmp_path, db_path)
    assert r.returncode == EXIT_OK, r.stderr[-2000:]
    d = json.loads((tmp_path / "cov.json").read_text())

    # (a) arithmetic identity
    assert d["universe"] == d["examined"] + d["dropped_total"]
    assert d["unaccounted"] == 0
    # (c) the counter: the orphan row is NAMED, not merely absent from a total.
    assert d["dropped"]["unit-absent-from-report"] == 1
    assert "unit-absent-from-report" in r.stderr
    assert d["examined"] == 0

    # (b) the same DB with the unit present must classify it differently -- the
    # two runs disagreeing is what proves the class is load-bearing.
    db2 = _absent_fixture(tmp_path, unit_in_report=True)
    r2 = _run_absent(tmp_path, db2)
    assert r2.returncode == EXIT_OK, r2.stderr[-2000:]
    d2 = json.loads((tmp_path / "cov.json").read_text())
    assert d2["dropped"].get("unit-absent-from-report", 0) == 0
    assert d2["dropped"]["present-in-report"] == 2


def test_phantom_name_exclusion_is_counted_even_though_it_is_deliberate(tmp_path):
    db_path = tmp_path / "p.db"
    db = sqlite3.connect(db_path)
    db.execute("CREATE TABLE functions(id INTEGER PRIMARY KEY, unit TEXT, "
               "symbol TEXT, size INT, verdict TEXT, current_percent REAL, "
               "attempt_count INT, excluded INT)")
    db.executemany("INSERT INTO functions VALUES (?,?,?,?,?,?,?,?)",
                   [(1, "default/known", "merged_82331360", 4, None, None, 0, 0),
                    (2, "default/known", "fn_82001234", 4, None, None, 0, 0)])
    db.commit()
    db.close()
    (tmp_path / "report.json").write_text(json.dumps(
        {"units": [{"name": "default/known", "functions": []}]}))
    (tmp_path / "objdiff.json").write_text(json.dumps(
        {"units": [{"name": "default/known"}]}))

    r = _run_absent(tmp_path, db_path)
    assert r.returncode == EXIT_OK, r.stderr[-2000:]
    d = json.loads((tmp_path / "cov.json").read_text())
    assert d["universe"] == d["examined"] + d["dropped_total"]
    assert d["dropped"]["phantom-name-shape"] == 2
    assert d["examined"] == 0
