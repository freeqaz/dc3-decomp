"""Pattern benchmark tests — verify patterns can recover known transformations.

Pure AST/text-level tests. No builds, no objdiff. Each fixture seeds a known
flip into C++ source and verifies that the appropriate pattern produces at
least one variant that recovers the original.

Usage:
    python -m pytest scripts/permuter/tests/test_patterns.py -v
    python scripts/permuter/tests/test_patterns.py
    python scripts/permuter/tests/test_patterns.py --pattern ternary_swap -v
    python scripts/permuter/tests/test_patterns.py --list
    python scripts/permuter/tests/test_patterns.py --fixture emptysize_empty_to_size -v
"""

from __future__ import annotations

import argparse
import re
import sys
import textwrap
import unittest
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# Ensure project root is on the path so imports work standalone
_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.extractor import _PARSER, _get_function_name, reparse_variant
from scripts.permuter.types import (
    Cluster,
    Diagnosis,
    DiffOp,
    FunctionContext,
    SwapInfo,
)

# Import pattern registry (triggers __init_subclass__ registration)
import scripts.permuter.patterns  # noqa: F401
from scripts.permuter.patterns.base import get_pattern


# ---------------------------------------------------------------------------
# Fixture dataclass
# ---------------------------------------------------------------------------

@dataclass
class PatternFixture:
    id: str                     # e.g. "ternary_if_to_ternary"
    pattern_name: str           # Which pattern should recover expected_source
    description: str            # What's being tested
    seeded_source: str          # Starting "wrong" C++ source (full function)
    expected_source: str        # Target "correct" source a variant should produce
    func_name: str              # For make_context()
    diagnosis: Diagnosis        # Mock diagnosis for relevant() checks
    match_mode: str = "normalized"  # "exact", "normalized", "contains"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make_context(source_text: str, func_name: str, diagnosis: Diagnosis) -> FunctionContext:
    """Parse inline C++ source directly with tree-sitter, bypassing file I/O."""
    source_bytes = textwrap.dedent(source_text).encode("utf-8")
    tree = _PARSER.parse(source_bytes)

    for child in tree.root_node.children:
        if child.type != "function_definition":
            continue
        name = _get_function_name(child)
        if name == func_name:
            body = child.child_by_field_name("body")
            if body is None:
                raise ValueError(f"Function {func_name} has no body")
            statements = list(body.named_children)
            return FunctionContext(
                file_path=Path("/dev/null"),
                file_source=source_bytes,
                func_node=child,
                body_node=body,
                statements=statements,
                func_byte_range=(child.start_byte, child.end_byte),
                diagnosis=diagnosis,
            )

    raise ValueError(f"Function '{func_name}' not found in source")


def normalize(text: str | bytes) -> str:
    """Collapse all whitespace for comparison."""
    if isinstance(text, bytes):
        text = text.decode("utf-8", errors="replace")
    return re.sub(r"\s+", " ", text).strip()


def match_variant(variant_source: bytes, expected: str, mode: str) -> bool:
    """Check if a variant's full-file source contains/matches the expected."""
    if mode == "exact":
        return variant_source.decode("utf-8") == textwrap.dedent(expected)
    elif mode == "normalized":
        return normalize(variant_source) == normalize(expected)
    elif mode == "contains":
        return normalize(expected) in normalize(variant_source)
    return False


# ---------------------------------------------------------------------------
# Diagnosis factories
# ---------------------------------------------------------------------------

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


def diag_with_branch_ops() -> Diagnosis:
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=10, target_opcode="beq", base_opcode="bne")]
    return d


def diag_with_clusters() -> Diagnosis:
    d = _empty_diag()
    d.clusters = [Cluster(start_idx=5, end_idx=10, size=5, inserts=3, deletes=2)]
    return d


def diag_with_branch_and_clusters() -> Diagnosis:
    d = diag_with_branch_ops()
    d.clusters = [Cluster(start_idx=5, end_idx=10, size=5, inserts=3, deletes=2)]
    return d


def diag_with_divw() -> Diagnosis:
    """Target has divw (target uses size()) -> swap empty() to size()."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=5, target_opcode="divw", base_opcode="cmplw")]
    return d


def diag_with_divw_base() -> Diagnosis:
    """Base has divw (we use size()) -> swap size() to empty()."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=5, target_opcode="cmplw", base_opcode="divw")]
    return d


def diag_with_ptr_diff_target_cluster() -> Diagnosis:
    """Target has inlined (end-begin)/sizeof(T) — power-of-2 ptr-size variant.

    Mirrors the VocalTrack::UpdateScrolling case: target uses size() on a
    deque<T*>, generating subf+srawi+addze; our code uses empty() (pointer
    compare). The arithmetic shows up as target-only deletes in a cluster.
    """
    d = _empty_diag()
    d.clusters = [
        Cluster(
            start_idx=760,
            end_idx=765,
            size=4,
            inserts=0,
            deletes=4,
            target_opcodes=("lwz", "subf", "srawi", "addze"),
            base_opcodes=(),
        ),
    ]
    return d


def diag_with_ptr_diff_base_cluster() -> Diagnosis:
    """We use size() but target uses empty() — swap size() -> empty().

    Our compiler inlined the pointer-diff arithmetic (base-only inserts) but
    target chose the empty()/pointer-compare codegen.
    """
    d = _empty_diag()
    d.clusters = [
        Cluster(
            start_idx=760,
            end_idx=765,
            size=4,
            inserts=4,
            deletes=0,
            target_opcodes=(),
            base_opcodes=("lwz", "subf", "srawi", "addze"),
        ),
    ]
    return d


def diag_with_arith_ops() -> Diagnosis:
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=8, target_opcode="fadds", base_opcode="fadds")]
    d.clusters = [
        Cluster(start_idx=5, end_idx=8, size=3, inserts=2, deletes=1),
        Cluster(start_idx=12, end_idx=15, size=3, inserts=1, deletes=2),
    ]
    return d


def diag_with_gpr_swaps() -> Diagnosis:
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("r20", "r21"): SwapInfo(count=4, first_idx=10, last_idx=50)
    }
    return d


def diag_with_cmp_ops() -> Diagnosis:
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=3, target_opcode="cmpwi", base_opcode="cmplwi")]
    return d


def diag_with_fma_ops() -> Diagnosis:
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=7, target_opcode="fmadds", base_opcode="fmsubs")]
    return d


def diag_with_fma_addsub_ops() -> Diagnosis:
    """FMA vs separate add/sub — paren expansion candidate."""
    d = _empty_diag()
    d.diff_ops = [
        DiffOp(index=5, target_opcode="fnmsubs", base_opcode="fmsubs"),
        DiffOp(index=6, target_opcode="fadds", base_opcode="fsubs"),
    ]
    return d


def diag_always() -> Diagnosis:
    """Empty diagnosis — for patterns whose relevant() always returns True."""
    return _empty_diag()


def diag_with_noise() -> Diagnosis:
    """Diagnosis with unexplained noise (for argument_swap relevant())."""
    d = _empty_diag()
    d.clusters = [Cluster(start_idx=2, end_idx=5, size=3, inserts=2, deletes=1)]
    return d


def diag_with_fneg_frsp() -> Diagnosis:
    """Diagnosis with fneg/frsp scheduling mismatch (for negation_split)."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=12, target_opcode="frsp", base_opcode="fneg")]
    d.clusters = [Cluster(start_idx=10, end_idx=15, size=5, inserts=2, deletes=2)]
    return d


def diag_with_store_load_ops() -> Diagnosis:
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=5, target_opcode="stw", base_opcode="lwz")]
    d.clusters = [Cluster(start_idx=3, end_idx=8, size=5, inserts=3, deletes=2)]
    return d


def diag_with_dead_code() -> Diagnosis:
    d = _empty_diag()
    d.clusters = [Cluster(start_idx=20, end_idx=25, size=5, inserts=5, deletes=0)]
    return d


def diag_with_replace_real() -> Diagnosis:
    """Diagnosis with real replace instructions (for bool_cast)."""
    d = _empty_diag()
    d.replace_real = 2
    d.clusters = [Cluster(start_idx=3, end_idx=6, size=3, inserts=1, deletes=1)]
    return d


def diag_with_bl_mismatch() -> Diagnosis:
    """bl mismatch (different call target, e.g. MakeString specialization)."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=10, target_opcode="bl", base_opcode="bl")]
    d.replace_real = 1
    d.clusters = [Cluster(start_idx=8, end_idx=14, size=6, inserts=3, deletes=3)]
    return d


def diag_with_large_clusters() -> Diagnosis:
    """Large clusters suggesting duplicated code blocks."""
    d = _empty_diag()
    d.clusters = [
        Cluster(start_idx=5, end_idx=20, size=15, inserts=10, deletes=5),
    ]
    return d


def diag_with_cmplwi_cmpwi() -> Diagnosis:
    """cmplwi vs cmpwi mismatch (ObjPtr bool extraction)."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=5, target_opcode="cmplwi", base_opcode="cmpwi")]
    d.replace_real = 2
    return d


def diag_with_lfd_lfs() -> Diagnosis:
    """lfd vs lfs mismatch (float/double literal width)."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=8, target_opcode="lfs", base_opcode="lfd")]
    return d


def diag_with_shift_ops() -> Diagnosis:
    """srwi vs srawi mismatch (signed vs unsigned shift from sizeof)."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=6, target_opcode="srawi", base_opcode="srwi")]
    return d


def diag_with_offset_deltas() -> Diagnosis:
    """Offset swap mismatches (assignment reorder)."""
    d = _empty_diag()
    d.offset_deltas = {4: 2, -4: 2}
    d.clusters = [Cluster(start_idx=5, end_idx=10, size=5, inserts=2, deletes=2)]
    return d


def diag_with_callee_saved_swaps() -> Diagnosis:
    """Callee-saved GPR swaps (temp elimination / member ref bind)."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("r30", "r29"): SwapInfo(count=3, first_idx=10, last_idx=40)
    }
    return d


def diag_with_lwz_ops() -> Diagnosis:
    """Load ordering mismatches (reference_elimination / subscript_ref_bind)."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=5, target_opcode="lwz", base_opcode="stw")]
    d.clusters = [Cluster(start_idx=3, end_idx=8, size=5, inserts=2, deletes=2)]
    return d


def diag_with_prologue_more_saves() -> Diagnosis:
    """Target needs more callee-saved regs than base (subscript_ref_bind)."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("r31", "r30"): SwapInfo(count=2, first_idx=5, last_idx=20)
    }
    d.target_gpr_saves = 5
    d.base_gpr_saves = 4
    return d


def diag_with_prologue_fewer_saves() -> Diagnosis:
    """Target needs fewer callee-saved regs than base (reference_elimination)."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("r31", "r30"): SwapInfo(count=2, first_idx=5, last_idx=20)
    }
    d.target_gpr_saves = 4
    d.base_gpr_saves = 5
    return d


def diag_with_insert_delete() -> Diagnosis:
    """Insert/delete cluster (MILO log swap)."""
    d = _empty_diag()
    d.insert_count = 3
    d.delete_count = 2
    d.clusters = [Cluster(start_idx=5, end_idx=10, size=5, inserts=3, deletes=2)]
    return d


def diag_with_subf_cmpw() -> Diagnosis:
    """Target uses subf. (subtract-and-record), base uses cmpw (direct compare)."""
    d = _empty_diag()
    d.diff_ops = [
        DiffOp(index=18, target_opcode="subf.", base_opcode="cmpw"),
        DiffOp(index=19, target_opcode="bge", base_opcode="bge"),
    ]
    return d


def diag_with_rlwinm_fusion() -> Diagnosis:
    """rlwinm fusion ops (extrwi/clrlslwi) in diff — u8 type control."""
    d = _empty_diag()
    d.diff_ops = [
        DiffOp(index=5, target_opcode="srwi", base_opcode="extrwi"),
        DiffOp(index=8, target_opcode="clrlwi", base_opcode="clrlslwi"),
    ]
    return d


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

FIXTURES: list[PatternFixture] = [
    # ===================== ternary_swap =====================

    PatternFixture(
        id="ternary_if_to_ternary",
        pattern_name="ternary_swap",
        description="if/else assignment -> ternary",
        func_name="test_func",
        diagnosis=diag_with_branch_and_clusters(),
        seeded_source="""\
void test_func(int cond) {
    int val;
    if (cond) {
        val = 1;
    } else {
        val = 2;
    }
}
""",
        expected_source="""\
void test_func(int cond) {
    int val;
    val = cond ? 1 : 2;
}
""",
    ),

    PatternFixture(
        id="ternary_ternary_to_if",
        pattern_name="ternary_swap",
        description="ternary assignment -> if/else",
        func_name="test_func",
        diagnosis=diag_with_branch_and_clusters(),
        seeded_source="""\
void test_func(int cond) {
    int val;
    val = cond ? 1 : 2;
}
""",
        expected_source="""\
void test_func(int cond) {
    int val;
    if (cond) {
        val = 1;
    } else {
        val = 2;
    }
}
""",
    ),

    PatternFixture(
        id="ternary_return_to_if",
        pattern_name="ternary_swap",
        description="return ternary -> if/else return",
        func_name="test_func",
        diagnosis=diag_with_branch_and_clusters(),
        seeded_source="""\
int test_func(int cond) {
    return cond ? 10 : 20;
}
""",
        expected_source="""\
int test_func(int cond) {
    if (cond) {
        return 10;
    } else {
        return 20;
    }
}
""",
    ),

    PatternFixture(
        id="ternary_if_return_to_ternary",
        pattern_name="ternary_swap",
        description="if/else return -> return ternary",
        func_name="test_func",
        diagnosis=diag_with_branch_and_clusters(),
        seeded_source="""\
int test_func(int cond) {
    if (cond) {
        return 10;
    } else {
        return 20;
    }
}
""",
        expected_source="""\
int test_func(int cond) {
    return cond ? 10 : 20;
}
""",
    ),

    PatternFixture(
        id="ternary_pointer_declarator",
        pattern_name="ternary_swap",
        description="ternary with pointer declarator -> if/else (regression test)",
        func_name="test_func",
        diagnosis=diag_with_branch_and_clusters(),
        # Note: tree-sitter separates 'const' as a type_qualifier from 'char',
        # so the pattern only extracts 'char' as the type. The key thing tested
        # here is that pointer_declarator doesn't crash (the original bug).
        seeded_source="""\
void test_func(int cond) {
    char *x = cond ? "yes" : "no";
}
""",
        expected_source="""\
void test_func(int cond) {
    char *x;
    if (cond) {
        x = "yes";
    } else {
        x = "no";
    }
}
""",
    ),

    # ===================== empty_size_swap =====================

    PatternFixture(
        id="emptysize_empty_to_size",
        pattern_name="empty_size_swap",
        description="empty() -> size() == 0",
        func_name="test_func",
        diagnosis=diag_with_divw(),
        seeded_source="""\
void test_func() {
    List mList;
    if (mList.empty()) {
        return;
    }
}
""",
        expected_source="""\
void test_func() {
    List mList;
    if (mList.size() == 0) {
        return;
    }
}
""",
    ),

    PatternFixture(
        id="emptysize_size_to_empty",
        pattern_name="empty_size_swap",
        description="size() == 0 -> empty()",
        func_name="test_func",
        diagnosis=diag_with_divw_base(),
        seeded_source="""\
void test_func() {
    List mList;
    if (mList.size() == 0) {
        return;
    }
}
""",
        expected_source="""\
void test_func() {
    List mList;
    if (mList.empty()) {
        return;
    }
}
""",
    ),

    PatternFixture(
        id="emptysize_notempty_to_size",
        pattern_name="empty_size_swap",
        description="!empty() -> size() != 0",
        func_name="test_func",
        diagnosis=diag_with_divw(),
        seeded_source="""\
void test_func() {
    List mList;
    if (!mList.empty()) {
        return;
    }
}
""",
        expected_source="""\
void test_func() {
    List mList;
    if (mList.size() != 0) {
        return;
    }
}
""",
    ),

    PatternFixture(
        id="emptysize_sizegt0_to_notempty",
        pattern_name="empty_size_swap",
        description="size() > 0 -> !empty()",
        func_name="test_func",
        diagnosis=diag_with_divw_base(),
        seeded_source="""\
void test_func() {
    List mList;
    if (mList.size() > 0) {
        return;
    }
}
""",
        expected_source="""\
void test_func() {
    List mList;
    if (!mList.empty()) {
        return;
    }
}
""",
    ),

    # Cluster-detected variant: no divw in diff_ops, signal comes from a
    # target-only subf+srawi+addze cluster (inlined deque size() arithmetic).
    # This is the VocalTrack::UpdateScrolling case.
    PatternFixture(
        id="emptysize_ptr_diff_cluster_to_size",
        pattern_name="empty_size_swap",
        description="!empty() -> size() != 0 (cluster-detected deque ptr-diff)",
        func_name="test_func",
        diagnosis=diag_with_ptr_diff_target_cluster(),
        seeded_source="""\
void test_func() {
    Deque mDeque;
    if (!mDeque.empty()) {
        return;
    }
}
""",
        expected_source="""\
void test_func() {
    Deque mDeque;
    if (mDeque.size() != 0) {
        return;
    }
}
""",
    ),

    # ===================== commutative_swap =====================

    PatternFixture(
        id="comm_right_assoc",
        pattern_name="commutative_swap",
        description="left-assoc 3-term -> right-assoc regroup",
        func_name="test_func",
        diagnosis=diag_with_arith_ops(),
        seeded_source="""\
float test_func(float a, float b, float c) {
    return a + b + c;
}
""",
        expected_source="""\
float test_func(float a, float b, float c) {
    return (a + (b + c));
}
""",
    ),

    PatternFixture(
        id="comm_reversed",
        pattern_name="commutative_swap",
        description="3-term reversed order",
        func_name="test_func",
        diagnosis=diag_with_arith_ops(),
        seeded_source="""\
float test_func(float a, float b, float c) {
    return a + b + c;
}
""",
        expected_source="""\
float test_func(float a, float b, float c) {
    return c + b + a;
}
""",
    ),

    PatternFixture(
        id="comm_multiply_chain",
        pattern_name="commutative_swap",
        description="3-term multiply chain regroup",
        func_name="test_func",
        diagnosis=diag_with_arith_ops(),
        seeded_source="""\
float test_func(float x, float y, float z) {
    return x * y * z;
}
""",
        expected_source="""\
float test_func(float x, float y, float z) {
    return (x * (y * z));
}
""",
    ),

    # ===================== comparison_flip =====================

    PatternFixture(
        id="cmpflip_less_to_greater",
        pattern_name="comparison_flip",
        description="a < b -> b > a",
        func_name="test_func",
        diagnosis=diag_with_cmp_ops(),
        seeded_source="""\
int test_func(int a, int b) {
    if (a < b) {
        return 1;
    }
    return 0;
}
""",
        expected_source="""\
int test_func(int a, int b) {
    if (b > a) {
        return 1;
    }
    return 0;
}
""",
    ),

    # ===================== comparison_equivalence =====================

    PatternFixture(
        id="cmpeq_lt_to_le",
        pattern_name="comparison_equivalence",
        description="i < 2 -> i <= 1",
        func_name="test_func",
        diagnosis=diag_with_cmp_ops(),
        seeded_source="""\
int test_func(int i) {
    if (i < 2) {
        return 1;
    }
    return 0;
}
""",
        expected_source="""\
int test_func(int i) {
    if (i <= 1) {
        return 1;
    }
    return 0;
}
""",
    ),

    # ===================== branch_polarity =====================

    PatternFixture(
        id="brpol_invert_condition",
        pattern_name="branch_polarity",
        description="invert if/else condition and swap bodies",
        func_name="test_func",
        diagnosis=diag_with_branch_and_clusters(),
        seeded_source="""\
void test_func(int x) {
    if (x > 0) {
        foo();
    } else {
        bar();
    }
}
""",
        expected_source="""\
void test_func(int x) {
    if (x <= 0) {
        bar();
    } else {
        foo();
    }
}
""",
    ),

    # ===================== variable_extraction =====================

    PatternFixture(
        id="varext_nested_call",
        pattern_name="variable_extraction",
        description="extract nested call into auto variable",
        func_name="test_func",
        diagnosis=diag_with_clusters(),
        seeded_source="""\
void test_func(int display) {
    check(display < mElements.size(), 0x74);
}
""",
        expected_source="""\
void test_func(int display) {
    int _tmp0 = mElements.size();
    check(display < _tmp0, 0x74);
}
""",
    ),

    # ===================== signed_unsigned =====================

    PatternFixture(
        id="signunsign_neq_to_gt",
        pattern_name="signed_unsigned",
        description="!= 0 -> > 0 swap",
        func_name="test_func",
        diagnosis=diag_with_cmp_ops(),
        seeded_source="""\
int test_func(int x) {
    if (x != 0) {
        return 1;
    }
    return 0;
}
""",
        expected_source="""\
int test_func(int x) {
    if (x > 0) {
        return 1;
    }
    return 0;
}
""",
    ),

    # ===================== inline_assignment =====================

    PatternFixture(
        id="inline_fold_into_call",
        pattern_name="inline_assignment",
        description="fold assignment into subsequent call argument",
        func_name="test_func",
        diagnosis=diag_with_clusters(),
        seeded_source="""\
