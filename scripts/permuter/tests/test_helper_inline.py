"""Tests for the helper_inline pattern.

Verifies:
- _substitute_members correctly prefixes mFoo references
- _substitute_members rejects bodies containing `this`
- end-to-end inlining when a helper is in a header next to the source
- pattern is registered with expected metadata

Usage:
    python -m pytest scripts/permuter/tests/test_helper_inline.py -x -q
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
    diag_with_clusters,
    make_context,
)
from scripts.permuter.extractor import _PARSER, _find_all_function_defs, _get_function_name
from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.patterns.helper_inline import (
    _substitute_members,
    _lookup_helper_body,
)
from scripts.permuter.types import FunctionContext


def _diag_with_replace_real():
    d = _empty_diag()
    d.replace_real = 1
    return d


# ---------------------------------------------------------------------------
# Registration tests
# ---------------------------------------------------------------------------


class HelperInlineRegistrationTests(unittest.TestCase):
    def test_pattern_is_registered(self):
        pat = get_pattern("helper_inline")
        self.assertEqual(pat.name, "helper_inline")
        self.assertEqual(pat.safety_tier, "moderate")
        self.assertEqual(pat.structural_domain, "data_flow")


class HelperInlineRelevanceTests(unittest.TestCase):
    def test_irrelevant_on_empty_diag(self):
        pat = get_pattern("helper_inline")
        self.assertFalse(pat.relevant(_empty_diag()))

    def test_relevant_on_clusters(self):
        pat = get_pattern("helper_inline")
        self.assertTrue(pat.relevant(diag_with_clusters()))


# ---------------------------------------------------------------------------
# Substitution unit tests
# ---------------------------------------------------------------------------


class SubstituteMembersTests(unittest.TestCase):
    def test_prefix_simple_member(self):
        out = _substitute_members(b"mType >= 1", b"obj", b"->")
        self.assertEqual(out, b"obj->mType >= 1")

    def test_prefix_chained_members(self):
        out = _substitute_members(
            b"mType >= kHmxGold && mType <= kHmxBronze",
            b"obj",
            b"->",
        )
        self.assertEqual(
            out,
            b"obj->mType >= kHmxGold && obj->mType <= kHmxBronze",
        )

    def test_dot_operator(self):
        out = _substitute_members(b"mFoo + mBar", b"x", b".")
        self.assertEqual(out, b"x.mFoo + x.mBar")

    def test_does_not_match_non_member(self):
        # `match` doesn't start with `m[A-Z]`, so it stays bare
        out = _substitute_members(b"match() && mFoo", b"o", b"->")
        self.assertEqual(out, b"match() && o->mFoo")

    def test_rejects_this_reference(self):
        out = _substitute_members(b"this->mType >= 1", b"obj", b"->")
        self.assertIsNone(out)

    def test_free_function_no_substitution(self):
        out = _substitute_members(b"mType >= 1", b"", b"")
        self.assertEqual(out, b"mType >= 1")


# ---------------------------------------------------------------------------
# Header lookup unit test
# ---------------------------------------------------------------------------


def _parse_header_defs(header_source: bytes):
    """Build the {simple_name: (func_node, body_node, source)} map for a
    single in-memory header source — mirrors what
    `_collect_header_function_defs` returns from disk."""
    tree = _PARSER.parse(header_source)
    out = {}
    for func_node in _find_all_function_defs(tree.root_node):
        name = _get_function_name(func_node)
        if name is None:
            continue
        body = func_node.child_by_field_name("body")
        if body is None:
            continue
        simple = name.rsplit("::", 1)[-1]
        out.setdefault(simple, (func_node, body, header_source))
    return out


class LookupHelperBodyTests(unittest.TestCase):
    def test_extracts_return_expression(self):
        header = textwrap.dedent("""\
            class Challenges {
            public:
                bool IsHMXChallenge() const {
                    return mType >= 1 && mType <= 3;
                }
            };
        """).encode("utf-8")
        defs = _parse_header_defs(header)
        body = _lookup_helper_body("IsHMXChallenge", defs)
        self.assertIsNotNone(body)
        self.assertIn(b"mType", body)
        self.assertIn(b">= 1", body)

    def test_skips_multi_statement_body(self):
        header = textwrap.dedent("""\
            class Foo {
                int Bar() const {
                    int x = 1;
                    return x + mY;
                }
            };
        """).encode("utf-8")
        defs = _parse_header_defs(header)
        self.assertIsNone(_lookup_helper_body("Bar", defs))

    def test_skips_body_with_call_expression(self):
        header = textwrap.dedent("""\
            class Foo {
                int Bar() const {
                    return GetWidget()->mY;
                }
            };
        """).encode("utf-8")
        defs = _parse_header_defs(header)
        self.assertIsNone(_lookup_helper_body("Bar", defs))

    def test_returns_none_when_method_missing(self):
        header = textwrap.dedent("""\
            class Foo {
                int Bar() const { return mY; }
            };
        """).encode("utf-8")
        defs = _parse_header_defs(header)
        self.assertIsNone(_lookup_helper_body("Baz", defs))


# ---------------------------------------------------------------------------
# End-to-end test (real files on disk)
# ---------------------------------------------------------------------------


class HelperInlineEndToEndTests(unittest.TestCase):
    """Stage a tiny project on disk and confirm a variant gets generated."""

    def _make_project(self, tmp_path: Path) -> Path:
        # .git marker so _project_root_for picks tmp_path
        (tmp_path / ".git").mkdir()
        (tmp_path / "Foo.h").write_bytes(textwrap.dedent("""\
            #pragma once

            class Foo {
            public:
                int mType;
                bool IsBig() const {
                    return mType >= 100;
                }
            };
        """).encode("utf-8"))

        cpp_src = textwrap.dedent("""\
            #include "Foo.h"

            int Foo::Compute() const {
                if (this->IsBig()) {
                    return 1;
                }
                return 0;
            }
        """).encode("utf-8")
        cpp_path = tmp_path / "Foo.cpp"
        cpp_path.write_bytes(cpp_src)
        return cpp_path

    def test_inlines_call_to_header_helper(self):
        from tempfile import TemporaryDirectory

        with TemporaryDirectory() as td:
            tmp_path = Path(td)
            cpp_path = self._make_project(tmp_path)
            cpp_src = cpp_path.read_bytes()

            # Build a FunctionContext pointing at Foo.cpp on disk so
            # `_collect_header_function_defs` can resolve includes.
            tree = _PARSER.parse(cpp_src)
            func_node = None
            for child in tree.root_node.children:
                if child.type != "function_definition":
                    continue
                if _get_function_name(child) == "Foo::Compute":
                    func_node = child
                    break
            self.assertIsNotNone(func_node, "Foo::Compute not found")
            body = func_node.child_by_field_name("body")

            ctx = FunctionContext(
                file_path=cpp_path,
                file_source=cpp_src,
                func_node=func_node,
                body_node=body,
                statements=list(body.named_children),
                func_byte_range=(func_node.start_byte, func_node.end_byte),
                diagnosis=_diag_with_replace_real(),
            )

            pat = get_pattern("helper_inline")
            variants = list(pat.generate(ctx))
            self.assertGreater(
                len(variants), 0,
                "Expected at least one variant inlining IsBig()",
            )
            # The receiver is `this` so substitution should be rejected
            # (this is the "rejects body with `this`" path — but the
            # call site uses `this->IsBig()`, the BODY uses `mType` which
            # is fine; rejection only applies to bodies referencing this).
            sources = [v.source.decode("utf-8") for v in variants]
            self.assertTrue(
                any("this->mType" in s for s in sources),
                f"Expected this->mType in inlined variant; got: {sources}",
            )


if __name__ == "__main__":
    unittest.main()
