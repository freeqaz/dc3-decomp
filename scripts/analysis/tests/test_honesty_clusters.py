"""Negative controls for the three cluster/catalog/health scanners.

Companion to `test_coverage.py`, which covers the contract itself; this file
covers the three scanners that were audited against it:

    scripts/analysis/header_cluster.py
    scripts/analysis/inlining_catalog.py
    scripts/analysis/function_health.py

STANDARD (same as test_coverage.py, restated because it is the whole point):
every test RECONSTRUCTS the false negative and asserts the check now fires.
Nothing here compares a synthesised value to a constant typed in the same
sitting — that pattern already let a real error through this project once.
Each test does one of:

  (a) runs the HISTORICAL code path and the repaired one over the SAME input
      and asserts they differ in the specific way claimed (differential — no
      expected constant to get wrong);
  (b) recomputes the expected number by an INDEPENDENT route (a second parse of
      the same file, a second query of the same DB) and compares the two;
  (c) asserts against an EXTERNAL ground truth this suite does not own — real
      SQLite semantics, or the project's own DDL in scripts/orchestrator/
      database.py and scripts/sync_match_percent.py.

Where a real artefact is available (build/373307D9/report.json) the historical
incident is replayed against it and the test skips rather than inventing
numbers when it is not.
"""
from __future__ import annotations

import json
import re
import sqlite3
import textwrap
from pathlib import Path

import pytest

from scripts.analysis.coverage import CoverageReport, EXIT_OK, EXIT_TRUNCATED

from scripts.analysis import header_cluster as hc
from scripts.analysis import inlining_catalog as ic
from scripts.analysis import function_health as fh


REPO = Path(__file__).resolve().parent.parent.parent.parent
REAL_REPORT = REPO / "build" / "373307D9" / "report.json"


# --------------------------------------------------------------------------- #
# Shared fixture: a report.json shaped like the real one, with the two tiers
# that matter — rows objdiff scored (fuzzy_match_percent present) and rows it
# did not (key ABSENT, which is what "we emitted no body" looks like).
# --------------------------------------------------------------------------- #

def _row(name, unit_ignored=None, *, fuzzy=None, normalized=0.0, size=16):
    r = {"name": name, "size": size, "address": "0x82000000",
         "metadata": {"demangled_name": f"public: void __cdecl {name}(void)"},
         "match_percent_normalized": normalized}
    if fuzzy is not None:
        r["fuzzy_match_percent"] = fuzzy
    return r


@pytest.fixture
def two_tier_report(tmp_path) -> Path:
    """A report with a scored tier and a NO-BODY tier (no fuzzy key at all)."""
    scored = [
        # two distinct percentages that share one round(pct, 1) bin
        _row("A_lo", fuzzy=91.96, normalized=91.96),
        _row("B_hi", fuzzy=92.04, normalized=92.04),
        _row("C_mid", fuzzy=92.00, normalized=92.00),
        # a "complete-looking" row that is NOT complete
        _row("D_9996", fuzzy=99.96, normalized=99.96),
        # a genuinely complete row
        _row("E_100", fuzzy=100.0, normalized=100.0),
    ]
    nobody = [_row(f"Z_nobody_{i}", fuzzy=None, normalized=0.0) for i in range(7)]
    doc = {"units": [
        {"name": "default/system/uno", "functions": scored[:3]},
        {"name": "default/system/dos", "functions": scored[3:]},
        {"name": "default/system/tres", "functions": nobody},
    ]}
    p = tmp_path / "report.json"
    p.write_text(json.dumps(doc))
    return p


def _historical_header_cluster_filter(report_path: Path) -> list[dict]:
    """The EXACT pre-fix body of header_cluster.load_report's inner loop.

    Kept verbatim so the differential tests below compare against what actually
    shipped, not against a paraphrase of it.
    """
    report = json.loads(Path(report_path).read_text())
    kept = []
    for unit in report.get("units", []):
        for func in unit.get("functions", []):
            pct = func.get("fuzzy_match_percent", 0)      # <-- default 0
            if pct >= 99.95 or pct <= 0:                  # <-- eats the no-body tier
                continue
            kept.append(func)
    return kept


