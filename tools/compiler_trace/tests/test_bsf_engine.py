"""BSF engine integration tests — validate register allocation tracing.

These tests compile real C++ source with the MSVC PPC cross-compiler,
capture BSF traces, extract assembly listings, and correlate them.
The goal is to empirically determine and validate:

1. Whether BSF traces are per-function or per-TU
2. Whether declaration order changes affect BSF traces
3. The actual color → physical register mapping
4. Whether the asm_diff pipeline correctly detects register swaps

These are INTEGRATION tests requiring:
- wibo (32-bit debug build for GDB tracing)
- MSVC PPC cross-compiler (cl.exe / c2.dll)
- GDB with ptrace access

Usage:
    python -m pytest tools/compiler_trace/tests/test_bsf_engine.py -v
    python -m pytest tools/compiler_trace/tests/test_bsf_engine.py -v -k "sensitivity"
    python -m pytest tools/compiler_trace/tests/test_bsf_engine.py -v -k "asm"
"""

from __future__ import annotations

import os
import re
import shutil
import sys
import tempfile
import textwrap
import unittest
from collections import Counter, defaultdict
from pathlib import Path
from typing import Optional

# Ensure project root is on the path
_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))


def _check_compiler_available() -> bool:
    """Check if MSVC PPC cross-compiler is available."""
    try:
        from tools.compiler_trace.invoker import CL_EXE, WIBO
        return CL_EXE.exists() and WIBO.exists()
    except Exception:
        return False


def _check_gdb_available() -> bool:
    """Check if GDB + 32-bit wibo are available for BSF tracing."""
    try:
        from tools.compiler_trace.bsf_trace import WIBO_32
        return WIBO_32.exists()
    except Exception:
        return False


SKIP_NO_COMPILER = not _check_compiler_available()
SKIP_NO_GDB = not _check_gdb_available()

# Temp directory for test artifacts — MUST be inside the project tree
# so that _make_cl_path produces relative paths (Z:\ escaping breaks GDB scripts)
TMPDIR = _PROJECT_ROOT / "build" / "bsf_tests"


def _ensure_tmpdir():
    TMPDIR.mkdir(parents=True, exist_ok=True)


# ---------------------------------------------------------------------------
# C++ test fixtures — minimal sources that exercise register allocation
# ---------------------------------------------------------------------------

# 2 variables, both live across calls → forces 2 callee-saved registers
SOURCE_2VAR = textwrap.dedent("""\
    extern int call(int);

    int test_2var_orig(int x) {
        int a = call(1);
        int b = call(2);
        return call(a) + call(b);
    }
""")

# Same function with declarations swapped
SOURCE_2VAR_SWAPPED = textwrap.dedent("""\
    extern int call(int);

    int test_2var_orig(int x) {
        int b = call(2);
        int a = call(1);
        return call(a) + call(b);
    }
""")

# 5 variables live across calls → forces 5 callee-saved registers
SOURCE_5VAR = textwrap.dedent("""\
    extern int call(int);

    int test_5var_orig(int x) {
        int a = call(1);
        int b = call(2);
        int c = call(3);
        int d = call(4);
        int e = call(5);
        return call(a) + call(b) + call(c) + call(d) + call(e);
    }
""")

# 5 variables with 2 swapped (a,b swapped)
SOURCE_5VAR_SWAP_AB = textwrap.dedent("""\
    extern int call(int);

    int test_5var_orig(int x) {
        int b = call(2);
        int a = call(1);
        int c = call(3);
        int d = call(4);
        int e = call(5);
        return call(a) + call(b) + call(c) + call(d) + call(e);
    }
""")

# 5 variables with different 2 swapped (d,e swapped)
SOURCE_5VAR_SWAP_DE = textwrap.dedent("""\
    extern int call(int);

    int test_5var_orig(int x) {
        int a = call(1);
        int b = call(2);
        int c = call(3);
        int e = call(5);
        int d = call(4);
        return call(a) + call(b) + call(c) + call(d) + call(e);
    }
""")

# Multi-function TU: two functions to test per-function isolation
SOURCE_MULTI_FUNC = textwrap.dedent("""\
    extern int call(int);

    int func_alpha(int x) {
        int a = call(1);
        int b = call(2);
        return call(a) + call(b);
    }

    int func_beta(int x) {
        int p = call(10);
        int q = call(20);
        int r = call(30);
        return call(p) + call(q) + call(r);
    }
""")

# Same TU with func_alpha declarations swapped
SOURCE_MULTI_FUNC_SWAPPED = textwrap.dedent("""\
    extern int call(int);

    int func_alpha(int x) {
        int b = call(2);
        int a = call(1);
        return call(a) + call(b);
    }

    int func_beta(int x) {
        int p = call(10);
        int q = call(20);
        int r = call(30);
        return call(p) + call(q) + call(r);
    }
""")

# Volatile-only: variables don't live across calls → volatile registers
SOURCE_VOLATILE_ONLY = textwrap.dedent("""\
    extern int pure(int, int, int);

    int test_volatile(int x) {
        int a = x + 1;
        int b = x + 2;
        int c = x + 3;
        return pure(a, b, c);
    }
""")

# Many callee-saved: 8 vars live across calls
SOURCE_8VAR = textwrap.dedent("""\
    extern int call(int);

    int test_8var(int x) {
        int a = call(1);
        int b = call(2);
        int c = call(3);
        int d = call(4);
        int e = call(5);
        int f = call(6);
        int g = call(7);
        int h = call(8);
        return call(a) + call(b) + call(c) + call(d)
             + call(e) + call(f) + call(g) + call(h);
    }
""")

# Real project source — tests against actual decomp code
# These use actual project headers and types
SOURCE_REAL_SIMPLE = None  # Set in setUpClass if available


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _write_source(content: str, name: str = "test") -> Path:
    """Write C++ source to a temp file, return path."""
    _ensure_tmpdir()
    path = TMPDIR / f"{name}.cpp"
    path.write_text(content)
    return path


