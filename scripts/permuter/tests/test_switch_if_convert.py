"""Executable tests for switch/if conversion and related registry wiring."""

from __future__ import annotations

import unittest

from scripts.permuter.composer import _FOLLOW_UP_MAP
from scripts.permuter.patterns.base import get_pattern, list_patterns
from scripts.permuter.tests.conftest import (
    diag_with_branch_and_clusters,
    diag_with_branch_ops,
    make_context,
    match_variant,
)


def _assert_has_variant(
    testcase: unittest.TestCase,
    pattern_name: str,
    source: str,
    expected: str,
    diagnosis,
    func_name: str = "test_func",
) -> None:
    pattern = get_pattern(pattern_name)
    ctx = make_context(source, func_name, diagnosis)
    variants = list(pattern.generate(ctx))
    testcase.assertGreater(len(variants), 0, "pattern generated no variants")
    testcase.assertTrue(
        any(match_variant(v.source, expected, "normalized") for v in variants),
        f"no variant matched expected output for {pattern_name}",
    )


class TestSwitchIfConvert(unittest.TestCase):
    def test_if_chain_to_switch(self):
        _assert_has_variant(
            self,
            "switch_if_convert",
            """\
void test_func(int state) {
    if (state == 0) {
        do_a();
    } else if (state == 1) {
        do_b();
    } else if (state == 2) {
        do_c();
    } else {
        do_d();
    }
}
""",
            """\
void test_func(int state) {
    switch (state) {
    case 0:
        do_a();
        break;
    case 1:
        do_b();
        break;
    case 2:
        do_c();
        break;
    default:
        do_d();
        break;
    }
}
""",
            diag_with_branch_ops(),
        )

    def test_infers_less_than_case(self):
        _assert_has_variant(
            self,
            "switch_if_convert",
            """\
void test_func(unsigned int i) {
    if (i == 0) {
        do_a();
    } else if (i == 1) {
        do_b();
    } else if (i < 3) {
        do_c();
    } else if (i == 3) {
        do_d();
    } else {
        do_e();
    }
}
""",
            """\
void test_func(unsigned int i) {
    switch (i) {
    case 0:
        do_a();
        break;
    case 1:
        do_b();
        break;
    case 2:
        do_c();
        break;
    case 3:
        do_d();
        break;
    default:
        do_e();
        break;
    }
}
""",
            diag_with_branch_and_clusters(),
        )

    def test_handles_casts_and_reversed_equality(self):
        _assert_has_variant(
            self,
            "switch_if_convert",
            """\
void test_func(unsigned int i) {
    if ((unsigned int)i == 0) {
        do_a();
    } else if (1 == i) {
        do_b();
    } else if (i == 2) {
        do_c();
    } else {
        do_d();
    }
}
""",
            """\
void test_func(unsigned int i) {
    switch ((unsigned int)i) {
    case 0:
        do_a();
        break;
    case 1:
        do_b();
        break;
    case 2:
        do_c();
        break;
    default:
        do_d();
        break;
    }
}
""",
            diag_with_branch_ops(),
        )

    def test_switch_to_if_chain(self):
        _assert_has_variant(
            self,
            "switch_if_convert",
            """\
void test_func(int state) {
    switch (state) {
    case 0:
        do_a();
        break;
    case 1:
        do_b();
        break;
    default:
        do_c();
        break;
    }
}
""",
            """\
void test_func(int state) {
    if (state == 0) {
        do_a();
    } else if (state == 1) {
        do_b();
    } else {
        do_c();
    }
}
""",
            diag_with_branch_ops(),
        )

    def test_rejects_side_effect_conditions(self):
        pattern = get_pattern("switch_if_convert")
        ctx = make_context(
            """\
void test_func(int i) {
    if (i++ == 0) {
        do_a();
    } else if (i == 1) {
        do_b();
    } else if (i == 2) {
        do_c();
    }
}
""",
            "test_func",
            diag_with_branch_ops(),
        )
        self.assertEqual(list(pattern.generate(ctx)), [])


class TestSwitchIfConvertWiring(unittest.TestCase):
    def test_follow_up_map_contains_switch_edges(self):
        self.assertIn("switch_if_convert", _FOLLOW_UP_MAP["branch_polarity"])
        self.assertEqual(
            _FOLLOW_UP_MAP["switch_if_convert"],
            ["branch_polarity", "declaration_reorder"],
        )

    def test_previously_unregistered_patterns_are_registered(self):
        patterns = set(list_patterns(include_opt_in=True))
        for name in {
            "bool_materialize",
            "byte_mask_extraction",
            "condition_arithmetic",
            "float_literal_pressure",
            "nor_prevention",
            "return_call_merge",
            "switch_if_convert",
            "tail_call_reorder",
        }:
            self.assertIn(name, patterns)


if __name__ == "__main__":
    unittest.main()