# =========================================================================== #
# header_cluster.py
# =========================================================================== #

def test_no_body_tier_left_the_population_without_a_trace(two_tier_report):
    """The audited defect, reconstructed: `get(..., 0)` + `pct <= 0: continue`.

    Differential — the historical filter and the repaired loader run over the
    same file, so there is no expected constant to write down.
    """
    old_kept = _historical_header_cluster_filter(two_tier_report)

    # Independent recount of the tier the old filter destroyed.
    doc = json.loads(two_tier_report.read_text())
    all_rows = [f for u in doc["units"] for f in u["functions"]]
    no_fuzzy = [f for f in all_rows if "fuzzy_match_percent" not in f]

    assert no_fuzzy, "fixture must contain a no-body tier or it proves nothing"
    assert all(f not in old_kept for f in no_fuzzy), \
        "the historical filter is supposed to have discarded these"

    cov = CoverageReport("header_cluster")
    new_kept = hc.load_report(two_tier_report, cov=cov, min_pct=0.0, max_pct=100.0)

    # The rows are STILL not examined — the fix is that they are now COUNTED.
    assert cov.unaccounted == 0, "every discard must be routed through cov.drop()"
    assert cov.as_dict()["universe"] == len(all_rows)
    assert cov.as_dict()["dropped"]["zero-pct-no-body"] == len(no_fuzzy)
    assert len(new_kept) == len(old_kept), \
        "the honesty pass must not change WHAT the scanner finds"


def test_the_denominator_appears_in_the_rendered_block(two_tier_report, capsys):
    cov = CoverageReport("header_cluster")
    hc.load_report(two_tier_report, cov=cov, min_pct=50.0, max_pct=100.0)
    assert cov.emit() == EXIT_OK
    err = capsys.readouterr().err
    doc = json.loads(two_tier_report.read_text())
    total = sum(len(u["functions"]) for u in doc["units"])
    assert f"universe            : {total}" in err
    assert "zero-pct-no-body" in err
    assert "complete-at-or-above-99.95" in err


def test_99_96_is_dropped_as_complete_and_says_so(two_tier_report):
    """`>= 99.95` treats 99.96 as done. Unchanged behaviour — but now named."""
    cov = CoverageReport("header_cluster")
    kept = hc.load_report(two_tier_report, cov=cov, min_pct=0.0, max_pct=100.0)
    assert "D_9996" not in {f.name for f in kept}
    d = cov.as_dict()
    assert d["dropped"]["complete-at-or-above-99.95"] >= 1
    # The block must state that this threshold is not 100%, or a reader repeats
    # the mistake that put a 99.96 function in the "done" pile.
    assert any("99.96 is NOT 100%" in n for n in d["notes"])


def test_exact_match_pct_cluster_is_really_a_0_1pp_band(two_tier_report):
    """Two functions 0.08pp APART are sold as one 'exact match%' cluster.

    Proven by construction: A_lo=91.96 and B_hi=92.04 are different numbers and
    land in the same round(pct, 1) bucket.
    """
    cov = CoverageReport("header_cluster")
    funcs = hc.load_report(two_tier_report, cov=cov, min_pct=0.0, max_pct=100.0)
    pcts = {f.name: f.pct for f in funcs}
    assert pcts["A_lo"] != pcts["B_hi"], "fixture must use two distinct percentages"

    clusters = hc.cluster_by_match_pct(funcs, min_cluster=2, min_units=1)
    holding = [c for c in clusters if {"A_lo", "B_hi"} <= {f.name for f in c.functions}]
    assert holding, "distinct percentages did NOT share a bin — fixture is stale"

    # The width must be stated, not left for the reader to infer.
    assert abs(max(pcts["B_hi"], pcts["A_lo"]) - min(pcts["B_hi"], pcts["A_lo"])) \
        <= hc._PCT_BIN_WIDTH
    assert "bin" in hc._BIN_LABEL
    assert any("bin" in n for n in cov.as_dict()["notes"])


