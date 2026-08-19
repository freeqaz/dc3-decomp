"""Negative controls for the scanner-honesty retrofit of three scanners:

    scripts/analysis/batch_pattern_scan.py
    scripts/analysis/findarray_receiver_scan.py
    scripts/analysis/vtable_dispatch_scan.py

Every test below RECONSTRUCTS the false negative and asserts the check now
fires.  Following scripts/analysis/tests/test_coverage.py, nothing here compares
a synthesised number against a constant typed in the same sitting — this project
already shipped one such tautology and it let a real error through.  Each test
is one of:

  (a) a REPLAY against the real build/373307D9/report.json, where the expected
      value is recomputed from the file by an independent code path (so the
      assertion breaks if either the scanner or the recomputation drifts), or
  (b) a fixture that PROVABLY exhibits the bug — the historical code shape is
      reproduced inline next to the fixed one, and the test asserts they
      disagree in the documented direction.

The report-backed tests skip cleanly when report.json is absent.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))))
sys.path.insert(0, REPO)

from scripts.analysis import batch_pattern_scan as bps          # noqa: E402
from scripts.analysis import findarray_receiver_scan as frs     # noqa: E402
from scripts.analysis import vtable_dispatch_scan as vds        # noqa: E402
from scripts.analysis.coverage import (                          # noqa: E402
    CoverageReport,
    EXIT_OK,
    EXIT_TRUNCATED,
    EXIT_UNACCOUNTED,
)

REPORT = os.path.join(REPO, "build", "373307D9", "report.json")
needs_report = pytest.mark.skipif(
    not os.path.exists(REPORT), reason=f"{REPORT} not built")


def _load_report():
    with open(REPORT) as f:
        return json.load(f)


def _all_rows(rep):
    """Independent re-derivation of the row population, used as the oracle."""
    for u in rep.get("units", []):
        for f in (u.get("functions") or []):
            yield u.get("name", ""), f


# =========================================================================== #
# batch_pattern_scan
# =========================================================================== #

# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 1 — `if pct is None: continue`, the fake_impl_scan defect
# verbatim.  This is a FIXTURE test: the historical loop is written out next to
# the fixed loader and the two are compared, so it proves the shape rather than
# a number.
# --------------------------------------------------------------------------- #

def _historical_loader(report, min_pct, max_pct, cov):
    """The pre-fix body of load_functions_from_report, kept verbatim.

    `pct = func.get("fuzzy_match_percent"); if pct is None: continue` — a key
    objdiff only emits for functions WE define.
    """
    results = []
    for unit in report.get("units", []):
        for func in unit.get("functions", []):
            pct = func.get("fuzzy_match_percent")
            if pct is None:
                continue                       # <-- the bare continue
            if min_pct <= pct <= max_pct:
                results.append(func)
            else:
                cov.drop("below---min-pct")
    cov.examine(len(results))
    return results


def _fixture_report(tmp_path, n_scored=40, n_bodyless=97):
    """A report.json shaped like the real one: a minority of rows carry no
    `fuzzy_match_percent` at all because we never wrote a body for them."""
    funcs = []
    for i in range(n_scored):
        funcs.append({"name": f"?scored{i}@@YAXXZ", "size": "16",
                      "fuzzy_match_percent": 90.0 + (i % 10) * 0.5,
                      "match_percent_normalized": 95.0})
    for i in range(n_bodyless):
        # NOTE: no `fuzzy_match_percent` key AT ALL — this is what objdiff emits
        # for a function we do not define.
        funcs.append({"name": f"?bodyless{i}@@YAXXZ", "size": "16",
                      "match_percent_normalized": 0.0})
    rep = {"units": [{"name": "default/fixture", "functions": funcs}]}
    p = tmp_path / "report.json"
    p.write_text(json.dumps(rep))
    return p, rep


def test_batch_historical_loader_leaves_bodyless_rows_unaccounted(tmp_path, capsys):
    """The bug, reproduced: the rows vanish and the arithmetic catches it."""
    p, rep = _fixture_report(tmp_path)
    total = sum(len(u["functions"]) for u in rep["units"])
    bodyless = sum(1 for _, f in _all_rows(rep) if f.get("fuzzy_match_percent") is None)

    cov = CoverageReport("batch_pattern_scan(historical)", allow_truncation=False)
    cov.universe(total, "function rows in report.json")
    _historical_loader(rep, 90.0, 99.9, cov)

    rc = cov.emit()
    err = capsys.readouterr().err

    assert bodyless > 0, "fixture must actually contain bodyless rows"
    assert rc == EXIT_UNACCOUNTED
    # The size of the hole is exactly the bodyless population — derived from the
    # fixture, not typed in.
    assert cov.unaccounted == bodyless
    assert "UNACCOUNTED" in err


def test_batch_fixed_loader_accounts_for_every_row(tmp_path, capsys):
    p, rep = _fixture_report(tmp_path)
    total = sum(len(u["functions"]) for u in rep["units"])

    cov = CoverageReport("batch_pattern_scan", allow_truncation=False)
    cov.universe(total, "function rows in report.json")
    kept = bps.load_functions_from_report(p, 90.0, 99.9, None, cov=cov)
    cov.examine(len(kept))

    assert cov.emit() == EXIT_OK
    err = capsys.readouterr().err
    assert cov.unaccounted == 0
    # The bodyless rows are still not examined — but they are now NAMED, which
    # is the entire difference between "exhausted" and "never looked at".
    d = cov.as_dict()
    assert d["dropped"]["no-base-body-outside-band"] > 0
    assert "no-base-body-outside-band" in err


@needs_report
def test_batch_loader_balances_against_the_real_report(capsys):
    """REPLAY: the same arithmetic over the real 48k-row report.json."""
    rep = _load_report()
    total = sum(1 for _ in _all_rows(rep))
    bodyless = sum(1 for _, f in _all_rows(rep) if f.get("fuzzy_match_percent") is None)

    cov = CoverageReport("batch_pattern_scan", allow_truncation=False)
    cov.universe(total, "function rows in report.json")
    kept = bps.load_functions_from_report(REPORT, 90.0, 99.9, None, cov=cov)
    cov.examine(len(kept))

    assert cov.emit() == EXIT_OK
    capsys.readouterr()
    assert cov.unaccounted == 0
    # Every bodyless row is accounted for by the fallback bucket. The oracle is
    # recomputed from the file above, not asserted against a literal.
    assert cov.as_dict()["dropped"]["no-base-body-outside-band"] == bodyless
    assert bodyless > 0, ("report.json has no bodyless rows — either the build "
                          "changed shape or this test lost its subject")


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 2 — the `--limit 200` default, and the fact that its cut was
# BIASED rather than a sample.  Both properties are proved from the real band.
# --------------------------------------------------------------------------- #

@needs_report
def test_batch_old_limit_default_truncated_the_band_and_says_so(capsys):
    OLD_DEFAULT = 200                      # the historical --limit default
    cov = CoverageReport("batch_pattern_scan", allow_truncation=False)
    rep = _load_report()
    cov.universe(sum(1 for _ in _all_rows(rep)), "function rows in report.json")
    band = bps.load_functions_from_report(REPORT, 90.0, 99.9, None, cov=cov)
    band.sort(key=lambda f: (-f["match_percent"], f["symbol"]))

    assert len(band) > OLD_DEFAULT, ("the 90.0-99.9 band no longer exceeds the old "
                                     "cap; this test has lost its subject")
    cut = band[:OLD_DEFAULT]
    cov.cap("--limit", OLD_DEFAULT, before=len(band), after=len(cut))
    cov.examine(len(cut))

    rc = cov.emit()
    err = capsys.readouterr().err
    assert rc == EXIT_TRUNCATED, "a capped run must not exit 0"
    assert "TRUNCATED" in err and "SAMPLE, not a census" in err
    # The banner must name the size of the hole, not merely admit one exists.
    assert str(len(band) - OLD_DEFAULT) in err
    assert cov.as_dict()["dropped"]["capped-by-limit"] == len(band) - OLD_DEFAULT


@needs_report
def test_batch_limit_cut_is_biased_not_a_sample():
    """The cap follows a DESCENDING match% sort, so it is not a random sample:
    the examined slice's floor sits strictly above the band's floor, and an
    entire contiguous match% range is invisible. Proved from the data."""
    OLD_DEFAULT = 200
    band = bps.load_functions_from_report(REPORT, 90.0, 99.9, None, cov=None)
    band.sort(key=lambda f: (-f["match_percent"], f["symbol"]))
    assert len(band) > OLD_DEFAULT

    examined = band[:OLD_DEFAULT]
    never = band[OLD_DEFAULT:]
    lo_examined = min(f["match_percent"] for f in examined)
    lo_band = min(f["match_percent"] for f in band)
    hi_never = max(f["match_percent"] for f in never)

    assert lo_examined > lo_band, "the cut would have to be biased for this to hold"
    assert hi_never <= lo_examined, ("the invisible rows form a contiguous range "
                                     "below the examined floor")


def test_batch_limit_default_is_now_uncapped():
    """CLI DEFAULT CHANGE: --limit was 200, it is now 0 (= no cap), and the help
    string has to say what it used to be and why it changed."""
    import argparse
    ap = argparse.ArgumentParser()
    # Rebuild the flag the way main() declares it by parsing main's parser.
    parser = _build_batch_parser()
    assert parser.get_default("limit") == 0
    help_text = _flag_help(parser, "--limit")
    assert "200" in help_text, "the help must name the OLD default"
    assert "biased" in help_text.lower() or "bias" in help_text.lower()


def _build_batch_parser():
    """Run main()'s argparse construction without running the scan."""
    import argparse
    captured = {}
    real_parse = argparse.ArgumentParser.parse_args

    def fake_parse(self, *a, **kw):
        captured["parser"] = self
        raise _StopArgparse()

    argparse.ArgumentParser.parse_args = fake_parse
    try:
        with pytest.raises(_StopArgparse):
            bps.main()
    finally:
        argparse.ArgumentParser.parse_args = real_parse
    return captured["parser"]


