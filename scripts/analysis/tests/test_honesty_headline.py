"""Negative controls for the three HEADLINE scanners.

`test_coverage.py` proves the contract module works. This file proves the three
scanners that produce QUOTED NUMBERS obey it — `remaining_work.py`
("## Remaining Work: 140 functions"), `ceiling_calculator.py` ("ceiling 87.3%",
"Effective completion"), and `reclassify_at_limit.py` ("1517 candidates", which
also WRITES those verdicts back into decomp.db).

Every test below reconstructs the false negative and asserts the check fires.
The standing rule from `test_coverage.py` applies and is the reason several
assertions look roundabout:

    NEVER assert a synthesised value against a constant you wrote in the same
    sitting. That is a tautology, and this project has already shipped one that
    let a real error through.

So expected values are recomputed from the FIXTURE by an independent route —
count the function dicts, count the NULL rows, ask SQLite itself — never typed
in as a literal next to the code that produces them.

NOTHING HERE TOUCHES THE REAL decomp.db. Every database is an in-memory or
tmp_path SQLite built by the test.
"""
from __future__ import annotations

import json
import os
import re
import sqlite3
import subprocess
import sys
from pathlib import Path

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))))

import scripts.analysis.remaining_work as RW
import scripts.analysis.ceiling_calculator as CC
from scripts.analysis.coverage import CoverageReport, EXIT_OK, EXIT_TRUNCATED


# --------------------------------------------------------------------------- #
# Fixture builders
# --------------------------------------------------------------------------- #

def _func(name: str, size: int, pct):
    """One report.json function row. `pct=None` == objdiff emitted no score."""
    d = {"name": name, "size": size, "metadata": {"demangled_name": name + "()"}}
    if pct is not None:
        d["fuzzy_match_percent"] = pct
    return d


def _unit(name: str, funcs, complete=True, source_path=None):
    return {
        "name": name,
        "metadata": {"complete": complete,
                     "source_path": source_path or f"src/{name}.cpp"},
        "functions": funcs,
    }


def _write_report(tmp_path: Path, units) -> str:
    p = tmp_path / "report.json"
    p.write_text(json.dumps({"units": units}))
    return str(p)


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 1 — remaining_work's double-counted denominator.
#
# `total = done + partial + len(stubs)` while `stubs` ALREADY contains every
# partial. On the real report at `--max-percent 95` that inflated 216 of 218
# units and moved 58 priority labels. Build a unit where `partial` is non-empty
# and check the reported total against the number of DISTINCT functions in the
# fixture — obtained by counting the fixture's own function list, which is a
# different computation from anything the scanner does.
# --------------------------------------------------------------------------- #

PARTIAL_UNIT_FUNCS = [
    _func("done_a", 100, 99.0),
    _func("done_b", 100, 98.0),
    _func("done_c", 100, 100.0),
    _func("partial_a", 300, 40.0),      # scored, below --max-percent
    _func("partial_b", 300, 70.0),
    _func("stub_a", 200, None),         # objdiff emitted NO score
    _func("stub_b", 200, 0.0),          # objdiff scored it exactly zero
]


def _analyze(tmp_path, units, **kw):
    cov = CoverageReport("remaining_work", allow_truncation=False)
    data = RW.analyze_report(_write_report(tmp_path, units), cov=cov, **kw)
    return data, cov


def test_unit_total_equals_the_distinct_function_count(tmp_path):
    units = [_unit("system/fake/Widget", PARTIAL_UNIT_FUNCS)]
    data, cov = _analyze(tmp_path, units, min_bytes=0, max_percent=95.0)
    info = data["categories"]["system/fake"]["units"][0][1]

    # Independent route to the truth: how many function rows does the FIXTURE
    # contain? Nothing below is a literal transcription of the scanner's math.
    truth = len(units[0]["functions"])
    assert info["total"] == truth, (
        f"reported total {info['total']} != {truth} distinct functions in the unit")
    assert info["done"] + info["stubs"] == truth

    # ...and the historical formula really did over-count, by exactly the size
    # of the partial bucket. If this ever stops holding, the control is vacuous.
    assert info["total_legacy"] == truth + info["partial"]
    assert info["partial"] > 0, "fixture must exercise the partial bucket"
    assert info["total_legacy"] > info["total"]

    assert cov.unaccounted == 0
    assert cov.emit() == EXIT_OK


# A unit sized to straddle a `priority_label` boundary: 13 done + 8 partial.
# True total 21 -> 61.9% -> "Medium"; double-counted total 29 -> 44.8% -> "Large
# gap". The boundary itself is read from priority_label(), never hardcoded here.
LABEL_MOVING_FUNCS = (
    [_func(f"done_{i}", 40, 99.0) for i in range(13)]
    + [_func(f"partial_{i}", 120, 55.0) for i in range(8)]
)


def test_double_count_moves_the_priority_label(tmp_path, capsys):
    """The inflated denominator is not cosmetic: it mislabels units."""
    units = [_unit("system/fake/Widget", LABEL_MOVING_FUNCS)]
    data, _ = _analyze(tmp_path, units, min_bytes=0, max_percent=95.0)
    info = data["categories"]["system/fake"]["units"][0][1]

    assert info["pct_done"] > info["pct_done_legacy"], \
        "the fix must raise pct_done, never lower it"
    assert RW.priority_label(info["pct_done"]) != RW.priority_label(info["pct_done_legacy"]), \
        "fixture must be built so the label actually moves"
    assert info["label_changed_by_fix"] is True
    # And the tool must SAY how many units it moved rather than silently moving them.
    md = RW.format_markdown(data)
    assert "Denominator fix" in md
    assert f"{data['funnel']['units_relabelled_by_total_fix']:,}" in md


