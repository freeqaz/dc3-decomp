"""Tests for the positive-Symbol gate in symbol_str_compare.

These tests verify the _looks_like_symbol heuristic correctly:
- REJECTS identifiers with no Symbol evidence (int, bool, pointer, enum)
- ACCEPTS identifiers declared as Symbol in the TU
- ACCEPTS identifiers whose cross-TU usage (.Str()/.mStr) proves they're Symbol
- ACCEPTS function-param Symbols (e.g. Symbol sym in signature)
- ACCEPTS field expressions where the field name is a proven Symbol member
- Does NOT fire on non-Symbol comparisons (reduces 92% build-failure rate)
"""

from __future__ import annotations

import sys
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.patterns.symbol_str_compare import (
    _looks_like_symbol,
    _ident_declared_as_symbol,
    _ident_declared_as_non_symbol,
    SymbolStrComparePattern,
)
from scripts.permuter.extractor import _PARSER, _get_function_name
from scripts.permuter.types import Cluster, Diagnosis, DiffOp, FunctionContext, SwapInfo


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


def _diag_bl_strcmp() -> Diagnosis:
    """Diagnosis with bl strcmp vs cmplw — canonical symbol_str_compare trigger."""
    return Diagnosis(
        total_instructions=30,
        match_counts={},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[DiffOp(0, "bl", "cmplw", "strcmp", "")],
        clusters=[],
        noise_explained=0,
        noise_total=1,
        replace_real=1,
    )


def _diag_bl_symbol_eq() -> Diagnosis:
    """Diagnosis with bl __eq__6SymbolFRC6Symbol vs cmplw."""
    return Diagnosis(
        total_instructions=30,
        match_counts={},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[DiffOp(0, "bl", "cmplw", "__eq__6SymbolFRC6Symbol", "")],
        clusters=[],
        noise_explained=0,
        noise_total=1,
        replace_real=1,
    )


class FakeNode:
    """Minimal tree-sitter Node stub for unit testing _looks_like_symbol."""

    def __init__(self, ntype: str, text: bytes = b"", parent=None):
        self.type = ntype
        self.text = text
        self.start_byte = 0
        self.end_byte = len(text)
        self.parent = parent
        self._children: dict[str, "FakeNode"] = {}

    def child_by_field_name(self, name: str):
        return self._children.get(name)


def _ident_node(text: bytes) -> FakeNode:
    return FakeNode("identifier", text)


def _field_expr_node(field_text: bytes) -> FakeNode:
    """Minimal field_expression node (obj.field)."""
    parent = FakeNode("field_expression")
    field = FakeNode("identifier", field_text)
    parent._children["field"] = field
    return parent


# ---------------------------------------------------------------------------
# Tests: _ident_declared_as_symbol / _ident_declared_as_non_symbol
# ---------------------------------------------------------------------------

