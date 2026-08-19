"""Negative controls for scripts/analysis/honesty_lint.py.

A lint that never fires is indistinguishable from a codebase with no defects,
which is the same epistemic failure it exists to catch. So every rule below is
tested BOTH ways:

  * FIRES  — fed the historical source that actually shipped the bug.
  * QUIET  — fed the fixed form, and fed a superficially-similar construct that
             is NOT a bug (an over-firing control). A rule that flags
             `LIKE '%xdk%'` or `sig = sig[:MAX_SIGNATURE_CHARS]` gets muted by
             the next person who reads its output, and then it protects nothing.

The historical strings are quoted verbatim from the incident log:
  certify_floor.py     symbol NOT LIKE '??_%'          -> hid 6,835 functions
  data_symbol_scan.py  tasks = tasks[:args.max_symbols] -> hid 14,549 symbols
"""
from __future__ import annotations

import os
import textwrap

import pytest

from scripts.analysis import honesty_lint as HL

REPO = HL.REPO


def _rules(src: str, path: str = "scripts/fake.py"):
    """Run every rule over a source string and return {rule: [findings]}."""
    out: dict[str, list] = {}
    for f in (HL.check_unescaped_like(path, src)
              + HL.check_uncounted_cap(path, src)
              + HL.check_swallowed_empty(path, src)
              + HL.check_worker_mutated_global(path, src)):
        out.setdefault(f.rule, []).append(f)
    return out


# --------------------------------------------------------------------------- #
# E1 — the certify_floor.py '??_%' wildcard
# --------------------------------------------------------------------------- #

HISTORICAL_LIKE_BUG = textwrap.dedent("""
    def band(conn):
        return conn.execute(
            "SELECT symbol FROM functions WHERE symbol NOT LIKE '??_%'").fetchall()
""")

FIXED_LIKE = textwrap.dedent(r"""
    def band(conn):
        return conn.execute(
            "SELECT symbol FROM functions WHERE symbol NOT LIKE '??\_%' ESCAPE '\'"
        ).fetchall()
""")


def test_E1_fires_on_the_historical_certify_floor_line():
    got = _rules(HISTORICAL_LIKE_BUG)
    assert "E1" in got, "the line that hid 6,835 functions must not lint clean"
    assert "??_%" in got["E1"][0].text


def test_E1_quiet_on_the_fixed_form():
    assert "E1" not in _rules(FIXED_LIKE)


@pytest.mark.parametrize("sql", [
    "\"SELECT * FROM f WHERE unit LIKE '%xdk%'\"",          # intended substring
    "\"SELECT * FROM f WHERE unit LIKE 'default/xdk/%'\"",  # intended prefix
    "\"SELECT * FROM f WHERE symbol LIKE ?\"",              # bound parameter
])
def test_E1_does_not_over_fire(sql):
    """Over-firing controls. A noisy rule gets disabled, and then it guards nothing."""
    assert "E1" not in _rules(f"q = {sql}\n"), f"E1 must stay quiet on {sql}"


def test_E1_ignores_prose_that_merely_quotes_the_bug():
    """coverage.py and this file both DISCUSS `NOT LIKE '??_%'` in docstrings."""
    src = '"""We used to write symbol NOT LIKE \'??_%\' which was wrong."""\nx = 1\n'
    assert "E1" not in _rules(src)
    # ...and a comment, likewise.
    assert "E1" not in _rules("# symbol NOT LIKE '??_%' was the bug\nx = 1\n")


# --------------------------------------------------------------------------- #
# E2 — the data_symbol_scan.py silent cap
# --------------------------------------------------------------------------- #

HISTORICAL_CAP_BUG = textwrap.dedent("""
    def main():
        tasks = build_tasks()
        if len(tasks) > args.max_symbols:
            tasks = tasks[:args.max_symbols]
        run(tasks)
        print(f"scanned={len(tasks)}", file=sys.stderr)
""")

CAP_WITH_A_BANNER = textwrap.dedent("""
    def main():
        tasks = build_tasks()
        if len(tasks) > args.max_symbols:
            before = len(tasks)
            tasks = tasks[:args.max_symbols]
            print(f"TRUNCATED: {before - len(tasks)} of {before} never examined")
        run(tasks)
""")


