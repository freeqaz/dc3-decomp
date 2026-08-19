"""Negative controls for the DTA scanner family + audit_normalized_masking.

Same standard as `test_coverage.py`: every test here RECONSTRUCTS a false
negative and asserts the check now fires. Nothing below compares a synthesised
number against a constant typed in the same sitting — where a count is asserted,
the expected value is RE-DERIVED from the artifact by an independent code path
(re-globbing the corpus, re-parsing the JSON), or the assertion is relational
("the run must report MORE marker lines than it managed to parse").

THE DEFECT BEING CONTROLLED FOR
===============================
All four DTA scanners loaded their config behind a bare ``if p.exists():`` with
no ``else``. ``orig-assets/`` is untracked, so it exists in the main checkout and
is absent from every git worktree. From a worktree they therefore parsed ZERO
DTA files, every key became unresolvable, every check short-circuited, and they
printed

    No DTA access issues found.

to **stdout**, while the only hint — ``Loaded 0 main configs, 0 unique keys`` —
went to **stderr**. Redirect stdout to a file and all you keep is the reassuring
half. This was live, not hypothetical: `dta_hierarchy_scan.py --query tasks` run
from a worktree printed `Parsed 0 DTA files, 0 unique keys` and then answered the
query as though the answer were known.

The single most important control in this file is therefore
`test_*_with_no_corpus_does_not_print_a_clean_verdict`: point a scanner at an
empty corpus and assert its STDOUT does not read as a clean bill of health.
"""
from __future__ import annotations

import json
import os
import re
import pathlib
import re
import subprocess
import sys

import pytest

REPO = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))))
ANALYSIS = os.path.join(REPO, "scripts", "analysis")

sys.path.insert(0, REPO)

from scripts.analysis import dta_hierarchy_scan as HS          # noqa: E402
from scripts.analysis import dta_access_audit as AA            # noqa: E402
from scripts.analysis import audit_normalized_masking as ANM   # noqa: E402
from scripts.analysis.coverage import (                        # noqa: E402
    EXIT_UNACCOUNTED, EXIT_NO_INPUT,
)

HIERARCHY = os.path.join(ANALYSIS, "dta_hierarchy_scan.py")
ACCESS = os.path.join(ANALYSIS, "dta_access_audit.py")
TRACE = os.path.join(ANALYSIS, "dta_trace_validator.py")
DATAFLOW = os.path.join(ANALYSIS, "dta_dataflow.py")

# Phrases that, standing alone, tell a reader "I looked and there was nothing
# wrong". Any of these on stdout with an empty corpus is the bug.
CLEAN_VERDICTS = (
    "No DTA access issues found.",
    "No DTA hierarchy mismatches found.",
    "No validation issues found.",
)


#: A path that cannot exist, used to switch OFF the implicit corpus sweep.
NO_CORPUS = "/nonexistent/dta-corpus-that-must-not-be-found"


def run(argv, cwd=None, seed=None):
    """Run a scanner with its corpus BOUNDED BY THE ARGUMENTS, never by the CWD.

    Three of these scanners used to sweep a hardcoded `orig-assets/extracted`
    relative to the working directory, unconditionally and IN ADDITION to
    --dta-dir. So --dta-dir could not actually bound the corpus, and every
    "empty corpus" control in this file was passing only because git worktrees
    do not contain orig-assets/ (it is untracked). Symlink the corpus in -- or
    simply run the suite from the main checkout, which is the merge target --
    and four of these controls silently stopped being controls: the scanner
    found 247 DTA files and printed real findings where the test expected
    INCONCLUSIVE. A control that passes because of where you ran it is not a
    control. `--extra-root` now names that sweep, and this helper switches it
    off unless a test asks for it, so the fixture corpus is the whole corpus.
    """
    env = dict(os.environ)
    env["PYTHONPATH"] = REPO
    if seed is not None:
        env["PYTHONHASHSEED"] = str(seed)
    if "--extra-root" not in argv and _supports_extra_root(argv[0]):
        argv = argv + ["--extra-root", NO_CORPUS]
    return subprocess.run([sys.executable] + argv, cwd=cwd or REPO, env=env,
                          capture_output=True, text=True, timeout=900)


def _supports_extra_root(script):
    """Read it off the tool rather than hardcoding a list that can rot."""
    return "--extra-root" in open(script, errors="replace").read()


# --------------------------------------------------------------------------- #
# A tiny but REAL corpus, so "the scanner found nothing" and "the scanner had
# nothing to look at" can be told apart by experiment rather than by assertion.
# --------------------------------------------------------------------------- #

GOOD_DTA = """\
;; a miniature stand-in for ham_keep.dta
(metagame_rank
   (tasks
      (one_time 10 20 30)
      (repeatable 1 2)))
(sound
   (volume 0.5))
"""


