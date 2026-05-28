"""Tests for the fpr_cascade_operand_hoist pattern + the is_all_noise gate.

The gate change is safety-critical: a new ``fpr_cascade_candidate`` parameter
that MUST default False so every non-opting caller keeps byte-identical
behavior. These tests pin both the pattern's two families (hoist / negate-fold)
and the regression-guard that the default path is unchanged.
"""

from __future__ import annotations

import pytest

from scripts.permuter.diagnosis import is_all_noise
from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.patterns.fpr_cascade_operand_hoist import (
    _is_fpr,
    has_fpr_cascade_hoist_candidate,
)
from scripts.permuter.types import Diagnosis, SwapInfo

from scripts.permuter.tests.conftest import make_context, normalize


PATTERN_NAME = "fpr_cascade_operand_hoist"


# ---------------------------------------------------------------------------
# Diagnosis factories
# ---------------------------------------------------------------------------

def _empty() -> Diagnosis:
    return Diagnosis(
        total_instructions=100,
        match_counts={"match": 99, "mismatch": 1},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[],
        clusters=[],
        noise_explained=0,
        noise_total=0,
    )


def diag_multi_instr_fpr() -> Diagnosis:
    """Multi-instruction FPR/FPR swap (first_idx != last_idx) — the target class."""
    d = _empty()
    d.reg_swap_pairs = {("f4", "f5"): SwapInfo(count=4, first_idx=10, last_idx=20)}
    return d


def diag_single_instr_fpr() -> Diagnosis:
    """Single-instruction FPR swap (first_idx == last_idx) — fma_reorder's class."""
    d = _empty()
    d.reg_swap_pairs = {("f0", "f10"): SwapInfo(count=2, first_idx=20, last_idx=20)}
    return d


def diag_gpr_swap() -> Diagnosis:
    d = _empty()
    d.reg_swap_pairs = {("r20", "r21"): SwapInfo(count=4, first_idx=10, last_idx=50)}
    return d


# ---------------------------------------------------------------------------
# Source fixtures (reconstructed pre-win shapes)
# ---------------------------------------------------------------------------

# Reconstructed RB3 Rot::RotateAboutX BEFORE the 99.37 -> 99.8% win.
ROTATE_PRE_WIN = """
void RotateAboutX(const Matrix3 &min, float fcos, float fsin, Matrix3 &mout) {
    mout.x.x = min.x.x;
    mout.x.y = min.x.y * fcos - min.x.z * fsin;
    mout.x.z = min.x.y * fsin + min.x.z * fcos;
}
"""

# Already-hoisted form (idempotence guard — should fire nothing).
ROTATE_POST_WIN = """
void RotateAboutX(const Matrix3 &min, float fcos, float fsin, Matrix3 &mout) {
    float xy = min.x.y;
    float xz = min.x.z;
    mout.x.x = min.x.x;
    mout.x.y = xy * fcos - xz * fsin;
    mout.x.z = xy * fsin + xz * fcos;
}
"""

# Reconstructed RB3 Geo::Intersect BEFORE the 99.34 -> 99.6% win.
GEO_PRE_WIN = """
bool Intersect(const Segment &seg, const BSPNode *n, float &t, Plane &p) {
    float nd = n->plane.d;
    float nc = n->plane.c;
    float nb = n->plane.b;
    float na = n->plane.a;
    p.c = -nc;
    p.a = -na;
    p.b = -nb;
    p.d = -nd;
    return true;
}
"""

# Already negate-folded form (idempotence guard for Family B).
GEO_POST_WIN = """
bool Intersect(const Segment &seg, const BSPNode *n, float &t, Plane &p) {
    float nd = -n->plane.d;
    float nc = -n->plane.c;
    float nb = -n->plane.b;
    float na = -n->plane.a;
    p.c = nc;
    p.a = na;
    p.b = nb;
    p.d = nd;
    return true;
}
"""

# Integer-operand cascade — no float arithmetic, must NOT fire.
INT_CASCADE = """
void compute(int *a, int *b, int *out) {
    out[0] = a[1] * b[2] - a[3] * b[4];
    out[1] = a[1] * b[5] + a[3] * b[6];
}
"""

# Single float store — no run of 2+ float-arith assignments.
SINGLE_FLOAT_STORE = """
void one(const Vec &v, Vec &o) {
    o.x = v.x * v.y - v.z;
}
"""

# Float math with no repeated operand — nothing to hoist.
NO_REPEAT_FLOAT = """
void noreps(const Vec &v, Vec &o) {
    o.x = v.a * v.b - v.c * v.d;
    o.y = v.e * v.f + v.g * v.h;
}
"""


def _generate(source: str, func: str, diagnosis: Diagnosis):
    ctx = make_context(source, func, diagnosis)
    pattern = get_pattern(PATTERN_NAME)
    return list(pattern.generate(ctx)), ctx