def test_fmt_pct_refuses_to_render_99_97_as_100(capsys):
    """The rounding hazard, as a differential against the naive format string."""
    naive = f"{99.97:.1f}"
    assert naive == "100.0", "sanity: the naive format really does round up"
    assert hc._fmt_pct(99.97) != naive
    assert hc._fmt_pct(99.97) == "<100"
    assert hc._fmt_pct(100.0) == "100.0"


def test_display_caps_announce_their_remainder(capsys):
    """A truncated printout that does not say `... and N more` is a sample."""
    funcs = [hc.FuncInfo(name=f"f{i}", demangled=f"void __cdecl f{i}(void)",
                         unit=f"default/system/u{i % 3}", pct=90.0 + i, size=8)
             for i in range(6)]
    clusters = hc.cluster_by_match_pct(funcs, min_cluster=1, min_units=1)
    assert len(clusters) > 1, "fixture must produce more clusters than the limit"
    hc.print_match_clusters(clusters, limit=1)
    out = capsys.readouterr().out
    assert f"showing 1 of {len(clusters)}" in out
    assert f"and {len(clusters) - 1} more clusters not shown" in out


@pytest.mark.skipif(not REAL_REPORT.exists(), reason="build/373307D9/report.json absent")
def test_replay_against_the_real_report_json():
    """Replay the incident on the real artefact.

    Every expected number is recomputed here by an independent parse of the same
    file, so this asserts agreement between two routes rather than against a
    number someone typed.
    """
    doc = json.loads(REAL_REPORT.read_text())
    rows = [f for u in doc["units"] for f in u.get("functions", [])]
    no_fuzzy = [f for f in rows if "fuzzy_match_percent" not in f]
    true_zero_on_fuzzy = [f for f in rows
                          if f.get("fuzzy_match_percent") is not None
                          and f["fuzzy_match_percent"] <= 0]

    # The premise of the audit: the `<= 0` guard was written for rows that do
    # not exist, and swallowed a tier that does.
    assert true_zero_on_fuzzy == [], \
        "the <=0 guard was supposed to have no true-zero rows to catch"
    assert len(no_fuzzy) > 0

    old_kept = _historical_header_cluster_filter(REAL_REPORT)
    cov = CoverageReport("header_cluster")
    new_kept = hc.load_report(REAL_REPORT, cov=cov, min_pct=50.0, max_pct=100.0)

    d = cov.as_dict()
    assert d["universe"] == len(rows)
    assert cov.unaccounted == 0, "the real report must balance too"
    # The scanner's own headline used to be `Loaded {len(old_kept)}` with the
    # 35%-of-the-report hole unmentioned; the hole is now a named drop.
    assert d["dropped"]["zero-pct-no-body"] + d["dropped"].get("below---min-pct", 0) \
        == len(no_fuzzy) + len([f for f in old_kept if f["fuzzy_match_percent"] < 50.0])
    assert d["examined"] == len([f for f in old_kept
                                 if 50.0 <= f["fuzzy_match_percent"] < 100.0])
    # Coverage on the OLD ruler was a single-digit fraction of the report.
    assert d["coverage_pct"] < 10.0


# =========================================================================== #
# inlining_catalog.py
# =========================================================================== #

def test_optimistic_default_made_every_no_body_function_claim_success(two_tier_report,
                                                                     tmp_path):
    """`get("fuzzy_match_percent", 100.0)` + `if pct >= 100.0: continue`.

    The default was the most optimistic value possible, so a MISSING key read as
    "already matching". Differential: run both predicates over the same rows.
    """
    doc = json.loads(two_tier_report.read_text())
    rows = [f for u in doc["units"] for f in u.get("functions", [])]

    old_survivors = [f for f in rows
                     if f.get("fuzzy_match_percent", 100.0) < 100.0]
    new_survivors = [f for f in rows if ic._row_pct(f)[0] < 100.0]

    no_fuzzy = [f for f in rows if "fuzzy_match_percent" not in f]
    assert no_fuzzy, "fixture must contain the no-body tier"
    assert all(f not in old_survivors for f in no_fuzzy), \
        "the historical default is supposed to have hidden these"
    assert all(f in new_survivors for f in no_fuzzy), \
        "the normalized fallback must make them countable"
    assert len(new_survivors) - len(old_survivors) == len(no_fuzzy)


