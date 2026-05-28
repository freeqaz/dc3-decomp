"""Tests for member_readback, cache_repeated_call, and symbol_str_compare patterns.

Pure AST/text-level tests — no builds, no objdiff.
"""

from __future__ import annotations

import sys
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.extractor import _PARSER, _get_function_name
from scripts.permuter.types import (
    Cluster,
    Diagnosis,
    DiffOp,
    FunctionContext,
    SwapInfo,
)

# Import the three modules directly (bypasses __init__.py, safe for testing
# without touching the shared registry file)
from scripts.permuter.patterns.member_readback import MemberReadbackPattern
from scripts.permuter.patterns.cache_repeated_call import CacheRepeatedCallPattern
from scripts.permuter.patterns.symbol_str_compare import SymbolStrComparePattern


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_ctx(source_text: str, func_name: str, diagnosis: Diagnosis) -> FunctionContext:
    source_bytes = textwrap.dedent(source_text).encode("utf-8")
    tree = _PARSER.parse(source_bytes)
    for child in tree.root_node.children:
        if child.type != "function_definition":
            continue
        if _get_function_name(child) == func_name:
            body = child.child_by_field_name("body")
            if body is None:
                raise ValueError(f"Function {func_name} has no body")
            return FunctionContext(
                file_path=Path("/dev/null"),
                file_source=source_bytes,
                func_node=child,
                body_node=body,
                statements=list(body.named_children),
                func_byte_range=(child.start_byte, child.end_byte),
                diagnosis=diagnosis,
            )
    raise ValueError(f"Function '{func_name}' not found")


def _diag_clrlwi() -> Diagnosis:
    return Diagnosis(
        total_instructions=20,
        match_counts={},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[DiffOp(0, "clrlwi.", "cmpwi")],
        clusters=[],
        noise_explained=0,
        noise_total=1,
    )


def _diag_bl_cmplw() -> Diagnosis:
    return Diagnosis(
        total_instructions=30,
        match_counts={},
        reg_swap_pairs={},
        offset_deltas={},
        # relevant() was sharpened to require the bl target be a recognized
        # Symbol-equality / strcmp symbol (not any generic bl mismatch), so the
        # diff_op must carry that arg.
        diff_ops=[DiffOp(0, "bl", "cmplw", target_arg="strcmp")],
        clusters=[Cluster(5, 10, 5, 2, 3, ("bl",), ())],
        noise_explained=0,
        noise_total=3,
        replace_real=2,
    )


def _diag_clusters() -> Diagnosis:
    return Diagnosis(
        total_instructions=30,
        match_counts={},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[DiffOp(0, "bl", "cmplw")],
        clusters=[Cluster(5, 10, 5, 2, 3, ("bl",), ())],
        noise_explained=0,
        noise_total=3,
    )


def _contains_normalized(haystack: bytes, needle: str) -> bool:
    import re
    h = re.sub(r"\s+", " ", haystack.decode("utf-8", errors="replace")).strip()
    n = re.sub(r"\s+", " ", needle).strip()
    return n in h


# ---------------------------------------------------------------------------
# member_readback tests
# ---------------------------------------------------------------------------

class TestMemberReadback(unittest.TestCase):

    def setUp(self):
        self.pattern = MemberReadbackPattern()

    def test_basic_bool_negation(self):
        """if (!arg) after mMember = arg; -> if (!mMember)"""
        src = """
        void Fn(bool enabled) {
            mEnabled = enabled;
            if (!enabled) {
                Foo();
            }
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_clrlwi())
        variants = list(self.pattern.generate(ctx))
        self.assertTrue(len(variants) >= 1, "Should generate at least one variant")
        found = any(
            _contains_normalized(v.source, "if (!mEnabled)")
            for v in variants
        )
        self.assertTrue(found, "Expected if (!mEnabled) variant")

    def test_relevant_clrlwi(self):
        """relevant() returns True for clrlwi./cmpwi mismatch."""
        self.assertTrue(self.pattern.relevant(_diag_clrlwi()))

    def test_no_variant_when_reassigned(self):
        """No variant when arg is reassigned before the if."""
        src = """
        void Fn(bool enabled) {
            mEnabled = enabled;
            enabled = false;
            if (!enabled) {
                Foo();
            }
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_clrlwi())
        variants = list(self.pattern.generate(ctx))
        # No variant: arg 'enabled' was reassigned before the bool test
        found = any(
            _contains_normalized(v.source, "if (!mEnabled)")
            for v in variants
        )
        self.assertFalse(found, "Should not replace when arg is reassigned")

    def test_no_variant_when_member_reassigned(self):
        """No variant when member is reassigned before the if."""
        src = """
        void Fn(bool enabled) {
            mEnabled = enabled;
            mEnabled = false;
            if (!enabled) {
                Foo();
            }
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_clrlwi())
        variants = list(self.pattern.generate(ctx))
        found = any(
            _contains_normalized(v.source, "if (!mEnabled)")
            for v in variants
        )
        self.assertFalse(found, "Should not replace when member is reassigned")

    def test_non_member_lhs_ignored(self):
        """Does not fire when LHS is not an mFoo-named member."""
        src = """
        void Fn(bool enabled) {
            localVar = enabled;
            if (!enabled) {
                Foo();
            }
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_clrlwi())
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 0)

    def test_registration(self):
        """Pattern can be retrieved from global registry after import."""
        from scripts.permuter.patterns.member_readback import MemberReadbackPattern as P
        from scripts.permuter.patterns.base import get_pattern
        import scripts.permuter.patterns.member_readback  # noqa: ensure registered
        p = get_pattern("member_readback")
        self.assertIsInstance(p, P)


