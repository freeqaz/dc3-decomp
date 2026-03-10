"""Pattern relevance, null guard, reparse, and budget allocation tests."""

from __future__ import annotations

import sys
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.types import SwapInfo
from scripts.permuter.tests.conftest import (
    _empty_diag,
    diag_with_arith_ops,
    diag_with_branch_and_clusters,
    diag_with_branch_ops,
    diag_with_callee_saved_swaps,
    diag_with_clusters,
    diag_with_cmp_ops,
    diag_with_divw,
    diag_with_divw_base,
    diag_with_fma_addsub_ops,
    diag_with_fma_ops,
    diag_with_gpr_swaps,
    diag_with_lwz_ops,
    diag_with_prologue_fewer_saves,
    diag_with_prologue_more_saves,
    diag_with_subf_cmpw,
    make_context,
    make_ghidra_context,
)
from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.extractor import reparse_variant

class TestPatternRelevance(unittest.TestCase):
    """Verify relevant() returns correct results for various diagnoses."""

    def test_ternary_swap_relevant_with_branches(self):
        p = get_pattern("ternary_swap")
        self.assertTrue(p.relevant(diag_with_branch_ops()))

    def test_ternary_swap_relevant_with_clusters(self):
        p = get_pattern("ternary_swap")
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_ternary_swap_irrelevant_empty(self):
        p = get_pattern("ternary_swap")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_empty_size_relevant_divw_target(self):
        p = get_pattern("empty_size_swap")
        self.assertTrue(p.relevant(diag_with_divw()))

    def test_empty_size_relevant_divw_base(self):
        p = get_pattern("empty_size_swap")
        self.assertTrue(p.relevant(diag_with_divw_base()))

    def test_empty_size_irrelevant_empty(self):
        p = get_pattern("empty_size_swap")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_commutative_relevant_arith(self):
        p = get_pattern("commutative_swap")
        self.assertTrue(p.relevant(diag_with_arith_ops()))

    def test_commutative_irrelevant_empty(self):
        p = get_pattern("commutative_swap")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_variable_extraction_relevant_with_clusters(self):
        p = get_pattern("variable_extraction")
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_variable_extraction_irrelevant_empty(self):
        p = get_pattern("variable_extraction")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_declaration_reorder_relevant_gpr(self):
        p = get_pattern("declaration_reorder")
        self.assertTrue(p.relevant(diag_with_gpr_swaps()))

    def test_declaration_reorder_irrelevant_empty(self):
        p = get_pattern("declaration_reorder")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_fma_relevant_fma_ops(self):
        p = get_pattern("fma_reorder")
        self.assertTrue(p.relevant(diag_with_fma_ops()))

    def test_fma_relevant_addsub_ops(self):
        p = get_pattern("fma_reorder")
        self.assertTrue(p.relevant(diag_with_fma_addsub_ops()))

    def test_fma_irrelevant_empty(self):
        p = get_pattern("fma_reorder")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_comparison_flip_relevant_cmp(self):
        p = get_pattern("comparison_flip")
        self.assertTrue(p.relevant(diag_with_cmp_ops()))

    def test_comparison_flip_irrelevant_empty(self):
        p = get_pattern("comparison_flip")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_comparison_equivalence_relevant_cmp(self):
        p = get_pattern("comparison_equivalence")
        self.assertTrue(p.relevant(diag_with_cmp_ops()))

    def test_comparison_equivalence_irrelevant_empty(self):
        p = get_pattern("comparison_equivalence")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_branch_polarity_relevant_branches(self):
        p = get_pattern("branch_polarity")
        self.assertTrue(p.relevant(diag_with_branch_ops()))

    def test_branch_polarity_irrelevant_empty(self):
        p = get_pattern("branch_polarity")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_signed_unsigned_relevant_cmp(self):
        p = get_pattern("signed_unsigned")
        self.assertTrue(p.relevant(diag_with_cmp_ops()))

    def test_signed_unsigned_irrelevant_empty(self):
        p = get_pattern("signed_unsigned")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_inline_assignment_relevant_clusters(self):
        p = get_pattern("inline_assignment")
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_inline_assignment_irrelevant_empty(self):
        p = get_pattern("inline_assignment")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_argument_swap_relevant_clusters(self):
        p = get_pattern("argument_swap")
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_argument_swap_irrelevant_empty(self):
        p = get_pattern("argument_swap")
        self.assertFalse(p.relevant(_empty_diag()))

    # reference_elimination
    def test_reference_elimination_relevant_callee_saved(self):
        p = get_pattern("reference_elimination")
        self.assertTrue(p.relevant(diag_with_callee_saved_swaps()))

    def test_reference_elimination_relevant_lwz(self):
        p = get_pattern("reference_elimination")
        self.assertTrue(p.relevant(diag_with_lwz_ops()))

    def test_reference_elimination_relevant_clusters(self):
        p = get_pattern("reference_elimination")
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_reference_elimination_relevant_prologue_fewer(self):
        p = get_pattern("reference_elimination")
        self.assertTrue(p.relevant(diag_with_prologue_fewer_saves()))

    def test_reference_elimination_irrelevant_empty(self):
        p = get_pattern("reference_elimination")
        self.assertFalse(p.relevant(_empty_diag()))

    # subscript_ref_bind
    def test_subscript_ref_bind_relevant_callee_saved(self):
        p = get_pattern("subscript_ref_bind")
        self.assertTrue(p.relevant(diag_with_callee_saved_swaps()))

    def test_subscript_ref_bind_relevant_lwz(self):
        p = get_pattern("subscript_ref_bind")
        self.assertTrue(p.relevant(diag_with_lwz_ops()))

    def test_subscript_ref_bind_relevant_clusters(self):
        p = get_pattern("subscript_ref_bind")
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_subscript_ref_bind_relevant_prologue_more(self):
        p = get_pattern("subscript_ref_bind")
        self.assertTrue(p.relevant(diag_with_prologue_more_saves()))

    def test_subscript_ref_bind_irrelevant_empty(self):
        p = get_pattern("subscript_ref_bind")
        self.assertFalse(p.relevant(_empty_diag()))

    # null_guard_elimination
    def test_null_guard_relevant_branch_ops(self):
        p = get_pattern("null_guard_elimination")
        self.assertTrue(p.relevant(diag_with_branch_ops()))

    def test_null_guard_relevant_clusters(self):
        p = get_pattern("null_guard_elimination")
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_null_guard_relevant_cmp_ops(self):
        p = get_pattern("null_guard_elimination")
        self.assertTrue(p.relevant(diag_with_cmp_ops()))

    def test_null_guard_irrelevant_empty(self):
        p = get_pattern("null_guard_elimination")
        self.assertFalse(p.relevant(_empty_diag()))

    # type_width_change
    def test_type_width_change_relevant_cmp(self):
        p = get_pattern("type_width_change")
        self.assertTrue(p.relevant(diag_with_cmp_ops()))

    def test_type_width_change_irrelevant_empty(self):
        p = get_pattern("type_width_change")
        self.assertFalse(p.relevant(_empty_diag()))

    # empty_size_swap with mulli
    def test_empty_size_relevant_mulli(self):
        """empty_size_swap should fire on mulli (size computation without divw)."""
        from scripts.permuter.types import DiffOp
        p = get_pattern("empty_size_swap")
        d = _empty_diag()
        d.diff_ops = [DiffOp(index=5, target_opcode="mulli", base_opcode="cmplw")]
        self.assertTrue(p.relevant(d))