@pytest.fixture
def corpus(tmp_path):
    """A 2-file DTA corpus + a src tree that exercises the scanners."""
    d = tmp_path / "dta"
    d.mkdir()
    (d / "main.dta").write_text(GOOD_DTA)
    (d / "extra.dta").write_text("(store (items a b c))\n")

    src = tmp_path / "src"
    src.mkdir()
    (src / "Ok.cpp").write_text(
        'void f() {\n'
        '  DataArray *cfg = SystemConfig("metagame_rank");\n'
        '  DataArray *t = cfg->FindArray("tasks");\n'
        '  int n = t->FindArray("one_time")->Int(1);\n'
        '}\n')
    # The typo'd-key bug class: "one_tyme" exists in NO dta file anywhere.
    (src / "Typo.cpp").write_text(
        'void g() {\n'
        '  DataArray *cfg = SystemConfig("metagame_rank");\n'
        '  DataArray *t = cfg->FindArray("tasks");\n'
        '  int n = t->FindArray("one_tyme")->Int(1);\n'
        '}\n')
    return {"dta": d, "src": src, "main": d / "main.dta"}


# --------------------------------------------------------------------------- #
# CONTROL 1 — the headline defect, per scanner. An empty corpus must not print
# a clean verdict, and the warning must be on STDOUT (a redirect of stdout is
# exactly how the reassuring half survived alone).
# --------------------------------------------------------------------------- #

@pytest.mark.parametrize("script,extra", [
    (HIERARCHY, []),
    (ACCESS, []),
])
def test_dta_scanner_with_no_corpus_does_not_print_a_clean_verdict(
        script, extra, tmp_path, corpus):
    """THE control. Reconstructs a worktree run: the corpus paths do not exist."""
    missing = str(tmp_path / "does" / "not" / "exist")
    argv = [script, "--src-dir", str(corpus["src"]),
            "--main-configs", missing + "/ham_keep.dta"]
    argv += ["--dta-dir", missing] if script == HIERARCHY else ["--dta-root", missing]
    r = run(argv + extra)

    for phrase in CLEAN_VERDICTS:
        assert phrase not in r.stdout, (
            f"{os.path.basename(script)} printed a clean verdict with an empty "
            f"corpus — this is the exact false negative:\n{r.stdout[:400]}")
    assert "INCONCLUSIVE" in r.stdout, (
        "the 'I checked nothing' warning must be on STDOUT, not only stderr — "
        f"stdout was:\n{r.stdout[:400]}")
    assert "CHECKED NOTHING" in r.stdout
    assert r.returncode != 0, "an inconclusive run must not exit 0"
    # And the surviving-half test, mechanically: stdout ALONE must be enough.
    assert "0 DTA files" in r.stdout


def test_trace_validator_with_no_corpus_does_not_print_a_clean_verdict(tmp_path):
    log = tmp_path / "t.log"
    log.write_text(
        "DTA_TRACE: metagame_rank.tasks.one_time[1] via Int "
        "(file main.dta, line 3)\n")
    missing = str(tmp_path / "nope")
    r = run([TRACE, str(log), "--main-configs", missing + "/ham_keep.dta",
             "--dta-dir", missing])
    for phrase in CLEAN_VERDICTS:
        assert phrase not in r.stdout, r.stdout[:400]
    assert "INCONCLUSIVE" in r.stdout
    assert r.returncode != 0


def test_query_mode_does_not_answer_from_an_empty_hierarchy(tmp_path):
    """`--query tasks` from a worktree printed 'not found in any DTA file.'

    That sentence is indistinguishable from the truth, and it was produced by a
    hierarchy containing zero files.
    """
    missing = str(tmp_path / "nope")
    r = run([HIERARCHY, "--query", "tasks",
             "--main-configs", missing + "/ham_keep.dta", "--dta-dir", missing])
    assert "not found in any DTA file." not in r.stdout
    assert "INCONCLUSIVE" in r.stdout
    assert r.returncode != 0


# --------------------------------------------------------------------------- #
# CONTROL 2 — control for the control. With a REAL corpus the same command must
# run to completion, exit 0, and carry its denominator. Otherwise control 1
# could be passing because the scanner is simply broken.
# --------------------------------------------------------------------------- #

def _dta_file_count(d):
    """Independent recount of the corpus, by a different code path than the tool."""
    return len(list(d.rglob("*.dta")))


def test_access_audit_with_a_real_corpus_reports_a_denominator(corpus, tmp_path):
    covj = tmp_path / "cov.json"
    r = run([ACCESS, "--src-dir", str(corpus["src"]),
             "--main-configs", str(corpus["main"]),
             "--dta-root", str(corpus["dta"]),
             "--coverage-json", str(covj)])
    assert r.returncode == 0, r.stdout + r.stderr
    assert "INCONCLUSIVE" not in r.stdout
    # The verdict may be clean — but it may NEVER be clean in isolation.
    if "No DTA access issues found" in r.stdout:
        assert re.search(r"of \d+ total", r.stdout), (
            "a clean verdict must state what it checked:\n" + r.stdout)
    d = json.loads(covj.read_text())
    assert d["dta_files_parsed"] == _dta_file_count(corpus["dta"])
    assert d["unaccounted"] == 0, "the source-file census must balance"
    assert d["access_sites"]["unaccounted"] == 0, "the call-site census must balance"