def _compile_with_asm(source: Path) -> tuple[Path, list[str]]:
    """Compile source and return (obj_path, asm_lines).

    Uses CompilerInvoker for correct MSVC PPC flags.
    """
    from tools.compiler_trace.invoker import CompilerInvoker

    invoker = CompilerInvoker()
    output_dir = TMPDIR / f"asm_{source.stem}"
    output_dir.mkdir(parents=True, exist_ok=True)

    result = invoker.compile_with_asm(source, output_dir, listing_type="/FAs")
    if result.returncode != 0:
        raise RuntimeError(f"Compilation failed: {result.stderr}")

    # Find the .cod or .asm file
    for ext in (".cod", ".asm"):
        files = list(output_dir.glob(f"*{ext}"))
        if files:
            asm_lines = files[0].read_text().splitlines()
            obj = output_dir / f"{source.stem}.obj"
            return obj, asm_lines

    raise RuntimeError(f"No assembly listing found in {output_dir}")


def _extract_function_registers(asm_lines: list[str], func_name: str) -> dict[str, list[str]]:
    """Extract register usage from an assembly listing for a function.

    Returns dict with keys:
        'callee_saved': list of callee-saved GPRs used (e.g. ['r31', 'r30'])
        'volatile': list of volatile GPRs used
        'fpr': list of FPRs used
        'all_gpr': list of all GPRs used
        'save_restore': registers in save/restore prologue/epilogue
    """
    from tools.compiler_trace.asm_diff import extract_function

    func_lines = extract_function(asm_lines, func_name)
    if not func_lines:
        return {'callee_saved': [], 'volatile': [], 'fpr': [], 'all_gpr': [], 'save_restore': []}

    gpr_pattern = re.compile(r'\br(\d+)\b')
    fpr_pattern = re.compile(r'\bf(\d+)\b')

    all_gprs: set[int] = set()
    all_fprs: set[int] = set()
    save_restore: set[int] = set()

    for line in func_lines:
        stripped = line.strip()

        # Track save/restore registers (prologue/epilogue)
        if '__savegprlr' in stripped or '__restgprlr' in stripped:
            # The register number after __savegprlr_ is the first saved register
            m = re.search(r'__(?:save|rest)gprlr_(\d+)', stripped)
            if m:
                first = int(m.group(1))
                for r in range(first, 32):
                    save_restore.add(r)

        # Collect all register mentions
        for m in gpr_pattern.finditer(stripped):
            all_gprs.add(int(m.group(1)))
        for m in fpr_pattern.finditer(stripped):
            all_fprs.add(int(m.group(1)))

    # Classify GPRs
    callee_saved = sorted(r for r in all_gprs if 13 <= r <= 31)
    volatile = sorted(r for r in all_gprs if 3 <= r <= 12)

    return {
        'callee_saved': [f'r{r}' for r in callee_saved],
        'volatile': [f'r{r}' for r in volatile],
        'fpr': [f'f{r}' for r in sorted(all_fprs)],
        'all_gpr': [f'r{r}' for r in sorted(all_gprs)],
        'save_restore': [f'r{r}' for r in sorted(save_restore)],
    }


def _extract_register_order(asm_lines: list[str], func_name: str,
                            var_names: list[str]) -> dict[str, str]:
    """Try to determine which register each variable got assigned.

    Looks for patterns like:
        mr rN, r3        (saving return value from call)
        stw r3, offset   (saving to stack, then loaded to rN later)

    Returns dict mapping variable name -> register (or '?' if unknown).
    This is heuristic and won't be 100% accurate.
    """
    from tools.compiler_trace.asm_diff import extract_function

    func_lines = extract_function(asm_lines, func_name)
    if not func_lines:
        return {v: '?' for v in var_names}

    # Look for 'mr rN, r3' patterns after 'bl call' — the return value
    # gets moved to a callee-saved register
    assignments: list[str] = []
    prev_was_bl = False
    for line in func_lines:
        stripped = line.strip()
        if stripped.startswith('bl') and 'call' in stripped:
            prev_was_bl = True
            continue
        if prev_was_bl:
            # Next instruction after bl often moves return value
            m = re.match(r'mr\s+(r\d+),\s*r3', stripped)
            if m:
                assignments.append(m.group(1))
            prev_was_bl = False

    # Map assignments to variable names (in declaration order)
    result = {}
    for i, var in enumerate(var_names):
        if i < len(assignments):
            result[var] = assignments[i]
        else:
            result[var] = '?'
    return result


# ---------------------------------------------------------------------------
# Test: BSF Trace Determinism
# ---------------------------------------------------------------------------

@unittest.skipIf(SKIP_NO_GDB, "GDB + 32-bit wibo not available")
class TestBSFTraceDeterminism(unittest.TestCase):
    """BSF traces should be deterministic for the same source."""

    def test_same_source_twice_identical(self):
        """Compiling the same source twice produces the same BSF trace."""
        from tools.compiler_trace.bsf_trace import trace_bsf

        src = _write_source(SOURCE_5VAR, "determ_a")
        trace1 = trace_bsf(src)
        trace2 = trace_bsf(src)

        self.assertEqual(trace1.total_calls, trace2.total_calls,
                         "Call count should be identical for same source")

        for i, (a, b) in enumerate(zip(trace1.calls, trace2.calls)):
            self.assertEqual(a.bit, b.bit,
                             f"Call #{i+1} bit diverged: {a.bit} vs {b.bit}")
            self.assertEqual(a.lo, b.lo,
                             f"Call #{i+1} lo mask diverged")
            self.assertEqual(a.hi, b.hi,
                             f"Call #{i+1} hi mask diverged")


# ---------------------------------------------------------------------------
# Test: BSF Trace Sensitivity to Source Changes
# ---------------------------------------------------------------------------

