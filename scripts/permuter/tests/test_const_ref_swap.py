"""Tests for the const_ref_swap permuter pattern.

Pure AST/text-level tests — no builds, no objdiff.
Covers the key rejection guards added to reduce the 38% build-failure rate.
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
)
from scripts.permuter.patterns.const_ref_swap import ConstRefSwapPattern


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_ctx(source_text: str, func_name: str) -> FunctionContext:
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
                diagnosis=_dummy_diagnosis(),
            )
    raise ValueError(f"Function '{func_name}' not found")


def _dummy_diagnosis() -> Diagnosis:
    return Diagnosis(
        total_instructions=20,
        match_counts={},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[DiffOp(0, "mr", "mr")],
        clusters=[],
        noise_explained=0,
        noise_total=1,
    )


def _variants(source_text: str, func_name: str) -> list[str]:
    """Return list of variant source strings generated for func_name."""
    ctx = _make_ctx(source_text, func_name)
    pat = ConstRefSwapPattern()
    return [v.source.decode("utf-8", errors="replace") for v in pat.generate(ctx)]


# ---------------------------------------------------------------------------
# Tests: copy → ref direction
# ---------------------------------------------------------------------------

class TestConstructorSyntaxRejected(unittest.TestCase):
    """Constructor-call syntax `T v(a, b)` must never be swapped to const-ref."""

    def test_vector4_constructor_not_swapped(self):
        src = """\
        void f() {
            Vector4 tapOffset(1.0f, 2.0f, 3.0f, 4.0f);
            use(tapOffset);
        }
        """
        variants = _variants(src, "f")
        # No variant should contain `const Vector4&`
        for v in variants:
            self.assertNotIn("const Vector4&", v,
                msg="Constructor-call Vector4 v(...) must not be converted to const Vector4&")

    def test_symbol_constructor_not_swapped(self):
        src = """\
        void f() {
            Symbol s("gameplay_mode");
            doWork(s);
        }
        """
        variants = _variants(src, "f")
        for v in variants:
            self.assertNotIn("const Symbol&", v,
                msg="Symbol s(\"...\") must not be converted to const Symbol&")

    def test_message_constructor_not_swapped(self):
        src = """\
        void f() {
            Message stopMsg("stop_narrator");
            send(stopMsg);
        }
        """
        variants = _variants(src, "f")
        for v in variants:
            self.assertNotIn("const Message&", v,
                msg="Message msg(\"...\") must not be converted to const Message&")

    def test_color_constructor_not_swapped(self):
        src = """\
        void f() {
            Hmx::Color white(1, 1, 1, 1);
            draw(white);
        }
        """
        variants = _variants(src, "f")
        for v in variants:
            self.assertNotIn("const Hmx::Color&", v)


class TestCallExpressionRejected(unittest.TestCase):
    """Function-call initializers should not be swapped (rvalue, skip to be safe)."""

    def test_call_expr_not_swapped(self):
        src = """\
        void f() {
            Symbol stance = getStance();
            use(stance);
        }
        """
        variants = _variants(src, "f")
        for v in variants:
            self.assertNotIn("const Symbol&", v,
                msg="Symbol s = call() must not be converted to const Symbol& (call_expression guard)")

    def test_method_call_not_swapped(self):
        src = """\
        void f() {
            Transform t = obj->GetTransform();
            apply(t);
        }
        """
        variants = _variants(src, "f")
        for v in variants:
            self.assertNotIn("const Transform&", v)


class TestAddressTakenRejected(unittest.TestCase):
    """If &var is used after declaration, must not convert copy to const-ref."""

    def test_address_taken_not_swapped(self):
        src = """\
        void f() {
            Vector3 pos = mPos;
            fillOut(&pos);
        }
        """
        variants = _variants(src, "f")
        for v in variants:
            self.assertNotIn("const Vector3&", v,
                msg="Vector3 pos = expr where &pos is taken must not become const Vector3&")

    def test_address_not_taken_is_swapped(self):
        src = """\
        void f() {
            Vector3 pos = mPos;
            use(pos);
        }
        """
        variants = _variants(src, "f")
        self.assertTrue(
            any("const Vector3&" in v for v in variants),
            "Vector3 pos = mPos (no &pos) should be a const-ref swap candidate"
        )


class TestModifiedAfterRejected(unittest.TestCase):
    """Variable modified after declaration must not become const-ref."""

    def test_assigned_after_not_swapped(self):
        src = """\
        void f() {
            Symbol s = mSym;
            s = other;
        }
        """
        variants = _variants(src, "f")
        for v in variants:
            self.assertNotIn("const Symbol&", v,
                msg="Symbol assigned-after must not become const Symbol&")


class TestPrimitiveTypesRejected(unittest.TestCase):
    """Primitive and Windows scalar typedefs must not be swapped."""

    def test_longlong_not_swapped(self):
        src = """\
        void f() {
            LONGLONG prevFrame = mFrame->liTimeStamp.QuadPart;
            use(prevFrame);
        }
        """
        variants = _variants(src, "f")
        for v in variants:
            self.assertNotIn("const LONGLONG&", v,
                msg="LONGLONG (scalar typedef) must not be swapped to const-ref")

    def test_handle_not_swapped(self):
        src = """\
        void f() {
            HANDLE h = mHandle;
            use(h);
        }
        """
        variants = _variants(src, "f")
        for v in variants:
            self.assertNotIn("const HANDLE&", v)


# ---------------------------------------------------------------------------
# Tests: ref → copy direction
# ---------------------------------------------------------------------------

class TestConstRefToCopy(unittest.TestCase):
    """const T& var = expr; → T var = expr; swap should work for simple cases."""

    def test_const_ref_to_copy(self):
        src = """\
        void f() {
            const Symbol& beatSym = mSym;
            use(beatSym);
        }
        """
        variants = _variants(src, "f")
        self.assertTrue(
            any("Symbol beatSym" in v and "const Symbol&" not in v for v in variants),
            "const Symbol& should be swappable to Symbol copy"
        )

    def test_non_const_ref_not_swapped(self):
        """Non-const (mutable) references must never be touched."""
        src = """\
        void f() {
            DataNode& n = DataVariable("foo");
            n = 42;
        }
        """
        variants = _variants(src, "f")
        # No variant should touch the DataNode& declaration
        for v in variants:
            self.assertNotIn("DataNode n", v,
                msg="Non-const DataNode& must not be touched")


# ---------------------------------------------------------------------------
# Tests: wins preserved
# ---------------------------------------------------------------------------

class TestWinCasePreserved(unittest.TestCase):
    """Pattern still finds the declared win cases (member access lvalue)."""

    def test_symbol_member_copy_swappable(self):
        """Symbol s = this->mSym; — field_expression, should remain a candidate."""
        src = """\
        void f() {
            Symbol stance = mStance;
            log(stance.Str());
        }
        """
        variants = _variants(src, "f")
        self.assertTrue(
            any("const Symbol&" in v for v in variants),
            "Symbol s = member; should still generate const-ref variant"
        )

    def test_subscript_lvalue_swappable(self):
        """Symbol s = arr[i]; — subscript_expression, should remain a candidate."""
        src = """\
        void f() {
            Symbol ret = smiles[idx];
            return ret;
        }
        """
        variants = _variants(src, "f")
        self.assertTrue(
            any("const Symbol&" in v for v in variants),
            "Symbol s = arr[i]; should still generate const-ref variant"
        )


if __name__ == "__main__":
    unittest.main()