# ---------------------------------------------------------------------------
# cache_repeated_call tests
# ---------------------------------------------------------------------------

class TestCacheRepeatedCall(unittest.TestCase):

    def setUp(self):
        self.pattern = CacheRepeatedCallPattern()

    def test_basic_end_twice(self):
        """MILO_ASSERT pattern: v.end() used twice -> hoisted local."""
        src = """
        void Fn(vector<int>& v, int x) {
            MILO_ASSERT(std::find(v.begin(), v.end(), x) == v.end(), 42);
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_clusters())
        variants = list(self.pattern.generate(ctx))
        self.assertTrue(len(variants) >= 1, "Should generate at least one variant")
        found = any(
            b"_e0 = v.end()" in v.source or b"auto _e" in v.source
            for v in variants
        )
        self.assertTrue(found, "Should hoist v.end() into a local")
        # Verify the hoisted local replaces both occurrences
        for v in variants:
            if b"auto _e" in v.source:
                text = v.source.decode("utf-8", errors="replace")
                # Should not have two v.end() calls any more
                self.assertLessEqual(
                    text.count("v.end()"), 1,
                    "Both occurrences should be replaced"
                )
                break

    def test_single_call_no_variant(self):
        """No variant for a call that appears only once."""
        src = """
        void Fn(vector<int>& v) {
            auto it = v.end();
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_clusters())
        variants = list(self.pattern.generate(ctx))
        self.assertEqual(len(variants), 0, "No hoisting when call appears only once")

    def test_relevant_clusters(self):
        """relevant() is True when clusters are present."""
        self.assertTrue(self.pattern.relevant(_diag_clusters()))

    def test_registration(self):
        from scripts.permuter.patterns.cache_repeated_call import CacheRepeatedCallPattern as P
        from scripts.permuter.patterns.base import get_pattern
        import scripts.permuter.patterns.cache_repeated_call  # noqa
        p = get_pattern("cache_repeated_call")
        self.assertIsInstance(p, P)


# ---------------------------------------------------------------------------
# symbol_str_compare tests
# ---------------------------------------------------------------------------

class TestSymbolStrCompare(unittest.TestCase):

    def setUp(self):
        self.pattern = SymbolStrComparePattern()

    def test_sym_vs_gNullStr(self):
        """sym != gNullStr -> sym.Str() != gNullStr (gNullStr stays unchanged)."""
        src = """
        void Fn(Symbol sym) {
            if (sym != gNullStr) {
                DoSomething();
            }
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_bl_cmplw())
        variants = list(self.pattern.generate(ctx))
        self.assertTrue(len(variants) >= 1)
        found_str = any(
            b"sym.Str() != gNullStr" in v.source for v in variants
        )
        found_mstr = any(
            b"sym.mStr != gNullStr" in v.source for v in variants
        )
        self.assertTrue(found_str or found_mstr, "Should produce sym.Str() or sym.mStr variant")
        # gNullStr should NOT get .Str() appended
        for v in variants:
            self.assertNotIn(b"gNullStr.Str()", v.source, "gNullStr must not get .Str()")
            self.assertNotIn(b"gNullStr.mStr", v.source, "gNullStr must not get .mStr")

    def test_sym_vs_sym(self):
        """symA == symB -> symA.Str() == symB.Str()"""
        src = """
        void Fn(Symbol symA, Symbol symB) {
            if (symA == symB) {
                DoSomething();
            }
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_bl_cmplw())
        variants = list(self.pattern.generate(ctx))
        self.assertTrue(len(variants) >= 1)
        found = any(
            b"symA.Str() == symB.Str()" in v.source or
            b"symA.mStr == symB.mStr" in v.source
            for v in variants
        )
        self.assertTrue(found, "Should produce both-Str or both-mStr variant")

    def test_already_converted_skipped(self):
        """If .Str() is already present, don't double-add it."""
        src = """
        void Fn(Symbol sym) {
            if (sym.Str() != gNullStr) {
                DoSomething();
            }
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_bl_cmplw())
        variants = list(self.pattern.generate(ctx))
        # sym already has .Str() — should not produce sym.Str().Str()
        for v in variants:
            self.assertNotIn(b".Str().Str()", v.source)
            self.assertNotIn(b".mStr.Str()", v.source)

    def test_milo_macro_skipped(self):
        """Comparisons inside MILO macros are skipped (handled by milo_str_conv)."""
        src = """
        void Fn(Symbol sym) {
            MILO_ASSERT(sym != gNullStr, 42);
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_bl_cmplw())
        variants = list(self.pattern.generate(ctx))
        # Should not fire inside MILO_ASSERT
        for v in variants:
            self.assertNotIn(b"sym.Str() != gNullStr", v.source,
                             "Should not modify comparisons inside MILO macros")

    def test_relevant_bl_cmplw(self):
        """relevant() returns True when bl vs cmplw mismatch present."""
        self.assertTrue(self.pattern.relevant(_diag_bl_cmplw()))

    def test_registration(self):
        from scripts.permuter.patterns.symbol_str_compare import SymbolStrComparePattern as P
        from scripts.permuter.patterns.base import get_pattern
        import scripts.permuter.patterns.symbol_str_compare  # noqa
        p = get_pattern("symbol_str_compare")
        self.assertIsInstance(p, P)


if __name__ == "__main__":
    unittest.main()