@unittest.skipIf(SKIP_NO_GDB, "GDB + 32-bit wibo not available")
class TestBSFTraceSensitivity(unittest.TestCase):
    """Test whether BSF traces change when source code changes.

    CRITICAL: These tests validate the core assumption that BSF tracing
    captures per-function register allocation decisions. If these fail,
    the BSF-guided permuter approach needs fundamental redesign.
    """

    def test_decl_swap_2var_changes_trace(self):
        """Swapping 2 declarations should produce a different BSF trace.

        If this FAILS: BSF isn't capturing per-function register allocation.
        """
        from tools.compiler_trace.bsf_trace import trace_bsf

        src_orig = _write_source(SOURCE_2VAR, "sens_2var_orig")
        src_swap = _write_source(SOURCE_2VAR_SWAPPED, "sens_2var_swap")

        trace_orig = trace_bsf(src_orig)
        trace_swap = trace_bsf(src_swap)

        # Check if ANY call differs
        differs = False
        diff_calls = []
        min_len = min(len(trace_orig.calls), len(trace_swap.calls))
        for i in range(min_len):
            a = trace_orig.calls[i]
            b = trace_swap.calls[i]
            if a.bit != b.bit or a.lo != b.lo or a.hi != b.hi:
                differs = True
                diff_calls.append((i + 1, a.bit, b.bit))

        if trace_orig.total_calls != trace_swap.total_calls:
            differs = True

        # This is the KEY assertion — if it fails, BSF doesn't see source changes
        self.assertTrue(
            differs,
            f"BSF trace IDENTICAL for original and swapped declarations. "
            f"orig={trace_orig.total_calls} calls, swap={trace_swap.total_calls} calls. "
            f"BSF is NOT capturing per-function register allocation!"
        )

        if diff_calls:
            # Log which calls differ for debugging
            print(f"\n  Divergent BSF calls ({len(diff_calls)} of {min_len}):")
            for idx, bit_a, bit_b in diff_calls[:10]:
                print(f"    Call #{idx}: bit {bit_a} -> {bit_b}")

    def test_decl_swap_5var_ab_changes_trace(self):
        """Swapping first 2 of 5 declarations should change BSF trace."""
        from tools.compiler_trace.bsf_trace import trace_bsf

        src_orig = _write_source(SOURCE_5VAR, "sens_5var_orig")
        src_swap = _write_source(SOURCE_5VAR_SWAP_AB, "sens_5var_ab")

        trace_orig = trace_bsf(src_orig)
        trace_swap = trace_bsf(src_swap)

        differs = any(
            a.bit != b.bit or a.lo != b.lo
            for a, b in zip(trace_orig.calls, trace_swap.calls)
        ) or trace_orig.total_calls != trace_swap.total_calls

        self.assertTrue(
            differs,
            "BSF trace unchanged when swapping a,b in 5-var function"
        )

    def test_decl_swap_5var_de_changes_trace(self):
        """Swapping last 2 of 5 declarations should change BSF trace."""
        from tools.compiler_trace.bsf_trace import trace_bsf

        src_orig = _write_source(SOURCE_5VAR, "sens_5var_orig2")
        src_swap = _write_source(SOURCE_5VAR_SWAP_DE, "sens_5var_de")

        trace_orig = trace_bsf(src_orig)
        trace_swap = trace_bsf(src_swap)

        differs = any(
            a.bit != b.bit or a.lo != b.lo
            for a, b in zip(trace_orig.calls, trace_swap.calls)
        ) or trace_orig.total_calls != trace_swap.total_calls

        self.assertTrue(
            differs,
            "BSF trace unchanged when swapping d,e in 5-var function"
        )

    def test_different_swap_positions_produce_different_traces(self):
        """Swapping (a,b) vs (d,e) should produce different traces from each other."""
        from tools.compiler_trace.bsf_trace import trace_bsf

        src_ab = _write_source(SOURCE_5VAR_SWAP_AB, "sens_diff_ab")
        src_de = _write_source(SOURCE_5VAR_SWAP_DE, "sens_diff_de")

        trace_ab = trace_bsf(src_ab)
        trace_de = trace_bsf(src_de)

        bits_ab = tuple(c.bit for c in trace_ab.calls)
        bits_de = tuple(c.bit for c in trace_de.calls)

        self.assertNotEqual(
            bits_ab, bits_de,
            "Swapping (a,b) and (d,e) produced identical BSF traces — "
            "BSF is not position-sensitive"
        )

    def test_adding_variable_changes_call_count(self):
        """Adding a variable should change the BSF call count or sequence."""
        from tools.compiler_trace.bsf_trace import trace_bsf

        src_5 = _write_source(SOURCE_5VAR, "sens_count_5")
        src_8 = _write_source(SOURCE_8VAR, "sens_count_8")

        trace_5 = trace_bsf(src_5)
        trace_8 = trace_bsf(src_8)

        # More variables should produce more BSF calls OR different bit patterns
        differs = (
            trace_5.total_calls != trace_8.total_calls or
            tuple(c.bit for c in trace_5.calls) != tuple(c.bit for c in trace_8.calls)
        )

        self.assertTrue(
            differs,
            f"5-var ({trace_5.total_calls} calls) and 8-var ({trace_8.total_calls} calls) "
            f"produced identical BSF traces"
        )

    def test_volatile_vs_callee_saved_different_traces(self):
        """Volatile-only vs callee-saved-heavy should produce different traces."""
        from tools.compiler_trace.bsf_trace import trace_bsf

        src_vol = _write_source(SOURCE_VOLATILE_ONLY, "sens_volatile")
        src_cs = _write_source(SOURCE_5VAR, "sens_callee")

        trace_vol = trace_bsf(src_vol)
        trace_cs = trace_bsf(src_cs)

        bits_vol = tuple(c.bit for c in trace_vol.calls)
        bits_cs = tuple(c.bit for c in trace_cs.calls)

        self.assertNotEqual(
            bits_vol, bits_cs,
            f"Volatile-only and callee-saved-heavy produced same BSF trace "
            f"({trace_vol.total_calls} vs {trace_cs.total_calls} calls)"
        )


# ---------------------------------------------------------------------------
# Test: BSF Trace Per-Function vs Per-TU
# ---------------------------------------------------------------------------