def test_hierarchy_scan_with_a_real_corpus_reports_a_denominator(corpus, tmp_path):
    covj = tmp_path / "cov.json"
    r = run([HIERARCHY, "--src-dir", str(corpus["src"]),
             "--main-configs", str(corpus["main"]),
             "--dta-dir", str(corpus["dta"]),
             "--coverage-json", str(covj)])
    assert r.returncode == 0, r.stdout + r.stderr
    assert "INCONCLUSIVE" not in r.stdout
    d = json.loads(covj.read_text())
    assert d["dta_files_parsed"] == _dta_file_count(corpus["dta"])
    assert d["unaccounted"] == 0
    assert d["call_sites"]["unaccounted"] == 0
    # A clean verdict must be accompanied by how many sites were verifiable.
    if "No DTA hierarchy mismatches found" in r.stdout:
        assert re.search(r"of \d+ total", r.stdout), r.stdout


# --------------------------------------------------------------------------- #
# CONTROL 3 — the key-that-exists-NOWHERE population.
#
# `if not nodes: continue  # Key not found in any DTA` dropped, uncounted, the
# single most valuable population this family can produce: the typo'd FindArray.
# --------------------------------------------------------------------------- #

def test_absent_key_is_reported_not_silently_skipped(corpus, tmp_path):
    covj = tmp_path / "cov.json"
    r = run([HIERARCHY, "--src-dir", str(corpus["src"]),
             "--main-configs", str(corpus["main"]),
             "--dta-dir", str(corpus["dta"]),
             "--coverage-json", str(covj)])
    assert r.returncode == 0, r.stdout + r.stderr
    # It is in the human report...
    assert "UNVERIFIABLE" in r.stdout
    assert "one_tyme" in r.stdout, (
        "the typo'd key must be surfaced as its own category:\n" + r.stdout)
    # ...and it is counted, not merely mentioned.
    d = json.loads(covj.read_text())
    absent = d["keys_absent_from_every_dta"]
    assert "one_tyme" in absent
    assert d["call_sites"]["dropped"]["key-absent-from-every-dta"] >= 1
    # The key that DOES exist must not be in there — otherwise the category
    # would be firing on everything and would carry no information.
    assert "one_time" not in absent


def test_absent_key_population_is_counted_by_the_access_auditor(corpus, tmp_path):
    covj = tmp_path / "cov.json"
    r = run([ACCESS, "--src-dir", str(corpus["src"]),
             "--main-configs", str(corpus["main"]),
             "--dta-root", str(corpus["dta"]),
             "--coverage-json", str(covj)])
    assert r.returncode == 0, r.stdout + r.stderr
    d = json.loads(covj.read_text())
    assert "one_tyme" in d["keys_absent_from_every_dta"]
    assert "one_tyme" in r.stdout


# --------------------------------------------------------------------------- #
# CONTROL 4 — a TRUNCATED hierarchy must not look like a small one.
#
# `process_include`: `if filepath is None: return` plus `except (IOError,
# UnicodeDecodeError): pass`. An unresolvable #include silently shrinks the very
# tree every check is measured against.
# --------------------------------------------------------------------------- #

def test_unresolvable_include_is_counted(tmp_path):
    HS.reset_parse_stats()
    p = tmp_path / "m.dta"
    p.write_text("(root\n   #include no_such_file.dta\n   (a 1))\n")
    root = HS.parse_dta_file(str(p))
    assert root is not None, "the file itself parses; only the include is missing"
    assert HS.PARSE_STATS["include-unresolved"] == 1
    assert "no_such_file.dta" in HS.UNRESOLVED_INCLUDES
    HS.reset_parse_stats()


def test_include_depth_cap_is_counted(tmp_path):
    """A 12-deep #include chain silently stopped at 10 and reported nothing."""
    HS.reset_parse_stats()
    depth = 13
    for i in range(depth):
        nxt = f"  #include f{i + 1}.dta\n" if i + 1 < depth else ""
        (tmp_path / f"f{i}.dta").write_text(f"(level{i}\n{nxt})\n")
    root = HS.parse_dta_file(str(tmp_path / "f0.dta"))
    assert root is not None
    assert HS.PARSE_STATS["include-depth-capped"] >= 1, (
        "the depth cap dropped a subtree without saying so")
    # Control: a shallow chain must NOT trip the cap, or the counter is just
    # always-on noise.
    HS.reset_parse_stats()
    (tmp_path / "s1.dta").write_text("(s1 (a 1))\n")
    (tmp_path / "s0.dta").write_text("(s0\n  #include s1.dta\n)\n")
    HS.parse_dta_file(str(tmp_path / "s0.dta"))
    assert HS.PARSE_STATS["include-depth-capped"] == 0
    HS.reset_parse_stats()


def test_unparseable_dta_is_not_credited_as_loaded(tmp_path, monkeypatch):
    """`add_file` used to return None on failure and the caller counted it anyway."""
    h = HS.DTAHierarchy()
    good = tmp_path / "g.dta"
    good.write_text("(a (b 1))\n")
    assert h.add_file(good) is True

    bad = tmp_path / "b.dta"
    bad.write_text("(a (b 1))\n")
    monkeypatch.setattr(HS, "parse_dta_file", lambda p: None)
    assert h.add_file(bad) is False, "a failed parse must not report success"


# --------------------------------------------------------------------------- #
# CONTROL 5 — the trace validator's pre-count `continue`.
#
# `entry = parse_trace_line(line); if entry is None: continue` runs BEFORE
# `total_traces += 1`. If the native emitter's format drifts, every line is
# dropped before it is counted and the tool prints
# "Validated 0 unique trace entries ... across 0 total runtime accesses" — a
# format break rendering as a successful empty run.
# --------------------------------------------------------------------------- #

