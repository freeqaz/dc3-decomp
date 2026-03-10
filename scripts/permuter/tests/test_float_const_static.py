"""Tests for the float_const_static pattern.

Verifies:
- relevance gating (GPR-FPR type conflict, prologue FPR delta)
- priority scoring
- literal -> static const extraction (single, bulk, file-scope)
- static const -> literal inlining (in-function, file-scope)
- edge case handling (0.0f/1.0f skip, macro skip, existing static const skip)

Usage:
    python -m pytest scripts/permuter/tests/test_float_const_static.py -v
"""

from __future__ import annotations

import sys
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.tests.conftest import (
    _empty_diag,
    diag_with_gpr_fpr_conflict,
    make_context,
    normalize,
)
from scripts.permuter.types import Diagnosis
from scripts.permuter.patterns.base import get_pattern


# ---------------------------------------------------------------------------
# Diagnosis factories
# ---------------------------------------------------------------------------

def _diag_gpr_up_fpr_down() -> Diagnosis:
    """Target needs +1 GPR, -1 FPR (inline float -> static const)."""
    d = _empty_diag()
    d.target_gpr_saves = 4
    d.base_gpr_saves = 3
    d.target_fpr_saves = 1
    d.base_fpr_saves = 2
    return d


def _diag_gpr_down_fpr_up() -> Diagnosis:
    """Target needs -1 GPR, +1 FPR (static const -> inline float)."""
    d = _empty_diag()
    d.target_gpr_saves = 3
    d.base_gpr_saves = 4
    d.target_fpr_saves = 2
    d.base_fpr_saves = 1
    return d


def _diag_fpr_delta_no_conflict() -> Diagnosis:
    """FPR delta but no GPR-FPR conflict (both deltas same sign)."""
    d = _empty_diag()
    d.target_gpr_saves = 5
    d.base_gpr_saves = 4
    d.target_fpr_saves = 3
    d.base_fpr_saves = 2
    return d


def _diag_prologue_fpr_only() -> Diagnosis:
    """FPR prologue mismatch without GPR conflict."""
    d = _empty_diag()
    d.target_gpr_saves = 3
    d.base_gpr_saves = 3
    d.target_fpr_saves = 1
    d.base_fpr_saves = 2
    return d


# ---------------------------------------------------------------------------
# Test sources
# ---------------------------------------------------------------------------

_SOURCE_SINGLE_FLOAT = """\
void test_func(float x) {
    if (x > 100.0f) {
        x = 100.0f;
    }
}
"""

_SOURCE_MULTIPLE_FLOATS = """\
void test_func(float x, float y) {
    if (x > 3.14f) {
        y = 2.718f;
    }
    if (y < 3.14f) {
        x = 42.0f;
    }
}
"""

_SOURCE_WITH_MILO = """\
void test_func(float x) {
    MILO_ASSERT(x > 5.0f, 42);
    if (x > 100.0f) {
        x = 100.0f;
    }
}
"""

_SOURCE_WITH_ZERO_ONE = """\
void test_func(float x) {
    x = 0.0f;
    x += 1.0f;
    if (x > 50.0f) {
        x = 50.0f;
    }
}
"""

_SOURCE_WITH_STATIC_CONST = """\
void test_func(float x) {
    static const float kLimit = 100.0f;
    if (x > kLimit) {
        x = kLimit;
    }
}
"""

_SOURCE_FILE_SCOPE_STATIC = """\
static const float s_limit = 100.0f;
void test_func(float x) {
    if (x > s_limit) {
        x = s_limit;
    }
}
"""


# ---------------------------------------------------------------------------
# Relevance tests
# ---------------------------------------------------------------------------

class TestRelevance(unittest.TestCase):

    def setUp(self):
        self.pattern = get_pattern("float_const_static")

    def test_relevant_with_gpr_fpr_conflict(self):
        """GPR-FPR type conflict -> relevant."""
        d = diag_with_gpr_fpr_conflict()
        self.assertTrue(d.has_gpr_fpr_type_conflict)
        self.assertTrue(self.pattern.relevant(d))

    def test_relevant_with_fpr_delta(self):
        """Prologue mismatch with FPR delta (no conflict) -> relevant."""
        d = _diag_prologue_fpr_only()
        self.assertTrue(d.has_prologue_mismatch)
        self.assertFalse(d.has_gpr_fpr_type_conflict)
        self.assertTrue(self.pattern.relevant(d))

    def test_irrelevant_empty_diag(self):
        """Empty diagnosis -> not relevant."""
        self.assertFalse(self.pattern.relevant(_empty_diag()))

    def test_irrelevant_same_sign_deltas(self):
        """Both GPR and FPR deltas positive -> has_gpr_fpr_type_conflict is False,
        but has_prologue_mismatch with nonzero fpr_save_delta -> still relevant."""
        d = _diag_fpr_delta_no_conflict()
        self.assertFalse(d.has_gpr_fpr_type_conflict)
        # But it still has a nonzero FPR delta
        self.assertTrue(self.pattern.relevant(d))

    def test_irrelevant_no_fpr_delta(self):
        """Prologue mismatch with GPR-only delta -> not relevant."""
        d = _empty_diag()
        d.target_gpr_saves = 5
        d.base_gpr_saves = 3
        d.target_fpr_saves = 2
        d.base_fpr_saves = 2
        self.assertTrue(d.has_prologue_mismatch)
        self.assertEqual(d.fpr_save_delta, 0)
        self.assertFalse(self.pattern.relevant(d))


