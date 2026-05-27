"""Tests for the wasted-compile optimization.

Two complementary mechanisms drop variants that can never produce a win
*before* the expensive compile:

1. variable_extraction no longer hoists a call out of a macro argument
   (e.g. inside MILO_ASSERT(...)). Those variants always failed to build or
   produced unmatchable stringized text — they were the single largest
   BUILD-FAILED bucket in the stress sweep.

2. A generic, baseline-relative pre-queue syntax probe in the generator drops
   any variant that introduces a NEW tree-sitter parse error vs the baseline.
   Baseline-relative so it never false-drops a compilable variant despite
   tree-sitter's known C++/macro fragility.
"""

from __future__ import annotations

import unittest
from unittest import mock

from scripts.permuter import clang_types
from scripts.permuter.patterns import variable_extraction as ve
from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.patterns.variable_extraction import (
    _inside_macro_argument,
    _int_decl_is_doomed,
    _is_macro_call,
)
from scripts.permuter.generator import (
    _count_parse_errors,
    _syntax_probe_filter,
)
from scripts.permuter.tests.conftest import make_context, diag_always


def _fake_typeinfo(kind, *, is_pointer=False, is_float=False,
                   is_signed_int=False, is_unsigned_int=False, spelling="X"):
    return clang_types.TypeInfo(
        kind=kind, spelling=spelling, is_pointer=is_pointer,
        is_signed_int=is_signed_int, is_unsigned_int=is_unsigned_int,
        is_float=is_float,
    )
from scripts.permuter.types import Variant


def _all_calls(ctx):
    """Return all call_expression nodes under ctx.body_node."""
    out = []

    def walk(node):
        if node.type == "call_expression":
            out.append(node)
        for c in node.children:
            walk(c)

    walk(ctx.body_node)
    return out


class TestVariableExtractionMacroFilter(unittest.TestCase):
    """variable_extraction must not extract calls inside macro arguments."""

    def test_milo_assert_arg_emits_no_extraction(self):
        # The only nested call is mElements.size(), and it lives inside a
        # MILO_ASSERT argument. The pattern must emit ZERO variants for it.
        pattern = get_pattern("variable_extraction")
        ctx = make_context(
            """\
void Foo::Bar() {
    MILO_ASSERT(display < mElements.size(), 0x74);
}
""",
            "Foo::Bar",
            diag_always(),
        )
        variants = list(pattern.generate(ctx))
        self.assertEqual(
            variants, [],
            "variable_extraction hoisted a call out of a MILO_ASSERT arg "
            f"(got {[v.description for v in variants]})",
        )

    def test_milo_warn_nested_call_emits_no_extraction(self):
        pattern = get_pattern("variable_extraction")
        ctx = make_context(
            """\
void Foo::Bar() {
    MILO_WARN("%d", Compute(GetValue()));
}
""",
            "Foo::Bar",
            diag_always(),
        )
        variants = list(pattern.generate(ctx))
        self.assertEqual(variants, [], "extracted a call inside MILO_WARN")

    def test_valid_site_still_extracts(self):
        # A call nested in a plain expression (NOT a macro) must still be a
        # valid extraction site — no false drop.
        pattern = get_pattern("variable_extraction")
        ctx = make_context(
            """\
void Foo::Bar() {
    int x = Compute(GetValue());
}
""",
            "Foo::Bar",
            diag_always(),
        )
        variants = list(pattern.generate(ctx))
        self.assertTrue(
            variants,
            "valid extraction site (GetValue() inside a plain initializer) "
            "produced no variants — false drop",
        )
        self.assertTrue(
            any(b"GetValue()" in v.source for v in variants)
        )

    def test_macro_and_valid_site_coexist(self):
        # Same function with both a doomed macro-arg call and a valid call.
        # Only the valid one should be extracted.
        pattern = get_pattern("variable_extraction")
        ctx = make_context(
            """\
void Foo::Bar() {
    MILO_ASSERT(display < mElements.size(), 0x74);
    if (a->Foo()->Bar() > 3) { return; }
}
""",
            "Foo::Bar",
            diag_always(),
        )
        variants = list(pattern.generate(ctx))
        self.assertTrue(variants, "valid method-chain site was dropped")
        # None of the emitted variants should hoist mElements.size().
        for v in variants:
            self.assertNotIn(
                b"= mElements.size();", v.source,
                "hoisted the MILO_ASSERT-arg call despite the filter",
            )

    def test_is_macro_call_recognizes_all_caps(self):
        from scripts.permuter.patterns.variable_extraction import _callee_text
        ctx = make_context(
            """\
void Foo::Bar() {
    MILO_ASSERT(x, 1);
    REGISTER_THING(y);
    helper(z);
}
""",
            "Foo::Bar",
            diag_always(),
        )
        # callee text of every call_expression flagged as a macro
        macro_callees = {
            _callee_text(n) for n in _all_calls(ctx) if _is_macro_call(n)
        }
        self.assertIn(b"MILO_ASSERT", macro_callees)
        self.assertIn(b"REGISTER_THING", macro_callees)
        # A normal lower-case function call is NOT a macro.
        self.assertNotIn(b"helper", macro_callees)

    def test_filter_disabled_by_env(self):
        # The escape hatch makes the filter inert (extraction reappears).
        import os
        pattern = get_pattern("variable_extraction")
        ctx = make_context(
            """\
void Foo::Bar() {
    MILO_ASSERT(display < mElements.size(), 0x74);
}
""",
            "Foo::Bar",
            diag_always(),
        )
        prev = os.environ.get("PERMUTER_VAREXT_MACRO_FILTER")
        os.environ["PERMUTER_VAREXT_MACRO_FILTER"] = "0"
        try:
            variants = list(pattern.generate(ctx))
        finally:
            if prev is None:
                os.environ.pop("PERMUTER_VAREXT_MACRO_FILTER", None)
            else:
                os.environ["PERMUTER_VAREXT_MACRO_FILTER"] = prev
        self.assertTrue(
            variants,
            "with the filter disabled, the macro-arg site should extract again",
        )


