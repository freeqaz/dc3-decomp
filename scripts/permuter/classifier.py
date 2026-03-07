"""Mismatch classifier — triage a Diagnosis into fixable vs unfixable categories.

Run after scorer.get_baseline(), before pattern generation. Classifications
inform budget allocation and can skip patterns that have no chance of helping.

Categories and fixability:

  FIXABLE:
    float_promotion     — fdiv↔fdivs / frsp presence (suffix/func fix)
    assert_line_delta   — uniform li immediate delta (adjust constants)
    missing_guard       — cmplwi+beq delete cluster (add null check)
    branch_polarity     — beq↔bne (if/else invert, sometimes fixable)
    comparison_sign     — cmpwi↔cmplwi (signed/unsigned cast)

  MAYBE_FIXABLE:
    callee_saved_regswap — r13-r31 / f14-f31 swaps (declaration reorder)

  UNFIXABLE:
    volatile_regswap    — r0-r12 / f0-f13 swaps (compiler-internal)
    prologue_mismatch   — different __savegprlr_N count
    sret_allocation     — stack offset swap only (compiler allocation order)
    fpr_scheduling      — same FP ops, different order (liveness-based)
    instruction_sched   — same 2 instructions reversed (arg preparation)
    icf_merged          — merged_<addr> symbol (linker ICF)
    static_guard        — ??_B vs $S guard naming
"""

from __future__ import annotations

from dataclasses import dataclass

from .types import Diagnosis

# Volatile register ranges (compiler-internal allocation, unfixable)
_VOLATILE_GPR = {f"r{i}" for i in range(0, 13)}  # r0-r12
_VOLATILE_FPR = {f"f{i}" for i in range(0, 14)}  # f0-f13
_VOLATILE_REGS = _VOLATILE_GPR | _VOLATILE_FPR

# Callee-saved register ranges (sometimes fixable via declaration reorder)
_CALLEE_SAVED_GPR = {f"r{i}" for i in range(13, 32)}  # r13-r31
_CALLEE_SAVED_FPR = {f"f{i}" for i in range(14, 32)}  # f14-f31

# FP single-precision opcodes
_FP_SINGLE = {"fdivs", "fmuls", "fadds", "fsubs", "fmadds", "fmsubs", "fnmsubs", "fnmadds"}
# FP double-precision opcodes
_FP_DOUBLE = {"fdiv", "fmul", "fadd", "fsub", "fmadd", "fmsub", "fnmsub", "fnmadd"}


@dataclass
class MismatchClassification:
    """A single classified mismatch type."""

    category: str
    fixable: str  # "yes", "maybe", "no"
    confidence: float  # 0.0-1.0
    instructions_affected: int
    detail: str

    @property
    def is_fixable(self) -> bool:
        return self.fixable == "yes"

    @property
    def is_maybe_fixable(self) -> bool:
        return self.fixable == "maybe"

    @property
    def is_unfixable(self) -> bool:
        return self.fixable == "no"


