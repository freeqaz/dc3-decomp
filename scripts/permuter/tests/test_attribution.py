"""Tests for instruction attribution pipeline."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.attribution import (
    AsmEntry,
    AsmInstruction,
    AsmListing,
    AttributedMismatch,
    MismatchRegion,
    aggregate_regions,
    attribute_function,
    attribute_mismatches,
    parse_asm_listing,
)


# Realistic /FAs listing fragment for a simple function
SAMPLE_LISTING = """\
?test_func@@YAHH@Z\tPROC NEAR\t\t\t; test_func, COMDAT

; Begin code for function: ?test_func@@YAHH@Z

; 3    : int test_func(int x) {

\tmflr         r12
\tbl           __savegprlr_29
\tstwu         r1,-80h(r1)
.endprolog
$M2555:

; 4    :     int a = GetValue();

\tli           r3,1
\tbl           ?GetValue@@YAHXZ
\tmr           r31,r3

; 5    :     int b = GetOther();

\tli           r3,2
\tbl           ?GetOther@@YAHXZ
\tmr           r30,r3

; 6    :     if (a > b) {

\tcmpw         r31,r30
\tble          $LN1

; 7    :         return a;

\tmr           r3,r31
\tb            $LN2

; 8    :     }
; 9    :     return b;

$LN1:
\tmr           r3,r30
$LN2:

; 10   : }

