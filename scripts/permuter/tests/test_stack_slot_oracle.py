"""Tests for the stack-slot oracle (constraint_solver C2 extension).

The oracle reads MWCC DWARF (base-side variable name -> r1 stack offset) and the
target/base slot-layout diff, then maps SWAPPED/DIFFER slots back to named
source locals and proposes declaration swaps. These tests exercise the *pure*
correlation logic (correlate_slots) and the constraint_solver integration on
synthetic DWARF maps + diagnoses — no real build/objdiff/Ghidra needed.
"""

from __future__ import annotations

from dataclasses import dataclass

from scripts.permuter.stack_slot_oracle import (
    SlotRecommendation,
    correlate_slots,
)
from scripts.permuter.constraint_solver import _resolve_oracle_swaps
from scripts.permuter.tests.conftest import make_context, _empty_diag


# ---------------------------------------------------------------------------
# Lightweight stand-ins mirroring the real Row / LocalInfo shape.
# ---------------------------------------------------------------------------

@dataclass
class FakeRow:
    verdict: str
    tgt_off: int | None = None
    base_off: int | None = None
    note: str = ""
    callee_save: bool = False


@dataclass
class FakeLocal:
    name: str
    is_param: bool = False


def _names(**kw) -> dict:
    """Build a {offset: FakeLocal} map from offset=name kwargs (offset as int)."""
    return {off: FakeLocal(name) for off, name in kw.items()}


# ---------------------------------------------------------------------------
# Part 1: SWAPPED rows -> high-confidence decl swaps
# ---------------------------------------------------------------------------

def test_swapped_pair_emits_named_swap():
    """Two SWAPPED slots both named -> a single (name_a, name_b) swap rec."""
    rows = [
        FakeRow(verdict="SWAPPED", tgt_off=0x10, base_off=0x10, note="with 0x14"),
        FakeRow(verdict="SWAPPED", tgt_off=0x14, base_off=0x14, note="with 0x10"),
    ]
    names = {0x10: FakeLocal("alpha"), 0x14: FakeLocal("beta")}
    recs = correlate_slots(rows, names)
    swaps = [r for r in recs if r.kind == "swap"]
    assert len(swaps) == 1
    assert {swaps[0].var_a, swaps[0].var_b} == {"alpha", "beta"}
    assert swaps[0].confidence >= 0.9


def test_swapped_pair_dedup_not_double_counted():
    """The mirror SWAPPED row must not yield a second, reversed swap."""
    rows = [
        FakeRow(verdict="SWAPPED", base_off=0x20, note="with 0x24"),
        FakeRow(verdict="SWAPPED", base_off=0x24, note="with 0x20"),
    ]
    names = {0x20: FakeLocal("first"), 0x24: FakeLocal("second")}
    recs = [r for r in correlate_slots(rows, names) if r.kind == "swap"]
    assert len(recs) == 1


def test_swapped_skips_param_and_unnamed():
    """A swap touching a parameter or unnamed/temp local is not emitted."""
    rows = [
        FakeRow(verdict="SWAPPED", base_off=0x10, note="with 0x14"),
        FakeRow(verdict="SWAPPED", base_off=0x14, note="with 0x10"),
    ]
    # 0x10 is a param -> skip the whole pair.
    names = {0x10: FakeLocal("argc", is_param=True), 0x14: FakeLocal("local")}
    assert [r for r in correlate_slots(rows, names) if r.kind == "swap"] == []

    # unnamed/compiler-temp ("_" prefix) -> skip.
    names2 = {0x10: FakeLocal("_tmp0"), 0x14: FakeLocal("local")}
    assert [r for r in correlate_slots(rows, names2) if r.kind == "swap"] == []


def test_swapped_skips_callee_save():
    """Callee-save (prologue) slots are never source-reorderable."""
    rows = [
        FakeRow(verdict="SWAPPED", base_off=0x10, note="with 0x14", callee_save=True),
        FakeRow(verdict="SWAPPED", base_off=0x14, note="with 0x10", callee_save=True),
    ]
    names = {0x10: FakeLocal("a"), 0x14: FakeLocal("b")}
    assert correlate_slots(rows, names) == []


# ---------------------------------------------------------------------------
# Part 2: DIFFER / TGT_ONLY -> advisory recs
# ---------------------------------------------------------------------------

def test_differ_emits_advisory_named_rec():
    rows = [FakeRow(verdict="DIFFER", base_off=0x30)]
    names = {0x30: FakeLocal("gamma")}
    recs = correlate_slots(rows, names)
    assert len(recs) == 1
    assert recs[0].kind == "differ"
    assert recs[0].var_a == "gamma"
    assert recs[0].confidence < 0.9  # advisory only


def test_tgt_only_emits_force_stack_advisory():
    rows = [FakeRow(verdict="TGT_ONLY", tgt_off=0x40)]
    recs = correlate_slots(rows, {})  # no name needed for force_stack
    assert len(recs) == 1
    assert recs[0].kind == "force_stack"
    assert recs[0].offset_a == 0x40


def test_swaps_ordered_before_advisory():
    """Recommendations are ordered swaps-first for actionability."""
    rows = [
        FakeRow(verdict="DIFFER", base_off=0x30),
        FakeRow(verdict="SWAPPED", base_off=0x10, note="with 0x14"),
        FakeRow(verdict="SWAPPED", base_off=0x14, note="with 0x10"),
        FakeRow(verdict="TGT_ONLY", tgt_off=0x40),
    ]
    names = {0x10: FakeLocal("a"), 0x14: FakeLocal("b"), 0x30: FakeLocal("c")}
    recs = correlate_slots(rows, names)
    kinds = [r.kind for r in recs]
    assert kinds[0] == "swap"
    assert kinds.index("swap") < kinds.index("differ") < kinds.index("force_stack")


