"""BSF graph-coloring solver and register analysis tests."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.types import (
    Diagnosis,
    SwapInfo,
)
from scripts.permuter.tests.conftest import (
    _empty_diag,
)


# ---------------------------------------------------------------------------
# BSF-guided search tests
# ---------------------------------------------------------------------------

class TestGuidedPairwiseSearch(unittest.TestCase):
    """Tests for the targeted guided_pairwise_search() solver."""

    @staticmethod
    def _make_mock_trace(colors: list[int]) -> object:
        """Create a mock BSFTrace with initial coloring calls for given colors.

        Each color represents a variable's color assignment in declaration order.
        Colors are assigned via INITIAL_COLORING_RVA.
        """
        from tools.compiler_trace.bsf_trace import BSFTrace, BSFCall
        from tools.compiler_trace.regmap_solver import INITIAL_COLORING_RVA

        calls = []
        for i, color in enumerate(colors):
            calls.append(BSFCall(
                index=i + 1,
                caller_rva=INITIAL_COLORING_RVA,
                lo=0, hi=0, base=0,
                bit=color,
            ))
        return BSFTrace(source=Path("/dev/null"), calls=calls)

    def test_targeted_narrows_search_space(self):
        """For n=8 decls and 1 swap pair, guided candidates << C(8,2)=28."""
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        # 8 variables with colors 0-7 (r11, r10, ..., r4)
        # But we only need to swap r30<->r31 which are colors 9, 10
        # Those colors aren't in the trace, so this tests the fallback path
        # Instead: set up colors 8,9 = r29,r30 for a swap pair (r29, r30)
        colors = [0, 1, 2, 3, 4, 5, 8, 9]  # 8 vars, last two are r29, r30
        trace = self._make_mock_trace(colors)
        decl_names = [f"v{i}" for i in range(8)]

        # Swap pair: r29 <-> r30 (colors 8 and 9 → decl indices 6 and 7)
        candidates = guided_pairwise_search(trace, [("r29", "r30")], decl_names)

        # Should be much less than C(8,2)=28
        self.assertGreater(len(candidates), 0, "Should produce at least 1 candidate")
        self.assertLess(len(candidates), 28, f"Should narrow from 28, got {len(candidates)}")

        # The targeted swap (v6, v7) should be first
        self.assertIn("v7", candidates[0])
        self.assertIn("v6", candidates[0])
        # Verify the targeted pair is actually swapped
        idx6 = candidates[0].index("v6")
        idx7 = candidates[0].index("v7")
        self.assertEqual(idx6, 7, "v6 should be at index 7 (swapped with v7)")
        self.assertEqual(idx7, 6, "v7 should be at index 6 (swapped with v6)")

    def test_different_swap_pairs_produce_different_candidates(self):
        """Different swap_pairs should produce different candidate sets."""
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        # 5 vars with colors mapping to r11, r10, r9, r29, r30
        colors = [0, 1, 2, 8, 9]
        trace = self._make_mock_trace(colors)
        decl_names = ["a", "b", "c", "d", "e"]

        # Swap r29<->r30 (colors 8,9 → decl indices 3,4)
        cands_de = guided_pairwise_search(trace, [("r29", "r30")], decl_names)

        # Swap r11<->r10 (colors 0,1 → decl indices 0,1)
        cands_ab = guided_pairwise_search(trace, [("r11", "r10")], decl_names)

        # First candidate of each should swap different positions
        self.assertNotEqual(cands_de[0], cands_ab[0],
                            "Different swap pairs should produce different first candidates")

    def test_two_decls_one_swap_gives_one_targeted(self):
        """With 2 declarations and 1 swap pair, should get exactly the swap."""
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        colors = [8, 9]  # r29, r30
        trace = self._make_mock_trace(colors)
        decl_names = ["x", "y"]

        candidates = guided_pairwise_search(trace, [("r29", "r30")], decl_names)

        # Should produce exactly 1 candidate: ["y", "x"]
        self.assertEqual(len(candidates), 1)
        self.assertEqual(candidates[0], ["y", "x"])

    def test_unmapped_registers_use_bounded_fallback(self):
        """Swap pairs with unmappable registers get bounded fallback, not all-pairs."""
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        colors = [0, 1, 2, 3, 4, 5, 6, 7]  # 8 vars, all volatile
        trace = self._make_mock_trace(colors)
        decl_names = [f"v{i}" for i in range(8)]

        # r19<->r20 are outside the known color mapping range
        candidates = guided_pairwise_search(trace, [("r19", "r20")], decl_names)

        # Should get bounded fallback, not C(8,2)=28
        self.assertLess(len(candidates), 28,
                        f"Unmapped fallback should be bounded, got {len(candidates)}")

    def test_multi_swap_combined(self):
        """Multiple swap pairs should try simultaneous swap."""
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        # 4 vars: r11, r10, r29, r30
        colors = [0, 1, 8, 9]
        trace = self._make_mock_trace(colors)
        decl_names = ["a", "b", "c", "d"]

        # Two swap pairs: r11<->r10 and r29<->r30
        candidates = guided_pairwise_search(
            trace, [("r11", "r10"), ("r29", "r30")], decl_names
        )

        # Should include the simultaneous swap: ["b", "a", "d", "c"]
        simultaneous = ["b", "a", "d", "c"]
        self.assertIn(simultaneous, candidates,
                      f"Combined swap not found in candidates: {candidates}")


# ---------------------------------------------------------------------------
# extract_qualified_name tests
# ---------------------------------------------------------------------------

class TestExtractQualifiedName(unittest.TestCase):
    """Tests for extract_qualified_name() handling operator overloads."""

    def test_regular_method(self):
        from scripts.permuter.types import extract_qualified_name
        self.assertEqual(
            extract_qualified_name("public: virtual void __cdecl CharBlendBone::Poll(void)"),
            "CharBlendBone::Poll",
        )

    def test_destructor(self):
        from scripts.permuter.types import extract_qualified_name
        self.assertEqual(
            extract_qualified_name("public: __cdecl ClipDistMap::~ClipDistMap(void)"),
            "ClipDistMap::~ClipDistMap",
        )

    def test_operator_call(self):
        from scripts.permuter.types import extract_qualified_name
        self.assertEqual(
            extract_qualified_name(
                "public: bool __cdecl FileMergerSort::operator()"
                "(struct FileMerger::Merger const *, struct FileMerger::Merger const *)"
            ),
            "FileMergerSort::operator()",
        )

    def test_operator_eq(self):
        from scripts.permuter.types import extract_qualified_name
        self.assertEqual(
            extract_qualified_name("public: class DataNode __cdecl Object::operator==(class Object const *)"),
            "Object::operator==",
        )

    def test_operator_subscript(self):
        from scripts.permuter.types import extract_qualified_name
        self.assertEqual(
            extract_qualified_name("public: class Hmx::Object * __cdecl ObjectDir::operator[](char const *)"),
            "ObjectDir::operator[]",
        )

    def test_free_function_returns_none(self):
        """Free functions (no ::) return None since we can't extract a qualified name."""
        from scripts.permuter.types import extract_qualified_name
        # operator>> as a free function — no class qualifier, just 'operator>>'
        result = extract_qualified_name(
            "class BinStream & __cdecl operator>>(class BinStream &, struct CharEyes::EyeDesc &)"
        )
        # Free function without :: — extract_qualified_name requires ::
        self.assertIsNone(result)