class TestIdentDeclaredAsSymbol(unittest.TestCase):
    """Unit tests for the TU grep helpers."""

    def test_symbol_param_declaration(self):
        """'Symbol sym' in function signature -> declared as Symbol."""
        tu = "void Fn(Symbol sym) { }"
        self.assertTrue(_ident_declared_as_symbol("sym", tu))

    def test_symbol_ref_param(self):
        """'const Symbol& sym' -> declared as Symbol."""
        tu = "void Fn(const Symbol& sym) { }"
        self.assertTrue(_ident_declared_as_symbol("sym", tu))

    def test_symbol_ref_param_no_const(self):
        """'Symbol& sym' -> declared as Symbol."""
        tu = "void Fn(Symbol& sym) { }"
        self.assertTrue(_ident_declared_as_symbol("sym", tu))

    def test_symbol_local_declaration(self):
        """'Symbol dataSym = ...' -> declared as Symbol."""
        tu = "void Fn() { Symbol dataSym = provider->DataSymbol(i); }"
        self.assertTrue(_ident_declared_as_symbol("dataSym", tu))

    def test_cross_tu_str_usage(self):
        """'ident.Str()' elsewhere in TU proves ident is Symbol."""
        tu = "void Fn(Symbol s) { const char* p = s.Str(); if (s == gNullStr) {} }"
        self.assertTrue(_ident_declared_as_symbol("s", tu))

    def test_cross_tu_mstr_usage(self):
        """'ident.mStr' elsewhere in TU proves ident is Symbol."""
        tu = "void Fn(Symbol s) { const char* p = s.mStr; }"
        self.assertTrue(_ident_declared_as_symbol("s", tu))

    def test_int_variable_not_symbol(self):
        """'int numItems' is NOT declared as Symbol."""
        tu = "void Fn() { int numItems = NumItems(); }"
        self.assertFalse(_ident_declared_as_symbol("numItems", tu))

    def test_bool_variable_not_symbol(self):
        """'bool inControllerMode' is NOT declared as Symbol."""
        tu = "void Fn() { bool inControllerMode = true; }"
        self.assertFalse(_ident_declared_as_symbol("inControllerMode", tu))

    def test_unrelated_identifier(self):
        """Identifier not mentioned as Symbol anywhere -> False."""
        tu = "void Fn(int x) { }"
        self.assertFalse(_ident_declared_as_symbol("x", tu))


class TestIdentDeclaredAsNonSymbol(unittest.TestCase):
    """Unit tests for the negative gate."""

    def test_arrow_usage_is_pointer(self):
        """'obj->member' pattern means obj is a pointer."""
        tu = "void Fn(Skeleton* skel) { skel->IsValid(); }"
        self.assertTrue(_ident_declared_as_non_symbol("skel", tu))

    def test_int_declaration(self):
        """'int numItems' -> non-Symbol."""
        tu = "void Fn() { int numItems = 5; }"
        self.assertTrue(_ident_declared_as_non_symbol("numItems", tu))

    def test_bool_declaration(self):
        """'bool handValid' -> non-Symbol."""
        tu = "void Fn() { bool handValid = false; }"
        self.assertTrue(_ident_declared_as_non_symbol("handValid", tu))

    def test_float_declaration(self):
        """'float pct' -> non-Symbol."""
        tu = "void Fn() { float pct = 0.0f; }"
        self.assertTrue(_ident_declared_as_non_symbol("pct", tu))

    def test_pointer_declaration(self):
        """'Provider* provider' -> non-Symbol."""
        tu = "void Fn(Provider* provider) { }"
        self.assertTrue(_ident_declared_as_non_symbol("provider", tu))

    def test_symbol_not_rejected(self):
        """'Symbol sym' does NOT trigger the non-Symbol gate."""
        tu = "void Fn(Symbol sym) { }"
        self.assertFalse(_ident_declared_as_non_symbol("sym", tu))


# ---------------------------------------------------------------------------
# Tests: _looks_like_symbol (integration of positive + negative gates)
# ---------------------------------------------------------------------------

