"""Tests for the compiler atlas module."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.compiler_atlas import (
    AtlasEntry,
    Confidence,
    all_entries,
    boost_patterns,
    lookup,
    lookup_for_diagnosis,
)


class TestAtlasEntries(unittest.TestCase):
    def test_has_entries(self):
        entries = all_entries()
        self.assertGreater(len(entries), 55, "Atlas should have 55+ entries")

    def test_entries_have_required_fields(self):
        for entry in all_entries():
            self.assertIsInstance(entry.name, str)
            self.assertTrue(len(entry.name) > 0, f"Entry has empty name")
            self.assertIsInstance(entry.confidence, Confidence)
            self.assertIsInstance(entry.fixable, bool)
            self.assertIsInstance(entry.opcodes, tuple)
            self.assertIsInstance(entry.pattern_names, tuple)

    def test_proven_entries_exist(self):
        proven = [e for e in all_entries() if e.confidence == Confidence.PROVEN]
        self.assertGreater(len(proven), 35, "Should have 35+ proven entries")

    def test_negative_entries_exist(self):
        negative = [e for e in all_entries() if e.confidence == Confidence.NEGATIVE]
        self.assertGreater(len(negative), 15, "Should have 15+ negative entries")

    def test_negative_entries_not_fixable(self):
        for entry in all_entries():
            if entry.confidence == Confidence.NEGATIVE:
                self.assertFalse(
                    entry.fixable,
                    f"Negative entry '{entry.name}' should not be fixable",
                )

    def test_unique_names(self):
        names = [e.name for e in all_entries()]
        self.assertEqual(len(names), len(set(names)), "Entry names must be unique")


class TestLookup(unittest.TestCase):
    def test_lookup_subf(self):
        entries = lookup(["subf."])
        self.assertGreater(len(entries), 0)
        names = {e.name for e in entries}
        self.assertIn("subf_loop_condition", names)

    def test_lookup_addic_subfe(self):
        entries = lookup(["addic", "subfe"])
        self.assertGreater(len(entries), 0)
        # Should find bool_zero_test
        names = {e.name for e in entries}
        self.assertIn("bool_zero_test", names)

    def test_lookup_empty(self):
        entries = lookup([])
        self.assertEqual(len(entries), 0)

    def test_lookup_unknown_opcode(self):
        entries = lookup(["xyzzy_nonexistent"])
        self.assertEqual(len(entries), 0)

    def test_fixable_only(self):
        entries = lookup(["mr"], fixable_only=True)
        for entry in entries:
            self.assertTrue(entry.fixable)

    def test_exclude_negative(self):
        entries = lookup(["mr"], include_negative=False)
        for entry in entries:
            self.assertNotEqual(entry.confidence, Confidence.NEGATIVE)

    def test_proven_ranked_first(self):
        entries = lookup(["lis", "addi"])
        if len(entries) >= 2:
            # Proven entries should come before negative
            proven_idx = None
            negative_idx = None
            for i, e in enumerate(entries):
                if e.confidence == Confidence.PROVEN and proven_idx is None:
                    proven_idx = i
                if e.confidence == Confidence.NEGATIVE and negative_idx is None:
                    negative_idx = i
            if proven_idx is not None and negative_idx is not None:
                self.assertLess(proven_idx, negative_idx)


class TestHarvestedLookups(unittest.TestCase):
    """Tests for entries harvested from docs/decomp/patterns/*.md."""

    def test_lookup_fsel(self):
        entries = lookup(["fsel", "fneg"])
        names = {e.name for e in entries}
        self.assertIn("fsel_clamp_template", names)

    def test_lookup_fmsubs_fnmsubs(self):
        entries = lookup(["fmsubs", "fnmsubs"])
        names = {e.name for e in entries}
        self.assertIn("fma_subtract_order", names)

    def test_lookup_rlwimi(self):
        entries = lookup(["rlwimi"])
        names = {e.name for e in entries}
        self.assertIn("byte_mask_extraction", names)

    def test_lookup_fctiwz(self):
        entries = lookup(["fctiwz"])
        names = {e.name for e in entries}
        self.assertIn("float_int_reconversion", names)

    def test_lookup_clrlwi_finds_local_bool(self):
        entries = lookup(["clrlwi"])
        names = {e.name for e in entries}
        self.assertIn("local_bool_extraction", names)

    def test_lookup_srawi_addze_finds_sizeof(self):
        entries = lookup(["srawi", "addze"])
        names = {e.name for e in entries}
        self.assertIn("sizeof_signed_cast", names)

    def test_lookup_fneg_frsp_finds_negation_split(self):
        entries = lookup(["fneg", "frsp"])
        names = {e.name for e in entries}
        self.assertIn("negation_split_frsp", names)

    def test_lookup_subfic_finds_negative(self):
        entries = lookup(["subfic"])
        names = {e.name for e in entries}
        self.assertIn("bool_negation_subfic", names)

    def test_lookup_lwzx_finds_large_offset(self):
        entries = lookup(["lwzx"])
        names = {e.name for e in entries}
        self.assertIn("large_offset_addressing", names)

    def test_fixable_only_excludes_new_negatives(self):
        entries = lookup(["fsel"], fixable_only=True)
        for entry in entries:
            self.assertTrue(entry.fixable,
                            f"fixable_only returned unfixable '{entry.name}'")

    def test_boost_fsel_clamp(self):
        entries = lookup(["fsel"])
        boost, suppress = boost_patterns(entries)
        self.assertIn("float_clamp", boost)

    def test_boost_ternary(self):
        # ternary_vs_ifelse uses beq/bne opcodes
        entries = [e for e in all_entries() if e.name == "ternary_vs_ifelse"]
        boost, suppress = boost_patterns(entries)
        self.assertIn("ternary_swap", boost)

    def test_boost_iterator_index(self):
        entries = lookup(["subf", "clrrwi"])
        boost, suppress = boost_patterns(entries)
        self.assertIn("iterator_index_compare", boost)

    def test_harvested_entries_have_provenance(self):
        """All harvested entries reference their source pattern doc."""
        harvested_prefixes = (
            "fixable-", "unfixable-compiler.md", "at-limit-systemic.md",
            "fixable-bool-mask.md", "fixable-copy-ctor.md", "fixable-macros.md",
        )
        for entry in all_entries():
            if any(entry.provenance.startswith(p) for p in harvested_prefixes):
                self.assertTrue(
                    len(entry.source_feature) > 0,
                    f"Harvested entry '{entry.name}' has empty source_feature",
                )


class TestBoostPatterns(unittest.TestCase):
    def test_returns_boost_set(self):
        entries = lookup(["subf."])
        boost, suppress = boost_patterns(entries)
        self.assertIn("loop_condition_subtract", boost)

    def test_negative_does_not_boost(self):
        entries = lookup(["mr"], include_negative=True)
        negative = [e for e in entries if e.confidence == Confidence.NEGATIVE]
        boost, suppress = boost_patterns(negative)
        self.assertEqual(len(boost), 0)

    def test_empty_entries(self):
        boost, suppress = boost_patterns([])
        self.assertEqual(len(boost), 0)
        self.assertEqual(len(suppress), 0)


class TestLookupForDiagnosis(unittest.TestCase):
    def test_diff_ops_lookup(self):
        entries = lookup_for_diagnosis(diff_ops=["subf."])
        names = {e.name for e in entries}
        self.assertIn("subf_loop_condition", names)

    def test_regswap_lookup(self):
        entries = lookup_for_diagnosis(reg_swap_pairs=[("r28", "r29")])
        self.assertGreater(len(entries), 0)

    def test_prologue_lookup(self):
        entries = lookup_for_diagnosis(has_prologue_mismatch=True)
        names = {e.name for e in entries}
        self.assertTrue(
            "regalloc_decl_order" in names or "regalloc_prologue_mismatch" in names,
        )

    def test_empty_diagnosis(self):
        entries = lookup_for_diagnosis()
        self.assertEqual(len(entries), 0)


if __name__ == "__main__":
    unittest.main()
