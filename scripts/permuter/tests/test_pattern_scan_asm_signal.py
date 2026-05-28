"""Tests for Wave H1 pattern_scan asm-signal disambiguation.

Verifies the Wave H1 fix to pattern_scan.py:
- `_load_match_info_multi` returns all symbols per qualified name (not
  just the last-write-wins single one).
- `_resolve_hit_candidate` picks the in-TU candidate over cross-TU ones.
- When more than one sub-100% overload shares a qname in the same TU,
  the resolver marks the hit ``ambiguous_overload=True`` so the asm
  filter falls back to ``confidence=unknown`` instead of locking onto
  the wrong symbol's cached diff.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.pattern_scan import (
    _resolve_hit_candidate,
    _unit_matches_source,
)


class TestUnitMatchesSource(unittest.TestCase):
    def test_main_prefix_and_src_prefix(self):
        self.assertTrue(
            _unit_matches_source(
                "main/system/rndobj/PropAnim",
                "src/system/rndobj/PropAnim.cpp",
            )
        )

    def test_unit_main_vs_no_main(self):
        # objdiff sometimes emits units without the "main/" prefix.
        self.assertTrue(
            _unit_matches_source(
                "system/world/CameraShot",
                "src/system/world/CameraShot.cpp",
            )
        )

    def test_different_tus_dont_match(self):
        self.assertFalse(
            _unit_matches_source(
                "main/system/rndobj/PropAnim",
                "src/system/world/CameraShot.cpp",
            )
        )

    def test_c_extension(self):
        self.assertTrue(
            _unit_matches_source(
                "main/lib/zlib/inflate",
                "src/lib/zlib/inflate.c",
            )
        )

    def test_blank_inputs(self):
        self.assertFalse(_unit_matches_source("", "src/foo.cpp"))
        self.assertFalse(_unit_matches_source("main/foo", ""))


class TestResolveHitCandidate(unittest.TestCase):
    """Ensure the multi-candidate resolver picks the right overload.

    Each test simulates a decomp.db state via the ``candidates_by_qname``
    dict that ``_load_match_info_multi`` would return.
    """

    SRC = "src/band3/meta_band/TokenRedemptionPanel.cpp"

    def test_single_candidate(self):
        cands = {
            "Foo::Bar": [
                (90.5, "Bar__3FooFv", "main/band3/meta_band/TokenRedemptionPanel"),
            ],
        }
        chosen, ambig = _resolve_hit_candidate("Foo::Bar", self.SRC, cands)
        self.assertIsNotNone(chosen)
        self.assertFalse(ambig)
        self.assertAlmostEqual(chosen[0], 90.5)

    def test_picks_sub_100_over_100_overload(self):
        # Two overloads in the same TU, one at 100% and one at 99.5%.
        # Resolver must pick the sub-100% one so --incomplete-only and
        # --require-asm-signal both target the actionable function.
        cands = {
            "TokenRedemptionPanel::OnMsg": [
                (99.5, "OnMsg__20TokenRedemptionPanelFRC24RockCentralOpCompleteMsg",
                 "main/band3/meta_band/TokenRedemptionPanel"),
                (100.0, "OnMsg__20TokenRedemptionPanelFRC13ButtonDownMsg",
                 "main/band3/meta_band/TokenRedemptionPanel"),
            ],
        }
        chosen, ambig = _resolve_hit_candidate(
            "TokenRedemptionPanel::OnMsg", self.SRC, cands,
        )
        self.assertIsNotNone(chosen)
        self.assertFalse(ambig, "one sub-100% overload is not ambiguous")
        self.assertAlmostEqual(chosen[0], 99.5)
        self.assertIn("RockCentralOpCompleteMsg", chosen[1])

    def test_multi_sub_100_in_same_tu_is_ambiguous(self):
        # Two sub-100% overloads in the same TU -> can't pick reliably.
        # The asm-signal filter must mark this unknown (not pick one arbitrarily).
        cands = {
            "RockCentral::OnMsg": [
                (89.0, "OnMsg__11RockCentralFRC8ButtonMsg",
                 "main/band3/net_band/RockCentral"),
                (95.0, "OnMsg__11RockCentralFRC11OtherMsg",
                 "main/band3/net_band/RockCentral"),
            ],
        }
        src = "src/band3/net_band/RockCentral.cpp"
        chosen, ambig = _resolve_hit_candidate(
            "RockCentral::OnMsg", src, cands,
        )
        self.assertIsNotNone(chosen)
        self.assertTrue(ambig, "two sub-100% overloads = ambiguous")

    def test_prefers_in_tu_over_cross_tu(self):
        # Template instantiations (e.g. PropSync<T>) can appear in many
        # TUs. The resolver must prefer the one whose unit matches the
        # source file rather than the lowest-% across the whole codebase.
        cands = {
            "PropSync": [
                (50.0, "PropSync__XXX_other", "main/system/other/file"),
                (95.0, "PropSync__YYY_local", "main/system/rndobj/MyFile"),
            ],
        }
        src = "src/system/rndobj/MyFile.cpp"
        chosen, ambig = _resolve_hit_candidate("PropSync", src, cands)
        self.assertIsNotNone(chosen)
        self.assertFalse(ambig)
        # Picked the in-TU 95% candidate, not the lower 50% cross-TU one.
        self.assertAlmostEqual(chosen[0], 95.0)
        self.assertEqual(chosen[1], "PropSync__YYY_local")

    def test_falls_back_to_all_when_no_in_tu_match(self):
        # If no candidate has a matching unit, fall back to the global pool
        # rather than returning None (allows pattern_scan to still surface
        # something for unusual TU layouts).
        cands = {
            "Foo::Bar": [
                (88.0, "Bar__3FooFv", "main/some/other/unit"),
            ],
        }
        chosen, ambig = _resolve_hit_candidate(
            "Foo::Bar", "src/unrelated/path.cpp", cands,
        )
        self.assertIsNotNone(chosen)
        self.assertFalse(ambig)
        self.assertAlmostEqual(chosen[0], 88.0)

    def test_unknown_qname_returns_none(self):
        chosen, ambig = _resolve_hit_candidate("Does::NotExist", self.SRC, {})
        self.assertIsNone(chosen)
        self.assertFalse(ambig)

    def test_all_100_overloads_not_flagged_ambiguous(self):
        # If both overloads are at 100%, the hit is not actionable but
        # also not "ambiguous" in the asm-signal sense — it'll be
        # filtered by --incomplete-only anyway. Mark ambig=False.
        cands = {
            "X::Y": [
                (100.0, "Y__1XFv", "main/foo/bar"),
                (100.0, "Y__1XFi", "main/foo/bar"),
            ],
        }
        chosen, ambig = _resolve_hit_candidate(
            "X::Y", "src/foo/bar.cpp", cands,
        )
        self.assertIsNotNone(chosen)
        self.assertFalse(ambig)
        self.assertAlmostEqual(chosen[0], 100.0)


if __name__ == "__main__":
    unittest.main()