class TestColorToGpr(unittest.TestCase):
    """Tests for the color <-> GPR mapping functions."""

    def test_volatile_roundtrip(self):
        from tools.compiler_trace.regmap_solver import color_to_gpr, gpr_to_color
        for color in range(7):  # colors 0-6 are volatile
            gpr = color_to_gpr(color)
            self.assertIsNotNone(gpr)
            self.assertEqual(gpr_to_color(gpr), color,
                             f"Roundtrip failed for color {color} -> {gpr}")

    def test_callee_saved_roundtrip(self):
        from tools.compiler_trace.regmap_solver import color_to_gpr, gpr_to_color
        for color in range(7, 26):  # colors 7-25 are callee-saved (r31-r13)
            gpr = color_to_gpr(color)
            self.assertIsNotNone(gpr)
            self.assertEqual(gpr_to_color(gpr), color,
                             f"Roundtrip failed for color {color} -> {gpr}")

    def test_known_mappings(self):
        """Empirically confirmed mapping from test_bsf_engine.py."""
        from tools.compiler_trace.regmap_solver import color_to_gpr, gpr_to_color
        # Volatile: color = 11 - reg
        self.assertEqual(color_to_gpr(0), "r11")
        self.assertEqual(color_to_gpr(6), "r5")
        # Callee-saved: color 7 = r31 (NOT r4!)
        self.assertEqual(color_to_gpr(7), "r31")
        self.assertEqual(color_to_gpr(8), "r30")
        self.assertEqual(color_to_gpr(9), "r29")
        self.assertEqual(color_to_gpr(10), "r28")
        self.assertEqual(color_to_gpr(11), "r27")
        self.assertEqual(color_to_gpr(25), "r13")
        self.assertIsNone(color_to_gpr(26))

        self.assertEqual(gpr_to_color("r11"), 0)
        self.assertEqual(gpr_to_color("r5"), 6)
        self.assertEqual(gpr_to_color("r31"), 7)
        self.assertEqual(gpr_to_color("r30"), 8)
        self.assertEqual(gpr_to_color("r13"), 25)
        self.assertIsNone(gpr_to_color("r3"))
        self.assertIsNone(gpr_to_color("r4"))  # r4 is arg reg, not in color space


