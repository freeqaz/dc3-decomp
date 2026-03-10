"""Pattern fixture definitions — 100 PatternFixture instances.

Pure data file, no test logic. Used by test_patterns.py for dynamic test generation.
"""

from __future__ import annotations

import sys
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.types import Diagnosis, DiffOp

from scripts.permuter.tests.conftest import (
    PatternFixture,
    diag_always,
    diag_with_arith_ops,
    diag_with_bl_mismatch,
    diag_with_bool_materialization,
    diag_with_branch_and_clusters,
    diag_with_branch_ops,
    diag_with_callee_saved_swaps,
    diag_with_clusters,
    diag_with_cmp_ops,
    diag_with_cmplwi_cmpwi,
    diag_with_dead_code,
    diag_with_divw,
    diag_with_divw_base,
    diag_with_fma_addsub_ops,
    diag_with_fma_ops,
    diag_with_fneg_frsp,
    diag_with_gpr_fpr_conflict,
    diag_with_gpr_swaps,
    diag_with_insert_delete,
    diag_with_large_clusters,
    diag_with_lfd_lfs,
    diag_with_lwz_ops,
    diag_with_noise,
    diag_with_offset_deltas,
    diag_with_prologue_fewer_saves,
    diag_with_prologue_more_saves,
    diag_with_replace_real,
    diag_with_store_load_ops,
    diag_with_cntlzw,
    diag_with_cntlzw_dot,
    diag_with_nor,
    diag_with_subf_cmpw,
)


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
        description="Add .Str() to obj->Name() in MILO_WARN",
        func_name="test_func",
        diagnosis=diag_with_bl_mismatch(),
        seeded_source="""\
void test_func(Hmx::Object *obj) {
    MILO_WARN("bad obj %s", obj->Name());
}
""",
        expected_source="""\
void test_func(Hmx::Object *obj) {
    MILO_WARN("bad obj %s", obj->Name().Str());
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
    MILO_NOTIFY("%s %s %s", PathName(this), ClassName(), Name());
}
""",
        expected_source="""\
void test_func() {
    MILO_NOTIFY("%s %s %s", PathName(this), ClassName().Str(), Name().Str());
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
    MILO_NOTIFY("%s %s", ClassName().Str(), Name());
}
""",
        expected_source="""\
void test_func() {
    MILO_NOTIFY("%s %s", ClassName().Str(), Name().Str());
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
        description="Drop leading && operand: if (!ptr && other) -> if (!ptr)",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        match_mode="contains",
        seeded_source="""\
void test_func(int sHamMaster) {
    if (!TheMetaMusic && sHamMaster) {
        TheMetaMusic = CreateMusic();
    }
}
""",
        expected_source="""\
