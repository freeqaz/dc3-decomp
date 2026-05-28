"""Unit tests for bool_materialize_guard pattern.

Coverage:
  relevant():
    - True for mfcr / crand / cror / extrwi in diff_ops
    - True for clusters with both beq/bne AND arithmetic opcodes
    - False for plain bge/ble flips (signed_unsigned_cast_polarity's job)
    - False for empty diff_ops with no clusters
    - True for ``extrwi.`` (record-bit suffix tolerated via strip_dot)

  priority():
    - 0.5 when relevant (fixed)
    - 0.0 when not relevant

  generate() — positive cases:
    - Member-flag name (``mGathering``) inside ``a + b`` wraps the bool
    - ``!x`` operand inside arithmetic wraps it (already-bool)
    - Comparison subexpression ``(x > 0)`` inside ``+`` wraps it
    - ``IsActive()`` predicate call inside arithmetic wraps it
    - Variant cap (≤ 6) is respected even when many shapes match
    - All variant pattern_names equal "bool_materialize_guard"

  generate() — negative cases:
    - No variants when the operand isn't bool-yielding (``a + b`` ints)
    - Pointer arithmetic (``&arr`` operand) is skipped
    - Float literal (``x + 1.0f``) is skipped
    - Already-bool ``true`` / ``false`` literals are NOT wrapped
    - Idempotent on functions with no arithmetic shapes

  generate() — inverse (strip-!!) variant:
    - ``!!x`` operand produces a strip variant

  No-overlap-with-bool_materialize:
    - bool_materialize fires on ``&&`` / ``&`` chains.  This pattern
      does not fire on those operators (only ``+ - * == !=``).
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
from scripts.permuter.patterns.bool_materialize_guard import (
    BoolMaterializeGuardPattern,
    _is_bool_yielding,
    _peel_double_bang,
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


def _diag_mfcr() -> Diagnosis:
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=10, target_opcode="mfcr", base_opcode="bne")]
    return d


def _diag_crand() -> Diagnosis:
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=10, target_opcode="crand", base_opcode="add")]
    return d


def _diag_extrwi() -> Diagnosis:
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=10, target_opcode="extrwi", base_opcode="lwz")]
    return d


def _diag_extrwi_dot() -> Diagnosis:
    """extrwi. (record-bit) tolerated via strip_dot."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=10, target_opcode="extrwi.", base_opcode="lwz")]
    return d


def _diag_branch_near_arith_cluster() -> Diagnosis:
    """Cluster containing both beq AND addi — proximity signal."""
    d = _empty_diag()
    d.clusters = [
        Cluster(
            start_idx=266, end_idx=269, size=4, inserts=2, deletes=2,
            target_opcodes=("li", "beq", "li", "addi"),
            base_opcodes=("cmpwi", "bne", "subf"),
        )
    ]
    return d