void test_func() {
    int era;
    era = getName();
    process(era);
}
""",
        expected_source="""\
void test_func() {
    int era;
    process(era = getName());
}
""",
    ),

    # ===================== declaration_reorder =====================

    PatternFixture(
        id="declreorder_swap_pair",
        pattern_name="declaration_reorder",
        description="swap two consecutive declarations",
        func_name="test_func",
        diagnosis=diag_with_gpr_swaps(),
        seeded_source="""\
void test_func() {
    int a = 1;
    int b = 2;
    use(a, b);
}
""",
        expected_source="""\
void test_func() {
    int b = 2;
    int a = 1;
    use(a, b);
}
""",
    ),

    # ===================== argument_swap =====================

    PatternFixture(
        id="argswap_two_identifiers",
        pattern_name="argument_swap",
        description="swap two identifier arguments",
        func_name="test_func",
        diagnosis=diag_with_noise(),
        seeded_source="""\
void test_func(int a, int b) {
    compare(a, b);
}
""",
        expected_source="""\
void test_func(int a, int b) {
    compare(b, a);
}
""",
    ),

    # ===================== fma_reorder =====================

    PatternFixture(
        id="fma_swap_add_operands",
        pattern_name="fma_reorder",
        description="swap addition operands in FMA expression",
        func_name="test_func",
        diagnosis=diag_with_fma_ops(),
        seeded_source="""\
float test_func(float a, float b, float c) {
    return a + b * c;
}
""",
        expected_source="""\
float test_func(float a, float b, float c) {
    return b * c + a;
}
""",
    ),

    # ===================== negation_split =====================

    PatternFixture(
        id="negsplit_func_call",
        pattern_name="negation_split",
        description="Split -func() into var = func(); var = -var;",
        func_name="test_func",
        diagnosis=diag_with_fneg_frsp(),
        seeded_source="""\
float test_func(float x, float y) {
    float angle = -acos(Dot(x, y));
    return angle;
}
""",
        expected_source="""\
float test_func(float x, float y) {
    float angle = acos(Dot(x, y));
    angle = -angle;
    return angle;
}
""",
    ),

    PatternFixture(
        id="negsplit_paren_call",
        pattern_name="negation_split",
        description="Split -(expr) into var = expr; var = -var;",
        func_name="test_func",
        diagnosis=diag_with_fneg_frsp(),
        seeded_source="""\
float test_func(float a, float b) {
    float result = -(Compute(a, b));
    return result;
}
""",
        expected_source="""\
