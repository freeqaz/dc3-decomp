"""Executable tests for return_call_merge and shared control-flow helpers."""

from __future__ import annotations

import unittest

from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.tests.conftest import (
    diag_with_branch_ops,
    make_context,
    match_variant,
)


class TestReturnCallMerge(unittest.TestCase):
    def test_merges_if_else_return_calls(self):
        pattern = get_pattern("return_call_merge")
        ctx = make_context(
            """\
int test_func(bool cond, int a, int b) {
    if (cond) {
        return Pick(a);
    } else {
        return Pick(b);
    }
}
""",
            "test_func",
            diag_with_branch_ops(),
        )
        variants = list(pattern.generate(ctx))
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
int test_func(bool cond, int a, int b) {
    auto _merged;
    if (cond) {
        _merged = a;
    } else {
        _merged = b;
    }
    return Pick(_merged);
}
""",
                    "normalized",
                )
                for v in variants
            )
        )

    def test_splits_assignment_then_return_call(self):
        pattern = get_pattern("return_call_merge")
        ctx = make_context(
            """\
int test_func(bool cond, int a, int b) {
    int _merged;
    if (cond) {
        _merged = a;
    } else {
        _merged = b;
    }
    return Pick(_merged);
}
""",
            "test_func",
            diag_with_branch_ops(),
        )
        variants = list(pattern.generate(ctx))
        self.assertTrue(
            any(
                match_variant(
                    v.source,
                    """\
int test_func(bool cond, int a, int b) {
    if (cond) {
        return Pick(a);
    } else {
        return Pick(b);
    }
}
""",
                    "normalized",
                )
                for v in variants
            )
        )


if __name__ == "__main__":
    unittest.main()