def _write_drifted_log(path):
    """Every line carries the marker; NONE matches TRACE_RE (format drift)."""
    lines = [
        "DTA_TRACE: metagame_rank.tasks.one_time idx=1 method=Int",
        "DTA_TRACE: sound.volume idx=1 method=Float",
        "DTA_TRACE: store.items idx=2 method=Sym",
    ]
    path.write_text("\n".join(lines) + "\n")
    return lines


def test_total_format_drift_is_reported_not_rendered_as_an_empty_success(
        corpus, tmp_path):
    log = tmp_path / "drift.log"
    written = _write_drifted_log(log)
    # Independent recount of the artifact, by a different code path than the tool.
    marker_lines = sum(1 for ln in log.read_text().splitlines()
                       if "DTA_TRACE:" in ln)
    assert marker_lines == len(written)

    r = run([TRACE, str(log), "--json",
             "--main-configs", str(corpus["main"]),
             "--dta-dir", str(corpus["dta"])])
    d = json.loads(r.stdout)
    st = d["stats"]
    assert st["marker_lines"] == marker_lines
    assert st["total_traces"] == 0, "none of these parse — that is the premise"
    # The whole point: the loss is VISIBLE.
    assert st["malformed_lines"] == marker_lines
    assert st["marker_lines"] > st["total_traces"]
    assert d["_coverage"]["dropped"]["trace-line-malformed"] == marker_lines
    assert d["_coverage"]["unaccounted"] == 0

    # ...and BALANCED IS NOT NON-EMPTY.  The books add up perfectly here --
    # 3 marker lines, 0 examined, 3 dropped, unaccounted 0 -- and this run
    # still checked nothing.  It used to exit 0, i.e. exactly like a run that
    # validated every trace in the log.  That is the residue of the DTA
    # defect: the corpus gate keys on "the corpus was empty", not on "this run
    # examined nothing".  The interesting claim is the DISAGREEMENT with a run
    # that did work, so assert against that rather than against a constant.
    assert d["_coverage"]["examined"] == 0
    assert d["_coverage"]["examined_nothing"] is True
    assert r.returncode == EXIT_NO_INPUT, r.stdout + r.stderr

    # The disagreement, measured rather than asserted: the SAME log plus one
    # parseable line must not exit the same way.
    ok_log = tmp_path / "one_good.log"
    ok_log.write_text(log.read_text() +
                      "DTA_TRACE: metagame_rank.tasks.one_time[1] via Int "
                      "(file main.dta, line 3)\n")
    ok = run([TRACE, str(ok_log), "--json",
              "--main-configs", str(corpus["main"]),
              "--dta-dir", str(corpus["dta"])])
    ok_d = json.loads(ok.stdout)
    assert ok_d["_coverage"]["examined"] > 0, "the control must really do work"
    assert ok_d["_coverage"]["examined_nothing"] is False
    assert ok.returncode != r.returncode, (
        "a run that validated nothing must not exit like one that validated "
        "something")


def test_mixed_log_reports_both_halves(corpus, tmp_path):
    log = tmp_path / "mixed.log"
    log.write_text(
        "unrelated engine log line\n"
        "DTA_TRACE: metagame_rank.tasks.one_time[1] via Int (file main.dta, line 3)\n"
        "DTA_TRACE: garbled-and-unparseable\n")
    marker_lines = sum(1 for ln in log.read_text().splitlines()
                       if "DTA_TRACE:" in ln)
    r = run([TRACE, str(log), "--stats",
             "--main-configs", str(corpus["main"]),
             "--dta-dir", str(corpus["dta"])])
    assert r.returncode == 0, r.stdout + r.stderr
    m = re.search(r"Lines with DTA_TRACE:\s+(\d+)", r.stdout)
    assert m and int(m.group(1)) == marker_lines
    assert re.search(r"MALFORMED \(unparsed\):\s+1", r.stdout)
    # A clean verdict here must still carry the malformed count.
    if "No validation issues found" in r.stdout:
        assert "malformed" in r.stdout


def test_empty_trace_file_is_inconclusive_not_clean(corpus, tmp_path):
    log = tmp_path / "empty.log"
    log.write_text("engine started\nengine stopped\n")
    r = run([TRACE, str(log),
             "--main-configs", str(corpus["main"]),
             "--dta-dir", str(corpus["dta"])])
    assert "INCONCLUSIVE" in r.stdout, r.stdout
    assert r.returncode != 0


# --------------------------------------------------------------------------- #
# CONTROL 6 — determinism. Two runs of the same input, different hash seeds.
# Raw `set` interpolation into report text rendered in hash order.
# --------------------------------------------------------------------------- #

@pytest.mark.parametrize("script,flag", [
    (HIERARCHY, "--dta-dir"),
    (ACCESS, "--dta-root"),
])
def test_output_is_byte_identical_across_hash_seeds(script, flag, corpus):
    argv = [script, "--src-dir", str(corpus["src"]),
            "--main-configs", str(corpus["main"]), flag, str(corpus["dta"])]
    a = run(argv, seed=1)
    b = run(argv, seed=987654321)
    assert a.stdout == b.stdout
    assert a.stderr == b.stderr
    assert a.returncode == b.returncode


