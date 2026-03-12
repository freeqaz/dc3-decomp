"""Tests for the redundant_guard_elimination pattern.

Verifies:
- else-if-to-else conversion generates correct output
- if-guard-removal generates correct output
- Detection of || in else-if conditions
- relevant() behavior with various diagnosis types
- Pattern metadata and registration

Usage:
    python -m pytest scripts/permuter/tests/test_redundant_guard_elimination.py -x -q
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
    diag_with_branch_ops,
    diag_with_clusters,
    make_context,
    normalize,
)
from scripts.permuter.types import Cluster, Diagnosis, DiffOp
from scripts.permuter.patterns.base import get_pattern


# ---------------------------------------------------------------------------
# Diagnosis factories
# ---------------------------------------------------------------------------

def _diag_with_insert_cluster_4_6() -> Diagnosis:
    """Insert cluster of 5 instructions (4-6 range triggers relevance)."""
    d = _empty_diag()
    d.clusters = [Cluster(start_idx=10, end_idx=15, size=7, inserts=5, deletes=2)]
    return d


def _diag_no_signals() -> Diagnosis:
    """Diagnosis with no relevant signals."""
    return Diagnosis(
        total_instructions=100,
        match_counts={"match": 100, "mismatch": 0},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[],
        clusters=[],
        noise_explained=0,
        noise_total=0,
    )


# ---------------------------------------------------------------------------
# Test sources
# ---------------------------------------------------------------------------

_SOURCE_ELSE_IF_OR = """\
void test_func(int a, int b) {
    if (a > 10) {
        a = 1;
    } else if (a || b) {
        if (a && !b) {
            a = 2;
        } else if (!a && b) {
            a = 3;
        } else {
            a = 4;
        }
    }
}
"""

_SOURCE_ELSE_IF_OR_CHAIN = """\
void test_func(int a, int b) {
    if (a > 10) {
        a = 1;
    } else if (a || b) {
        a = 2;
    } else {
        a = 3;
    }
}
"""

_SOURCE_IF_OR_GUARD = """\
void test_func(int a, int b) {
    if (a || b) {
        if (a && !b) {
            a = 1;
        } else if (!a && b) {
            a = 2;
        }
    }
}
"""

_SOURCE_NO_OR = """\
void test_func(int a) {
    if (a > 10) {
        a = 1;
    } else if (a > 5) {
        a = 2;
    }
}
"""

_SOURCE_NESTED_ELSE_IF_OR = """\
void test_func(int a, int b, int c) {
    if (a > 10) {
        a = 1;
    } else if (a || b) {
        a = 2;
    } else if (b || c) {
        a = 3;
    }
}
"""

_SOURCE_IF_OR_NO_NESTED = """\
void test_func(int a, int b) {
    if (a || b) {
        a = 1;
    }
}
"""


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestPatternRegistration(unittest.TestCase):
    """Verify the pattern is registered and has correct metadata."""

    def test_pattern_registered(self):
        pat = get_pattern("redundant_guard_elimination")
        self.assertIsNotNone(pat)

    def test_pattern_name(self):
        pat = get_pattern("redundant_guard_elimination")
        self.assertEqual(pat.name, "redundant_guard_elimination")

    def test_safety_tier(self):
        pat = get_pattern("redundant_guard_elimination")
        self.assertEqual(pat.safety_tier, "normal")

    def test_structural_domain(self):
        pat = get_pattern("redundant_guard_elimination")
        self.assertEqual(pat.structural_domain, "control_flow")

    def test_follow_ups(self):
        pat = get_pattern("redundant_guard_elimination")
        self.assertIn("branch_polarity", pat.follow_ups)
        self.assertIn("comparison_flip", pat.follow_ups)

    def test_metadata(self):
        pat = get_pattern("redundant_guard_elimination")
        meta = pat.metadata()
        self.assertEqual(meta["name"], "redundant_guard_elimination")
        self.assertEqual(meta["safety_tier"], "normal")
        self.assertFalse(meta["opt_in"])


class TestRelevance(unittest.TestCase):
    """Test relevant() with various diagnosis types."""

    def setUp(self):
        self.pat = get_pattern("redundant_guard_elimination")

    def test_relevant_with_insert_cluster_4_6(self):
        diag = _diag_with_insert_cluster_4_6()
        self.assertTrue(self.pat.relevant(diag))

    def test_relevant_with_branch_ops(self):
        diag = diag_with_branch_ops()
        self.assertTrue(self.pat.relevant(diag))

    def test_relevant_with_clusters(self):
        diag = diag_with_clusters()
        self.assertTrue(self.pat.relevant(diag))

    def test_not_relevant_no_signals(self):
        diag = _diag_no_signals()
        self.assertFalse(self.pat.relevant(diag))

    def test_priority_insert_cluster_4_6(self):
        diag = _diag_with_insert_cluster_4_6()
        self.assertGreaterEqual(self.pat.priority(diag), 0.5)

    def test_priority_general_clusters(self):
        diag = diag_with_clusters()
        self.assertGreater(self.pat.priority(diag), 0.0)

    def test_priority_no_signals(self):
        diag = _diag_no_signals()
        self.assertEqual(self.pat.priority(diag), 0.0)


class TestElseIfToElse(unittest.TestCase):
    """Test else-if-to-else conversion."""

    def test_basic_else_if_or_removal(self):
        """else if (a || b) { body } -> else { body }"""
        pat = get_pattern("redundant_guard_elimination")
        ctx = make_context(_SOURCE_ELSE_IF_OR, "test_func", _empty_diag())
        variants = list(pat.generate(ctx))
        self.assertGreaterEqual(len(variants), 1)

        # Check that at least one variant removes the else-if condition
        found = False
        for v in variants:
            text = v.source.decode("utf-8", errors="replace")
            # The variant should have "else {" without "else if"
            if "} else {" in text and "else if (a || b)" not in text:
                found = True
                break
        self.assertTrue(found, "Expected variant converting else-if to bare else")

    def test_else_if_or_with_further_chain(self):
        """else if (a || b) { ... } else { ... } -> else { ... } else { ... }
        preserves the trailing else clause."""
        pat = get_pattern("redundant_guard_elimination")
        ctx = make_context(_SOURCE_ELSE_IF_OR_CHAIN, "test_func", _empty_diag())
        variants = list(pat.generate(ctx))
        self.assertGreaterEqual(len(variants), 1)

        found = False
        for v in variants:
            text = v.source.decode("utf-8", errors="replace")
            # Must NOT contain the original condition
            if "else if (a || b)" not in text:
                # Must still contain the else clause content "a = 3"
                if "a = 3" in text:
                    found = True
                    break
        self.assertTrue(found, "Expected variant preserving trailing else chain")

    def test_no_variants_without_or(self):
        """No variants when else-if has no || condition."""
        pat = get_pattern("redundant_guard_elimination")
        ctx = make_context(_SOURCE_NO_OR, "test_func", _empty_diag())
        variants = list(pat.generate(ctx))
        # Should not produce else-if-to-else variants (no ||)
        else_if_variants = [
            v for v in variants
            if "else-if" in v.description.lower() or "Remove redundant else-if" in v.description
        ]
        self.assertEqual(len(else_if_variants), 0)

    def test_nested_else_if_or_chains(self):
        """Multiple else-if with || should generate variants for each."""
        pat = get_pattern("redundant_guard_elimination")
        ctx = make_context(_SOURCE_NESTED_ELSE_IF_OR, "test_func", _empty_diag())
        variants = list(pat.generate(ctx))
        # Should get at least 2 variants (one for each else-if with ||)
        else_if_variants = [
            v for v in variants
            if "else-if" in v.description.lower() or "Remove redundant else-if" in v.description
        ]
        self.assertGreaterEqual(len(else_if_variants), 2)

    def test_variant_pattern_name(self):
        """Variants should have the correct pattern_name."""
        pat = get_pattern("redundant_guard_elimination")
        ctx = make_context(_SOURCE_ELSE_IF_OR, "test_func", _empty_diag())
        variants = list(pat.generate(ctx))
        for v in variants:
            self.assertEqual(v.pattern_name, "redundant_guard_elimination")


class TestIfGuardRemoval(unittest.TestCase):
    """Test if-guard-removal conversion."""

    def test_if_or_guard_with_nested_branches(self):
        """if (a || b) { nested_ifs } -> { nested_ifs }"""
        pat = get_pattern("redundant_guard_elimination")
        ctx = make_context(_SOURCE_IF_OR_GUARD, "test_func", _empty_diag())
        variants = list(pat.generate(ctx))

        # Should produce at least one if-guard-removal variant
        guard_variants = [
            v for v in variants
            if "if guard" in v.description.lower() or "Remove redundant if guard" in v.description
        ]
        self.assertGreaterEqual(len(guard_variants), 1)

        # Check the variant removes the outer if condition
        found = False
        for v in guard_variants:
            text = v.source.decode("utf-8", errors="replace")
            if "if (a || b)" not in text:
                # Must still contain the inner if/else
                if "if (a && !b)" in text:
                    found = True
                    break
        self.assertTrue(found, "Expected variant removing outer if guard")

    def test_no_guard_removal_without_nested(self):
        """No if-guard-removal when body has no nested ifs."""
        pat = get_pattern("redundant_guard_elimination")
        ctx = make_context(_SOURCE_IF_OR_NO_NESTED, "test_func", _empty_diag())
        variants = list(pat.generate(ctx))
        guard_variants = [
            v for v in variants
            if "if guard" in v.description.lower() or "Remove redundant if guard" in v.description
        ]
        self.assertEqual(len(guard_variants), 0)

    def test_no_guard_removal_with_else(self):
        """No if-guard-removal when if has an else clause."""
        source = """\
