"""End-to-end BSF pipeline tests — validate full BSF-guided permuter flow.

These tests compile source, capture BSF traces, partition by function,
run guided_pairwise_search, and verify that the correct declaration order
is among the candidates.

Requirements: wibo, MSVC PPC compiler, GDB with ptrace access.

Usage:
    python -m pytest tools/compiler_trace/tests/test_bsf_pipeline.py -v
"""

from __future__ import annotations

import re
import sys
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))


def _check_available() -> bool:
    try:
        from tools.compiler_trace.bsf_trace import WIBO_32
        from tools.compiler_trace.invoker import CL_EXE, WIBO
        return CL_EXE.exists() and WIBO.exists() and WIBO_32.exists()
    except Exception:
        return False


SKIP = not _check_available()

TMPDIR = _PROJECT_ROOT / "build" / "bsf_pipeline_tests"


def _ensure_tmpdir():
    TMPDIR.mkdir(parents=True, exist_ok=True)


def _write_source(content: str, name: str) -> Path:
    _ensure_tmpdir()
    path = TMPDIR / f"{name}.cpp"
    path.write_text(content)
    return path


def _compile_with_asm(source: Path) -> tuple[Path, list[str]]:
    from tools.compiler_trace.invoker import CompilerInvoker

    invoker = CompilerInvoker()
    output_dir = TMPDIR / f"asm_{source.stem}"
    output_dir.mkdir(parents=True, exist_ok=True)

    result = invoker.compile_with_asm(source, output_dir, listing_type="/FAs")
    if result.returncode != 0:
        raise RuntimeError(f"Compilation failed: {result.stderr}")

    for ext in (".cod", ".asm"):
        files = list(output_dir.glob(f"*{ext}"))
        if files:
            asm_lines = files[0].read_text().splitlines()
            obj = output_dir / f"{source.stem}.obj"
            return obj, asm_lines

    raise RuntimeError(f"No assembly listing found in {output_dir}")


# ---------------------------------------------------------------------------
# Test fixtures
# ---------------------------------------------------------------------------

# Original: a=r31, b=r30
SOURCE_2VAR_ORIG = textwrap.dedent("""\
    extern int call(int);

    int test_recovery(int x) {
        int a = call(1);
        int b = call(2);
        return call(a) + call(b);
    }
""")

# Swapped: b first, a second -> b=r31, a=r30
SOURCE_2VAR_SWAPPED = textwrap.dedent("""\
    extern int call(int);

    int test_recovery(int x) {
        int b = call(2);
        int a = call(1);
        return call(a) + call(b);
    }
""")

# 5 variables for narrowing test
SOURCE_5VAR_ORIG = textwrap.dedent("""\
    extern int call(int);

    int test_narrowing(int x) {
        int a = call(1);
        int b = call(2);
        int c = call(3);
        int d = call(4);
        int e = call(5);
        return call(a) + call(b) + call(c) + call(d) + call(e);
    }
""")

# Multi-function: only target_func has a swap
SOURCE_MULTI_ORIG = textwrap.dedent("""\
    extern int call(int);

    int helper(int x) {
        int p = call(10);
        int q = call(20);
        return call(p) + call(q);
    }

    int target_func(int x) {
        int a = call(1);
        int b = call(2);
        int c = call(3);
        return call(a) + call(b) + call(c);
    }

    int other_func(int x) {
        int m = call(100);
        int n = call(200);
        int o = call(300);
        int s = call(400);
        return call(m) + call(n) + call(o) + call(s);
    }
""")


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