# --------------------------------------------------------------------------- #
# CONTROL 7 — audit_normalized_masking's own fail-open, in its headline category.
#
# `norm_sym` stripped from the FIRST '@', which in MSVC mangling precedes the
# CLASS name. Two calls to DIFFERENT classes' same-named method collapsed to the
# same token, cancelled out of the leftover multiset, and the verdict came back
# BENIGN — for the scanner whose entire job is finding wrong-callee bugs.
# --------------------------------------------------------------------------- #

RNDFLARE_LOAD = "?Load@RndFlare@@UAEXAAVBinStream@@@Z"
RNDTEXT_LOAD = "?Load@RndText@@UAEXAAVBinStream@@@Z"
BITCRUSH_SAVE = "?Save@FxSendBitCrush@@UBEXAAVBinStream@@@Z"
DISTORTION_SAVE = "?Save@FxSendDistortion@@UBEXAAVBinStream@@@Z"


@pytest.mark.parametrize("a,b", [(RNDFLARE_LOAD, RNDTEXT_LOAD),
                                 (BITCRUSH_SAVE, DISTORTION_SAVE)])
def test_different_classes_same_method_do_not_collapse(a, b):
    assert ANM.norm_sym(a) != ANM.norm_sym(b), (
        "calls to two DIFFERENT classes' same-named method must not normalise "
        "to one token — that is fail-open in the wrong-callee category")
    # The class name has to survive; the signature is the part that may not.
    assert "RndFlare" in ANM.norm_sym(RNDFLARE_LOAD)
    assert "BinStream" not in ANM.norm_sym(RNDFLARE_LOAD)


def test_same_symbol_still_collapses_to_itself():
    """Control for the control: the normaliser must still cancel real matches."""
    assert ANM.norm_sym(RNDFLARE_LOAD) == ANM.norm_sym(RNDFLARE_LOAD)
    # Pool constants must keep collapsing, or every run turns into noise.
    assert ANM.norm_sym("__real@40400000") == "POOL"
    assert ANM.norm_sym("@F_1234") == "POOL"
    assert ANM.norm_sym("??_C@_05ABCD@hi@") == "POOL"


def _leftover(target_sym, base_sym):
    """Reproduce diff_one's multiset arithmetic for a single `bl` pair."""
    from collections import Counter
    mk = lambda s: [{"type": "Symbol", "value": s}]          # noqa: E731
    t = Counter({ANM.value_sig("bl", mk(target_sym)): 1})
    b = Counter({ANM.value_sig("bl", mk(base_sym)): 1})
    return (t - b) + (b - t)


def test_wrong_callee_survives_the_multiset_instead_of_cancelling():
    """The false negative at the level where it mattered: the leftover multiset."""
    leftover = _leftover(RNDFLARE_LOAD, RNDTEXT_LOAD)
    assert leftover, (
        "a call to RndText::Load where the target calls RndFlare::Load must "
        "leave a residue; it used to cancel and the verdict was BENIGN")
    cats = {ANM.categorize_sig(sig) for sig in leftover}
    assert cats == {"reloc_target"}, cats

    # Control: an ACTUALLY identical call must still cancel, or everything
    # becomes a REVIEW and the tool is useless in the other direction.
    assert not _leftover(RNDFLARE_LOAD, RNDFLARE_LOAD)
    # ...and so must two different pool constants (benign placement noise).
    assert not _leftover("__real@40400000", "__real@40800000")


# --------------------------------------------------------------------------- #
# CONTROL 8 — the fake_impl_scan defect, verbatim, on live report.json.
#
# `if n is None or raw is None: continue` on `fuzzy_match_percent`, a key
# objdiff only emits for functions WE define a body for. Replay the old loop
# against the real report and assert the drop is (a) real and (b) invisible
# without counting. No hardcoded totals: both numbers come from the artifact.
# --------------------------------------------------------------------------- #

REPORT = os.path.join(REPO, "build", "373307D9", "report.json")


@pytest.mark.skipif(not os.path.exists(REPORT), reason="report.json not built")
def test_missing_fuzzy_percent_tier_is_large_and_was_invisible():
    rep = json.load(open(REPORT))
    rows = [f for u in rep["units"] for f in (u.get("functions") or [])]
    assert rows, "report.json has no function rows at all"

    # The historical loop, reproduced exactly.
    kept = [f for f in rows
            if f.get("match_percent_normalized") is not None
            and f.get("fuzzy_match_percent") is not None]
    dropped = len(rows) - len(kept)

    assert dropped > 0, (
        "if this ever reaches 0 the control is vacuous — check the ruler")
    # The drop is the 'we defined no body' tier, and it is not a rounding error:
    # it must be a substantial fraction, or the historical incident is misfiled.
    assert dropped / len(rows) > 0.05
    # And it is entirely the fuzzy field, not the normalized one — which is why
    # `n is None or raw is None` read as harmless.
    assert all(f.get("match_percent_normalized") is not None for f in rows)
    fuzzy_none = sum(1 for f in rows if f.get("fuzzy_match_percent") is None)
    assert fuzzy_none == dropped