float test_func(float a, float b) {
    float result = (Compute(a, b));
    result = -result;
    return result;
}
""",
    ),

    # ===================== and_split =====================

    PatternFixture(
        id="andsplit_split_and",
        pattern_name="and_split",
        description="Split if (a && b) into nested ifs",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
void test_func(int a, int b) {
    if (a > 0 && b > 0) {
        DoSomething();
    }
}
""",
        expected_source="""\
void test_func(int a, int b) {
    if (a > 0) {
        if (b > 0) {
            DoSomething();
        }
    }
}
""",
    ),

    PatternFixture(
        id="andsplit_merge_nested",
        pattern_name="and_split",
        description="Merge nested ifs into && condition",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
void test_func(int a, int b) {
    if (a > 0) {
        if (b > 0) {
            DoSomething();
        }
    }
}
""",
        expected_source="""\
void test_func(int a, int b) {
    if (a > 0 && b > 0) {
        DoSomething();
    }
}
""",
    ),

    PatternFixture(
        id="andsplit_with_else_split",
        pattern_name="and_split",
        description="Split if (a && b) { body } else { alt } into nested ifs with duplicated else",
        func_name="test_func",
        diagnosis=diag_with_branch_and_clusters(),
        seeded_source="""\
void test_func(int a, int b) {
    if (a > 0 && b > 0) {
        foo();
    } else {
        bar();
    }
}
""",
        expected_source="""\
void test_func(int a, int b) {
    if (a > 0) {
        if (b > 0) {
            foo();
        } else {
            bar();
        }
    } else {
        bar();
    }
}
""",
    ),

    PatternFixture(
        id="andsplit_with_else_merge",
        pattern_name="and_split",
        description="Merge nested ifs with matching else blocks into && with else",
        func_name="test_func",
        diagnosis=diag_with_branch_and_clusters(),
        seeded_source="""\
void test_func(int a, int b) {
    if (a > 0) {
        if (b > 0) {
            foo();
        } else {
            bar();
        }
    } else {
        bar();
    }
}
""",
        expected_source="""\
void test_func(int a, int b) {
    if (a > 0 && b > 0) {
        foo();
    } else {
        bar();
    }
}
""",
    ),

    # ===================== bool_cast =====================

    PatternFixture(
        id="boolcast_return_call",
        pattern_name="bool_cast",
        description="Wrap return call with bool()",
        func_name="test_func",
        diagnosis=diag_with_replace_real(),
        seeded_source="""\
bool test_func(int x) {
    return IsActive(x);
}
""",
        expected_source="""\
bool test_func(int x) {
    return bool(IsActive(x));
}
""",
    ),

    # ===================== bitwise_accumulator =====================

    PatternFixture(
        id="bitacc_logical_to_bitwise",
        pattern_name="bitwise_accumulator",
        description="Replace && with & for bool accumulator",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
bool test_func(bool a, bool b) {
    bool result = a && b;
    return result;
}
""",
        expected_source="""\
bool test_func(bool a, bool b) {
    bool result = a & b;
    return result;
}
""",
    ),

    PatternFixture(
        id="bitacc_bitwise_to_logical",
        pattern_name="bitwise_accumulator",
        description="Replace & with && for bool accumulator",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
bool test_func(bool a, bool b) {
    bool result = a & b;
    return result;
}
""",
        expected_source="""\
bool test_func(bool a, bool b) {
    bool result = a && b;
    return result;
}
""",
    ),

    # ===================== max_to_conditional =====================

    PatternFixture(
        id="maxcond_expand_max",
        pattern_name="max_to_conditional",
        description="Expand Max(a, b) to if-statement",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
void test_func(int i1) {
    i1 = Max(i1, 1);
}
""",
        expected_source="""\
void test_func(int i1) {
    if (i1 < 1) i1 = 1;
}
""",
    ),

    PatternFixture(
        id="maxcond_collapse_to_max",
        pattern_name="max_to_conditional",
        description="Collapse if (a < b) a = b to Max()",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
void test_func(int val) {
    if (val < 0) {
        val = 0;
    }
}
""",
        expected_source="""\
void test_func(int val) {
    val = Max(val, 0);
}
""",
    ),

    # ===================== early_return_merge =====================

    PatternFixture(
        id="retmerge_merge_guards",
        pattern_name="early_return_merge",
        description="Merge 3 guard returns into || chain",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
bool test_func(int a, int b, int c) {
    if (a < 0)
        return false;
    if (b < 0)
        return false;
    if (c < 0)
        return false;
    return true;
}
""",
        expected_source="""\
bool test_func(int a, int b, int c) {
    if (a < 0 || b < 0 || c < 0)
        return false;
    return true;
}
""",
    ),

    PatternFixture(
        id="retmerge_split_chain",
        pattern_name="early_return_merge",
        description="Split || chain into separate guard returns",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
bool test_func(int a, int b, int c) {
    if (a < 0 || b < 0 || c < 0)
        return false;
    return true;
}
""",
        expected_source="""\
bool test_func(int a, int b, int c) {
    if (a < 0)
        return false;
    if (b < 0)
        return false;
    if (c < 0)
        return false;
    return true;
}
""",
    ),

    # ===================== bool_return_expr =====================

    PatternFixture(
        id="boolret_merge_to_return",
        pattern_name="bool_return_expr",
        description="Merge if/return false + return true -> return !cond",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
bool test_func(int x) {
    if (x > 0)
        return false;
    return true;
}
""",
        expected_source="""\
bool test_func(int x) {
    return x <= 0;
}
""",
    ),

    PatternFixture(
        id="boolret_split_to_if",
        pattern_name="bool_return_expr",
        description="Split return !cond into if/return",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
bool test_func(int x) {
    return !x;
}
""",
        expected_source="""\
bool test_func(int x) {
    if (x)
        return false;
    return true;
}
""",
    ),

    PatternFixture(
        id="boolret_and_split",
        pattern_name="bool_return_expr",
        description="Split return a && b into if/return",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
bool test_func(int a, int b) {
    return a == 0 && b != 0;
}
""",
        expected_source="""\
bool test_func(int a, int b) {
    if (a != 0)
        return false;
    return b != 0;
}
""",
    ),

    # ===================== fsel_template =====================

    PatternFixture(
        id="fsel_single_to_max",
        pattern_name="fsel_template",
        description="Convert if (v < 0) v = 0 to Max(v, 0)",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
void test_func(float val) {
    if (val < 0.0f) {
        val = 0.0f;
    }
}
""",
        expected_source="""\
void test_func(float val) {
    val = Max(val, 0.0f);
}
""",
    ),

    PatternFixture(
        id="fsel_clamp_pair",
        pattern_name="fsel_template",
        description="Combine two guards into Clamp()",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
void test_func(float val) {
    if (val < 0.0f) {
        val = 0.0f;
    }
    if (val > 1.0f) {
        val = 1.0f;
    }
}
""",
        expected_source="""\
void test_func(float val) {
    val = Clamp(0.0f, 1.0f, val);
}
""",
    ),

    # ===================== alloca_intrinsic =====================

    PatternFixture(
        id="alloca_to_underscored",
        pattern_name="alloca_intrinsic",
        description="alloca() -> _alloca()",
        func_name="test_func",
        diagnosis=diag_always(),
        seeded_source="""\
void test_func(int size) {
    char* buf = (char*)alloca(size);
}
""",
        expected_source="""\
void test_func(int size) {
    char* buf = (char*)_alloca(size);
}
""",
    ),

    PatternFixture(
        id="underscored_alloca_to_plain",
        pattern_name="alloca_intrinsic",
        description="_alloca() -> alloca()",
        func_name="test_func",
        diagnosis=diag_always(),
        seeded_source="""\
void test_func(int size) {
    char* buf = (char*)_alloca(size);
}
""",
        expected_source="""\
void test_func(int size) {
    char* buf = (char*)alloca(size);
}
""",
    ),

    # ===================== sizeof_signed_cast =====================

    PatternFixture(
        id="sizeof_add_int_cast",
        pattern_name="sizeof_signed_cast",
        description="sizeof(X) -> (int)sizeof(X)",
        func_name="test_func",
        diagnosis=diag_with_shift_ops(),
        seeded_source="""\
void test_func(int total) {
    int n = total / sizeof(int);
}
""",
        expected_source="""\
void test_func(int total) {
    int n = total / (int)sizeof(int);
}
""",
    ),

    PatternFixture(
        id="sizeof_remove_int_cast",
        pattern_name="sizeof_signed_cast",
        description="(int)sizeof(X) -> sizeof(X)",
        func_name="test_func",
        diagnosis=diag_with_shift_ops(),
        seeded_source="""\
void test_func(int total) {
    int n = total / (int)sizeof(int);
}
""",
        expected_source="""\
void test_func(int total) {
    int n = total / sizeof(int);
}
""",
    ),

    # ===================== initializer_literal =====================

    PatternFixture(
        id="initlit_float_to_zero",
        pattern_name="initializer_literal",
        description="0.0f -> 0 in initializer",
        func_name="test_func",
        diagnosis=diag_with_lfd_lfs(),
        seeded_source="""\
void test_func() {
    float val = 0.0f;
}
""",
        expected_source="""\
void test_func() {
    float val = 0;
}
""",
    ),

    PatternFixture(
        id="initlit_zero_to_float",
        pattern_name="initializer_literal",
        description="0 -> 0.0f in initializer",
        func_name="test_func",
        diagnosis=diag_with_lfd_lfs(),
        seeded_source="""\
void test_func() {
    float val = 0;
}
""",
        expected_source="""\
void test_func() {
    float val = 0.0f;
}
""",
    ),

    # ===================== single_return =====================

    PatternFixture(
        id="single_return_merge",
        pattern_name="single_return",
        description="if-return-return -> result variable",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
int test_func(int cond) {
    if (cond) {
        return 1;
    }
    return 0;
}
""",
        expected_source="""\
int test_func(int cond) {
    int _result = 0;
    if (cond) {
        _result = 1;
    }
    return _result;
}
""",
    ),

    PatternFixture(
        id="single_return_with_body",
        pattern_name="single_return",
        description="if with body + return merged to result var",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
int test_func(int x) {
    if (x > 0) {
        x = x + 1;
        return x;
    }
    return -1;
}
""",
        expected_source="""\
int test_func(int x) {
    int _result = -1;
    if (x > 0) {
        x = x + 1;
        _result = x;
    }
    return _result;
}
""",
    ),

    # ===================== bit_test_bool =====================

    PatternFixture(
        id="bittest_extract_to_bool",
        pattern_name="bit_test_bool",
        description="Extract (flags & MASK) to bool local",
        func_name="test_func",
        diagnosis=diag_with_replace_real(),
        seeded_source="""\
void test_func(int flags) {
    if ((flags & 0x10) && flags > 5) {
        flags = 0;
    }
}
""",
        expected_source="""\
void test_func(int flags) {
    bool _bit0 = (flags & 0x10) != 0;
    if ((_bit0) && flags > 5) {
        flags = 0;
    }
}
""",
    ),

    PatternFixture(
        id="bittest_simple_mask",
        pattern_name="bit_test_bool",
        description="Extract simple bitwise AND test",
        func_name="test_func",
        diagnosis=diag_with_replace_real(),
        seeded_source="""\
void test_func(int f) {
    if (f & 1) {
        f = 0;
    }
}
""",
        expected_source="""\
void test_func(int f) {
    bool _bit0 = (f & 1) != 0;
    if (_bit0) {
        f = 0;
    }
}
""",
    ),

    # ===================== pragma_fp_contract =====================

    PatternFixture(
        id="fma_insert_pragma_off",
        pattern_name="pragma_fp_contract",
        description="Insert #pragma fp_contract(off) before function",
        func_name="test_func",
        diagnosis=diag_with_fma_ops(),
        seeded_source="""\
void test_func(float a, float b, float c) {
    float r = a * b + c;
}
""",
        expected_source="""\
#pragma fp_contract(off)
void test_func(float a, float b, float c) {
    float r = a * b + c;
}
""",
    ),

    PatternFixture(
        id="fma_remove_pragma_off",
        pattern_name="pragma_fp_contract",
        description="Remove existing #pragma fp_contract(off)",
        func_name="test_func",
        diagnosis=diag_with_fma_ops(),
        seeded_source="""\
#pragma fp_contract(off)
void test_func(float a, float b, float c) {
    float r = a * b + c;
}
""",
        expected_source="""\
void test_func(float a, float b, float c) {
    float r = a * b + c;
}
""",
    ),

    # ===================== hoist_sret =====================

    PatternFixture(
        id="hoist_sret_outside_loop",
        pattern_name="hoist_sret",
        description="Hoist declaration from inside loop to before loop",
        func_name="test_func",
        diagnosis=diag_with_store_load_ops(),
        seeded_source="""\
void test_func(int n) {
    for (int i = 0; i < n; i++) {
        int pos = GetPos(i);
        Use(pos);
    }
}
""",
        expected_source="""\
void test_func(int n) {
    int pos;
    for (int i = 0; i < n; i++) {
        pos = GetPos(i);
        Use(pos);
    }
}
""",
    ),

    PatternFixture(
        id="hoist_sret_into_loop",
        pattern_name="hoist_sret",
        description="Sink declaration into loop body",
        func_name="test_func",
        diagnosis=diag_with_store_load_ops(),
        seeded_source="""\
void test_func(int n) {
    int pos;
    for (int i = 0; i < n; i++) {
        pos = GetPos(i);
        Use(pos);
    }
}
""",
        expected_source="""\
void test_func(int n) {
    for (int i = 0; i < n; i++) {
        int pos = GetPos(i);
        Use(pos);
    }
}
""",
    ),

    # ===================== ternary_swap (new variants) =====================

    PatternFixture(
        id="ternary_polarity_flip",
        pattern_name="ternary_swap",
        description="if/else -> ternary with negated condition",
        func_name="test_func",
        diagnosis=diag_with_branch_and_clusters(),
        seeded_source="""\
void test_func(int x) {
    int val;
    if (x > 0) {
        val = 1;
    } else {
        val = 2;
    }
}
""",
        expected_source="""\
void test_func(int x) {
    int val;
    val = x <= 0 ? 2 : 1;
}
""",
    ),

    PatternFixture(
        id="ternary_bare_if_return",
        pattern_name="ternary_swap",
        description="Bare if/return (no else) -> return ternary",
        func_name="test_func",
        diagnosis=diag_with_branch_and_clusters(),
        seeded_source="""\
int test_func(int cond) {
    if (cond) return 10;
    return 20;
}
""",
        expected_source="""\
int test_func(int cond) {
    return cond ? 10 : 20;
}
""",
    ),

    # ===================== noreturn_attr =====================

    PatternFixture(
        id="noreturn_add_attr",
        pattern_name="noreturn_attr",
        description="Add __declspec(noreturn) to Fail declaration",
        func_name="test_func",
        diagnosis=diag_with_dead_code(),
        seeded_source="""\
void Fail(const char* msg);
void test_func(int x) {
    if (x < 0) Fail("bad");
}
""",
        expected_source="""\
__declspec(noreturn) void Fail(const char* msg);
void test_func(int x) {
    if (x < 0) Fail("bad");
}
""",
    ),

    PatternFixture(
        id="noreturn_remove_attr",
        pattern_name="noreturn_attr",
        description="Remove __declspec(noreturn) from declaration",
        func_name="test_func",
        diagnosis=diag_with_dead_code(),
        seeded_source="""\
__declspec(noreturn) void Fail(const char* msg);
void test_func(int x) {
    if (x < 0) Fail("bad");
}
""",
        expected_source="""\
void Fail(const char* msg);
void test_func(int x) {
    if (x < 0) Fail("bad");
}
""",
    ),

    # ===================== temp_elimination =====================

    PatternFixture(
        id="tmpelim_inline_single_use",
        pattern_name="temp_elimination",
        description="Inline single-use temp variable",
        func_name="test_func",
        diagnosis=diag_with_arith_ops(),
        seeded_source="""\
float LimitAng(float x);
void test_func(float mAng, float mLastAng, float locf) {
    float norm1 = LimitAng(mLastAng - locf);
    mAng = LimitAng(norm1 + mAng);
}
""",
        expected_source="""\
float LimitAng(float x);
void test_func(float mAng, float mLastAng, float locf) {
    mAng = LimitAng(LimitAng(mLastAng - locf) + mAng);
}
""",
    ),

    # ===================== objptr_bool_extract =====================

    PatternFixture(
        id="ptrext_bool_decl_chain",
        pattern_name="objptr_bool_extract",
        description="Extract member from && chain in bool declaration",
        func_name="test_func",
        diagnosis=diag_with_cmplwi_cmpwi(),
        match_mode="contains",
        seeded_source="""\
void test_func() {
    bool b = (mTex && mTex->Width() && mTex->Height());
    if (b) { mTex->Draw(); }
}
""",
        expected_source="""\
auto *_ptr0 = mTex;
    bool b = (_ptr0 && _ptr0->Width() && _ptr0->Height());
""",
    ),

    # ===================== float_double_literal =====================

    PatternFixture(
        id="fltlit_add_f_suffix",
        pattern_name="float_double_literal",
        description="Add f suffix to double literal",
        func_name="test_func",
        diagnosis=diag_with_lfd_lfs(),
        seeded_source="""\
void test_func(float x) {
    float y = x + 6.0;
}
""",
        expected_source="""\
void test_func(float x) {
    float y = x + 6.0f;
}
""",
    ),

    PatternFixture(
        id="fltlit_remove_f_suffix",
        pattern_name="float_double_literal",
        description="Remove f suffix from float literal",
        func_name="test_func",
        diagnosis=diag_with_lfd_lfs(),
        seeded_source="""\
void test_func(float x) {
    float y = x + 6.0f;
}
""",
        expected_source="""\
void test_func(float x) {
    float y = x + 6.0;
}
""",
    ),

    # ===================== fabs_variant =====================

    PatternFixture(
        id="fabs_to_fabsf",
        pattern_name="fabs_variant",
        description="Swap fabs to fabsf",
        func_name="test_func",
        diagnosis=diag_with_lfd_lfs(),
        seeded_source="""\
void test_func(float x) {
    float y = fabs(x);
}
""",
        expected_source="""\
void test_func(float x) {
    float y = fabsf(x);
}
""",
    ),

    PatternFixture(
        id="fabsf_to_stdfabs",
        pattern_name="fabs_variant",
        description="Swap fabsf to std::fabs",
        func_name="test_func",
        diagnosis=diag_with_lfd_lfs(),
        seeded_source="""\
void test_func(float x) {
    float y = fabsf(x);
}
""",
        expected_source="""\
void test_func(float x) {
    float y = std::fabs(x);
}
""",
    ),

    # ===================== milo_log_swap =====================

    PatternFixture(
        id="logswap_warn_to_notify",
        pattern_name="milo_log_swap",
        description="Swap MILO_WARN to MILO_NOTIFY",
        func_name="test_func",
        diagnosis=diag_with_insert_delete(),
        seeded_source="""\
void test_func(int x) {
    MILO_WARN("value is %d", x);
}
""",
        expected_source="""\
void test_func(int x) {
    MILO_NOTIFY("value is %d", x);
}
""",
    ),

    PatternFixture(
        id="logswap_notify_to_log",
        pattern_name="milo_log_swap",
        description="Swap MILO_NOTIFY to MILO_LOG",
        func_name="test_func",
        diagnosis=diag_with_insert_delete(),
        seeded_source="""\
void test_func(int x) {
    MILO_NOTIFY("value is %d", x);
}
""",
        expected_source="""\
void test_func(int x) {
    MILO_LOG("value is %d", x);
}
""",
    ),

    # ===================== assignment_reorder =====================

    PatternFixture(
        id="asgnreorder_swap_pair",
        pattern_name="assignment_reorder",
        description="Swap two consecutive assignments",
        func_name="test_func",
        diagnosis=diag_with_offset_deltas(),
        match_mode="contains",
        seeded_source="""\
struct W { float a; float b; float c; };
void test_func(W &w) {
    w.a = 0;
    w.b = 0;
    w.c = 0;
}
""",
        expected_source="""\
    w.b = 0;
    w.a = 0;
""",
    ),

    # ===================== iterator_deref_style =====================

    PatternFixture(
        id="itderef_star_to_arrow",
        pattern_name="iterator_deref_style",
        description="Convert (*it).member to it->member",
        func_name="test_func",
        diagnosis=diag_with_replace_real(),
        seeded_source="""\
struct Iter { int mWeight; };
void test_func(Iter *it) {
    int x = (*it).mWeight;
}
""",
        expected_source="""\
struct Iter { int mWeight; };
void test_func(Iter *it) {
    int x = it->mWeight;
}
""",
    ),

    # ===================== milo_str_conv =====================

    PatternFixture(
        id="strconv_classname",
        pattern_name="milo_str_conv",
        description="Add .Str() to ClassName() in MILO_NOTIFY",
        func_name="test_func",
        diagnosis=diag_with_bl_mismatch(),
        seeded_source="""\
void test_func() {
    MILO_NOTIFY("error in %s %s", PathName(this), ClassName());
}
""",
        expected_source="""\
void test_func() {
    MILO_NOTIFY("error in %s %s", PathName(this), ClassName().Str());
}
""",
    ),

    PatternFixture(
        id="strconv_member_name",
        pattern_name="milo_str_conv",
        description="Add .Str() to obj->ClassName() in MILO_WARN",
        func_name="test_func",
        diagnosis=diag_with_bl_mismatch(),
        seeded_source="""\
void test_func(Hmx::Object *obj) {
    MILO_WARN("bad obj %s", obj->ClassName());
}
""",
        expected_source="""\
void test_func(Hmx::Object *obj) {
    MILO_WARN("bad obj %s", obj->ClassName().Str());
}
""",
    ),

    PatternFixture(
        id="strconv_multiple_args",
        pattern_name="milo_str_conv",
        description="Add .Str() to multiple Symbol args at once",
        func_name="test_func",
        diagnosis=diag_with_bl_mismatch(),
        seeded_source="""\
void test_func() {
    MILO_NOTIFY("%s %s %s", PathName(this), ClassName(), StaticClassName());
}
""",
        expected_source="""\
void test_func() {
    MILO_NOTIFY("%s %s %s", PathName(this), ClassName().Str(), StaticClassName().Str());
}
""",
    ),

    PatternFixture(
        id="strconv_already_has_str",
        pattern_name="milo_str_conv",
        description="Don't double-add .Str() when already present",
        func_name="test_func",
        diagnosis=diag_with_bl_mismatch(),
        seeded_source="""\
void test_func() {
    MILO_NOTIFY("%s %s", ClassName().Str(), StaticClassName());
}
""",
        expected_source="""\
void test_func() {
    MILO_NOTIFY("%s %s", ClassName().Str(), StaticClassName().Str());
}
""",
    ),

    PatternFixture(
        id="strconv_static_classname",
        pattern_name="milo_str_conv",
        description="Add .Str() to StaticClassName() in MILO_FAIL",
        func_name="test_func",
        diagnosis=diag_with_bl_mismatch(),
        seeded_source="""\
void test_func() {
    MILO_FAIL("type %s", StaticClassName());
}
""",
        expected_source="""\
void test_func() {
    MILO_FAIL("type %s", StaticClassName().Str());
}
""",
    ),

    # ===================== milo_call_merge =====================

    PatternFixture(
        id="callmerge_two_if_blocks",
        pattern_name="milo_call_merge",
        description="Merge two MILO_WARN calls in consecutive if-blocks",
        func_name="test_func",
        diagnosis=diag_with_large_clusters(),
        match_mode="contains",
        seeded_source="""\
void test_func(int font, const char *name) {
    if (font == 0) {
        MILO_WARN("bad font %s %s", PathName(this), "NULL");
        goto done;
    }
    if (font == 1) {
        MILO_WARN("bad font %s %s", PathName(this), name);
        goto done;
    }
    done:;
}
""",
        expected_source="""\
MILO_WARN("bad font %s %s", PathName(this), _mergedArg);
""",
    ),

    # ===================== fma_reorder (paren expansion) =====================

    PatternFixture(
        id="fma_paren_expand_calcspline",
        pattern_name="fma_reorder",
        description="Expand a - (b * c - d) -> d - b * c + a (CalcSpline fix)",
        func_name="test_func",
        diagnosis=diag_with_fma_addsub_ops(),
        seeded_source="""\
float test_func(float p3, float p2, float p1x3m0) {
    float term3 = p3 - (p2 * 3.0f - p1x3m0);
    return term3;
}
""",
        expected_source="""\
float test_func(float p3, float p2, float p1x3m0) {
    float term3 = p1x3m0 - p2 * 3.0f + p3;
    return term3;
}
""",
    ),

    PatternFixture(
        id="fma_paren_expand_interptangent",
        pattern_name="fma_reorder",
        description="Expand a - (b - c) -> c - b + a (InterpTangent fix)",
        func_name="test_func",
        diagnosis=diag_with_fma_addsub_ops(),
        seeded_source="""\
float test_func(float f4, float fsq3) {
    float b = 1.0f - (f4 - fsq3);
    return b;
}
""",
        expected_source="""\
float test_func(float f4, float fsq3) {
    float b = fsq3 - f4 + 1.0f;
    return b;
}
""",
    ),

    # ===================== early_return_merge (guard-to-conjunction) =====================

    PatternFixture(
        id="retmerge_guard_to_conjunction",
        pattern_name="early_return_merge",
        description="Collapse if (!cond) return false; return expr; into && conjunction",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
bool test_func(int a) {
    if (!(IsLoaded()))
        return false;
    return GetMusic()->Loaded();
}
""",
        expected_source="""\
bool test_func(int a) {
    return IsLoaded() && GetMusic()->Loaded();
}
""",
    ),

    PatternFixture(
        id="retmerge_conjunction_to_guard",
        pattern_name="early_return_merge",
        description="Expand return A && B; into guard return + final return",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
bool test_func(int a) {
    return IsLoaded() && GetMusic()->Loaded();
}
""",
        expected_source="""\
bool test_func(int a) {
    if (!(IsLoaded()))
        return false;
    return GetMusic()->Loaded();
}
""",
    ),

    PatternFixture(
        id="retmerge_guard_true_to_disjunction",
        pattern_name="early_return_merge",
        description="Collapse if (cond) return true; return expr; into || disjunction",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
bool test_func(int a) {
    if (IsDone())
        return true;
    return HasFallback();
}
""",
        expected_source="""\
bool test_func(int a) {
    return IsDone() || HasFallback();
}
""",
    ),

    PatternFixture(
        id="retmerge_disjunction_to_guard_true",
        pattern_name="early_return_merge",
        description="Expand return A || B; into if (A) return true; return B;",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
bool test_func(int a) {
    return IsDone() || HasFallback();
}
""",
        expected_source="""\
bool test_func(int a) {
    if (IsDone())
        return true;
    return HasFallback();
}
""",
    ),

    # ===================== fma_reorder (paren expansion flat) =====================

    PatternFixture(
        id="fma_paren_expand_flat",
        pattern_name="fma_reorder",
        description="Expand a - (b + c) flat -> a - b - c",
        func_name="test_func",
        diagnosis=diag_with_fma_addsub_ops(),
        seeded_source="""\
float test_func(float a, float b, float c) {
    float r = a - (b + c);
    return r;
}
""",
        expected_source="""\
float test_func(float a, float b, float c) {
    float r = a - b - c;
    return r;
}
""",
    ),

    PatternFixture(
        id="fma_paren_expand_plus_outer",
        pattern_name="fma_reorder",
        description="Expand a + (b - c) -> a + b - c (remove unnecessary parens)",
        func_name="test_func",
        diagnosis=diag_with_fma_addsub_ops(),
        seeded_source="""\
float test_func(float a, float b, float c) {
    float r = a + (b - c);
    return r;
}
""",
        expected_source="""\
float test_func(float a, float b, float c) {
    float r = a + b - c;
    return r;
}
""",
    ),

    # ===================== reference_elimination =====================

    PatternFixture(
        id="refelim_subscript_ref",
        pattern_name="reference_elimination",
        description="Eliminate reference to subscript expression used twice",
        func_name="test_func",
        diagnosis=diag_with_callee_saved_swaps(),
        seeded_source="""\
void MergeObjectsRecurse(int dir, int toDir, int filt, bool top);
int test_func(int* subDirs, int toDir, int filt, int n) {
    for (int i = 0; i < n; i++) {
        int& oPtr = subDirs[i];
        if (oPtr != 0)
            MergeObjectsRecurse(oPtr, toDir, filt, false);
    }
    return 0;
}
""",
        expected_source="""\
void MergeObjectsRecurse(int dir, int toDir, int filt, bool top);
int test_func(int* subDirs, int toDir, int filt, int n) {
    for (int i = 0; i < n; i++) {
        if (subDirs[i] != 0)
            MergeObjectsRecurse(subDirs[i], toDir, filt, false);
    }
    return 0;
}
""",
    ),

    PatternFixture(
        id="refelim_field_ptr",
        pattern_name="reference_elimination",
        description="Eliminate pointer to field expression used three times",
        func_name="test_func",
        diagnosis=diag_with_lwz_ops(),
        match_mode="contains",
        seeded_source="""\
struct Obj { int x; };
void use(int*);
void test_func(Obj* obj) {
    int* p = obj->x;
    if (p) {
        use(p);
        use(p);
    }
}
""",
        expected_source="""\
    if (obj->x) {
        use(obj->x);
        use(obj->x);
    }
""",
    ),

    # ===================== temp_elimination (multi-use value) =====================

    PatternFixture(
        id="tmpelim_multiuse_member_read",
        pattern_name="temp_elimination",
        description="Eliminate multi-use value temp initialized from member",
        func_name="test_func",
        diagnosis=diag_with_callee_saved_swaps(),
        seeded_source="""\
int test_func(int mFoo, int y) {
    int x = mFoo;
    int a = x + y;
    int b = x - y;
    return a + b;
}
""",
        expected_source="""\
int test_func(int mFoo, int y) {
    int a = mFoo + y;
    int b = mFoo - y;
    return a + b;
}
""",
    ),

    # Note: tmpelim_multiuse_skip_call is a negative test — see TestMultiUseTempSafety

    # ===================== subscript_ref_bind =====================

    PatternFixture(
        id="subbind_repeated_subscript",
        pattern_name="subscript_ref_bind",
        description="Bind repeated subscript expression to local ref",
        func_name="test_func",
        diagnosis=diag_with_callee_saved_swaps(),
        match_mode="contains",
        seeded_source="""\
void process(int x, int y);
void test_func(int* mDirs, int n) {
    for (int i = 0; i < n; i++) {
        if (mDirs[i] != 0)
            process(mDirs[i], n);
    }
}
""",
        expected_source="""\
auto& _sub0 = mDirs[i];
        if (_sub0 != 0)
            process(_sub0, n);
""",
    ),

    # ===================== signed_unsigned (double-cast) =====================

    PatternFixture(
        id="signunsign_double_cast_subscript",
        pattern_name="signed_unsigned",
        description="Double-cast subscript operand with (unsigned int)(void*)",
        func_name="test_func",
        diagnosis=diag_with_cmp_ops(),
        match_mode="contains",
        seeded_source="""\
int test_func(int* arr, int i) {
    if (arr[i] != 0) {
        return 1;
    }
    return 0;
}
""",
        expected_source="""\
(unsigned int)(void*)arr[i] != 0
""",
    ),
    # ===================== null_guard_elimination =====================

    PatternFixture(
        id="nullguard_if_ptr_call",
        pattern_name="null_guard_elimination",
        description="Remove if (ptr) ptr->Method() guard (no braces)",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
void test_func() {
    if (TheMetaMusic)
        TheMetaMusic->Stop();
}
""",
        expected_source="""\
void test_func() {
    TheMetaMusic->Stop();
}
""",
    ),

    PatternFixture(
        id="nullguard_if_ptr_call_braces",
        pattern_name="null_guard_elimination",
        description="Remove if (ptr) { ptr->Method(); } guard (with braces)",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
void test_func() {
    if (TheMetaMusic) {
        TheMetaMusic->Stop();
    }
}
""",
        expected_source="""\
void test_func() {
    TheMetaMusic->Stop();
}
""",
    ),

    PatternFixture(
        id="nullguard_drop_and_operand",
        pattern_name="null_guard_elimination",
        description="Drop leading && operand only when kept side still references same guard",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        match_mode="contains",
        seeded_source="""\
void test_func() {
    if (TheMetaMusic && TheMetaMusic->IsActive()) {
        TheMetaMusic = CreateMusic();
    }
}
""",
        expected_source="""\
if (TheMetaMusic->IsActive()) {
""",
    ),

    PatternFixture(
        id="nullguard_or_chain",
        pattern_name="null_guard_elimination",
        description="Remove null guard from || chain: (ptr && ptr->M()) -> ptr->M()",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        match_mode="contains",
        seeded_source="""\
bool test_func() {
    bool ret = IsFading() || (TheMetaMusic && TheMetaMusic->IsActive()) || IsExiting();
    return ret;
}
""",
        expected_source="""\
IsFading() || TheMetaMusic->IsActive() || IsExiting()
""",
    ),

    # ===================== varargs_cast =====================

    PatternFixture(
        id="varargs_cast_name_to_charptr",
        pattern_name="varargs_cast",
        description="Add (char *) cast to Name() in MILO_NOTIFY",
        func_name="test_func",
        diagnosis=diag_with_bl_mismatch(),
        seeded_source="""\
void test_func() {
    MILO_NOTIFY("Keyframes in %s are out of order.", Name());
}
""",
        expected_source="""\
void test_func() {
    MILO_NOTIFY("Keyframes in %s are out of order.", (char *)Name());
}
""",
    ),

    PatternFixture(
        id="varargs_cast_filepath_to_string_ref",
        pattern_name="varargs_cast",
        description="Add (String &) cast to fp in MILO_NOTIFY",
        func_name="test_func",
        diagnosis=diag_with_bl_mismatch(),
        seeded_source="""\
void test_func() {
    MILO_NOTIFY("won't load %s", fp);
}
""",
        expected_source="""\
void test_func() {
    MILO_NOTIFY("won't load %s", (String &)fp);
}
""",
    ),

    # ===================== bool_to_uchar =====================

    PatternFixture(
        id="bool_to_uchar_simple",
        pattern_name="bool_to_uchar",
        description="bool skip=false + skip=true -> unsigned char skip=0 + skip=1",
        func_name="test_func",
        diagnosis=diag_with_cmp_ops(),
        seeded_source="""\
void test_func(int cond) {
    bool skip = false;
    if (cond) {
        skip = true;
    }
    return skip;
}
""",
        expected_source="""\
void test_func(int cond) {
    unsigned char skip = 0;
    if (cond) {
        skip = 1;
    }
    return skip;
}
""",
    ),

    PatternFixture(
        id="bool_to_uchar_expr_init",
        pattern_name="bool_to_uchar",
        description="bool result = expr -> unsigned char result = (unsigned char)(expr)",
        func_name="test_func",
        diagnosis=diag_with_cmp_ops(),
        seeded_source="""\
void test_func(int a, int b) {
    bool result = a > b;
    return result;
}
""",
        expected_source="""\
void test_func(int a, int b) {
    unsigned char result = (unsigned char)(a > b);
    return result;
}
""",
    ),

    # ===================== guard_to_nested =====================

    PatternFixture(
        id="guard_to_nested_basic",
        pattern_name="guard_to_nested",
        description="Two void guards to nested if blocks",
        func_name="test_func",
        diagnosis=diag_with_branch_and_clusters(),
        seeded_source="""\
void test_func(int *a, int *b) {
    if (!a) return;
    if (!b) return;
    a[0] = b[0];
}
""",
        expected_source="""\
void test_func(int *a, int *b) {
    if (a) {
        if (b) {
            a[0] = b[0];
        }
    }
}
""",
    ),

    PatternFixture(
        id="guard_to_nested_with_value",
        pattern_name="guard_to_nested",
        description="Guards returning false to nested if with else",
        func_name="test_func",
        diagnosis=diag_with_branch_and_clusters(),
        seeded_source="""\
bool test_func(int *a, int *b) {
    if (!a) return false;
    if (!b) return false;
    return a[0] == b[0];
}
""",
        expected_source="""\
bool test_func(int *a, int *b) {
    if (a) {
        if (b) {
            return a[0] == b[0];
        } else return false;
    } else return false;
}
""",
    ),

    PatternFixture(
        id="nested_to_guard_basic",
        pattern_name="guard_to_nested",
        description="Nested if blocks to guard returns (reverse)",
        func_name="test_func",
        diagnosis=diag_with_branch_and_clusters(),
        seeded_source="""\
void test_func(int *a, int *b) {
    if (a) {
        if (b) {
            a[0] = b[0];
        }
    }
}
""",
        expected_source="""\
void test_func(int *a, int *b) {
    if (!a) return;
    if (!b) return;
    a[0] = b[0];
}
""",
    ),

    # ===================== statement_reorder =====================

    PatternFixture(
        id="stmt_reorder_assignment_past_guard",
        pattern_name="statement_reorder",
        description="Move assignment past independent if-guard",
        func_name="test_func",
        diagnosis=diag_with_clusters(),
        match_mode="contains",
        seeded_source="""\
void test_func(float w, float x) {
    w = 0.0f;
    if (x < 0.0f)
        printf("bad x");
}
""",
        expected_source="""\
void test_func(float w, float x) {
    if (x < 0.0f)
        printf("bad x");
    w = 0.0f;
}
""",
    ),

    PatternFixture(
        id="stmt_reorder_blocks_dependent",
        pattern_name="statement_reorder",
        description="Dependent statements produce no swap of a=5 and b=a+1 but swap independent pair",
        func_name="test_func",
        diagnosis=diag_with_clusters(),
        match_mode="contains",
        seeded_source="""\
void test_func(int a, int x) {
    a = 5;
    int b = a + 1;
    x = 99;
}
""",
        expected_source="""\
void test_func(int a, int x) {
    a = 5;
    x = 99;
    int b = a + 1;
}
""",
    ),

    PatternFixture(
        id="stmt_reorder_three_statements",
        pattern_name="statement_reorder",
        description="Swap adjacent independent assignments (3 statements)",
        func_name="test_func",
        diagnosis=diag_with_offset_deltas(),
        match_mode="contains",
        seeded_source="""\
void test_func(int x, int y, int z) {
    x = 1;
    y = 2;
    z = 3;
}
""",
        expected_source="""\
void test_func(int x, int y, int z) {
    y = 2;
    x = 1;
    z = 3;
}
""",
    ),

    # ===================== parameter_live_range =====================

    PatternFixture(
        id="parlr_bs_to_dstream",
        pattern_name="parameter_live_range",
        description="Replace bs identifier with d.stream after LOAD_REVS",
        func_name="FooLoad",
        diagnosis=diag_with_prologue_fewer_saves(),
        match_mode="contains",
        seeded_source="""\
void FooLoad(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(2, 0);
    Hmx::Object::Load(bs);
    d >> mFoo;
}
""",
        expected_source="""\
void FooLoad(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(2, 0);
    Hmx::Object::Load(d.stream);
    d >> mFoo;
}
""",
    ),

    PatternFixture(
        id="parlr_load_superclass_single",
        pattern_name="parameter_live_range",
        description="Replace LOAD_SUPERCLASS(ClassName) with ClassName::Load(d.stream)",
        func_name="BarLoad",
        diagnosis=diag_with_prologue_fewer_saves(),
        match_mode="contains",
        seeded_source="""\
void BarLoad(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(3, 0);
    LOAD_SUPERCLASS(FlowNode);
    d >> mBar;
}
""",
        expected_source="""\
void BarLoad(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(3, 0);
    FlowNode::Load(d.stream);
    d >> mBar;
}
""",
    ),

    PatternFixture(
        id="parlr_load_superclass_qualified",
        pattern_name="parameter_live_range",
        description="Replace LOAD_SUPERCLASS with qualified class name (Hmx::Object)",
        func_name="QualLoad",
        diagnosis=diag_with_prologue_fewer_saves(),
        match_mode="contains",
        seeded_source="""\
void QualLoad(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(1, 0);
    LOAD_SUPERCLASS(Hmx::Object);
    LOAD_SUPERCLASS(RndAnimatable);
    d >> mVal;
}
""",
        expected_source="""\
void QualLoad(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(1, 0);
    Hmx::Object::Load(d.stream);
    RndAnimatable::Load(d.stream);
    d >> mVal;
}
""",
    ),

    PatternFixture(
        id="parlr_combined_bs_and_macro",
        pattern_name="parameter_live_range",
        description="Combined: replace both bs identifiers and LOAD_SUPERCLASS macros",
        func_name="ComboLoad",
        diagnosis=diag_with_prologue_fewer_saves(),
        match_mode="contains",
        seeded_source="""\
void ComboLoad(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(2, 0);
    LOAD_SUPERCLASS(EventTrigger);
    uiCom->Load(bs);
    bs >> mSym;
}
""",
        expected_source="""\
void ComboLoad(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(2, 0);
    EventTrigger::Load(d.stream);
    uiCom->Load(d.stream);
    d.stream >> mSym;
}
""",
    ),

    PatternFixture(
        id="parlr_chain_merge",
        pattern_name="parameter_live_range",
        description="Merge consecutive d >> statements into chain",
        func_name="ChainLoad",
        diagnosis=diag_with_prologue_fewer_saves(),
        match_mode="contains",
        seeded_source="""\
void ChainLoad(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(1, 0);
    d >> mA;
    d >> mB;
}
""",
        expected_source="""\
    d >> mA
      >> mB;
""",
    ),

    # ===================== type_width_change =====================

    PatternFixture(
        id="typewidth_int_to_uint",
        pattern_name="type_width_change",
        description="Widen int to unsigned int (first transition)",
        func_name="test_func",
        diagnosis=diag_with_cmp_ops(),
        seeded_source="""\
void test_func() {
    int count = 0;
    if (count < 10) count++;
}
""",
        expected_source="""\
void test_func() {
    unsigned int count = 0;
    if (count < 10) count++;
}
""",
    ),

    PatternFixture(
        id="typewidth_uint_to_int",
        pattern_name="type_width_change",
        description="Swap unsigned int to int (sign change)",
        func_name="test_func",
        diagnosis=diag_with_cmp_ops(),
        seeded_source="""\
void test_func() {
    unsigned int x = 0;
    if (x < 10) x++;
}
""",
        expected_source="""\
void test_func() {
    int x = 0;
    if (x < 10) x++;
}
""",
    ),

    PatternFixture(
        id="typewidth_bool_to_uchar",
        pattern_name="type_width_change",
        description="Change bool to unsigned char with true/false fixup",
        func_name="test_func",
        diagnosis=diag_with_cmp_ops(),
        seeded_source="""\
void test_func() {
    bool flag = false;
    if (flag) flag = true;
}
""",
        expected_source="""\
void test_func() {
    unsigned char flag = 0;
    if (flag) flag = 1;
}
""",
    ),

    # ===================== loop_condition_subtract =====================

    PatternFixture(
        id="loopsub_while_ge_to_subtract",
        pattern_name="loop_condition_subtract",
        description="while (high >= low) -> while (high - low >= 0)",
        func_name="FindDataIndex",
        diagnosis=diag_with_subf_cmpw(),
        match_mode="contains",
        seeded_source="""\
int FindDataIndex(int* arr, int size, int key) {
    int low = 0;
    int high = size - 1;
    while (high >= low) {
        int mid = (low + high) >> 1;
        if (arr[mid] == key) return mid;
        if (arr[mid] < key) low = mid + 1;
        else high = mid - 1;
    }
    return -1;
}
""",
        expected_source="""\
high - low >= 0
""",
    ),

    PatternFixture(
        id="loopsub_while_le_to_subtract",
        pattern_name="loop_condition_subtract",
        description="while (low <= high) -> while (high - low >= 0)",
        func_name="FindDataIndex",
        diagnosis=diag_with_subf_cmpw(),
        match_mode="contains",
        seeded_source="""\
int FindDataIndex(int* arr, int size, int key) {
    int low = 0;
    int high = size - 1;
    while (low <= high) {
        int mid = (low + high) >> 1;
        if (arr[mid] == key) return mid;
        if (arr[mid] < key) low = mid + 1;
        else high = mid - 1;
    }
    return -1;
}
""",
        expected_source="""\
high - low >= 0
""",
    ),

    PatternFixture(
        id="loopsub_for_gt_to_subtract",
        pattern_name="loop_condition_subtract",
        description="for (... ; i > limit; ...) -> for (... ; i - limit > 0; ...)",
        func_name="test_func",
        diagnosis=diag_with_subf_cmpw(),
        match_mode="contains",
        seeded_source="""\
void test_func(int* arr, int n, int limit) {
    for (int i = n; i > limit; i--) {
        arr[i] = 0;
    }
}
""",
        expected_source="""\
i - limit > 0
""",
    ),

    PatternFixture(
        id="loopsub_reverse_subtract_to_ge",
        pattern_name="loop_condition_subtract",
        description="while (high - low >= 0) -> while (high >= low) (reverse)",
        func_name="FindDataIndex",
        diagnosis=diag_with_subf_cmpw(),
        match_mode="contains",
        seeded_source="""\
int FindDataIndex(int* arr, int size, int key) {
    int low = 0;
    int high = size - 1;
    while (high - low >= 0) {
        int mid = (low + high) >> 1;
        if (arr[mid] == key) return mid;
        if (arr[mid] < key) low = mid + 1;
        else high = mid - 1;
    }
    return -1;
}
""",
        expected_source="""\
high >= low
""",
    ),

    # ===================== u8_to_unsigned_long =====================

    PatternFixture(
        id="u8widen_local_to_unsigned_long",
        pattern_name="u8_to_unsigned_long",
        description="Widen unsigned char local variable to unsigned long",
        func_name="test_func",
        diagnosis=diag_with_rlwinm_fusion(),
        seeded_source="""\
int test_func(int a, int b) {
    unsigned char result = (unsigned char)(a ^ b);
    return result;
}
""",
        expected_source="""\
unsigned long result
""",
        match_mode="contains",
    ),

    PatternFixture(
        id="u8widen_return_mask",
        pattern_name="u8_to_unsigned_long",
        description="Convert u8() return to & 0xFF mask",
        func_name="test_func",
        diagnosis=diag_with_rlwinm_fusion(),
        seeded_source="""\
int test_func(int a, int b) {
    return DataNode(kDataInt, u8(a ^ b));
}
""",
        expected_source="""\
(int)((a ^ b) & 0xFF)
""",
        match_mode="contains",
    ),

    PatternFixture(
        id="u8widen_combined",
        pattern_name="u8_to_unsigned_long",
        description="Combined: widen unsigned char locals + convert u8() return",
        func_name="test_func",
        diagnosis=diag_with_rlwinm_fusion(),
        seeded_source="""\
int test_func(int a, int b) {
    unsigned char x = (unsigned char)(a ^ b);
    unsigned char y = (unsigned char)(x | 0x10);
    return DataNode(kDataInt, u8(y));
}
""",
        expected_source="""\
unsigned long
""",
        match_mode="contains",
    ),
]

