"""ASM register mapping and listing parser tests."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))


class TestAsmRegMap(unittest.TestCase):
    """Tests for AsmRegMap dataclass and asm_guided_search()."""

    def test_simple_swap_produces_targeted_candidate(self):
        """With a→r31, b→r30 and swap pair (r30, r31), first candidate swaps a↔b."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(
            var_to_reg={"a": "r31", "b": "r30", "c": "r29"},
            reg_to_var={"r31": "a", "r30": "b", "r29": "c"},
            callee_saved_count=3,
        )
        candidates = asm_guided_search(regmap, [("r30", "r31")], ["a", "b", "c"])

        self.assertGreater(len(candidates), 0)
        self.assertEqual(candidates[0], ["b", "a", "c"],
                         "First candidate should swap the two targeted vars")

    def test_swap_pair_order_does_not_matter(self):
        """(r30, r31) and (r31, r30) should produce the same candidates."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(
            var_to_reg={"x": "r31", "y": "r30"},
            reg_to_var={"r31": "x", "r30": "y"},
            callee_saved_count=2,
        )
        cands_a = asm_guided_search(regmap, [("r30", "r31")], ["x", "y"])
        cands_b = asm_guided_search(regmap, [("r31", "r30")], ["x", "y"])

        self.assertEqual(cands_a, cands_b)

    def test_unmapped_swap_pair_produces_no_candidates(self):
        """Swap pair referencing registers not in the mapping yields nothing."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(
            var_to_reg={"a": "r31"},
            reg_to_var={"r31": "a"},
            callee_saved_count=2,
        )
        candidates = asm_guided_search(regmap, [("r28", "r27")], ["a", "b"])
        self.assertEqual(candidates, [])

    def test_empty_regmap_produces_no_candidates(self):
        """An empty AsmRegMap should produce no candidates."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(callee_saved_count=3)
        candidates = asm_guided_search(regmap, [("r30", "r31")], ["a", "b", "c"])
        self.assertEqual(candidates, [])

    def test_fpr_swap_pairs_are_skipped(self):
        """asm_guided_search only handles GPR swaps, FPR pairs are ignored."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(
            var_to_reg={"a": "r31", "b": "r30"},
            reg_to_var={"r31": "a", "r30": "b"},
            callee_saved_count=2,
        )
        candidates = asm_guided_search(regmap, [("f30", "f31")], ["a", "b"])
        self.assertEqual(candidates, [])

    def test_multi_swap_produces_simultaneous_candidate(self):
        """Two swap pairs should produce a combined simultaneous swap."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(
            var_to_reg={"a": "r31", "b": "r30", "c": "r29", "d": "r28"},
            reg_to_var={"r31": "a", "r30": "b", "r29": "c", "r28": "d"},
            callee_saved_count=4,
        )
        candidates = asm_guided_search(
            regmap, [("r31", "r30"), ("r29", "r28")], ["a", "b", "c", "d"]
        )

        # Should include the simultaneous swap: ["b", "a", "d", "c"]
        self.assertIn(["b", "a", "d", "c"], candidates)

    def test_neighbor_variants_are_generated(self):
        """Targeted swap should also produce ±1 neighbor variants."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(
            var_to_reg={"a": "r31", "b": "r30", "c": "r29"},
            reg_to_var={"r31": "a", "r30": "b", "r29": "c"},
            callee_saved_count=3,
        )
        candidates = asm_guided_search(regmap, [("r30", "r31")], ["a", "b", "c"])

        # Should have more than just the direct swap
        self.assertGreater(len(candidates), 1,
                           "Should include neighbor variants beyond the direct swap")

    def test_single_var_produces_no_candidates(self):
        """Need at least 2 declarations for any swap."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(
            var_to_reg={"a": "r31"},
            reg_to_var={"r31": "a"},
            callee_saved_count=1,
        )
        candidates = asm_guided_search(regmap, [("r30", "r31")], ["a"])
        self.assertEqual(candidates, [])


class TestParseAsmListing(unittest.TestCase):
    """Tests for parse_asm_listing() — /FAs listing parser."""

    @staticmethod
    def _make_listing(func_name: str, body_lines: list[str],
                      savegprlr: int | None = None,
                      individual_saves: list[int] | None = None) -> list[str]:
        """Build a synthetic /FAs listing for testing.

        Args:
            func_name: Function name for PROC/ENDP markers.
            body_lines: Lines between .endprolog and ENDP.
            savegprlr: If set, add bl __savegprlr_N prologue.
            individual_saves: If set, add individual stw saves.
        """
        lines = [f"{func_name} PROC NEAR"]
        if savegprlr is not None:
            lines.append(f"\tbl\t__savegprlr_{savegprlr}")
        if individual_saves:
            for reg in individual_saves:
                lines.append(f"\tstw\tr{reg}, -{(32-reg)*8}(r1)")
        lines.append(".endprolog")
        lines.extend(body_lines)
        lines.append(f"{func_name} ENDP")
        return lines

    def test_basic_var_to_reg_mapping(self):
        """Source declaration followed by mr to callee-saved reg is captured."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("TestFunc", [
            "; 10   : \tint a = GetValue();",
            "\tbl\t?GetValue@@YAHXZ",
            "\tmr\tr31, r3",
            "; 11   : \tint b = GetOther();",
            "\tbl\t?GetOther@@YAHXZ",
            "\tmr\tr30, r3",
        ], savegprlr=30)

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        self.assertEqual(regmap.var_to_reg.get("a"), "r31")
        self.assertEqual(regmap.var_to_reg.get("b"), "r30")
        self.assertEqual(regmap.callee_saved_count, 2)

    def test_function_not_found_returns_none(self):
        """When function name doesn't match, returns None."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("OtherFunc", [
            "; 10   : \tint x = 1;",
            "\tli\tr31, 1",
        ], savegprlr=31)

        regmap = parse_asm_listing(listing, "NonExistent")
        self.assertIsNone(regmap)

    def test_zero_callee_saved_returns_empty(self):
        """Function with no callee-saved registers returns empty mapping."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = [
            "TestFunc PROC NEAR",
            ".endprolog",
            "; 5    : \treturn 42;",
            "\tli\tr3, 42",
            "\tblr",
            "TestFunc ENDP",
        ]

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        self.assertEqual(regmap.callee_saved_count, 0)
        self.assertEqual(regmap.var_to_reg, {})

    def test_param_saves_not_mapped_to_vars(self):
        """mr rN, r3/r4 (parameter saves) should not be attributed to variables."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("TestFunc", [
            "; 10   : \tif (x > 0) {",
            "\tmr\tr31, r3",       # param save — should be marked as param
            "\tmr\tr30, r4",       # param save — should be marked as param
            "; 11   : \tint result = compute();",
            "\tbl\t?compute@@YAHXZ",
            "\tmr\tr29, r3",       # THIS should map to 'result'
        ], savegprlr=29)

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        # r31 and r30 should be params, not variable mappings
        self.assertNotIn("r31", {v: k for k, v in regmap.var_to_reg.items()})
        self.assertNotIn("r30", {v: k for k, v in regmap.var_to_reg.items()})
        # r29 should map to 'result'
        self.assertEqual(regmap.var_to_reg.get("result"), "r29")

    def test_non_declaration_source_resets_context(self):
        """A non-declaration source comment should not carry over to next asm."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("TestFunc", [
            "; 10   : \tint a = GetValue();",
            "\tbl\t?GetValue@@YAHXZ",
            "\tmr\tr31, r3",
            "; 11   : \tif (a > 0) {",  # Not a declaration
            "\tmr\tr30, r3",             # Should NOT map to 'a' again
        ], savegprlr=30)

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        self.assertEqual(regmap.var_to_reg.get("a"), "r31")
        # r30 should not be mapped to anything (source context was reset)
        self.assertNotIn("r30", regmap.reg_to_var.keys() - {"r30"}
                         if "r30" in regmap.reg_to_var else set())
        self.assertNotIn("a", [v for k, v in regmap.reg_to_var.items() if k == "r30"])

    def test_unwind_handler_stops_parsing(self):
        """__unwind labels should stop parsing to avoid false mappings."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("TestFunc", [
            "; 10   : \tint a = GetValue();",
            "\tbl\t?GetValue@@YAHXZ",
            "\tmr\tr31, r3",
            "; 11   : \t}",
            "\tblr",
            "__unwind$12345:",
            "\taddi\tr31, r12, -144",
            ".endprolog",
            "; 10   : \tint b = Other();",  # In unwind — should not be parsed
            "\tmr\tr30, r3",
        ], savegprlr=30)

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        self.assertEqual(regmap.var_to_reg.get("a"), "r31")
        # 'b' should NOT be mapped (it's in the unwind handler)
        self.assertNotIn("b", regmap.var_to_reg)

    def test_function_definition_not_treated_as_declaration(self):
        """Source comment showing a function def should not produce a var mapping."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("TestFunc", [
            "; 10   : void TestFunc(int x) {",
            "\tmr\tr31, r3",
            "; 11   : \tint result = compute();",
            "\tbl\t?compute@@YAHXZ",
            "\tmr\tr30, r3",
        ], savegprlr=30)

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        # "TestFunc" should not be in var_to_reg
        self.assertNotIn("TestFunc", regmap.var_to_reg)
        # But 'result' should be mapped
        self.assertEqual(regmap.var_to_reg.get("result"), "r30")

    def test_already_assigned_reg_not_overwritten(self):
        """A callee-saved register already assigned to one var should not be
        reassigned to a later variable."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("TestFunc", [
            "; 10   : \tint a = First();",
            "\tbl\t?First@@YAHXZ",
            "\tmr\tr31, r3",
            "; 11   : \tint b = Second();",
            "\tbl\t?Second@@YAHXZ",
            "\tmr\tr31, r3",  # r31 already taken by 'a'
        ], savegprlr=31)

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        self.assertEqual(regmap.var_to_reg.get("a"), "r31")
        # 'b' should NOT be mapped to r31 (already taken)
        self.assertNotIn("b", regmap.var_to_reg)

    def test_individual_saves_detected(self):
        """Functions using individual stw saves (debug builds) are handled."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("TestFunc", [
            "; 10   : \tint a = First();",
            "\tbl\t?First@@YAHXZ",
            "\tmr\tr31, r3",
            "; 11   : \tint b = Second();",
            "\tbl\t?Second@@YAHXZ",
            "\tmr\tr30, r3",
        ], individual_saves=[30, 31])

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        self.assertEqual(regmap.callee_saved_count, 2)


if __name__ == "__main__":
    unittest.main()