@pytest.fixture
def header_dir(tmp_path) -> Path:
    """Headers exercising the two structural blind spots."""
    d = tmp_path / "src" / "system" / "ui"
    d.mkdir(parents=True)
    # (1) an accessor the single-brace-depth regex CANNOT see
    (d / "Nested.h").write_text(textwrap.dedent("""\
        #pragma once
        class Nested {
        public:
            int Clamped() const { if (mV < 0) { return 0; } return mV; }
            int Plain() const { return mV; }
        protected:
            int mV;
        };
    """))
    # (2) a multi-class header: FirstClass's accessor gets LastClass's name
    (d / "TwoClasses.h").write_text(textwrap.dedent("""\
        #pragma once
        class FirstClass {
        public:
            int Alpha() const { return mA; }
        protected:
            int mA;
        };

        class LastClass {
        public:
            int Beta() const { return mB; }
        protected:
            int mB;
        };
    """))
    return tmp_path


def test_brace_depth_damage_is_real_and_declared(header_dir):
    """The stated limitation must be a fact about the code, not decoration.

    The audit called this "structurally invisible"; the executable truth is
    worse and more specific — the body is CAPTURED BUT TRUNCATED at the first
    `}`, so a multi-branch method is filed as a *trivial one-statement
    accessor*.  This test pins the actual behaviour, so if the regex is ever
    repaired the limitation text is forced to be revisited.
    """
    text = (header_dir / "src" / "system" / "ui" / "Nested.h").read_text()
    accs = {a.method_name: a for a in ic._scan_single_header(text, "ui/Nested.h")}

    assert "Plain" in accs, "the flat accessor must be seen (control)"
    assert not accs["Plain"].body_truncated
    assert accs["Plain"].statement_count == accs["Plain"].body.count(";")

    clamped = accs["Clamped"]
    source_body = text.split("int Clamped() const", 1)[1]
    assert "return mV;" in source_body, "sanity: the real body has a second return"
    assert "return mV;" not in clamped.body, \
        "if the full body is now captured, LIMITATION_REGEX_BRACE_DEPTH is stale"
    assert clamped.body_truncated, "unbalanced braces must be flagged"
    # ...and it is misclassified as a consequence, which is the actual harm.
    assert clamped.size_class == "trivial"
    assert clamped.to_dict()["body_truncated"] is True

    # The tool must say so wherever it publishes a count.
    assert any("SINGLE-BRACE-DEPTH" in lim for lim in ic.ALL_LIMITATIONS)
    cov = CoverageReport("scan")
    ic.scan_header_accessors([header_dir / "src" / "system"],
                             project_root=header_dir, cov=cov)
    d = cov.as_dict()
    assert any("SINGLE-BRACE-DEPTH" in n for n in d["notes"])
    assert d["accessors_with_truncated_body"] >= 1


def test_class_misattribution_is_real_and_sized(header_dir):
    """`matches[-1]` attributes FirstClass::Alpha to LastClass. Counted, not fixed."""
    path = header_dir / "src" / "system" / "ui" / "TwoClasses.h"
    accs = ic._scan_single_header(path.read_text(), "ui/TwoClasses.h")
    by_method = {a.method_name: a.class_name for a in accs}

    assert by_method["Alpha"] == "LastClass", \
        "the last-class heuristic is supposed to mis-attribute Alpha"
    assert by_method["Alpha"] != "FirstClass"
    # The size of the error must be reported...
    cov = CoverageReport("scan")
    ic.scan_header_accessors([header_dir / "src" / "system"],
                             project_root=header_dir, cov=cov)
    d = cov.as_dict()
    # ...and recomputed independently from the same headers.
    expected = sum(len(ic._scan_single_header(p.read_text(), str(p)))
                   for p in sorted((header_dir / "src").rglob("*.h"))
                   if ic._count_class_decls(p.read_text()) > 1)
    assert d["accessors_possibly_misattributed"] == expected
    assert d["multiclass_headers"] >= 1