@pytest.mark.skipif(not os.path.exists(REPORT), reason="report.json not built")
def test_exact_100_selection_is_a_float_test_against_a_rounding_surface():
    """`n == 100.0` on a ruler that ROUNDS: 99.97 renders as '100.0'."""
    rep = json.load(open(REPORT))
    band = [f for u in rep["units"] for f in (u.get("functions") or [])
            if (f.get("match_percent_normalized") or 0) >= 99.9
            and (f.get("match_percent_normalized") or 0) < 100.0]
    assert band, "no near-100 rows — the caveat would be vacuous, re-check"
    # The dangerous subset: rows that RENDER as '100.0' at this project's usual
    # one-decimal precision yet fail the exact `== 100.0` selection. Two real
    # bugs have already hidden under exactly that rounding.
    renders_as_100 = [f for f in band
                      if f"{f['match_percent_normalized']:.1f}" == "100.0"]
    assert renders_as_100, (
        "no row both renders as 100.0 and fails `== 100.0` — if this ever "
        "becomes true the caveat is obsolete, not merely unproven")
    assert all(f["match_percent_normalized"] != 100.0 for f in renders_as_100)


@pytest.mark.skipif(not os.path.exists(REPORT), reason="report.json not built")
def test_audit_reports_its_denominator_and_flags_the_cap(tmp_path, monkeypatch):
    """Run the selection half of main() with a cap and assert it confesses.

    A synthetic report whose rows are all unselectable means zero objdiff
    subprocesses, so this stays a unit test.
    """
    rows_in = json.load(open(REPORT))
    # Keep it small AND make every row unselectable, so no worker ever runs.
    units = []
    for u in rows_in["units"][:40]:
        fns = []
        for f in (u.get("functions") or [])[:20]:
            g = dict(f)
            g["fuzzy_match_percent"] = None      # the invisible tier
            fns.append(g)
        units.append({"name": u.get("name"), "functions": fns})
    fake = tmp_path / "report.json"
    fake.write_text(json.dumps({"units": units}))

    n_rows = sum(len(u["functions"]) for u in units)
    assert n_rows > 0

    covj = tmp_path / "cov.json"
    monkeypatch.setattr(ANM, "REPORT", str(fake))
    monkeypatch.setattr(sys, "argv", [
        "audit_normalized_masking", "--out", str(tmp_path / "res.json"),
        "--coverage-json", str(covj)])
    with pytest.raises(SystemExit):
        ANM.main()

    d = json.loads(covj.read_text())
    assert d["universe"] == n_rows, "the denominator is every row in the report"
    assert d["examined"] == 0
    assert d["dropped"]["missing-fuzzy-percent"] == n_rows
    assert d["unaccounted"] == 0
    assert d["complete"] is True   # a complete census that examined nothing


def test_a_bare_continue_in_the_audit_selection_would_be_caught():
    """Proof the arithmetic check is load-bearing, not decorative.

    Build the historical scanner shape — drop the no-body tier without counting
    it — and assert the contract refuses to call it a census.
    """
    from scripts.analysis.coverage import CoverageReport
    rows = ([{"match_percent_normalized": 100.0, "fuzzy_match_percent": 90.0}] * 7
            + [{"match_percent_normalized": 100.0}] * 11)
    cov = CoverageReport("replay")
    cov.universe(len(rows), "rows")
    for r in rows:
        if r.get("fuzzy_match_percent") is None:
            continue                       # <-- the historical bare continue
        cov.examine()
    assert cov.emit(open(os.devnull, "w")) == EXIT_UNACCOUNTED
    assert cov.unaccounted == sum(1 for r in rows
                                  if r.get("fuzzy_match_percent") is None)


# --------------------------------------------------------------------------- #
# CONTROL — dta_dataflow, the fourth DTA scanner.
#
# It was listed as fixed alongside its three siblings and was never touched:
# byte-identical blob at the merge base, at the lane tip and on main. It kept
# `if p.exists():` with no `else` and a bare `print("No DTA access issues
# found.")`. Measured on this tree, same script, corpus the only variable:
#
#     corpus absent      28 B  "No DTA access issues found."   exit 0
#     corpus present  50,302 B  "Total: 30 findings"           exit 0
#
# 30 real findings and a 28-byte all-clear, told apart by nothing a caller can
# branch on. Its own paths are relative to CWD (no --dta-dir flag), so the
# control runs it from a directory that has a src tree and no orig-assets --
# which is precisely what a git worktree is.
# --------------------------------------------------------------------------- #

def test_dataflow_with_no_corpus_does_not_print_a_clean_verdict(tmp_path, corpus):
    (tmp_path / "src").mkdir(exist_ok=True)
    for f in corpus["src"].iterdir():
        (tmp_path / "src" / f.name).write_text(f.read_text())
    assert not (tmp_path / "orig-assets").exists(), (
        "the fixture must really lack the corpus, or this is not a control")

    r = run([DATAFLOW, "--src-dir", str(tmp_path / "src")], cwd=str(tmp_path))

    assert "No DTA access issues found." not in r.stdout, (
        f"the verbatim historical sentence, from a run that checked nothing:\n"
        f"{r.stdout[:400]}")
    assert "INCONCLUSIVE" in r.stdout, r.stdout
    assert "CHECKED NOTHING" in r.stdout
    assert "0 DTA files" in r.stdout
    assert r.returncode != 0, "an inconclusive run must not exit 0"


