"""Tests for the argument_swap pattern.

Covers the tighter type+overload guards added to prevent the 54.5% build-
failure rate caused by swapping typed cast arguments with incompatible types,
and pointer identifiers with non-pointer identifiers.
"""

from __future__ import annotations

import unittest

from scripts.permuter.patterns.argument_swap import (
    ArgumentSwapPattern,
    _args_type_compatible,
    _ident_is_pointer_like,
)
from scripts.permuter.tests.conftest import diag_with_clusters, make_context

_PATTERN = ArgumentSwapPattern()


def _variants(source: str, func: str = "f") -> list[str]:
    """Return list of variant descriptions generated for a function."""
    ctx = make_context(source, func, diag_with_clusters())
    return [v.description for v in _PATTERN.generate(ctx)]


# ---------------------------------------------------------------------------
# Cast-expression guards
# ---------------------------------------------------------------------------

class TestCastExpressionGuard(unittest.TestCase):
    """Cast expressions carry explicit type annotations; swapping with a
    differently-typed argument always causes a build failure."""

    def test_rejects_cast_vs_expr(self):
        """(VShaderConstant)0 vs identifier — different types, must reject."""
        src = """\
void f() {
    SetShaderConst((VShaderConstant)0, myBuffer);
}
"""
        self.assertEqual(_variants(src), [], "cast+expr must be rejected")

    def test_rejects_cast_vs_number(self):
        """(RndRenderState::ClampMode)2 vs number literal — must reject."""
        src = """\
void f() {
    SetClamp(9, (RndRenderState::ClampMode)2);
}
"""
        self.assertEqual(_variants(src), [], "cast+number must be rejected")

    def test_rejects_cast_different_types(self):
        """Two casts to different types — incompatible, must reject."""
        src = """\
void f() {
    Bind((PShaderConstant)8, (const Vector4 &)mDepthRangeValues);
}
"""
        self.assertEqual(_variants(src), [], "cast+cast(different types) must be rejected")

    def test_allows_cast_same_type(self):
        """Two casts to the same type — may be legally swappable."""
        src = """\
void f() {
    Blend((float)alpha, (float)beta);
}
"""
        variants = _variants(src)
        self.assertEqual(len(variants), 1, "cast+cast(same type) should produce one variant")

    def test_allows_expr_expr(self):
        """Two plain identifiers of similar pointer-ness — still allowed."""
        src = """\
void f(float a, float b) {
    Clamp(a, b);
}
"""
        variants = _variants(src)
        self.assertEqual(len(variants), 1, "expr+expr should produce one variant")


# ---------------------------------------------------------------------------
# Pointer identifier guard
# ---------------------------------------------------------------------------

class TestPointerIdentifierGuard(unittest.TestCase):
    """When one identifier is a pointer and the other is not (e.g. a Symbol
    global), swapping causes a build failure."""

    def test_rejects_pointer_vs_non_pointer_ident(self):
        """RemoveSink(mObj*, gNullStr) — pointer vs Symbol, must reject."""
        src = """\
void f() {
    StorePreviewMgr *mStorePreviewMgr = nullptr;
    RemoveSink(mStorePreviewMgr, gNullStr);
}
"""
        variants = _variants(src)
        self.assertEqual(variants, [], "pointer+non-pointer identifier swap must be rejected")

    def test_allows_two_pointers(self):
        """Two pointer identifiers of the same type should be swapped."""
        src = """\
void f() {
    Foo *pA = nullptr;
    Foo *pB = nullptr;
    Transfer(pA, pB);
}
"""
        variants = _variants(src)
        self.assertEqual(len(variants), 1, "pointer+pointer should produce one variant")

    def test_ident_is_pointer_like_arrow(self):
        """_ident_is_pointer_like via arrow usage."""
        from scripts.permuter.types import FunctionContext
        from pathlib import Path
        import textwrap

        src = textwrap.dedent("""\
        void f() {
            obj->Method();
        }
        """).encode("utf-8")

        # Build a minimal FunctionContext-like object with just file_source
        class FakeCtx:
            file_source = src

        self.assertTrue(_ident_is_pointer_like("obj", FakeCtx()))
        self.assertFalse(_ident_is_pointer_like("val", FakeCtx()))

    def test_ident_is_pointer_like_declaration(self):
        """_ident_is_pointer_like via pointer declaration."""
        class FakeCtx:
            file_source = b"Node *ptr = getNode();"

        self.assertTrue(_ident_is_pointer_like("ptr", FakeCtx()))
        self.assertFalse(_ident_is_pointer_like("x", FakeCtx()))


# ---------------------------------------------------------------------------
# Existing guard regression tests
# ---------------------------------------------------------------------------

class TestExistingGuardsStillWork(unittest.TestCase):
    """The new guards must not weaken existing type-safety checks."""

    def test_rejects_string_vs_expr(self):
        """String literal vs identifier — pre-existing guard must hold."""
        src = """\
void f() {
    strcmp(name, "foo");
}
"""
        self.assertEqual(_variants(src), [], "string+expr must be rejected")

    def test_rejects_number_vs_expr(self):
        """Number literal vs identifier — pre-existing guard must hold."""
        src = """\
void f() {
    foo(42, ptr);
}
"""
        self.assertEqual(_variants(src), [], "number+expr must be rejected")

    def test_rejects_this_swap(self):
        """this is never swappable."""
        src = """\
void f() {
    bar(this, other);
}
"""
        self.assertEqual(_variants(src), [], "this+expr must be rejected")


# ---------------------------------------------------------------------------
# Win-preservation regression tests
# ---------------------------------------------------------------------------

class TestWinPatternsPreserved(unittest.TestCase):
    """All historical winning swap categories must still be generated."""

    def test_expr_expr_identifiers(self):
        """identifier + identifier (same signedness context) — classic win."""
        src = """\
void f(float practiceStart, float practiceEnd) {
    Clamp(practiceStart, practiceEnd);
}
"""
        variants = _variants(src)
        self.assertEqual(len(variants), 1)
        self.assertIn("practiceStart", variants[0])
        self.assertIn("practiceEnd", variants[0])

    def test_number_number(self):
        """Two number literals — allowed (e.g. Swap(0, 1) -> Swap(1, 0))."""
        src = """\
void f() {
    Foo(0, 1);
}
"""
        variants = _variants(src)
        self.assertEqual(len(variants), 1)

    def test_call_expr_and_identifier(self):
        """call_expression + identifier — wins like UIManager::OnIsResource."""
        src = """\
void f() {
    Compare(GetPath(arr->File()), arr->Str(1));
}
"""
        variants = _variants(src)
        self.assertEqual(len(variants), 1, "call_expr+call_expr should produce one variant")

    def test_end_begin_swap(self):
        """begin()+end() swap — wins like DingoServer::AddDelayedCalls."""
        src = """\
void f() {
    std::copy(mJobs.end(), mJobs.begin());
}
"""
        variants = _variants(src)
        self.assertEqual(len(variants), 1)


if __name__ == "__main__":
    unittest.main()