@unittest.skipIf(SKIP_NO_GDB, "GDB + 32-bit wibo not available")
class TestBSFTraceScope(unittest.TestCase):
    """Test whether BSF traces are per-function or per-TU."""

    def test_multi_func_swap_only_affects_target(self):
        """Swapping decls in func_alpha should not change func_beta's allocation.

        If BSF gives us per-TU data, we need to be able to identify
        which calls belong to which function.
        """
        from tools.compiler_trace.bsf_trace import trace_bsf
        from tools.compiler_trace.bsf_diff import diff_bsf_traces

        src_orig = _write_source(SOURCE_MULTI_FUNC, "scope_multi_orig")
        src_swap = _write_source(SOURCE_MULTI_FUNC_SWAPPED, "scope_multi_swap")

        trace_orig = trace_bsf(src_orig)
        trace_swap = trace_bsf(src_swap)

        result = diff_bsf_traces(trace_orig, trace_swap)

        # Record the findings regardless of pass/fail
        print(f"\n  Multi-function BSF diff:")
        print(f"    Total calls: {result.total_calls_a} vs {result.total_calls_b}")
        print(f"    Divergences: {len(result.divergences)}")
        for d in result.divergences[:10]:
            print(f"      #{d.call_index}: bit {d.bit_a}->{d.bit_b} "
                  f"[{d.phase}] mask_diff={d.mask_differs}")

        # Key insight: do we see divergences? If not, BSF isn't per-function.
        if not result.divergences:
            self.fail(
                "No BSF divergences when swapping declarations in multi-func TU. "
                "BSF trace is NOT sensitive to per-function changes."
            )

    def test_different_tu_sizes_different_call_counts(self):
        """TUs with different numbers of functions should have different call counts."""
        from tools.compiler_trace.bsf_trace import trace_bsf

        src_2func = _write_source(SOURCE_MULTI_FUNC, "scope_2func")
        src_1func = _write_source(SOURCE_5VAR, "scope_1func")

        trace_2func = trace_bsf(src_2func)
        trace_1func = trace_bsf(src_1func)

        # If call counts are the same, BSF might be capturing fixed init code
        print(f"\n  1-func TU: {trace_1func.total_calls} calls")
        print(f"  2-func TU: {trace_2func.total_calls} calls")

        if trace_1func.total_calls == trace_2func.total_calls:
            bits_1 = tuple(c.bit for c in trace_1func.calls)
            bits_2 = tuple(c.bit for c in trace_2func.calls)
            if bits_1 == bits_2:
                self.fail(
                    f"1-func and 2-func TUs have identical BSF traces "
                    f"({trace_1func.total_calls} calls each). "
                    f"BSF is likely capturing compiler init, not function allocation."
                )


# ---------------------------------------------------------------------------
# Test: Assembly Listing Register Assignment
# ---------------------------------------------------------------------------

@unittest.skipIf(SKIP_NO_COMPILER, "MSVC PPC compiler not available")
class TestAsmRegisterAssignment(unittest.TestCase):
    """Compile C++ and examine actual register assignments in assembly.

    These tests don't use BSF at all — they directly verify that
    declaration order affects register assignment in the output.
    """

    def test_compile_5var_produces_callee_saved(self):
        """5 variables live across calls should use callee-saved registers."""
        src = _write_source(SOURCE_5VAR, "asm_5var")
        _, asm_lines = _compile_with_asm(src)

        regs = _extract_function_registers(asm_lines, "test_5var_orig")

        self.assertGreater(
            len(regs['callee_saved']), 0,
            f"Expected callee-saved register usage. Got: {regs}"
        )

        print(f"\n  5-var function registers:")
        print(f"    callee_saved: {regs['callee_saved']}")
        print(f"    volatile: {regs['volatile']}")
        print(f"    save_restore: {regs['save_restore']}")

    def test_compile_volatile_only_no_callee_saved(self):
        """Variables not live across calls should use only volatile registers."""
        src = _write_source(SOURCE_VOLATILE_ONLY, "asm_volatile")
        _, asm_lines = _compile_with_asm(src)

        regs = _extract_function_registers(asm_lines, "test_volatile")

        print(f"\n  Volatile-only function registers:")
        print(f"    callee_saved: {regs['callee_saved']}")
        print(f"    volatile: {regs['volatile']}")
        print(f"    save_restore: {regs['save_restore']}")

        # Volatile-only shouldn't need save/restore (no callee-saved usage)
        if regs['save_restore']:
            print(f"    NOTE: Compiler used callee-saved despite no cross-call liveness")

    def test_decl_swap_changes_register_assignment(self):
        """Swapping declarations should swap register assignments.

        This is the ground truth test — regardless of BSF, does the compiler
        actually assign different registers when declarations are reordered?
        """
        src_orig = _write_source(SOURCE_2VAR, "asm_swap_orig")
        src_swap = _write_source(SOURCE_2VAR_SWAPPED, "asm_swap_swap")

        _, asm_orig = _compile_with_asm(src_orig)
        _, asm_swap = _compile_with_asm(src_swap)

        regs_orig = _extract_register_order(asm_orig, "test_2var_orig", ["a", "b"])
        regs_swap = _extract_register_order(asm_swap, "test_2var_orig", ["b", "a"])

        print(f"\n  2-var register assignments:")
        print(f"    Original (a,b): a={regs_orig.get('a','?')} b={regs_orig.get('b','?')}")
        print(f"    Swapped  (b,a): b={regs_swap.get('b','?')} a={regs_swap.get('a','?')}")

        # The actual register numbers should differ or be swapped
        if regs_orig.get('a') != '?' and regs_swap.get('a') != '?':
            # If same variable gets same register regardless of position, decl order
            # doesn't affect register allocation (important finding!)
            if regs_orig['a'] == regs_swap['a']:
                print("    WARNING: Declaration order did NOT change register assignment!")
                print("    This means the compiler uses a different ordering heuristic.")

    def test_5var_swap_ab_changes_registers(self):
        """Swapping first 2 of 5 declarations should change some register assignments."""
        src_orig = _write_source(SOURCE_5VAR, "asm_5var_orig")
        src_swap = _write_source(SOURCE_5VAR_SWAP_AB, "asm_5var_ab")

        _, asm_orig = _compile_with_asm(src_orig)
        _, asm_swap = _compile_with_asm(src_swap)

        vars_orig = ["a", "b", "c", "d", "e"]
        vars_swap = ["b", "a", "c", "d", "e"]

        regs_orig = _extract_register_order(asm_orig, "test_5var_orig", vars_orig)
        regs_swap = _extract_register_order(asm_swap, "test_5var_orig", vars_swap)

        print(f"\n  5-var register assignments (orig vs swap-ab):")
        for v in vars_orig:
            r_orig = regs_orig.get(v, '?')
            r_swap = regs_swap.get(v, '?')
            marker = " <-- CHANGED" if r_orig != r_swap and r_orig != '?' and r_swap != '?' else ""
            print(f"    {v}: {r_orig} -> {r_swap}{marker}")

    def test_asm_diff_detects_register_swap(self):
        """The asm_diff module should detect register swaps between variants."""
        from tools.compiler_trace.asm_diff import (
            extract_function, normalize_listing, detect_register_swaps
        )

        src_orig = _write_source(SOURCE_2VAR, "asmdiff_orig")
        src_swap = _write_source(SOURCE_2VAR_SWAPPED, "asmdiff_swap")

        _, asm_orig = _compile_with_asm(src_orig)
        _, asm_swap = _compile_with_asm(src_swap)

        func_orig = extract_function(asm_orig, "test_2var_orig")
        func_swap = extract_function(asm_swap, "test_2var_orig")

        self.assertGreater(len(func_orig), 0, "Could not extract function from original ASM")
        self.assertGreater(len(func_swap), 0, "Could not extract function from swapped ASM")

        norm_orig = normalize_listing(func_orig)
        norm_swap = normalize_listing(func_swap)

        swaps = detect_register_swaps(norm_orig, norm_swap)

        print(f"\n  Detected register swaps between variants:")
        print(f"    Swaps: {swaps}")
        print(f"    Original lines: {len(func_orig)}")
        print(f"    Swapped lines: {len(func_swap)}")

        # If the compiler respects declaration order, we should see swaps
        if not swaps:
            print("    NOTE: No register swaps detected. Compiler may not be sensitive to decl order.")