def test_absent_fuzzy_percent_is_not_reported_as_scored_zero(tmp_path):
    """`pct is None` (no objdiff score) vs `pct == 0.0` (scored zero) must differ.

    Both are remaining work and both still land in the same bucket — that part
    was right. What was wrong is that the output printed them identically as
    `0.0%`, so "we never wrote this" and "we wrote it and it matches nothing"
    were indistinguishable in every report this tool ever produced.
    """
    units = [_unit("system/fake/Widget", PARTIAL_UNIT_FUNCS)]
    data, _ = _analyze(tmp_path, units, min_bytes=0, max_percent=95.0)
    info = data["categories"]["system/fake"]["units"][0][1]

    # Independent count from the fixture: rows with no fuzzy_match_percent key.
    truth_unscored = sum(1 for f in units[0]["functions"]
                         if "fuzzy_match_percent" not in f)
    truth_zero = sum(1 for f in units[0]["functions"]
                     if f.get("fuzzy_match_percent") == 0.0)
    assert info["unscored"] == truth_unscored
    assert info["scored_zero"] == truth_zero
    assert truth_unscored and truth_zero, "fixture must contain both shapes"

    syms = RW.format_symbols(data)
    assert "[no objdiff score]" in syms
    assert "[0.0%]" in syms


def test_skiplist_and_min_bytes_removals_are_in_the_headline(tmp_path):
    """The `data_symbol_scan` shape: a filter nobody mentions.

    Reconstruct a report where the hardcoded skip list and the byte threshold
    each remove a unit, and assert the headline names both costs. The expected
    counts are summed from the fixture, not typed.
    """
    skipped = _unit("system/synth_xbox/Thing",           # SKIP_SUBSYSTEMS
                    [_func("s1", 4000, None), _func("s2", 4000, None)])
    tiny = _unit("system/fake/Tiny", [_func("t1", 8, None)])   # below --min-bytes
    kept = _unit("system/fake/Big", [_func("k1", 9000, None), _func("k2", 10, 100.0)])
    data, cov = _analyze(tmp_path, [skipped, tiny, kept], min_bytes=500,
                         max_percent=0.0)
    f = data["funnel"]

    assert f["skipped_stubs"] == len(skipped["functions"])
    assert f["skipped_bytes"] == sum(x["size"] for x in skipped["functions"])
    assert f["below_min_stubs"] == len(tiny["functions"])
    assert f["below_min_bytes"] == sum(x["size"] for x in tiny["functions"])
    # The pool is the sum of the three legs, computed here from the fixture.
    assert f["pool_stubs"] == (f["skipped_stubs"] + f["below_min_stubs"]
                               + f["reported_stubs"])

    md = RW.format_markdown(data)
    assert "**Denominator:**" in md
    assert f"{f['pool_stubs']:,} remaining-work functions" in md
    assert f"| {f['skipped_units']:,} | {f['skipped_stubs']:,} |" in md
    assert f"| {f['below_min_units']:,} | {f['below_min_stubs']:,} |" in md

    # Coverage arithmetic must balance against the raw function-row count.
    truth_rows = sum(len(u["functions"]) for u in (skipped, tiny, kept))
    assert cov.as_dict()["universe"] == truth_rows
    assert cov.unaccounted == 0
    assert cov.emit() == EXIT_OK


def test_incomplete_units_are_dropped_but_counted(tmp_path):
    incomplete = _unit("system/fake/NotDone", [_func("x", 100, None)], complete=False)
    kept = _unit("system/fake/Big", [_func("k1", 9000, None)])
    data, cov = _analyze(tmp_path, [incomplete, kept], min_bytes=0, max_percent=0.0)
    d = cov.as_dict()
    assert d["dropped"]["unit-not-complete"] == len(incomplete["functions"])
    assert cov.unaccounted == 0
    assert data["funnel"]["units_not_complete"] == 1


def test_near_complete_display_slice_declares_its_residual(tmp_path):
    """`near_complete[:15]` was an unlabelled slice — a sample rendered as a list."""
    funcs_per_unit = 12
    units = []
    for i in range(20):
        fs = [_func(f"d{i}_{j}", 50, 100.0) for j in range(funcs_per_unit)]
        fs.append(_func(f"stub{i}", 600 + i, None))
        units.append(_unit(f"system/fake/U{i:02d}", fs))
    data, _ = _analyze(tmp_path, units, min_bytes=0, max_percent=0.0)

    md = RW.format_markdown(data, max_near_complete=5)
    eligible = [1 for ci in data["categories"].values() for _, info in ci["units"]
                if info["pct_done"] > 85 and info["stubs"] <= 20]
    assert len(eligible) > 5, "fixture must overflow the display slice"
    assert f"*+{len(eligible) - 5} more*" in md


def test_remaining_work_is_deterministic(tmp_path):
    units = [_unit(f"system/fake/U{i}", [_func(f"s{i}", 700, None)]) for i in range(8)]
    report = _write_report(tmp_path, units)

    def run():
        cov = CoverageReport("remaining_work")
        data = RW.analyze_report(report, min_bytes=0, max_percent=0.0, cov=cov)
        return RW.format_markdown(data), RW.format_json(data, include_symbols=True)

    a_md, a_js = run()
    b_md, b_js = run()
    assert a_md == b_md
    assert a_js == b_js


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 2 — a NULL `current_percent` must land in a COUNTED bucket.
#
# `WHERE current_percent >= ? AND current_percent <= ?` evaluates to NULL, not
# false, for a NULL column — so the row is neither selected nor mentioned. On
# the live DB that is 1,231 of 3,796 AT_LIMIT rows. Assert against real SQLite
# semantics (as test_coverage.py does for LIKE), then assert the rewritten path
# accounts for them.
# --------------------------------------------------------------------------- #