class _StopArgparse(Exception):
    pass


def _flag_help(parser, flag):
    for a in parser._actions:
        if flag in a.option_strings:
            return a.help or ""
    raise AssertionError(f"{flag} not found")


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 3 — `--pattern` was unvalidated, and the scanner's OWN
# documented example used a value it never emits.
# --------------------------------------------------------------------------- #

def test_the_documented_pattern_example_was_not_a_real_pattern_type():
    """`--pattern extrwi` appeared in the usage block; the emitted type is
    `extrwi_rlwinm`. Unvalidated, it ran and printed zero hits."""
    emitted = _pattern_types_the_detectors_can_emit()
    assert "extrwi" not in emitted, "the historical example really was a typo"
    assert "extrwi_rlwinm" in emitted


def _pattern_types_the_detectors_can_emit():
    """Derive the pattern_type vocabulary from the SOURCE of the detectors, so
    this is not a restatement of PATTERN_CHOICES."""
    import re as _re
    src = open(os.path.join(REPO, "scripts", "analysis",
                            "batch_pattern_scan.py")).read()
    return set(_re.findall(r'pattern_type="([a-z0-9_]+)"', src))


def test_pattern_choices_cover_every_emitted_type_and_reject_typos():
    emitted = _pattern_types_the_detectors_can_emit()
    missing = emitted - set(bps.PATTERN_CHOICES)
    assert not missing, f"--pattern cannot select {sorted(missing)}"

    parser = _build_batch_parser()
    with pytest.raises(SystemExit) as e:
        parser.parse_args(["--pattern", "extrwi"])
    assert e.value.code == 2, "a typo must be rejected, not silently yield 0 hits"
    # ...and the real value still parses.
    assert parser.parse_args(["--pattern", "extrwi_rlwinm"]).pattern == "extrwi_rlwinm"


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 4 — a total objdiff outage used to render as `TOTAL: 0 hit(s)`.
# --------------------------------------------------------------------------- #

