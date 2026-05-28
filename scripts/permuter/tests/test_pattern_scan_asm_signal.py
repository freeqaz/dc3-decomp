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

import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter import pattern_scan
from scripts.permuter.pattern_scan import (
    _build_diff_index,
    _load_diagnosis_for_symbol,
    _load_match_info_multi,
    _resolve_hit_candidate,
    _scan_file,
    _unit_matches_source,
)
from scripts.permuter.patterns import get_pattern


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


class TestLoadMatchInfoMultiNullDemangled(unittest.TestCase):
    """Regression: a NULL ``demangled`` row must not nuke the whole dict.

    decomp.db legitimately contains rows whose ``demangled`` column is NULL
    (static-init ``__sinit_*`` thunks, bare ``main``, asm-only symbols). The
    original loader called ``extract_qualified_name(row['demangled'])`` with
    no None guard; the resulting TypeError was swallowed by a broad
    ``except Exception: return {}`` so EVERY entry was discarded. That left
    ``_scan_file`` with no candidates → every hit serialized with empty
    ``symbol`` and ``match_percent=None`` → the asm-signal gate had nothing
    to look up → all hits fell through to ``confidence=unknown``.
    """

    def _make_db(self, path: Path, rows):
        conn = sqlite3.connect(str(path))
        conn.execute(
            "CREATE TABLE functions ("
            "symbol TEXT, demangled TEXT, unit TEXT, current_percent REAL)"
        )
        conn.executemany(
            "INSERT INTO functions (symbol, demangled, unit, current_percent) "
            "VALUES (?, ?, ?, ?)",
            rows,
        )
        conn.commit()
        conn.close()

    def test_null_demangled_row_does_not_empty_dict(self):
        with tempfile.TemporaryDirectory() as td:
            db_path = Path(td) / "decomp.db"
            self._make_db(
                db_path,
                [
                    # A NULL-demangled row interleaved with good rows — this
                    # is the row that used to crash extract_qualified_name.
                    ("__sinit_\\App_cpp", None, "main/App", 0.0),
                    ("main", None, "main/Main", 100.0),
                    ("__ct__3AppFiPPc", "App::App(int, char**)",
                     "main/App", 98.0),
                    ("Bar__3FooFv", "Foo::Bar()",
                     "main/system/obj/Foo", 90.5),
                ],
            )
            orig = pattern_scan.DECOMP_DB
            try:
                pattern_scan.DECOMP_DB = db_path
                multi = _load_match_info_multi()
            finally:
                pattern_scan.DECOMP_DB = orig

        # The two good rows must survive despite the two NULL-demangled rows.
        self.assertIn("App::App", multi)
        self.assertIn("Foo::Bar", multi)
        self.assertAlmostEqual(multi["App::App"][0][0], 98.0)
        self.assertEqual(multi["App::App"][0][1], "__ct__3AppFiPPc")


# ── Minimal C++ TU that triggers the return_this_op_assign pattern. ──
# A ref-returning operator= whose body lacks a trailing `return *this;` is
# exactly what return_this_op_assign.generate() emits a variant for.
_OP_ASSIGN_SRC = b"""\
struct Foo {
    int x;
    Foo& operator=(const Foo& o) {
        x = o.x;
    }
};
"""


class TestScanFilePopulatesHitFields(unittest.TestCase):
    """A produced hit must carry real ``source_path`` and ``symbol``.

    This is the user-visible symptom of the regression: hits serialized
    with ``symbol=''`` and ``match_percent=None``.
    """

    def test_hit_carries_source_and_symbol(self):
        with tempfile.TemporaryDirectory() as td:
            src_path = Path(td) / "Foo.cpp"
            src_path.write_bytes(_OP_ASSIGN_SRC)

            # match_info_multi keyed by the name the extractor produces for
            # the function (a bare ``operator=`` here — the AST-level name,
            # which is what _scan_file looks up against the candidate dict).
            qname = "operator="
            unit = "main/system/obj/Foo"
            match_info_multi = {
                qname: [(90.5, "__as__3FooFRC3Foo", unit)],
            }

            hits = _scan_file(
                src_path,
                [get_pattern("return_this_op_assign")],
                unit_name=unit,
                match_info={},
                show_variants=False,
                match_info_multi=match_info_multi,
            )

        self.assertTrue(hits, "expected at least one pattern hit")
        hit = hits[0]
        self.assertEqual(hit.source_path, str(src_path))
        self.assertEqual(hit.symbol, "__as__3FooFRC3Foo",
                         "hit must carry the resolved symbol, not ''")
        self.assertIsNotNone(hit.match_percent,
                             "hit must carry a match_percent, not None")
        self.assertAlmostEqual(hit.match_percent, 90.5)


class TestAsmSignalGatingProducesNonUnknown(unittest.TestCase):
    """End-to-end gating: a matching cached diff yields a non-unknown verdict.

    Builds a synthetic ``diff_*.json`` whose instructions diagnose to a
    diff_op the ``return_this_op_assign`` pattern's ``relevant()`` accepts,
    indexes it, then runs the same lookup + gate ``pattern_scan.main`` uses
    and asserts the confidence resolves to ``asm_signal_match`` (NOT unknown).
    """

    SYMBOL = "__as__3FooFRC3Foo"

    def _write_diff(self, cache_dir: Path):
        # match_type "replace" with target/base opcodes -> a diff_op; an `mr`
        # target opcode satisfies return_this_op_assign.relevant().
        diff = {
            "symbol": self.SYMBOL,
            "unit": "main/system/obj/Foo",
            "fuzzy_match_percent": 90.5,
            "instructions": [
                {
                    "index": 0,
                    "match_type": "replace",
                    "target": {"opcode": "mr", "args": ["r3", "r4"]},
                    "base": {"opcode": "addi", "args": ["r3", "r4", "0"]},
                },
            ],
        }
        # Filename slug is irrelevant — _build_diff_index keys off the
        # in-file "symbol" field, not the name.
        path = cache_dir / "diff_foo_deadbeef0000.json"
        path.write_text(json.dumps(diff))
        return path

    def test_cached_diff_yields_asm_signal_match(self):
        with tempfile.TemporaryDirectory() as td:
            cache_dir = Path(td)
            self._write_diff(cache_dir)

            diff_index = _build_diff_index(cache_dir)
            self.assertIn(self.SYMBOL, diff_index,
                          "diff index must key off the in-file symbol field")

            diag, _ = _load_diagnosis_for_symbol(
                self.SYMBOL, diff_index, cache_dir,
                fresh_objdiff=False, fresh_attempted=set(),
            )
            self.assertIsNotNone(diag,
                                 "a cached diff with instructions must "
                                 "produce a diagnosis (not None)")

            pattern = get_pattern("return_this_op_assign")
            is_rel = pattern.relevant(diag)
            confidence = "asm_signal_match" if is_rel else "excluded"
            self.assertEqual(
                confidence, "asm_signal_match",
                "an mr-opcode diff_op must gate to asm_signal_match, "
                "not unknown/excluded",
            )

    def test_missing_symbol_is_unknown(self):
        # Sanity check the other side of the gate: a symbol absent from the
        # cache yields no diagnosis (-> the caller marks it unknown).
        with tempfile.TemporaryDirectory() as td:
            cache_dir = Path(td)
            diff_index = _build_diff_index(cache_dir)  # empty cache
            diag, _ = _load_diagnosis_for_symbol(
                "Nonexistent__3FooFv", diff_index, cache_dir,
                fresh_objdiff=False, fresh_attempted=set(),
            )
            self.assertIsNone(diag)


if __name__ == "__main__":
    unittest.main()