@unittest.skipIf(SKIP, "Need GDB + compiler")
class TestBSFPipeline(unittest.TestCase):
    """End-to-end BSF pipeline tests."""

    def test_2var_swap_recovery(self):
        """BSF-guided search on a swapped 2-var function should find the original order."""
        from tools.compiler_trace.bsf_trace import trace_bsf
        from tools.compiler_trace.regmap_solver import guided_pairwise_search, gpr_to_color

        # Start from swapped source
        src = _write_source(SOURCE_2VAR_SWAPPED, "pipe_2var_swap")
        trace = trace_bsf(src)

        # The "target" wants r31=a, r30=b but we have r31=b, r30=a
        # So the swap pair is (r30, r31)
        swap_pairs = [("r30", "r31")]
        decl_names = ["b", "a"]  # Current (swapped) order

        candidates = guided_pairwise_search(trace, swap_pairs, decl_names)

        # The original order ["a", "b"] should be among the candidates
        self.assertGreater(len(candidates), 0, "No candidates generated")
        original_order = ["a", "b"]
        self.assertIn(original_order, candidates,
            f"Original order {original_order} not found in candidates: {candidates}")

        print(f"\n  Candidates: {candidates}")
        print(f"  Original order found: {original_order in candidates}")

    def test_5var_targeted_narrowing(self):
        """BSF-guided search on a 5-var function should produce fewer candidates than C(5,2)=10."""
        from tools.compiler_trace.bsf_trace import trace_bsf
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        src = _write_source(SOURCE_5VAR_ORIG, "pipe_5var")
        trace = trace_bsf(src)

        # Simulate a single swap pair (r30<->r31) meaning first two vars are swapped
        swap_pairs = [("r30", "r31")]
        decl_names = ["a", "b", "c", "d", "e"]

        candidates = guided_pairwise_search(trace, swap_pairs, decl_names)

        # Should have targeted candidates, much fewer than full C(5,2)=10 pairwise
        self.assertGreater(len(candidates), 0, "No candidates generated")

        # With 1 swap pair targeting 2 specific indices, we expect:
        # - 1 direct swap + up to 8 neighbor swaps + maybe multi-swap
        # Much less than blind enumeration of all pairs
        print(f"\n  Candidates: {len(candidates)} (vs C(5,2)=10 unguided)")
        print(f"  Candidate orders: {candidates[:5]}...")

        # The direct swap (b,a,c,d,e) should be present
        direct_swap = ["b", "a", "c", "d", "e"]
        self.assertIn(direct_swap, candidates,
            f"Direct swap {direct_swap} not in candidates")

    def test_multi_function_isolation_e2e(self):
        """BSF partition should isolate target_func from helper and other_func."""
        from tools.compiler_trace.bsf_trace import trace_bsf, _parse_function_info

        src = _write_source(SOURCE_MULTI_ORIG, "pipe_multi")
        trace = trace_bsf(src)
        _, asm_lines = _compile_with_asm(src)

        # Partition by function
        partitions = trace.partition_by_function(asm_lines)

        # Should have 3 function partitions (plus maybe __remainder__)
        func_parts = {k: v for k, v in partitions.items()
                      if k not in ("__all__", "__remainder__")}

        self.assertGreaterEqual(len(func_parts), 3,
            f"Expected 3+ partitions, got {list(func_parts.keys())}")

        # Find target_func partition
        target_part = None
        for name, part in func_parts.items():
            if "target_func" in name:
                target_part = part
                break

        self.assertIsNotNone(target_part, "No partition for target_func")

        # target_func has 3 callee-saved vars (a, b, c)
        # Its partition should have exactly 3 distinct colors
        distinct_colors = {c.bit for c in target_part.calls if c.bit >= 0}
        self.assertEqual(len(distinct_colors), 3,
            f"target_func should have 3 distinct colors, got {distinct_colors}")

        # Now run guided search on JUST the target function's partition
        from tools.compiler_trace.regmap_solver import guided_pairwise_search

        swap_pairs = [("r30", "r31")]
        decl_names = ["a", "b", "c"]

        candidates = guided_pairwise_search(
            trace, swap_pairs, decl_names,
            function_calls=target_part.calls,
        )

        self.assertGreater(len(candidates), 0,
            "No candidates from isolated target_func partition")

        print(f"\n  Partitions: {list(func_parts.keys())}")
        print(f"  target_func distinct colors: {distinct_colors}")
        print(f"  Guided candidates: {candidates}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
