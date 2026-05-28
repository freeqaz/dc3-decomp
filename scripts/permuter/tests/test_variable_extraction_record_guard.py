"""Regression tests for the variable_extraction record-return syntactic guard.

Wave F3 — without a libclang compdb available, the pattern's
``return_type is None`` branch previously emitted ``int _tmp = X(...);``
even when ``X`` was a constructor (record return). MWCC rejects that, so
the variant was dispatched to the build queue only to BUILD FAILED.

The fix adds a syntactic fallback (`_syntactic_record_return`) that flags
PascalCase callees lacking a known scalar-returning prefix
(Get/Find/Compute/Is/Has/...). This module asserts the guard:

* rejects ``RGGemInfo(tick, ...)`` constructor-style RHS extractions, and
* still emits useful extractions for scalar-returning callees like
  ``GetFret(data)``.

The trigger function was
``SongParser::HandleRGGemStart``, originating in the Wave E2c batch_auto
BUILD FAILED sweep on ``system/beatmatch/SongParser``.
"""

from __future__ import annotations

import unittest

from scripts.permuter.patterns.variable_extraction import (
    VariableExtractionPattern,
    _syntactic_record_return,
)
from scripts.permuter.extractor import _PARSER
from scripts.permuter.tests.conftest import diag_with_clusters, make_context

_PATTERN = VariableExtractionPattern()


def _variant_descriptions(source: str, func: str = "f") -> list[str]:
    ctx = make_context(source, func, diag_with_clusters())
    return [v.description for v in _PATTERN.generate(ctx)]


def _variant_sources(source: str, func: str = "f") -> list[bytes]:
    ctx = make_context(source, func, diag_with_clusters())
    return [v.source for v in _PATTERN.generate(ctx)]


def _first_call(source: str):
    """Return the first call_expression node in *source*."""
    tree = _PARSER.parse(source.encode("utf-8"))
    stack = [tree.root_node]
    while stack:
        node = stack.pop()
        if node.type == "call_expression":
            return node
        stack.extend(node.children)
    raise AssertionError(f"no call_expression found in: {source!r}")


class TestSyntacticRecordReturn(unittest.TestCase):
    """Direct unit tests for the _syntactic_record_return helper."""

    def test_constructor_pascal_case_flagged(self):
        # Bare ClassName-shaped callee — likely a constructor.
        for callee in ("RGGemInfo", "Vector3", "String", "MyType", "Matrix4x4"):
            with self.subTest(callee=callee):
                call = _first_call(f"void f() {{ x = {callee}(a, b); }}")
                self.assertTrue(_syntactic_record_return(call))

    def test_scalar_prefix_not_flagged(self):
        # PascalCase with a known scalar-returning prefix — keep emitting.
        for callee in (
            "GetFret", "FindIndex", "ComputeSize", "CountThings", "HasFlag",
            "IsActive", "ShouldRetry", "ReadBits", "SizeOf", "NumElements",
        ):
            with self.subTest(callee=callee):
                call = _first_call(f"void f() {{ x = {callee}(a); }}")
                self.assertFalse(_syntactic_record_return(call))

    def test_lowercase_callee_not_flagged(self):
        # Built-in casts / free functions / methods routed through a
        # field_expression all get a pass (only bare PascalCase identifiers
        # are matched).
        for callee in ("strcmp", "printf", "memcpy", "atoi"):
            with self.subTest(callee=callee):
                call = _first_call(f"void f() {{ x = {callee}(a); }}")
                self.assertFalse(_syntactic_record_return(call))

    def test_method_call_not_flagged(self):
        # obj.Method(...) has a field_expression function — not flagged.
        call = _first_call("void f() { x = obj.Method(a); }")
        self.assertFalse(_syntactic_record_return(call))


class TestVariantEmission(unittest.TestCase):
    """End-to-end pattern emission, mirroring the SongParser bug shape."""

    def test_constructor_rhs_extraction_skipped(self):
        """The HandleRGGemStart bug shape: assignment to a record element from
        a constructor call. The untyped ``int _tmp = RGGemInfo(...)`` extract
        must NOT be emitted (it's a hard compile error)."""
        src = """\
void f(SongParser::DifficultyInfo &info, int tick, unsigned char uc,
       unsigned char data, unsigned char channel) {
    info.mRGGemsInfo[uc - 24] = RGGemInfo(tick, info.mActivePlayers,
                                          GetFret(data), channel);
}
"""
        for desc in _variant_descriptions(src):
            self.assertNotIn(
                "into auto _tmp",
                desc,
                f"untyped extraction emitted for record-constructor RHS: {desc}",
            )

    def test_scalar_call_extraction_still_emitted(self):
        """Counter-test: a PascalCase-but-scalar-returning callee like
        GetFret should still be extractable. Guards against over-blocking.
        """
        src = """\
int f(int data) {
    int n = 1 + GetFret(data);
    return n;
}
"""
        # `GetFret` has a scalar-returning prefix, so the syntactic guard
        # must NOT block the extraction.
        descs = _variant_descriptions(src)
        self.assertTrue(
            any("GetFret(data)" in d and "into auto _tmp" in d for d in descs),
            f"GetFret extraction lost to over-blocking. descs={descs}",
        )

    def test_method_chain_extraction_still_emitted(self):
        """Counter-test: method-call extraction on a field_expression is
        unaffected by the new guard."""
        src = """\
int f(MyObj &obj) {
    return obj.GetCount() + 1;
}
"""
        descs = _variant_descriptions(src)
        self.assertTrue(
            any("obj.GetCount()" in d for d in descs),
            f"method-call extraction lost. descs={descs}",
        )


if __name__ == "__main__":
    unittest.main()
