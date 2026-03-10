"""Tests for shared control-flow helper utilities."""

from __future__ import annotations

import unittest

from scripts.permuter.control_flow import (
    else_compound_body,
    is_bare_return_statement,
    iter_compound_statements,
    noncomment_named_children,
    trailing_run,
)
from scripts.permuter.tests.conftest import diag_with_clusters, make_context


class TestControlFlowHelpers(unittest.TestCase):
    def test_iter_compound_statements_includes_nested_blocks(self):
        ctx = make_context(
            """\
void test_func(int ok) {
    if (ok) {
        First();
    }
}
""",
            "test_func",
            diag_with_clusters(),
        )
        compounds = list(iter_compound_statements(ctx.body_node))
        self.assertEqual(len(compounds), 2)

    def test_else_compound_body_skips_else_if(self):
        ctx = make_context(
            """\
void test_func(int ok, int more) {
    if (ok) {
        First();
    } else if (more) {
        Second();
    } else {
        Third();
    }
}
""",
            "test_func",
            diag_with_clusters(),
        )
        if_stmt = ctx.statements[0]
        alt = if_stmt.child_by_field_name("alternative")
        self.assertIsNotNone(alt)
        self.assertIsNone(else_compound_body(alt))

    def test_trailing_run_and_bare_return_detection(self):
        ctx = make_context(
            """\
void test_func() {
    Alpha();
    Beta();
    return;
}
""",
            "test_func",
            diag_with_clusters(),
        )
        children = noncomment_named_children(ctx.body_node)
        self.assertTrue(is_bare_return_statement(children[-1], ctx.file_source))
        run = trailing_run(children[:-1], lambda stmt: stmt.type == "expression_statement")
        self.assertEqual(len(run), 2)


if __name__ == "__main__":
    unittest.main()