\taddi         r1,r1,80h
\tb            __restgprlr_29
?test_func@@YAHH@Z\tENDP
"""


class TestParseAsmListing(unittest.TestCase):
    def test_parses_function(self):
        listing = parse_asm_listing(SAMPLE_LISTING, "test_func")
        self.assertIsNotNone(listing)
        self.assertGreater(len(listing.entries), 0)

    def test_extracts_prologue_helper(self):
        listing = parse_asm_listing(SAMPLE_LISTING, "test_func")
        self.assertIsNotNone(listing)
        self.assertIsNotNone(listing.prologue_helper)
        self.assertIn("savegprlr_29", listing.prologue_helper)
        self.assertEqual(listing.callee_saved_count, 3)  # 32 - 29

    def test_source_line_attribution(self):
        listing = parse_asm_listing(SAMPLE_LISTING, "test_func")
        self.assertIsNotNone(listing)
        # The GetValue call block should be attributed to line 4
        found_line4 = False
        for entry in listing.entries:
            if entry.source_line == 4:
                found_line4 = True
                self.assertIn("GetValue", entry.source_text)
                self.assertGreater(len(entry.instructions), 0)
        self.assertTrue(found_line4, "Should find entry for line 4")

    def test_instruction_parsing(self):
        listing = parse_asm_listing(SAMPLE_LISTING, "test_func")
        self.assertIsNotNone(listing)
        all_instrs = listing.all_instructions()
        self.assertGreater(len(all_instrs), 5)
        # Check that opcodes are parsed
        opcodes = {instr.opcode for _, instr in all_instrs}
        self.assertIn("bl", opcodes)
        self.assertIn("mr", opcodes)

    def test_returns_none_for_missing_function(self):
        listing = parse_asm_listing(SAMPLE_LISTING, "nonexistent_func")
        self.assertIsNone(listing)

    def test_instruction_count(self):
        listing = parse_asm_listing(SAMPLE_LISTING, "test_func")
        self.assertIsNotNone(listing)
        count = listing.instruction_count()
        self.assertGreater(count, 8)

    def test_source_line_for_index(self):
        listing = parse_asm_listing(SAMPLE_LISTING, "test_func")
        self.assertIsNotNone(listing)
        # First few instructions are prologue (line 3)
        entry = listing.source_line_for_index(0)
        self.assertIsNotNone(entry)
        self.assertEqual(entry.source_line, 3)


class TestAttributeMismatches(unittest.TestCase):
    def _make_listing(self):
        return parse_asm_listing(SAMPLE_LISTING, "test_func")

    def test_attributes_opcode_mismatch(self):
        listing = self._make_listing()
        self.assertIsNotNone(listing)

        # Find the index of the cmpw instruction
        all_instrs = listing.all_instructions()
        cmpw_idx = None
        for i, (entry, instr) in enumerate(all_instrs):
            if instr.opcode == "cmpw":
                cmpw_idx = i
                break
        self.assertIsNotNone(cmpw_idx)

        diff_instructions = [
            {
                "index": cmpw_idx,
                "diff_kind": "replace",
                "target_opcode": "subf.",
                "base_opcode": "cmpw",
            },
        ]

        attributed = attribute_mismatches(listing, diff_instructions)
        self.assertEqual(len(attributed), 1)
        self.assertEqual(attributed[0].mismatch_type, "opcode")
        self.assertEqual(attributed[0].target_opcode, "subf.")
        self.assertEqual(attributed[0].base_opcode, "cmpw")
        # Should be attributed to line 6 (the if statement)
        self.assertEqual(attributed[0].source_line, 6)
        self.assertGreater(attributed[0].confidence, 0.5)

    def test_skips_matching_instructions(self):
        listing = self._make_listing()
        diff_instructions = [
            {"index": 0, "diff_kind": "match", "target_opcode": "mflr", "base_opcode": "mflr"},
        ]
        attributed = attribute_mismatches(listing, diff_instructions)
        self.assertEqual(len(attributed), 0)

    def test_handles_insert_delete(self):
        listing = self._make_listing()
        diff_instructions = [
            {"index": 2, "diff_kind": "insert", "target_opcode": "nop", "base_opcode": ""},
            {"index": 3, "diff_kind": "delete", "target_opcode": "", "base_opcode": "li"},
        ]
        attributed = attribute_mismatches(listing, diff_instructions)
        self.assertEqual(len(attributed), 2)
        types = {a.mismatch_type for a in attributed}
        self.assertIn("extra", types)
        self.assertIn("missing", types)

    def test_interpolates_unattributed(self):
        listing = self._make_listing()
        # Use an index beyond the listing — should interpolate from neighbors
        big_idx = listing.instruction_count() + 5
        diff_instructions = [
            {"index": big_idx, "diff_kind": "replace", "target_opcode": "stw", "base_opcode": "lwz"},
        ]
        attributed = attribute_mismatches(listing, diff_instructions)
        self.assertEqual(len(attributed), 1)
        # Confidence should be low (interpolated or unattributed)
        self.assertLessEqual(attributed[0].confidence, 0.5)


class TestAggregateRegions(unittest.TestCase):
    def test_groups_adjacent_lines(self):
        listing = parse_asm_listing(SAMPLE_LISTING, "test_func")
        self.assertIsNotNone(listing)

        mismatches = [
            AttributedMismatch(0, "subf.", "cmpw", "opcode", "src/test.cpp", 6, "if (a > b)", 0.9),
            AttributedMismatch(1, "bge", "ble", "opcode", "src/test.cpp", 6, "if (a > b)", 0.9),
            AttributedMismatch(2, "mr", "mr", "register", "src/test.cpp", 7, "return a;", 0.9),
        ]

        regions = aggregate_regions(mismatches, listing, gap_tolerance=2)
        # Lines 6 and 7 should merge into one region
        self.assertEqual(len(regions), 1)
        self.assertEqual(regions[0].start_line, 6)
        self.assertEqual(regions[0].end_line, 7)
        self.assertEqual(regions[0].mismatch_count, 3)
        self.assertEqual(regions[0].dominant_type, "opcode")

    def test_separates_distant_lines(self):
        listing = parse_asm_listing(SAMPLE_LISTING, "test_func")
        self.assertIsNotNone(listing)

        mismatches = [
            AttributedMismatch(0, "x", "y", "opcode", "src/test.cpp", 4, "line4", 0.9),
            AttributedMismatch(1, "x", "y", "opcode", "src/test.cpp", 9, "line9", 0.9),
        ]

        regions = aggregate_regions(mismatches, listing, gap_tolerance=2)
        # Lines 4 and 9 are too far apart — separate regions
        self.assertEqual(len(regions), 2)

    def test_sorted_by_impact(self):
        listing = parse_asm_listing(SAMPLE_LISTING, "test_func")
        self.assertIsNotNone(listing)

        mismatches = [
            AttributedMismatch(0, "x", "y", "opcode", "src/test.cpp", 4, "line4", 0.9),
            AttributedMismatch(1, "a", "b", "opcode", "src/test.cpp", 9, "line9", 0.9),
            AttributedMismatch(2, "c", "d", "opcode", "src/test.cpp", 9, "line9", 0.9),
            AttributedMismatch(3, "e", "f", "register", "src/test.cpp", 9, "line9", 0.9),
        ]

        regions = aggregate_regions(mismatches, listing, gap_tolerance=2)
        self.assertEqual(len(regions), 2)
        # Region with 3 mismatches should come first
        self.assertEqual(regions[0].impact, 3)
        self.assertEqual(regions[1].impact, 1)

    def test_handles_unattributed(self):
        listing = parse_asm_listing(SAMPLE_LISTING, "test_func")
        mismatches = [
            AttributedMismatch(0, "x", "y", "opcode", None, None, None, 0.0),
        ]
        regions = aggregate_regions(mismatches, listing)
        self.assertEqual(len(regions), 1)
        self.assertEqual(regions[0].source_file, "<unknown>")
        self.assertEqual(regions[0].unattributed_count, 1)


class TestFullPipeline(unittest.TestCase):
    def test_attribute_function(self):
        all_match = [
            {"index": i, "diff_kind": "match", "target_opcode": "nop", "base_opcode": "nop"}
            for i in range(5)
        ]
        # Add one mismatch
        all_match.append({
            "index": 5,
            "diff_kind": "replace",
            "target_opcode": "subf.",
            "base_opcode": "cmpw",
        })

        listing, attributed, regions = attribute_function(
            SAMPLE_LISTING, "test_func", all_match,
        )
        self.assertIsNotNone(listing)
        self.assertEqual(len(attributed), 1)
        self.assertGreaterEqual(len(regions), 1)

    def test_returns_empty_for_missing_function(self):
        listing, attributed, regions = attribute_function(
            SAMPLE_LISTING, "no_such_func", [],
        )
        self.assertIsNone(listing)
        self.assertEqual(attributed, [])
        self.assertEqual(regions, [])


class TestMismatchRegionProperties(unittest.TestCase):
    def test_match_ratio(self):
        region = MismatchRegion(
            source_file="test.cpp",
            start_line=10,
            end_line=12,
            source_lines=["line10", "line11", "line12"],
            mismatches=[
                AttributedMismatch(0, "x", "y", "opcode", "test.cpp", 10, "line10", 0.9),
            ],
            dominant_type="opcode",
            total_instructions=10,
            matched_instructions=9,
        )
        self.assertAlmostEqual(region.match_ratio, 0.9)

    def test_match_ratio_zero_instructions(self):
        region = MismatchRegion(
            source_file="test.cpp",
            start_line=1,
            end_line=1,
            source_lines=[],
            mismatches=[],
            dominant_type="unknown",
            total_instructions=0,
            matched_instructions=0,
        )
        self.assertAlmostEqual(region.match_ratio, 1.0)


if __name__ == "__main__":
    unittest.main()
