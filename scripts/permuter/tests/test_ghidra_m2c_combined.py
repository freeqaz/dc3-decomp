"""Tests for combined Ghidra + m2c synthesis (perf/ghidra-m2c-combined).

Today m2c was used only as a *fallback* when Ghidra was absent. These tests
cover the new combined design where the two independent decompilers are used
*together*:

  1. Agreement  -> high-confidence single preferred order.
  2. Disagreement -> synthesize candidates for BOTH orderings.
  3. One-only    -> that one (no regression on prior fallback behavior).

The same combination is applied to control-flow guard-shape tags.
"""

from __future__ import annotations

import textwrap

from scripts.permuter.constraint_solver import (
    extract_constraints,
    synthesize,
    _combine_cf_tags,
    _decl_order_edit_sets,
)
from scripts.permuter.ghidra_ast import VarInfo, parse_ghidra
from scripts.permuter.ghidra_var_match import combine_var_orders
from scripts.permuter.m2c import extract_condition_structure_from_text
from scripts.permuter.tests.conftest import (
    diag_with_gpr_swaps,
    make_context,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _vi(names_with_prefix):
    """Build a VarInfo list in first-use order."""
    out = []
    for i, (name, prefix) in enumerate(names_with_prefix):
        decl_type = {
            "i": "int", "u": "uint", "f": "float", "p": "void*", "": "int",
        }[prefix]
        out.append(VarInfo(
            name=name, first_use_line=i, first_use_byte=i * 10,
            type_prefix=prefix, decl_type=decl_type,
        ))
    return out


# ---------------------------------------------------------------------------
# combine_var_orders: the consensus core
# ---------------------------------------------------------------------------

def test_combine_agree_when_shared_order_matches():
    """Both present, shared locals in same relative order -> high-confidence."""
    g = _vi([("a", "i"), ("b", "i"), ("c", "i")])
    m = _vi([("a", "i"), ("b", "i"), ("c", "i")])
    res = combine_var_orders(g, m)
    assert res.verdict == "agree"
    assert res.high_confidence is True
    # Single preferred order — Ghidra's VarInfo list (richer type info).
    assert len(res.orders) == 1
    assert [v.name for v in res.orders[0]] == ["a", "b", "c"]


def test_combine_disagree_emits_both_orders():
    """Both present, shared locals in conflicting order -> two hypotheses."""
    g = _vi([("a", "i"), ("b", "i"), ("c", "i")])
    m = _vi([("c", "i"), ("b", "i"), ("a", "i")])  # reversed shared order
    res = combine_var_orders(g, m)
    assert res.verdict == "disagree"
    assert res.high_confidence is False
    assert len(res.orders) == 2
    # Ghidra first, then m2c.
    assert [v.name for v in res.orders[0]] == ["a", "b", "c"]
    assert [v.name for v in res.orders[1]] == ["c", "b", "a"]


def test_combine_ghidra_only():
    g = _vi([("a", "i"), ("b", "i")])
    res = combine_var_orders(g, None)
    assert res.verdict == "ghidra_only"
    assert res.high_confidence is False
    assert len(res.orders) == 1
    assert [v.name for v in res.orders[0]] == ["a", "b"]


def test_combine_m2c_only():
    m = _vi([("x", "i"), ("y", "i")])
    res = combine_var_orders(None, m)
    assert res.verdict == "m2c_only"
    assert res.high_confidence is False
    assert len(res.orders) == 1
    assert [v.name for v in res.orders[0]] == ["x", "y"]


def test_combine_none():
    res = combine_var_orders(None, None)
    assert res.verdict == "none"
    assert res.orders == []
    assert res.high_confidence is False


def test_combine_no_shared_locals_is_not_a_conflict():
    """Disjoint names (Ghidra iVarN vs m2c var_rN) -> no conflict signal.

    We can't cross-map invented names, so disjoint sets are treated as
    agreement (no disagreement) — the search still tries the preferred order.
    """
    g = _vi([("iVar1", "i"), ("iVar2", "i")])
    m = _vi([("var_r3", "i"), ("var_r4", "i")])
    res = combine_var_orders(g, m)
    assert res.verdict == "agree"
    assert res.high_confidence is True


# ---------------------------------------------------------------------------
# extract_constraints wiring: decl_order + confidence flags
# ---------------------------------------------------------------------------

_SRC = textwrap.dedent("""\
    void test_func() {
        int a = 1;
        int b = 2;
        int c = a + b;
    }
""")


def _ctx_with(ghidra_code=None, m2c_code=None):
    ctx = make_context(_SRC, "test_func", diag_with_gpr_swaps())
    if ghidra_code is not None:
        ctx.ghidra_ast = parse_ghidra(ghidra_code)
        ctx.ghidra_code = ghidra_code
    if m2c_code is not None:
        ctx.m2c_code = m2c_code
    return ctx


def test_extract_constraints_agree_sets_high_confidence():
    """Ghidra and m2c agree on first-use order -> high-confidence flag set."""
    ghidra_code = textwrap.dedent("""\
        void test_func(void) {
            int a; int b; int c;
            a = 1; b = 2; c = a + b;
        }
    """)
    # m2c emits one local declaration per line (text-based extractor).
    m2c_code = textwrap.dedent("""\
        void test_func(void) {
            s32 a;
            s32 b;
            s32 c;

            a = 1;
            b = 2;
            c = a + b;
        }
    """)
    cs = extract_constraints(_ctx_with(ghidra_code, m2c_code))
    assert cs.decl_order == ["a", "b", "c"], cs.decl_order
    assert cs.decl_order_verdict == "agree"
    assert cs.decl_order_high_confidence is True
    assert cs.ghidra_available is True


def test_extract_constraints_disagree_records_verdict():
    """Ghidra and m2c disagree -> verdict 'disagree', not high-confidence."""
    ghidra_code = textwrap.dedent("""\
        void test_func(void) {
            int a; int b; int c;
            a = 1; b = 2; c = a + b;
        }
    """)
    # m2c sees c used first, then b, then a (conflicting shared order).
    m2c_code = textwrap.dedent("""\
        void test_func(void) {
            s32 a;
            s32 b;
            s32 c;

            c = 0;
            b = 2;
            a = 1;
        }
    """)
    cs = extract_constraints(_ctx_with(ghidra_code, m2c_code))
    assert cs.decl_order_verdict == "disagree"
    assert cs.decl_order_high_confidence is False
    # Preferred (first) hypothesis = Ghidra's order.
    assert cs.decl_order == ["a", "b", "c"], cs.decl_order


def test_extract_constraints_ghidra_only_preserved():
    """No m2c -> Ghidra order, verdict ghidra_only, no high confidence."""
    ghidra_code = textwrap.dedent("""\
        void test_func(void) {
            int gx; int gy;
            gx = 1; gy = 2;
        }
    """)
    cs = extract_constraints(_ctx_with(ghidra_code, None))
    assert cs.decl_order == ["gx", "gy"], cs.decl_order
    assert cs.decl_order_verdict == "ghidra_only"
    assert cs.decl_order_high_confidence is False


def test_extract_constraints_m2c_only_preserved():
    """No Ghidra -> m2c order, verdict m2c_only (prior fallback behavior)."""
    m2c_code = textwrap.dedent("""\
        void test_func(void) {
            s32 m2c_a;
            s32 m2c_b;

            m2c_a = 1;
            m2c_b = 2;
        }
    """)
    cs = extract_constraints(_ctx_with(None, m2c_code))
    assert cs.decl_order == ["m2c_a", "m2c_b"], cs.decl_order
    assert cs.decl_order_verdict == "m2c_only"
    assert cs.ghidra_available is False


def test_extract_constraints_no_sources_leaves_decl_order_unset():
    cs = extract_constraints(_ctx_with(None, None))
    assert cs.decl_order is None
    assert cs.decl_order_verdict == ""
    assert cs.decl_order_high_confidence is False


# ---------------------------------------------------------------------------
# Disagreement -> synthesize emits a candidate for BOTH orderings
# ---------------------------------------------------------------------------

def test_synthesize_disagree_emits_both_hypotheses():
    """When Ghidra/m2c disagree, synthesize must produce >1 distinct candidate.

    Source decls [b, a, c]. Ghidra wants order [a, b, c]; m2c wants [c, a, b].
    These produce different reorders, so we expect a primary synth_0 AND an
    alt hypothesis synth_alt0 with different source bytes.
    """
    src = textwrap.dedent("""\
        void test_func() {
            int b = 2;
            int a = 1;
            int c = a + b;
        }
    """)
    ctx = make_context(src, "test_func", diag_with_gpr_swaps())
    # Ghidra first-use order: a, b, c
    ghidra_code = textwrap.dedent("""\
        void test_func(void) {
            int a; int b; int c;
            a = 1; b = 2; c = a + b;
        }
    """)
    # m2c first-use order: c, a, b (conflicts with Ghidra on shared a/b/c).
    m2c_code = textwrap.dedent("""\
        void test_func(void) {
            s32 a;
            s32 b;
            s32 c;

            c = 0;
            a = 1;
            b = 2;
        }
    """)
    ctx.ghidra_ast = parse_ghidra(ghidra_code)
    ctx.ghidra_code = ghidra_code
    ctx.m2c_code = m2c_code

    cs = extract_constraints(ctx)
    assert cs.decl_order_verdict == "disagree"

    # Two hypotheses must yield two distinct edit-sets.
    edit_sets = _decl_order_edit_sets(cs, ctx)
    assert len(edit_sets) == 2, (
        f"Expected 2 decl-order hypotheses, got {len(edit_sets)}"
    )

    result = synthesize(ctx)
    names = {v.name for v in result.variants}
    # An alternative-hypothesis variant must be present alongside the primary.
    assert any(n.startswith("synth_alt") for n in names), names
    # And it must be tagged distinctly.
    alt = [v for v in result.variants if v.name.startswith("synth_alt")]
    assert alt and "ghidra_m2c_alt" in alt[0].tags


def test_synthesize_agree_tags_high_confidence():
    """When Ghidra/m2c agree, the synthesized variant carries the agree tag."""
    src = textwrap.dedent("""\
        void test_func() {
            int b = 2;
            int a = 1;
            int c = a + b;
        }
    """)
    ctx = make_context(src, "test_func", diag_with_gpr_swaps())
    # Both decompilers agree on first-use order: a, b, c.
    code = textwrap.dedent("""\
        void test_func(void) {
            int a; int b; int c;
            a = 1; b = 2; c = a + b;
        }
    """)
    ctx.ghidra_ast = parse_ghidra(code)
    ctx.ghidra_code = code
    ctx.m2c_code = textwrap.dedent("""\
        void test_func(void) {
            s32 a;
            s32 b;
            s32 c;

            a = 1;
            b = 2;
            c = a + b;
        }
    """)

    cs = extract_constraints(ctx)
    assert cs.decl_order_high_confidence is True

    result = synthesize(ctx)
    # No alt hypothesis on agreement.
    assert not any(v.name.startswith("synth_alt") for v in result.variants)
    # Any synthesized variant should carry the agree tag.
    if result.variants:
        assert all("ghidra_m2c_agree" in v.tags for v in result.variants)


def test_synthesize_single_source_no_alt_variant():
    """Ghidra-only (no m2c) -> single hypothesis, no alt variant (no regress)."""
    src = textwrap.dedent("""\
        void test_func() {
            int b = 2;
            int a = 1;
            int c = a + b;
        }
    """)
    ctx = make_context(src, "test_func", diag_with_gpr_swaps())
    ghidra_code = textwrap.dedent("""\
        void test_func(void) {
            int a; int b; int c;
            a = 1; b = 2; c = a + b;
        }
    """)
    ctx.ghidra_ast = parse_ghidra(ghidra_code)
    ctx.ghidra_code = ghidra_code
    # No m2c.

    result = synthesize(ctx)
    assert not any(v.name.startswith("synth_alt") for v in result.variants)


# ---------------------------------------------------------------------------
# Control-flow guard-shape combination
# ---------------------------------------------------------------------------

def test_m2c_condition_structure_extractor():
    """m2c text extractor classifies conjunction / guard_return shapes."""
    code = textwrap.dedent("""\
        s32 test(s32 a, s32 b) {
            if (a != 0 && b != 0) {
                return 1;
            }
            if (a == 0) return 0;
            return 2;
        }
    """)
    tags = extract_condition_structure_from_text(code)
    assert "conjunction" in tags
    assert "guard_return" in tags
    assert "guard_return_false" in tags


def test_combine_cf_tags_agreed_first():
    """Tags present in both decompilers are ordered before single-source tags."""
    ghidra = ["conjunction", "guard_return"]
    m2c = ["guard_return", "disjunction"]
    combined = _combine_cf_tags(ghidra, m2c)
    # guard_return is shared -> first; then single-source tags.
    assert combined[0] == "guard_return"
    assert set(combined) == {"conjunction", "guard_return", "disjunction"}


def test_combine_cf_tags_is_additive():
    """Union never drops a tag a single source found."""
    assert _combine_cf_tags(["conjunction"], []) == ["conjunction"]
    assert _combine_cf_tags([], ["disjunction"]) == ["disjunction"]
    assert _combine_cf_tags([], []) == []


def test_extract_constraints_cf_high_confidence_on_agreement():
    """Ghidra and m2c agree on cf shape -> cf_high_confidence set."""
    src = textwrap.dedent("""\
        int test_func(int a, int b) {
            if (a != 0 && b != 0) {
                return 1;
            }
            return 0;
        }
    """)
    ctx = make_context(src, "test_func", diag_with_gpr_swaps())
    ghidra_code = textwrap.dedent("""\
        int test_func(int a, int b) {
            if (a != 0 && b != 0) {
                return 1;
            }
            return 0;
        }
    """)
    ctx.ghidra_ast = parse_ghidra(ghidra_code)
    ctx.ghidra_code = ghidra_code
    ctx.m2c_code = textwrap.dedent("""\
        s32 test_func(s32 a, s32 b) {
            if (a != 0 && b != 0) {
                return 1;
            }
            return 0;
        }
    """)
    cs = extract_constraints(ctx)
    # Both saw "conjunction" -> agreement.
    assert cs.cf_high_confidence is True
    assert "conjunction" in set(cs.cf_directions.values())