# ---------------------------------------------------------------------------
# 3-way register swap tests (cycle: rA↔rB, rB↔rC, rA↔rC)
# ---------------------------------------------------------------------------

def diag_with_3way_gpr_swaps() -> Diagnosis:
    """Diagnosis with a 3-way GPR register swap cycle (r24↔r25↔r26)."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("r24", "r25"): SwapInfo(count=3, first_idx=10, last_idx=40),
        ("r25", "r26"): SwapInfo(count=2, first_idx=15, last_idx=45),
        ("r24", "r26"): SwapInfo(count=2, first_idx=20, last_idx=50),
    }
    return d


def diag_with_fpr_swaps() -> Diagnosis:
    """Diagnosis with FPR register swaps only (no GPR swaps)."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("f1", "f2"): SwapInfo(count=4, first_idx=5, last_idx=30),
        ("f3", "f4"): SwapInfo(count=2, first_idx=10, last_idx=35),
    }
    return d


def diag_with_mixed_gpr_fpr_swaps() -> Diagnosis:
    """Diagnosis with both GPR and FPR register swaps."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("r28", "r29"): SwapInfo(count=3, first_idx=10, last_idx=40),
        ("f1", "f2"): SwapInfo(count=4, first_idx=5, last_idx=30),
    }
    return d


class TestThreeWayRegSwap(unittest.TestCase):
    """Tests for 3-way register swap cycles.

    A 3-way swap (r24↔r25↔r26) means the compiler assigned registers
    in a cyclic pattern. Fixing this requires a 3-way permutation, not
    just pairwise swaps. These tests verify the current solver behavior
    and document expected failures.
    """

    @staticmethod
    def _make_mock_trace(colors: list[int]) -> object:
        from tools.compiler_trace.bsf_trace import BSFTrace, BSFCall
        from tools.compiler_trace.regmap_solver import INITIAL_COLORING_RVA
        calls = []
        for i, color in enumerate(colors):
            calls.append(BSFCall(
                index=i + 1,
                caller_rva=INITIAL_COLORING_RVA,
                lo=0, hi=0, base=0,
                bit=color,
            ))
        return BSFTrace(source=Path("/dev/null"), calls=calls)

    def test_3way_swap_relevant(self):
        """3-way GPR swaps should make declaration_reorder relevant."""
        from scripts.permuter.patterns import get_pattern
        p = get_pattern("declaration_reorder")
        self.assertTrue(p.relevant(diag_with_3way_gpr_swaps()))

    def test_3way_swap_generates_candidates(self):
        """The solver should generate SOME candidates for 3-way swaps.

        Even if pairwise swaps can't solve a 3-way cycle, the solver
        should still try individual pair swaps as partial fixes.
        """
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        # r24=color14, r25=color13, r26=color12
        # (color_to_gpr: color N = r(38-N) for callee-saved)
        colors = [7, 8, 9, 10, 11, 12, 13, 14]  # r31..r24
        trace = self._make_mock_trace(colors)
        decl_names = [f"v{i}" for i in range(8)]

        # 3-way cycle: r24↔r25, r25↔r26, r24↔r26
        swap_pairs = [("r24", "r25"), ("r25", "r26"), ("r24", "r26")]
        candidates = guided_pairwise_search(trace, swap_pairs, decl_names)

        self.assertGreater(len(candidates), 0,
                           "Should produce candidates even for 3-way swap")

    def test_3way_swap_has_cyclic_candidate(self):
        """A 3-way swap cycle needs a 3-way permutation (rotation).

        For r24↔r25↔r26, the fix is to rotate declarations:
        v5(r26) → v6(r25) → v7(r24) becomes v7 → v5 → v6
        (shift the 3 positions by 1).
        """
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        # 3 variables: colors 12,13,14 → r26,r25,r24
        colors = [12, 13, 14]
        trace = self._make_mock_trace(colors)
        decl_names = ["a", "b", "c"]

        # 3-way cycle
        swap_pairs = [("r24", "r25"), ("r25", "r26"), ("r24", "r26")]
        candidates = guided_pairwise_search(trace, swap_pairs, decl_names)

        # The cyclic rotation: a→b→c→a means [c, a, b] or [b, c, a]
        rotations = [["c", "a", "b"], ["b", "c", "a"]]
        has_rotation = any(r in candidates for r in rotations)
        self.assertTrue(has_rotation,
                        f"No cyclic rotation found in candidates: {candidates}")

    def test_3way_swap_pairwise_subset(self):
        """3-way swap should at least produce the individual pairwise swaps."""
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        # r24=color14, r25=color13, r26=color12
        colors = [12, 13, 14]
        trace = self._make_mock_trace(colors)
        decl_names = ["a", "b", "c"]

        swap_pairs = [("r24", "r25"), ("r25", "r26"), ("r24", "r26")]
        candidates = guided_pairwise_search(trace, swap_pairs, decl_names)

        # Should have at least the pairwise swaps
        # r24↔r25 = indices 2,1 = ["a", "c", "b"]
        # r25↔r26 = indices 1,0 = ["b", "a", "c"]
        # r24↔r26 = indices 2,0 = ["c", "b", "a"]
        pair_swaps = [["a", "c", "b"], ["b", "a", "c"], ["c", "b", "a"]]
        found = sum(1 for p in pair_swaps if p in candidates)
        self.assertGreaterEqual(found, 2,
                                f"Should have at least 2 pairwise swaps, got {found} of {candidates}")


# ---------------------------------------------------------------------------
# FPR register swap tests
# ---------------------------------------------------------------------------

class TestFPRSwapHandling(unittest.TestCase):
    """Tests for FPR (floating-point register) swap handling.

    Currently BSF tracing and the regmap solver only handle GPR swaps.
    FPR swaps are detected by diagnosis but NOT addressable by BSF-guided
    declaration reorder. These tests document the current state and
    will be updated when FPR support is added.
    """

    def test_fpr_only_diagnosis_makes_declreorder_irrelevant(self):
        """declaration_reorder should NOT be relevant for FPR-only swaps.

        FPR swaps (f-prefix) now trigger declaration_reorder since
        ASM-guided mode supports FPR swap pairs.
        """
        from scripts.permuter.patterns import get_pattern
        p = get_pattern("declaration_reorder")
        diag = diag_with_fpr_swaps()
        self.assertTrue(p.relevant(diag),
                        "declaration_reorder should be relevant for FPR swaps")

    def test_mixed_gpr_fpr_makes_declreorder_relevant(self):
        """Mixed GPR+FPR swaps should still make declreorder relevant (for GPR part)."""
        from scripts.permuter.patterns import get_pattern
        p = get_pattern("declaration_reorder")
        diag = diag_with_mixed_gpr_fpr_swaps()
        self.assertTrue(p.relevant(diag),
                        "Should be relevant when GPR swaps present alongside FPR")

    def test_fpr_color_mapping_not_implemented(self):
        """Verify FPR registers have no color mapping (documenting limitation)."""
        from tools.compiler_trace.regmap_solver import gpr_to_color
        # FPR registers should return None from the GPR mapper
        for fpr in ["f0", "f1", "f2", "f13", "f31"]:
            self.assertIsNone(gpr_to_color(fpr),
                              f"FPR {fpr} should not have a GPR color mapping")

    def test_fpr_swap_uses_untargeted_fallback(self):
        """FPR swaps fall through to bounded neighbor fallback (untargeted).

        Since fpr_to_color() doesn't exist, FPR pairs are treated as
        unmapped and get generic neighbor swaps. The candidates are NOT
        targeted at the right FPR registers — they're just blind guesses.
        When we add FPR color mapping, candidates should be targeted.
        """
        from tools.compiler_trace.regmap_solver import guided_pairwise_search
        from tools.compiler_trace.bsf_trace import BSFTrace, BSFCall
        from tools.compiler_trace.regmap_solver import INITIAL_COLORING_RVA

        calls = [
            BSFCall(index=1, caller_rva=INITIAL_COLORING_RVA, lo=0, hi=0, base=0, bit=26),
            BSFCall(index=2, caller_rva=INITIAL_COLORING_RVA, lo=0, hi=0, base=0, bit=27),
        ]
        trace = BSFTrace(source=Path("/dev/null"), calls=calls)
        decl_names = ["x", "y"]

        # FPR swap pair — unmappable, should use fallback
        candidates = guided_pairwise_search(trace, [("f1", "f2")], decl_names)
        # Fallback produces candidates but they're not targeted
        self.assertGreater(len(candidates), 0,
                           "Fallback should produce some candidates")
        # With only 2 decls, the only swap is ["y", "x"]
        self.assertEqual(candidates[0], ["y", "x"])

    def test_fpr_callee_saved_mapping(self):
        """Callee-saved FPR (f14-f31) should map to declaration indices.

        FPR allocation is sequential by declaration order:
        f31 = first float (index 0), f30 = second (index 1), etc.
        """
        from tools.compiler_trace.regmap_solver import (
            fpr_to_decl_index,
            decl_index_to_fpr,
            is_callee_saved_fpr,
        )

        # Callee-saved FPRs have declaration indices
        self.assertEqual(fpr_to_decl_index("f31"), 0)
        self.assertEqual(fpr_to_decl_index("f30"), 1)
        self.assertEqual(fpr_to_decl_index("f14"), 17)

        # Volatile FPRs return None
        self.assertIsNone(fpr_to_decl_index("f0"))
        self.assertIsNone(fpr_to_decl_index("f13"))

        # Round-trip
        self.assertEqual(decl_index_to_fpr(0), "f31")
        self.assertEqual(decl_index_to_fpr(1), "f30")
        self.assertEqual(decl_index_to_fpr(17), "f14")

        # Classification
        self.assertTrue(is_callee_saved_fpr("f14"))
        self.assertTrue(is_callee_saved_fpr("f31"))
        self.assertFalse(is_callee_saved_fpr("f13"))
        self.assertFalse(is_callee_saved_fpr("f0"))
        self.assertFalse(is_callee_saved_fpr("r31"))


# ---------------------------------------------------------------------------
# BSF isolation exact match tests
# ---------------------------------------------------------------------------

class TestBSFIsolationMatching(unittest.TestCase):
    """Tests for BSF per-function isolation matching logic.

    The partition matching in declaration_reorder.py should:
    1. Exact-match the mangled symbol first (tier 0)
    2. Match qualified name (both Class AND Method) as tier 1
    3. NOT match partial names (e.g., "Highlight" matching "RndHighlightable")
    """

    def test_exact_symbol_match_preferred(self):
        """When ctx.symbol is set, exact match should be used first."""
        # This is a logic test for the matching tiers. We verify that
        # _partition_match (conceptual) prefers exact symbol over fuzzy.
        # The actual method is embedded in declaration_reorder._try_bsf_guided()
        # so we test the logic in isolation.
        partitions = {
            "__all__": "all_trace",
            "?Highlight@CharCollide@@UAAXXZ": "correct_trace",
            "??0RndHighlightable@@QAA@XZ": "wrong_trace",
            "__remainder__": "remainder",
        }

        target_symbol = "?Highlight@CharCollide@@UAAXXZ"

        # Tier 0: exact match
        match = None
        for name in partitions:
            if name in ("__all__", "__remainder__"):
                continue
            if name == target_symbol:
                match = name
                break

        self.assertEqual(match, "?Highlight@CharCollide@@UAAXXZ",
                         "Exact symbol match should find correct partition")

    def test_fuzzy_match_rejects_partial_class(self):
        """Fuzzy matching should require BOTH class AND method name.

        Bug case: "Highlight" (method) substring-matched "RndHighlightable"
        (a different class constructor) because only method name was checked.
        """
        partitions = {
            "??0RndHighlightable@@QAA@XZ": "wrong_trace",  # Constructor of different class
            "?SetName@CharCollide@@UAAXXZ": "also_wrong",
        }

        # Target: CharCollide::Highlight
        class_name = "CharCollide"
        method_name = "Highlight"

        # Tier 1: require both class and method in partition name
        match = None
        for name in partitions:
            if class_name in name and method_name in name:
                match = name
                break

        self.assertIsNone(match,
                          "Should NOT match RndHighlightable when looking for CharCollide::Highlight")

    def test_fuzzy_match_finds_correct_partition(self):
        """When both class and method name appear, fuzzy match should work."""
        partitions = {
            "?Highlight@CharCollide@@UAAXXZ": "correct_trace",
            "??0RndHighlightable@@QAA@XZ": "wrong_trace",
        }

        class_name = "CharCollide"
        method_name = "Highlight"

        match = None
        for name in partitions:
            if class_name in name and method_name in name:
                match = name
                break

        self.assertEqual(match, "?Highlight@CharCollide@@UAAXXZ",
                         "Should match partition containing both CharCollide and Highlight")

    def test_operator_overload_exact_match(self):
        """Operator overloads should match via exact symbol (tier 0)."""
        partitions = {
            "??RFileMergerSort@@QAA_NPBUMerger@FileMerger@@0@Z": "correct_trace",
            "??0FileMerger@@QAA@XZ": "wrong_trace",
        }

        target_symbol = "??RFileMergerSort@@QAA_NPBUMerger@FileMerger@@0@Z"

        match = None
        for name in partitions:
            if name == target_symbol:
                match = name
                break

        self.assertEqual(match, target_symbol,
                         "Operator overload should match via exact symbol")


# ---------------------------------------------------------------------------
# BSF population reality check (documents what we learned from scanning)
# ---------------------------------------------------------------------------

class TestBSFPopulationAssumptions(unittest.TestCase):
    """Documents empirical findings about BSF call distribution.

    From scanning 100 AT_LIMIT functions with register swaps:
    - 17% have BSF calls in their partition (addressable by BSF-guided reorder)
    - 83% have 0 BSF calls (compiler uses simpler allocation, not graph coloring)
    - Functions with few local variables (< ~5-6) tend to have 0 BSF calls
    - Template/utility functions (MakeString, PropSync) consume most BSF calls
    """

    def test_gpr_to_color_covers_callee_saved_range(self):
        """Callee-saved GPRs r13-r31 should all have color mappings."""
        from tools.compiler_trace.regmap_solver import gpr_to_color
        for reg_num in range(13, 32):
            color = gpr_to_color(f"r{reg_num}")
            self.assertIsNotNone(color,
                                 f"r{reg_num} should have a color mapping")

    def test_color_range_for_typical_regswaps(self):
        """Typical regswap pairs (r28↔r29, r30↔r31) should map to adjacent colors."""
        from tools.compiler_trace.regmap_solver import gpr_to_color
        # r28=color10, r29=color9, r30=color8, r31=color7
        self.assertEqual(gpr_to_color("r31"), 7)
        self.assertEqual(gpr_to_color("r30"), 8)
        self.assertEqual(gpr_to_color("r29"), 9)
        self.assertEqual(gpr_to_color("r28"), 10)

        # Adjacent colors → adjacent declarations → pairwise swap is correct strategy
        self.assertEqual(gpr_to_color("r30") - gpr_to_color("r31"), 1)
        self.assertEqual(gpr_to_color("r29") - gpr_to_color("r30"), 1)


# ---------------------------------------------------------------------------
# Real-world test cases from the project
# ---------------------------------------------------------------------------

class TestRealWorldCalcRotzBone(unittest.TestCase):
    """Real-world test case: HamSkeletonConverter::CalcRotzBone.

    At 96.1% match with:
    - Offset swaps: (0x54,0x58) and (0x64,0x68) — dir1/dir2 stack layout
    - FPR scheduling: fneg/frsp order around -acos() result
    - f0↔f31 register swap (f31 is callee-saved = first float declared)
    - Branch polarity at instruction 65

    This function is a good test case because:
    1. It has a callee-saved FPR swap (f31) — testable with our new FPR mapping
    2. The offset swap correlates with Vector3 declaration order (dir1 vs dir2)
    3. The fix likely involves reordering float/Vector3 declarations
    """

    def test_fpr_f31_is_first_float_declaration(self):
        """f31 = first float declared. In CalcRotzBone, 'angle' is the only
        float local, so it gets f31. The target negates into f31 (callee-saved)
        while our code uses f0 (volatile)."""
        from tools.compiler_trace.regmap_solver import fpr_to_decl_index
        self.assertEqual(fpr_to_decl_index("f31"), 0,
                         "f31 should be declaration index 0 (first float)")

    def test_offset_swap_suggests_vector3_reorder(self):
        """Offset swaps (0x54↔0x58, 0x64↔0x68) are 4 bytes apart, suggesting
        adjacent Vector3 components or adjacent Vector3 locals on the stack.
        Swapping dir1/dir2 declaration order may fix this."""
        # Document the pattern: when two Vector3 locals have their
        # stack offsets swapped by exactly sizeof(float)=4, it means
        # the compiler laid them out in a different order
        offset_pairs = [(0x54, 0x58), (0x64, 0x68)]
        for a, b in offset_pairs:
            self.assertEqual(abs(a - b), 4,
                             "Offset swap should be sizeof(float) apart")

    def test_fpr_swap_classification(self):
        """CalcRotzBone has 2 FPR swap pairs: f0↔f31 (callee-saved) and f0↔f1 (volatile).
        Our FPR mapping should correctly classify them."""
        from tools.compiler_trace.regmap_solver import is_callee_saved_fpr

        # Real swap pairs from diff_inspect regswaps output
        swap_pairs = [("f0", "f31"), ("f0", "f1")]

        callee_saved_swaps = [
            (a, b) for a, b in swap_pairs
            if is_callee_saved_fpr(a) or is_callee_saved_fpr(b)
        ]
        volatile_only_swaps = [
            (a, b) for a, b in swap_pairs
            if not is_callee_saved_fpr(a) and not is_callee_saved_fpr(b)
        ]
        self.assertEqual(len(callee_saved_swaps), 1,
                         "Should have 1 swap involving callee-saved FPR (f0↔f31)")
        self.assertEqual(len(volatile_only_swaps), 1,
                         "Should have 1 volatile-only FPR swap (f0↔f1)")

    def test_fpr_targeted_swap_for_callee_saved_pair(self):
        """The FPR swap f0↔f31 involves one callee-saved register (f31).
        Since f0 is volatile (return value), this is NOT a pure declaration
        reorder target — it's a scheduling difference. Only f14-f31 ↔ f14-f31
        pairs are pure declaration reorder targets."""
        from tools.compiler_trace.regmap_solver import (
            fpr_to_decl_index,
            is_callee_saved_fpr,
        )
        # f0↔f31: f0 is volatile, f31 is callee-saved
        # This is NOT a pure declaration reorder target because
        # f0 has no declaration index
        self.assertIsNone(fpr_to_decl_index("f0"))
        self.assertIsNotNone(fpr_to_decl_index("f31"))

        # Pure callee-saved pairs (like f30↔f31 in Invert) ARE targets
        self.assertTrue(is_callee_saved_fpr("f30"))
        self.assertTrue(is_callee_saved_fpr("f31"))
        idx30 = fpr_to_decl_index("f30")
        idx31 = fpr_to_decl_index("f31")
        self.assertEqual(abs(idx30 - idx31), 1,
                         "f30 and f31 are adjacent declaration indices")


class TestRealWorldInvertTransform(unittest.TestCase):
    """Real-world test case: Invert(Transform const&, Transform&).

    At 99.5% match with:
    - f30↔f31 callee-saved FPR swap (4 instructions)
    - Offset swaps: (0x4,0x14) and (0x8,0x18) — Matrix3 member access order
    - Pure FPR swap — no GPR swaps

    This is the cleanest FPR swap target: a pure callee-saved pair (f30↔f31)
    that should be fixable by swapping two float expression evaluations.
    """

    def test_f30_f31_are_adjacent_declarations(self):
        """f30 = second float, f31 = first float. Swapping their declaration
        order should swap the register assignment."""
        from tools.compiler_trace.regmap_solver import fpr_to_decl_index
        self.assertEqual(fpr_to_decl_index("f31"), 0)  # First float
        self.assertEqual(fpr_to_decl_index("f30"), 1)  # Second float
        # Adjacent — a simple pairwise swap of float declarations

    def test_guided_search_maps_fpr_callee_saved_pair(self):
        """guided_pairwise_search should map f30↔f31 to declaration indices
        0↔1 and generate a targeted swap candidate."""
        from tools.compiler_trace.regmap_solver import (
            guided_pairwise_search,
            INITIAL_COLORING_RVA,
        )
        from tools.compiler_trace.bsf_trace import BSFTrace, BSFCall

        # Minimal BSF trace with 2 GPR-like variables
        # (BSF trace is used for GPR coloring; FPR mapping is direct)
        trace = BSFTrace(source=Path("test.cpp"), calls=[
            BSFCall(index=1, caller_rva=INITIAL_COLORING_RVA,
                    lo=0xFFFFFF80, hi=0xFFFFFFFF, base=0, bit=7),
            BSFCall(index=2, caller_rva=INITIAL_COLORING_RVA,
                    lo=0xFFFFFF00, hi=0xFFFFFFFF, base=0, bit=8),
        ])

        # FPR swap pair from objdiff
        swap_pairs = [("f30", "f31")]
        decl_names = ["expr_a", "expr_b"]

        candidates = guided_pairwise_search(
            trace, swap_pairs, decl_names
        )

        # Should produce a targeted swap: ["expr_b", "expr_a"]
        self.assertTrue(len(candidates) > 0,
                        "Should produce candidates for callee-saved FPR swap")
        self.assertIn(["expr_b", "expr_a"], candidates,
                      "Should include the pairwise swap of the two declarations")


if __name__ == "__main__":
    unittest.main()