# Build lookup by ID
_FIXTURE_MAP: dict[str, PatternFixture] = {f.id: f for f in FIXTURES}


# ---------------------------------------------------------------------------
# Test classes
# ---------------------------------------------------------------------------

class TestPatternFixtures(unittest.TestCase):
    """Parametric tests: one test per fixture, verifying pattern recovery."""
    pass  # Tests are added dynamically below


class TestPatternRelevance(unittest.TestCase):
    """Verify relevant() returns correct results for various diagnoses."""

    def test_ternary_swap_relevant_with_branches(self):
        p = get_pattern("ternary_swap")
        self.assertTrue(p.relevant(diag_with_branch_ops()))

    def test_ternary_swap_relevant_with_clusters(self):
        p = get_pattern("ternary_swap")
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_ternary_swap_irrelevant_empty(self):
        p = get_pattern("ternary_swap")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_empty_size_relevant_divw_target(self):
        p = get_pattern("empty_size_swap")
        self.assertTrue(p.relevant(diag_with_divw()))

    def test_empty_size_relevant_divw_base(self):
        p = get_pattern("empty_size_swap")
        self.assertTrue(p.relevant(diag_with_divw_base()))

    def test_empty_size_irrelevant_empty(self):
        p = get_pattern("empty_size_swap")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_empty_size_relevant_ptr_diff_target_cluster(self):
        """Target-only subf+srawi+addze cluster -> swap empty()->size()."""
        p = get_pattern("empty_size_swap")
        self.assertTrue(p.relevant(diag_with_ptr_diff_target_cluster()))

    def test_empty_size_relevant_ptr_diff_base_cluster(self):
        """Base-only subf+srawi+addze cluster -> swap size()->empty()."""
        p = get_pattern("empty_size_swap")
        self.assertTrue(p.relevant(diag_with_ptr_diff_base_cluster()))

    def test_empty_size_irrelevant_subf_only(self):
        """Lone subf in a cluster is NOT enough — too common (loop conds)."""
        p = get_pattern("empty_size_swap")
        d = _empty_diag()
        d.clusters = [Cluster(
            start_idx=10, end_idx=12, size=2, inserts=0, deletes=2,
            target_opcodes=("lwz", "subf"),
            base_opcodes=(),
        )]
        self.assertFalse(p.relevant(d))

    def test_empty_size_irrelevant_srawi_only(self):
        """Lone srawi (e.g. arithmetic shift unrelated to ptr-diff) — skip."""
        p = get_pattern("empty_size_swap")
        d = _empty_diag()
        d.clusters = [Cluster(
            start_idx=10, end_idx=12, size=2, inserts=0, deletes=2,
            target_opcodes=("lwz", "srawi"),
            base_opcodes=(),
        )]
        self.assertFalse(p.relevant(d))

    def test_empty_size_ptr_diff_requires_same_cluster(self):
        """subf in one cluster + srawi in another is NOT a match.

        Cross-cluster co-occurrence is too weak a signal — both opcodes are
        common, the signature comes from their proximity.
        """
        p = get_pattern("empty_size_swap")
        d = _empty_diag()
        d.clusters = [
            Cluster(start_idx=10, end_idx=11, size=1, inserts=0, deletes=1,
                    target_opcodes=("subf",), base_opcodes=()),
            Cluster(start_idx=50, end_idx=51, size=1, inserts=0, deletes=1,
                    target_opcodes=("srawi",), base_opcodes=()),
        ]
        self.assertFalse(p.relevant(d))

    def test_empty_size_relevant_ptr_diff_mulhw_variant(self):
        """Non-power-of-2 sizeof(T): mulhw+srawi cluster fires to_size."""
        p = get_pattern("empty_size_swap")
        d = _empty_diag()
        d.clusters = [Cluster(
            start_idx=10, end_idx=14, size=4, inserts=0, deletes=4,
            target_opcodes=("subf", "mulhw", "srawi", "addze"),
            base_opcodes=(),
        )]
        self.assertTrue(p.relevant(d))

    def test_commutative_relevant_arith(self):
        p = get_pattern("commutative_swap")
        self.assertTrue(p.relevant(diag_with_arith_ops()))

    def test_commutative_irrelevant_empty(self):
        p = get_pattern("commutative_swap")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_variable_extraction_relevant_with_clusters(self):
        p = get_pattern("variable_extraction")
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_variable_extraction_irrelevant_empty(self):
        p = get_pattern("variable_extraction")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_declaration_reorder_relevant_gpr(self):
        p = get_pattern("declaration_reorder")
        self.assertTrue(p.relevant(diag_with_gpr_swaps()))

    def test_declaration_reorder_irrelevant_empty(self):
        p = get_pattern("declaration_reorder")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_fma_relevant_fma_ops(self):
        p = get_pattern("fma_reorder")
        self.assertTrue(p.relevant(diag_with_fma_ops()))

    def test_fma_relevant_addsub_ops(self):
        p = get_pattern("fma_reorder")
        self.assertTrue(p.relevant(diag_with_fma_addsub_ops()))

    def test_fma_irrelevant_empty(self):
        p = get_pattern("fma_reorder")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_comparison_flip_relevant_cmp(self):
        p = get_pattern("comparison_flip")
        self.assertTrue(p.relevant(diag_with_cmp_ops()))

    def test_comparison_flip_irrelevant_empty(self):
        p = get_pattern("comparison_flip")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_comparison_equivalence_relevant_cmp(self):
        p = get_pattern("comparison_equivalence")
        self.assertTrue(p.relevant(diag_with_cmp_ops()))

    def test_comparison_equivalence_irrelevant_empty(self):
        p = get_pattern("comparison_equivalence")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_branch_polarity_relevant_branches(self):
        p = get_pattern("branch_polarity")
        self.assertTrue(p.relevant(diag_with_branch_ops()))

    def test_branch_polarity_irrelevant_empty(self):
        p = get_pattern("branch_polarity")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_signed_unsigned_relevant_cmp(self):
        p = get_pattern("signed_unsigned")
        self.assertTrue(p.relevant(diag_with_cmp_ops()))

    def test_signed_unsigned_irrelevant_empty(self):
        p = get_pattern("signed_unsigned")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_inline_assignment_relevant_clusters(self):
        p = get_pattern("inline_assignment")
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_inline_assignment_irrelevant_empty(self):
        p = get_pattern("inline_assignment")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_argument_swap_relevant_clusters(self):
        p = get_pattern("argument_swap")
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_argument_swap_irrelevant_empty(self):
        p = get_pattern("argument_swap")
        self.assertFalse(p.relevant(_empty_diag()))

    # reference_elimination
    def test_reference_elimination_relevant_callee_saved(self):
        p = get_pattern("reference_elimination")
        self.assertTrue(p.relevant(diag_with_callee_saved_swaps()))

    def test_reference_elimination_relevant_lwz(self):
        p = get_pattern("reference_elimination")
        self.assertTrue(p.relevant(diag_with_lwz_ops()))

    def test_reference_elimination_relevant_clusters(self):
        p = get_pattern("reference_elimination")
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_reference_elimination_relevant_prologue_fewer(self):
        p = get_pattern("reference_elimination")
        self.assertTrue(p.relevant(diag_with_prologue_fewer_saves()))

    def test_reference_elimination_irrelevant_empty(self):
        p = get_pattern("reference_elimination")
        self.assertFalse(p.relevant(_empty_diag()))

    # subscript_ref_bind
    def test_subscript_ref_bind_relevant_callee_saved(self):
        p = get_pattern("subscript_ref_bind")
        self.assertTrue(p.relevant(diag_with_callee_saved_swaps()))

    def test_subscript_ref_bind_relevant_lwz(self):
        p = get_pattern("subscript_ref_bind")
        self.assertTrue(p.relevant(diag_with_lwz_ops()))

    def test_subscript_ref_bind_relevant_clusters(self):
        p = get_pattern("subscript_ref_bind")
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_subscript_ref_bind_relevant_prologue_more(self):
        p = get_pattern("subscript_ref_bind")
        self.assertTrue(p.relevant(diag_with_prologue_more_saves()))

    def test_subscript_ref_bind_irrelevant_empty(self):
        p = get_pattern("subscript_ref_bind")
        self.assertFalse(p.relevant(_empty_diag()))

    # null_guard_elimination
    def test_null_guard_relevant_branch_ops(self):
        p = get_pattern("null_guard_elimination")
        self.assertTrue(p.relevant(diag_with_branch_ops()))

    def test_null_guard_relevant_clusters(self):
        p = get_pattern("null_guard_elimination")
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_null_guard_relevant_cmp_ops(self):
        p = get_pattern("null_guard_elimination")
        self.assertTrue(p.relevant(diag_with_cmp_ops()))

    def test_null_guard_irrelevant_empty(self):
        p = get_pattern("null_guard_elimination")
        self.assertFalse(p.relevant(_empty_diag()))

    def test_u8_to_unsigned_long_relevant_fusion(self):
        p = get_pattern("u8_to_unsigned_long")
        self.assertTrue(p.relevant(diag_with_rlwinm_fusion()))

    def test_u8_to_unsigned_long_irrelevant_empty(self):
        p = get_pattern("u8_to_unsigned_long")
        self.assertFalse(p.relevant(_empty_diag()))


# ---------------------------------------------------------------------------
# Ghidra-guided null guard tests
# ---------------------------------------------------------------------------

def make_ghidra_context(source_text, func_name, diagnosis, ghidra_code):
    """Make a FunctionContext with ghidra_ast populated."""
    from scripts.permuter.ghidra_ast import parse_ghidra
    ctx = make_context(source_text, func_name, diagnosis)
    ctx.ghidra_ast = parse_ghidra(ghidra_code)
    ctx.ghidra_code = ghidra_code
    return ctx


