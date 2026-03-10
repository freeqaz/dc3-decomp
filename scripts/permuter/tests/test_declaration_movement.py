"""Executable tests for declaration_movement safety and generation."""

from __future__ import annotations

import unittest

from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.tests.conftest import (
    diag_with_gpr_swaps,
    make_context,
    match_variant,
)


class TestDeclarationMovement(unittest.TestCase):
    def test_moves_accessor_initialized_declaration(self):
        pattern = get_pattern("declaration_movement")
        ctx = make_context(
            """\
void test_func() {
    int local = GetValue();
    int total = 0;
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
void test_func() {
    int total = 0;
    int local = GetValue();
}
""",
                    "normalized",
                )
                for v in variants
            )
        )

    def test_does_not_move_mutator_initialized_declaration(self):
        pattern = get_pattern("declaration_movement")
        ctx = make_context(
            """\
void test_func() {
    int local = SetValue();
    int total = 0;
    int count = 1;
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        self.assertFalse(
            any(
                match_variant(
                    v.source,
                    """\
void test_func() {
    int total = 0;
    int count = 1;
    int local = SetValue();
}
""",
                    "normalized",
                )
                for v in variants
            )
        )

    def test_does_not_move_call_initializer_across_logging_call(self):
        pattern = get_pattern("declaration_movement")
        ctx = make_context(
            """\
void test_func() {
    int local = GetValue();
    printf("log");
    int total = 0;
}
""",
            "test_func",
            diag_with_gpr_swaps(),
        )

        variants = list(pattern.generate(ctx))
        self.assertFalse(
            any(
                match_variant(
                    v.source,
                    """\
void test_func() {
    printf("log");
    int local = GetValue();
    int total = 0;
}
""",
                    "normalized",
                )
                for v in variants
            )
        )


if __name__ == "__main__":
    unittest.main()