# ---------------------------------------------------------------------------
# Test: BSF Color → Register Mapping Discovery
# ---------------------------------------------------------------------------

@unittest.skipIf(SKIP_NO_GDB or SKIP_NO_COMPILER, "Need GDB + compiler")
class TestColorToRegisterMapping(unittest.TestCase):
    """Empirically determine the BSF color → physical register mapping.

    Compiles test functions, captures both BSF trace AND assembly listing,
    then correlates BSF color assignments with actual register usage.
    """

    def test_discover_color_mapping_5var(self):
        """Compile 5-var function, get BSF + ASM, correlate colors to registers."""
        from tools.compiler_trace.bsf_trace import trace_bsf
        from tools.compiler_trace.regmap_solver import extract_initial_colorings

        src = _write_source(SOURCE_5VAR, "colormap_5var")

        # Get BSF trace
        trace = trace_bsf(src)
        colorings = extract_initial_colorings(trace)

        # Get ASM and extract register assignments
        _, asm_lines = _compile_with_asm(src)
        var_regs = _extract_register_order(asm_lines, "test_5var_orig",
                                           ["a", "b", "c", "d", "e"])
        func_regs = _extract_function_registers(asm_lines, "test_5var_orig")

        print(f"\n  Color mapping discovery (5-var):")
        print(f"    BSF calls: {trace.total_calls}")
        print(f"    Initial colorings: {len(colorings)}")
        for i, ca in enumerate(colorings):
            from tools.compiler_trace.regmap_solver import color_to_gpr
            predicted = color_to_gpr(ca.color) or "unmapped"
            print(f"      [{i}] color={ca.color} -> predicted={predicted}")
        print(f"    ASM register assignments:")
        for v, r in var_regs.items():
            print(f"      {v} -> {r}")
        print(f"    Callee-saved used: {func_regs['callee_saved']}")
        print(f"    Save/restore: {func_regs['save_restore']}")

        # Try to correlate: if we have N colorings and N register assignments,
        # build a color→register table
        known_vars = [(v, r) for v, r in var_regs.items() if r != '?']
        if known_vars and colorings:
            print(f"\n    Correlation attempt:")
            for i, (v, r) in enumerate(known_vars):
                if i < len(colorings):
                    print(f"      var={v} reg={r} bsf_color={colorings[i].color}")
                else:
                    print(f"      var={v} reg={r} (no BSF coloring for this index)")

    def test_discover_mapping_via_swap(self):
        """Use two variants to definitively map colors to registers.

        Compile A (a,b,c,d,e) and B (b,a,c,d,e).
        BSF diff tells us which colors changed.
        ASM diff tells us which registers changed.
        The intersection reveals the mapping.
        """
        from tools.compiler_trace.bsf_trace import trace_bsf
        from tools.compiler_trace.bsf_diff import diff_bsf_traces
        from tools.compiler_trace.asm_diff import (
            extract_function, normalize_listing, detect_register_swaps
        )

        src_orig = _write_source(SOURCE_5VAR, "map_swap_orig")
        src_swap = _write_source(SOURCE_5VAR_SWAP_AB, "map_swap_ab")

        # BSF traces
        trace_orig = trace_bsf(src_orig)
        trace_swap = trace_bsf(src_swap)
        bsf_diff = diff_bsf_traces(trace_orig, trace_swap)

        # ASM listings
        _, asm_orig = _compile_with_asm(src_orig)
        _, asm_swap = _compile_with_asm(src_swap)

        func_orig = extract_function(asm_orig, "test_5var_orig")
        func_swap = extract_function(asm_swap, "test_5var_orig")

        norm_orig = normalize_listing(func_orig)
        norm_swap = normalize_listing(func_swap)
        reg_swaps = detect_register_swaps(norm_orig, norm_swap)

        print(f"\n  Swap-based color→register discovery:")
        print(f"    BSF divergences: {len(bsf_diff.divergences)}")
        for d in bsf_diff.divergences[:10]:
            print(f"      #{d.call_index}: color {d.bit_a}->{d.bit_b} [{d.phase}]")
        print(f"    ASM register swaps: {reg_swaps}")

        # If both BSF and ASM show changes, we can correlate
        if bsf_diff.divergences and reg_swaps:
            bsf_colors_changed = set()
            for d in bsf_diff.divergences:
                bsf_colors_changed.add(d.bit_a)
                bsf_colors_changed.add(d.bit_b)

            asm_regs_changed = set()
            for ra, rb in reg_swaps.items():
                asm_regs_changed.add(ra)
                asm_regs_changed.add(rb)

            print(f"\n    BSF colors involved: {sorted(bsf_colors_changed)}")
            print(f"    ASM registers involved: {sorted(asm_regs_changed)}")
            print(f"    => These should correspond to each other")
        elif not bsf_diff.divergences:
            print(f"\n    BSF showed no divergences — cannot determine mapping")
        elif not reg_swaps:
            print(f"\n    ASM showed no register swaps — cannot determine mapping")


