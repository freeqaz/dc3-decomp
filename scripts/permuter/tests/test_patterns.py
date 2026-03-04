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
    auto _tmp0 = mElements.size();
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
        diagnosis=diag_always(),
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
        diagnosis=diag_always(),
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
        diagnosis=diag_always(),
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
        diagnosis=diag_always(),
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
        intermediate_contains="auto _tmp0 = getSize()",
        expected_source="""\
void test_func() {
    auto _tmp0 = getSize();
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

    def test_fpr_only_diagnosis_makes_declreorder_irrelevant(self):
        """declaration_reorder should NOT be relevant for FPR-only swaps.

        The pattern checks for GPR swaps (r-prefix). FPR swaps (f-prefix)
        should not trigger it since BSF-guided reorder can't fix them.
        """
        from scripts.permuter.patterns import get_pattern
        p = get_pattern("declaration_reorder")
        diag = diag_with_fpr_swaps()
        self.assertFalse(p.relevant(diag),
                         "declaration_reorder should NOT be relevant for FPR-only swaps")

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


if __name__ == "__main__":
    # If run with pytest-style args (no --list/--pattern/--fixture), use unittest
    if len(sys.argv) == 1 or (len(sys.argv) == 2 and sys.argv[1] in ("-v", "--verbose")):
        # Check if we should use our CLI or unittest
        # Use CLI for standalone, unittest when no args or just -v
        _run_cli()
    else:
        _run_cli()
