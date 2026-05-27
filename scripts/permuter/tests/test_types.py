"""Tests for scripts.permuter.types — qualified-name extraction.

Focus: regression coverage for the template-operator bug where
`operator>><Hmx::Object>(...)` used to parse as base `Hmx::Object`
plus suffix `>`, returning the bogus name `Hmx::Object>`.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.types import extract_qualified_name


class ExtractQualifiedNameTests(unittest.TestCase):
    def test_stl_template_operator_shift_free(self):
        # The historical bug: parsed as "Hmx::Object>".
        self.assertEqual(
            extract_qualified_name(
                "operator>><Hmx::Object>(Hmx::Object& obj, BinStream& stream)"
            ),
            "operator>>",
        )

    def test_stl_template_operator_lshift_free(self):
        self.assertEqual(
            extract_qualified_name("operator<<(BinStream&, const Foo&)"),
            "operator<<",
        )

    def test_namespaced_template_operator(self):
        self.assertEqual(
            extract_qualified_name("std::operator>><Hmx::Object>(int)"),
            "std::operator>>",
        )

    def test_real_comparison_operators(self):
        for op in ("<", ">", "<=", ">=", "==", "!="):
            with self.subTest(op=op):
                self.assertEqual(
                    extract_qualified_name(f"Foo::operator{op}(int x)"),
                    f"Foo::operator{op}",
                )

    def test_shift_operators_on_class(self):
        self.assertEqual(
            extract_qualified_name("Class::operator<<(int x)"),
            "Class::operator<<",
        )
        self.assertEqual(
            extract_qualified_name("Class::operator>>(int x)"),
            "Class::operator>>",
        )

    def test_call_subscript_increment_arithmetic(self):
        cases = [
            ("Class::operator()(int)", "Class::operator()"),
            ("Class::operator[](int)", "Class::operator[]"),
            ("Class::operator++(int)", "Class::operator++"),
            ("Class::operator+(int)", "Class::operator+"),
            ("Class::operator+=(int)", "Class::operator+="),
        ]
        for inp, expected in cases:
            with self.subTest(inp=inp):
                self.assertEqual(extract_qualified_name(inp), expected)

    def test_qualified_method(self):
        self.assertEqual(
            extract_qualified_name("Hmx::Foo::Bar(int)"),
            "Hmx::Foo::Bar",
        )
        self.assertEqual(
            extract_qualified_name("Foo::Bar(int)"),
            "Foo::Bar",
        )

    def test_mwcc_free_function(self):
        self.assertEqual(
            extract_qualified_name("MakeColor(float, float, float, Hmx::Color&)"),
            "MakeColor",
        )

    def test_cdecl_free_function(self):
        self.assertEqual(
            extract_qualified_name("void __cdecl MyFunc(int)"),
            "MyFunc",
        )

    def test_no_match_returns_none(self):
        self.assertIsNone(extract_qualified_name(""))
        self.assertIsNone(extract_qualified_name(";;;not a function"))


if __name__ == "__main__":
    unittest.main()