# ---------------------------------------------------------------------------
# _is_fpr helper
# ---------------------------------------------------------------------------

class TestIsFpr:
    def test_fpr_names(self):
        assert _is_fpr("f0")
        assert _is_fpr("f31")
        assert _is_fpr("f4")

    def test_non_fpr(self):
        assert not _is_fpr("r3")
        assert not _is_fpr("r30")
        assert not _is_fpr("")
        assert not _is_fpr("fp")  # 'fp' isn't f<number>


# ---------------------------------------------------------------------------
# relevance + priority
# ---------------------------------------------------------------------------

class TestRelevance:
    def test_relevant_on_multi_instr_fpr_swap(self):
        p = get_pattern(PATTERN_NAME)
        assert p.relevant(diag_multi_instr_fpr()) is True

    def test_not_relevant_on_single_instr_fpr_swap(self):
        p = get_pattern(PATTERN_NAME)
        assert p.relevant(diag_single_instr_fpr()) is False

    def test_not_relevant_on_gpr_swap(self):
        p = get_pattern(PATTERN_NAME)
        assert p.relevant(diag_gpr_swap()) is False

    def test_not_relevant_on_empty(self):
        p = get_pattern(PATTERN_NAME)
        assert p.relevant(_empty()) is False

    def test_priority_when_relevant(self):
        p = get_pattern(PATTERN_NAME)
        assert p.priority(diag_multi_instr_fpr()) == pytest.approx(0.55)

    def test_priority_when_not_relevant(self):
        p = get_pattern(PATTERN_NAME)
        assert p.priority(_empty()) == 0.0
        assert p.priority(diag_single_instr_fpr()) == 0.0
        assert p.priority(diag_gpr_swap()) == 0.0


# ---------------------------------------------------------------------------
# Gate regression-guards (CRITICAL — default path must be unchanged)
# ---------------------------------------------------------------------------

class TestGateRegression:
    def test_default_false_matches_today_gpr_only(self):
        # GPR swap -> not noise, regardless of the new flag.
        d = diag_gpr_swap()
        assert is_all_noise(d) is False
        assert is_all_noise(d, fpr_cascade_candidate=False) is False
        # Even opting in must not flip a GPR swap to noise.
        assert is_all_noise(d, fpr_cascade_candidate=True) is False

    def test_default_false_matches_today_single_instr_fpr(self):
        # Single-instruction FPR swap -> not noise (fma_reorder's class).
        d = diag_single_instr_fpr()
        assert is_all_noise(d) is False
        assert is_all_noise(d, fpr_cascade_candidate=False) is False
        # Opting in must NOT change the single-instruction case.
        assert is_all_noise(d, fpr_cascade_candidate=True) is False

    def test_default_false_matches_today_empty(self):
        # Empty diagnosis -> pure noise, both default and opted-in.
        d = _empty()
        assert is_all_noise(d) is True
        assert is_all_noise(d, fpr_cascade_candidate=False) is True
        assert is_all_noise(d, fpr_cascade_candidate=True) is True

    def test_multi_instr_fpr_default_is_noise(self):
        # CURRENT behavior preserved: without opting in, multi-instr FPR swap
        # is still classified as noise.
        d = diag_multi_instr_fpr()
        assert is_all_noise(d) is True
        assert is_all_noise(d, fpr_cascade_candidate=False) is True

    def test_multi_instr_fpr_opt_in_unlocks(self):
        # The single behavioral change: opting in flips the multi-instruction
        # FPR swap to NOT noise (so the pattern gets a chance).
        d = diag_multi_instr_fpr()
        assert is_all_noise(d, fpr_cascade_candidate=True) is False


# ---------------------------------------------------------------------------
# AST detector (used by the gate)
# ---------------------------------------------------------------------------

class TestDetector:
    def test_detects_family_a_candidate(self):
        ctx = make_context(ROTATE_PRE_WIN, "RotateAboutX", diag_multi_instr_fpr())
        assert has_fpr_cascade_hoist_candidate(ctx) is True

    def test_detects_family_b_candidate(self):
        ctx = make_context(GEO_PRE_WIN, "Intersect", diag_multi_instr_fpr())
        assert has_fpr_cascade_hoist_candidate(ctx) is True

    def test_no_candidate_int_cascade(self):
        ctx = make_context(INT_CASCADE, "compute", diag_multi_instr_fpr())
        assert has_fpr_cascade_hoist_candidate(ctx) is False

    def test_no_candidate_no_repeat(self):
        ctx = make_context(NO_REPEAT_FLOAT, "noreps", diag_multi_instr_fpr())
        assert has_fpr_cascade_hoist_candidate(ctx) is False

    def test_no_candidate_already_hoisted(self):
        ctx = make_context(ROTATE_POST_WIN, "RotateAboutX", diag_multi_instr_fpr())
        assert has_fpr_cascade_hoist_candidate(ctx) is False

    def test_no_candidate_already_negate_folded(self):
        ctx = make_context(GEO_POST_WIN, "Intersect", diag_multi_instr_fpr())
        assert has_fpr_cascade_hoist_candidate(ctx) is False

    def test_detector_accepts_raw_source(self):
        # The gate may pass raw bytes when no parsed context is handy.
        assert has_fpr_cascade_hoist_candidate(ROTATE_PRE_WIN.encode("utf-8")) is True
        assert has_fpr_cascade_hoist_candidate(INT_CASCADE) is False