def _diag_bge_ble_only() -> Diagnosis:
    """Pure polarity flip — handled by signed_unsigned_cast_polarity."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=10, target_opcode="bge", base_opcode="ble")]
    return d


# ---------------------------------------------------------------------------
# Tests: relevant()
# ---------------------------------------------------------------------------


class TestRelevant(unittest.TestCase):
    def setUp(self):
        self.pattern = BoolMaterializeGuardPattern()

    def test_relevant_for_mfcr(self):
        """mfcr in diff_ops is a direct bool-materialize signal."""
        self.assertTrue(self.pattern.relevant(_diag_mfcr()))

    def test_relevant_for_crand(self):
        """crand is the cr-logic family — clear bool-materialize signal."""
        self.assertTrue(self.pattern.relevant(_diag_crand()))

    def test_relevant_for_cror(self):
        d = _empty_diag()
        d.diff_ops = [DiffOp(index=10, target_opcode="cror", base_opcode="or")]
        self.assertTrue(self.pattern.relevant(d))

    def test_relevant_for_extrwi(self):
        """extrwi extracts bool bit into gpr — directly relevant."""
        self.assertTrue(self.pattern.relevant(_diag_extrwi()))

    def test_relevant_for_extrwi_record_bit(self):
        """extrwi. with record-bit suffix still triggers via strip_dot."""
        self.assertTrue(self.pattern.relevant(_diag_extrwi_dot()))

    def test_relevant_for_branch_near_arith_cluster(self):
        """Cluster with beq + addi -> proximity signal -> relevant."""
        self.assertTrue(self.pattern.relevant(_diag_branch_near_arith_cluster()))

    def test_not_relevant_for_bge_ble_only(self):
        """Pure polarity flips are handled by signed_unsigned_cast_polarity.

        This pattern must NOT pile onto those — keeps the variant budget tight.
        """
        self.assertFalse(self.pattern.relevant(_diag_bge_ble_only()))

    def test_not_relevant_for_empty_diag(self):
        """No diff_ops, no clusters -> not relevant."""
        self.assertFalse(self.pattern.relevant(_empty_diag()))

    def test_not_relevant_for_branch_alone_cluster(self):
        """Cluster with beq but NO arithmetic opcode -> not relevant.

        A bare comparison/branch cluster is not specifically a bool-in-
        arithmetic shape; defer to other patterns.
        """
        d = _empty_diag()
        d.clusters = [Cluster(
            start_idx=5, end_idx=8, size=3, inserts=2, deletes=1,
            target_opcodes=("cmpwi", "beq"),
            base_opcodes=("cmpwi", "bne"),
        )]
        self.assertFalse(self.pattern.relevant(d))


# ---------------------------------------------------------------------------
# Tests: priority()
# ---------------------------------------------------------------------------


class TestPriority(unittest.TestCase):
    def setUp(self):
        self.pattern = BoolMaterializeGuardPattern()

    def test_priority_fixed_at_half_when_relevant(self):
        """Spec: priority is exactly 0.5 when triggered."""
        self.assertAlmostEqual(self.pattern.priority(_diag_mfcr()), 0.5)

    def test_priority_zero_when_not_relevant(self):
        self.assertAlmostEqual(self.pattern.priority(_empty_diag()), 0.0)

    def test_priority_zero_for_bge_ble_only(self):
        """Even with diff_ops present, if not the right shape -> 0.0."""
        self.assertAlmostEqual(self.pattern.priority(_diag_bge_ble_only()), 0.0)


# ---------------------------------------------------------------------------
# Tests: generate() — positive cases
# ---------------------------------------------------------------------------


class TestGeneratePositive(unittest.TestCase):
    def setUp(self):
        self.pattern = BoolMaterializeGuardPattern()
        self.diag = _diag_mfcr()

    def _variants_for(self, src: str, func_name: str = "fn") -> list:
        ctx = _make_ctx(src, func_name, self.diag)
        return list(self.pattern.generate(ctx))

    def test_member_flag_in_addition(self):
        """Member-name ``mGathering`` inside ``+`` should be wrapped in !!."""
        src = """
        int Compute(int selected, int mGathering, int mFirstShowing) {
            return selected + (mGathering - mFirstShowing);
        }
        """
        variants = self._variants_for(src, "Compute")
        self.assertGreater(len(variants), 0, "Expected at least one variant")
        # At least one variant should wrap mGathering with !!
        sources = [v.source for v in variants]
        wrapped = any(b"!!(mGathering)" in s for s in sources)
        self.assertTrue(
            wrapped,
            f"Expected !!(mGathering); descriptions: {[v.description for v in variants]}",
        )

    def test_already_bool_unary_not(self):
        """``!x`` is already bool — wrapping it forces the materialize anyway."""
        src = """
        int fn(int sel, int a, int b) {
            return sel + (!a - b);
        }
        """
        variants = self._variants_for(src)
        # !a is bool-yielding, so it qualifies for wrapping.
        self.assertGreater(len(variants), 0)
        sources = [v.source for v in variants]
        self.assertTrue(any(b"!!(!a)" in s for s in sources),
                        f"Expected !!(!a); got: {[v.description for v in variants]}")

    def test_comparison_subexpr(self):
        """``(x > 0)`` inside ``+`` should be wrapped in !!."""
        src = """
        int fn(int sel, int x, int y) {
            return sel + ((x > 0) - y);
        }
        """
        variants = self._variants_for(src)
        # The comparison is bool-yielding; we expect a wrap variant.
        # Since the operand is parenthesized, the wrap goes around the parens:
        # ``(x > 0)`` -> ``!!((x > 0))``.
        sources = [v.source for v in variants]
        wrapped = any(b"!!((x > 0))" in s or b"!!(x > 0)" in s for s in sources)
        self.assertTrue(
            wrapped,
            f"Expected !!((x > 0)) or !!(x > 0); got: "
            f"{[v.description for v in variants]}",
        )

    def test_predicate_call(self):
        """``IsActive()`` is a Is*/Has*/Can* predicate -> bool-yielding."""
        src = """
        bool IsActive();
        int Compute(int sel, int y) {
            return sel + (IsActive() - y);
        }
        """
        variants = self._variants_for(src, "Compute")
        self.assertGreater(len(variants), 0,
                           f"Expected variants for IsActive() predicate")
        sources = [v.source for v in variants]
        wrapped = any(b"!!(IsActive())" in s for s in sources)
        self.assertTrue(
            wrapped,
            f"Expected !!(IsActive()); got: {[v.description for v in variants]}",
        )

    def test_variant_cap_respected(self):
        """Even if many bool-yielding shapes exist, cap at 6 variants."""
        src = """
        int Compute(int s, int mGathering, int mPicking, int mFocused,
                    int mActive, int mDirty, int mReady, int mDone, int mFirst) {
            int a = s + (mGathering - mFirst);
            int b = s + (mPicking - mFirst);
            int c = s + (mFocused - mFirst);
            int d = s + (mActive - mFirst);
            int e = s + (mDirty - mFirst);
            int f = s + (mReady - mFirst);
            int g = s + (mDone - mFirst);
            return a + b + c + d + e + f + g;
        }
        """
        variants = self._variants_for(src, "Compute")
        self.assertLessEqual(len(variants), 6,
                             f"Expected <= 6 variants, got {len(variants)}")

    def test_all_variant_pattern_names_match(self):
        """Every emitted variant carries the canonical pattern_name."""
        src = """
        int Compute(int s, int mGathering, int y) {
            return s + (mGathering - y);
        }
        """
        variants = self._variants_for(src, "Compute")
        for v in variants:
            self.assertEqual(v.pattern_name, "bool_materialize_guard")


# ---------------------------------------------------------------------------
# Tests: generate() — negative cases
# ---------------------------------------------------------------------------


class TestGenerateNegative(unittest.TestCase):
    def setUp(self):
        self.pattern = BoolMaterializeGuardPattern()
        self.diag = _diag_mfcr()

    def _variants_for(self, src: str, func_name: str = "fn") -> list:
        ctx = _make_ctx(src, func_name, self.diag)
        return list(self.pattern.generate(ctx))

    def test_no_bool_yielding_operands(self):
        """Plain int arithmetic — no bool subexpression -> 0 variants."""
        src = """
        int fn(int a, int b, int c) {
            return a + b - c;
        }
        """
        variants = self._variants_for(src)
        self.assertEqual(
            len(variants), 0,
            f"Expected 0 variants; got: {[v.description for v in variants]}",
        )

    def test_pointer_arithmetic_skipped(self):
        """``&arr`` operand means pointer arithmetic — must skip."""
        src = """
        int fn(int arr[], int idx) {
            int* p = &arr[idx];
            return *p;
        }
        """
        variants = self._variants_for(src)
        self.assertEqual(len(variants), 0,
                         f"Pointer arithmetic must not emit variants")

    def test_float_literal_skipped(self):
        """``x + 1.0f`` is float arithmetic — must skip even if x looks bool-ish."""
        src = """
        float fn(float x) {
            return x + 1.0f;
        }
        """
        variants = self._variants_for(src)
        self.assertEqual(len(variants), 0,
                         "Float literal operand should be skipped")

    def test_excluded_member_names_not_wrapped(self):
        """``mCount`` / ``mElapsed`` are known integers — must NOT be wrapped."""
        src = """
        int Compute(int mCount, int mElapsed) {
            return mCount + mElapsed;
        }
        """
        variants = self._variants_for(src, "Compute")
        self.assertEqual(
            len(variants), 0,
            f"Excluded member names should not be wrapped; "
            f"got: {[v.description for v in variants]}",
        )

    def test_true_false_literals_not_wrapped(self):
        """``true``/``false`` are pure bool literals — pointless to wrap."""
        # We need a binary_expression where one side is true/false. Since
        # ``true`` is type ``true`` in tree-sitter, our bool-yield heuristic
        # already won't match — but make sure we don't crash either.
        src = """
        int fn(int x) {
            return x + true;
        }
        """
        variants = self._variants_for(src)
        # true is not in our bool-yielding shapes (it's neither !, comparison,
        # member-flag, nor predicate call) — so no variants.
        sources = [v.source for v in variants]
        wrapped_true = any(b"!!(true)" in s for s in sources)
        self.assertFalse(wrapped_true, "Must NOT wrap literal true")

    def test_idempotent_on_function_with_no_arithmetic(self):
        """Pure-assignment function -> no arithmetic binary_expressions -> 0 variants."""
        src = """
        void fn(int* p) {
            *p = 1;
        }
        """
        variants = self._variants_for(src)
        self.assertEqual(len(variants), 0)


# ---------------------------------------------------------------------------
# Tests: inverse (strip-!!) variant
# ---------------------------------------------------------------------------


class TestStripDoubleBang(unittest.TestCase):
    def setUp(self):
        self.pattern = BoolMaterializeGuardPattern()
        self.diag = _diag_mfcr()

    def test_already_double_bang_produces_strip_variant(self):
        """``!!x`` operand should generate the strip variant (inverse direction)."""
        src = """
        int Compute(int s, int mGathering, int y) {
            return s + (!!mGathering - y);
        }
        """
        ctx = _make_ctx(src, "Compute", self.diag)
        variants = list(self.pattern.generate(ctx))
        sources = [v.source for v in variants]
        # The strip variant should have ``(mGathering - y)`` with no !!
        stripped = any(b"(mGathering - y)" in s for s in sources)
        self.assertTrue(
            stripped,
            f"Expected strip variant producing '(mGathering - y)'; "
            f"got: {[v.description for v in variants]}",
        )


# ---------------------------------------------------------------------------
# Tests: _peel_double_bang helper
# ---------------------------------------------------------------------------


class TestPeelDoubleBang(unittest.TestCase):
    def test_peel_double_bang_on_simple_ident(self):
        """``!!x`` parses as unary(!, unary(!, x)); peel returns the inner x."""
        src_bytes = b"int fn() { int x = 0; return !!x; }"
        tree = _PARSER.parse(src_bytes)
        # Walk to find the !!x node.
        from scripts.permuter.ast_queries import walk
        peeled_found = False
        for n in walk(tree.root_node):
            if n.type == "unary_expression":
                inner = _peel_double_bang(n)
                if inner is not None:
                    self.assertEqual(
                        src_bytes[inner.start_byte:inner.end_byte], b"x",
                    )
                    peeled_found = True
                    break
        self.assertTrue(peeled_found, "Should have found a !! to peel")


# ---------------------------------------------------------------------------
# Tests: _is_bool_yielding helper directly
# ---------------------------------------------------------------------------


class TestIsBoolYielding(unittest.TestCase):
    def _bool_yield_of_first_arith_operand(self, src: str) -> bool | None:
        """Parse, find first ``+``/``-`` binary, return _is_bool_yielding(left)."""
        src_bytes = textwrap.dedent(src).encode("utf-8")
        tree = _PARSER.parse(src_bytes)
        from scripts.permuter.ast_queries import walk
        for n in walk(tree.root_node):
            if n.type != "binary_expression":
                continue
            op = n.child_by_field_name("operator")
            if op is None or op.text not in (b"+", b"-", b"*"):
                continue
            left = n.child_by_field_name("left")
            if left is None:
                continue
            return _is_bool_yielding(left, src_bytes)
        return None

    def test_member_flag_is_bool_yielding(self):
        """``mIsActive`` -> True (m + uppercase)."""
        src = "int fn() { int x = 0; return mIsActive + x; }"
        self.assertTrue(self._bool_yield_of_first_arith_operand(src))

    def test_plain_int_ident_not_bool_yielding(self):
        """``a`` lowercase -> False."""
        src = "int fn() { int a = 0, x = 0; return a + x; }"
        self.assertFalse(self._bool_yield_of_first_arith_operand(src))

    def test_excluded_member_not_bool_yielding(self):
        """``mCount`` is in the exclusion list -> False."""
        src = "int fn() { int x = 0; return mCount + x; }"
        self.assertFalse(self._bool_yield_of_first_arith_operand(src))


if __name__ == "__main__":
    unittest.main()
