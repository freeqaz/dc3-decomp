"""Tests for the target facts module."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.target_facts import (
    TargetFact,
    TargetFacts,
    extract_facts,
    extract_from_atlas,
    extract_from_diagnosis,
    extract_from_shape_facts,
)


def _make_diagnosis(**overrides):
    """Create a mock Diagnosis object."""
    d = MagicMock()
    d.has_prologue_mismatch = overrides.get("has_prologue_mismatch", False)
    d.gpr_save_delta = overrides.get("gpr_save_delta", 0)
    d.fpr_save_delta = overrides.get("fpr_save_delta", 0)
    d.noise_total = overrides.get("noise_total", 0)
    d.noise_explained = overrides.get("noise_explained", 0)
    d.reg_swap_pairs = overrides.get("reg_swap_pairs", [])
    return d


def _make_region(start, end, dominant_type="opcode", mismatch_count=3,
                 total_instructions=10, match_ratio=0.7):
    """Create a mock MismatchRegion."""
    r = MagicMock()
    r.start_line = start
    r.end_line = end
    r.dominant_type = dominant_type
    r.mismatch_count = mismatch_count
    r.total_instructions = total_instructions
    r.match_ratio = match_ratio
    return r


def _make_atlas_entry(name, opcodes, fixable=True, confidence_value="proven",
                      pattern_names=()):
    """Create a mock AtlasEntry."""
    e = MagicMock()
    e.name = name
    e.opcodes = tuple(opcodes)
    e.fixable = fixable
    e.confidence = MagicMock()
    e.confidence.value = confidence_value
    e.pattern_names = tuple(pattern_names)
    e.source_feature = "test feature"
    e.gap_estimate = "1-3%"
    return e


class TestTargetFact(unittest.TestCase):
    def test_global_fact(self):
        f = TargetFact("test", None, {}, 0.9, "test")
        self.assertTrue(f.is_global)

    def test_regional_fact(self):
        f = TargetFact("test", (10, 20), {}, 0.9, "test")
        self.assertFalse(f.is_global)


class TestTargetFacts(unittest.TestCase):
    def test_add_and_query(self):
        tf = TargetFacts()
        tf.add(TargetFact("mismatch_class", (10, 20), {"class": "opcode"}, 0.9, "test"))
        tf.add(TargetFact("register_pressure", None, {"gpr_delta": 1}, 0.85, "test"))
        self.assertEqual(len(tf.facts), 2)

    def test_by_kind(self):
        tf = TargetFacts()
        tf.add(TargetFact("mismatch_class", (10, 20), {}, 0.9, "a"))
        tf.add(TargetFact("register_pressure", None, {}, 0.8, "b"))
        tf.add(TargetFact("mismatch_class", (30, 40), {}, 0.7, "c"))
        results = tf.by_kind("mismatch_class")
        self.assertEqual(len(results), 2)

    def test_for_region_overlapping(self):
        tf = TargetFacts()
        tf.add(TargetFact("mismatch_class", (10, 20), {}, 0.9, "a"))
        tf.add(TargetFact("mismatch_class", (30, 40), {}, 0.7, "b"))
        tf.add(TargetFact("register_pressure", None, {}, 0.8, "c"))  # global
        results = tf.for_region(15, 25)
        self.assertEqual(len(results), 2)  # region (10,20) + global

    def test_for_region_non_overlapping(self):
        tf = TargetFacts()
        tf.add(TargetFact("mismatch_class", (10, 20), {}, 0.9, "a"))
        results = tf.for_region(50, 60)
        self.assertEqual(len(results), 0)

    def test_high_confidence(self):
        tf = TargetFacts()
        tf.add(TargetFact("a", None, {}, 0.9, "x"))
        tf.add(TargetFact("b", None, {}, 0.5, "y"))
        tf.add(TargetFact("c", None, {}, 0.8, "z"))
        results = tf.high_confidence(0.7)
        self.assertEqual(len(results), 2)

    def test_has_no_touch(self):
        tf = TargetFacts()
        tf.add(TargetFact("no_touch_zone", (10, 20), {}, 0.9, "atlas.volatile_regswap"))
        self.assertTrue(tf.has_no_touch(15, 18))
        self.assertFalse(tf.has_no_touch(25, 30))

    def test_pattern_recommendations(self):
        tf = TargetFacts()
        tf.add(TargetFact("mismatch_class", None, {
            "boost_patterns": ["loop_condition_subtract"],
        }, 0.9, "a"))
        tf.add(TargetFact("mismatch_class", None, {
            "boost_patterns": ["signed_unsigned"],
        }, 0.8, "b"))
        boost, suppress = tf.pattern_recommendations()
        self.assertIn("loop_condition_subtract", boost)
        self.assertIn("signed_unsigned", boost)

    def test_summary_lines_include_shapes_and_routing(self):
        tf = TargetFacts()
        tf.add(TargetFact("codegen_shape", None, {
            "shape_kind": "byte_fusion",
            "shape_category": "separate_shift_and_mask",
            "boost_patterns": ["u8_to_unsigned_long"],
        }, 0.8, "ppc_shape.byte_fusion.separate_shift_and_mask"))
        tf.add(TargetFact("codegen_shape", None, {
            "shape_kind": "bool_materialization",
            "shape_category": "signed_positive",
            "boost_patterns": ["bool_materialize", "signed_unsigned"],
        }, 0.95, "ppc_shape.bool_materialization.signed_positive"))
        tf.add(TargetFact("mismatch_class", None, {
            "boost_patterns": ["comparison_flip"],
        }, 0.9, "atlas.example"))

        lines = tf.summary_lines()
        joined = "\n".join(lines)
        self.assertIn("Target facts:", joined)
        self.assertIn("Codegen shapes:", joined)
        self.assertIn("separate_shift_and_mask", joined)
        self.assertIn("signed_positive", joined)
        self.assertIn("Pattern boosts:", joined)
        self.assertIn("u8_to_unsigned_long", joined)


class TestExtractFromDiagnosis(unittest.TestCase):
    def test_prologue_mismatch(self):
        diag = _make_diagnosis(has_prologue_mismatch=True, gpr_save_delta=2)
        facts = extract_from_diagnosis(diag)
        reg_facts = [f for f in facts if f.kind == "register_pressure"]
        self.assertGreater(len(reg_facts), 0)
        self.assertEqual(reg_facts[0].payload["gpr_save_delta"], 2)

    def test_noise_ratio(self):
        diag = _make_diagnosis(noise_total=20, noise_explained=18)
        facts = extract_from_diagnosis(diag)
        noise_facts = [f for f in facts if f.payload.get("class") == "mostly_noise"]
        self.assertEqual(len(noise_facts), 1)

    def test_regswap_pairs(self):
        diag = _make_diagnosis(reg_swap_pairs=[("r28", "r29"), ("r3", "r4")])
        facts = extract_from_diagnosis(diag)
        reg_facts = [f for f in facts if f.kind == "register_pressure"
                     and "swap" in f.payload]
        self.assertEqual(len(reg_facts), 2)
        # r28/r29 should be callee-saved (fixable)
        callee = [f for f in reg_facts if f.payload["callee_saved"]]
        self.assertEqual(len(callee), 1)

    def test_regions(self):
        diag = _make_diagnosis()
        regions = [
            _make_region(100, 110, "opcode", 5, 20, 0.75),
            _make_region(200, 205, "register", 2, 8, 0.75),
        ]
        facts = extract_from_diagnosis(diag, regions)
        region_facts = [f for f in facts if f.kind == "mismatch_class" and f.region]
        self.assertEqual(len(region_facts), 2)
        self.assertEqual(region_facts[0].region, (100, 110))

    def test_none_diagnosis(self):
        facts = extract_from_diagnosis(None)
        self.assertEqual(len(facts), 0)


class TestExtractFromAtlas(unittest.TestCase):
    def test_fixable_entry(self):
        entries = [_make_atlas_entry("test", ("subf.",), fixable=True,
                                     pattern_names=("loop_condition_subtract",))]
        facts = extract_from_atlas(entries)
        self.assertEqual(len(facts), 1)
        self.assertEqual(facts[0].kind, "mismatch_class")
        self.assertIn("loop_condition_subtract", facts[0].payload["boost_patterns"])

    def test_negative_entry(self):
        entries = [_make_atlas_entry("volatile", ("mr",), fixable=False,
                                     confidence_value="negative")]
        facts = extract_from_atlas(entries)
        self.assertEqual(len(facts), 1)
        self.assertEqual(facts[0].kind, "no_touch_zone")

    def test_confidence_mapping(self):
        proven = _make_atlas_entry("p", ("x",), confidence_value="proven")
        inferred = _make_atlas_entry("i", ("y",), confidence_value="inferred")
        negative = _make_atlas_entry("n", ("z",), fixable=False,
                                      confidence_value="negative")
        facts = extract_from_atlas([proven, inferred, negative])
        self.assertAlmostEqual(facts[0].confidence, 0.95)
        self.assertAlmostEqual(facts[1].confidence, 0.7)
        self.assertAlmostEqual(facts[2].confidence, 0.9)


class TestExtractFromShapeFacts(unittest.TestCase):
    def test_separate_byte_fusion_no_action_when_target_also_separate(self):
        """When both base and target use separate shift+mask, no boost/suppress needed."""
        diag = _make_diagnosis()
        diag.diff_ops = [MagicMock(target_opcode="srwi"), MagicMock(target_opcode="clrlwi")]
        facts = extract_from_shape_facts([
            {"kind": "byte_fusion", "category": "separate_shift_and_mask", "confidence": 0.8},
        ], diagnosis=diag)
        self.assertEqual(len(facts), 1)
        self.assertEqual(facts[0].kind, "codegen_shape")
        # Neither boost nor suppress — base and target agree
        self.assertNotIn("boost_patterns", facts[0].payload)
        self.assertNotIn("suppress_patterns", facts[0].payload)

    def test_fused_byte_fusion_suppresses_widening_pattern(self):
        diag = _make_diagnosis()
        diag.diff_ops = [MagicMock(target_opcode="extrwi")]
        facts = extract_from_shape_facts([
            {"kind": "byte_fusion", "category": "fused_shr_mask", "confidence": 0.95},
        ], diagnosis=diag)
        self.assertEqual(len(facts), 1)
        self.assertIn("u8_to_unsigned_long", facts[0].payload["suppress_patterns"])

    def test_fused_byte_fusion_boosts_when_target_wants_separate(self):
        diag = _make_diagnosis()
        diag.diff_ops = [MagicMock(target_opcode="srwi"), MagicMock(target_opcode="clrlwi")]
        facts = extract_from_shape_facts([
            {"kind": "byte_fusion", "category": "fused_shr_mask", "confidence": 0.95},
        ], diagnosis=diag)
        self.assertEqual(len(facts), 1)
        self.assertIn("u8_to_unsigned_long", facts[0].payload["boost_patterns"])

    def test_bool_materialization_boosts_bool_patterns(self):
        diag = _make_diagnosis()
        diag.diff_ops = [MagicMock(target_opcode="subfe"), MagicMock(target_opcode="addic")]
        facts = extract_from_shape_facts([
            {"kind": "bool_materialization", "category": "signed_positive", "confidence": 0.95},
        ], diagnosis=diag)
        self.assertEqual(len(facts), 1)
        self.assertIn("bool_materialize", facts[0].payload["boost_patterns"])
        self.assertIn("signed_unsigned", facts[0].payload["boost_patterns"])

    def test_bool_materialization_without_target_signal_is_descriptive_only(self):
        facts = extract_from_shape_facts([
            {"kind": "bool_materialization", "category": "signed_positive", "confidence": 0.95},
        ], diagnosis=_make_diagnosis())
        self.assertEqual(len(facts), 1)
        self.assertNotIn("boost_patterns", facts[0].payload)

    def test_switch_if_chain_boosts_switch_conversion_when_target_wants_dispatch(self):
        diag = _make_diagnosis()
        diag.diff_ops = [
            MagicMock(target_opcode="mtctr"),
            MagicMock(target_opcode="bdzne"),
        ]
        facts = extract_from_shape_facts([
            {"kind": "switch_dispatch", "category": "switch_if_chain", "confidence": 0.72},
        ], diagnosis=diag)
        self.assertEqual(len(facts), 1)
        self.assertIn("switch_if_convert", facts[0].payload["boost_patterns"])

    def test_switch_if_chain_suppresses_without_switch_markers(self):
        """When target has no switch table markers, suppress conversion."""
        facts = extract_from_shape_facts([
            {"kind": "switch_dispatch", "category": "switch_if_chain", "confidence": 0.72},
        ], diagnosis=_make_diagnosis())
        self.assertEqual(len(facts), 1)
        self.assertIn("switch_if_convert", facts[0].payload["suppress_patterns"])

    def test_switch_table_boosts_conversion_when_target_uses_compare_chain(self):
        """When base uses switch table but target uses compare chain, boost conversion."""
        diag = _make_diagnosis()
        diag.diff_ops = [
            MagicMock(target_opcode="cmpwi"),
            MagicMock(target_opcode="beq"),
        ]
        facts = extract_from_shape_facts([
            {"kind": "switch_dispatch", "category": "switch_table", "confidence": 0.88},
        ], diagnosis=diag)
        self.assertEqual(len(facts), 1)
        self.assertIn("switch_if_convert", facts[0].payload["boost_patterns"])

    def test_switch_table_suppresses_when_no_compare_markers(self):
        """When base has switch table and target has no compare markers, suppress."""
        facts = extract_from_shape_facts([
            {"kind": "switch_dispatch", "category": "switch_table", "confidence": 0.88},
        ], diagnosis=_make_diagnosis())
        self.assertEqual(len(facts), 1)
        self.assertIn("switch_if_convert", facts[0].payload["suppress_patterns"])

    def test_switch_ctr_chain_suppresses_switch_conversion_when_target_matches(self):
        diag = _make_diagnosis()
        diag.diff_ops = [
            MagicMock(target_opcode="mtctr"),
            MagicMock(target_opcode="bdzne"),
        ]
        facts = extract_from_shape_facts([
            {"kind": "switch_dispatch", "category": "switch_ctr_chain", "confidence": 0.88},
        ], diagnosis=diag)
        self.assertEqual(len(facts), 1)
        self.assertIn("switch_if_convert", facts[0].payload["suppress_patterns"])

    def test_tail_direct_call_suppresses_tail_call_reorder(self):
        diag = _make_diagnosis()
        facts = extract_from_shape_facts([
            {"kind": "call_shape", "category": "tail_direct_call", "confidence": 0.95},
        ], diagnosis=diag)
        self.assertEqual(len(facts), 1)
        self.assertIn("tail_call_reorder", facts[0].payload["suppress_patterns"])

    def test_call_sequence_return_boosts_tail_call_reorder_when_target_prefers_tail(self):
        diag = _make_diagnosis(has_prologue_mismatch=True, gpr_save_delta=-1)
        diag.diff_ops = [MagicMock(target_opcode="b", base_opcode="bl")]
        facts = extract_from_shape_facts([
            {"kind": "call_shape", "category": "call_sequence_return", "confidence": 0.8},
        ], diagnosis=diag)
        self.assertEqual(len(facts), 1)
        self.assertIn("tail_call_reorder", facts[0].payload["boost_patterns"])

    def test_call_sequence_return_boosts_tail_call_reorder_by_default(self):
        facts = extract_from_shape_facts([
            {"kind": "call_shape", "category": "call_sequence_return", "confidence": 0.8},
        ], diagnosis=_make_diagnosis())
        self.assertEqual(len(facts), 1)
        self.assertIn("tail_call_reorder", facts[0].payload["boost_patterns"])

    def test_cached_return_value_boosts_temp_elimination(self):
        diag = _make_diagnosis(has_prologue_mismatch=True, gpr_save_delta=-1)
        diag.diff_ops = [MagicMock(target_opcode="b", base_opcode="bl")]
        facts = extract_from_shape_facts([
            {"kind": "call_shape", "category": "cached_return_value", "confidence": 0.9},
        ], diagnosis=diag)
        self.assertEqual(len(facts), 1)
        self.assertIn("temp_elimination", facts[0].payload["boost_patterns"])
        self.assertIn("tail_call_reorder", facts[0].payload["boost_patterns"])

    def test_cached_return_value_boosts_temp_elimination_by_default(self):
        facts = extract_from_shape_facts([
            {"kind": "call_shape", "category": "cached_return_value", "confidence": 0.9},
        ], diagnosis=_make_diagnosis())
        self.assertEqual(len(facts), 1)
        self.assertIn("temp_elimination", facts[0].payload["boost_patterns"])
        self.assertIn("tail_call_reorder", facts[0].payload["boost_patterns"])

    def test_direct_call_return_suppresses_tail_reorder_when_target_wants_non_tail(self):
        diag = _make_diagnosis(has_prologue_mismatch=True, gpr_save_delta=1)
        diag.diff_ops = [MagicMock(target_opcode="bl", base_opcode="b")]
        facts = extract_from_shape_facts([
            {"kind": "call_shape", "category": "direct_call_return", "confidence": 0.85},
        ], diagnosis=diag)
        self.assertEqual(len(facts), 1)
        self.assertIn("tail_call_reorder", facts[0].payload["suppress_patterns"])

    def test_virtual_dispatch_fact(self):
        facts = extract_from_shape_facts([
            {"kind": "virtual_dispatch", "category": "vtable_call", "confidence": 0.9},
        ], diagnosis=_make_diagnosis())
        self.assertEqual(len(facts), 1)
        self.assertTrue(facts[0].payload.get("virtual_call"))

    def test_prologue_shape_high_gpr_boosts_variable_extraction(self):
        facts = extract_from_shape_facts([
            {"kind": "prologue_shape", "category": "register_save", "confidence": 0.95,
             "callee_saved_gprs": 12, "callee_saved_fprs": 0, "stack_frame_size": 112},
        ], diagnosis=_make_diagnosis())
        self.assertEqual(len(facts), 1)
        self.assertEqual(facts[0].payload["callee_saved_gprs"], 12)
        self.assertIn("variable_extraction", facts[0].payload.get("boost_patterns", []))

    def test_prologue_shape_with_fprs_boosts_signed_unsigned(self):
        facts = extract_from_shape_facts([
            {"kind": "prologue_shape", "category": "register_save", "confidence": 0.95,
             "callee_saved_gprs": 3, "callee_saved_fprs": 5, "stack_frame_size": 80},
        ], diagnosis=_make_diagnosis())
        self.assertEqual(len(facts), 1)
        self.assertIn("signed_unsigned", facts[0].payload.get("boost_patterns", []))

    def test_cfg_complexity_fact(self):
        facts = extract_from_shape_facts([
            {"kind": "control_flow", "category": "cfg_complexity", "confidence": 0.85,
             "block_count": 10, "loop_count": 2, "nesting_depth": 1},
        ], diagnosis=_make_diagnosis())
        self.assertEqual(len(facts), 1)
        self.assertEqual(facts[0].payload.get("block_count"), 10)
        self.assertEqual(facts[0].payload.get("loop_count"), 2)

    def test_counted_loop_boosts_foreach(self):
        facts = extract_from_shape_facts([
            {"kind": "control_flow", "category": "counted_loop", "confidence": 0.95},
        ], diagnosis=_make_diagnosis())
        self.assertEqual(len(facts), 1)
        self.assertTrue(facts[0].payload.get("counted_loop"))
        self.assertIn("foreach_to_dowhile", facts[0].payload.get("boost_patterns", []))

    def test_fma_fact(self):
        facts = extract_from_shape_facts([
            {"kind": "float_fusion", "category": "fused_multiply_add", "confidence": 0.95,
             "count": 3},
        ], diagnosis=_make_diagnosis())
        self.assertEqual(len(facts), 1)
        self.assertEqual(facts[0].payload.get("fma_count"), 3)

    def test_operation_profile_fact(self):
        facts = extract_from_shape_facts([
            {"kind": "operation_profile", "category": "aggregate", "confidence": 1.0,
             "total_ops": 50, "direct_calls": 3, "indirect_calls": 2, "float_ops": 5},
        ], diagnosis=_make_diagnosis())
        self.assertEqual(len(facts), 1)
        self.assertEqual(facts[0].payload.get("total_ops"), 50)
        self.assertEqual(facts[0].payload.get("indirect_calls"), 2)


class TestExtractFacts(unittest.TestCase):
    def test_combines_all_sources(self):
        diag = _make_diagnosis(has_prologue_mismatch=True, gpr_save_delta=1)
        regions = [_make_region(50, 60)]
        atlas = [_make_atlas_entry("test", ("subf.",),
                                    pattern_names=("loop_condition_subtract",))]
        shape_facts = [{"kind": "byte_fusion", "category": "separate_shift_and_mask", "confidence": 0.8}]
        diag.diff_ops = [MagicMock(target_opcode="srwi"), MagicMock(target_opcode="clrlwi")]
        facts = extract_facts(
            diagnosis=diag,
            regions=regions,
            atlas_entries=atlas,
            shape_facts=shape_facts,
        )
        self.assertGreater(len(facts.facts), 2)
        # Should have diagnosis facts + region facts + atlas facts
        kinds = {f.kind for f in facts.facts}
        self.assertIn("register_pressure", kinds)
        self.assertIn("mismatch_class", kinds)
        self.assertIn("codegen_shape", kinds)

    def test_empty_sources(self):
        facts = extract_facts()
        self.assertEqual(len(facts.facts), 0)


if __name__ == "__main__":
    unittest.main()