def test_scan_header_accessors_balances_and_counts_every_skip(header_dir):
    cov = CoverageReport("scan")
    ic.scan_header_accessors([header_dir / "src" / "system"],
                             project_root=header_dir, cov=cov)
    n_headers = len(list((header_dir / "src").rglob("*.h")))
    d = cov.as_dict()
    assert d["universe"] == n_headers
    assert cov.unaccounted == 0
    assert cov.emit() == EXIT_OK


def test_typo_in_src_dir_is_loud_instead_of_zero_accessors(header_dir, capsys):
    """`Found 0 inline accessors` + exit 0 was indistinguishable from a typo."""
    missing = str(header_dir / "src" / "systemm")     # the classic typo

    # Library contract is unchanged (existing callers depend on it)...
    assert ic.scan_header_accessors([missing]) == []

    # ...but the CLI must refuse.
    rc = ic.main(["scan-headers", "--src-dir", missing])
    out = capsys.readouterr()
    assert rc != 0, "a mistyped --src-dir must not exit 0"
    assert "systemm" in out.err
    with pytest.raises(ic.MissingSourceDirError):
        ic.scan_header_accessors([missing], strict_dirs=True)


def test_unreadable_header_is_counted_not_skipped(header_dir, monkeypatch):
    """The bare `except OSError: continue` used to vanish a file silently."""
    real_read = Path.read_text

    def boom(self, *a, **kw):
        if self.name == "Nested.h":
            raise PermissionError("simulated")
        return real_read(self, *a, **kw)

    monkeypatch.setattr(Path, "read_text", boom)
    cov = CoverageReport("scan")
    ic.scan_header_accessors([header_dir / "src" / "system"],
                             project_root=header_dir, cov=cov)
    d = cov.as_dict()
    assert d["dropped"]["header-unreadable"] == 1
    assert cov.unaccounted == 0, "an unreadable file must still balance the census"


def test_include_count_declares_itself_unimplemented(header_dir, capsys):
    """`-1` used to look like a measurement that came back empty."""
    assert ic._count_includes_from_ninja("anything") == {}, \
        "if this now returns data, LIMITATION_INCLUDE_COUNT is stale"
    cat = ic.build_catalog([header_dir / "src" / "system"], project_root=header_dir)
    assert cat["summary"]["include_count_status"] == "unimplemented"
    assert any("UNIMPLEMENTED" in lim for lim in cat["summary"]["limitations"])
    for data in cat["headers"].values():
        assert data["include_count"] == ic.INCLUDE_COUNT_UNIMPLEMENTED
        assert data["include_count_status"] == "unimplemented"


def test_headers_scanned_no_longer_means_headers_with_accessors(header_dir):
    """The old `headers_scanned` was len(headers WITH accessors) — always smaller."""
    d = header_dir / "src" / "system" / "ui"
    (d / "Empty.h").write_text("#pragma once\nclass Nothing { public: int mX; };\n")

    cov = CoverageReport("cat")
    cat = ic.build_catalog([header_dir / "src" / "system"],
                           known_outlines=[],     # else _KNOWN_OUTLINES adds a header
                           project_root=header_dir, cov=cov)
    n_files = len(list((header_dir / "src").rglob("*.h")))
    assert cat["summary"]["headers_with_accessors"] < n_files, \
        "fixture must include a header with no accessors"
    assert cov.as_dict()["universe"] == n_files


def test_explicit_empty_known_outlines_no_longer_resurrects_the_defaults(header_dir):
    """`known_outlines if known_outlines else _KNOWN_OUTLINES` on an EMPTY list.

    `known_outlines` is normalised from None above that line, so the fallback
    could only fire for a caller who explicitly said "none" — and it silently
    re-added the two built-ins, counted them as `outlined`, and filed them under
    a header the scan never opened. Differential against the same call with the
    defaults left in place.
    """
    empty = ic.build_catalog([header_dir / "src" / "system"],
                             known_outlines=[], project_root=header_dir)
    defaulted = ic.build_catalog([header_dir / "src" / "system"],
                                 project_root=header_dir)

    assert empty["summary"]["outlined"] == 0
    assert defaulted["summary"]["outlined"] == len(ic._KNOWN_OUTLINES), \
        "the default path must be untouched by this fix"
    phantom = {ko["header"] for ko in ic._KNOWN_OUTLINES}
    assert not (phantom & set(empty["headers"])), \
        "a header that was never scanned must not appear in the catalog"
    assert phantom & set(defaulted["headers"]), "sanity: the default path adds them"