# ---------------------------------------------------------------------------
# Test: BSF Trace Structure Analysis
# ---------------------------------------------------------------------------

@unittest.skipIf(SKIP_NO_GDB, "GDB + 32-bit wibo not available")
class TestBSFTraceStructure(unittest.TestCase):
    """Analyze the structure of BSF traces to understand what we're capturing."""

    def test_caller_rva_distribution(self):
        """Map which caller RVAs produce BSF calls and how many."""
        from tools.compiler_trace.bsf_trace import trace_bsf
        from tools.compiler_trace.regmap_solver import INITIAL_COLORING_RVA, COALESCING_RVA, RECOLORING_RVA

        src = _write_source(SOURCE_5VAR, "struct_rva")
        trace = trace_bsf(src)

        by_rva: dict[int, int] = Counter()
        for c in trace.calls:
            by_rva[c.caller_rva] += 1

        known_rvas = {
            INITIAL_COLORING_RVA: "initial_coloring",
            COALESCING_RVA: "coalescing",
            RECOLORING_RVA: "recoloring",
        }

        print(f"\n  Caller RVA distribution ({trace.total_calls} total calls):")
        for rva, count in sorted(by_rva.items()):
            name = known_rvas.get(rva, "UNKNOWN")
            print(f"    0x{rva:06x} ({name}): {count} calls")

        # All calls should come from known RVAs
        unknown = {rva for rva in by_rva if rva not in known_rvas}
        if unknown:
            print(f"    WARNING: Unknown caller RVAs: {[f'0x{r:06x}' for r in unknown]}")

    def test_base_field_distribution(self):
        """Analyze the 'base' field to understand register class structure."""
        from tools.compiler_trace.bsf_trace import trace_bsf

        src = _write_source(SOURCE_8VAR, "struct_base")
        trace = trace_bsf(src)

        by_base: dict[int, list] = defaultdict(list)
        for c in trace.calls:
            by_base[c.base].append(c)

        print(f"\n  Register class base distribution:")
        for base, calls in sorted(by_base.items()):
            bits = sorted(set(c.bit for c in calls))
            callers = sorted(set(c.caller_rva for c in calls))
            caller_names = []
            for rva in callers:
                name = {0x027242: "INIT", 0x026B5E: "COAL", 0x0272E8: "RECOL"}.get(rva, f"0x{rva:06x}")
                caller_names.append(name)
            print(f"    base={base}: {len(calls)} calls, bits={bits}, phases={caller_names}")

    def test_availability_mask_analysis(self):
        """Analyze availability masks to understand the register file layout.

        The availability mask tells us which colors/registers are available
        at each BSF call. The union of all masks reveals the full register set.
        """
        from tools.compiler_trace.bsf_trace import trace_bsf
        from tools.compiler_trace.regmap_solver import INITIAL_COLORING_RVA

        src = _write_source(SOURCE_8VAR, "struct_mask")
        trace = trace_bsf(src)

        initial = trace.phase_calls(INITIAL_COLORING_RVA)

        # Union of all lo masks
        lo_union = 0
        hi_union = 0
        for c in initial:
            lo_union |= c.lo
            hi_union |= c.hi

        # Extract individual bit positions
        lo_bits = [i for i in range(32) if lo_union & (1 << i)]
        hi_bits = [i + 32 for i in range(32) if hi_union & (1 << i)]

        print(f"\n  Availability mask analysis (initial coloring, {len(initial)} calls):")
        print(f"    lo union: 0x{lo_union:08x} -> bits {lo_bits}")
        print(f"    hi union: 0x{hi_union:08x} -> bits {hi_bits}")
        print(f"    Total allocable positions: {len(lo_bits) + len(hi_bits)}")

        # Group by base to see per-class register files
        by_base: dict[int, tuple[int, int]] = {}
        for c in initial:
            if c.base not in by_base:
                by_base[c.base] = (0, 0)
            prev_lo, prev_hi = by_base[c.base]
            by_base[c.base] = (prev_lo | c.lo, prev_hi | c.hi)

        for base, (lo, hi) in sorted(by_base.items()):
            lo_bits = [i for i in range(32) if lo & (1 << i)]
            hi_bits = [i + 32 for i in range(32) if hi & (1 << i)]
            print(f"    base={base}: lo_bits={lo_bits} hi_bits={hi_bits}")

    def test_coloring_sequence_patterns(self):
        """Look for repeating patterns in BSF call sequences.

        If the same sequence repeats, it might indicate function boundaries
        or repeated template instantiations.
        """
        from tools.compiler_trace.bsf_trace import trace_bsf
        from tools.compiler_trace.regmap_solver import INITIAL_COLORING_RVA

        src = _write_source(SOURCE_MULTI_FUNC, "struct_seq")
        trace = trace_bsf(src)

        initial = trace.phase_calls(INITIAL_COLORING_RVA)
        bits = [c.bit for c in initial]

        # Look for bit=-1 as potential function boundary marker
        neg1_indices = [i for i, b in enumerate(bits) if b == -1]

        # Look for mask resets (lo going back to a high value)
        resets = []
        for i in range(1, len(initial)):
            prev_lo = initial[i-1].lo
            cur_lo = initial[i].lo
            # A "reset" is when available bits suddenly increase
            if bin(cur_lo).count('1') > bin(prev_lo).count('1') + 2:
                resets.append(i)

        print(f"\n  Sequence pattern analysis ({len(initial)} initial calls):")
        print(f"    bit=-1 positions: {neg1_indices}")
        print(f"    Mask reset positions: {resets[:20]}")
        print(f"    Bit sequence: {bits}")


# ---------------------------------------------------------------------------
# Test: Real Project Source (Integration)
# ---------------------------------------------------------------------------