FUNCS_DDL = """
CREATE TABLE functions (
    id INTEGER PRIMARY KEY,
    symbol TEXT NOT NULL,
    demangled TEXT,
    unit TEXT,
    current_percent REAL,
    verdict TEXT,
    verdict_reason TEXT,
    excluded INTEGER DEFAULT 0,
    updated_at TIMESTAMP
)
"""

# (symbol, demangled, unit, current_percent, excluded)
BAND_ROWS = [
    ("?a@C@@QAEXXZ", "void C::a(void)", "src/u1.cpp", 91.0, 0),
    ("?b@C@@QAEXXZ", "void C::b(void)", "src/u1.cpp", 99.95, 0),   # above --max-pct
    ("?c@C@@QAEXXZ", "void C::c(void)", "src/u1.cpp", None, 0),    # THE NULL ROW
    ("?d@C@@QAEXXZ", "void C::d(void)", "src/u1.cpp", None, 0),    # and another
    ("?e@C@@QAEXXZ", "void C::e(void)", "src/u1.cpp", 12.0, 1),    # excluded=1
]


def _build_db(path: Path):
    db = sqlite3.connect(str(path))
    db.execute(FUNCS_DDL)
    db.executemany(
        "INSERT INTO functions(symbol, demangled, unit, current_percent, verdict, excluded) "
        "VALUES (?,?,?,?,'AT_LIMIT',?)", BAND_ROWS)
    db.commit()
    db.close()


def test_sql_band_really_does_swallow_null_rows(tmp_path):
    """Demonstrate the defect with SQLite itself before asserting the fix."""
    p = tmp_path / "d.db"
    _build_db(p)
    db = sqlite3.connect(str(p))
    total = db.execute("SELECT COUNT(*) FROM functions WHERE verdict='AT_LIMIT'").fetchone()[0]
    banded = db.execute(
        "SELECT COUNT(*) FROM functions WHERE verdict='AT_LIMIT' "
        "AND current_percent >= 0 AND current_percent <= 99.9").fetchone()[0]
    nulls = db.execute(
        "SELECT COUNT(*) FROM functions WHERE verdict='AT_LIMIT' "
        "AND current_percent IS NULL").fetchone()[0]
    db.close()

    truth_nulls = sum(1 for r in BAND_ROWS if r[3] is None)
    assert nulls == truth_nulls > 0
    # The rows vanish, and the band's own result carries no trace of them.
    assert banded == total - truth_nulls - sum(
        1 for r in BAND_ROWS if r[3] is not None and r[3] > 99.9)


def test_null_current_percent_is_dropped_into_a_named_bucket(tmp_path, monkeypatch):
    import scripts.analysis.reclassify_at_limit as RA

    p = tmp_path / "d.db"
    _build_db(p)
    src = tmp_path / "src"
    src.mkdir()
    (src / "u1.cpp").write_text("// fixture\n")
    monkeypatch.setattr(RA, "DECOMP_DB", p)
    monkeypatch.setattr(RA, "REPO_ROOT", tmp_path)

    cov = CoverageReport("reclassify_at_limit", allow_truncation=False)
    cands = RA.query_at_limit_functions(
        unit_source_map={"src/u1.cpp": "src/u1.cpp"},
        unit_pattern=None, min_pct=0, max_pct=99.9, limit=0, cov=cov)
    d = cov.as_dict()

    truth_nulls = sum(1 for r in BAND_ROWS if r[3] is None)
    truth_above = sum(1 for r in BAND_ROWS if r[3] is not None and r[3] > 99.9)

    assert d["universe"] == len(BAND_ROWS)
    assert d["dropped"]["null-current-percent"] == truth_nulls
    assert d["dropped"]["above--max-pct"] == truth_above
    assert cov.unaccounted == 0, "the funnel must balance: universe == examined + drops"
    assert d["examined"] == len(cands)
    assert cov.emit() == EXIT_OK

    # ...and --include-null-percent must actually recover them, not just count them.
    cov2 = CoverageReport("reclassify_at_limit", allow_truncation=False)
    cands2 = RA.query_at_limit_functions(
        unit_source_map={"src/u1.cpp": "src/u1.cpp"},
        unit_pattern=None, min_pct=0, max_pct=99.9, limit=0, cov=cov2,
        include_null_percent=True)
    assert len(cands2) - len(cands) == truth_nulls
    assert "null-current-percent" not in cov2.as_dict()["dropped"]
    assert cov2.unaccounted == 0