def test_suspect_sample_carries_its_total(two_tier_report, header_dir):
    """`suspect[:5]  # limit for readability` truncated the payload unlabelled."""
    # Give one function more than the sample limit of suspect accessors by
    # naming a class that every accessor in the fixture belongs to.
    doc = json.loads(two_tier_report.read_text())
    doc["units"].append({"name": "default/system/ui", "functions": [
        {"name": "?Draw@LastClass@@UAAXXZ", "size": 8,
         "match_percent_normalized": 80.0, "fuzzy_match_percent": 80.0,
         "metadata": {"demangled_name":
                      "public: virtual void __cdecl LastClass::Draw(void)"}}]})
    p = Path(two_tier_report)
    p.write_text(json.dumps(doc))

    # Widen the header dir so LastClass owns > _SUSPECT_SAMPLE_LIMIT accessors.
    ui = header_dir / "src" / "system" / "ui"
    body = "\n".join(f"    int Get{i}() const {{ return m{i}; }}" for i in range(8))
    (ui / "Many.h").write_text(
        "#pragma once\nclass LastClass {\npublic:\n" + body + "\n};\n")

    cov = CoverageReport("mm")
    hcov = CoverageReport("mm-headers")
    res = ic.scan_inline_mismatches(p, [header_dir / "src" / "system"],
                                    project_root=header_dir,
                                    cov=cov, header_cov=hcov)
    target = [c for c in res["candidates"] if "LastClass::Draw" in c["function"]]
    assert target, "fixture function must survive as a candidate"
    c = target[0]
    assert len(c["suspect_accessors"]) == ic._SUSPECT_SAMPLE_LIMIT
    assert c["suspect_total"] > len(c["suspect_accessors"]), \
        "fixture must actually overflow the sample limit"
    assert cov.unaccounted == 0


def test_missing_report_is_loud_not_an_empty_result(tmp_path, header_dir):
    with pytest.raises(FileNotFoundError):
        ic.scan_inline_mismatches(tmp_path / "nope.json",
                                  [header_dir / "src" / "system"],
                                  project_root=header_dir)


# =========================================================================== #
# function_health.py — the highest-consequence finding
# =========================================================================== #

# Ground truth for the schema comes from the PROJECT'S OWN DDL, not from a
# table definition written here — otherwise "these columns do not exist" would
# be a claim about this test file rather than about decomp.db.
def _project_functions_ddl() -> tuple[str, str]:
    from scripts.orchestrator import database as odb
    alter_src = (REPO / "scripts" / "sync_match_percent.py").read_text()
    m = re.search(r'"(ALTER TABLE functions ADD COLUMN match_percent_normalized[^"]*)"',
                  alter_src)
    assert m, ("scripts/sync_match_percent.py no longer contains the "
               "match_percent_normalized migration — this test's ground truth is stale")
    return odb.SCHEMA, m.group(1)


@pytest.fixture
def real_schema_db(tmp_path) -> Path:
    schema, alter = _project_functions_ddl()
    p = tmp_path / "decomp.db"
    conn = sqlite3.connect(str(p))
    conn.executescript(schema)
    conn.execute(alter)
    rows = [
        ("?A@@YAXXZ", "void __cdecl A(void)", "default/system/rndobj/Mesh", 99.97, 99.5),
        ("?B@@YAXXZ", "void __cdecl B(void)", "default/system/rndobj/Mesh", 95.0, 95.0),
        ("?C@@YAXXZ", "void __cdecl C(void)", "default/system/char/Char", 92.0, 92.0),
        ("?D@@YAXXZ", "void __cdecl D(void)", "default/system/rndobj/Mesh", None, 91.0),
        ("?E@@YAXXZ", "void __cdecl E(void)", "default/system/rndobj/Mesh", 10.0, 10.0),
        ("?F@@YAXXZ", "void __cdecl F(void)", "default/system/rndobj/Mesh", 100.0, 100.0),
    ]
    conn.executemany(
        "INSERT INTO functions (symbol, demangled, unit, match_percent_normalized,"
        " current_percent) VALUES (?, ?, ?, ?, ?)", rows)
    conn.commit()
    conn.close()
    return p