class TestSyntaxProbe(unittest.TestCase):
    """The generic pre-queue parse-error probe must be baseline-relative."""

    def test_clean_source_has_zero_errors(self):
        src = b"void f() { int x = g(); }\n"
        self.assertEqual(_count_parse_errors(src), 0)

    def test_broken_source_has_errors(self):
        # Missing closing brace / dangling operator → parse errors.
        src = b"void f() { int x = ; }\n"
        self.assertGreater(_count_parse_errors(src), 0)

    def test_probe_drops_new_error_variant(self):
        baseline = b"void f() { int x = g(); }\n"
        good = Variant("good", "p", "ok", b"void f() { int x = h(); }\n")
        bad = Variant("bad", "p", "broken", b"void f() { int x = ; }\n")
        survivors = list(_syntax_probe_filter([good, bad], baseline))
        names = [v.name for v in survivors]
        self.assertIn("good", names)
        self.assertNotIn("bad", names)

    def test_probe_keeps_variant_when_baseline_already_errors(self):
        # Baseline-relative: if the baseline already trips tree-sitter (macro
        # fragility), a variant with the SAME error count must NOT be dropped.
        baseline = b"void f() { BEGIN_THING int x = ; }\n"
        base_errs = _count_parse_errors(baseline)
        self.assertGreater(base_errs, 0, "fixture must pre-trip tree-sitter")
        # A variant that keeps the same (pre-existing) error must survive.
        variant = Variant(
            "v", "p", "same-errs",
            b"void f() { BEGIN_THING int y = ; }\n",
        )
        survivors = list(_syntax_probe_filter([variant], baseline))
        self.assertEqual(
            [v.name for v in survivors], ["v"],
            "baseline-relative probe false-dropped a variant whose error "
            "count did not increase over baseline",
        )

    def test_probe_passes_noop_variant(self):
        baseline = b"void f() { int x = g(); }\n"
        noop = Variant("noop", "p", "unchanged", baseline)
        survivors = list(_syntax_probe_filter([noop], baseline))
        self.assertEqual([v.name for v in survivors], ["noop"])