@unittest.skipIf(SKIP_NO_GDB or SKIP_NO_COMPILER, "Need GDB + compiler")
class TestRealProjectSource(unittest.TestCase):
    """Test BSF tracing on actual decomp source files.

    These tests use real .cpp files from the project to validate BSF
    behavior on production code rather than synthetic fixtures.
    """

    REAL_SOURCE = _PROJECT_ROOT / "src" / "system" / "hamobj" / "HamNavList.cpp"

    @classmethod
    def setUpClass(cls):
        if not cls.REAL_SOURCE.exists():
            raise unittest.SkipTest(f"Real source not found: {cls.REAL_SOURCE}")

    def test_real_source_trace_nonzero(self):
        """Real project source should produce BSF calls."""
        from tools.compiler_trace.bsf_trace import trace_bsf

        trace = trace_bsf(self.REAL_SOURCE)
        self.assertGreater(trace.total_calls, 0, "No BSF calls from real source")

        print(f"\n  {self.REAL_SOURCE.name}: {trace.total_calls} BSF calls")

    def test_real_source_asm_extracts_function(self):
        """Should be able to extract a specific function from real ASM listing."""
        from tools.compiler_trace.asm_diff import extract_function

        _, asm_lines = _compile_with_asm(self.REAL_SOURCE)

        func = extract_function(asm_lines, "NumItems")
        print(f"\n  NumItems: {len(func)} ASM lines")

        if func:
            regs = _extract_function_registers(asm_lines, "NumItems")
            print(f"    callee_saved: {regs['callee_saved']}")
            print(f"    volatile: {regs['volatile']}")
            print(f"    save_restore: {regs['save_restore']}")

            self.assertGreater(len(func), 0, "Function extraction returned empty")

    def test_real_source_decl_swap_asm_diff(self):
        """Swap declarations in a real function and check ASM changes.

        Uses HamNavList::NumItems which has a known r30<->r31 swap.
        Compiles from the original source directory to preserve include paths.
        """
        from tools.compiler_trace.asm_diff import (
            extract_function, normalize_listing, detect_register_swaps
        )

        content = self.REAL_SOURCE.read_text()

        # Find NumItems function and swap its declarations
        # int count; int i; -> int i; int count;
        if 'int count;\n    int i;' not in content:
            self.skipTest("Could not find expected declaration pattern in NumItems")

        swapped = content.replace(
            'int count;\n    int i;',
            'int i;\n    int count;'
        )

        # Write swapped version in the SAME directory as the original
        # so include paths resolve correctly
        swap_path = self.REAL_SOURCE.parent / f"_bsf_test_swap_{self.REAL_SOURCE.name}"
        try:
            swap_path.write_text(swapped)

            _, asm_orig = _compile_with_asm(self.REAL_SOURCE)
            _, asm_swap = _compile_with_asm(swap_path)

            func_orig = extract_function(asm_orig, "NumItems")
            func_swap = extract_function(asm_swap, "NumItems")

            if not func_orig or not func_swap:
                self.skipTest("Could not extract NumItems from ASM listing")

            norm_orig = normalize_listing(func_orig)
            norm_swap = normalize_listing(func_swap)

            swaps = detect_register_swaps(norm_orig, norm_swap)

            print(f"\n  NumItems declaration swap ASM diff:")
            print(f"    Register swaps: {swaps}")
            print(f"    Lines changed: {sum(1 for a, b in zip(norm_orig, norm_swap) if a != b)}")

            # If we see r30<->r31, that confirms declaration order controls register assignment
            if swaps:
                swap_pairs = set()
                for ra, rb in swaps.items():
                    pair = tuple(sorted([ra, rb]))
                    swap_pairs.add(pair)
                print(f"    Swap pairs: {swap_pairs}")

                if ('r30', 'r31') in swap_pairs or ('r31', 'r30') in swap_pairs:
                    print("    CONFIRMED: Declaration swap resolves r30<->r31")
        finally:
            if swap_path.exists():
                swap_path.unlink()


# ---------------------------------------------------------------------------
# Test: Current color_to_gpr / gpr_to_color Accuracy
# ---------------------------------------------------------------------------

class TestCurrentMappingAccuracy(unittest.TestCase):
    """Validate the color→GPR mapping against empirical data.

    These mappings were determined by TestColorToRegisterMapping:
    - Compiled 5-var synthetic function, captured BSF + ASM
    - Correlated BSF color assignments with register usage in assembly
    - Formula: volatile colors 0-6 → reg = 11-color, callee-saved colors 7+ → reg = 38-color
    """

    def test_callee_saved_mapping(self):
        """Verify callee-saved color→GPR mapping matches empirical data."""
        from tools.compiler_trace.regmap_solver import color_to_gpr, gpr_to_color

        # Empirically confirmed via TestColorToRegisterMapping::test_discover_color_mapping_5var
        expected = {
            7: "r31",   # var a (1st declared)
            8: "r30",   # var b (2nd declared)
            9: "r29",   # var c (3rd declared)
            10: "r28",  # var d (4th declared)
            11: "r27",  # var e (5th declared)
        }
        for color, reg in expected.items():
            self.assertEqual(
                color_to_gpr(color), reg,
                f"color_to_gpr({color}) should be {reg}"
            )
            self.assertEqual(
                gpr_to_color(reg), color,
                f"gpr_to_color({reg}) should be {color}"
            )

    def test_volatile_mapping(self):
        """Verify volatile color→GPR mapping."""
        from tools.compiler_trace.regmap_solver import color_to_gpr, gpr_to_color

        # Volatile: colors 0-6 → r11 down to r5
        expected = {
            0: "r11",
            1: "r10",
            2: "r9",
            3: "r8",
            4: "r7",
            5: "r6",
            6: "r5",
        }
        for color, reg in expected.items():
            self.assertEqual(
                color_to_gpr(color), reg,
                f"color_to_gpr({color}) should be {reg}"
            )
            self.assertEqual(
                gpr_to_color(reg), color,
                f"gpr_to_color({reg}) should be {color}"
            )

    def test_boundary_color_7_is_callee_saved(self):
        """Color 7 must map to r31 (callee-saved), NOT r4 (volatile).

        This was the key bug in the old mapping: color 7 was incorrectly
        mapped to volatile r4 when it's actually the first callee-saved (r31).
        """
        from tools.compiler_trace.regmap_solver import color_to_gpr
        self.assertEqual(color_to_gpr(7), "r31", "color 7 must be r31 (callee-saved)")

    def test_round_trip_all_known_gprs(self):
        """color_to_gpr(gpr_to_color(r)) == r for all known GPRs."""
        from tools.compiler_trace.regmap_solver import color_to_gpr, gpr_to_color

        for num in list(range(5, 12)) + list(range(13, 32)):
            reg = f"r{num}"
            color = gpr_to_color(reg)
            self.assertIsNotNone(color, f"gpr_to_color({reg}) should not be None")
            self.assertEqual(
                color_to_gpr(color), reg,
                f"Round-trip failed: {reg} -> color {color} -> {color_to_gpr(color)}"
            )