class TestLooksLikeSymbol(unittest.TestCase):
    """Integration tests for _looks_like_symbol with real TU source."""

    def test_accepts_symbol_param(self):
        """'Symbol sym' parameter is accepted as Symbol."""
        tu = "void Fn(Symbol sym) { if (sym == gNullStr) {} }"
        node = _ident_node(b"sym")
        self.assertTrue(_looks_like_symbol(node, b"sym", tu))

    def test_accepts_symbol_local(self):
        """Locally declared 'Symbol dataSym' accepted."""
        tu = "void Fn() { Symbol dataSym = p->DataSymbol(i); if (dataSym == gNullStr) {} }"
        node = _ident_node(b"dataSym")
        self.assertTrue(_looks_like_symbol(node, b"dataSym", tu))

    def test_accepts_symbol_proven_by_str_usage(self):
        """Identifier used as 'ident.Str()' elsewhere in TU is accepted."""
        tu = "void Fn(Symbol s) { const char* p = s.Str(); if (s == gNullStr) {} }"
        node = _ident_node(b"s")
        self.assertTrue(_looks_like_symbol(node, b"s", tu))

    def test_rejects_int_operand(self):
        """'int numItems' declared in TU -> rejected (would produce int_var.Str())."""
        tu = "void Fn() { int numItems = NumItems(); if (numItems == 1) {} }"
        node = _ident_node(b"numItems")
        self.assertFalse(_looks_like_symbol(node, b"numItems", tu))

    def test_rejects_bool_operand(self):
        """'bool handValid' -> rejected."""
        tu = "void Fn() { bool handValid = false; if (handValid == true) {} }"
        node = _ident_node(b"handValid")
        self.assertFalse(_looks_like_symbol(node, b"handValid", tu))

    def test_rejects_float_operand(self):
        """'float pct' -> rejected."""
        tu = "void Fn() { float pct = 0.5f; if (pct == 0.0f) {} }"
        node = _ident_node(b"pct")
        self.assertFalse(_looks_like_symbol(node, b"pct", tu))

    def test_rejects_pointer_operand_arrow(self):
        """Identifier used with '->' in TU is rejected (it's a pointer)."""
        tu = "void Fn(Provider* p) { p->IsActive(0); if (p == nullptr) {} }"
        node = _ident_node(b"p")
        self.assertFalse(_looks_like_symbol(node, b"p", tu))

    def test_rejects_m_upper_int_member(self):
        """mScrollDir (int member starting m+Upper) is rejected — no Symbol evidence."""
        # Old code accepted ALL mFoo as potential Symbols — now we require TU evidence
        tu = "void Fn() { if (mScrollBehavior.mScrollDir == 0) {} }"
        node = _ident_node(b"mScrollDir")
        self.assertFalse(_looks_like_symbol(node, b"mScrollDir", tu))

    def test_rejects_null_sentinel(self):
        """gNullStr is always rejected (char* sentinel)."""
        tu = "void Fn(Symbol sym) { if (sym == gNullStr) {} }"
        node = _ident_node(b"gNullStr")
        self.assertFalse(_looks_like_symbol(node, b"gNullStr", tu))

    def test_rejects_string_literal(self):
        """String literal node type is always rejected."""
        tu = 'void Fn(Symbol sym) { if (sym == "foo") {} }'
        node = FakeNode("string_literal", b'"foo"')
        self.assertFalse(_looks_like_symbol(node, b'"foo"', tu))

    def test_accepts_field_expression_with_symbol_member(self):
        """field_expression where field is a proven Symbol member is accepted."""
        # mSomeSym is declared as Symbol somewhere in TU
        tu = "void Fn() { Symbol mSomeSym; if (mSomeSym == gNullStr) {} }"
        node = _field_expr_node(b"mSomeSym")
        self.assertTrue(_looks_like_symbol(node, b"obj.mSomeSym", tu))

    def test_rejects_field_expression_non_symbol_field(self):
        """field_expression where field is not a proven Symbol is rejected."""
        # mScrollDir is an int member — no Symbol declaration in TU
        tu = "void Fn() { if (mBehavior.mScrollDir == 0) {} }"
        node = _field_expr_node(b"mScrollDir")
        self.assertFalse(_looks_like_symbol(node, b"mBehavior.mScrollDir", tu))

    def test_rejects_unknown_identifier_no_evidence(self):
        """Unknown identifier with no TU Symbol evidence is rejected (fail-closed)."""
        tu = "void Fn() { if (foo == bar) {} }"
        node = _ident_node(b"foo")
        self.assertFalse(_looks_like_symbol(node, b"foo", tu))

    def test_accepts_call_classname(self):
        """ClassName() call is accepted (returns Symbol)."""
        tu = "void Fn(Hmx::Object* obj) { if (obj->ClassName() == gNullStr) {} }"
        # Build a minimal call_expression node
        call_node = FakeNode("call_expression", b"obj->ClassName()")
        func_node = FakeNode("field_expression")
        field = FakeNode("identifier", b"ClassName")
        func_node._children["field"] = field
        call_node._children["function"] = func_node
        self.assertTrue(_looks_like_symbol(call_node, b"obj->ClassName()", tu))