def classify_mismatches(diagnosis: Diagnosis) -> list[MismatchClassification]:
    """Analyze a Diagnosis and classify each mismatch type."""
    results: list[MismatchClassification] = []

    # 1. Register swap classification
    for (r1, r2), info in diagnosis.reg_swap_pairs.items():
        is_r1_volatile = r1 in _VOLATILE_REGS
        is_r2_volatile = r2 in _VOLATILE_REGS

        if is_r1_volatile and is_r2_volatile:
            results.append(MismatchClassification(
                category="volatile_regswap",
                fixable="no",
                confidence=0.95,
                instructions_affected=info.count,
                detail=f"{r1}<->{r2} ({info.count} instructions, both volatile)",
            ))
        elif is_r1_volatile or is_r2_volatile:
            # Mixed: one volatile, one callee-saved — callee-saved might be fixable
            results.append(MismatchClassification(
                category="callee_saved_regswap",
                fixable="maybe",
                confidence=0.7,
                instructions_affected=info.count,
                detail=f"{r1}<->{r2} ({info.count} instructions, mixed volatile/callee-saved)",
            ))
        else:
            results.append(MismatchClassification(
                category="callee_saved_regswap",
                fixable="maybe",
                confidence=0.6,
                instructions_affected=info.count,
                detail=f"{r1}<->{r2} ({info.count} instructions, callee-saved)",
            ))

    # 2. Prologue mismatch
    if diagnosis.has_prologue_mismatch:
        gpr_delta = diagnosis.gpr_save_delta
        fpr_delta = diagnosis.fpr_save_delta
        detail_parts = []
        affected = 0
        if gpr_delta != 0:
            detail_parts.append(
                f"GPR: target saves {diagnosis.target_gpr_saves}, "
                f"ours saves {diagnosis.base_gpr_saves} (delta={gpr_delta:+d})"
            )
            affected += abs(gpr_delta) * 2  # save + restore
        if fpr_delta != 0:
            detail_parts.append(
                f"FPR: target saves {diagnosis.target_fpr_saves}, "
                f"ours saves {diagnosis.base_fpr_saves} (delta={fpr_delta:+d})"
            )
            affected += abs(fpr_delta) * 2
        results.append(MismatchClassification(
            category="prologue_mismatch",
            fixable="no",
            confidence=0.85,
            instructions_affected=affected,
            detail="; ".join(detail_parts),
        ))

    # 3. Float promotion detection
    fp_promo_count = 0
    for d in diagnosis.diff_ops:
        if d.target_opcode in _FP_SINGLE and d.base_opcode in _FP_DOUBLE:
            fp_promo_count += 1
        elif d.target_opcode in _FP_DOUBLE and d.base_opcode in _FP_SINGLE:
            fp_promo_count += 1
        elif d.target_opcode == "frsp" or d.base_opcode == "frsp":
            fp_promo_count += 1
        elif (d.target_opcode == "lfd" and d.base_opcode == "lfs") or \
             (d.target_opcode == "lfs" and d.base_opcode == "lfd"):
            fp_promo_count += 1
    if fp_promo_count > 0:
        results.append(MismatchClassification(
            category="float_promotion",
            fixable="yes",
            confidence=0.8,
            instructions_affected=fp_promo_count,
            detail=f"{fp_promo_count} FP single/double opcode mismatches",
        ))

    # 4. Assert line delta detection
    # offset_deltas with small uniform values suggest line number drift
    small_deltas = {
        d: c for d, c in diagnosis.offset_deltas.items()
        if 1 <= abs(d) <= 30 and c >= 2
    }
    if small_deltas:
        top_delta = max(small_deltas.items(), key=lambda x: x[1])
        results.append(MismatchClassification(
            category="assert_line_delta",
            fixable="yes",
            confidence=0.6,  # Lower confidence — offset_deltas mixes struct offsets too
            instructions_affected=top_delta[1],
            detail=f"dominant small delta={top_delta[0]:+d} ({top_delta[1]} instructions)",
        ))

    # 5. Branch polarity
    branch_polarity_count = 0
    for d in diagnosis.diff_ops:
        if _is_branch_polarity_swap(d.target_opcode, d.base_opcode):
            branch_polarity_count += 1
    if branch_polarity_count > 0:
        results.append(MismatchClassification(
            category="branch_polarity",
            fixable="yes" if branch_polarity_count <= 2 else "maybe",
            confidence=0.5,
            instructions_affected=branch_polarity_count,
            detail=f"{branch_polarity_count} branch polarity swaps",
        ))

    # 6. Comparison signedness
    cmp_sign_count = 0
    for d in diagnosis.diff_ops:
        if _is_comparison_sign_swap(d.target_opcode, d.base_opcode):
            cmp_sign_count += 1
    if cmp_sign_count > 0:
        results.append(MismatchClassification(
            category="comparison_sign",
            fixable="yes",
            confidence=0.7,
            instructions_affected=cmp_sign_count,
            detail=f"{cmp_sign_count} signed/unsigned comparison swaps",
        ))

    # 7. Insert/delete clusters — could be missing guards, missing calls, etc.
    for cluster in diagnosis.clusters:
        if cluster.deletes > 0 and cluster.inserts == 0 and cluster.size <= 3:
            results.append(MismatchClassification(
                category="missing_guard",
                fixable="yes",
                confidence=0.5,
                instructions_affected=cluster.size,
                detail=f"delete cluster at idx {cluster.start_idx}-{cluster.end_idx} "
                       f"({cluster.size} instructions)",
            ))

    return results