def test_excluded_rows_are_eligible_by_default_and_the_tool_says_so(tmp_path, monkeypatch):
    """excluded=1 rows are WRITABLE by default. That must be stated, not implied."""
    import scripts.analysis.reclassify_at_limit as RA

    p = tmp_path / "d.db"
    _build_db(p)
    (tmp_path / "src").mkdir()
    (tmp_path / "src" / "u1.cpp").write_text("// fixture\n")
    monkeypatch.setattr(RA, "DECOMP_DB", p)
    monkeypatch.setattr(RA, "REPO_ROOT", tmp_path)
    usm = {"src/u1.cpp": "src/u1.cpp"}

    truth_excluded = sum(1 for r in BAND_ROWS if r[4])
    assert truth_excluded > 0, "fixture must contain an excluded row"

    cov = CoverageReport("reclassify_at_limit")
    got = RA.query_at_limit_functions(usm, None, 0, 99.9, 0, cov=cov)
    assert any(c["excluded"] for c in got), \
        "default behaviour is UNCHANGED: excluded rows are still candidates"
    assert any("excluded=1" in n and "ELIGIBLE FOR UPDATE" in n
               for n in cov.as_dict()["notes"]), \
        "the tool must SAY that excluded rows can be written"

    cov2 = CoverageReport("reclassify_at_limit")
    got2 = RA.query_at_limit_functions(usm, None, 0, 99.9, 0, cov=cov2, skip_excluded=True)
    assert cov2.as_dict()["dropped"]["excluded-row"] == truth_excluded
    assert len(got) - len(got2) == truth_excluded
    assert cov2.unaccounted == 0


def test_demangler_parse_failures_are_counted_not_silently_skipped(tmp_path, monkeypatch):
    """The accidental-blindness class: a name we cannot parse is a row nobody saw."""
    import scripts.analysis.reclassify_at_limit as RA

    p = tmp_path / "d.db"
    db = sqlite3.connect(str(p))
    db.execute(FUNCS_DDL)
    rows = [
        ("?ok@C@@QAEXXZ", "void C::ok(void)", "src/u1.cpp", 50.0, 0),
        ("?weird@@", "", "src/u1.cpp", 50.0, 0),          # empty demangled
        ("?weird2@@", "   ", "src/u1.cpp", 50.0, 0),      # whitespace only
    ]
    db.executemany(
        "INSERT INTO functions(symbol, demangled, unit, current_percent, verdict, excluded) "
        "VALUES (?,?,?,?,'AT_LIMIT',?)", rows)
    db.commit()
    db.close()
    (tmp_path / "src").mkdir()
    (tmp_path / "src" / "u1.cpp").write_text("// fixture\n")
    monkeypatch.setattr(RA, "DECOMP_DB", p)
    monkeypatch.setattr(RA, "REPO_ROOT", tmp_path)

    cov = CoverageReport("reclassify_at_limit", allow_truncation=False)
    cands = RA.query_at_limit_functions({"src/u1.cpp": "src/u1.cpp"}, None, 0, 99.9, 0,
                                        cov=cov)
    d = cov.as_dict()
    # Independent route: ask the demangler itself which fixture rows it fails on.
    from decomp_synth.types import extract_qualified_name
    truth_fail = sum(1 for r in rows if not extract_qualified_name(r[1] or ""))
    assert truth_fail > 0, "fixture must contain names the demangler rejects"
    assert d["dropped"].get("demangler-parse-failure") == truth_fail
    assert len(cands) == len(rows) - truth_fail
    assert cov.unaccounted == 0


def test_limit_truncates_the_analysis_and_exits_nonzero(tmp_path, monkeypatch):
    import scripts.analysis.reclassify_at_limit as RA

    p = tmp_path / "d.db"
    _build_db(p)
    (tmp_path / "src").mkdir()
    (tmp_path / "src" / "u1.cpp").write_text("// fixture\n")
    monkeypatch.setattr(RA, "DECOMP_DB", p)
    monkeypatch.setattr(RA, "REPO_ROOT", tmp_path)

    cov = CoverageReport("reclassify_at_limit", allow_truncation=False)
    full = RA.query_at_limit_functions({"src/u1.cpp": "src/u1.cpp"}, None, 0, 99.9, 0,
                                       cov=CoverageReport("x"))
    assert len(full) >= 2, "fixture must have something to truncate"
    got = RA.query_at_limit_functions({"src/u1.cpp": "src/u1.cpp"}, None, 0, 99.9, 1,
                                      cov=cov)
    assert len(got) == 1
    assert cov.truncated is True
    assert cov.as_dict()["dropped"]["capped-by-limit"] == len(full) - 1
    assert cov.emit() == EXIT_TRUNCATED
    assert cov.unaccounted == 0


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 3 — one symbol in two units must not be rewritten together.
#
# `UPDATE functions SET verdict=... WHERE symbol = ?` selected the row by symbol
# alone, though the candidate was chosen as a (symbol, unit) pair. Today's
# schema declares `symbol ... UNIQUE`, so the bug is LATENT — which is exactly
# why a test is worth more than an inspection: reconstruct the schema WITHOUT
# that constraint and show the old statement over-writes while the new one does
# not.
# --------------------------------------------------------------------------- #

DUP_ROWS = [
    ("?Foo@Bar@@QAEXXZ", "void Bar::Foo(void)", "src/alpha.cpp", 80.0),
    ("?Foo@Bar@@QAEXXZ", "void Bar::Foo(void)", "src/beta.cpp", 80.0),
    ("?Other@Bar@@QAEXXZ", "void Bar::Other(void)", "src/alpha.cpp", 80.0),
]


def _build_dup_db(path: Path):
    db = sqlite3.connect(str(path))
    db.execute(FUNCS_DDL)                 # NOTE: no UNIQUE on symbol
    db.executemany(
        "INSERT INTO functions(symbol, demangled, unit, current_percent, verdict) "
        "VALUES (?,?,?,?,'AT_LIMIT')", DUP_ROWS)
    db.commit()
    db.close()
    return db


