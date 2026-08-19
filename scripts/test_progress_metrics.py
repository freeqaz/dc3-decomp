"""Tests for the link-glue shadow dedup in progress_metrics.py.

The bug: ``default/link_glue`` is scaffolding this repo invented so the link
resolves, and every row in it scores 0 %. dtk emits a glue entry for some
symbols that ALSO exist in a real unit at 100 %, so report.json contains the
same function twice and the canonical headline counts it once as matched and
once as unmatched.

These tests exist mainly to hold the *narrowness* of the fix. Dropping the
whole glue unit would be easy and wrong: ~36 of its rows have no real
counterpart and some name genuine unwritten work. The negative controls below
are the point of the file.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from progress_metrics import GLUE_UNIT, dedup_glue_shadows  # noqa: E402


def _fn(name: str, norm: float | None, size: int = 8) -> dict:
    return {"name": name, "match_percent_normalized": norm, "size": size}


def _report(*units: tuple[str, list[dict]]) -> dict:
    return {"units": [{"name": n, "functions": f, "measures": {}} for n, f in units]}


def test_shadow_of_a_matched_real_symbol_is_dropped() -> None:
    data = _report(
        ("default/system/oggvorbis/floor0", [_fn("floor0_unpack", 100.0)]),
        (GLUE_UNIT, [_fn("floor0_unpack", 0.0)]),
    )
    assert dedup_glue_shadows(data) == {"floor0_unpack"}


def test_glue_row_with_no_real_counterpart_is_kept() -> None:
    """NEGATIVE CONTROL. ``__link_glue_noop`` exists only in the glue unit.

    It must stay in the denominator — it is genuinely unmatched.
    """
    data = _report(
        ("default/system/utl/MemMgr", [_fn("?MemOrPoolFree@@YAXHPAXPBDH1@Z", 100.0)]),
        (GLUE_UNIT, [_fn("__link_glue_noop", 0.0), _fn("_strnicmp", 0.0)]),
    )
    assert dedup_glue_shadows(data) == set()


def test_shadow_of_an_UNMATCHED_real_symbol_is_kept() -> None:
    """NEGATIVE CONTROL, and the one that matters most.

    If the real copy is not yet at 100 %, the glue row is not a double-count of
    finished work — dropping it would delete real remaining work from the
    denominator and silently inflate the headline. This is precisely the
    failure mode this project keeps finding in its own tooling.
    """
    data = _report(
        ("default/system/utl/MakeString", [_fn("??6FormatString@@QAAAAV0@M@Z", 62.5)]),
        (GLUE_UNIT, [_fn("??6FormatString@@QAAAAV0@M@Z", 0.0)]),
    )
    assert dedup_glue_shadows(data) == set()


def test_a_matched_symbol_in_a_real_unit_is_never_dropped() -> None:
    """NEGATIVE CONTROL: the dedup must only ever remove rows from the glue unit."""
    data = _report(
        ("default/system/oggvorbis/floor0", [_fn("floor0_unpack", 100.0)]),
        ("default/system/oggvorbis/floor1", [_fn("floor0_unpack", 100.0)]),
    )
    assert dedup_glue_shadows(data) == set()


def test_null_normalized_does_not_count_as_matched() -> None:
    """A real-unit row with ``match_percent_normalized: null`` is not evidence
    of a match, so its glue twin must be kept."""
    data = _report(
        ("default/system/net/curl/lib/warnless", [_fn("curlx_ultous", None)]),
        (GLUE_UNIT, [_fn("curlx_ultous", 0.0)]),
    )
    assert dedup_glue_shadows(data) == set()


def test_no_glue_unit_at_all_is_a_no_op() -> None:
    data = _report(("default/system/utl/MemMgr", [_fn("x", 100.0)]))
    assert dedup_glue_shadows(data) == set()


if __name__ == "__main__":
    import traceback

    failures = 0
    for name, fn in sorted(globals().items()):
        if not name.startswith("test_") or not callable(fn):
            continue
        try:
            fn()
            print(f"PASS  {name}")
        except Exception:  # noqa: BLE001
            failures += 1
            print(f"FAIL  {name}")
            traceback.print_exc()
    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)