def test_empty_inputs_no_crash():
    assert correlate_slots([], {}) == []
    assert correlate_slots([FakeRow(verdict="MATCH", base_off=0x10)], {}) == []


# ---------------------------------------------------------------------------
# Part 3: constraint_solver integration — oracle pairs -> byte-level edits
# ---------------------------------------------------------------------------

_SWAP_SRC = """\
void test_func() {
    int alpha = 1;
    int beta = 2;
    use(alpha, beta);
}
"""


def test_resolve_oracle_swaps_produces_decl_swap_edits():
    """A high-confidence oracle pair becomes two decl_order swap edits that,
    when applied, exchange the two declarations in source."""
    ctx = make_context(_SWAP_SRC, "test_func", _empty_diag())

    class CS:
        oracle_swap_pairs = [("alpha", "beta")]

    edits = _resolve_oracle_swaps(CS(), ctx)
    assert len(edits) == 2
    assert all(e.category == "decl_order" for e in edits)

    # Apply the edits and confirm the declarations swapped order.
    from scripts.permuter.constraint_solver import _apply_edits
    out = _apply_edits(ctx.file_source, edits).decode()
    # `beta` now declared before `alpha` (the two int-decl lines are exchanged).
    assert out.index("int beta = 2;") < out.index("int alpha = 1;")


def test_resolve_oracle_swaps_skips_unknown_names():
    """Names not present as a top-level decl are skipped (clean partial)."""
    ctx = make_context(_SWAP_SRC, "test_func", _empty_diag())

    class CS:
        oracle_swap_pairs = [("alpha", "does_not_exist")]

    assert _resolve_oracle_swaps(CS(), ctx) == []


def test_resolve_oracle_swaps_one_pair_per_name():
    """A name already swapped is not reused by a later overlapping pair."""
    ctx = make_context(_SWAP_SRC, "test_func", _empty_diag())

    class CS:
        # second pair reuses alpha -> must be dropped
        oracle_swap_pairs = [("alpha", "beta"), ("alpha", "beta")]

    edits = _resolve_oracle_swaps(CS(), ctx)
    assert len(edits) == 2  # only the first pair


# ---------------------------------------------------------------------------
# Part 4: oracle gating / no-op behavior
# ---------------------------------------------------------------------------

def test_oracle_disabled_via_env(monkeypatch):
    from scripts.permuter import constraint_solver as csmod
    monkeypatch.setenv("PERMUTER_STACK_SLOT_ORACLE", "0")
    assert csmod._stack_slot_oracle_enabled() is False
    # _extract_stack_slot_swaps returns [] when disabled, regardless of ctx.
    ctx = make_context(_SWAP_SRC, "test_func", _empty_diag())
    ctx.symbol = "test_func"
    assert csmod._extract_stack_slot_swaps(ctx) == []


def test_oracle_no_symbol_is_noop(monkeypatch):
    from scripts.permuter import constraint_solver as csmod
    monkeypatch.setenv("PERMUTER_STACK_SLOT_ORACLE", "1")
    ctx = make_context(_SWAP_SRC, "test_func", _empty_diag())
    ctx.symbol = None  # no symbol -> oracle can't run
    assert csmod._extract_stack_slot_swaps(ctx) == []


def test_oracle_default_enabled(monkeypatch):
    from scripts.permuter import constraint_solver as csmod
    monkeypatch.delenv("PERMUTER_STACK_SLOT_ORACLE", raising=False)
    assert csmod._stack_slot_oracle_enabled() is True


# ---------------------------------------------------------------------------
# Part 5: full constraint_solver integration path (oracle pairs -> variant)
# ---------------------------------------------------------------------------

def test_oracle_pairs_flow_through_resolve_to_edits():
    """Injected oracle pairs must surface as applied decl swaps via the public
    Phase-2 resolve_to_edits path AND the synthesize() shared-edit helper."""
    from scripts.permuter import constraint_solver as csmod
    ctx = make_context(_SWAP_SRC, "test_func", _empty_diag())

    constraints = csmod.extract_constraints(ctx)  # no ghidra/m2c
    constraints.oracle_swap_pairs = [("alpha", "beta")]

    edits = csmod.resolve_to_edits(constraints, ctx)
    out = csmod._apply_edits(ctx.file_source, edits).decode()
    assert out.index("int beta = 2;") < out.index("int alpha = 1;")

    # Same edits must appear in the hypothesis-shared set synthesize() uses.
    shared = csmod._non_decl_resolved_edits(constraints, ctx)
    assert any(e.category == "decl_order" and b"beta" in e.replacement
               for e in shared)


def test_extract_constraints_populates_oracle_when_no_symbol(monkeypatch):
    """With no symbol, extract_constraints must leave oracle_swap_pairs empty
    (clean no-op) — and never raise."""
    from scripts.permuter import constraint_solver as csmod
    monkeypatch.setenv("PERMUTER_STACK_SLOT_ORACLE", "1")
    ctx = make_context(_SWAP_SRC, "test_func", _empty_diag())
    ctx.symbol = None
    constraints = csmod.extract_constraints(ctx)
    assert constraints.oracle_swap_pairs == []