class TestTernarySwapGenerationGuard(unittest.TestCase):
    """Test that ternary_swap generates nothing on bodies without swappable constructs."""

    def test_ternary_swap_no_variants_without_swappable(self):
        """ternary_swap generates nothing when body has no if/else or ternary."""
        source = "void test_func(int x) {\n    x++;\n    x *= 2;\n}\n"
        ctx = make_context(source, "test_func", diag_with_branch_and_clusters())
        p = get_pattern("ternary_swap")
        variants = list(p.generate(ctx))
        self.assertEqual(len(variants), 0)

    def test_ternary_swap_generates_with_if_else(self):
        """ternary_swap generates variants when body has if/else."""
        source = """\
void test_func(int cond) {
    int x;
    if (cond) {
        x = 1;
    } else {
        x = 2;
    }
}
"""
        ctx = make_context(source, "test_func", diag_with_branch_and_clusters())
        p = get_pattern("ternary_swap")
        variants = list(p.generate(ctx))
        self.assertGreater(len(variants), 0)


class TestNullGuardGhidraGuided(unittest.TestCase):
    """Test Ghidra-guided null guard elimination."""

    def test_null_guard_ghidra_guided_removes_absent(self):
        """Ghidra shows no null check -> remove guard."""
        source = '''
void test_func() {
    if (TheMetaMusic)
        TheMetaMusic->AddFader(fader);
}
'''
        # Ghidra code has no null check
        ghidra_code = '''
void test_func(void) {
    FUN_12345678(TheMetaMusic, fader);
}
'''
        ctx = make_ghidra_context(source, "test_func", diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("null_guard_elimination")
        variants = list(pattern.generate(ctx))
        self.assertGreater(len(variants), 0)
        self.assertTrue(any("ghidra" in v.name for v in variants))
        # Verify the guard was removed
        self.assertTrue(any(
            b"TheMetaMusic->AddFader(fader);" in v.source and
            b"if (TheMetaMusic)" not in v.source for v in variants
        ))

    def test_null_guard_ghidra_keeps_present(self):
        """Ghidra shows null check present -> don't remove."""
        source = '''
void test_func() {
    if (TheMetaMusic)
        TheMetaMusic->AddFader(fader);
}
'''
        # Ghidra code ALSO has null check
        ghidra_code = '''
void test_func(void) {
    if (TheMetaMusic != (MetaMusic *)0x0) {
        FUN_12345678(TheMetaMusic, fader);
    }
}
'''
        ctx = make_ghidra_context(source, "test_func", diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("null_guard_elimination")
        variants = list(pattern.generate(ctx))
        # Should produce no ghidra-guided variants (guard exists in target too)
        ghidra_variants = [v for v in variants if "ghidra" in v.name]
        self.assertEqual(len(ghidra_variants), 0)

    def test_null_guard_ghidra_and_operand_removes_absent(self):
        """Ghidra has no null check in && -> drop the leading operand.

        The safety check in _drop_leading_and_operand requires the kept side
        to reference the dropped guard (e.g. TheMetaMusic && TheMetaMusic->X()).
        When the sides are unrelated (TheMetaMusic && sHamMaster), the drop is
        unsafe and correctly refused — the kept side must reference the guard.
        """
        source = '''
void test_func(int sHamMaster) {
    if (TheMetaMusic && TheMetaMusic->IsActive()) {
        DoSomething();
    }
}
'''
        # Ghidra just calls directly, no null check on TheMetaMusic
        ghidra_code = '''
void test_func(int param_1) {
    if (FUN_AABBCCDD() != 0) {
        FUN_12345678();
    }
}
'''
        ctx = make_ghidra_context(source, "test_func", diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("null_guard_elimination")
        variants = list(pattern.generate(ctx))
        ghidra_variants = [v for v in variants if "ghidra" in v.name]
        self.assertGreater(len(ghidra_variants), 0)

    def test_null_guard_ghidra_implicit_check_keeps(self):
        """Ghidra has implicit null check (if (var)) -> don't remove."""
        source = '''
void test_func() {
    if (TheMetaMusic)
        TheMetaMusic->Stop();
}
'''
        # Ghidra also has the check implicitly
        ghidra_code = '''
void test_func(void) {
    if (TheMetaMusic) {
        FUN_12345678(TheMetaMusic);
    }
}
'''
        ctx = make_ghidra_context(source, "test_func", diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("null_guard_elimination")
        variants = list(pattern.generate(ctx))
        ghidra_variants = [v for v in variants if "ghidra" in v.name]
        self.assertEqual(len(ghidra_variants), 0)

    def test_null_guard_ghidra_falls_back_to_blind(self):
        """When Ghidra produces no candidates, fall through to blind mode."""
        source = '''
void test_func() {
    if (TheMetaMusic)
        TheMetaMusic->Stop();
}
'''
        # Ghidra also has the null check -> no ghidra candidates -> blind mode runs
        ghidra_code = '''
void test_func(void) {
    if (TheMetaMusic != (MetaMusic *)0x0) {
        FUN_12345678(TheMetaMusic);
    }
}
'''
        ctx = make_ghidra_context(source, "test_func", diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("null_guard_elimination")
        variants = list(pattern.generate(ctx))
        # Blind mode should still produce variants (non-ghidra)
        blind_variants = [v for v in variants if "ghidra" not in v.name]
        self.assertGreater(len(blind_variants), 0)


# ---------------------------------------------------------------------------
# Negative tests (patterns should NOT produce certain variants)
# ---------------------------------------------------------------------------


class TestMultiUseTempSafety(unittest.TestCase):
    """Verify multi-use value temp elimination skips call initializers."""

    def test_skip_call_initializer(self):
        """Multi-use temp with call init should NOT be eliminated (re-evaluates side effects)."""
        source = """\
int GetCount();
int test_func(int y) {
    int x = GetCount();
    int a = x + y;
    int b = x - y;
    return a + b;
}
"""
        ctx = make_context(source, "test_func", diag_with_callee_saved_swaps())
        p = get_pattern("temp_elimination")
        variants = list(p.generate(ctx))

        # No variant should substitute GetCount() at multiple use sites
        bad_pattern = "GetCount() + y"
        for v in variants:
            v_text = v.source.decode("utf-8", errors="replace")
            self.assertNotIn(
                bad_pattern, v_text,
                f"Variant '{v.name}' unsafely inlined call at multiple sites: {v.description}"
            )

    def test_skip_intervening_side_effects(self):
        """Multi-use temp should not be eliminated if calls exist between decl and use."""
        source = """\
void sideEffect();
int test_func(int mFoo, int y) {
    int x = mFoo;
    sideEffect();
    int a = x + y;
    int b = x - y;
    return a + b;
}
"""
        ctx = make_context(source, "test_func", diag_with_callee_saved_swaps())
        p = get_pattern("temp_elimination")
        variants = list(p.generate(ctx))

        # No variant should eliminate x when sideEffect() sits between decl and use
        for v in variants:
            v_text = v.source.decode("utf-8", errors="replace")
            # If x was eliminated, mFoo would appear where x was used
            if "mFoo + y" in v_text and "int x" not in v_text:
                self.fail(
                    f"Variant '{v.name}' eliminated temp past side-effecting call: {v.description}"
                )



# ---------------------------------------------------------------------------
# Reparse variant tests
# ---------------------------------------------------------------------------

class TestReparseVariant(unittest.TestCase):
    """Tests for reparse_variant() — re-parsing modified source."""

    def test_reparse_preserves_function(self):
        """Reparse finds the same function in modified source."""
        source = """\
void test_func() {
    int a = 1;
    int b = 2;
}
"""
        ctx = make_context(source, "test_func", _empty_diag())

        # Modify source: change 'int a = 1' to 'int a = 42'
        new_source = ctx.file_source.replace(b"int a = 1", b"int a = 42")
        reparsed = reparse_variant(ctx, new_source)

        self.assertEqual(reparsed.file_source, new_source)
        self.assertEqual(len(reparsed.statements), 2)
        # Verify the function node text contains the modification
        func_text = new_source[reparsed.func_byte_range[0]:reparsed.func_byte_range[1]]
        self.assertIn(b"int a = 42", func_text)

    def test_reparse_preserves_diagnosis(self):
        """Diagnosis from original ctx is carried forward."""
        source = """\
void test_func() {
    int x = 1;
}
"""
        diag = diag_with_cmp_ops()
        ctx = make_context(source, "test_func", diag)

        new_source = ctx.file_source.replace(b"int x = 1", b"int x = 2")
        reparsed = reparse_variant(ctx, new_source)

        self.assertIs(reparsed.diagnosis, diag)

    def test_reparse_raises_on_missing_function(self):
        """ValueError if function name disappears from source."""
        source = """\
void test_func() {
    int x = 1;
}
"""
        ctx = make_context(source, "test_func", _empty_diag())

        # Replace function name with something else
        new_source = ctx.file_source.replace(b"test_func", b"other_func")
        with self.assertRaises(ValueError):
            reparse_variant(ctx, new_source)



# ---------------------------------------------------------------------------
# Budget allocation tests
# ---------------------------------------------------------------------------

class TestBudgetAllocation(unittest.TestCase):
    """Tests for allocate_budgets() in generator.py."""

    def test_minimum_budget_per_relevant_pattern(self):
        """Every relevant pattern gets at least _MIN_BUDGET."""
        from scripts.permuter.generator import allocate_budgets, _MIN_BUDGET

        # Use patterns that are relevant for the given diagnosis
        patterns = [get_pattern("variable_extraction"), get_pattern("signed_unsigned")]
        diag = diag_with_cmp_ops()  # signed_unsigned relevant, variable_extraction always relevant
        budgets = allocate_budgets(patterns, 100, diag)

        for p in patterns:
            self.assertGreaterEqual(
                budgets.get(p.name, 0), _MIN_BUDGET,
                f"Pattern '{p.name}' budget {budgets.get(p.name, 0)} < {_MIN_BUDGET}",
            )

    def test_irrelevant_patterns_get_zero(self):
        """Patterns where relevant() is False get 0 budget."""
        from scripts.permuter.generator import allocate_budgets

        # empty_size_swap requires divw in diff_ops; empty diag has none
        # variable_extraction also requires some mismatch signal
        patterns = [get_pattern("empty_size_swap"), get_pattern("variable_extraction")]
        diag = _empty_diag()
        budgets = allocate_budgets(patterns, 100, diag)

        self.assertEqual(budgets.get("empty_size_swap", 0), 0)
        self.assertEqual(budgets.get("variable_extraction", 0), 0)

    def test_total_does_not_exceed_budget(self):
        """Sum of allocated budgets <= total_budget."""
        from scripts.permuter.generator import allocate_budgets

        patterns = [
            get_pattern("variable_extraction"),
            get_pattern("signed_unsigned"),
            get_pattern("comparison_flip"),
            get_pattern("declaration_reorder"),
        ]
        # Use a diagnosis where all patterns are relevant
        diag = diag_with_cmp_ops()
        diag.reg_swap_pairs = {("r20", "r21"): SwapInfo(count=4, first_idx=10, last_idx=50)}

        for total in [10, 50, 100, 200]:
            budgets = allocate_budgets(patterns, total, diag)
            allocated = sum(budgets.values())
            self.assertLessEqual(
                allocated, total,
                f"Allocated {allocated} > budget {total}: {budgets}",
            )

    def test_no_diagnosis_all_relevant(self):
        """When diagnosis is None, all patterns are relevant."""
        from scripts.permuter.generator import allocate_budgets

        patterns = [get_pattern("variable_extraction"), get_pattern("empty_size_swap")]
        budgets = allocate_budgets(patterns, 50, None)

        for p in patterns:
            self.assertGreater(
                budgets.get(p.name, 0), 0,
                f"Pattern '{p.name}' should be relevant when diagnosis is None",
            )



# ---------------------------------------------------------------------------
# Diagnosis noise classification tests (from real-world permuter runs)
# ---------------------------------------------------------------------------

class TestDiagnosisNoise(unittest.TestCase):
    """Tests for diagnosis noise classification, including address relocation heuristic."""

    def test_addr_reloc_lis_addi_counted_as_noise(self):
        """lis/addi pairs without diff_breakdown should be classified as noise."""
        from scripts.permuter.diagnosis import diagnose_baseline, is_all_noise

        # Simulate objdiff JSON with lis/addi diff_arg (no diff_breakdown)
        instrs = []
        # 90 equal instructions
        for i in range(90):
            instrs.append({"index": i, "match_type": "equal", "target": {"opcode": "mr"}, "base": {"opcode": "mr"}})
        # 5 lis/addi pairs as diff_arg with no diff_breakdown (address relocation noise)
        for i in range(5):
            instrs.append({"index": 90 + i * 2, "match_type": "diff_arg",
                           "target": {"opcode": "lis"}, "base": {"opcode": "lis"}})
            instrs.append({"index": 91 + i * 2, "match_type": "diff_arg",
                           "target": {"opcode": "addi"}, "base": {"opcode": "addi"}})

        objdiff_json = {"instructions": instrs}
        diag = diagnose_baseline(objdiff_json)

        self.assertEqual(diag.noise_total, 10)
        self.assertEqual(diag.noise_explained, 10)
        self.assertTrue(is_all_noise(diag))

    def test_bl_without_breakdown_counted_as_noise(self):
        """bl (branch-link) without diff_breakdown should be classified as noise."""
        from scripts.permuter.diagnosis import diagnose_baseline

        instrs = [
            {"index": 0, "match_type": "equal", "target": {"opcode": "mr"}, "base": {"opcode": "mr"}},
            {"index": 1, "match_type": "diff_arg", "target": {"opcode": "bl"}, "base": {"opcode": "bl"}},
        ]
        diag = diagnose_baseline({"instructions": instrs})
        self.assertEqual(diag.noise_explained, 1)
        self.assertEqual(diag.noise_total, 1)

    def test_non_reloc_opcode_without_breakdown_not_noise(self):
        """diff_arg with unknown opcode and no diff_breakdown should NOT be counted as noise."""
        from scripts.permuter.diagnosis import diagnose_baseline

        instrs = [
            {"index": 0, "match_type": "equal", "target": {"opcode": "mr"}, "base": {"opcode": "mr"}},
            {"index": 1, "match_type": "diff_arg", "target": {"opcode": "stw"}, "base": {"opcode": "stw"}},
        ]
        diag = diagnose_baseline({"instructions": instrs})
        self.assertEqual(diag.noise_explained, 0)
        self.assertEqual(diag.noise_total, 1)

    def test_diff_arg_with_breakdown_still_analyzed(self):
        """diff_arg with diff_breakdown should use the existing analysis path."""
        from scripts.permuter.diagnosis import diagnose_baseline

        instrs = [
            {"index": 0, "match_type": "equal", "target": {"opcode": "mr"}, "base": {"opcode": "mr"}},
            {"index": 1, "match_type": "diff_arg", "target": {"opcode": "lfs"}, "base": {"opcode": "lfs"},
             "diff_breakdown": {"arguments": [{"arg_type": "immediate", "target": {"value": 0xec}, "base": {"value": 0xe8}}]}},
        ]
        diag = diagnose_baseline({"instructions": instrs})
        self.assertEqual(diag.noise_explained, 1)  # immediate with numeric values = noise



class TestLoopConditionSubtractRelevance(unittest.TestCase):
    """Verify loop_condition_subtract relevance for subf./cmpw diagnoses."""

    def test_relevant_with_subf_cmpw(self):
        p = get_pattern("loop_condition_subtract")
        self.assertTrue(p.relevant(diag_with_subf_cmpw()))

    def test_relevant_with_cmpw_only(self):
        p = get_pattern("loop_condition_subtract")
        self.assertTrue(p.relevant(diag_with_cmp_ops()))

    def test_irrelevant_empty(self):
        p = get_pattern("loop_condition_subtract")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_priority_high_for_subf(self):
        p = get_pattern("loop_condition_subtract")
        self.assertAlmostEqual(p.priority(diag_with_subf_cmpw()), 0.8)

    def test_priority_low_for_generic_cmpw(self):
        p = get_pattern("loop_condition_subtract")
        self.assertAlmostEqual(p.priority(diag_with_cmp_ops()), 0.3)


if __name__ == "__main__":
    unittest.main()
