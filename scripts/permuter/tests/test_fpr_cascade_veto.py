"""Tests for the dominant-FPR-cascade veto in structural ``relevant()`` gates.

Three patterns — ``store_then_compound_add``, ``demorgan_guard`` and
``cache_repeated_call`` — used to fire ``asm_signal_match`` on functions whose
real and overwhelming blocker is a floating-point register-allocation cascade,
not the structural shape they address. Each swept with zero gain (one BUILD
FAILED). The fix: a shared ``is_fpr_cascade_dominated`` veto that returns False
from ``relevant()`` when the diff is dominated by multi-instruction FPR swaps.

These tests pin the veto's threshold behavior on synthetic diagnoses modeled on
the two confirmed false positives (VocalPart::GetNoteSliceWeight: 13 multi-instr
FPR pairs; CharSleeve::Poll: 31) and the confirmed wins that must NOT be vetoed
(Color::MakeColor: 8 multi-instr FPR pairs; FindBestScoringHint: 0).
"""

from __future__ import annotations

from scripts.permuter.classifier import (
    _FPR_CASCADE_PAIR_THRESHOLD,
    count_multi_instruction_fpr_swap_pairs,
    is_fpr_cascade_dominated,
)
from scripts.permuter.patterns.base import get_pattern
from scripts.permuter.types import Cluster, DiffOp, Diagnosis, SwapInfo


# ---------------------------------------------------------------------------
# Diagnosis factories
# ---------------------------------------------------------------------------

def _base(total: int = 200) -> Diagnosis:
    return Diagnosis(
        total_instructions=total,
        match_counts={"match": total - 1, "mismatch": 1},
        reg_swap_pairs={},
        offset_deltas={},
        diff_ops=[],
        clusters=[],
        noise_explained=0,
        noise_total=0,
    )


def _fpr_cascade(num_multi_fpr_pairs: int) -> Diagnosis:
    """A diagnosis dominated by N multi-instruction FPR swap pairs.

    Also carries a weak structural signal (a store diff_op + a cluster with a
    target-only ``stw`` and a ``bl``) so the patterns' opcode/cluster gates
    WOULD otherwise fire — proving the veto, not the absence of signal, is what
    excludes the function.
    """
    d = _base()
    pairs: dict[tuple[str, str], SwapInfo] = {}
    for i in range(num_multi_fpr_pairs):
        # Spread the FPR pairs across the f14..f31 callee-saved + f0..f13
        # volatile ranges; multi-instruction => first_idx != last_idx.
        a = f"f{(i % 18) + 14}"
        b = f"f{((i + 1) % 18) + 14}"
        pairs[(a, b)] = SwapInfo(count=3, first_idx=10 + i, last_idx=40 + i)
    d.reg_swap_pairs = pairs
    # Weak structural signals every gate keys on:
    d.diff_ops = [DiffOp(index=5, target_opcode="stw", base_opcode="lwz")]
    d.clusters = [
        Cluster(
            start_idx=8, end_idx=12, size=3, inserts=1, deletes=2,
            target_opcodes=("stw", "bl"), base_opcodes=("bl",),
        )
    ]
    return d


def _clean_structural() -> Diagnosis:
    """A clean structural diagnosis: store/bl signal, no FPR cascade."""
    d = _base()
    d.diff_ops = [
        DiffOp(index=5, target_opcode="stw", base_opcode="lwz"),
        DiffOp(index=9, target_opcode="bl", base_opcode="bl"),
    ]
    d.clusters = [
        Cluster(
            start_idx=8, end_idx=10, size=2, inserts=1, deletes=1,
            target_opcodes=("stw", "bl"), base_opcodes=("bl",),
        )
    ]
    # A single benign GPR swap (declaration-reorder territory) is fine.
    d.reg_swap_pairs = {("r20", "r21"): SwapInfo(count=2, first_idx=30, last_idx=31)}
    return d


def _single_instr_fpr_only() -> Diagnosis:
    """Many single-instruction FPR swaps (commutative, fma_reorder's class).

    These are NOT multi-instruction cascades, so the veto must NOT fire even at
    high count — they are potentially fixable by operand commutation.
    """
    d = _clean_structural()
    pairs: dict[tuple[str, str], SwapInfo] = {}
    for i in range(20):
        a = f"f{i % 13}"
        b = f"f{(i + 1) % 13}"
        pairs[(a, b)] = SwapInfo(count=1, first_idx=50 + i, last_idx=50 + i)
    d.reg_swap_pairs = pairs
    return d


# ---------------------------------------------------------------------------
# Helper-level tests
# ---------------------------------------------------------------------------

def test_threshold_is_between_evidence_cases():
    """The configured threshold separates wins (<=8) from false positives (>=13)."""
    assert 8 < _FPR_CASCADE_PAIR_THRESHOLD <= 13


def test_count_only_counts_multi_instruction_fpr_pairs():
    # 5 multi-instruction FPR pairs.
    assert count_multi_instruction_fpr_swap_pairs(_fpr_cascade(5)) == 5
    # Single-instruction FPR swaps don't count, even at volume.
    assert count_multi_instruction_fpr_swap_pairs(_single_instr_fpr_only()) == 0
    # A lone GPR swap doesn't count.
    assert count_multi_instruction_fpr_swap_pairs(_clean_structural()) == 0


def test_veto_fires_on_getnotesliceweight_profile():
    # GetNoteSliceWeight: 13 multi-instruction FPR swap pairs.
    assert is_fpr_cascade_dominated(_fpr_cascade(13)) is True


def test_veto_fires_on_charsleeve_poll_profile():
    # CharSleeve::Poll: 31 multi-instruction FPR swap pairs.
    assert is_fpr_cascade_dominated(_fpr_cascade(31)) is True


def test_veto_does_not_fire_on_makecolor_profile():
    # Color::MakeColor: 8 multi-instruction FPR swap pairs — a real win.
    assert is_fpr_cascade_dominated(_fpr_cascade(8)) is False


def test_veto_does_not_fire_on_clean_structural():
    assert is_fpr_cascade_dominated(_clean_structural()) is False


def test_veto_ignores_single_instruction_fpr_swaps():
    # 20 single-instruction FPR swaps must not be vetoed (fma_reorder's domain).
    assert is_fpr_cascade_dominated(_single_instr_fpr_only()) is False


# ---------------------------------------------------------------------------
# Pattern-level tests — relevant() must honor the veto
# ---------------------------------------------------------------------------

_VETOED_PATTERNS = ("store_then_compound_add", "demorgan_guard", "cache_repeated_call")


def test_patterns_veto_getnotesliceweight_profile():
    diag = _fpr_cascade(13)
    for name in _VETOED_PATTERNS:
        assert get_pattern(name).relevant(diag) is False, name


def test_patterns_veto_charsleeve_profile():
    diag = _fpr_cascade(31)
    for name in _VETOED_PATTERNS:
        assert get_pattern(name).relevant(diag) is False, name


def test_patterns_keep_makecolor_profile():
    # 8 multi-instr FPR pairs + a store/bl structural signal: must stay relevant.
    diag = _fpr_cascade(8)
    for name in _VETOED_PATTERNS:
        assert get_pattern(name).relevant(diag) is True, name


def test_patterns_keep_clean_structural():
    diag = _clean_structural()
    for name in _VETOED_PATTERNS:
        assert get_pattern(name).relevant(diag) is True, name
