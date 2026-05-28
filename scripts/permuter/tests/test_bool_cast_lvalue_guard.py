"""Regression tests for the bool_cast assignment-LHS syntactic guard.

Wave F3 — Pattern 3 of bool_cast wrapped any assignment RHS that was a
call in ``bool(...)``. That works when the LHS is a scalar but is a hard
compile error when the LHS is a record-typed lvalue (``arr[i] = bool(X)``
or ``obj.field = bool(X)``).

The fix adds ``_bool_assignable_lvalue`` — only ``identifier`` LHS shapes
keep the wrap path. Subscript and member-access LHS shapes are rejected
syntactically (no libclang required).

Trigger: ``SongParser::HandleRGGemStart``'s
``info.mRGGemsInfo[uc - 24] = RGGemInfo(...)`` line, originating in the
Wave E2c batch_auto BUILD FAILED sweep.
"""

from __future__ import annotations

import unittest

from scripts.permuter.patterns.bool_cast import (
    BoolCastPattern,
    _bool_assignable_lvalue,
)
from scripts.permuter.extractor import _PARSER
from scripts.permuter.tests.conftest import diag_with_replace_real, make_context

_PATTERN = BoolCastPattern()


def _variant_descriptions(source: str, func: str = "f") -> list[str]:
    ctx = make_context(source, func, diag_with_replace_real())
    return [v.description for v in _PATTERN.generate(ctx)]


def _first_assignment_lhs(source: str):
    """Return the LHS node of the first assignment_expression."""
    tree = _PARSER.parse(source.encode("utf-8"))
    stack = [tree.root_node]
    while stack:
        node = stack.pop()
        if node.type == "assignment_expression":
            return node.child_by_field_name("left")
        stack.extend(node.children)
    raise AssertionError(f"no assignment_expression found in: {source!r}")


class TestBoolAssignableLvalue(unittest.TestCase):
    """Direct unit tests for the _bool_assignable_lvalue helper."""

    def test_identifier_allowed(self):
        lhs = _first_assignment_lhs("void f() { flag = call(); }")
        self.assertTrue(_bool_assignable_lvalue(lhs))

    def test_subscript_rejected(self):
        lhs = _first_assignment_lhs("void f() { arr[i] = call(); }")
        self.assertFalse(_bool_assignable_lvalue(lhs))

    def test_field_access_rejected(self):
        lhs = _first_assignment_lhs("void f() { obj.field = call(); }")
        self.assertFalse(_bool_assignable_lvalue(lhs))

    def test_arrow_field_rejected(self):
        lhs = _first_assignment_lhs("void f() { ptr->field = call(); }")
        self.assertFalse(_bool_assignable_lvalue(lhs))


class TestPatternEmission(unittest.TestCase):
    """End-to-end variant emission."""

    def test_subscript_lhs_does_not_wrap(self):
        """The HandleRGGemStart bug shape — subscript LHS, record-returning
        RHS. Pattern 3 must NOT emit a `bool(...)` wrap."""
        src = """\
void f(SongParser::DifficultyInfo &info, int tick, unsigned char uc,
       unsigned char data, unsigned char channel) {
    info.mRGGemsInfo[uc - 24] = RGGemInfo(tick, info.mActivePlayers,
                                          GetFret(data), channel);
}
"""
        for desc in _variant_descriptions(src):
            self.assertNotIn(
                "Wrap assignment RHS with bool()",
                desc,
                f"bool() wrap emitted for subscript LHS: {desc}",
            )

    def test_field_lhs_does_not_wrap(self):
        src = """\
struct S { int val; };
void f(S &s) {
    s.val = GetSomething();
}
"""
        for desc in _variant_descriptions(src):
            self.assertNotIn(
                "Wrap assignment RHS with bool()",
                desc,
                f"bool() wrap emitted for member LHS: {desc}",
            )

    def test_identifier_lhs_still_wraps(self):
        """Counter-test: ``flag = call();`` must still emit a wrap variant
        (this was the pattern's intended win shape)."""
        src = """\
void f() {
    bool flag;
    flag = IsActive();
}
"""
        descs = _variant_descriptions(src)
        self.assertTrue(
            any("Wrap assignment RHS with bool()" in d for d in descs),
            f"identifier-LHS bool wrap lost. descs={descs}",
        )


if __name__ == "__main__":
    unittest.main()