def test_dataflow_states_its_denominator_when_it_does_have_a_corpus(tmp_path, corpus):
    """Control for the control: with a corpus it must run, and its verdict must
    carry the exculpatory count on the SAME stream as the verdict."""
    (tmp_path / "src").mkdir(exist_ok=True)
    for f in corpus["src"].iterdir():
        (tmp_path / "src" / f.name).write_text(f.read_text())
    extracted = tmp_path / "orig-assets" / "extracted"
    (extracted / "config").mkdir(parents=True)
    (extracted / "config" / "ham_keep.dta").write_text(GOOD_DTA)

    r = run([DATAFLOW, "--src-dir", str(tmp_path / "src"),
             "--extra-root", str(extracted)], cwd=str(tmp_path))

    assert "INCONCLUSIVE" not in r.stdout, r.stdout
    # The count must not be able to appear without its denominator.
    assert "checkable DTA access sites of" in r.stdout, r.stdout
    assert "Total:" in r.stdout


def test_dataflow_bare_clean_sentence_is_gone_from_the_source():
    """The sentence itself, not just its current reachability.

    A future edit that restores an unconditional `print("No DTA access issues
    found.")` would pass every behavioural test above only until the corpus gate
    moved. Pin the string.
    """
    src = open(DATAFLOW, errors="replace").read()
    # It survives inside a comment documenting the defect; it must not survive
    # as something the program can print on its own.
    assert 'print("No DTA access issues found.")' not in src
    assert "print('No DTA access issues found.')" not in src


# --------------------------------------------------------------------------- #
# A drop with a FALSE REASON is worse than an uncounted drop.
#
# dta_hierarchy_scan dropped 106 Find* sites saying "re-examined via
# assign_checks". The skip predicate matched the bare prefix
# `\w+ = receiver->FindArray`; ASSIGN_FINDARRAY_RE -- the regex that actually
# feeds assign_checks -- requires the closing paren immediately after the key,
# so it matches ONE-ARGUMENT FindArray only. Every two-argument
# `var = recv->FindArray("k", true)` was therefore labelled "covered elsewhere"
# and covered nowhere.
#
# An uncounted drop is invisible; this one was worse, because it actively told
# the reader the population had been handled.
# --------------------------------------------------------------------------- #

def test_the_assign_recheck_claim_is_true_of_every_site_that_carries_it():
    """Re-derive both populations from the SOURCE regexes, not from the tool's
    own printed counts, so this cannot pass by agreeing with the thing it
    checks."""
    labelled, captured = set(), set()
    for path in sorted(pathlib.Path(os.path.join(REPO, "src")).rglob("*")):
        if path.suffix not in (".cpp", ".h") or not path.is_file():
            continue
        try:
            lines = path.read_text(errors="replace").splitlines()
        except OSError:
            continue
        for ln, line in enumerate(lines, 1):
            for m in HS.ASSIGN_FINDARRAY_RE.finditer(line):
                captured.add((str(path), ln, m.group(2), m.group(3)))
            for m in HS.FINDARRAY_RE.finditer(line):
                recv, key = m.groups()
                if re.search(rf'\w+\s*=\s*{re.escape(recv)}->FindArray', line):
                    labelled.add((str(path), ln, recv, key))

    orphans = labelled - captured
    # (b) The defect must be REAL and non-trivial, or this is not a control.
    assert orphans, ("no orphaned sites — the tree changed shape; re-measure "
                     "rather than deleting this test")
    assert len(labelled) > len(captured & labelled)

    # ...and it must be a two-argument FindArray in every case, which is the
    # mechanism, derived rather than asserted.
    for path, ln, recv, key in sorted(orphans)[:20]:
        src = open(path, errors="replace").read().splitlines()[ln - 1]
        assert re.search(rf'{re.escape(recv)}->FindArray\(\s*"{re.escape(key)}"\s*,',
                         src), (f"{path}:{ln} is orphaned for some OTHER reason "
                                f"than the trailing-argument one: {src.strip()}")


@pytest.mark.skipif(not os.path.isdir(os.path.join(REPO, "orig-assets")),
                    reason="needs the DTA corpus (absent from worktrees)")
