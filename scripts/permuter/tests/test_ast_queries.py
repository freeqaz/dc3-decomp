"""Tests for ast_queries shared walkers and utilities."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

# Ensure project root is on the path
_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.extractor import _PARSER
from scripts.permuter.ast_queries import (
    find_calls,
    find_comparisons,
    find_if_else,
    get_indent,
    get_line_start,
    identifiers_in,
    node_text,
    walk,
)


def _parse(source: str):
    """Parse C++ source and return root node + source bytes."""
    src = source.encode("utf-8")
    tree = _PARSER.parse(src)
    return tree.root_node, src


def _func_body(root, name: str = "test_func"):
    """Get the body compound_statement of a named function."""
    for child in root.children:
        if child.type == "function_definition":
            decl = child.child_by_field_name("declarator")
            if decl and decl.text and name in decl.text.decode():
                body = child.child_by_field_name("body")
                if body:
                    return body
    raise ValueError(f"Function {name} not found")


class TestFindComparisons(unittest.TestCase):

    def test_default_ops_match_all_six(self):
        root, _ = _parse("void f() { a<b; a>b; a<=b; a>=b; a==b; a!=b; }")
        comps = list(find_comparisons(root))
        self.assertEqual(len(comps), 6)

    def test_restricted_ops(self):
        root, _ = _parse("void f() { a<b; a==b; a!=b; a>b; }")
        comps = list(find_comparisons(root, ops={"<", ">"}))
        self.assertEqual(len(comps), 2)

    def test_no_matches(self):
        root, _ = _parse("void f() { a + b; }")
        comps = list(find_comparisons(root))
        self.assertEqual(len(comps), 0)


class TestFindCalls(unittest.TestCase):

    def test_nested_and_toplevel(self):
        root, _ = _parse("void f() { foo(bar(x)); }")
        calls = list(find_calls(root))
        # Should find both foo(...) and bar(x)
        self.assertEqual(len(calls), 2)

    def test_no_calls(self):
        root, _ = _parse("void f() { int x = 1; }")
        calls = list(find_calls(root))
        self.assertEqual(len(calls), 0)


class TestFindIfElse(unittest.TestCase):

    def test_yields_if_with_else(self):
        root, _ = _parse("void f() { if (x) { a(); } else { b(); } }")
        results = list(find_if_else(root))
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].type, "if_statement")

    def test_skips_if_without_else(self):
        root, _ = _parse("void f() { if (x) { a(); } }")
        results = list(find_if_else(root))
        self.assertEqual(len(results), 0)


class TestGetIndent(unittest.TestCase):

    def test_nested_indent(self):
        source = "void f() {\n    int x = 1;\n}"
        root, src = _parse(source)
        body = _func_body(root, "f")
        stmt = body.named_children[0]  # "int x = 1;"
        indent = get_indent(src, stmt)
        self.assertEqual(indent, b"    ")

    def test_no_indent(self):
        source = "int x = 1;"
        root, src = _parse(source)
        # root's first child
        node = root.children[0]
        indent = get_indent(src, node)
        self.assertEqual(indent, b"")


class TestGetLineStart(unittest.TestCase):

    def test_line_start(self):
        source = "void f() {\n    int x = 1;\n}"
        root, src = _parse(source)
        body = _func_body(root, "f")
        stmt = body.named_children[0]  # "int x = 1;"
        ls = get_line_start(src, stmt)
        self.assertEqual(ls, 11)  # after "void f() {\n"


class TestIdentifiersIn(unittest.TestCase):

    def test_collects_all_identifiers(self):
        root, _ = _parse("void f() { foo(a, b + c); }")
        body = _func_body(root, "f")
        stmt = body.named_children[0]  # expression_statement
        ids = identifiers_in(stmt)
        self.assertIn("foo", ids)
        self.assertIn("a", ids)
        self.assertIn("b", ids)
        self.assertIn("c", ids)

    def test_empty_for_literal(self):
        root, _ = _parse("void f() { 42; }")
        body = _func_body(root, "f")
        stmt = body.named_children[0]
        ids = identifiers_in(stmt)
        self.assertEqual(len(ids), 0)


class TestNodeText(unittest.TestCase):

    def test_extracts_text(self):
        source = "void f() { int x = 1; }"
        root, src = _parse(source)
        body = _func_body(root, "f")
        stmt = body.named_children[0]
        text = node_text(src, stmt)
        self.assertEqual(text, b"int x = 1;")


if __name__ == "__main__":
    unittest.main()