def test_E2_fires_on_the_historical_data_symbol_scan_cap():
    got = _rules(HISTORICAL_CAP_BUG)
    assert "E2" in got, "a cap with a summary that prints only `scanned=` must not lint clean"
    assert "max_symbols" in got["E2"][0].text


def test_E2_quiet_once_the_truncation_is_announced():
    assert "E2" not in _rules(CAP_WITH_A_BANNER)


@pytest.mark.parametrize("line", [
    "symbol = symbol[:paren_idx]",              # parsing, not a population
    "commit_from = commit_from[:10]",           # a literal, not a cap flag
    "sig = sig[:MAX_SIGNATURE_CHARS]",          # a STRING, named for characters
    "name = name[:width]",                      # ditto
    "other = xs[:args.limit]",                  # not self-assigning; a view, not a cut
])
def test_E2_does_not_over_fire(line):
    assert "E2" not in _rules(line + "\n"), f"E2 must stay quiet on `{line}`"


# --------------------------------------------------------------------------- #
# W rules
# --------------------------------------------------------------------------- #

def test_W1_fires_on_error_to_empty_result():
    src = "def load(p):\n    try:\n        return read(p)\n    except OSError:\n        return []\n"
    assert "W1" in _rules(src)


def test_W2_fires_on_a_global_rebound_in_a_pool_using_module():
    """The data_symbol_scan race shape: a lazy global + a worker pool."""
    src = textwrap.dedent("""
        from concurrent.futures import ThreadPoolExecutor
        _IDX = None
        def load():
            global _IDX
            _IDX = {}
    """)
    assert "W2" in _rules(src)
    # No pool in the module => not the race shape => quiet.
    assert "W2" not in _rules("_IDX = None\ndef load():\n    global _IDX\n    _IDX = {}\n")


# --------------------------------------------------------------------------- #
# Repo-level regression gate.
#
# The count may only go DOWN. A new scanner that ships either defect fails here,
# which is the whole point: the next instance of this bug should be caught by CI
# and not by someone noticing a suspicious total eighteen months later.
# --------------------------------------------------------------------------- #

# Paths with a KNOWN-OPEN E-finding at the time this gate was written. Removing
# an entry (because it was fixed) is always fine; ADDING one requires a reason
# in review, because it means a new silent cap or wildcard shipped.
KNOWN_OPEN = {
    # (path, rule)
}


def test_repo_has_no_unexpected_honesty_errors():
    findings = HL.lint_repo(REPO)
    errors = [f for f in findings if f.severity == "ERROR"]
    unexpected = [f for f in errors if (f.path, f.rule) not in KNOWN_OPEN]
    assert not unexpected, (
        "new lying-by-omission finding(s):\n" +
        "\n".join(f"  {f.path}:{f.line} [{f.rule}] {f.text}\n      {f.detail}"
                  for f in unexpected))


def test_lint_is_deterministic():
    """Two runs of the linter must agree, or its own count is not a measurement."""
    a = [tuple(f) for f in HL.lint_repo(REPO)]
    b = [tuple(f) for f in HL.lint_repo(REPO)]
    assert a == b


def test_display_only_allowlist_entries_all_carry_a_reason():
    """An unexplained allowlist is how an allowlist becomes the bug it prevented."""
    for path, reason in HL.ALLOW_DISPLAY_ONLY.items():
        assert len(reason) > 20, f"{path}: allowlist entry needs a real justification"
        assert os.path.exists(os.path.join(REPO, path)), f"{path}: stale allowlist entry"


def test_allowlist_has_no_dead_entries():
    """An entry that excuses a finding the file no longer produces must be removed.

    A dead exemption is its own small lie: it asserts "we looked at this and it
    is fine" about a site the checker never examined. Four of the five original
    entries were dead — two because the file was fixed properly, two because the
    slice was written inline rather than self-assigning, so E2 could never have
    matched them in the first place. This test is what stops the list growing
    back.
    """
    dead = []
    for path in HL.ALLOW_DISPLAY_ONLY:
        full = os.path.join(REPO, path)
        if not os.path.exists(full):
            continue
        src = open(full, errors="replace").read()
        if not HL.check_uncounted_cap(path, src):
            dead.append(path)
    assert not dead, (
        "ALLOW_DISPLAY_ONLY entries that no longer excuse anything — delete them:\n  "
        + "\n  ".join(dead))