class TestIntDeclDoomed(unittest.TestCase):
    """variable_extraction must not emit `int _tmp = <call>` for non-int returns.

    Under mwcc (C++98) the untyped form is spelled `int _tmp = <call>`, which is
    a hard compile error for record/void/pointer returns and a never-matching
    truncation for float returns. These were a large BUILD-FAILED bucket.
    """

    TK = clang_types.TypeKind

    def test_doomed_predicate(self):
        self.assertTrue(_int_decl_is_doomed(_fake_typeinfo(self.TK.RECORD)))
        self.assertTrue(_int_decl_is_doomed(_fake_typeinfo(self.TK.VOID)))
        self.assertTrue(_int_decl_is_doomed(
            _fake_typeinfo(self.TK.POINTER, is_pointer=True)))
        self.assertTrue(_int_decl_is_doomed(
            _fake_typeinfo(self.TK.FLOAT, is_float=True)))
        # Safe: int/bool/enum can be spelled `int`.
        self.assertFalse(_int_decl_is_doomed(
            _fake_typeinfo(self.TK.SIGNED_INT, is_signed_int=True)))
        self.assertFalse(_int_decl_is_doomed(
            _fake_typeinfo(self.TK.UNSIGNED_INT, is_unsigned_int=True)))
        self.assertFalse(_int_decl_is_doomed(_fake_typeinfo(self.TK.BOOL)))
        self.assertFalse(_int_decl_is_doomed(_fake_typeinfo(self.TK.ENUM)))

    def test_record_return_skips_untyped_int(self):
        # Mock libclang to report the call returns a record (class) type.
        ctx = make_context(
            """\
void Foo::Bar() {
    int x = Compute(GetValue());
}
""",
            "Foo::Bar",
            diag_always(),
        )
        pattern = get_pattern("variable_extraction")
        with mock.patch.object(
            ve, "_resolve_return_type",
            return_value=_fake_typeinfo(self.TK.RECORD, spelling="Symbol"),
        ):
            variants = list(pattern.generate(ctx))
        # No `int _tmpN = ...` line may appear — that form is doomed for a record.
        for v in variants:
            text = v.source.decode("utf-8")
            self.assertNotRegex(
                text, r"\bint _tmp\d+ =",
                f"emitted a doomed `int _tmp = <record call>` ({v.name})",
            )

    def test_int_return_keeps_untyped(self):
        # When the call returns int, the untyped form is valid and kept.
        ctx = make_context(
            """\
void Foo::Bar() {
    int x = Compute(GetValue());
}
""",
            "Foo::Bar",
            diag_always(),
        )
        pattern = get_pattern("variable_extraction")
        with mock.patch.object(
            ve, "_resolve_return_type",
            return_value=_fake_typeinfo(self.TK.SIGNED_INT, is_signed_int=True),
        ):
            variants = list(pattern.generate(ctx))
        self.assertTrue(
            any("int _tmp" in v.source.decode("utf-8") for v in variants),
            "int-returning call should still produce an untyped int extraction",
        )

    def test_unresolved_type_keeps_untyped(self):
        # libclang unavailable / unresolved → no false drop, keep untyped form.
        ctx = make_context(
            """\
void Foo::Bar() {
    int x = Compute(GetValue());
}
""",
            "Foo::Bar",
            diag_always(),
        )
        pattern = get_pattern("variable_extraction")
        with mock.patch.object(ve, "_resolve_return_type", return_value=None):
            variants = list(pattern.generate(ctx))
        self.assertTrue(
            any("int _tmp" in v.source.decode("utf-8") for v in variants),
            "unresolved type must NOT drop the untyped form (no false drops)",
        )


if __name__ == "__main__":
    unittest.main()