if (!TheMetaMusic) {
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

    # ===================== bool_cast (comparison extraction) =====================

    PatternFixture(
        id="bool_cast_comparison_extraction",
        pattern_name="bool_cast",
        description="Extract comparison in if-condition to bool local",
        func_name="test_func",
        diagnosis=diag_with_replace_real(),
        match_mode="contains",
        seeded_source="""\
void test_func(DataArray* da) {
    if (da->Size() > 1) {
        do_something();
    }
}
""",
        expected_source="""\
    bool _cond = da->Size() > 1;
    if (_cond)
""",
    ),

    # ===================== switch_if_convert =====================

    PatternFixture(
        id="switch_if_convert_if_chain_to_switch",
        pattern_name="switch_if_convert",
        description="Convert equality if/else-if chain into switch",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
void test_func(int state) {
    if (state == 0) {
        do_a();
    } else if (state == 1) {
        do_b();
    } else if (state == 2) {
        do_c();
    } else {
        do_d();
    }
}
""",
        expected_source="""\
void test_func(int state) {
    switch (state) {
    case 0:
        do_a();
        break;
    case 1:
        do_b();
        break;
    case 2:
        do_c();
        break;
    default:
        do_d();
        break;
    }
}
""",
    ),

    PatternFixture(
        id="switch_if_convert_infers_less_than_case",
        pattern_name="switch_if_convert",
        description="Infer a dense missing case from an i < N branch",
        func_name="test_func",
        diagnosis=diag_with_branch_and_clusters(),
        seeded_source="""\
void test_func(unsigned int i) {
    if (i == 0) {
        do_a();
    } else if (i == 1) {
        do_b();
    } else if (i < 3) {
        do_c();
    } else if (i == 3) {
        do_d();
    } else {
        do_e();
    }
}
""",
        expected_source="""\
void test_func(unsigned int i) {
    switch (i) {
    case 0:
        do_a();
        break;
    case 1:
        do_b();
        break;
    case 2:
        do_c();
        break;
    case 3:
        do_d();
        break;
    default:
        do_e();
        break;
    }
}
""",
    ),

    PatternFixture(
        id="switch_if_convert_handles_casts_and_reversed_equality",
        pattern_name="switch_if_convert",
        description="Treat casted and reversed comparisons as the same switch variable",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
void test_func(unsigned int i) {
    if ((unsigned int)i == 0) {
        do_a();
    } else if (1 == i) {
        do_b();
    } else if (i == 2) {
        do_c();
    } else {
        do_d();
    }
}
""",
        expected_source="""\
void test_func(unsigned int i) {
    switch ((unsigned int)i) {
    case 0:
        do_a();
        break;
    case 1:
        do_b();
        break;
    case 2:
        do_c();
        break;
    default:
        do_d();
        break;
    }
}
""",
    ),

    PatternFixture(
        id="switch_if_convert_switch_to_if_chain",
        pattern_name="switch_if_convert",
        description="Convert switch dispatch back into if/else-if chain",
        func_name="test_func",
        diagnosis=diag_with_branch_ops(),
        seeded_source="""\
void test_func(int state) {
    switch (state) {
    case 0:
        do_a();
        break;
    case 1:
        do_b();
        break;
    default:
        do_c();
        break;
    }
}
""",
        expected_source="""\
void test_func(int state) {
    if (state == 0) {
        do_a();
    } else if (state == 1) {
        do_b();
    } else {
        do_c();
    }
}
""",
    ),

    # ===================== byte_mask_extraction =====================

    PatternFixture(
        id="byte_mask_extraction_u8",
        pattern_name="byte_mask_extraction",
        description="Extract u8() byte mask to local variable in bitwise expr",
        func_name="test_func",
        diagnosis=Diagnosis(
            total_instructions=100,
            match_counts={"match": 90, "mismatch": 10},
            reg_swap_pairs={},
            offset_deltas={},
            diff_ops=[DiffOp(index=5, target_opcode="rlwimi", base_opcode="slwi")],
            clusters=[],
            noise_explained=0,
            noise_total=0,
        ),
        match_mode="contains",
        seeded_source="""\
void test_func(unsigned long w) {
    unsigned long ret = u8(w) | (u8(w) << 8);
}
""",
        expected_source="""\
    unsigned long _bm = u8(w);
    unsigned long ret = _bm | (_bm << 8);
""",
    ),

    # ===================== condition_arithmetic =====================

    PatternFixture(
        id="condition_arithmetic__neq_zero_to_implicit",
        pattern_name="condition_arithmetic",
        description="if (x != 0) -> if (x)",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
void test_func(int x) {
    if (x != 0) {
        do_thing();
    }
}
""",
        expected_source="""\
void test_func(int x) {
    if (x) {
        do_thing();
    }
}
""",
    ),

    PatternFixture(
        id="condition_arithmetic__eq_zero_to_not",
        pattern_name="condition_arithmetic",
        description="if (x == 0) -> if (!x)",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
void test_func(int x) {
    if (x == 0) {
        do_thing();
    }
}
""",
        expected_source="""\
void test_func(int x) {
    if (!x) {
        do_thing();
    }
}
""",
    ),

    PatternFixture(
        id="condition_arithmetic__implicit_to_neq_zero",
        pattern_name="condition_arithmetic",
        description="if (x) -> if (x != 0)",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
void test_func(int x) {
    if (x) {
        do_thing();
    }
}
""",
        expected_source="""\
void test_func(int x) {
    if (x != 0) {
        do_thing();
    }
}
""",
    ),

    PatternFixture(
        id="condition_arithmetic__neq_literal_to_subtract",
        pattern_name="condition_arithmetic",
        description="if (k != 1) -> if (k - 1)",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
void test_func(int k) {
    if (k != 1) {
        do_thing();
    }
}
""",
        expected_source="""\
void test_func(int k) {
    if (k - 1) {
        do_thing();
    }
}
""",
    ),

    PatternFixture(
        id="condition_arithmetic__eq_literal_to_not_subtract",
        pattern_name="condition_arithmetic",
        description="if (k == 1) -> if (!(k - 1))",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
void test_func(int k) {
    if (k == 1) {
        do_thing();
    }
}
""",
        expected_source="""\
void test_func(int k) {
    if (!(k - 1)) {
        do_thing();
    }
}
""",
    ),

    PatternFixture(
        id="condition_arithmetic__subtract_to_neq",
        pattern_name="condition_arithmetic",
        description="if (k - 1) -> if (k != 1)",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
void test_func(int k) {
    if (k - 1) {
        do_thing();
    }
}
""",
        expected_source="""\
void test_func(int k) {
    if (k != 1) {
        do_thing();
    }
}
""",
    ),

    PatternFixture(
        id="condition_arithmetic__bool_subscript",
        pattern_name="condition_arithmetic",
        description="arr[side == 0] -> arr[1 - side]",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
void test_func(int side, int* arr) {
    int val = arr[side == 0];
}
""",
        expected_source="""\
void test_func(int side, int* arr) {
    int val = arr[1 - side];
}
""",
    ),

    # --- return expression transforms ---

    PatternFixture(
        id="condition_arithmetic__return_eq_literal",
        pattern_name="condition_arithmetic",
        description="return state == 2 -> return !(state - 2)",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
bool test_func(int state) {
    return state == 2;
}
""",
        expected_source="""\
bool test_func(int state) {
    return !(state - 2);
}
""",
    ),

    PatternFixture(
        id="condition_arithmetic__return_neq_zero",
        pattern_name="condition_arithmetic",
        description="return mSignature != 0 -> return mSignature",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
bool test_func(int mSignature) {
    return mSignature != 0;
}
""",
        expected_source="""\
bool test_func(int mSignature) {
    return mSignature;
}
""",
    ),

    PatternFixture(
        id="condition_arithmetic__return_neq_literal",
        pattern_name="condition_arithmetic",
        description="return mState != 2 -> return mState - 2",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
bool test_func(int mState) {
    return mState != 2;
}
""",
        expected_source="""\
bool test_func(int mState) {
    return mState - 2;
}
""",
    ),

    # --- while/for condition transforms ---

    PatternFixture(
        id="condition_arithmetic__while_implicit_to_neq",
        pattern_name="condition_arithmetic",
        description="while (state) -> while (state != 0)",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
void test_func(int state) {
    while (state) {
        state--;
    }
}
""",
        expected_source="""\
void test_func(int state) {
    while (state != 0) {
        state--;
    }
}
""",
    ),

    PatternFixture(
        id="condition_arithmetic__for_neq_to_subtract",
        pattern_name="condition_arithmetic",
        description="for condition: k != 2 -> k - 2",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
void test_func() {
    for (int k = 0; k != 2; k++) {
        do_thing();
    }
}
""",
        expected_source="""\
void test_func() {
    for (int k = 0; k - 2; k++) {
        do_thing();
    }
}
""",
    ),

    # --- subscript !x transforms ---

    PatternFixture(
        id="condition_arithmetic__subscript_not_to_eq_zero",
        pattern_name="condition_arithmetic",
        description="arr[!side] -> arr[side == 0]",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
void test_func(int side, int* arr) {
    int val = arr[!side];
}
""",
        expected_source="""\
void test_func(int side, int* arr) {
    int val = arr[side == 0];
}
""",
    ),

    PatternFixture(
        id="condition_arithmetic__subscript_not_to_1_minus",
        pattern_name="condition_arithmetic",
        description="arr[!side] -> arr[1 - side]",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
void test_func(int side, int* arr) {
    int val = arr[!side];
}
""",
        expected_source="""\
void test_func(int side, int* arr) {
    int val = arr[1 - side];
}
""",
    ),

    # --- !(x - N) also yields !expr -> == 0 ---

    PatternFixture(
        id="condition_arithmetic__not_subtract_to_eq",
        pattern_name="condition_arithmetic",
        description="if (!(k - 1)) -> if (k == 1)",
        func_name="test_func",
        diagnosis=diag_with_cntlzw(),
        seeded_source="""\
void test_func(int k) {
    if (!(k - 1)) {
        do_thing();
    }
}
""",
        expected_source="""\
void test_func(int k) {
    if (k == 1) {
        do_thing();
    }
}
""",
    ),

    # --- dot-suffixed opcode matching (extrwi. / rlwinm.) ---

    PatternFixture(
        id="condition_arithmetic__dot_suffix_opcode",
        pattern_name="condition_arithmetic",
        description="Detects extrwi. (dot-suffixed) as strong signal",
        func_name="test_func",
        diagnosis=diag_with_cntlzw_dot(),
        seeded_source="""\
bool test_func(int state) {
    return state == 3;
}
""",
        expected_source="""\
bool test_func(int state) {
    return !(state - 3);
}
""",
    ),

    # ===================== nor_prevention =====================

    PatternFixture(
        id="nor_prevention_u8_xor",
        pattern_name="nor_prevention",
        description="Widen u8 cast to u32 before XOR to prevent NOR peephole",
        func_name="test_func",
        diagnosis=diag_with_nor(),
        match_mode="contains",
        seeded_source="""\
void test_func(u8 w) {
    u32 tmp = (u8)(w >> 3) ^ 0x1F;
}
""",
        expected_source="""\
    u32 _w32 = w;
""",
    ),

    # ===================== bool_materialize =====================

    PatternFixture(
        id="boolmat_and_to_bool_cast",
        pattern_name="bool_materialize",
        description="Add (bool) cast to && RHS comparison (triggers subfc/eqv branchless)",
        func_name="test_func",
        diagnosis=diag_with_bool_materialization(),
        match_mode="contains",
        seeded_source="""\
void test_func(bool a, int x) {
    if (a && x > 1) {
        do_stuff();
    }
}
""",
        expected_source="""\
(bool)(x > 1)
""",
    ),

    PatternFixture(
        id="boolmat_and_to_bitwise",
        pattern_name="bool_materialize",
        description="Swap && to & for fully branchless boolean",
        func_name="test_func",
        diagnosis=diag_with_bool_materialization(),
        match_mode="contains",
        seeded_source="""\
void test_func(bool a, int x) {
    if (a && x > 1) {
        do_stuff();
    }
}
""",
        expected_source="""\
a & (x > 1)
""",
    ),

    PatternFixture(
        id="boolmat_bitwise_to_and",
        pattern_name="bool_materialize",
        description="Swap & to && (add short-circuit)",
        func_name="test_func",
        diagnosis=diag_with_bool_materialization(),
        match_mode="contains",
        seeded_source="""\
void test_func(bool a, int x) {
    if (a & (x > 1)) {
        do_stuff();
    }
}
""",
        expected_source="""\
a && x > 1
""",
    ),

    # ===================== type_width_change =====================

    PatternFixture(
        id="typewidth_int_to_uchar",
        pattern_name="type_width_change",
        description="Narrow int to unsigned int (first transition)",
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

    # ===================== float_literal_pressure =====================

    PatternFixture(
        id="fltpres_inline_to_static",
        pattern_name="float_literal_pressure",
        description="Extract repeated float literal to static const (GPR addr cache)",
        func_name="test_func",
        diagnosis=diag_with_gpr_fpr_conflict(),
        match_mode="contains",
        seeded_source="""\
void test_func(float* out, float x) {
    if (x > 100.0f) x = 100.0f;
    call();
    if (x > 100.0f) x = 100.0f;
    *out = x;
}
""",
        expected_source="""\
static const float
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
        id="loopsub_for_ge_to_subtract",
        pattern_name="loop_condition_subtract",
        description="for (... ; i >= limit; ...) -> for (... ; i - limit >= 0; ...)",
        func_name="test_func",
        diagnosis=diag_with_subf_cmpw(),
        match_mode="contains",
        seeded_source="""\
void test_func(int* arr, int n) {
    for (int i = n; i >= 0; i--) {
        arr[i] = 0;
    }
}
""",
        expected_source="""\
i - 0 >= 0
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
]

# Build lookup by ID
_FIXTURE_MAP: dict[str, PatternFixture] = {f.id: f for f in FIXTURES}