def test_the_historical_batch_sql_raises_against_the_projects_own_schema(real_schema_db):
    """THE finding: the batch query named two columns that do not exist.

    Ground truth is `scripts/orchestrator/database.py`'s DDL, so this asserts
    against the project's schema rather than against our expectation of it.
    """
    conn = sqlite3.connect(f"file:{real_schema_db}?mode=ro", uri=True)
    historical = ("SELECT symbol, demangled, unit, source_path, match_percent "
                  "FROM functions WHERE match_percent >= ? AND match_percent < ?")
    with pytest.raises(sqlite3.OperationalError) as exc:
        conn.execute(historical, [90.0, 99.99]).fetchall()
    assert "no such column" in str(exc.value)
    conn.close()

    cols = {c for c in sqlite3.connect(str(real_schema_db))
            .execute("PRAGMA table_info(functions)").fetchall()
            for c in [c[1]]}
    assert "match_percent" not in cols and "source_path" not in cols
    assert {"current_percent", "match_percent_normalized"} <= cols


def test_swallowed_operational_error_used_to_read_as_no_work_exists(real_schema_db):
    """`except Exception: return []` turned the schema error into an empty pool."""
    def historical_query() -> list:
        conn = sqlite3.connect(str(real_schema_db))
        try:
            cur = conn.execute(
                "SELECT symbol, demangled, unit, source_path, match_percent "
                "FROM functions WHERE match_percent >= ? AND match_percent < ? "
                "ORDER BY match_percent DESC LIMIT ?", [90.0, 99.99, 50])
            return [dict(r) for r in cur.fetchall()]
        except Exception:                      # <-- the historical handler
            return []
        finally:
            conn.close()

    assert historical_query() == [], \
        "the old code really did report an empty pool for a schema error"

    cov = CoverageReport("fh")
    rows = fh._query_functions("default/system/rndobj/*", 90.0, 99.99, 0,
                               cov=cov, db_path=real_schema_db)
    # Recompute the expectation by an independent query rather than asserting a
    # number typed here.
    conn = sqlite3.connect(f"file:{real_schema_db}?mode=ro", uri=True)
    expected = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE match_percent_normalized IS NOT NULL "
        "AND match_percent_normalized >= 90.0 AND match_percent_normalized < 99.99 "
        "AND unit GLOB 'default/system/rndobj/*'").fetchone()[0]
    conn.close()
    assert expected > 0, "fixture must contain matching work or it proves nothing"
    assert len(rows) == expected
    assert cov.unaccounted == 0


def test_empty_placeholder_database_is_a_loud_error(tmp_path):
    """`build/373307D9/decomp.db` is 0 bytes; `.exists()` said yes anyway."""
    stub = tmp_path / "decomp.db"
    stub.write_bytes(b"")
    assert stub.exists(), "a zero-byte file passes exists() — that was the trap"
    with pytest.raises(fh.DatabaseUnavailableError) as exc:
        fh.resolve_db_path(stub)
    assert "functions" in str(exc.value)


def test_unknown_percent_column_is_rejected_not_interpolated(real_schema_db):
    with pytest.raises(fh.QueryFailedError):
        fh._query_functions(None, 0.0, 100.0, 0,
                            db_path=real_schema_db, percent_column="match_percent")


def test_null_percent_rows_are_counted_not_silently_absent(real_schema_db):
    cov = CoverageReport("fh")
    fh._query_functions(None, 0.0, 100.0, 0, cov=cov, db_path=real_schema_db)
    conn = sqlite3.connect(f"file:{real_schema_db}?mode=ro", uri=True)
    n_null = conn.execute("SELECT COUNT(*) FROM functions "
                          "WHERE match_percent_normalized IS NULL").fetchone()[0]
    total = conn.execute("SELECT COUNT(*) FROM functions").fetchone()[0]
    conn.close()
    assert n_null > 0, "fixture must contain a NULL-percent row"
    d = cov.as_dict()
    assert d["universe"] == total
    assert d["dropped"]["percent-is-null"] == n_null
    assert cov.unaccounted == 0


