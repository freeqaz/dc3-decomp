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


if __name__ == "__main__":
    # If run with pytest-style args (no --list/--pattern/--fixture), use unittest
    if len(sys.argv) == 1 or (len(sys.argv) == 2 and sys.argv[1] in ("-v", "--verbose")):
        # Check if we should use our CLI or unittest
        # Use CLI for standalone, unittest when no args or just -v
        _run_cli()
    else:
        _run_cli()