# ---------------------------------------------------------------------------
# Family A: hoist repeated float operand loads
# ---------------------------------------------------------------------------

class TestFamilyA:
    def test_hoists_repeated_member_loads(self):
        variants, _ctx = _generate(
            ROTATE_PRE_WIN, "RotateAboutX", diag_multi_instr_fpr()
        )
        assert variants, "expected at least one hoist variant"
        # Some variant must declare both repeated operands as float locals and
        # rewrite the consuming rows to use them.
        ok = False
        for v in variants:
            text = normalize(v.source)
            if (
                "float _fpr0 = min.x.y" in text
                and "float _fpr1 = min.x.z" in text
                and "_fpr0 * fcos - _fpr1 * fsin" in text
                and "_fpr0 * fsin + _fpr1 * fcos" in text
            ):
                ok = True
        assert ok, f"no fully-hoisted variant found in {[v.name for v in variants]}"

    def test_emits_a_reordering_variant(self):
        variants, _ctx = _generate(
            ROTATE_PRE_WIN, "RotateAboutX", diag_multi_instr_fpr()
        )
        names = [v.name for v in variants]
        assert any(n.endswith("_ident") for n in names)
        assert any(n.endswith("_rev") for n in names)
        # The two orderings must produce DISTINCT source (decls in swapped order).
        ident = next(v for v in variants if v.name.endswith("_ident"))
        rev = next(v for v in variants if v.name.endswith("_rev"))
        assert ident.source != rev.source

    def test_all_variants_carry_pattern_name(self):
        variants, _ctx = _generate(
            ROTATE_PRE_WIN, "RotateAboutX", diag_multi_instr_fpr()
        )
        assert all(v.pattern_name == PATTERN_NAME for v in variants)

    def test_variant_cap(self):
        variants, _ctx = _generate(
            ROTATE_PRE_WIN, "RotateAboutX", diag_multi_instr_fpr()
        )
        assert len(variants) <= 6


# ---------------------------------------------------------------------------
# Family B: fold negation into backing float decls
# ---------------------------------------------------------------------------

class TestFamilyB:
    def test_folds_negation_into_decls(self):
        variants, _ctx = _generate(GEO_PRE_WIN, "Intersect", diag_multi_instr_fpr())
        assert variants, "expected at least one negate-fold variant"
        ok = False
        for v in variants:
            text = normalize(v.source)
            if (
                "float nd = -n->plane.d" in text
                and "float nc = -n->plane.c" in text
                and "float nb = -n->plane.b" in text
                and "float na = -n->plane.a" in text
                # stores no longer negate
                and "p.c = nc" in text
                and "p.d = nd" in text
                and "p.c = -nc" not in text
            ):
                ok = True
        assert ok, f"no negate-folded variant in {[v.name for v in variants]}"

    def test_reproduces_committed_win_shape(self):
        # The identity-ordered Family B variant should match the committed RB3
        # win (4e47fd0a) byte-for-byte modulo whitespace.
        variants, _ctx = _generate(GEO_PRE_WIN, "Intersect", diag_multi_instr_fpr())
        ident = next((v for v in variants if v.name.startswith("fprnegfold")
                      and v.name.endswith("_ident")), None)
        assert ident is not None
        assert normalize(ident.source) == normalize(GEO_POST_WIN)


# ---------------------------------------------------------------------------
# Negatives + idempotence
# ---------------------------------------------------------------------------

class TestNegatives:
    def test_integer_cascade_no_fire(self):
        variants, _ctx = _generate(INT_CASCADE, "compute", diag_multi_instr_fpr())
        assert variants == []

    def test_single_float_store_no_cascade(self):
        variants, _ctx = _generate(
            SINGLE_FLOAT_STORE, "one", diag_multi_instr_fpr()
        )
        assert variants == []

    def test_no_repeated_operand_no_fire(self):
        variants, _ctx = _generate(
            NO_REPEAT_FLOAT, "noreps", diag_multi_instr_fpr()
        )
        assert variants == []

    def test_idempotent_already_hoisted(self):
        variants, _ctx = _generate(
            ROTATE_POST_WIN, "RotateAboutX", diag_multi_instr_fpr()
        )
        assert variants == []

    def test_idempotent_already_negate_folded(self):
        variants, _ctx = _generate(
            GEO_POST_WIN, "Intersect", diag_multi_instr_fpr()
        )
        assert variants == []