void test_func(int a, int b) {
    if (a || b) {
        if (a) {
            a = 1;
        }
    } else {
        a = 2;
    }
}
"""
        pat = get_pattern("redundant_guard_elimination")
        ctx = make_context(source, "test_func", _empty_diag())
        variants = list(pat.generate(ctx))
        guard_variants = [
            v for v in variants
            if "if guard" in v.description.lower() or "Remove redundant if guard" in v.description
        ]
        self.assertEqual(len(guard_variants), 0)


class TestComposerIntegration(unittest.TestCase):
    """Verify the pattern is wired into the composer follow-up map."""

    def test_follow_up_map_entry(self):
        from scripts.permuter.composer import _FOLLOW_UP_MAP
        self.assertIn("redundant_guard_elimination", _FOLLOW_UP_MAP)
        follow_ups = _FOLLOW_UP_MAP["redundant_guard_elimination"]
        self.assertIn("branch_polarity", follow_ups)
        self.assertIn("comparison_flip", follow_ups)


class TestEdgeCases(unittest.TestCase):
    """Edge case tests."""

    def test_variant_produces_valid_source(self):
        """Generated variants should be parseable C++."""
        from scripts.permuter.extractor import _PARSER
        pat = get_pattern("redundant_guard_elimination")
        ctx = make_context(_SOURCE_ELSE_IF_OR, "test_func", _empty_diag())
        variants = list(pat.generate(ctx))
        for v in variants:
            tree = _PARSER.parse(v.source)
            # Should have no ERROR nodes at top level
            errors = [n for n in tree.root_node.children if n.type == "ERROR"]
            self.assertEqual(len(errors), 0,
                             f"Variant {v.name} produced syntax errors: {v.source.decode()}")

    def test_max_variant_cap(self):
        """Pattern should not produce more than 10 variants."""
        # Build source with many else-if || chains
        lines = ["void test_func(int a, int b, int c, int d) {"]
        lines.append("    if (a > 100) { a = 0; }")
        for i in range(15):
            lines.append(f"    else if (a || b) {{ a = {i + 1}; }}")
        lines.append("}")
        source = "\n".join(lines) + "\n"

        pat = get_pattern("redundant_guard_elimination")
        ctx = make_context(source, "test_func", _empty_diag())
        variants = list(pat.generate(ctx))
        self.assertLessEqual(len(variants), 10)

    def test_description_truncation(self):
        """Long conditions should be truncated in descriptions."""
        source = """\
void test_func(int a, int b) {
    if (a > 10) {
        a = 1;
    } else if (a_very_long_variable_name || another_really_long_variable_name) {
        a = 2;
    }
}
"""
        pat = get_pattern("redundant_guard_elimination")
        ctx = make_context(source, "test_func", _empty_diag())
        variants = list(pat.generate(ctx))
        for v in variants:
            # Description should not be excessively long
            self.assertLessEqual(len(v.description), 200)


if __name__ == "__main__":
    unittest.main()