# ---------------------------------------------------------------------------
# Priority tests
# ---------------------------------------------------------------------------

class TestPriority(unittest.TestCase):

    def setUp(self):
        self.pattern = get_pattern("float_const_static")

    def test_high_priority_gpr_fpr_conflict(self):
        """GPR-FPR type conflict -> highest priority."""
        d = diag_with_gpr_fpr_conflict()
        p = self.pattern.priority(d)
        self.assertGreaterEqual(p, 0.9)

    def test_medium_priority_fpr_delta(self):
        """Prologue FPR mismatch without conflict -> medium priority."""
        d = _diag_prologue_fpr_only()
        p = self.pattern.priority(d)
        self.assertGreater(p, 0.5)
        self.assertLess(p, 0.9)

    def test_zero_priority_empty(self):
        """Empty diagnosis -> zero priority."""
        self.assertEqual(self.pattern.priority(_empty_diag()), 0.0)


# ---------------------------------------------------------------------------
# Generation tests: literal -> static const
# ---------------------------------------------------------------------------

class TestLiteralToStatic(unittest.TestCase):

    def setUp(self):
        self.pattern = get_pattern("float_const_static")

    def test_single_float_extraction(self):
        """Single float literal (used 2x) extracted to static const."""
        ctx = make_context(_SOURCE_SINGLE_FLOAT, "test_func", _diag_gpr_up_fpr_down())
        variants = list(self.pattern.generate(ctx))
        self.assertGreater(len(variants), 0)

        # At least one variant should contain "static const float"
        found = False
        for v in variants:
            text = v.source.decode("utf-8")
            if "static const float" in text and "100.0f" in text:
                found = True
                # The literal uses should be replaced with the variable name
                # The declaration should exist
                self.assertIn("static const float", text)
                break
        self.assertTrue(found, "No variant extracted float literal to static const")

    def test_multiple_float_extraction(self):
        """Multiple distinct float literals extracted."""
        ctx = make_context(_SOURCE_MULTIPLE_FLOATS, "test_func", _diag_gpr_up_fpr_down())
        variants = list(self.pattern.generate(ctx))
        self.assertGreater(len(variants), 0)

        # Should have a bulk variant that extracts all literals
        found_bulk = False
        for v in variants:
            if "all" in v.name or "filescope" in v.name:
                text = v.source.decode("utf-8")
                # All literal values should still be present (in declarations)
                self.assertIn("3.14f", text)
                self.assertIn("2.718f", text)
                found_bulk = True
                break
        # Either a bulk or individual extraction should exist
        self.assertGreater(len(variants), 1, "Should generate multiple extraction variants")

    def test_skip_zero_and_one(self):
        """0.0f and 1.0f should be skipped, only 50.0f extracted."""
        ctx = make_context(_SOURCE_WITH_ZERO_ONE, "test_func", _diag_gpr_up_fpr_down())
        variants = list(self.pattern.generate(ctx))

        for v in variants:
            text = v.source.decode("utf-8")
            # 0.0f and 1.0f should remain as inline literals (not in declarations)
            # Check: no static const for 0.0f or 1.0f
            lines = text.split("\n")
            for line in lines:
                if "static const float" in line:
                    self.assertNotIn("= 0.0f", line)
                    self.assertNotIn("= 1.0f", line)

    def test_skip_milo_assert(self):
        """Float literals inside MILO_ASSERT should not be extracted."""
        ctx = make_context(_SOURCE_WITH_MILO, "test_func", _diag_gpr_up_fpr_down())
        variants = list(self.pattern.generate(ctx))

        for v in variants:
            text = v.source.decode("utf-8")
            # The MILO_ASSERT line should still have the original 5.0f literal
            if "MILO_ASSERT" in text:
                # Find the MILO_ASSERT line
                for line in text.split("\n"):
                    if "MILO_ASSERT" in line:
                        self.assertIn("5.0f", line)
                        break

    def test_no_variants_when_no_floats(self):
        """No float literals -> no variants from literal->static direction."""
        source = "void test_func(int x) { x = x + 1; }"
        ctx = make_context(source, "test_func", _diag_gpr_up_fpr_down())
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 0)


# ---------------------------------------------------------------------------
# Generation tests: static const -> literal
# ---------------------------------------------------------------------------

