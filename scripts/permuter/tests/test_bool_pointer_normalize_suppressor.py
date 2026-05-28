"""Tests for the bool_pointer_normalize_suppressor pattern.

Covers:
  * Pattern registration + metadata.
  * Forward direction: each of the 4 inbound integer casts (long, int,
    unsigned long, unsigned int) maps to the 3 *other* alternative forms
    via the variant list.
  * Reverse direction: ``reinterpret_cast<int>(ptr)`` swaps back to all
    4 C-style casts.
  * Negative cases: numeric-literal operand, non-pointer expression,
    no adjacent bitwise operator, non-integer cast type.
  * Priority gating: relevant() and priority() respond to the documented
    detection signals (cntlzw / extrwi / extsw / replace-clusters).
  * Idempotence: the original cast style is never re-emitted as a variant.
  * Pointer detection heuristic: address-of, arrow access, Ptr-suffix
    members.
  * Variant cap (<=8 per function).

Usage:
    python -m pytest scripts/permuter/tests/test_bool_pointer_normalize_suppressor.py -x -q
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
from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.patterns.bool_pointer_normalize_suppressor import (
    _is_pointer_expression,
)


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


def _empty_diag() -> Diagnosis:
    return Diagnosis(
        total_instructions=100,
        match_counts={"match": 90, "mismatch": 10},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[],
        clusters=[],
        noise_explained=0,
        noise_total=0,
    )


def _diag_with(*opcodes: tuple[str, str]) -> Diagnosis:
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=i, target_opcode=t, base_opcode=b)
                  for i, (t, b) in enumerate(opcodes)]
    return d


def _diag_cluster(size: int) -> Diagnosis:
    d = _empty_diag()
    d.clusters = [Cluster(start_idx=0, end_idx=size, size=size,
                          inserts=size // 2, deletes=size // 2)]
    return d


def _variants(src: str, func_name: str = "fn",
              diagnosis: Diagnosis | None = None) -> list:
    pat = get_pattern("bool_pointer_normalize_suppressor")
    ctx = _make_ctx(src, func_name, diagnosis or _empty_diag())
    return list(pat.generate(ctx))


def _variant_sources(src: str, func_name: str = "fn") -> list[str]:
    return [v.source.decode("utf-8") for v in _variants(src, func_name)]


# ---------------------------------------------------------------------------
# Registration & metadata
# ---------------------------------------------------------------------------

class TestRegistration(unittest.TestCase):
    def test_registered(self):
        pat = get_pattern("bool_pointer_normalize_suppressor")
        self.assertEqual(pat.name, "bool_pointer_normalize_suppressor")
        self.assertEqual(pat.structural_domain, "expr_shape")
        self.assertEqual(pat.safety_tier, "conservative")


# ---------------------------------------------------------------------------
# Priority gate
# ---------------------------------------------------------------------------

class TestPriority(unittest.TestCase):
    def setUp(self):
        self.pat = get_pattern("bool_pointer_normalize_suppressor")

    def test_relevant_cntlzw(self):
        self.assertTrue(self.pat.relevant(_diag_with(("cntlzw", "or"))))

    def test_relevant_extrwi(self):
        self.assertTrue(self.pat.relevant(_diag_with(("extrwi", "rlwinm"))))

    def test_relevant_extsw(self):
        self.assertTrue(self.pat.relevant(_diag_with(("extsw", "mr"))))

    def test_relevant_rlwinm_dot(self):
        self.assertTrue(self.pat.relevant(_diag_with(("rlwinm.", "and."))))

    def test_relevant_small_cluster(self):
        # Replace clusters of size 2-4 are accepted.
        self.assertTrue(self.pat.relevant(_diag_cluster(3)))

    def test_not_relevant_large_cluster(self):
        # Size 10 cluster (way bigger than the bool-normalize tail).
        self.assertFalse(self.pat.relevant(_diag_cluster(10)))

    def test_not_relevant_empty(self):
        self.assertFalse(self.pat.relevant(_empty_diag()))

    def test_not_relevant_unrelated_ops(self):
        # fadds/fmuls have nothing to do with pointer-as-int normalization.
        self.assertFalse(self.pat.relevant(_diag_with(("fadds", "fmuls"))))

    def test_priority_value(self):
        self.assertAlmostEqual(self.pat.priority(_diag_with(("cntlzw", "or"))), 0.5)

    def test_priority_zero_when_not_relevant(self):
        self.assertAlmostEqual(self.pat.priority(_empty_diag()), 0.0)


# ---------------------------------------------------------------------------
# Forward direction:  4 C-style casts -> 3 alternatives each
# ---------------------------------------------------------------------------

class TestForwardVariants(unittest.TestCase):
    """Each of the 4 inbound integer casts must map to alternative forms.

    Spec emits 4 transformations per match (``reinterpret_cast<int>``,
    ``(uintptr_t)``, ``(unsigned int)``, ``(unsigned long)``) and removes
    the inbound's own label so a variant always differs from the input.
    """

    def _assert_alternatives(
        self,
        src: str,
        original_label: str,
        expected_set: set[str],
    ) -> None:
        sources = _variant_sources(src)
        # The original cast (followed by the bare identifier ptr) must
        # NEVER be re-emitted — substring search would be ambiguous because
        # ``(int)`` is contained in ``(unsigned int)`` etc.; we anchor on
        # the trailing identifier name to disambiguate.
        original_with_ptr = f"{original_label}ptr"
        for s in sources:
            self.assertNotIn(
                original_with_ptr, s,
                f"Original cast {original_label!r} re-emitted in variant",
            )
        # Every expected alternative must appear in at least one variant.
        for expected in expected_set:
            found = any(expected in s for s in sources)
            self.assertTrue(
                found,
                f"Expected variant containing {expected!r} but got: {sources}",
            )
        self.assertEqual(
            len(sources), len(expected_set),
            f"Expected {len(expected_set)} variants, got {len(sources)}: "
            f"{[v.description for v in _variants(src)]}",
        )

    def test_long_pointer_cast(self):
        """(long)ptr -> 4 alternatives (none is the same cast)."""
        src = """
        void fn(int *ptr) {
            int x = (long)ptr & 7;
        }
        """
        self._assert_alternatives(
            src, "(long)",
            {"reinterpret_cast<int>(ptr)", "(uintptr_t)ptr",
             "(unsigned int)ptr", "(unsigned long)ptr"},
        )

    def test_int_pointer_cast(self):
        """(int)ptr -> 4 alternatives (none of them is (int))."""
        src = """
        void fn(int *ptr) {
            int x = (int)ptr & 0xF;
        }
        """
        self._assert_alternatives(
            src, "(int)",
            {"reinterpret_cast<int>(ptr)", "(uintptr_t)ptr",
             "(unsigned int)ptr", "(unsigned long)ptr"},
        )

    def test_unsigned_int_pointer_cast(self):
        """(unsigned int)ptr -> 3 alternatives (self is filtered)."""
        src = """
        void fn(int *ptr) {
            int x = (unsigned int)ptr | 0x1;
        }
        """
        self._assert_alternatives(
            src, "(unsigned int)",
            {"reinterpret_cast<int>(ptr)", "(uintptr_t)ptr",
             "(unsigned long)ptr"},
        )

    def test_unsigned_long_pointer_cast(self):
        """(unsigned long)ptr -> 3 alternatives (self is filtered)."""
        src = """
        void fn(int *ptr) {
            int x = (unsigned long)ptr & 7;
        }
        """
        self._assert_alternatives(
            src, "(unsigned long)",
            {"reinterpret_cast<int>(ptr)", "(uintptr_t)ptr",
             "(unsigned int)ptr"},
        )

    def test_address_of_local(self):
        src = """
        void fn() {
            int local;
            int x = (long)&local & 3;
        }
        """
        sources = _variant_sources(src)
        # All four alternatives should appear.
        self.assertTrue(any("reinterpret_cast<int>(&local)" in s for s in sources))
        self.assertTrue(any("(uintptr_t)&local" in s for s in sources))
        self.assertTrue(any("(unsigned int)&local" in s for s in sources))
        self.assertTrue(any("(unsigned long)&local" in s for s in sources))


# ---------------------------------------------------------------------------
# Reverse direction:  reinterpret_cast<int> -> 4 C-style alternatives
# ---------------------------------------------------------------------------

class TestReverseVariants(unittest.TestCase):
    def test_reverse_to_all_four_styles(self):
        src = """
        void fn(int *ptr) {
            int x = reinterpret_cast<int>(ptr) & 7;
        }
        """
        sources = _variant_sources(src)
        self.assertEqual(len(sources), 4)
        # All four C-style alternatives must appear.
        self.assertTrue(any("(long)ptr" in s for s in sources))
        self.assertTrue(any("(uintptr_t)ptr" in s for s in sources))
        self.assertTrue(any("(unsigned int)ptr" in s for s in sources))
        self.assertTrue(any("(unsigned long)ptr" in s for s in sources))
        # Original reinterpret_cast must NOT survive in any variant (we
        # replaced it).
        for s in sources:
            self.assertNotIn("reinterpret_cast<int>(ptr)", s)

    def test_reverse_no_variants_without_bitwise(self):
        # reinterpret_cast<int>(ptr) on its own with no &/|/^/<<>> — skip.
        src = """
        void fn(int *ptr) {
            int x = reinterpret_cast<int>(ptr);
        }
        """
        self.assertEqual(len(_variants(src)), 0)


# ---------------------------------------------------------------------------
# Negative cases
# ---------------------------------------------------------------------------

class TestNegative(unittest.TestCase):
    def test_numeric_literal_cast(self):
        # (long)42 & 7 — operand is a literal, not a pointer.
        src = """
        void fn() {
            int x = (long)42 & 7;
        }
        """
        self.assertEqual(len(_variants(src)), 0)

    def test_non_pointer_identifier(self):
        # 'count' is not pointer-named and never declared as a pointer.
        src = """
        void fn(int count) {
            int x = (long)count & 7;
        }
        """
        self.assertEqual(len(_variants(src)), 0)

    def test_no_bitwise_op_nearby(self):
        # (long)ptr alone is a valid cast but no adjacent bitwise op.
        src = """
        void fn(int *ptr) {
            long x = (long)ptr;
        }
        """
        self.assertEqual(len(_variants(src)), 0)

    def test_non_integer_cast(self):
        # (float)x & 7 — not an integer-width cast we care about.
        src = """
        void fn(int *ptr) {
            float x = (float)ptr;
        }
        """
        self.assertEqual(len(_variants(src)), 0)

    def test_signed_long_value_not_pointer(self):
        # Even with the right cast type, a plain `int x` operand is not a
        # pointer, so we skip.
        src = """
        void fn() {
            int x = 12;
            int y = (long)x & 7;
        }
        """
        self.assertEqual(len(_variants(src)), 0)


# ---------------------------------------------------------------------------
# Idempotence + variant cap
# ---------------------------------------------------------------------------

class TestIdempotence(unittest.TestCase):
    def test_original_cast_never_reemitted_forward(self):
        # (long)ptr -> no variant produces "(long)ptr" as the replacement.
        src = """
        void fn(int *ptr) {
            int x = (long)ptr & 7;
        }
        """
        sources = _variant_sources(src)
        # The literal cast text "(long)ptr" must not appear after the splice
        # (the variant replaced the original byte range).
        for s in sources:
            self.assertNotIn("(long)ptr", s)

    def test_variant_cap_respected(self):
        # Many cast sites — must still cap at <=8 variants.
        src = """
        void fn(int *a, int *b, int *c, int *d) {
            int w = (long)a & 7;
            int x = (long)b & 7;
            int y = (long)c & 7;
            int z = (long)d & 7;
        }
        """
        variants = _variants(src)
        self.assertLessEqual(len(variants), 8)


# ---------------------------------------------------------------------------
# Pointer detection heuristic
# ---------------------------------------------------------------------------

class TestPointerHeuristic(unittest.TestCase):
    """Direct unit tests for _is_pointer_expression."""

    def _operand_of_cast(self, src: str) -> tuple[FunctionContext, object]:
        ctx = _make_ctx(src, "fn", _empty_diag())
        from scripts.permuter.ast_queries import walk
        for n in walk(ctx.body_node):
            if n.type == "cast_expression":
                named = [c for c in n.named_children if c.type != "comment"]
                if len(named) >= 2:
                    return ctx, named[1]
        raise AssertionError("No cast_expression found")

    def test_address_of_local(self):
        ctx, value = self._operand_of_cast("""
        void fn() {
            int local;
            int x = (long)&local & 3;
        }
        """)
        self.assertTrue(_is_pointer_expression(value, ctx.file_source, ctx))

    def test_arrow_member(self):
        # We pass ``ptr->member`` directly as the cast operand.
        ctx, value = self._operand_of_cast("""
        struct S { int m; };
        void fn(S *ptr) {
            int x = (long)ptr->m & 3;
        }
        """)
        # ``ptr->m`` is a field_expression with arrow — pointer-like context.
        self.assertTrue(_is_pointer_expression(value, ctx.file_source, ctx))

    def test_ptr_suffix_identifier(self):
        # ``mWidgetPtr`` matches the Ptr-suffix convention.
        ctx, value = self._operand_of_cast("""
        void fn(int *mWidgetPtr) {
            int x = (long)mWidgetPtr & 3;
        }
        """)
        self.assertTrue(_is_pointer_expression(value, ctx.file_source, ctx))

    def test_plain_int_not_pointer(self):
        ctx, value = self._operand_of_cast("""
        void fn() {
            int count = 4;
            int x = (long)count & 3;
        }
        """)
        self.assertFalse(_is_pointer_expression(value, ctx.file_source, ctx))

    def test_numeric_literal_not_pointer(self):
        ctx, value = self._operand_of_cast("""
        void fn() {
            int x = (long)42 & 3;
        }
        """)
        self.assertFalse(_is_pointer_expression(value, ctx.file_source, ctx))


if __name__ == "__main__":
    unittest.main()