@needs_report
def test_total_objdiff_failure_does_not_render_as_a_clean_scan(monkeypatch, capsys):
    monkeypatch.setattr(bps, "run_objdiff_json",
                        lambda symbol: (None, "objdiff rc=1: boom"))
    monkeypatch.setattr(sys, "argv",
                        ["batch_pattern_scan.py", "--min", "99.89", "--max", "99.9"])
    with pytest.raises(SystemExit) as e:
        bps.main()
    out = capsys.readouterr()

    assert "NO FUNCTIONS WERE INSPECTED" in out.out
    assert "TOOL FAILURE" in out.out
    assert "objdiff failures" in out.out
    # The failures are DROPS, not silent successes, so the coverage arithmetic
    # still balances and the run does not claim to be a census of anything.
    assert "objdiff-failed" in out.err
    assert e.value.code == EXIT_OK       # balanced, but the text is unmistakable
    # The old summary line is gone: `Scanned:` was the post-truncation count.
    assert "| Scanned:" not in out.out
    assert "In band:" in out.out and "Inspected:" in out.out


# =========================================================================== #
# findarray_receiver_scan
# =========================================================================== #

# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 5 — the relative default path with no `else`.  Run from any
# cwd but the repo root, the file list came out empty and the scan printed a
# clean bill of health for ZERO files.
# --------------------------------------------------------------------------- #