def test_unqualified_update_really_would_rewrite_every_unit(tmp_path):
    """Demonstrate the defect before asserting the fix — or the fix proves nothing."""
    p = tmp_path / "dup.db"
    _build_dup_db(p)
    db = sqlite3.connect(str(p))
    cur = db.execute(
        "UPDATE functions SET verdict = NULL, verdict_reason = 'oops' WHERE symbol = ?",
        (DUP_ROWS[0][0],))
    db.commit()
    hit = cur.rowcount
    db.close()

    truth_dupes = sum(1 for r in DUP_ROWS if r[0] == DUP_ROWS[0][0])
    assert truth_dupes > 1, "fixture must place one symbol in more than one unit"
    assert hit == truth_dupes, \
        "the historical statement rewrites every unit that shares the symbol"


def test_qualified_update_touches_only_the_diagnosed_unit(tmp_path, monkeypatch):
    import scripts.analysis.reclassify_at_limit as RA

    p = tmp_path / "dup.db"
    _build_dup_db(p)
    monkeypatch.setattr(RA, "DECOMP_DB", p)

    target_unit = DUP_ROWS[0][2]
    other_unit = DUP_ROWS[1][2]
    result = RA.ReclassifyResult(
        symbol=DUP_ROWS[0][0], demangled=DUP_ROWS[0][1], unit=target_unit,
        current_percent=80.0, category="STRUCTURAL", action="REOPEN",
        verdict_reason="has_fixable_structural [reloc=none]",
    )
    n = RA.apply_reclassification(result)
    assert n == 1, f"the qualified UPDATE must match exactly one row, matched {n}"

    db = sqlite3.connect(str(p))
    got = dict(db.execute(
        "SELECT unit, verdict IS NULL FROM functions WHERE symbol = ?",
        (DUP_ROWS[0][0],)).fetchall())
    db.close()
    assert got[target_unit] == 1, "the diagnosed unit must be reopened"
    assert got[other_unit] == 0, \
        "the OTHER unit's row must be untouched — it was never diagnosed"


def test_persisted_verdict_reason_carries_its_ruler(tmp_path, monkeypatch):
    """A verdict outlives its run; a verdict without its ruler is not a measurement."""
    import scripts.analysis.reclassify_at_limit as RA

    tag = RA.ruler_tag()
    assert tag.startswith("reloc=")
    assert tag != "reloc="

    p = tmp_path / "dup.db"
    _build_dup_db(p)
    monkeypatch.setattr(RA, "DECOMP_DB", p)
    reason = f"noise_only [{tag}]"
    RA.apply_reclassification(RA.ReclassifyResult(
        symbol=DUP_ROWS[2][0], demangled=DUP_ROWS[2][1], unit=DUP_ROWS[2][2],
        current_percent=80.0, category="NOISE_ONLY", action="KEEP",
        verdict_reason=reason))
    db = sqlite3.connect(str(p))
    stored = db.execute("SELECT verdict_reason FROM functions WHERE symbol = ?",
                        (DUP_ROWS[2][0],)).fetchone()[0]
    db.close()
    assert tag in stored, "the ruler must be persisted alongside the verdict"


def test_provenance_names_both_the_db_and_the_measured_tree():
    import scripts.analysis.reclassify_at_limit as RA
    text = "\n".join(RA.provenance_lines())
    assert "DB WRITTEN" in text
    assert "TREE MEASURED" in text
    assert str(RA.DECOMP_DB) in text


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 4 — ceiling_calculator called the most FIXABLE class
# unfixable, and clamped a disagreeing ceiling up to `current` so the
# disagreement disappeared.
# --------------------------------------------------------------------------- #

def test_insert_delete_and_immediate_are_not_in_the_hard_floor():
    b = CC.MismatchBreakdown(regswap=1, merged=1, relocation=1, scheduling=1,
                             save_restore=1, immediate=3, insert_delete=4)
    # Derive the expectation from the dataclass fields, not from a typed number.
    assert b.hard_unfixable == b.total_unfixable - (b.immediate + b.insert_delete)
    assert b.soft_unfixable == b.immediate + b.insert_delete
    assert b.hard_unfixable < b.total_unfixable
    d = b.to_dict()
    assert d["hard_unfixable"] == b.hard_unfixable
    assert d["soft_unfixable"] == b.soft_unfixable
    # The historical `total_unfixable` is UNCHANGED — the conservative ceiling
    # must stay bit-identical to what it always was.
    assert d["total_unfixable"] == (b.regswap + b.merged + b.relocation + b.immediate
                                    + b.scheduling + b.save_restore + b.insert_delete)


def _instr(idx, match_type, t_op="add", b_op="add", t_args="", b_args=""):
    return {
        "index": idx, "match_type": match_type,
        "target": {"opcode": t_op, "args": t_args, "typed_args": []},
        "base": {"opcode": b_op, "args": b_args, "typed_args": []},
    }


def _fake_objdiff(n_matched, n_insert_delete, fuzzy):
    instrs = [_instr(i, "matched") for i in range(n_matched)]
    instrs += [_instr(n_matched + i, "insert")
               for i in range(n_insert_delete)]
    return {"instructions": instrs, "fuzzy_match_percent": fuzzy}