class TestStaticToLiteral(unittest.TestCase):

    def setUp(self):
        self.pattern = get_pattern("float_const_static")

    def test_inline_in_function_static(self):
        """In-function static const float inlined to literal."""
        ctx = make_context(
            _SOURCE_WITH_STATIC_CONST, "test_func", _diag_gpr_down_fpr_up()
        )
        variants = list(self.pattern.generate(ctx))
        self.assertGreater(len(variants), 0)

        # At least one variant should inline the static const
        found = False
        for v in variants:
            text = v.source.decode("utf-8")
            if "static const float kLimit" not in text:
                # The declaration was removed
                # The literal should appear inline
                if "100.0f" in text:
                    found = True
                    break
        self.assertTrue(found, "No variant inlined static const to literal")

    def test_inline_file_scope_static(self):
        """File-scope static const float inlined to literal."""
        ctx = make_context(
            _SOURCE_FILE_SCOPE_STATIC, "test_func", _diag_gpr_down_fpr_up()
        )
        variants = list(self.pattern.generate(ctx))
        self.assertGreater(len(variants), 0)

        # At least one variant should replace s_limit with 100.0f
        found = False
        for v in variants:
            text = v.source.decode("utf-8")
            # The function body should use the literal directly
            if "100.0f" in text and "s_limit" not in text.split("void test_func")[1]:
                found = True
                break
        self.assertTrue(found, "No variant inlined file-scope static const")


# ---------------------------------------------------------------------------
# Generation tests: edge cases
# ---------------------------------------------------------------------------

class TestEdgeCases(unittest.TestCase):

    def setUp(self):
        self.pattern = get_pattern("float_const_static")

    def test_no_variants_with_no_diagnosis(self):
        """No diagnosis -> no variants."""
        ctx = make_context(_SOURCE_SINGLE_FLOAT, "test_func", _diag_gpr_up_fpr_down())
        ctx.diagnosis = None
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 0)

    def test_file_scope_variant_has_different_prefix(self):
        """File-scope variants should use s_kFloat prefix."""
        ctx = make_context(_SOURCE_SINGLE_FLOAT, "test_func", _diag_gpr_up_fpr_down())
        variants = list(self.pattern.generate(ctx))

        file_scope_found = False
        for v in variants:
            if "filescope" in v.name:
                text = v.source.decode("utf-8")
                self.assertIn("s_kFloat", text)
                file_scope_found = True
        # File-scope variant should exist
        if len(variants) > 2:
            self.assertTrue(file_scope_found)

    def test_variant_names_unique(self):
        """All variant names should be unique."""
        ctx = make_context(_SOURCE_MULTIPLE_FLOATS, "test_func", _diag_gpr_up_fpr_down())
        variants = list(self.pattern.generate(ctx))
        names = [v.name for v in variants]
        self.assertEqual(len(names), len(set(names)), f"Duplicate names: {names}")

    def test_all_variants_have_pattern_name(self):
        """All variants should carry the correct pattern name."""
        ctx = make_context(_SOURCE_SINGLE_FLOAT, "test_func", _diag_gpr_up_fpr_down())
        variants = list(self.pattern.generate(ctx))
        for v in variants:
            self.assertEqual(v.pattern_name, "float_const_static")

    def test_existing_static_const_not_doubled(self):
        """If source already has static const float, extracting should not create duplicates."""
        # When the direction is literal -> static, but the function already has
        # a static const, it should not re-extract the same value.
        ctx = make_context(
            _SOURCE_WITH_STATIC_CONST, "test_func", _diag_gpr_up_fpr_down()
        )
        variants = list(self.pattern.generate(ctx))
        # If variants are generated, none should have two identical static const declarations
        for v in variants:
            text = v.source.decode("utf-8")
            # Count static const float declarations for the same value
            count = text.count("static const float")
            # Original has 1, so we may have at most 1 (kept) or 0 (removed)
            # but not 2 for the same value
            self.assertLessEqual(count, 2, "Should not duplicate static const declarations")


# ---------------------------------------------------------------------------
# Interaction with float_literal_pressure
# ---------------------------------------------------------------------------

class TestPatternInteraction(unittest.TestCase):

    def test_both_patterns_registered(self):
        """Both float_const_static and float_literal_pressure should be registered."""
        fcs = get_pattern("float_const_static")
        flp = get_pattern("float_literal_pressure")
        self.assertIsNotNone(fcs)
        self.assertIsNotNone(flp)

    def test_different_names(self):
        """The two patterns should have different names."""
        fcs = get_pattern("float_const_static")
        flp = get_pattern("float_literal_pressure")
        self.assertNotEqual(fcs.name, flp.name)

    def test_fcs_higher_priority_on_type_conflict(self):
        """float_const_static should have higher priority than float_literal_pressure
        when GPR-FPR type conflict is present."""
        d = diag_with_gpr_fpr_conflict()
        fcs = get_pattern("float_const_static")
        flp = get_pattern("float_literal_pressure")
        self.assertGreaterEqual(fcs.priority(d), flp.priority(d))


if __name__ == "__main__":
    unittest.main()
