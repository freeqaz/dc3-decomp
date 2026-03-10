"""Shared test infrastructure for pattern benchmark tests.

Provides PatternFixture/ComposedFixture dataclasses, helper functions,
and diagnosis factory functions used across all test_pattern_*.py files.
"""

from __future__ import annotations

import re
import sys
import textwrap
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


def make_ghidra_context(source_text, func_name, diagnosis, ghidra_code):
    """Make a FunctionContext with ghidra_ast populated."""
    from scripts.permuter.ghidra_ast import parse_ghidra
    ctx = make_context(source_text, func_name, diagnosis)
    ctx.ghidra_ast = parse_ghidra(ghidra_code)
    ctx.ghidra_code = ghidra_code
    return ctx


def _similarity(a: str, b: str) -> float:
    """Simple character-level similarity ratio."""
    if not a and not b:
        return 1.0
    common = sum(1 for ca, cb in zip(a, b) if ca == cb)
    return common / max(len(a), len(b))


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


def diag_with_subf_cmpw() -> Diagnosis:
    """Target uses subf. (subtract-and-record), base uses cmpw (direct compare)."""
    d = _empty_diag()
    d.diff_ops = [
        DiffOp(index=18, target_opcode="subf.", base_opcode="cmpw"),
        DiffOp(index=19, target_opcode="bge", base_opcode="bge"),
    ]
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


def diag_with_cntlzw() -> Diagnosis:
    """cntlzw/extrwi mismatch (arithmetic boolean vs comparison branch)."""
    d = _empty_diag()
    d.diff_ops = [
        DiffOp(index=5, target_opcode="subi", base_opcode="cmpwi"),
        DiffOp(index=6, target_opcode="cntlzw", base_opcode="beq"),
        DiffOp(index=7, target_opcode="extrwi", base_opcode=""),
    ]
    return d


def diag_with_cntlzw_dot() -> Diagnosis:
    """extrwi./rlwinm. mismatch — dot-suffixed opcodes (record bit set)."""
    d = _empty_diag()
    d.diff_ops = [
        DiffOp(index=5, target_opcode="extrwi.", base_opcode="rlwinm."),
    ]
    d.replace_real = 1
    return d


def diag_with_nor() -> Diagnosis:
    """nor vs xori mismatch (NOR peephole on narrow-type XOR)."""
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=5, target_opcode="xori", base_opcode="nor")]
    return d


def diag_with_insert_delete() -> Diagnosis:
    """Insert/delete cluster (MILO log swap)."""
    d = _empty_diag()
    d.insert_count = 3
    d.delete_count = 2
    d.clusters = [Cluster(start_idx=5, end_idx=10, size=5, inserts=3, deletes=2)]
    return d


def diag_with_bool_materialization() -> Diagnosis:
    """Boolean materialization sequences (subfc/eqv/addze) in target deletes."""
    d = _empty_diag()
    d.bool_materialization_sequences = 1
    d.clusters = [Cluster(start_idx=13, end_idx=17, size=5, inserts=0, deletes=5)]
    return d


def diag_with_member_ref_bind_swaps() -> Diagnosis:
    """GPR swaps suggesting member-ref-bind optimization."""
    d = _empty_diag()
    d.reg_swap_pairs = {
        ("r20", "r21"): SwapInfo(count=4, first_idx=10, last_idx=50)
    }
    return d


def diag_with_gpr_fpr_conflict() -> Diagnosis:
    """GPR-FPR type conflict (opposite-sign save deltas)."""
    d = _empty_diag()
    d.target_gpr_saves = 3
    d.base_gpr_saves = 2
    d.target_fpr_saves = 2
    d.base_fpr_saves = 3
    d.has_gpr_fpr_type_conflict = True
    return d


# ---------------------------------------------------------------------------
# member_ref_bind fixtures
# ---------------------------------------------------------------------------

MEMBER_REF_BIND_FIXTURES: list[PatternFixture] = [
    PatternFixture(
        id="membind_member_to_ref",
        pattern_name="member_ref_bind",
        description="Bind repeated member access mCount to auto& reference",
        seeded_source="""\
class Foo {
    int mCount;
    void test_func() {
        if (mCount > 0) {
            mCount++;
            mCount *= 2;
            mCount--;
        }
    }
};
""",
        expected_source="""\
class Foo {
    int mCount;
    void test_func() {
        auto& _ref0 = mCount;
        if (_ref0 > 0) {
            _ref0++;
            _ref0 *= 2;
            _ref0--;
        }
    }
};
""",
        func_name="test_func",
        diagnosis=diag_with_gpr_swaps(),
        match_mode="contains",
    ),

    PatternFixture(
        id="membind_param_to_ref",
        pattern_name="member_ref_bind",
        description="Bind reference parameter v to const auto& local",
        seeded_source="""\
struct Vec { float x, y, z; };
float dot(const Vec& a, const Vec& b);
float test_func(const Vec& v) {
    return dot(v, v);
}
""",
        expected_source="""\
struct Vec { float x, y, z; };
float dot(const Vec& a, const Vec& b);
float test_func(const Vec& v) {
    const auto& _ref0 = v;
    return dot(_ref0, _ref0);
}
""",
        func_name="test_func",
        diagnosis=diag_with_gpr_swaps(),
        match_mode="contains",
    ),
]