def test_two_ceilings_diverge_on_a_pure_insert_delete_function(monkeypatch):
    monkeypatch.setattr(CC, "detect_patterns", lambda instrs: [])
    data = _fake_objdiff(n_matched=8, n_insert_delete=2, fuzzy=50.0)
    monkeypatch.setattr(CC, "run_objdiff_json", lambda sym, extra=None: (data, None))

    r = CC.analyze_function("?sym@@", "src/u.cpp", current_pct=50.0)
    assert r.error is None
    assert r.breakdown.insert_delete == sum(
        1 for i in data["instructions"] if i["match_type"] == "insert")

    total = r.total_instructions
    # Recompute both ceilings from the BREAKDOWN the tool produced, by the
    # definitions in the docstring — not from literals.
    exp_cons = 100.0 * (1.0 - (r.breakdown.total_unfixable + r.breakdown.other) / total)
    exp_opt = 100.0 * (1.0 - r.breakdown.hard_unfixable / total)
    assert r.ceiling_percent == pytest.approx(max(r.current_percent, exp_cons))
    assert r.ceiling_percent_optimistic == pytest.approx(max(r.ceiling_percent, exp_opt))
    assert r.ceiling_percent_optimistic > r.ceiling_percent, (
        "a function whose only mismatches are insert/delete must not have its "
        "ceiling and its optimistic ceiling agree — that was the bug")


def test_clamped_ceiling_is_recorded_instead_of_hidden(monkeypatch):
    monkeypatch.setattr(CC, "detect_patterns", lambda instrs: [])
    # 2 of 10 instructions unfixable => raw conservative ceiling 80%, but the
    # grader says 95%. The clamp hides that disagreement; record it.
    data = _fake_objdiff(n_matched=8, n_insert_delete=2, fuzzy=95.0)
    monkeypatch.setattr(CC, "run_objdiff_json", lambda sym, extra=None: (data, None))

    r = CC.analyze_function("?sym@@", "src/u.cpp", current_pct=95.0)
    raw_expected = 100.0 * (
        1.0 - (r.breakdown.total_unfixable + r.breakdown.other) / r.total_instructions)
    assert r.ceiling_percent_raw == pytest.approx(raw_expected)
    assert r.ceiling_percent_raw < r.current_percent
    assert r.clamped_to_current is True
    assert r.ceiling_percent == pytest.approx(r.current_percent)

    # Control for the control: no clamp when the raw ceiling is already above.
    data2 = _fake_objdiff(n_matched=99, n_insert_delete=1, fuzzy=50.0)
    monkeypatch.setattr(CC, "run_objdiff_json", lambda sym, extra=None: (data2, None))
    r2 = CC.analyze_function("?sym2@@", "src/u.cpp", current_pct=50.0)
    assert r2.clamped_to_current is False
    assert r2.ceiling_percent == pytest.approx(r2.ceiling_percent_raw)


def test_objdiff_failure_reason_survives_to_the_result(monkeypatch):
    monkeypatch.setattr(CC, "run_objdiff_json",
                        lambda sym, extra=None: (None, "objdiff timeout (30s)"))
    r = CC.analyze_function("?sym@@", "src/u.cpp")
    assert r.error == "objdiff timeout (30s)", \
        "every failure mode used to collapse into the string 'objdiff failed'"


def test_ceiling_calculator_counts_every_db_row_it_drops(tmp_path):
    p = tmp_path / "d.db"
    _build_db(p)
    cov = CoverageReport("ceiling_calculator", allow_truncation=False)
    got = CC.load_at_limit_functions(str(p), min_pct=0.0, max_pct=100.0, cov=cov)
    d = cov.as_dict()

    assert d["universe"] == len(BAND_ROWS)
    assert d["dropped"]["excluded-row"] == sum(1 for r in BAND_ROWS if r[4])
    assert d["dropped"]["null-current-percent"] == sum(
        1 for r in BAND_ROWS if r[3] is None and not r[4])
    cov.examine(len(got))
    assert cov.unaccounted == 0
    # NULL rows must be recoverable, not merely counted.
    cov2 = CoverageReport("ceiling_calculator")
    got2 = CC.load_at_limit_functions(str(p), min_pct=0.0, max_pct=100.0, cov=cov2,
                                      include_null_percent=True)
    assert len(got2) - len(got) == sum(1 for r in BAND_ROWS if r[3] is None and not r[4])


def test_ceiling_calculator_order_is_total(tmp_path):
    """Ties on current_percent must not be broken by SQLite's arrival order."""
    p = tmp_path / "tie.db"
    db = sqlite3.connect(str(p))
    db.execute(FUNCS_DDL)
    tied = [(f"?s{i}@@", f"s{i}", f"src/u{i % 3}.cpp", 88.0, 0) for i in range(12)]
    db.executemany(
        "INSERT INTO functions(symbol, demangled, unit, current_percent, verdict, excluded) "
        "VALUES (?,?,?,?,'AT_LIMIT',?)", tied)
    db.commit()
    db.close()

    a = [r["symbol"] for r in CC.load_at_limit_functions(str(p))]
    b = [r["symbol"] for r in CC.load_at_limit_functions(str(p))]
    assert a == b
    assert a == sorted(a), "with every percent tied, the order must fall back to symbol"


def test_ceiling_calculator_db_handle_is_read_only(tmp_path):
    """decomp.db is shared with concurrent agents; this tool must not be able to write."""
    p = tmp_path / "ro.db"
    _build_db(p)
    conn = CC._connect_ro(str(p))
    with pytest.raises(sqlite3.OperationalError):
        conn.execute("UPDATE functions SET verdict = 'X'")
    conn.close()


# =========================================================================== #
# grindarray_divergence — a hardcoded verdict that routes the next engineer.
#
# The audit table filed its trailing `CONCLUSION:` block as "noted, not
# load-bearing". The population claim above it is true — full 64x256x256, no
# sampling — but the block was an unconditional `print` of a fixed string, and
# it does not merely conclude: it says *"Investigate the key derivation
# pipeline BEFORE GrindArray."* A stored verdict that tells the next engineer
# where NOT to look is load-bearing by definition.
#
# Sabotage every x64 op to return (ppc + 1) & 0xFF and the script printed
# `[DIVERGENCE FOUND]`, `[NO MATCH]` and `*** DIVERGE ***` — then, sixty lines
# later, printed the conclusion BYTE-IDENTICAL to the clean run (1,296 B, empty
# diff), exit 0 both times.
# =========================================================================== #