def compute_fixability_score(classifications: list[MismatchClassification]) -> float:
    """Compute 0.0-1.0 score representing how likely the function is fixable.

    1.0 = all mismatches are fixable, 0.0 = all unfixable.
    """
    if not classifications:
        return 0.5  # No classification data — assume neutral

    total_affected = sum(c.instructions_affected for c in classifications)
    if total_affected == 0:
        return 0.5

    fixable_weight = 0.0
    for c in classifications:
        weight = c.instructions_affected / total_affected
        if c.fixable == "yes":
            fixable_weight += weight * c.confidence
        elif c.fixable == "maybe":
            fixable_weight += weight * c.confidence * 0.3
        # "no" contributes 0

    return min(1.0, fixable_weight)


def format_classifications(classifications: list[MismatchClassification]) -> str:
    """Format classifications as a human-readable summary."""
    if not classifications:
        return "No classifications"

    lines = []
    # Group regswap classifications to avoid verbosity
    regswaps_by_fix: dict[str, list[MismatchClassification]] = {}
    non_regswaps: list[MismatchClassification] = []

    for c in sorted(classifications, key=lambda x: x.instructions_affected, reverse=True):
        if c.category in ("volatile_regswap", "callee_saved_regswap"):
            regswaps_by_fix.setdefault(c.fixable, []).append(c)
        else:
            non_regswaps.append(c)

    # Show non-regswap classifications individually
    for c in non_regswaps:
        icon = {"yes": "+", "maybe": "~", "no": "-"}[c.fixable]
        lines.append(f"  [{icon}] {c.category}: {c.detail}")

    # Show regswaps in condensed form
    for fixable, group in sorted(regswaps_by_fix.items(),
                                  key=lambda x: {"no": 0, "maybe": 1, "yes": 2}[x[0]]):
        icon = {"yes": "+", "maybe": "~", "no": "-"}[fixable]
        total_instrs = sum(c.instructions_affected for c in group)
        if len(group) <= 2:
            for c in group:
                lines.append(f"  [{icon}] {c.category}: {c.detail}")
        else:
            cat = group[0].category
            top_pairs = [c.detail.split(" (")[0] for c in group[:3]]
            lines.append(
                f"  [{icon}] {cat}: {len(group)} pairs, "
                f"{total_instrs} instructions ({', '.join(top_pairs)}, ...)"
            )

    score = compute_fixability_score(classifications)
    lines.append(f"  Fixability score: {score:.2f}")
    return "\n".join(lines)


def _is_branch_polarity_swap(op1: str, op2: str) -> bool:
    """Check if two opcodes represent a branch polarity swap (beq↔bne etc.)."""
    _PAIRS = {
        ("beq", "bne"), ("bne", "beq"),
        ("blt", "bge"), ("bge", "blt"),
        ("bgt", "ble"), ("ble", "bgt"),
    }
    # Strip prediction hints (+/-)
    a = op1.rstrip("+-")
    b = op2.rstrip("+-")
    return (a, b) in _PAIRS


def _is_comparison_sign_swap(op1: str, op2: str) -> bool:
    """Check if two opcodes represent a signed↔unsigned comparison swap."""
    _PAIRS = {
        ("cmpwi", "cmplwi"), ("cmplwi", "cmpwi"),
        ("cmpw", "cmplw"), ("cmplw", "cmpw"),
    }
    return (op1, op2) in _PAIRS