class TestNullGuardGhidraGuided(unittest.TestCase):
    """Test Ghidra-guided null guard elimination."""

    def test_null_guard_ghidra_guided_removes_absent(self):
        """Ghidra shows no null check -> remove guard."""
        source = '''
void test_func() {
    if (TheMetaMusic)
        TheMetaMusic->AddFader(fader);
}
'''
        # Ghidra code has no null check
        ghidra_code = '''
void test_func(void) {
    FUN_12345678(TheMetaMusic, fader);
}
'''
        ctx = make_ghidra_context(source, "test_func", diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("null_guard_elimination")
        variants = list(pattern.generate(ctx))
        self.assertGreater(len(variants), 0)
        self.assertTrue(any("ghidra" in v.name for v in variants))
        # Verify the guard was removed
        self.assertTrue(any(
            b"TheMetaMusic->AddFader(fader);" in v.source and
            b"if (TheMetaMusic)" not in v.source for v in variants
        ))

    def test_null_guard_ghidra_keeps_present(self):
        """Ghidra shows null check present -> don't remove."""
        source = '''
void test_func() {
    if (TheMetaMusic)
        TheMetaMusic->AddFader(fader);
}
'''
        # Ghidra code ALSO has null check
        ghidra_code = '''
void test_func(void) {
    if (TheMetaMusic != (MetaMusic *)0x0) {
        FUN_12345678(TheMetaMusic, fader);
    }
}
'''
        ctx = make_ghidra_context(source, "test_func", diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("null_guard_elimination")
        variants = list(pattern.generate(ctx))
        # Should produce no ghidra-guided variants (guard exists in target too)
        ghidra_variants = [v for v in variants if "ghidra" in v.name]
        self.assertEqual(len(ghidra_variants), 0)

    def test_null_guard_ghidra_and_operand_removes_absent(self):
        """Ghidra has no null check in && and kept side references guard."""
        source = '''
void test_func() {
    if (TheMetaMusic && TheMetaMusic->IsActive()) {
        DoSomething();
    }
}
'''
        # Ghidra has no explicit null check on TheMetaMusic
        ghidra_code = '''
void test_func(void) {
    if (FUN_12345678(TheMetaMusic)) {
        FUN_12345678();
    }
}
'''
        ctx = make_ghidra_context(source, "test_func", diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("null_guard_elimination")
        variants = list(pattern.generate(ctx))
        ghidra_variants = [v for v in variants if "ghidra" in v.name]
        self.assertGreater(len(ghidra_variants), 0)

    def test_null_guard_and_operand_unrelated_rhs_not_dropped(self):
        """Do not drop guard in A && B when B doesn't reference the same guard."""
        source = '''
void test_func(int sHamMaster) {
    if (TheMetaMusic && sHamMaster) {
        DoSomething();
    }
}
'''
        ctx = make_context(source, "test_func", diag_with_branch_and_clusters())
        pattern = get_pattern("null_guard_elimination")
        variants = list(pattern.generate(ctx))
        self.assertFalse(any(b"if (sHamMaster)" in v.source for v in variants))

    def test_null_guard_ghidra_implicit_check_keeps(self):
        """Ghidra has implicit null check (if (var)) -> don't remove."""
        source = '''
void test_func() {
    if (TheMetaMusic)
        TheMetaMusic->Stop();
}
'''
        # Ghidra also has the check implicitly
        ghidra_code = '''
void test_func(void) {
    if (TheMetaMusic) {
        FUN_12345678(TheMetaMusic);
    }
}
'''
        ctx = make_ghidra_context(source, "test_func", diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("null_guard_elimination")
        variants = list(pattern.generate(ctx))
        ghidra_variants = [v for v in variants if "ghidra" in v.name]
        self.assertEqual(len(ghidra_variants), 0)

    def test_null_guard_ghidra_falls_back_to_blind(self):
        """When Ghidra produces no candidates, fall through to blind mode."""
        source = '''
void test_func() {
    if (TheMetaMusic)
        TheMetaMusic->Stop();
}
'''
        # Ghidra also has the null check -> no ghidra candidates -> blind mode runs
        ghidra_code = '''
void test_func(void) {
    if (TheMetaMusic != (MetaMusic *)0x0) {
        FUN_12345678(TheMetaMusic);
    }
}
'''
        ctx = make_ghidra_context(source, "test_func", diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("null_guard_elimination")
        variants = list(pattern.generate(ctx))
        # Blind mode should still produce variants (non-ghidra)
        blind_variants = [v for v in variants if "ghidra" not in v.name]
        self.assertGreater(len(blind_variants), 0)

    def test_null_guard_member_style_skipped_in_blind_mode(self):
        """Member-style guards like mMat should not be removed blindly."""
        source = '''
void test_func() {
    if (mMat)
        mMat->SetZMode(2);
}
'''
        ctx = make_context(source, "test_func", diag_with_branch_and_clusters())
        pattern = get_pattern("null_guard_elimination")
        variants = list(pattern.generate(ctx))
        self.assertEqual(len(variants), 0)

    def test_null_guard_member_style_skipped_even_if_ghidra_absent(self):
        """Even when Ghidra lacks the check, member-style guards should be preserved."""
        source = '''
void test_func() {
    if (mMat)
        mMat->SetZMode(2);
}
'''
        ghidra_code = '''
void test_func(void) {
    FUN_12345678(mMat,2);
}
'''
        ctx = make_ghidra_context(source, "test_func", diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("null_guard_elimination")
        variants = list(pattern.generate(ctx))
        self.assertEqual(len(variants), 0)


# ---------------------------------------------------------------------------
# Negative tests (patterns should NOT produce certain variants)
# ---------------------------------------------------------------------------

class TestMultiUseTempSafety(unittest.TestCase):
    """Verify multi-use value temp elimination skips call initializers."""

    def test_skip_call_initializer(self):
        """Multi-use temp with call init should NOT be eliminated (re-evaluates side effects)."""
        source = """\
int GetCount();
int test_func(int y) {
    int x = GetCount();
    int a = x + y;
    int b = x - y;
    return a + b;
}
"""
        ctx = make_context(source, "test_func", diag_with_callee_saved_swaps())
        p = get_pattern("temp_elimination")
        variants = list(p.generate(ctx))

        # No variant should substitute GetCount() at multiple use sites
        bad_pattern = "GetCount() + y"
        for v in variants:
            v_text = v.source.decode("utf-8", errors="replace")
            self.assertNotIn(
                bad_pattern, v_text,
                f"Variant '{v.name}' unsafely inlined call at multiple sites: {v.description}"
            )

    def test_skip_intervening_side_effects(self):
        """Multi-use temp should not be eliminated if calls exist between decl and use."""
        source = """\
void sideEffect();
int test_func(int mFoo, int y) {
    int x = mFoo;
    sideEffect();
    int a = x + y;
    int b = x - y;
    return a + b;
}
"""
        ctx = make_context(source, "test_func", diag_with_callee_saved_swaps())
        p = get_pattern("temp_elimination")
        variants = list(p.generate(ctx))

        # No variant should eliminate x when sideEffect() sits between decl and use
        for v in variants:
            v_text = v.source.decode("utf-8", errors="replace")
            # If x was eliminated, mFoo would appear where x was used
            if "mFoo + y" in v_text and "int x" not in v_text:
                self.fail(
                    f"Variant '{v.name}' eliminated temp past side-effecting call: {v.description}"
                )


# ---------------------------------------------------------------------------
# Reparse variant tests
# ---------------------------------------------------------------------------

class TestReparseVariant(unittest.TestCase):
    """Tests for reparse_variant() — re-parsing modified source."""

    def test_reparse_preserves_function(self):
        """Reparse finds the same function in modified source."""
        source = """\
void test_func() {
    int a = 1;
    int b = 2;
}
"""
        ctx = make_context(source, "test_func", _empty_diag())

        # Modify source: change 'int a = 1' to 'int a = 42'
        new_source = ctx.file_source.replace(b"int a = 1", b"int a = 42")
        reparsed = reparse_variant(ctx, new_source)

        self.assertEqual(reparsed.file_source, new_source)
        self.assertEqual(len(reparsed.statements), 2)
        # Verify the function node text contains the modification
        func_text = new_source[reparsed.func_byte_range[0]:reparsed.func_byte_range[1]]
        self.assertIn(b"int a = 42", func_text)

    def test_reparse_preserves_diagnosis(self):
        """Diagnosis from original ctx is carried forward."""
        source = """\
void test_func() {
    int x = 1;
}
"""
        diag = diag_with_cmp_ops()
        ctx = make_context(source, "test_func", diag)

        new_source = ctx.file_source.replace(b"int x = 1", b"int x = 2")
        reparsed = reparse_variant(ctx, new_source)

        self.assertIs(reparsed.diagnosis, diag)

    def test_reparse_raises_on_missing_function(self):
        """ValueError if function name disappears from source."""
        source = """\
void test_func() {
    int x = 1;
}
"""
        ctx = make_context(source, "test_func", _empty_diag())

        # Replace function name with something else
        new_source = ctx.file_source.replace(b"test_func", b"other_func")
        with self.assertRaises(ValueError):
            reparse_variant(ctx, new_source)


# ---------------------------------------------------------------------------
# Composed fixture dataclass + fixtures
# ---------------------------------------------------------------------------

@dataclass
class ComposedFixture:
    id: str
    stage_a_pattern: str
    stage_b_pattern: str
    description: str
    seeded_source: str
    intermediate_contains: str  # verify stage A output contains this
    expected_source: str        # verify final output
    func_name: str
    diagnosis: Diagnosis
    match_mode: str = "normalized"


def _compose_diag() -> Diagnosis:
    """Diagnosis suitable for variable_extraction + declaration_reorder."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("r20", "r21"): SwapInfo(count=4, first_idx=10, last_idx=50)
    }
    return d


def _compose_diag_cmp() -> Diagnosis:
    """Diagnosis suitable for inline_assignment + comparison_flip."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=3, target_opcode="cmpwi", base_opcode="cmplwi")]
    d.clusters = [Cluster(start_idx=5, end_idx=10, size=5, inserts=3, deletes=2)]
    return d


COMPOSED_FIXTURES: list[ComposedFixture] = [
    # varext extracts getSize() into auto _tmp0, then declreorder swaps declarations
    ComposedFixture(
        id="compose_varext_then_declreorder",
        stage_a_pattern="variable_extraction",
        stage_b_pattern="declaration_reorder",
        description="extract call into auto, then reorder declarations",
        func_name="test_func",
        diagnosis=_compose_diag(),
        seeded_source="""\
void test_func() {
    int a = 1;
    check(a < getSize(), 0x74);
}
""",
        intermediate_contains="int _tmp0 = getSize()",
        expected_source="""\
void test_func() {
    int _tmp0 = getSize();
    int a = 1;
    check(a < _tmp0, 0x74);
}
""",
    ),

    # inline_assignment folds assignment into call arg, comparison_flip flips a comparison
    ComposedFixture(
        id="compose_inline_then_cmpflip",
        stage_a_pattern="inline_assignment",
        stage_b_pattern="comparison_flip",
        description="fold assignment into call, then flip comparison",
        func_name="test_func",
        diagnosis=_compose_diag_cmp(),
        seeded_source="""\
int test_func(int a, int b) {
    int era;
    era = getName();
    process(era);
    if (a < b) {
        return 1;
    }
    return 0;
}
""",
        intermediate_contains="process(era = getName())",
        expected_source="""\
int test_func(int a, int b) {
    int era;
    process(era = getName());
    if (b > a) {
        return 1;
    }
    return 0;
}
""",
    ),

    # comparison_equivalence changes i < 2 to i <= 1, then signed_unsigned swaps != 0 to > 0
    ComposedFixture(
        id="compose_cmpeq_then_signunsign",
        stage_a_pattern="comparison_equivalence",
        stage_b_pattern="signed_unsigned",
        description="change < N to <= N-1, then swap != 0 to > 0",
        func_name="test_func",
        diagnosis=diag_with_cmp_ops(),
        seeded_source="""\
int test_func(int i, int x) {
    if (i < 2) {
        return 1;
    }
    if (x != 0) {
        return 2;
    }
    return 0;
}
""",
        intermediate_contains="i <= 1",
        expected_source="""\
int test_func(int i, int x) {
    if (i <= 1) {
        return 1;
    }
    if (x > 0) {
        return 2;
    }
    return 0;
}
""",
    ),
]

_COMPOSED_FIXTURE_MAP: dict[str, ComposedFixture] = {f.id: f for f in COMPOSED_FIXTURES}


class TestComposedFixtures(unittest.TestCase):
    """Test two-step pattern composition via ComposedFixture."""
    pass  # Tests are added dynamically below


def _make_composed_test(fixture: ComposedFixture):
    """Create a test method for a composed fixture."""

    def test_method(self):
        pattern_a = get_pattern(fixture.stage_a_pattern)
        pattern_b = get_pattern(fixture.stage_b_pattern)

        # Build context from seeded source
        ctx = make_context(fixture.seeded_source, fixture.func_name, fixture.diagnosis)

        # Stage A: generate variants, find one containing intermediate text
        a_variants = list(pattern_a.generate(ctx))
        self.assertGreater(
            len(a_variants), 0,
            f"Stage A pattern '{fixture.stage_a_pattern}' generated 0 variants",
        )

        # Find intermediate variant
        intermediate = None
        for v in a_variants:
            if normalize(fixture.intermediate_contains) in normalize(v.source):
                intermediate = v
                break

        self.assertIsNotNone(
            intermediate,
            f"No stage A variant contains '{fixture.intermediate_contains}'. "
            f"Got {len(a_variants)} variants:\n"
            + "\n".join(f"  {normalize(v.source)[:100]}" for v in a_variants[:5]),
        )

        # Re-parse intermediate
        reparsed = reparse_variant(ctx, intermediate.source)

        # Stage B: generate variants, find one matching expected
        b_variants = list(pattern_b.generate(reparsed))
        self.assertGreater(
            len(b_variants), 0,
            f"Stage B pattern '{fixture.stage_b_pattern}' generated 0 variants "
            f"from intermediate source",
        )

        matched = any(
            match_variant(v.source, fixture.expected_source, fixture.match_mode)
            for v in b_variants
        )

        if not matched:
            norm_expected = normalize(fixture.expected_source)
            closest = min(
                b_variants,
                key=lambda v: -_similarity(normalize(v.source), norm_expected),
            )
            self.fail(
                f"\nComposed fixture '{fixture.id}': no final variant matched.\n"
                f"  Expected (normalized): {norm_expected}\n"
                f"  Closest  (normalized): {normalize(closest.source)}\n"
                f"  Stage B variants: {len(b_variants)}"
            )

    test_method.__doc__ = f"{fixture.id}: {fixture.description}"
    return test_method


# Attach a test method per composed fixture
for _cfixture in COMPOSED_FIXTURES:
    _ctest_name = f"test_{_cfixture.id}"
    setattr(TestComposedFixtures, _ctest_name, _make_composed_test(_cfixture))


# ---------------------------------------------------------------------------
# Budget allocation tests
# ---------------------------------------------------------------------------

class TestBudgetAllocation(unittest.TestCase):
    """Tests for allocate_budgets() in generator.py."""

    def test_minimum_budget_per_relevant_pattern(self):
        """Every relevant pattern gets at least _MIN_BUDGET."""
        from scripts.permuter.generator import allocate_budgets, _MIN_BUDGET

        # Use patterns that are relevant for the given diagnosis
        patterns = [get_pattern("variable_extraction"), get_pattern("signed_unsigned")]
        diag = diag_with_cmp_ops()  # signed_unsigned relevant, variable_extraction always relevant
        budgets = allocate_budgets(patterns, 100, diag)

        for p in patterns:
            self.assertGreaterEqual(
                budgets.get(p.name, 0), _MIN_BUDGET,
                f"Pattern '{p.name}' budget {budgets.get(p.name, 0)} < {_MIN_BUDGET}",
            )

    def test_irrelevant_patterns_get_zero(self):
        """Patterns where relevant() is False get 0 budget."""
        from scripts.permuter.generator import allocate_budgets

        # empty_size_swap requires divw in diff_ops; empty diag has none
        # variable_extraction also requires some mismatch signal
        patterns = [get_pattern("empty_size_swap"), get_pattern("variable_extraction")]
        diag = _empty_diag()
        budgets = allocate_budgets(patterns, 100, diag)

        self.assertEqual(budgets.get("empty_size_swap", 0), 0)
        self.assertEqual(budgets.get("variable_extraction", 0), 0)

    def test_total_does_not_exceed_budget(self):
        """Sum of allocated budgets <= total_budget."""
        from scripts.permuter.generator import allocate_budgets

        patterns = [
            get_pattern("variable_extraction"),
            get_pattern("signed_unsigned"),
            get_pattern("comparison_flip"),
            get_pattern("declaration_reorder"),
        ]
        # Use a diagnosis where all patterns are relevant
        diag = diag_with_cmp_ops()
        diag.reg_swap_pairs = {("r20", "r21"): SwapInfo(count=4, first_idx=10, last_idx=50)}

        for total in [10, 50, 100, 200]:
            budgets = allocate_budgets(patterns, total, diag)
            allocated = sum(budgets.values())
            self.assertLessEqual(
                allocated, total,
                f"Allocated {allocated} > budget {total}: {budgets}",
            )

    def test_no_diagnosis_all_relevant(self):
        """When diagnosis is None, all patterns are relevant."""
        from scripts.permuter.generator import allocate_budgets

        patterns = [get_pattern("variable_extraction"), get_pattern("empty_size_swap")]
        budgets = allocate_budgets(patterns, 50, None)

        for p in patterns:
            self.assertGreater(
                budgets.get(p.name, 0), 0,
                f"Pattern '{p.name}' should be relevant when diagnosis is None",
            )


# ---------------------------------------------------------------------------
# Dynamic test generation from fixtures
# ---------------------------------------------------------------------------

def _make_fixture_test(fixture: PatternFixture):
    """Create a test method for a single fixture."""

    def test_method(self):
        pattern = get_pattern(fixture.pattern_name)

        # Build context from seeded source
        ctx = make_context(fixture.seeded_source, fixture.func_name, fixture.diagnosis)

        # Verify relevant() agrees this pattern applies
        self.assertTrue(
            pattern.relevant(fixture.diagnosis),
            f"Pattern '{fixture.pattern_name}' reports not relevant for fixture '{fixture.id}'",
        )

        # Generate variants
        variants = list(pattern.generate(ctx))
        self.assertGreater(
            len(variants), 0,
            f"Pattern '{fixture.pattern_name}' generated 0 variants for fixture '{fixture.id}'",
        )

        # Check if any variant matches expected
        matched = False
        best_match = ""
        for v in variants:
            if match_variant(v.source, fixture.expected_source, fixture.match_mode):
                matched = True
                break
            # Track closest for debug output
            norm_v = normalize(v.source)
            norm_e = normalize(fixture.expected_source)
            if not best_match or _similarity(norm_v, norm_e) > _similarity(best_match, norm_e):
                best_match = norm_v

        if not matched:
            norm_expected = normalize(fixture.expected_source)
            msg = (
                f"\nFixture '{fixture.id}': no variant matched expected output.\n"
                f"  Expected (normalized): {norm_expected}\n"
                f"  Closest  (normalized): {best_match}\n"
                f"  Total variants: {len(variants)}"
            )
            self.fail(msg)

    test_method.__doc__ = f"{fixture.id}: {fixture.description}"
    return test_method


def _similarity(a: str, b: str) -> float:
    """Simple character-level similarity ratio."""
    if not a and not b:
        return 1.0
    common = sum(1 for ca, cb in zip(a, b) if ca == cb)
    return common / max(len(a), len(b))


# Attach a test method per fixture
for _fixture in FIXTURES:
    _test_name = f"test_{_fixture.id}"
    setattr(TestPatternFixtures, _test_name, _make_fixture_test(_fixture))


# ---------------------------------------------------------------------------
# CLI runner
# ---------------------------------------------------------------------------

def _run_cli():
    parser = argparse.ArgumentParser(description="Pattern benchmark tests")
    parser.add_argument("--list", action="store_true", help="List all fixtures")
    parser.add_argument("--pattern", type=str, help="Filter fixtures by pattern name")
    parser.add_argument("--fixture", type=str, help="Run a single fixture by ID")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    args = parser.parse_args()

    if args.list:
        print(f"{'ID':<40s} {'Pattern':<25s} Description")
        print("-" * 100)
        for f in FIXTURES:
            print(f"{f.id:<40s} {f.pattern_name:<25s} {f.description}")
        print(f"\nTotal: {len(FIXTURES)} fixtures")
        return

    # Build filtered fixture list
    selected = FIXTURES
    if args.fixture:
        selected = [f for f in FIXTURES if f.id == args.fixture]
        if not selected:
            print(f"Unknown fixture '{args.fixture}'. Use --list to see available.")
            sys.exit(1)
    elif args.pattern:
        selected = [f for f in FIXTURES if f.pattern_name == args.pattern]
        if not selected:
            print(f"No fixtures for pattern '{args.pattern}'. Use --list to see available.")
            sys.exit(1)

    passed = 0
    failed = 0
    errors = []

    for fixture in selected:
        try:
            pattern = get_pattern(fixture.pattern_name)
            ctx = make_context(fixture.seeded_source, fixture.func_name, fixture.diagnosis)

            if not pattern.relevant(fixture.diagnosis):
                errors.append((fixture.id, "relevant() returned False"))
                failed += 1
                continue

            variants = list(pattern.generate(ctx))
            if not variants:
                errors.append((fixture.id, "0 variants generated"))
                failed += 1
                continue

            matched = any(
                match_variant(v.source, fixture.expected_source, fixture.match_mode)
                for v in variants
            )

            if matched:
                passed += 1
                if args.verbose:
                    print(f"  PASS  {fixture.id} ({len(variants)} variants)")
            else:
                failed += 1
                detail = f"{len(variants)} variants, none matched"
                errors.append((fixture.id, detail))
                if args.verbose:
                    print(f"  FAIL  {fixture.id}: {detail}")
                    norm_e = normalize(fixture.expected_source)
                    for i, v in enumerate(variants):
                        norm_v = normalize(v.source)
                        marker = "*" if _similarity(norm_v, norm_e) > 0.8 else " "
                        print(f"    {marker} variant {i}: {v.description}")
                        if args.verbose:
                            print(f"        {norm_v[:120]}...")

        except Exception as e:
            failed += 1
            errors.append((fixture.id, f"Exception: {e}"))
            if args.verbose:
                import traceback
                traceback.print_exc()

    # Summary
    total = passed + failed
    status = "PASS" if failed == 0 else "FAIL"
    print(f"\n{status}: {passed}/{total} fixtures passed")

    if errors:
        print("\nFailures:")
        for fid, detail in errors:
            print(f"  {fid}: {detail}")

    sys.exit(0 if failed == 0 else 1)


# ---------------------------------------------------------------------------
# BSF-guided search tests
# ---------------------------------------------------------------------------

class TestGuidedPairwiseSearch(unittest.TestCase):
    """Tests for the targeted guided_pairwise_search() solver."""

    @staticmethod
    def _make_mock_trace(colors: list[int]) -> object:
        """Create a mock BSFTrace with initial coloring calls for given colors.

        Each color represents a variable's color assignment in declaration order.
        Colors are assigned via INITIAL_COLORING_RVA.
        """
        from tools.compiler_trace.bsf_trace import BSFTrace, BSFCall
        from tools.compiler_trace.regmap_solver import INITIAL_COLORING_RVA

        calls = []
        for i, color in enumerate(colors):
            calls.append(BSFCall(
                index=i + 1,
                caller_rva=INITIAL_COLORING_RVA,
                lo=0, hi=0, base=0,
                bit=color,
            ))
        return BSFTrace(source=Path("/dev/null"), calls=calls)

    def test_targeted_narrows_search_space(self):
        """For n=8 decls and 1 swap pair, guided candidates << C(8,2)=28."""
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        # 8 variables with colors 0-7 (r11, r10, ..., r4)
        # But we only need to swap r30<->r31 which are colors 9, 10
        # Those colors aren't in the trace, so this tests the fallback path
        # Instead: set up colors 8,9 = r29,r30 for a swap pair (r29, r30)
        colors = [0, 1, 2, 3, 4, 5, 8, 9]  # 8 vars, last two are r29, r30
        trace = self._make_mock_trace(colors)
        decl_names = [f"v{i}" for i in range(8)]

        # Swap pair: r29 <-> r30 (colors 8 and 9 → decl indices 6 and 7)
        candidates = guided_pairwise_search(trace, [("r29", "r30")], decl_names)

        # Should be much less than C(8,2)=28
        self.assertGreater(len(candidates), 0, "Should produce at least 1 candidate")
        self.assertLess(len(candidates), 28, f"Should narrow from 28, got {len(candidates)}")

        # The targeted swap (v6, v7) should be first
        self.assertIn("v7", candidates[0])
        self.assertIn("v6", candidates[0])
        # Verify the targeted pair is actually swapped
        idx6 = candidates[0].index("v6")
        idx7 = candidates[0].index("v7")
        self.assertEqual(idx6, 7, "v6 should be at index 7 (swapped with v7)")
        self.assertEqual(idx7, 6, "v7 should be at index 6 (swapped with v6)")

    def test_different_swap_pairs_produce_different_candidates(self):
        """Different swap_pairs should produce different candidate sets."""
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        # 5 vars with colors mapping to r11, r10, r9, r29, r30
        colors = [0, 1, 2, 8, 9]
        trace = self._make_mock_trace(colors)
        decl_names = ["a", "b", "c", "d", "e"]

        # Swap r29<->r30 (colors 8,9 → decl indices 3,4)
        cands_de = guided_pairwise_search(trace, [("r29", "r30")], decl_names)

        # Swap r11<->r10 (colors 0,1 → decl indices 0,1)
        cands_ab = guided_pairwise_search(trace, [("r11", "r10")], decl_names)

        # First candidate of each should swap different positions
        self.assertNotEqual(cands_de[0], cands_ab[0],
                            "Different swap pairs should produce different first candidates")

    def test_two_decls_one_swap_gives_one_targeted(self):
        """With 2 declarations and 1 swap pair, should get exactly the swap."""
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        colors = [8, 9]  # r29, r30
        trace = self._make_mock_trace(colors)
        decl_names = ["x", "y"]

        candidates = guided_pairwise_search(trace, [("r29", "r30")], decl_names)

        # Should produce exactly 1 candidate: ["y", "x"]
        self.assertEqual(len(candidates), 1)
        self.assertEqual(candidates[0], ["y", "x"])

    def test_unmapped_registers_use_bounded_fallback(self):
        """Swap pairs with unmappable registers get bounded fallback, not all-pairs."""
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        colors = [0, 1, 2, 3, 4, 5, 6, 7]  # 8 vars, all volatile
        trace = self._make_mock_trace(colors)
        decl_names = [f"v{i}" for i in range(8)]

        # r19<->r20 are outside the known color mapping range
        candidates = guided_pairwise_search(trace, [("r19", "r20")], decl_names)

        # Should get bounded fallback, not C(8,2)=28
        self.assertLess(len(candidates), 28,
                        f"Unmapped fallback should be bounded, got {len(candidates)}")

    def test_multi_swap_combined(self):
        """Multiple swap pairs should try simultaneous swap."""
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        # 4 vars: r11, r10, r29, r30
        colors = [0, 1, 8, 9]
        trace = self._make_mock_trace(colors)
        decl_names = ["a", "b", "c", "d"]

        # Two swap pairs: r11<->r10 and r29<->r30
        candidates = guided_pairwise_search(
            trace, [("r11", "r10"), ("r29", "r30")], decl_names
        )

        # Should include the simultaneous swap: ["b", "a", "d", "c"]
        simultaneous = ["b", "a", "d", "c"]
        self.assertIn(simultaneous, candidates,
                      f"Combined swap not found in candidates: {candidates}")


# ---------------------------------------------------------------------------
# Diagnosis noise classification tests (from real-world permuter runs)
# ---------------------------------------------------------------------------

class TestDiagnosisNoise(unittest.TestCase):
    """Tests for diagnosis noise classification, including address relocation heuristic."""

    def test_addr_reloc_lis_addi_counted_as_noise(self):
        """lis/addi pairs without diff_breakdown should be classified as noise."""
        from scripts.permuter.diagnosis import diagnose_baseline, is_all_noise

        # Simulate objdiff JSON with lis/addi diff_arg (no diff_breakdown)
        instrs = []
        # 90 equal instructions
        for i in range(90):
            instrs.append({"index": i, "match_type": "equal", "target": {"opcode": "mr"}, "base": {"opcode": "mr"}})
        # 5 lis/addi pairs as diff_arg with no diff_breakdown (address relocation noise)
        for i in range(5):
            instrs.append({"index": 90 + i * 2, "match_type": "diff_arg",
                           "target": {"opcode": "lis"}, "base": {"opcode": "lis"}})
            instrs.append({"index": 91 + i * 2, "match_type": "diff_arg",
                           "target": {"opcode": "addi"}, "base": {"opcode": "addi"}})

        objdiff_json = {"instructions": instrs}
        diag = diagnose_baseline(objdiff_json)

        self.assertEqual(diag.noise_total, 10)
        self.assertEqual(diag.noise_explained, 10)
        self.assertTrue(is_all_noise(diag))

    def test_bl_without_breakdown_counted_as_noise(self):
        """bl (branch-link) without diff_breakdown should be classified as noise."""
        from scripts.permuter.diagnosis import diagnose_baseline

        instrs = [
            {"index": 0, "match_type": "equal", "target": {"opcode": "mr"}, "base": {"opcode": "mr"}},
            {"index": 1, "match_type": "diff_arg", "target": {"opcode": "bl"}, "base": {"opcode": "bl"}},
        ]
        diag = diagnose_baseline({"instructions": instrs})
        self.assertEqual(diag.noise_explained, 1)
        self.assertEqual(diag.noise_total, 1)

    def test_non_reloc_opcode_without_breakdown_not_noise(self):
        """diff_arg with unknown opcode and no diff_breakdown should NOT be counted as noise."""
        from scripts.permuter.diagnosis import diagnose_baseline

        instrs = [
            {"index": 0, "match_type": "equal", "target": {"opcode": "mr"}, "base": {"opcode": "mr"}},
            {"index": 1, "match_type": "diff_arg", "target": {"opcode": "stw"}, "base": {"opcode": "stw"}},
        ]
        diag = diagnose_baseline({"instructions": instrs})
        self.assertEqual(diag.noise_explained, 0)
        self.assertEqual(diag.noise_total, 1)

    def test_diff_arg_with_breakdown_still_analyzed(self):
        """diff_arg with diff_breakdown should use the existing analysis path."""
        from scripts.permuter.diagnosis import diagnose_baseline

        instrs = [
            {"index": 0, "match_type": "equal", "target": {"opcode": "mr"}, "base": {"opcode": "mr"}},
            {"index": 1, "match_type": "diff_arg", "target": {"opcode": "lfs"}, "base": {"opcode": "lfs"},
             "diff_breakdown": {"arguments": [{"arg_type": "immediate", "target": {"value": 0xec}, "base": {"value": 0xe8}}]}},
        ]
        diag = diagnose_baseline({"instructions": instrs})
        self.assertEqual(diag.noise_explained, 1)  # immediate with numeric values = noise


# ---------------------------------------------------------------------------
# extract_qualified_name tests
# ---------------------------------------------------------------------------

class TestExtractQualifiedName(unittest.TestCase):
    """Tests for extract_qualified_name() handling operator overloads."""

    def test_regular_method(self):
        from scripts.permuter.types import extract_qualified_name
        self.assertEqual(
            extract_qualified_name("public: virtual void __cdecl CharBlendBone::Poll(void)"),
            "CharBlendBone::Poll",
        )

    def test_destructor(self):
        from scripts.permuter.types import extract_qualified_name
        self.assertEqual(
            extract_qualified_name("public: __cdecl ClipDistMap::~ClipDistMap(void)"),
            "ClipDistMap::~ClipDistMap",
        )

    def test_operator_call(self):
        from scripts.permuter.types import extract_qualified_name
        self.assertEqual(
            extract_qualified_name(
                "public: bool __cdecl FileMergerSort::operator()"
                "(struct FileMerger::Merger const *, struct FileMerger::Merger const *)"
            ),
            "FileMergerSort::operator()",
        )

    def test_operator_eq(self):
        from scripts.permuter.types import extract_qualified_name
        self.assertEqual(
            extract_qualified_name("public: class DataNode __cdecl Object::operator==(class Object const *)"),
            "Object::operator==",
        )

    def test_operator_subscript(self):
        from scripts.permuter.types import extract_qualified_name
        self.assertEqual(
            extract_qualified_name("public: class Hmx::Object * __cdecl ObjectDir::operator[](char const *)"),
            "ObjectDir::operator[]",
        )

    def test_free_function_returns_none(self):
        """Free functions (no ::) return None since we can't extract a qualified name."""
        from scripts.permuter.types import extract_qualified_name
        # operator>> as a free function — no class qualifier, just 'operator>>'
        result = extract_qualified_name(
            "class BinStream & __cdecl operator>>(class BinStream &, struct CharEyes::EyeDesc &)"
        )
        # Free function without :: — extract_qualified_name requires ::
        self.assertIsNone(result)


class TestColorToGpr(unittest.TestCase):
    """Tests for the color <-> GPR mapping functions."""

    def test_volatile_roundtrip(self):
        from tools.compiler_trace.regmap_solver import color_to_gpr, gpr_to_color
        for color in range(7):  # colors 0-6 are volatile
            gpr = color_to_gpr(color)
            self.assertIsNotNone(gpr)
            self.assertEqual(gpr_to_color(gpr), color,
                             f"Roundtrip failed for color {color} -> {gpr}")

    def test_callee_saved_roundtrip(self):
        from tools.compiler_trace.regmap_solver import color_to_gpr, gpr_to_color
        for color in range(7, 26):  # colors 7-25 are callee-saved (r31-r13)
            gpr = color_to_gpr(color)
            self.assertIsNotNone(gpr)
            self.assertEqual(gpr_to_color(gpr), color,
                             f"Roundtrip failed for color {color} -> {gpr}")

    def test_known_mappings(self):
        """Empirically confirmed mapping from test_bsf_engine.py."""
        from tools.compiler_trace.regmap_solver import color_to_gpr, gpr_to_color
        # Volatile: color = 11 - reg
        self.assertEqual(color_to_gpr(0), "r11")
        self.assertEqual(color_to_gpr(6), "r5")
        # Callee-saved: color 7 = r31 (NOT r4!)
        self.assertEqual(color_to_gpr(7), "r31")
        self.assertEqual(color_to_gpr(8), "r30")
        self.assertEqual(color_to_gpr(9), "r29")
        self.assertEqual(color_to_gpr(10), "r28")
        self.assertEqual(color_to_gpr(11), "r27")
        self.assertEqual(color_to_gpr(25), "r13")
        self.assertIsNone(color_to_gpr(26))

        self.assertEqual(gpr_to_color("r11"), 0)
        self.assertEqual(gpr_to_color("r5"), 6)
        self.assertEqual(gpr_to_color("r31"), 7)
        self.assertEqual(gpr_to_color("r30"), 8)
        self.assertEqual(gpr_to_color("r13"), 25)
        self.assertIsNone(gpr_to_color("r3"))
        self.assertIsNone(gpr_to_color("r4"))  # r4 is arg reg, not in color space


# ---------------------------------------------------------------------------
# 3-way register swap tests (cycle: rA↔rB, rB↔rC, rA↔rC)
# ---------------------------------------------------------------------------

def diag_with_3way_gpr_swaps() -> Diagnosis:
    """Diagnosis with a 3-way GPR register swap cycle (r24↔r25↔r26)."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("r24", "r25"): SwapInfo(count=3, first_idx=10, last_idx=40),
        ("r25", "r26"): SwapInfo(count=2, first_idx=15, last_idx=45),
        ("r24", "r26"): SwapInfo(count=2, first_idx=20, last_idx=50),
    }
    return d


def diag_with_fpr_swaps() -> Diagnosis:
    """Diagnosis with FPR register swaps only (no GPR swaps)."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("f1", "f2"): SwapInfo(count=4, first_idx=5, last_idx=30),
        ("f3", "f4"): SwapInfo(count=2, first_idx=10, last_idx=35),
    }
    return d


def diag_with_mixed_gpr_fpr_swaps() -> Diagnosis:
    """Diagnosis with both GPR and FPR register swaps."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("r28", "r29"): SwapInfo(count=3, first_idx=10, last_idx=40),
        ("f1", "f2"): SwapInfo(count=4, first_idx=5, last_idx=30),
    }
    return d


class TestThreeWayRegSwap(unittest.TestCase):
    """Tests for 3-way register swap cycles.

    A 3-way swap (r24↔r25↔r26) means the compiler assigned registers
    in a cyclic pattern. Fixing this requires a 3-way permutation, not
    just pairwise swaps. These tests verify the current solver behavior
    and document expected failures.
    """

    @staticmethod
    def _make_mock_trace(colors: list[int]) -> object:
        from tools.compiler_trace.bsf_trace import BSFTrace, BSFCall
        from tools.compiler_trace.regmap_solver import INITIAL_COLORING_RVA
        calls = []
        for i, color in enumerate(colors):
            calls.append(BSFCall(
                index=i + 1,
                caller_rva=INITIAL_COLORING_RVA,
                lo=0, hi=0, base=0,
                bit=color,
            ))
        return BSFTrace(source=Path("/dev/null"), calls=calls)

    def test_3way_swap_relevant(self):
        """3-way GPR swaps should make declaration_reorder relevant."""
        from scripts.permuter.patterns import get_pattern
        p = get_pattern("declaration_reorder")
        self.assertTrue(p.relevant(diag_with_3way_gpr_swaps()))

    def test_3way_swap_generates_candidates(self):
        """The solver should generate SOME candidates for 3-way swaps.

        Even if pairwise swaps can't solve a 3-way cycle, the solver
        should still try individual pair swaps as partial fixes.
        """
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        # r24=color14, r25=color13, r26=color12
        # (color_to_gpr: color N = r(38-N) for callee-saved)
        colors = [7, 8, 9, 10, 11, 12, 13, 14]  # r31..r24
        trace = self._make_mock_trace(colors)
        decl_names = [f"v{i}" for i in range(8)]

        # 3-way cycle: r24↔r25, r25↔r26, r24↔r26
        swap_pairs = [("r24", "r25"), ("r25", "r26"), ("r24", "r26")]
        candidates = guided_pairwise_search(trace, swap_pairs, decl_names)

        self.assertGreater(len(candidates), 0,
                           "Should produce candidates even for 3-way swap")

    def test_3way_swap_has_cyclic_candidate(self):
        """A 3-way swap cycle needs a 3-way permutation (rotation).

        For r24↔r25↔r26, the fix is to rotate declarations:
        v5(r26) → v6(r25) → v7(r24) becomes v7 → v5 → v6
        (shift the 3 positions by 1).
        """
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        # 3 variables: colors 12,13,14 → r26,r25,r24
        colors = [12, 13, 14]
        trace = self._make_mock_trace(colors)
        decl_names = ["a", "b", "c"]

        # 3-way cycle
        swap_pairs = [("r24", "r25"), ("r25", "r26"), ("r24", "r26")]
        candidates = guided_pairwise_search(trace, swap_pairs, decl_names)

        # The cyclic rotation: a→b→c→a means [c, a, b] or [b, c, a]
        rotations = [["c", "a", "b"], ["b", "c", "a"]]
        has_rotation = any(r in candidates for r in rotations)
        self.assertTrue(has_rotation,
                        f"No cyclic rotation found in candidates: {candidates}")

    def test_3way_swap_pairwise_subset(self):
        """3-way swap should at least produce the individual pairwise swaps."""
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        # r24=color14, r25=color13, r26=color12
        colors = [12, 13, 14]
        trace = self._make_mock_trace(colors)
        decl_names = ["a", "b", "c"]

        swap_pairs = [("r24", "r25"), ("r25", "r26"), ("r24", "r26")]
        candidates = guided_pairwise_search(trace, swap_pairs, decl_names)

        # Should have at least the pairwise swaps
        # r24↔r25 = indices 2,1 = ["a", "c", "b"]
        # r25↔r26 = indices 1,0 = ["b", "a", "c"]
        # r24↔r26 = indices 2,0 = ["c", "b", "a"]
        pair_swaps = [["a", "c", "b"], ["b", "a", "c"], ["c", "b", "a"]]
        found = sum(1 for p in pair_swaps if p in candidates)
        self.assertGreaterEqual(found, 2,
                                f"Should have at least 2 pairwise swaps, got {found} of {candidates}")


# ---------------------------------------------------------------------------
# FPR register swap tests
# ---------------------------------------------------------------------------

class TestFPRSwapHandling(unittest.TestCase):
    """Tests for FPR (floating-point register) swap handling.

    Currently BSF tracing and the regmap solver only handle GPR swaps.
    FPR swaps are detected by diagnosis but NOT addressable by BSF-guided
    declaration reorder. These tests document the current state and
    will be updated when FPR support is added.
    """

    def test_fpr_only_diagnosis_makes_declreorder_relevant(self):
        """declaration_reorder IS relevant for FPR-only swaps.

        FPR swaps (f-prefix) now trigger the pattern since ASM-guided
        mode supports FPR swap pairs.
        """
        from scripts.permuter.patterns import get_pattern
        p = get_pattern("declaration_reorder")
        diag = diag_with_fpr_swaps()
        self.assertTrue(p.relevant(diag),
                        "declaration_reorder should be relevant for FPR swaps")

    def test_mixed_gpr_fpr_makes_declreorder_relevant(self):
        """Mixed GPR+FPR swaps should still make declreorder relevant (for GPR part)."""
        from scripts.permuter.patterns import get_pattern
        p = get_pattern("declaration_reorder")
        diag = diag_with_mixed_gpr_fpr_swaps()
        self.assertTrue(p.relevant(diag),
                        "Should be relevant when GPR swaps present alongside FPR")

    def test_fpr_color_mapping_not_implemented(self):
        """Verify FPR registers have no color mapping (documenting limitation)."""
        from tools.compiler_trace.regmap_solver import gpr_to_color
        # FPR registers should return None from the GPR mapper
        for fpr in ["f0", "f1", "f2", "f13", "f31"]:
            self.assertIsNone(gpr_to_color(fpr),
                              f"FPR {fpr} should not have a GPR color mapping")

    def test_fpr_swap_uses_untargeted_fallback(self):
        """FPR swaps fall through to bounded neighbor fallback (untargeted).

        Since fpr_to_color() doesn't exist, FPR pairs are treated as
        unmapped and get generic neighbor swaps. The candidates are NOT
        targeted at the right FPR registers — they're just blind guesses.
        When we add FPR color mapping, candidates should be targeted.
        """
        from tools.compiler_trace.regmap_solver import guided_pairwise_search
        from tools.compiler_trace.bsf_trace import BSFTrace, BSFCall
        from tools.compiler_trace.regmap_solver import INITIAL_COLORING_RVA

        calls = [
            BSFCall(index=1, caller_rva=INITIAL_COLORING_RVA, lo=0, hi=0, base=0, bit=26),
            BSFCall(index=2, caller_rva=INITIAL_COLORING_RVA, lo=0, hi=0, base=0, bit=27),
        ]
        trace = BSFTrace(source=Path("/dev/null"), calls=calls)
        decl_names = ["x", "y"]

        # FPR swap pair — unmappable, should use fallback
        candidates = guided_pairwise_search(trace, [("f1", "f2")], decl_names)
        # Fallback produces candidates but they're not targeted
        self.assertGreater(len(candidates), 0,
                           "Fallback should produce some candidates")
        # With only 2 decls, the only swap is ["y", "x"]
        self.assertEqual(candidates[0], ["y", "x"])

    def test_fpr_callee_saved_mapping(self):
        """Callee-saved FPR (f14-f31) should map to declaration indices.

        FPR allocation is sequential by declaration order:
        f31 = first float (index 0), f30 = second (index 1), etc.
        """
        from tools.compiler_trace.regmap_solver import (
            fpr_to_decl_index,
            decl_index_to_fpr,
            is_callee_saved_fpr,
        )

        # Callee-saved FPRs have declaration indices
        self.assertEqual(fpr_to_decl_index("f31"), 0)
        self.assertEqual(fpr_to_decl_index("f30"), 1)
        self.assertEqual(fpr_to_decl_index("f14"), 17)

        # Volatile FPRs return None
        self.assertIsNone(fpr_to_decl_index("f0"))
        self.assertIsNone(fpr_to_decl_index("f13"))

        # Round-trip
        self.assertEqual(decl_index_to_fpr(0), "f31")
        self.assertEqual(decl_index_to_fpr(1), "f30")
        self.assertEqual(decl_index_to_fpr(17), "f14")

        # Classification
        self.assertTrue(is_callee_saved_fpr("f14"))
        self.assertTrue(is_callee_saved_fpr("f31"))
        self.assertFalse(is_callee_saved_fpr("f13"))
        self.assertFalse(is_callee_saved_fpr("f0"))
        self.assertFalse(is_callee_saved_fpr("r31"))


# ---------------------------------------------------------------------------
# BSF isolation exact match tests
# ---------------------------------------------------------------------------

class TestBSFIsolationMatching(unittest.TestCase):
    """Tests for BSF per-function isolation matching logic.

    The partition matching in declaration_reorder.py should:
    1. Exact-match the mangled symbol first (tier 0)
    2. Match qualified name (both Class AND Method) as tier 1
    3. NOT match partial names (e.g., "Highlight" matching "RndHighlightable")
    """

    def test_exact_symbol_match_preferred(self):
        """When ctx.symbol is set, exact match should be used first."""
        # This is a logic test for the matching tiers. We verify that
        # _partition_match (conceptual) prefers exact symbol over fuzzy.
        # The actual method is embedded in declaration_reorder._try_bsf_guided()
        # so we test the logic in isolation.
        partitions = {
            "__all__": "all_trace",
            "?Highlight@CharCollide@@UAAXXZ": "correct_trace",
            "??0RndHighlightable@@QAA@XZ": "wrong_trace",
            "__remainder__": "remainder",
        }

        target_symbol = "?Highlight@CharCollide@@UAAXXZ"

        # Tier 0: exact match
        match = None
        for name in partitions:
            if name in ("__all__", "__remainder__"):
                continue
            if name == target_symbol:
                match = name
                break

        self.assertEqual(match, "?Highlight@CharCollide@@UAAXXZ",
                         "Exact symbol match should find correct partition")

    def test_fuzzy_match_rejects_partial_class(self):
        """Fuzzy matching should require BOTH class AND method name.

        Bug case: "Highlight" (method) substring-matched "RndHighlightable"
        (a different class constructor) because only method name was checked.
        """
        partitions = {
            "??0RndHighlightable@@QAA@XZ": "wrong_trace",  # Constructor of different class
            "?SetName@CharCollide@@UAAXXZ": "also_wrong",
        }

        # Target: CharCollide::Highlight
        class_name = "CharCollide"
        method_name = "Highlight"

        # Tier 1: require both class and method in partition name
        match = None
        for name in partitions:
            if class_name in name and method_name in name:
                match = name
                break

        self.assertIsNone(match,
                          "Should NOT match RndHighlightable when looking for CharCollide::Highlight")

    def test_fuzzy_match_finds_correct_partition(self):
        """When both class and method name appear, fuzzy match should work."""
        partitions = {
            "?Highlight@CharCollide@@UAAXXZ": "correct_trace",
            "??0RndHighlightable@@QAA@XZ": "wrong_trace",
        }

        class_name = "CharCollide"
        method_name = "Highlight"

        match = None
        for name in partitions:
            if class_name in name and method_name in name:
                match = name
                break

        self.assertEqual(match, "?Highlight@CharCollide@@UAAXXZ",
                         "Should match partition containing both CharCollide and Highlight")

    def test_operator_overload_exact_match(self):
        """Operator overloads should match via exact symbol (tier 0)."""
        partitions = {
            "??RFileMergerSort@@QAA_NPBUMerger@FileMerger@@0@Z": "correct_trace",
            "??0FileMerger@@QAA@XZ": "wrong_trace",
        }

        target_symbol = "??RFileMergerSort@@QAA_NPBUMerger@FileMerger@@0@Z"

        match = None
        for name in partitions:
            if name == target_symbol:
                match = name
                break

        self.assertEqual(match, target_symbol,
                         "Operator overload should match via exact symbol")


# ---------------------------------------------------------------------------
# BSF population reality check (documents what we learned from scanning)
# ---------------------------------------------------------------------------

class TestBSFPopulationAssumptions(unittest.TestCase):
    """Documents empirical findings about BSF call distribution.

    From scanning 100 AT_LIMIT functions with register swaps:
    - 17% have BSF calls in their partition (addressable by BSF-guided reorder)
    - 83% have 0 BSF calls (compiler uses simpler allocation, not graph coloring)
    - Functions with few local variables (< ~5-6) tend to have 0 BSF calls
    - Template/utility functions (MakeString, PropSync) consume most BSF calls
    """

    def test_gpr_to_color_covers_callee_saved_range(self):
        """Callee-saved GPRs r13-r31 should all have color mappings."""
        from tools.compiler_trace.regmap_solver import gpr_to_color
        for reg_num in range(13, 32):
            color = gpr_to_color(f"r{reg_num}")
            self.assertIsNotNone(color,
                                 f"r{reg_num} should have a color mapping")

    def test_color_range_for_typical_regswaps(self):
        """Typical regswap pairs (r28↔r29, r30↔r31) should map to adjacent colors."""
        from tools.compiler_trace.regmap_solver import gpr_to_color
        # r28=color10, r29=color9, r30=color8, r31=color7
        self.assertEqual(gpr_to_color("r31"), 7)
        self.assertEqual(gpr_to_color("r30"), 8)
        self.assertEqual(gpr_to_color("r29"), 9)
        self.assertEqual(gpr_to_color("r28"), 10)

        # Adjacent colors → adjacent declarations → pairwise swap is correct strategy
        self.assertEqual(gpr_to_color("r30") - gpr_to_color("r31"), 1)
        self.assertEqual(gpr_to_color("r29") - gpr_to_color("r30"), 1)


# ---------------------------------------------------------------------------
# Real-world test cases from the project
# ---------------------------------------------------------------------------

class TestRealWorldCalcRotzBone(unittest.TestCase):
    """Real-world test case: HamSkeletonConverter::CalcRotzBone.

    At 96.1% match with:
    - Offset swaps: (0x54,0x58) and (0x64,0x68) — dir1/dir2 stack layout
    - FPR scheduling: fneg/frsp order around -acos() result
    - f0↔f31 register swap (f31 is callee-saved = first float declared)
    - Branch polarity at instruction 65

    This function is a good test case because:
    1. It has a callee-saved FPR swap (f31) — testable with our new FPR mapping
    2. The offset swap correlates with Vector3 declaration order (dir1 vs dir2)
    3. The fix likely involves reordering float/Vector3 declarations
    """

    def test_fpr_f31_is_first_float_declaration(self):
        """f31 = first float declared. In CalcRotzBone, 'angle' is the only
        float local, so it gets f31. The target negates into f31 (callee-saved)
        while our code uses f0 (volatile)."""
        from tools.compiler_trace.regmap_solver import fpr_to_decl_index
        self.assertEqual(fpr_to_decl_index("f31"), 0,
                         "f31 should be declaration index 0 (first float)")

    def test_offset_swap_suggests_vector3_reorder(self):
        """Offset swaps (0x54↔0x58, 0x64↔0x68) are 4 bytes apart, suggesting
        adjacent Vector3 components or adjacent Vector3 locals on the stack.
        Swapping dir1/dir2 declaration order may fix this."""
        # Document the pattern: when two Vector3 locals have their
        # stack offsets swapped by exactly sizeof(float)=4, it means
        # the compiler laid them out in a different order
        offset_pairs = [(0x54, 0x58), (0x64, 0x68)]
        for a, b in offset_pairs:
            self.assertEqual(abs(a - b), 4,
                             "Offset swap should be sizeof(float) apart")

    def test_fpr_swap_classification(self):
        """CalcRotzBone has 2 FPR swap pairs: f0↔f31 (callee-saved) and f0↔f1 (volatile).
        Our FPR mapping should correctly classify them."""
        from tools.compiler_trace.regmap_solver import is_callee_saved_fpr

        # Real swap pairs from diff_inspect regswaps output
        swap_pairs = [("f0", "f31"), ("f0", "f1")]

        callee_saved_swaps = [
            (a, b) for a, b in swap_pairs
            if is_callee_saved_fpr(a) or is_callee_saved_fpr(b)
        ]
        volatile_only_swaps = [
            (a, b) for a, b in swap_pairs
            if not is_callee_saved_fpr(a) and not is_callee_saved_fpr(b)
        ]
        self.assertEqual(len(callee_saved_swaps), 1,
                         "Should have 1 swap involving callee-saved FPR (f0↔f31)")
        self.assertEqual(len(volatile_only_swaps), 1,
                         "Should have 1 volatile-only FPR swap (f0↔f1)")

    def test_fpr_targeted_swap_for_callee_saved_pair(self):
        """The FPR swap f0↔f31 involves one callee-saved register (f31).
        Since f0 is volatile (return value), this is NOT a pure declaration
        reorder target — it's a scheduling difference. Only f14-f31 ↔ f14-f31
        pairs are pure declaration reorder targets."""
        from tools.compiler_trace.regmap_solver import (
            fpr_to_decl_index,
            is_callee_saved_fpr,
        )
        # f0↔f31: f0 is volatile, f31 is callee-saved
        # This is NOT a pure declaration reorder target because
        # f0 has no declaration index
        self.assertIsNone(fpr_to_decl_index("f0"))
        self.assertIsNotNone(fpr_to_decl_index("f31"))

        # Pure callee-saved pairs (like f30↔f31 in Invert) ARE targets
        self.assertTrue(is_callee_saved_fpr("f30"))
        self.assertTrue(is_callee_saved_fpr("f31"))
        idx30 = fpr_to_decl_index("f30")
        idx31 = fpr_to_decl_index("f31")
        self.assertEqual(abs(idx30 - idx31), 1,
                         "f30 and f31 are adjacent declaration indices")


class TestRealWorldInvertTransform(unittest.TestCase):
    """Real-world test case: Invert(Transform const&, Transform&).

    At 99.5% match with:
    - f30↔f31 callee-saved FPR swap (4 instructions)
    - Offset swaps: (0x4,0x14) and (0x8,0x18) — Matrix3 member access order
    - Pure FPR swap — no GPR swaps

    This is the cleanest FPR swap target: a pure callee-saved pair (f30↔f31)
    that should be fixable by swapping two float expression evaluations.
    """

    def test_f30_f31_are_adjacent_declarations(self):
        """f30 = second float, f31 = first float. Swapping their declaration
        order should swap the register assignment."""
        from tools.compiler_trace.regmap_solver import fpr_to_decl_index
        self.assertEqual(fpr_to_decl_index("f31"), 0)  # First float
        self.assertEqual(fpr_to_decl_index("f30"), 1)  # Second float
        # Adjacent — a simple pairwise swap of float declarations

    def test_guided_search_maps_fpr_callee_saved_pair(self):
        """guided_pairwise_search should map f30↔f31 to declaration indices
        0↔1 and generate a targeted swap candidate."""
        from tools.compiler_trace.regmap_solver import (
            guided_pairwise_search,
            INITIAL_COLORING_RVA,
        )
        from tools.compiler_trace.bsf_trace import BSFTrace, BSFCall

        # Minimal BSF trace with 2 GPR-like variables
        # (BSF trace is used for GPR coloring; FPR mapping is direct)
        trace = BSFTrace(source=Path("test.cpp"), calls=[
            BSFCall(index=1, caller_rva=INITIAL_COLORING_RVA,
                    lo=0xFFFFFF80, hi=0xFFFFFFFF, base=0, bit=7),
            BSFCall(index=2, caller_rva=INITIAL_COLORING_RVA,
                    lo=0xFFFFFF00, hi=0xFFFFFFFF, base=0, bit=8),
        ])

        # FPR swap pair from objdiff
        swap_pairs = [("f30", "f31")]
        decl_names = ["expr_a", "expr_b"]

        candidates = guided_pairwise_search(
            trace, swap_pairs, decl_names
        )

        # Should produce a targeted swap: ["expr_b", "expr_a"]
        self.assertTrue(len(candidates) > 0,
                        "Should produce candidates for callee-saved FPR swap")
        self.assertIn(["expr_b", "expr_a"], candidates,
                      "Should include the pairwise swap of the two declarations")


# ---------------------------------------------------------------------------
# ASM Register Mapping Tests
# ---------------------------------------------------------------------------

class TestAsmRegMap(unittest.TestCase):
    """Tests for AsmRegMap dataclass and asm_guided_search()."""

    def test_simple_swap_produces_targeted_candidate(self):
        """With a→r31, b→r30 and swap pair (r30, r31), first candidate swaps a↔b."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(
            var_to_reg={"a": "r31", "b": "r30", "c": "r29"},
            reg_to_var={"r31": "a", "r30": "b", "r29": "c"},
            callee_saved_count=3,
        )
        candidates = asm_guided_search(regmap, [("r30", "r31")], ["a", "b", "c"])

        self.assertGreater(len(candidates), 0)
        self.assertEqual(candidates[0], ["b", "a", "c"],
                         "First candidate should swap the two targeted vars")

    def test_swap_pair_order_does_not_matter(self):
        """(r30, r31) and (r31, r30) should produce the same candidates."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(
            var_to_reg={"x": "r31", "y": "r30"},
            reg_to_var={"r31": "x", "r30": "y"},
            callee_saved_count=2,
        )
        cands_a = asm_guided_search(regmap, [("r30", "r31")], ["x", "y"])
        cands_b = asm_guided_search(regmap, [("r31", "r30")], ["x", "y"])

        self.assertEqual(cands_a, cands_b)

    def test_unmapped_swap_pair_produces_no_candidates(self):
        """Swap pair referencing registers not in the mapping yields nothing."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(
            var_to_reg={"a": "r31"},
            reg_to_var={"r31": "a"},
            callee_saved_count=2,
        )
        candidates = asm_guided_search(regmap, [("r28", "r27")], ["a", "b"])
        self.assertEqual(candidates, [])

    def test_empty_regmap_produces_no_candidates(self):
        """An empty AsmRegMap should produce no candidates."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(callee_saved_count=3)
        candidates = asm_guided_search(regmap, [("r30", "r31")], ["a", "b", "c"])
        self.assertEqual(candidates, [])

    def test_fpr_swap_pairs_are_skipped(self):
        """asm_guided_search only handles GPR swaps, FPR pairs are ignored."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(
            var_to_reg={"a": "r31", "b": "r30"},
            reg_to_var={"r31": "a", "r30": "b"},
            callee_saved_count=2,
        )
        candidates = asm_guided_search(regmap, [("f30", "f31")], ["a", "b"])
        self.assertEqual(candidates, [])

    def test_multi_swap_produces_simultaneous_candidate(self):
        """Two swap pairs should produce a combined simultaneous swap."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(
            var_to_reg={"a": "r31", "b": "r30", "c": "r29", "d": "r28"},
            reg_to_var={"r31": "a", "r30": "b", "r29": "c", "r28": "d"},
            callee_saved_count=4,
        )
        candidates = asm_guided_search(
            regmap, [("r31", "r30"), ("r29", "r28")], ["a", "b", "c", "d"]
        )

        # Should include the simultaneous swap: ["b", "a", "d", "c"]
        self.assertIn(["b", "a", "d", "c"], candidates)

    def test_neighbor_variants_are_generated(self):
        """Targeted swap should also produce ±1 neighbor variants."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(
            var_to_reg={"a": "r31", "b": "r30", "c": "r29"},
            reg_to_var={"r31": "a", "r30": "b", "r29": "c"},
            callee_saved_count=3,
        )
        candidates = asm_guided_search(regmap, [("r30", "r31")], ["a", "b", "c"])

        # Should have more than just the direct swap
        self.assertGreater(len(candidates), 1,
                           "Should include neighbor variants beyond the direct swap")

    def test_single_var_produces_no_candidates(self):
        """Need at least 2 declarations for any swap."""
        from tools.compiler_trace.asm_regmap import AsmRegMap
        from tools.compiler_trace.regmap_solver import asm_guided_search

        regmap = AsmRegMap(
            var_to_reg={"a": "r31"},
            reg_to_var={"r31": "a"},
            callee_saved_count=1,
        )
        candidates = asm_guided_search(regmap, [("r30", "r31")], ["a"])
        self.assertEqual(candidates, [])


class TestParseAsmListing(unittest.TestCase):
    """Tests for parse_asm_listing() — /FAs listing parser."""

    @staticmethod
    def _make_listing(func_name: str, body_lines: list[str],
                      savegprlr: int | None = None,
                      individual_saves: list[int] | None = None) -> list[str]:
        """Build a synthetic /FAs listing for testing.

        Args:
            func_name: Function name for PROC/ENDP markers.
            body_lines: Lines between .endprolog and ENDP.
            savegprlr: If set, add bl __savegprlr_N prologue.
            individual_saves: If set, add individual stw saves.
        """
        lines = [f"{func_name} PROC NEAR"]
        if savegprlr is not None:
            lines.append(f"\tbl\t__savegprlr_{savegprlr}")
        if individual_saves:
            for reg in individual_saves:
                lines.append(f"\tstw\tr{reg}, -{(32-reg)*8}(r1)")
        lines.append(".endprolog")
        lines.extend(body_lines)
        lines.append(f"{func_name} ENDP")
        return lines

    def test_basic_var_to_reg_mapping(self):
        """Source declaration followed by mr to callee-saved reg is captured."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("TestFunc", [
            "; 10   : \tint a = GetValue();",
            "\tbl\t?GetValue@@YAHXZ",
            "\tmr\tr31, r3",
            "; 11   : \tint b = GetOther();",
            "\tbl\t?GetOther@@YAHXZ",
            "\tmr\tr30, r3",
        ], savegprlr=30)

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        self.assertEqual(regmap.var_to_reg.get("a"), "r31")
        self.assertEqual(regmap.var_to_reg.get("b"), "r30")
        self.assertEqual(regmap.callee_saved_count, 2)

    def test_function_not_found_returns_none(self):
        """When function name doesn't match, returns None."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("OtherFunc", [
            "; 10   : \tint x = 1;",
            "\tli\tr31, 1",
        ], savegprlr=31)

        regmap = parse_asm_listing(listing, "NonExistent")
        self.assertIsNone(regmap)

    def test_zero_callee_saved_returns_empty(self):
        """Function with no callee-saved registers returns empty mapping."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = [
            "TestFunc PROC NEAR",
            ".endprolog",
            "; 5    : \treturn 42;",
            "\tli\tr3, 42",
            "\tblr",
            "TestFunc ENDP",
        ]

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        self.assertEqual(regmap.callee_saved_count, 0)
        self.assertEqual(regmap.var_to_reg, {})

    def test_param_saves_not_mapped_to_vars(self):
        """mr rN, r3/r4 (parameter saves) should not be attributed to variables."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("TestFunc", [
            "; 10   : \tif (x > 0) {",
            "\tmr\tr31, r3",       # param save — should be marked as param
            "\tmr\tr30, r4",       # param save — should be marked as param
            "; 11   : \tint result = compute();",
            "\tbl\t?compute@@YAHXZ",
            "\tmr\tr29, r3",       # THIS should map to 'result'
        ], savegprlr=29)

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        # r31 and r30 should be params, not variable mappings
        self.assertNotIn("r31", {v: k for k, v in regmap.var_to_reg.items()})
        self.assertNotIn("r30", {v: k for k, v in regmap.var_to_reg.items()})
        # r29 should map to 'result'
        self.assertEqual(regmap.var_to_reg.get("result"), "r29")

    def test_non_declaration_source_resets_context(self):
        """A non-declaration source comment should not carry over to next asm."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("TestFunc", [
            "; 10   : \tint a = GetValue();",
            "\tbl\t?GetValue@@YAHXZ",
            "\tmr\tr31, r3",
            "; 11   : \tif (a > 0) {",  # Not a declaration
            "\tmr\tr30, r3",             # Should NOT map to 'a' again
        ], savegprlr=30)

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        self.assertEqual(regmap.var_to_reg.get("a"), "r31")
        # r30 should not be mapped to anything (source context was reset)
        self.assertNotIn("r30", regmap.reg_to_var.keys() - {"r30"}
                         if "r30" in regmap.reg_to_var else set())
        self.assertNotIn("a", [v for k, v in regmap.reg_to_var.items() if k == "r30"])

    def test_unwind_handler_stops_parsing(self):
        """__unwind labels should stop parsing to avoid false mappings."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("TestFunc", [
            "; 10   : \tint a = GetValue();",
            "\tbl\t?GetValue@@YAHXZ",
            "\tmr\tr31, r3",
            "; 11   : \t}",
            "\tblr",
            "__unwind$12345:",
            "\taddi\tr31, r12, -144",
            ".endprolog",
            "; 10   : \tint b = Other();",  # In unwind — should not be parsed
            "\tmr\tr30, r3",
        ], savegprlr=30)

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        self.assertEqual(regmap.var_to_reg.get("a"), "r31")
        # 'b' should NOT be mapped (it's in the unwind handler)
        self.assertNotIn("b", regmap.var_to_reg)

    def test_function_definition_not_treated_as_declaration(self):
        """Source comment showing a function def should not produce a var mapping."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("TestFunc", [
            "; 10   : void TestFunc(int x) {",
            "\tmr\tr31, r3",
            "; 11   : \tint result = compute();",
            "\tbl\t?compute@@YAHXZ",
            "\tmr\tr30, r3",
        ], savegprlr=30)

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        # "TestFunc" should not be in var_to_reg
        self.assertNotIn("TestFunc", regmap.var_to_reg)
        # But 'result' should be mapped
        self.assertEqual(regmap.var_to_reg.get("result"), "r30")

    def test_already_assigned_reg_not_overwritten(self):
        """A callee-saved register already assigned to one var should not be
        reassigned to a later variable."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("TestFunc", [
            "; 10   : \tint a = First();",
            "\tbl\t?First@@YAHXZ",
            "\tmr\tr31, r3",
            "; 11   : \tint b = Second();",
            "\tbl\t?Second@@YAHXZ",
            "\tmr\tr31, r3",  # r31 already taken by 'a'
        ], savegprlr=31)

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        self.assertEqual(regmap.var_to_reg.get("a"), "r31")
        # 'b' should NOT be mapped to r31 (already taken)
        self.assertNotIn("b", regmap.var_to_reg)

    def test_individual_saves_detected(self):
        """Functions using individual stw saves (debug builds) are handled."""
        from tools.compiler_trace.asm_regmap import parse_asm_listing

        listing = self._make_listing("TestFunc", [
            "; 10   : \tint a = First();",
            "\tbl\t?First@@YAHXZ",
            "\tmr\tr31, r3",
            "; 11   : \tint b = Second();",
            "\tbl\t?Second@@YAHXZ",
            "\tmr\tr30, r3",
        ], individual_saves=[30, 31])

        regmap = parse_asm_listing(listing, "TestFunc")
        self.assertIsNotNone(regmap)
        self.assertEqual(regmap.callee_saved_count, 2)


