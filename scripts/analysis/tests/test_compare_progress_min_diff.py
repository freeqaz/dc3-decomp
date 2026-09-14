"""A crossing to or from 100 must never be hidden by the min_diff threshold.

WHY THIS EXISTS (measured 2026-09-14, dc3 wave 6, lane w6-c)
------------------------------------------------------------
`compare_functions` took `min_diff: float = 0.5` and the only caller,
`main()`'s `--functions` branch, never passed it -- there was no CLI override at
all.  Crossing a function to 100% is the single event this project is scored on,
and in the 99.5-100 band *every* crossing moves the percent by less than 0.5 BY
CONSTRUCTION.  So `measure_progress.sh --functions` reported

    0 improvements, 48373 unchanged, +0.00%

over a tree that had just crossed `RndFont::Load` (99.99418 -> 100.0) and
`UILabel::PreLoad` (99.77305 -> 100.0) -- 5,568 bytes.  Both reports were
correct on disk; the comparison threw the result away.  The only trace was the
coverage block's `unchanged_within_min_diff` count, which does not say a
crossing happened, so a lane checking its own work with this tool would conclude
it had achieved nothing.

This is the project's recurring failure shape: a broken measurement and a clean
result look identical.  A threshold is a statement about MAGNITUDE; arriving at
100 is CATEGORICAL, so it is exempted rather than tuned.

Run: python3 -m pytest scripts/analysis/tests/test_compare_progress_min_diff.py
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from compare_progress import compare_functions  # noqa: E402


def _report(percents):
    """A minimal report.json shape carrying one unit and the given functions."""
    return {
        "units": [{
            "name": "u",
            "functions": [
                {
                    "name": name,
                    "size": "100",
                    "match_percent_normalized": pct,
                    "fuzzy_match_percent": pct,
                    "metadata": {"demangled_name": name},
                }
                for name, pct in percents.items()
            ],
        }]
    }


# The two real functions that exposed this, plus the controls that keep the
# test honest in the other direction.
BASE = _report({
    "RndFont__Load": 99.99418,     # real crossing, 0.006pp -- 80x under min_diff
    "UILabel__PreLoad": 99.77305,  # real crossing, 0.227pp -- under min_diff
    "left_100": 100.0,             # a REGRESSION off 100 must also survive
    "tiny_move": 97.10,            # sub-threshold, NOT a crossing -> filtered
    "big_move": 80.0,              # supra-threshold -> reported either way
})
CURRENT = _report({
    "RndFont__Load": 100.0,
    "UILabel__PreLoad": 100.0,
    "left_100": 99.6,
    "tiny_move": 97.31,
    "big_move": 90.0,
})


def _changed_names(populations):
    return {row["name"] for row in populations["changed"]}


def test_crossings_survive_the_default_threshold():
    """Both crossings move far less than the 0.5 default and must still appear."""
    changed = _changed_names(compare_functions(BASE, CURRENT))
    assert "RndFont__Load" in changed, "a 0.006pp crossing to 100 was filtered out"
    assert "UILabel__PreLoad" in changed, "a 0.227pp crossing to 100 was filtered out"


def test_regression_off_100_survives_the_default_threshold():
    """Leaving 100 is the same categorical event in the losing direction."""
    changed = _changed_names(compare_functions(BASE, CURRENT))
    assert "left_100" in changed, "a function that FELL off 100 was filtered out"


def test_threshold_still_filters_ordinary_small_moves():
    """The exemption must be for crossings only -- not a disabled threshold.

    Without this, 'report everything' would pass the tests above while making
    min_diff meaningless and burying real regressions in noise.
    """
    populations = compare_functions(BASE, CURRENT)
    assert "tiny_move" not in _changed_names(populations)
    assert populations["unchanged"] == 1, (
        f"expected exactly 1 filtered row, got {populations['unchanged']}"
    )


def test_big_moves_are_reported():
    """Degeneracy control: if nothing were ever reported, the tests above could
    only fail, and a tautologically-empty 'changed' list must not read as pass."""
    assert "big_move" in _changed_names(compare_functions(BASE, CURRENT))


def test_min_diff_is_honoured_when_passed():
    """The CLI flag has to actually reach the comparison."""
    fine = compare_functions(BASE, CURRENT, min_diff=0.001)
    assert "tiny_move" in _changed_names(fine), "min_diff=0.001 did not lower the bar"
    assert fine["unchanged"] == 0

    coarse = compare_functions(BASE, CURRENT, min_diff=50.0)
    coarse_names = _changed_names(coarse)
    assert "big_move" not in coarse_names, "min_diff=50 did not raise the bar"
    # ...and the crossings are STILL exempt at any threshold.
    assert "RndFont__Load" in coarse_names
    assert "left_100" in coarse_names