# ---------------------------------------------------------------------------
# Integration tests: generate() produces variants only for Symbol operands
# ---------------------------------------------------------------------------

class TestGeneratePositiveGate(unittest.TestCase):
    """End-to-end tests ensuring generate() only fires on confirmed Symbols."""

    def setUp(self):
        self.pattern = SymbolStrComparePattern()

    def test_generates_for_symbol_param(self):
        """generate() fires when sym is declared 'Symbol sym' in function."""
        src = """
        void Fn(Symbol sym) {
            if (sym != gNullStr) {
                DoSomething();
            }
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_bl_strcmp())
        variants = list(self.pattern.generate(ctx))
        self.assertGreater(len(variants), 0, "Should generate variants for Symbol param")
        found = any(b"sym.Str()" in v.source or b"sym.mStr" in v.source for v in variants)
        self.assertTrue(found, "Variant must contain sym.Str() or sym.mStr")

    def test_no_variants_for_int_comparison(self):
        """generate() does NOT fire when only int variables are compared."""
        src = """
        void Fn() {
            int numItems = NumItems();
            if (numItems == 1) {
                DoSomething();
            }
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_bl_strcmp())
        variants = list(self.pattern.generate(ctx))
        # numItems is declared as int — should produce no Symbol variants
        bad = [v for v in variants if b"numItems.Str()" in v.source or b"numItems.mStr" in v.source]
        self.assertEqual(len(bad), 0, f"Should not emit .Str() on int: {[v.description for v in bad]}")

    def test_no_variants_for_bool_comparison(self):
        """generate() does NOT fire when only bool variables are compared."""
        src = """
        void Fn() {
            bool handValid = IsHandValid();
            if (handValid == true) {
                DoSomething();
            }
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_bl_strcmp())
        variants = list(self.pattern.generate(ctx))
        bad = [v for v in variants if b"handValid.Str()" in v.source or b"handValid.mStr" in v.source]
        self.assertEqual(len(bad), 0, "Should not emit .Str() on bool")

    def test_no_variants_for_mscrolldir_int(self):
        """mScrollDir (m+Upper but int) no longer triggers false-positive."""
        src = """
        void Fn() {
            int mScrollDir = 0;
            if (mScrollDir == 0) {
                DoSomething();
            }
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_bl_strcmp())
        variants = list(self.pattern.generate(ctx))
        bad = [v for v in variants if b"mScrollDir.Str()" in v.source or b"mScrollDir.mStr" in v.source]
        self.assertEqual(len(bad), 0, "mScrollDir declared int must not get .Str()")

    def test_generates_for_symbol_local(self):
        """generate() fires when sym is declared locally as Symbol."""
        src = """
        void Fn() {
            Symbol sym = provider->DataSymbol(i);
            if (sym == gNullStr) {
                DoSomething();
            }
        }
        """
        ctx = _make_ctx(src, "Fn", _diag_bl_strcmp())
        variants = list(self.pattern.generate(ctx))
        self.assertGreater(len(variants), 0, "Should generate variants for Symbol local")
        found = any(b"sym.Str()" in v.source or b"sym.mStr" in v.source for v in variants)
        self.assertTrue(found)

    def test_relevant_bl_strcmp(self):
        """relevant() returns True for bl strcmp vs cmplw mismatch."""
        self.assertTrue(self.pattern.relevant(_diag_bl_strcmp()))

    def test_relevant_bl_symbol_eq(self):
        """relevant() returns True for bl __eq__6SymbolFRC6Symbol mismatch."""
        self.assertTrue(self.pattern.relevant(_diag_bl_symbol_eq()))


if __name__ == "__main__":
    unittest.main()