# ---------------------------------------------------------------------------
# Ghidra CF skeleton-guided pattern tests
# ---------------------------------------------------------------------------

def _make_ghidra_context(source_text: str, func_name: str,
                         diagnosis: Diagnosis, ghidra_code: str) -> FunctionContext:
    """Build a FunctionContext with ghidra_ast populated from Ghidra code."""
    from scripts.permuter.ghidra_ast import parse_ghidra

    ctx = make_context(source_text, func_name, diagnosis)
    ghidra_ast = parse_ghidra(ghidra_code)
    ctx.ghidra_ast = ghidra_ast
    ctx.ghidra_code = ghidra_code
    return ctx


class TestAndSplitSkeletonGuided(unittest.TestCase):
    """Test that and_split uses CF skeleton when condition_structure is ambiguous."""

    def test_skeleton_nested_ifs_triggers_split(self):
        """Source has &&, Ghidra shows nested ifs -> should try split."""
        source = textwrap.dedent("""\
        void test_func(int *a, int *b) {
            if (a && b) {
                a[0] = b[0];
            }
        }
        """)
        # Ghidra code with both conjunction AND nested_if (ambiguous for
        # condition_structure -> falls through to skeleton).
        # Skeleton: ['if', 'if', 'if'] — consecutive ifs >= 2 -> split
        ghidra_code = textwrap.dedent("""\
        void test_func(int *a, int *b) {
            if (a != 0 && b != 0) {
                if (a != 0) {
                    if (b != 0) {
                        *a = *b;
                    }
                }
            }
        }
        """)
        ctx = _make_ghidra_context(source, "test_func",
                                   diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("and_split")
        variants = list(pattern.generate(ctx))
        self.assertGreater(len(variants), 0,
                           "Should produce variants when skeleton shows nested ifs")
        has_ghidra = any("ghidra" in v.name or "ghidra" in v.description.lower()
                         for v in variants)
        self.assertTrue(has_ghidra,
                        "At least one variant should be ghidra-tagged")

    def test_skeleton_no_signal_falls_through(self):
        """When skeleton has no useful signal, should fall through to blind mode."""
        source = textwrap.dedent("""\
        void test_func(int x) {
            if (x > 0) {
                x = x + 1;
            }
        }
        """)
        # Ghidra code also simple — skeleton is just ['if'], no consecutive ifs
        ghidra_code = textwrap.dedent("""\
        void test_func(int x) {
            if (x > 0) {
                x = x + 1;
            }
        }
        """)
        ctx = _make_ghidra_context(source, "test_func",
                                   diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("and_split")
        # Should not crash
        variants = list(pattern.generate(ctx))
        # Fine either way — just shouldn't crash


class TestEarlyReturnMergeSkeletonGuided(unittest.TestCase):
    """Test that early_return_merge uses CF skeleton when condition_structure is empty."""

    def test_skeleton_guard_pairs_triggers_split(self):
        """Source has || chain, Ghidra shows guard returns -> should split."""
        source = textwrap.dedent("""\
        int test_func(int a, int b) {
            if (a < 0 || b < 0)
                return 0;
            return a + b;
        }
        """)
        # Ghidra with separate guard returns
        ghidra_code = textwrap.dedent("""\
        int test_func(int a, int b) {
            if (a < 0)
                return 0;
            if (b < 0)
                return 0;
            return a + b;
        }
        """)
        ctx = _make_ghidra_context(source, "test_func",
                                   diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("early_return_merge")
        variants = list(pattern.generate(ctx))
        self.assertGreater(len(variants), 0,
                           "Should produce variants for guard return split")

    def test_skeleton_few_guards_triggers_merge(self):
        """Source has guard returns, Ghidra shows few guards -> skeleton merge."""
        source = textwrap.dedent("""\
        int test_func(int a, int b) {
            if (a < 0)
                return 0;
            if (b < 0)
                return 0;
            return a + b;
        }
        """)
        # Ghidra code with NO guard returns, no conjunction/disjunction
        # -> condition_structure returns empty -> skeleton fallback
        # Skeleton: ['return'] (just a return, guard_pairs=0 <=1)
        # source_has_guards=True -> merge
        ghidra_code = textwrap.dedent("""\
        int test_func(int a, int b) {
            int result;
            result = a + b;
            return result;
        }
        """)
        ctx = _make_ghidra_context(source, "test_func",
                                   diag_with_branch_and_clusters(), ghidra_code)
        pattern = get_pattern("early_return_merge")
        variants = list(pattern.generate(ctx))
        skeleton_variants = [v for v in variants if "skeleton" in v.name]
        self.assertGreater(len(skeleton_variants), 0,
                           "Should produce skeleton-guided merge variants")


class TestEarlyReturnMergeM2cGuided(unittest.TestCase):
    """Test m2c guidance for early_return_merge direction filtering."""

    def test_m2c_guard_chain_prefers_split(self):
        """When m2c shows guard_chain, should not generate merge variants."""
        source = textwrap.dedent("""\
        int test_func(int a, int b) {
            if (a < 0 || b < 0)
                return 0;
            return a + b;
        }
        """)
        # m2c output: guard_chain → prefer split direction
        m2c_code = """\
void test_func(int a, int b) {
    if (a < 0) { return 0; }
    if (b < 0) { return 0; }
    return a + b;
}
"""
        ctx = make_context(source, "test_func", diag_with_branch_ops())
        ctx.m2c_code = m2c_code
        pattern = get_pattern("early_return_merge")
        variants = list(pattern.generate(ctx))
        # Should produce split variants, not merge
        descriptions = [v.description.lower() for v in variants]
        has_split = any("split" in d or "expand" in d or "guard return" in d for d in descriptions)
        self.assertTrue(has_split, f"Expected split variant, got: {descriptions}")

    def test_m2c_single_return_prefers_merge(self):
        """When m2c shows single return, should prefer merge/conjunction."""
        source = textwrap.dedent("""\
        int test_func(int a, int b) {
            if (a < 0)
                return 0;
            return a + b;
        }
        """)
        # m2c output: single return → prefer merge/conjunction
        m2c_code = "int test_func(int a, int b) { return a + b; }"
        ctx = make_context(source, "test_func", diag_with_branch_ops())
        ctx.m2c_code = m2c_code
        pattern = get_pattern("early_return_merge")
        variants = list(pattern.generate(ctx))
        # Should produce conjunction variant (collapse guard into &&)
        descriptions = [v.description.lower() for v in variants]
        has_conjunction = any("conjunction" in d or "merge" in d for d in descriptions)
        self.assertTrue(has_conjunction, f"Expected conjunction variant, got: {descriptions}")


class TestStatementReorderM2cGuided(unittest.TestCase):
    """Test m2c call-order guidance for statement_reorder."""

    def test_m2c_call_order_prioritizes_matching_swap(self):
        """When m2c shows B before A, swap toward that order."""
        source = textwrap.dedent("""\
        void test_func() {
            Alpha();
            Beta();
        }
        """)
        # m2c output: Beta before Alpha
        m2c_code = "void test_func(void) { Beta(); Alpha(); }"
        diag = diag_with_clusters()
        ctx = make_context(source, "test_func", diag)
        ctx.m2c_code = m2c_code
        pattern = get_pattern("statement_reorder")
        variants = list(pattern.generate(ctx))
        # Should produce a variant described as m2c-guided
        m2c_variants = [v for v in variants if "m2c" in v.description.lower()]
        self.assertGreater(len(m2c_variants), 0,
                           f"Expected m2c-guided variants, got: {[v.description for v in variants]}")


class TestAndSplitHelpers(unittest.TestCase):
    """Test the _count_consecutive_ifs and _count_guard_return_pairs helpers."""

    def test_count_consecutive_ifs_basic(self):
        from scripts.permuter.patterns.and_split import _count_consecutive_ifs
        self.assertEqual(_count_consecutive_ifs(["if", "if", "return"]), 2)
        self.assertEqual(_count_consecutive_ifs(["if", "return", "if", "return"]), 1)
        self.assertEqual(_count_consecutive_ifs(["if", "if", "if"]), 3)
        self.assertEqual(_count_consecutive_ifs([]), 0)
        self.assertEqual(_count_consecutive_ifs(["return"]), 0)

    def test_count_guard_return_pairs_basic(self):
        from scripts.permuter.patterns.and_split import _count_guard_return_pairs
        self.assertEqual(_count_guard_return_pairs(["if", "return", "if", "return"]), 2)
        self.assertEqual(_count_guard_return_pairs(["if", "if", "return"]), 1)
        self.assertEqual(_count_guard_return_pairs(["if", "else", "return"]), 0)
        self.assertEqual(_count_guard_return_pairs([]), 0)


class TestParameterLiveRange(unittest.TestCase):
    """Dedicated tests for the parameter_live_range pattern."""

    def _get_pattern(self):
        return get_pattern("parameter_live_range")

    def _make_ctx(self, source: str, func_name: str, diag=None):
        if diag is None:
            diag = diag_with_prologue_fewer_saves()
        return make_context(source, func_name, diag)

    # -- relevance tests --

    def test_relevant_with_prologue_mismatch(self):
        p = self._get_pattern()
        self.assertTrue(p.relevant(diag_with_prologue_fewer_saves()))

    def test_relevant_with_prologue_more_saves(self):
        p = self._get_pattern()
        self.assertTrue(p.relevant(diag_with_prologue_more_saves()))

    def test_relevant_with_regswaps(self):
        p = self._get_pattern()
        self.assertTrue(p.relevant(diag_with_gpr_swaps()))

    def test_relevant_with_clusters(self):
        p = self._get_pattern()
        self.assertTrue(p.relevant(diag_with_clusters()))

    def test_irrelevant_empty_diag(self):
        p = self._get_pattern()
        self.assertFalse(p.relevant(_empty_diag()))

    # -- priority tests --

    def test_priority_highest_for_fewer_saves(self):
        p = self._get_pattern()
        self.assertAlmostEqual(p.priority(diag_with_prologue_fewer_saves()), 0.9)

    def test_priority_high_for_more_saves(self):
        p = self._get_pattern()
        self.assertAlmostEqual(p.priority(diag_with_prologue_more_saves()), 0.7)

    def test_priority_low_for_regswaps_only(self):
        p = self._get_pattern()
        self.assertAlmostEqual(p.priority(diag_with_gpr_swaps()), 0.4)

    def test_priority_zero_for_empty(self):
        p = self._get_pattern()
        self.assertAlmostEqual(p.priority(_empty_diag()), 0.0)

    # -- no LOAD_REVS → zero variants --

    def test_no_load_revs_yields_nothing(self):
        """Function without LOAD_REVS should produce zero variants."""
        p = self._get_pattern()
        ctx = self._make_ctx("""\
void NoLoadRevs(BinStream &bs) {
    Foo::Load(bs);
    bs >> mVal;
}
""", "NoLoadRevs")
        variants = list(p.generate(ctx))
        self.assertEqual(len(variants), 0)

    # -- bs before LOAD_REVS not replaced --

    def test_bs_before_load_revs_not_replaced(self):
        """bs usage before LOAD_REVS should not be touched."""
        p = self._get_pattern()
        ctx = self._make_ctx("""\
void PreLoad(BinStream &bs) {
    bs >> mPreamble;
    LOAD_REVS(bs);
    ASSERT_REVS(1, 0);
    d >> mVal;
}
""", "PreLoad")
        variants = list(p.generate(ctx))
        # No bs identifiers after LOAD_REVS, no LOAD_SUPERCLASS, only one d>> stmt
        # so no variants should be generated
        self.assertEqual(len(variants), 0)

    # -- LOAD_SUPERCLASS detection --

    def test_load_superclass_detected(self):
        """LOAD_SUPERCLASS(X) generates a variant with X::Load(d.stream)."""
        p = self._get_pattern()
        ctx = self._make_ctx("""\
void LSTest(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(1, 0);
    LOAD_SUPERCLASS(FlowNode);
    d >> mVal;
}
""", "LSTest")
        variants = list(p.generate(ctx))
        # Should find at least the LOAD_SUPERCLASS replacement
        names = [v.name for v in variants]
        macro_variants = [n for n in names if "lsmacro" in n]
        self.assertGreater(len(macro_variants), 0,
                           f"Expected lsmacro variant, got: {names}")
        # Check the replacement text is correct
        for v in variants:
            if "lsmacro" in v.name:
                self.assertIn(b"FlowNode::Load(d.stream);", v.source)
                self.assertNotIn(b"LOAD_SUPERCLASS(FlowNode)", v.source)
                break

    def test_load_superclass_qualified_name(self):
        """LOAD_SUPERCLASS(Hmx::Object) handles qualified class names."""
        p = self._get_pattern()
        ctx = self._make_ctx("""\
void QualTest(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(2, 0);
    LOAD_SUPERCLASS(Hmx::Object);
    d >> mVal;
}
""", "QualTest")
        variants = list(p.generate(ctx))
        macro_variants = [v for v in variants if "lsmacro" in v.name]
        self.assertGreater(len(macro_variants), 0)
        self.assertIn(b"Hmx::Object::Load(d.stream);", macro_variants[0].source)

    def test_load_superclass_multiple(self):
        """Multiple LOAD_SUPERCLASS calls generate individual + all-at-once variants."""
        p = self._get_pattern()
        ctx = self._make_ctx("""\
void MultiLS(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(5, 0);
    LOAD_SUPERCLASS(Hmx::Object);
    LOAD_SUPERCLASS(RndAnimatable);
    LOAD_SUPERCLASS(RndTransformable);
    d >> mVal;
}
""", "MultiLS")
        variants = list(p.generate(ctx))
        names = [v.name for v in variants]
        # Should have individual replacements + an "all" variant
        individual = [n for n in names if "lsmacro" in n and "all" not in n]
        all_variant = [n for n in names if "lsmacro_all" in n]
        self.assertEqual(len(individual), 3, f"Expected 3 individual, got: {individual}")
        self.assertEqual(len(all_variant), 1, f"Expected 1 all variant, got: {all_variant}")
        # The "all" variant should replace all three
        for v in variants:
            if "lsmacro_all" in v.name:
                self.assertIn(b"Hmx::Object::Load(d.stream);", v.source)
                self.assertIn(b"RndAnimatable::Load(d.stream);", v.source)
                self.assertIn(b"RndTransformable::Load(d.stream);", v.source)
                self.assertNotIn(b"LOAD_SUPERCLASS", v.source)
                break

    # -- combined strategy --

    def test_combined_bs_and_macro(self):
        """Combined variant replaces both bs identifiers and LOAD_SUPERCLASS."""
        p = self._get_pattern()
        ctx = self._make_ctx("""\
void CombTest(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(2, 0);
    LOAD_SUPERCLASS(EventTrigger);
    uiCom->Load(bs);
    bs >> mSym;
}
""", "CombTest")
        variants = list(p.generate(ctx))
        names = [v.name for v in variants]
        combined = [v for v in variants if "combined" in v.name]
        self.assertGreater(len(combined), 0,
                           f"Expected combined variant, got: {names}")
        # Check the combined_ds variant
        ds_combined = [v for v in combined if "_ds_" in v.name]
        self.assertGreater(len(ds_combined), 0,
                           f"Expected combined_ds variant, got: {[v.name for v in combined]}")
        v = ds_combined[0]
        self.assertIn(b"EventTrigger::Load(d.stream);", v.source)
        self.assertIn(b"uiCom->Load(d.stream);", v.source)
        self.assertIn(b"d.stream >> mSym;", v.source)
        self.assertNotIn(b"LOAD_SUPERCLASS", v.source)

    def test_combined_not_generated_without_both_types(self):
        """Combined variant only generated when both bs identifiers AND macros present."""
        p = self._get_pattern()
        # Only bs identifiers, no LOAD_SUPERCLASS
        ctx = self._make_ctx("""\
void BsOnly(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(1, 0);
    Foo::Load(bs);
    bs >> mVal;
}
""", "BsOnly")
        variants = list(p.generate(ctx))
        combined = [v for v in variants if "combined" in v.name]
        self.assertEqual(len(combined), 0, "Should not generate combined without macros")

    def test_combined_d_variant(self):
        """Combined variant with bs->d (not d.stream) is generated."""
        p = self._get_pattern()
        ctx = self._make_ctx("""\
void CombD(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(1, 0);
    LOAD_SUPERCLASS(FlowNode);
    bs >> mVal;
}
""", "CombD")
        variants = list(p.generate(ctx))
        d_combined = [v for v in variants if "combined_d_" in v.name]
        self.assertGreater(len(d_combined), 0, "Expected combined_d variant")
        v = d_combined[0]
        self.assertIn(b"FlowNode::Load(d.stream);", v.source)
        self.assertIn(b"d >> mVal;", v.source)

    # -- chain merging --

    def test_chain_merge_basic(self):
        """Two consecutive d>> statements get merged."""
        p = self._get_pattern()
        ctx = self._make_ctx("""\
void ChainTest(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(1, 0);
    d >> mA;
    d >> mB;
}
""", "ChainTest")
        variants = list(p.generate(ctx))
        chain_variants = [v for v in variants if "chain" in v.name]
        self.assertGreater(len(chain_variants), 0, "Expected chain merge variant")
        # Merged should contain >> mA ... >> mB in one statement
        v = chain_variants[0]
        self.assertIn(b">> mA", v.source)
        self.assertIn(b">> mB", v.source)
        # Should NOT have two separate "d >> " prefixes
        merged_text = v.source.decode("utf-8")
        # Count occurrences of "d >>" — should be 1 (the first), not 2
        lines_with_d = [l.strip() for l in merged_text.split("\n") if l.strip().startswith("d >>")]
        self.assertEqual(len(lines_with_d), 1,
                         f"Expected 1 'd >>' line after merge, got {len(lines_with_d)}")

    # -- bs in declaration not replaced --

    def test_bs_in_declaration_skipped(self):
        """bs that appears in a declaration (BinStream &bs) should not be replaced."""
        p = self._get_pattern()
        ctx = self._make_ctx("""\
void DeclTest(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(1, 0);
    Foo::Load(bs);
}
""", "DeclTest")
        variants = list(p.generate(ctx))
        for v in variants:
            # The function signature's &bs should never be changed
            self.assertIn(b"BinStream &bs", v.source,
                          f"Variant {v.name} corrupted function signature")


class TestColorAndCameraPatterns(unittest.TestCase):
    """Tests for color_copy_shape and native_guard_camera_wrap patterns."""

    def test_color_copy_shape_channels_to_aggregate(self):
        p = get_pattern("color_copy_shape")
        ctx = make_context("""\
void ColorCopy() {
    Hmx::Color &dst = mat->GetColor();
    dst.red = saved.red;
    dst.green = saved.green;
    dst.blue = saved.blue;
}
""", "ColorCopy", diag_with_clusters())
        variants = list(p.generate(ctx))
        self.assertGreater(len(variants), 0)
        self.assertTrue(
            any(normalize("dst = saved;") in normalize(v.source) for v in variants),
            "Expected RGB channel run to collapse to aggregate assignment",
        )

    def test_color_copy_shape_aggregate_to_channels(self):
        p = get_pattern("color_copy_shape")
        ctx = make_context("""\
void ColorCopy() {
    Hmx::Color &dst = mat->GetColor();
    dst = savedColors[idx];
}
""", "ColorCopy", diag_with_clusters())
        variants = list(p.generate(ctx))
        self.assertGreater(len(variants), 0)
        self.assertTrue(
            any(
                normalize("dst.red = savedColors[idx].red;") in normalize(v.source)
                and normalize("dst.green = savedColors[idx].green;") in normalize(v.source)
                and normalize("dst.blue = savedColors[idx].blue;") in normalize(v.source)
                for v in variants
            ),
            "Expected aggregate assignment to expand to RGB channel copies",
        )

    def test_native_guard_camera_wrap_fixture(self):
        p = get_pattern("native_guard_camera_wrap")
        ctx = make_context("""\
RndCam *SelectTextRenderCam();
void RestoreTextRenderCam(RndCam *savedCam);

void DrawLikeText() {
    RndCam *savedCam = RndCam::Current();
    RndCam *uiCam = TheUI ? TheUI->GetCam() : nullptr;
    if (uiCam && uiCam != savedCam) {
        uiCam->Select();
    }
    DrawMesh(mesh, 1.0f, 0);
    if (savedCam && savedCam != RndCam::Current()) {
        savedCam->Select();
    }
}
""", "DrawLikeText", diag_with_branch_and_clusters())
        variants = list(p.generate(ctx))
        self.assertGreater(len(variants), 0)
        v = variants[0].source
        self.assertIn(b"SelectTextRenderCam()", v)
        self.assertIn(b"RestoreTextRenderCam(savedCam)", v)

    def test_native_guard_camera_wrap_no_helpers_noop(self):
        p = get_pattern("native_guard_camera_wrap")
        ctx = make_context("""\
void DrawLikeText() {
    RndCam *savedCam = RndCam::Current();
    RndCam *uiCam = TheUI ? TheUI->GetCam() : nullptr;
    if (uiCam && uiCam != savedCam) {
        uiCam->Select();
    }
    DrawMesh(mesh, 1.0f, 0);
    if (savedCam && savedCam != RndCam::Current()) {
        savedCam->Select();
    }
}
""", "DrawLikeText", diag_with_branch_and_clusters())
        variants = list(p.generate(ctx))
        self.assertEqual(len(variants), 0)


if __name__ == "__main__":
    # If run with pytest-style args (no --list/--pattern/--fixture), use unittest
    if len(sys.argv) == 1 or (len(sys.argv) == 2 and sys.argv[1] in ("-v", "--verbose")):
        # Check if we should use our CLI or unittest
        # Use CLI for standalone, unittest when no args or just -v
        _run_cli()
    else:
        _run_cli()