def test_limit_truncates_the_analysis_and_now_says_so(real_schema_db, capsys):
    """`--limit` lived in the SQL, so `--top N` ranked N of the first 50 only."""
    cov_full = CoverageReport("fh")
    full = fh._query_functions(None, 0.0, 100.0, 0,
                               cov=cov_full, db_path=real_schema_db)
    assert len(full) > 1, "fixture must have more rows than the cap"

    cov = CoverageReport("fh")
    capped = fh._query_functions(None, 0.0, 100.0, 1,
                                 cov=cov, db_path=real_schema_db)
    assert len(capped) == 1
    d = cov.as_dict()
    assert d["pool_size_before_limit"] == len(full), \
        "a capped run must still know the size of the pool it sampled"
    assert d["truncated"] is True
    assert d["dropped"]["capped-by-limit"] == len(full) - 1
    assert cov.emit() == EXIT_TRUNCATED
    assert "SAMPLE, not a census" in capsys.readouterr().err


def test_limit_zero_is_a_census(real_schema_db):
    cov = CoverageReport("fh")
    fh._query_functions(None, 0.0, 100.0, 0, cov=cov, db_path=real_schema_db)
    assert cov.as_dict()["truncated"] is False
    assert cov.emit() == EXIT_OK


def test_which_rows_a_cap_keeps_is_now_deterministic(tmp_path):
    """`ORDER BY match_percent DESC` alone left ties in an undefined order.

    Expectation is derived from SQLite itself (all-tied rows, sorted), not from
    a list written here.
    """
    schema, alter = _project_functions_ddl()
    p = tmp_path / "decomp.db"
    conn = sqlite3.connect(str(p))
    conn.executescript(schema)
    conn.execute(alter)
    symbols = [f"?Sym{i:03d}@@YAXXZ" for i in range(40)]
    conn.executemany(
        "INSERT INTO functions (symbol, unit, match_percent_normalized) "
        "VALUES (?, 'default/system/u', 95.0)", [(s,) for s in symbols])
    conn.commit()
    conn.close()

    a = [r["symbol"] for r in fh._query_functions(None, 0.0, 100.0, 5, db_path=p)]
    b = [r["symbol"] for r in fh._query_functions(None, 0.0, 100.0, 5, db_path=p)]
    assert a == b
    assert a == sorted(symbols)[:5], \
        "with every percent tied, the tie-break must decide — and it must be symbol"


def _health(pct: float) -> "fh.HealthReport":
    return fh.HealthReport(symbol="?X@@YAXXZ", demangled="void __cdecl X(void)",
                           unit="u", source_path="", match_percent=pct,
                           total_instructions=10, ceiling_percent=100.0)


def test_batch_table_no_longer_renders_99_97_as_100():
    """`{:6.1f}` on the batch table was the rounded-100 surface.

    Two cases, because the fix has two halves: extra precision handles 99.97,
    and the `<100` guard handles anything that would still round up.
    """
    assert f"{99.97:6.1f}".strip() == "100.0", \
        "sanity: the old format really did round 99.97 up to 100.0"
    table = fh._format_batch_table([_health(99.97)])
    assert "100.0%" not in table, "a sub-100 match must never render as 100.0"
    assert "99.970" in table

    # A value that survives even 3 decimals must still refuse to say 100.
    assert f"{99.9999:.3f}" == "100.000", "sanity: 3 decimals alone are not enough"
    guarded = fh._format_batch_table([_health(99.9999)])
    assert "100.000%" not in guarded.split("\n")[-1].split()[0] + "%"
    assert "<100" in guarded
    # ...while a genuine 100 still prints as 100.
    assert "100.000" in fh._format_batch_table([_health(100.0)])


def test_objdiff_failure_reports_its_reason(monkeypatch):
    """`except Exception: return None` made every failure mode identical."""
    monkeypatch.setattr(fh, "PROJECT_DIR", Path("/definitely/not/a/repo"))
    data, err = fh._run_objdiff("?X@@YAXXZ")
    assert data is None
    assert "objdiff-cli not found" in err, \
        "the reason must travel with the failure, not be discarded"