GRIND = os.path.join(REPO, "scripts", "analysis", "grindarray_divergence.py")

_SABOTAGE_ANCHOR = ('if __name__ == "__main__":\n'
                    '    divergent_ops = scan_all_ops_exhaustive()')
_SABOTAGE = '''if __name__ == "__main__":
    def _mk(i):
        return lambda operand, w: (ppc_ops[i](operand, w) + 1) & 0xFF
    for _i in range(len(x64_ops)):
        x64_ops[_i] = _mk(_i)
    divergent_ops = scan_all_ops_exhaustive()'''


def _run_grind(path):
    return subprocess.run([sys.executable, path], capture_output=True,
                          text=True, cwd=REPO, timeout=1800)


def _conclusion_block(stdout):
    """The trailing verdict only — the part that routes the reader."""
    for marker in ("CONCLUSION:", "NO CONCLUSION:"):
        i = stdout.find(marker)
        if i != -1:
            return stdout[i:]
    return ""


def test_grindarray_conclusion_is_gated_on_the_computed_result(tmp_path):
    src = open(GRIND, errors="replace").read()
    assert _SABOTAGE_ANCHOR in src, "the entry point moved; re-derive this control"
    sab = tmp_path / "grind_sabotaged.py"
    sab.write_text(src.replace(_SABOTAGE_ANCHOR, _SABOTAGE))

    clean = _run_grind(GRIND)
    broken = _run_grind(str(sab))

    # (a) The sabotage must really break the computation, or this is not a
    # control. Assert on the tool's OWN divergence markers, upstream of the
    # block under test.
    assert "[DIVERGENCE FOUND]" in broken.stdout
    assert "*** DIVERGE ***" in broken.stdout
    assert "[DIVERGENCE FOUND]" not in clean.stdout

    # (b) The verdict must now DISAGREE with itself across the two runs. This
    # is the assertion the historical form failed: the two blocks were byte
    # identical.
    assert _conclusion_block(clean.stdout) != _conclusion_block(broken.stdout), (
        "the conclusion block is byte-identical on a clean run and on a run "
        "whose own output says [DIVERGENCE FOUND] — it is hardcoded")

    # The clean run still reaches the stored conclusion: the gate must not have
    # simply deleted a true finding.
    assert "CONCLUSION: GrindArray is NOT the divergence source" in clean.stdout
    assert clean.returncode == 0

    # The refuted run must not reproduce it, and above all must not reproduce
    # the ROUTING — that sentence is what sends the next engineer elsewhere.
    assert "CONCLUSION: GrindArray is NOT the divergence source" not in broken.stdout
    assert "Investigate the key derivation pipeline BEFORE GrindArray" not in broken.stdout
    assert "GrindArray is a LIVE SUSPECT" in broken.stdout
    assert broken.returncode != 0
    assert broken.returncode != clean.returncode


def test_grindarray_gate_uses_ops_that_fire_not_ops_that_exist():
    """The conclusion's own premise is 'op5 diverges but is NEVER called'.

    A gate on `not divergent_ops` would be wrong in the other direction: it
    would refuse to reproduce a TRUE finding, because op5 really does diverge
    on this tree. The clean run proves the distinction is live — it must report
    a divergent op AND still reach the conclusion.
    """
    clean = _run_grind(GRIND)
    assert "op5" in clean.stdout or "op 5" in clean.stdout
    assert "Ops that actually FIRE for this key:" in clean.stdout
    fired = clean.stdout.split("Ops that actually FIRE for this key:")[1]
    fired = fired.split("\n")[0]
    assert "5" not in fired.replace("15", "").replace("25", "").replace(
        "35", "").replace("45", "").replace("53", "").replace("55", ""), (
        f"op5 must not be in the firing set, or the premise is false: {fired}")
    assert "CONCLUSION: GrindArray is NOT the divergence source" in clean.stdout


# =========================================================================== #
# function_health — the insert_delete classification the lane fixed in
# ceiling_calculator and left standing in a sibling it edited in the same
# branch.
#
# ceiling_calculator.py's header calls insert_delete "the single most FIXABLE
# class" and excludes it from the optimistic floor. function_health.py went on
# filing it `fixable=False`, and that fed straight into
# `Only unfixable mismatches remain` -> verdict at_limit — an AT_LIMIT
# certificate manufactured out of the class the project had already ruled
# reachable.
# =========================================================================== #

import scripts.analysis.function_health as FH  # noqa: E402


def _cat(name, count, fixable=False, contested=False):
    return FH.MismatchCategory(name=name, count=count, fixable=fixable,
                               description="x", contested=contested)


def test_insert_delete_alone_no_longer_certifies_at_limit():
    # (b) The two classes must be treated DIFFERENTLY — that is the finding.
    # A hard floor still certifies; a contested class must not.
    hard, _, _ = FH._compute_verdict(95.0, [_cat("Register swap", 12)], 100.0, 5.0)
    soft, reason, _ = FH._compute_verdict(
        95.0, [_cat("Insert/Delete", 12, contested=True)], 100.0, 5.0)

    assert hard == "at_limit", "a genuine hard floor must still certify"
    assert soft != "at_limit", (
        "insert_delete alone must not manufacture an at_limit certificate — "
        "ceiling_calculator treats it as reachable")
    assert soft == "contested"
    assert "not an at_limit certificate" in reason.lower()
    # ...and it must say how much is contested vs how much is a real floor.
    assert "12" in reason and "0 are hard floors" in reason