# ---------------------------------------------------------------------------
# Test: BSF Partitioning (Per-Function Isolation)
# ---------------------------------------------------------------------------

@unittest.skipIf(SKIP_NO_GDB or SKIP_NO_COMPILER, "Need GDB + compiler")
class TestBSFPartitioning(unittest.TestCase):
    """Test BSF trace partitioning by function.

    Validates that partition_by_function() correctly splits BSF traces
    into per-function segments using assembly listing data.
    """

    def test_partition_multi_func(self):
        """Multi-function TU should produce separate partitions per function."""
        from tools.compiler_trace.bsf_trace import trace_bsf

        src = _write_source(SOURCE_MULTI_FUNC, "part_multi")
        trace = trace_bsf(src)
        _, asm_lines = _compile_with_asm(src)

        partitions = trace.partition_by_function(asm_lines)

        # Should have entries for both func_alpha and func_beta
        func_names = [k for k in partitions if k not in ("__all__", "__remainder__")]
        self.assertGreaterEqual(len(func_names), 2,
            f"Expected 2+ function partitions, got {func_names}")

        # Find the partitions (names will be mangled)
        alpha_part = None
        beta_part = None
        for name in func_names:
            if "alpha" in name.lower():
                alpha_part = partitions[name]
            elif "beta" in name.lower():
                beta_part = partitions[name]

        self.assertIsNotNone(alpha_part, "No partition found for func_alpha")
        self.assertIsNotNone(beta_part, "No partition found for func_beta")

        # Both should have non-empty call lists
        self.assertGreater(len(alpha_part.calls), 0,
            "func_alpha partition has no BSF calls")
        self.assertGreater(len(beta_part.calls), 0,
            "func_beta partition has no BSF calls")

        # Their calls should be disjoint (no shared BSF call indices)
        alpha_indices = {c.index for c in alpha_part.calls}
        beta_indices = {c.index for c in beta_part.calls}
        overlap = alpha_indices & beta_indices
        self.assertEqual(len(overlap), 0,
            f"Partitions overlap on BSF call indices: {overlap}")

        print(f"\n  Partitions: {func_names}")
        print(f"  alpha: {len(alpha_part.calls)} calls")
        print(f"  beta: {len(beta_part.calls)} calls")

    def test_partition_matches_asm_registers(self):
        """Each function's partition color count should match its callee-saved register count."""
        from tools.compiler_trace.bsf_trace import trace_bsf, _parse_function_info

        src = _write_source(SOURCE_MULTI_FUNC, "part_regs")
        trace = trace_bsf(src)
        _, asm_lines = _compile_with_asm(src)

        # Get function info from asm listing
        func_info = _parse_function_info(asm_lines)
        self.assertGreaterEqual(len(func_info), 2, "Expected 2+ functions in listing")

        # Get partitions
        partitions = trace.partition_by_function(asm_lines)

        for func_name, expected_count in func_info:
            if expected_count == 0:
                continue

            # Find matching partition
            part = None
            for pname, ptrace in partitions.items():
                if pname == func_name:
                    part = ptrace
                    break

            if part is None:
                continue

            # Count distinct colors in this partition
            distinct_colors = {c.bit for c in part.calls if c.bit >= 0}
            self.assertEqual(
                len(distinct_colors), expected_count,
                f"{func_name}: expected {expected_count} distinct colors, "
                f"got {len(distinct_colors)} ({distinct_colors})"
            )

            print(f"\n  {func_name}: {expected_count} callee-saved regs, "
                  f"{len(distinct_colors)} distinct colors")

    def test_partition_swap_only_affects_target(self):
        """Swapping decls in func_alpha should only change alpha's partition."""
        from tools.compiler_trace.bsf_trace import trace_bsf

        src_orig = _write_source(SOURCE_MULTI_FUNC, "part_orig")
        src_swap = _write_source(SOURCE_MULTI_FUNC_SWAPPED, "part_swap")

        trace_orig = trace_bsf(src_orig)
        trace_swap = trace_bsf(src_swap)

        _, asm_orig = _compile_with_asm(src_orig)
        _, asm_swap = _compile_with_asm(src_swap)

        parts_orig = trace_orig.partition_by_function(asm_orig)
        parts_swap = trace_swap.partition_by_function(asm_swap)

        # Find beta partitions
        beta_orig = None
        beta_swap = None
        for name, part in parts_orig.items():
            if "beta" in name.lower():
                beta_orig = part
        for name, part in parts_swap.items():
            if "beta" in name.lower():
                beta_swap = part

        self.assertIsNotNone(beta_orig, "No beta partition in original")
        self.assertIsNotNone(beta_swap, "No beta partition in swapped")

        # Beta's color sequence should be identical between orig and swapped
        # (since only func_alpha was swapped)
        orig_bits = [c.bit for c in beta_orig.calls]
        swap_bits = [c.bit for c in beta_swap.calls]

        self.assertEqual(orig_bits, swap_bits,
            f"func_beta partition changed when only func_alpha was swapped!\n"
            f"  orig: {orig_bits}\n  swap: {swap_bits}")

        # Find alpha partitions — these SHOULD differ
        alpha_orig = None
        alpha_swap = None
        for name, part in parts_orig.items():
            if "alpha" in name.lower():
                alpha_orig = part
        for name, part in parts_swap.items():
            if "alpha" in name.lower():
                alpha_swap = part

        if alpha_orig and alpha_swap:
            orig_alpha_bits = [c.bit for c in alpha_orig.calls]
            swap_alpha_bits = [c.bit for c in alpha_swap.calls]
            # Alpha's bits should be the same set (same colors) but
            # the assignment ORDER may differ. The BSF bit values themselves
            # are colors, so same set of colors with possibly different ordering
            print(f"\n  alpha orig bits: {orig_alpha_bits}")
            print(f"  alpha swap bits: {swap_alpha_bits}")
            print(f"  beta unchanged: {orig_bits == swap_bits}")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    unittest.main(verbosity=2)