def _historical_path_expansion(paths):
    """The pre-fix loop, verbatim — note the missing `else`."""
    from pathlib import Path
    files = []
    for p in paths:
        p = Path(p)
        if p.is_dir():
            files.extend(p.rglob('*.cpp'))
            files.extend(p.rglob('*.h'))
        elif p.is_file():
            files.append(p)
        # <-- no else: a non-existent path is silently ignored
    return files


def test_missing_input_path_used_to_be_silent_and_is_now_loud(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    bogus = "definitely-not-a-directory-9f3a/"

    # The bug: zero files, no error, and the caller then printed "No suspicious
    # receiver confusion patterns found."
    assert _historical_path_expansion([bogus]) == []

    files, resolved, errors = frs.resolve_paths([bogus])
    assert files == []
    assert errors, "a non-existent input path must be reported"
    assert bogus in errors[0] and "no such file or directory" in errors[0]


def test_missing_input_path_exits_nonzero_end_to_end(tmp_path):
    r = subprocess.run(
        [sys.executable, os.path.join(REPO, "scripts", "analysis",
                                      "findarray_receiver_scan.py"),
         "definitely-not-a-directory-9f3a/"],
        cwd=str(tmp_path), capture_output=True, text=True)
    assert r.returncode == 2
    assert "refusing to scan" in r.stderr
    # The old clean-bill-of-health line must NOT appear.
    assert "No suspicious receiver confusion patterns found" not in r.stdout


def test_default_relative_path_still_resolves_from_a_foreign_cwd(tmp_path, monkeypatch):
    """The historical failure mode, end to end: `src/` from another cwd."""
    monkeypatch.chdir(tmp_path)
    assert _historical_path_expansion(["src/"]) == []      # the bug
    files, resolved, errors = frs.resolve_paths(["src/"])
    assert not errors
    assert len(files) > 0, "the repo-root fallback must find the real source tree"


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 6 — an unreadable file became `return []`, i.e. "no bugs".
# --------------------------------------------------------------------------- #

def test_undecodable_file_is_counted_not_silently_clean(tmp_path):
    bad = tmp_path / "bad.cpp"
    # Latin-1 bytes that are not valid UTF-8, plus a genuine FindArray pattern
    # so the file WOULD be relevant if it could be read at all.
    bad.write_bytes(b'void f(){ DataArray*a=c->FindArray("x"); c->FindArray("y"); }'
                    b'\n// \xe9\xe8\xff caf\xe9\n')

    findings, disposition = frs.scan_file(str(bad))
    assert findings == []
    assert disposition == 'unreadable', \
        "a decode failure must be distinguishable from a clean file"

    # And a clean-but-readable file with the same emptiness is a DIFFERENT
    # disposition — which is the whole point.
    ok = tmp_path / "ok.cpp"
    ok.write_text("int main(){return 0;}\n")
    assert frs.scan_file(str(ok))[1] == 'not-relevant'


@needs_report
def test_real_tree_has_undecodable_files_and_they_are_dropped_by_name():
    """REPLAY against the real src/ tree: the count is recomputed independently."""
    from pathlib import Path
    files, _, errors = frs.resolve_paths(["src/"])
    assert not errors
    oracle = 0
    for f in files:
        try:
            f.read_text()
        except (IOError, UnicodeDecodeError):
            oracle += 1
    assert oracle > 0, "src/ no longer contains an undecodable file"

    got = sum(1 for f in files if frs.scan_file(str(f))[1] == 'unreadable')
    assert got == oracle


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 7 — the relevance gate counts only 'FindArray' while the
# regexes understand six lookup methods.  The gate is NOT widened here; the test
# proves the blind spot is real AND that it is now a counted number.
# --------------------------------------------------------------------------- #

# The MetagameRank shape: a child is stored, the child IS used as a receiver,
# and the PARENT is then used bare for a lookup that should have gone through
# the child. Spelled with FindStr, which the regexes understand and the
# relevance gate does not count.
FINDSTR_BUG = '''\
void Foo::Load(DataArray* cfg) {
    DataArray* taskArr = cfg->FindStr("tasks");
    taskArr->FindStr("repeatable");
    cfg->FindStr("one_time");
}
'''


def test_gate_blind_spot_is_real_and_now_counted(tmp_path):
    f = tmp_path / "findstr_only.cpp"
    f.write_text(FINDSTR_BUG)

    findings, disposition = frs.scan_file(str(f))
    # The blind spot, demonstrated: a textbook mixed-receiver shape, zero findings.
    assert findings == [], "if this starts finding things the gate was widened"
    assert disposition == 'gate-missed-non-findarray'

    # The identical bug spelled with FindArray IS found — so the difference is
    # the gate, not the detector.
    g = tmp_path / "findarray.cpp"
    g.write_text(FINDSTR_BUG.replace("FindStr", "FindArray"))
    gf, gd = frs.scan_file(str(g))
    assert gd == 'examined'
    assert any(x['severity'] == 'MIXED_RECEIVER' for x in gf), \
        "the detector handles this shape; only the relevance gate rejected it"


@needs_report
def test_gate_blind_spot_population_is_reported_on_the_real_tree():
    files, _, errors = frs.resolve_paths(["src/"])
    assert not errors
    oracle = 0
    for f in files:
        try:
            c = f.read_text()
        except (IOError, UnicodeDecodeError):
            continue
        if c.count('FindArray') >= 2:
            continue
        if 'ObjDirItr' in c and 'SetName' in c:
            continue
        if sum(c.count(m) for m in frs.LOOKUP_METHODS) >= 2:
            oracle += 1
    got = sum(1 for f in files
              if frs.scan_file(str(f))[1] == 'gate-missed-non-findarray')
    assert got == oracle
    assert oracle > 0, ("the gate no longer excludes any multi-lookup file — "
                        "either it was widened or this test lost its subject")


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 8 — SHADOW_PARENT findings were filtered out BEFORE counting,
# so the summary printed `0 SHADOW_PARENT` while some existed.
# --------------------------------------------------------------------------- #

@needs_report
def test_shadow_parent_count_is_not_pinned_to_zero_without_all():
    script = os.path.join(REPO, "scripts", "analysis", "findarray_receiver_scan.py")
    plain = subprocess.run([sys.executable, script, "--json"],
                           cwd=REPO, capture_output=True, text=True)
    with_all = subprocess.run([sys.executable, script, "--all", "--json"],
                              cwd=REPO, capture_output=True, text=True)
    p = json.loads(plain.stdout)
    a = json.loads(with_all.stdout)

    n_shadow = a["counts_before_display_filter"].get("SHADOW_PARENT", 0)
    assert n_shadow > 0, "the tree no longer has SHADOW_PARENT findings"

    # THE BUG: without --all the count used to read 0. The count is now taken
    # before the display filter, so both runs agree — while the LISTING still
    # differs, which is what --all is for.
    assert p["counts_before_display_filter"] == a["counts_before_display_filter"]
    assert not any(f["severity"] == "SHADOW_PARENT" for f in p["findings"])
    assert sum(1 for f in a["findings"] if f["severity"] == "SHADOW_PARENT") == n_shadow

    # The human summary must say "suppressed", never a bare 0.
    human = subprocess.run([sys.executable, script], cwd=REPO,
                           capture_output=True, text=True)
    assert "SUPPRESSED" in human.stdout
    assert f"{n_shadow} SHADOW_PARENT" in human.stdout


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 9 — set iteration over strings drove the output order.
# Replays the historical evidence: four PYTHONHASHSEED values, four hashes.
# --------------------------------------------------------------------------- #

TWO_CHILDREN = '''\
void Foo::Load(DataArray* cfg) {
    DataArray* zebraArr = cfg->FindArray("zebra");
    DataArray* alphaArr = cfg->FindArray("alpha");
    DataArray* midArr   = cfg->FindArray("mid");
    zebraArr->FindArray("z1");
    alphaArr->FindArray("a1");
    cfg->FindArray("late");
}
'''


def test_child_var_order_is_hash_seed_independent(tmp_path):
    f = tmp_path / "children.cpp"
    f.write_text(TWO_CHILDREN)
    script = os.path.join(REPO, "scripts", "analysis", "findarray_receiver_scan.py")

    outs = set()
    for seed in ("0", "1", "2", "3"):
        env = dict(os.environ, PYTHONHASHSEED=seed)
        r = subprocess.run([sys.executable, script, "--all", "--json", str(f)],
                           cwd=REPO, capture_output=True, text=True, env=env)
        assert r.returncode == 0, r.stderr
        outs.add(r.stdout)
    assert len(outs) == 1, f"output order varies with PYTHONHASHSEED ({len(outs)} variants)"

    # And the emitted order really is sorted, not merely stable by luck.
    d = json.loads(outs.pop())
    for finding in d["findings"]:
        names = [c["var"] for c in finding.get("children", [])]
        assert names == sorted(names)


def test_set_iteration_really_was_the_mechanism():
    """Control for the control: prove a str-set's order is seed-dependent, so
    the test above is testing something real and not a no-op."""
    prog = ("import json;"
            "print(json.dumps(list({'zebraArr','alphaArr','midArr'})))")
    seen = set()
    for seed in ("0", "1", "2", "3", "4", "5"):
        r = subprocess.run([sys.executable, "-c", prog],
                           capture_output=True, text=True,
                           env=dict(os.environ, PYTHONHASHSEED=seed))
        seen.add(r.stdout)
    assert len(seen) > 1, "str set iteration is not seed-sensitive on this build"


# =========================================================================== #
# vtable_dispatch_scan
# =========================================================================== #

# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 10 — `if n is None or raw is None: continue` dropped the same
# 16,920 rows uncounted.  The skip is defensible; being unable to state it was
# not.  This also pins the ONE row that breaks the blanket justification.
# --------------------------------------------------------------------------- #

@needs_report
def test_vtable_gather_accounts_for_every_report_row(capsys):
    rep = _load_report()
    total = sum(1 for _ in _all_rows(rep))

    cov = CoverageReport("vtable_dispatch_scan", allow_truncation=False)
    cov.universe(total, "function rows in report.json")
    cands = vds.gather_candidates(REPORT, 98.0, False, cov=cov)
    cov.examine(len(cands))

    assert cov.emit() == EXIT_OK
    capsys.readouterr()
    assert cov.unaccounted == 0
    d = cov.as_dict()
    # The population is now named rather than absent.
    noraw = sum(1 for _, f in _all_rows(rep) if f.get("fuzzy_match_percent") is None)
    named = (d["dropped"].get("no-fuzzy-percent-norm-zero", 0)
             + d["dropped"].get("no-fuzzy-percent-but-nonzero-norm", 0))
    assert named == noraw and noraw > 0


@needs_report
def test_the_all_of_them_are_norm_zero_justification_has_an_exception():
    """The audit justified the skip with "all 16,920 have norm == 0.0, so no
    raw-vs-norm gap is possible". That generalisation is off by one, and the
    scanner now buckets the exception separately instead of absorbing it.

    Both sides are computed from report.json: the oracle by scanning the file,
    the claim by running the scanner. Neither is a literal typed here.
    """
    rep = _load_report()
    exceptions = [(u, f["name"]) for u, f in _all_rows(rep)
                  if f.get("fuzzy_match_percent") is None
                  and f.get("match_percent_normalized")]
    assert exceptions, ("no exception rows left — if the build changed this test "
                        "should be retired deliberately, not deleted silently")

    cov = CoverageReport("vtable_dispatch_scan")
    cov.universe(sum(1 for _ in _all_rows(rep)))
    vds.gather_candidates(REPORT, 98.0, False, cov=cov)
    d = cov.as_dict()
    assert d["dropped"]["no-fuzzy-percent-but-nonzero-norm"] == len(exceptions)
    # ...and it is genuinely a separate bucket, not folded into the big one.
    assert d["dropped"]["no-fuzzy-percent-norm-zero"] != d["dropped"][
        "no-fuzzy-percent-but-nonzero-norm"]


@needs_report
def test_vtable_scanned_line_is_not_the_denominator():
    """`scanned : N` was the post-filter candidate count and was quoted as the
    scope of a 'the class looks exhausted' conclusion. The candidate set must be
    provably far smaller than the row population."""
    rep = _load_report()
    total = sum(1 for _ in _all_rows(rep))
    cands = vds.gather_candidates(REPORT, 98.0, False, cov=None)
    assert 0 < len(cands) < total / 2, \
        "candidates must be a small minority of rows for the omission to matter"


# --------------------------------------------------------------------------- #
# NEGATIVE CONTROL 11 — as_completed order + a sort keyed only on
# (confidence, raw), and `errors[:20]` slicing a completion-ordered list.
# --------------------------------------------------------------------------- #

def _fake_results():
    """Rows that TIE on (confidence, raw) — the ordering the old key cannot
    resolve. Distinct symbols, so a total order exists."""
    return [{"symbol": s, "unit": "u", "raw": raw, "norm": 99.9,
             "best_confidence": conf, "hits": [{"idx": 0}]}
            for s, raw, conf in [
                ("?zeta@@YAXXZ", 99.5, "strong"),
                ("?alpha@@YAXXZ", 99.5, "strong"),
                ("?mid@@YAXXZ", 99.5, "strong"),
                ("?beta@@YAXXZ", 98.0, "medium"),
                ("?gamma@@YAXXZ", 98.0, "medium"),
            ]]


RANK = {"strong": 0, "medium": 1, "weak": 2}


def test_old_hit_sort_key_was_completion_order_dependent():
    """The bug: with ties on (confidence, raw), Python's stable sort preserves
    whatever order as_completed() produced."""
    a = _fake_results()
    b = list(reversed(_fake_results()))     # a different completion order
    old = lambda r: (RANK[r["best_confidence"]], r["raw"] or 0)
    assert [x["symbol"] for x in sorted(a, key=old)] != \
           [x["symbol"] for x in sorted(b, key=old)]


def test_new_hit_sort_key_is_total_and_order_invariant():
    new = lambda r: (RANK[r["best_confidence"]],
                     r["raw"] if r["raw"] is not None else 0,
                     r["symbol"])
    a = _fake_results()
    b = list(reversed(_fake_results()))
    assert [x["symbol"] for x in sorted(a, key=new)] == \
           [x["symbol"] for x in sorted(b, key=new)]
    # ...and the primary ordering (confidence, then raw) is unchanged.
    ranks = [RANK[x["best_confidence"]] for x in sorted(a, key=new)]
    assert ranks == sorted(ranks)


def test_errors_are_sorted_before_the_twenty_row_slice():
    errs = [{"symbol": f"?sym{i:02d}@@YAXXZ", "unit": "u", "error": "boom"}
            for i in range(25)]
    shuffled = errs[13:] + errs[:13]        # a different completion order

    old_a = {e["symbol"] for e in errs[:20]}
    old_b = {e["symbol"] for e in shuffled[:20]}
    assert old_a != old_b, "the unsorted slice really did depend on arrival order"

    key = lambda r: (r["symbol"], r.get("unit") or "")
    new_a = {e["symbol"] for e in sorted(errs, key=key)[:20]}
    new_b = {e["symbol"] for e in sorted(shuffled, key=key)[:20]}
    assert new_a == new_b