def test_the_never_rechecked_population_has_its_own_slug():
    """The two populations must be told apart in the coverage block, with the
    honest one saying plainly that nothing checks these."""
    r = run([HIERARCHY, "--extra-root",
             os.path.join(REPO, "orig-assets", "extracted")])
    blob = r.stderr + r.stdout
    assert "assignment-site-NOT-rechecked-anywhere" in blob, (
        "the sites nothing re-checks must not share a slug with the sites "
        "something does")
    assert "Nothing checks these" in blob
    # The sites that ARE re-checked no longer carry a drop slug at all: the
    # deferred assign_checks loop disposes of them under its own real reason
    # (examined / key-absent / receiver-unresolvable). A slug that merely
    # promises a later check is exactly the claim that turned out to be false,
    # so it must not survive as a category.
    assert "assignment-site-checked-separately" not in blob

    # ...and the whole thing must still BALANCE, which is what caught the two
    # leftovers while this was being written: the self-reassignment site that
    # nothing disposed, and the positional-access bump that skipped the ledger.
    assert "UNACCOUNTED" not in blob, blob[-1500:]
    # NOT a literal count.  The site population is a property of how many
    # source files the tree happens to contain -- 274 in a bare worktree
    # (2,288 files), 2,133 in the main checkout (5,140 files, including
    # untracked/generated ones).  Pinning "274" here would have been the very
    # mistake this suite is about: a number that is a property of the
    # ENVIRONMENT asserted as a property of the code.  The distinct-vs-events
    # claim is checked relationally in
    # test_the_published_rates_are_distinct_sites_not_site_events.
    m = re.search(r"among (\d+) verifiable call sites \(of (\d+) total\)", blob)
    assert m, blob[-800:]
    verifiable, total = int(m.group(1)), int(m.group(2))
    assert 0 < verifiable <= total, (verifiable, total)


# --------------------------------------------------------------------------- #
# DOUBLE-COUNTING IS INVISIBLE TO THE EXIT-4 TRIPWIRE.
#
# Both DTA site scanners walked the same lines twice with the same regex and
# both bumped the universe AND disposed, so one site entered the books twice
# and left twice. `universe == examined + dropped` held perfectly. The coverage
# contract catches an UNCOUNTED row; it cannot catch a TWICE-COUNTED one, so
# this needs its own control.
#
#   access_audit    1,679 events / 1,658 distinct — 21 doubled, 8 of them
#                   examined twice -> 37/1,679 = 2.2%  becomes 29/1,658 = 1.75%
#   hierarchy_scan    331 events /   274 distinct — 57 doubled, none examined
#                   twice           -> 70/331 = 21%    becomes 70/274 = 25.5%
#
# The two rates move in OPPOSITE directions, which is why quoting them as a
# comparable pair was misleading on top of being wrong.
# --------------------------------------------------------------------------- #

def test_site_ledger_counts_a_site_once_and_lets_a_check_upgrade_a_drop():
    led = AA.SiteLedger()
    k = ("f.cpp", 10, (0, 5))

    # (b) The historical shape: two passes, one site. The first must register
    # it and the second must NOT.
    assert led.bump(k) is True
    assert led.bump(k) is False, "a second pass must not re-enter the universe"
    assert led.distinct == 1

    # Pass 1 could not resolve the receiver; pass 2 checked bounds fine. Two
    # passes checking different things are two chances to examine ONE site.
    led.drop(k, "receiver-unresolvable")
    led.examine(k)
    assert led.distinct == 1

    class _Sink:
        def __init__(self):
            self.examined = 0
            self.drops = []

        def examine(self, n=1):
            self.examined += n

        def drop(self, reason, n=1, note=""):
            self.drops.append(reason)

    sink = _Sink()
    led.flush(sink)
    assert sink.examined == 1 and sink.drops == [], (
        "a site that ANY pass managed to check is examined, once")


def test_site_ledger_does_not_downgrade_an_examine():
    led = AA.SiteLedger()
    k = ("f.cpp", 1, (0, 1))
    led.bump(k)
    led.examine(k)
    led.drop(k, "receiver-unresolvable")   # a later pass fails; irrelevant
    assert led.distinct == 1
    class _Sink:
        def __init__(self): self.examined = 0; self.drops = []
        def examine(self, n=1): self.examined += n
        def drop(self, reason, n=1, note=""): self.drops.append(reason)
    sink = _Sink()
    led.flush(sink)
    assert sink.examined == 1 and not sink.drops


@pytest.mark.skipif(not os.path.isdir(os.path.join(REPO, "orig-assets")),
                    reason="needs the DTA corpus (absent from worktrees)")
def test_the_published_rates_are_distinct_sites_not_site_events():
    """Recount the distinct sites here, by a different expression than the tool,
    and require the tool's denominator to equal it."""
    import collections
    events, distinct = 0, set()
    for path in sorted(pathlib.Path(os.path.join(REPO, "src")).rglob("*")):
        if path.suffix not in (".cpp", ".h") or not path.is_file():
            continue
        try:
            lines = path.read_text(errors="replace").splitlines()
        except OSError:
            continue
        for ln, line in enumerate(lines, 1):
            if line.strip().startswith("//"):
                continue
            for m in HS.FINDARRAY_RE.finditer(line):
                events += 1
                distinct.add((str(path), ln, m.span()))
            for m in HS.ASSIGN_FINDARRAY_RE.finditer(line):
                events += 1     # the historical second count of the same text

    # (b) The over-count must be REAL, or this is not a control.
    assert events > len(distinct), (
        "the two passes must really see overlapping text, or nothing was "
        "ever double-counted")

    r = run([HIERARCHY, "--extra-root",
             os.path.join(REPO, "orig-assets", "extracted")])
    blob = r.stderr + r.stdout
    m = re.search(r"universe\s+:\s+(\d+)\s+\(Find\*\(\) call sites", blob)
    assert m, blob[-1200:]
    assert int(m.group(1)) == len(distinct), (
        f"denominator {m.group(1)} is not the distinct-site count "
        f"{len(distinct)} (site EVENTS would be {events})")