def test_a_mixed_function_reports_both_populations():
    _, reason, _ = FH._compute_verdict(
        95.0,
        [_cat("Register swap", 3), _cat("Insert/Delete", 9, contested=True)],
        100.0, 5.0)
    assert "9 of 12" in reason
    assert "3 are hard floors" in reason


def test_the_contested_classes_are_the_ones_ceiling_calculator_names():
    """Derive the set from the OTHER tool rather than restating a constant here.

    ceiling_calculator's optimistic floor is the hard classes only, so the
    classes it omits are exactly the ones function_health must not treat as
    floors.
    """
    cc = open(os.path.join(REPO, "scripts", "analysis",
                           "ceiling_calculator.py"), errors="replace").read()
    assert "insert_delete" in cc and "hard_unfixable" in cc
    contested = {k for k, v in FH._MISMATCH_DESCS.items()
                 if len(v) > 3 and v[3]}
    assert "insert_delete" in contested, (
        "the class ceiling_calculator calls the most fixable must be contested "
        "here too, or the two tools disagree in silence")
    assert "regswap" not in contested and "merged" not in contested


# =========================================================================== #
# compare_progress.py's "verified 471/471 units" — a hand-written verification
# count that NOTHING in the repo computes.
#
# The claim it backs is true and load-bearing (the unit-level
# `measures.fuzzy_match_percent` really is the size-weighted mean of the
# per-function NORMALIZED values, despite the key name). The evidence for it
# was a number in a comment, twice, which is exactly as checkable as a
# remembered one. It is a test now.
# =========================================================================== #

REPORT = os.path.join(REPO, "build", "373307D9", "report.json")
needs_the_report = pytest.mark.skipif(
    not os.path.exists(REPORT), reason="needs a built tree: report.json")

UNIT_TOLERANCE = 1e-5   # f32 serialisation; see the assertion below


def _weighted(fns, key):
    num = sum(int(f.get("size", 0)) * float(f.get(key) or 0.0) for f in fns)
    den = sum(int(f.get("size", 0)) for f in fns)
    return (num / den) if den else None


@needs_the_report
def test_unit_measure_really_is_the_normalized_weighted_mean():
    units = json.load(open(REPORT)).get("units", [])
    checked = skipped = agreed = 0
    worst, worst_unit = 0.0, None
    key_absent = 0

    for u in units:
        fns = u.get("functions") or []
        if not (u.get("measures", {}).get("total_code") or 0):
            skipped += 1          # empty unit: the weighted mean is 0/0
            continue
        recomputed = _weighted(fns, "match_percent_normalized")
        if recomputed is None:
            skipped += 1
            continue
        stored = u["measures"].get("fuzzy_match_percent")
        if stored is None:
            key_absent += 1       # serde omits the 0.0 default
            stored = 0.0
        checked += 1
        d = abs(float(stored) - recomputed)
        if d > worst:
            worst, worst_unit = d, u["name"]
        if d <= UNIT_TOLERANCE:
            agreed += 1

    # (a) The arithmetic identity — no fixture value makes this vacuous.
    assert checked + skipped == len(units)
    assert agreed == checked, (
        f"{checked - agreed} units disagree; worst {worst:.3g} at {worst_unit}")

    # (c) The tolerance is honest, not a fudge: it must be tight enough that
    # the WRONG ruler fails it.
    assert worst < UNIT_TOLERANCE, f"max delta {worst:.3g} at {worst_unit}"
    assert checked > 1000, "too few units checked for this to mean anything"

    # (b) THE NEGATIVE CONTROL. Weighting the per-function *fuzzy* values must
    # NOT reproduce the unit measure — otherwise the test passes no matter
    # which ruler objdiff actually used, and proves nothing.
    fuzzy_agree = 0
    for u in units:
        fns = u.get("functions") or []
        if not (u.get("measures", {}).get("total_code") or 0):
            continue
        alt = _weighted(fns, "fuzzy_match_percent")
        stored = u["measures"].get("fuzzy_match_percent") or 0.0
        if alt is not None and abs(float(stored) - alt) <= UNIT_TOLERANCE:
            fuzzy_agree += 1
    assert fuzzy_agree < agreed, (
        "weighting the FUZZY values agrees just as often — this test cannot "
        "tell the two rulers apart and is therefore not evidence")


@needs_the_report
def test_the_471_claim_is_gone_and_the_replacement_is_computable():
    """The number in the comment must be one this file can produce."""
    src = open(os.path.join(REPO, "scripts", "analysis", "compare_progress.py"),
               errors="replace").read()
    units = json.load(open(REPORT)).get("units", [])
    total = len(units)
    skipped = sum(1 for u in units
                  if not (u.get("measures", {}).get("total_code") or 0))

    # The historical claim may be QUOTED -- a refuted number that goes
    # unrecorded gets re-filed -- but it must never stand as a live one. Every
    # occurrence has to sit inside the paragraph that refutes it.
    for m in re.finditer(r"471", src):
        window = src[max(0, m.start() - 400):m.start() + 400]
        assert "used to claim" in window or "NOTHING IN THIS REPO COMPUTES" in window, (
            "a bare '471' is back as a live verification count")
    assert f"{total:,}" in src, (
        f"the comment must state the real unit total ({total:,})")
    assert f"{total - skipped:,}" in src, (
        f"...and the real checked count ({total - skipped:,})")
    assert str(skipped) in src, f"...and why {skipped} were skipped"
