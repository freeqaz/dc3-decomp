#!/usr/bin/env python3
"""
Differential Testing Harness for MSVC Xbox 360 PPC Compiler (c2.dll)

Compiles carefully crafted test cases with /FAcs, extracts per-function
assembly, and diffs the output across source variations to map codegen decisions.

Usage:
    python3 msvc-src/tools/diff_test.py --suite regalloc
    python3 msvc-src/tools/diff_test.py --suite inline_threshold
    python3 msvc-src/tools/diff_test.py --suite peephole
    python3 msvc-src/tools/diff_test.py --suite bsf_threshold
    python3 msvc-src/tools/diff_test.py --suite branch_polarity
    python3 msvc-src/tools/diff_test.py --suite float_precision
    python3 msvc-src/tools/diff_test.py --suite fpr_allocation
    python3 msvc-src/tools/diff_test.py --suite template_signedness
    python3 msvc-src/tools/diff_test.py --suite cross_call_live_range
    python3 msvc-src/tools/diff_test.py --suite scope_nesting
    python3 msvc-src/tools/diff_test.py --suite static_local_guard
    python3 msvc-src/tools/diff_test.py --suite all
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# Paths
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
WIBO = REPO_ROOT / "build" / "tools" / "wibo"
COMPILER_DIR = REPO_ROOT / "build" / "compilers" / "X360" / "16.00.11886.00"
CL_EXE = COMPILER_DIR / "cl.exe"
RESULTS_DIR = REPO_ROOT / "msvc-src" / "results"


@dataclass
class FunctionAsm:
    """Parsed assembly for a single function."""
    name: str
    mangled: str
    instructions: list[tuple[int, str, str]]  # (offset, hex, mnemonic+operands)
    prologue_helper: Optional[str] = None
    callee_saved_gprs: list[str] = field(default_factory=list)
    callee_saved_fprs: list[str] = field(default_factory=list)
    stack_frame_size: int = 0
    raw_lines: list[str] = field(default_factory=list)

    @property
    def instruction_count(self) -> int:
        return len(self.instructions)

    @property
    def asm_hash(self) -> str:
        """Hash of instruction sequence (ignoring addresses)."""
        content = "\n".join(f"{m}" for _, _, m in self.instructions)
        return hashlib.md5(content.encode()).hexdigest()[:12]


@dataclass
class FunctionDiff:
    """Structural diff between two function assemblies."""
    register_swaps: dict[str, str]  # old_reg -> new_reg
    instruction_diffs: int  # count of changed instructions
    structural_match: bool  # same structure, possibly different registers
    prologue_changed: bool
    stack_size_changed: bool
    callee_saved_changed: bool


def compile_source(source: str, extra_flags: list[str] = None) -> Optional[str]:
    """Compile source with /FAcs, return path to .cod listing file."""
    with tempfile.TemporaryDirectory(prefix="diff_test_") as tmpdir:
        src_path = os.path.join(tmpdir, "test.cpp")
        with open(src_path, "w") as f:
            f.write(source)

        cmd = [
            str(WIBO), str(CL_EXE),
            "/c", "/FAcs", "/Ox", "/GS-", "/nologo",
        ]
        if extra_flags:
            cmd.extend(extra_flags)
        cmd.append("test.cpp")

        try:
            result = subprocess.run(
                cmd, cwd=tmpdir, capture_output=True, text=True, timeout=30
            )
        except subprocess.TimeoutExpired:
            print(f"  TIMEOUT compiling", file=sys.stderr)
            return None

        cod_path = os.path.join(tmpdir, "test.cod")
        if not os.path.exists(cod_path):
            print(f"  COMPILE FAILED: {result.stderr}", file=sys.stderr)
            return None

        # Copy to a persistent location
        with open(cod_path, "r") as f:
            content = f.read()
        return content


def parse_asm_listing(listing: str) -> dict[str, FunctionAsm]:
    """Parse a .cod listing into per-function FunctionAsm objects."""
    functions = {}
    current_func = None
    current_mangled = None
    current_insns = []
    current_raw = []
    in_function = False

    # Pattern: function PROC NEAR
    proc_re = re.compile(r'^(\S+)\s+PROC\s+NEAR')
    # Pattern: function ENDP
    endp_re = re.compile(r'^(\S+)\s+ENDP')
    # Pattern: instruction line "  00010  38600000   li  r3,0"
    insn_re = re.compile(r'^\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(.+)$')
    # Pattern: prologue helper call
    save_re = re.compile(r'bl\s+(__save[gf]prlr_\d+|__savegprlr_\d+|__savefpr_\d+)')
    # Pattern: stwu/stdu for stack frame
    frame_re = re.compile(r'stwu\s+r1,-(\d+|[0-9a-f]+h)\(r1\)')
    # Pattern: callee-saved store (std rN or stfd fN)
    gpr_save_re = re.compile(r'(?:std?|stw)\s+(r\d+)')
    fpr_save_re = re.compile(r'stfd\s+(f\d+)')

    for line in listing.split('\n'):
        proc_match = proc_re.match(line)
        if proc_match:
            current_mangled = proc_match.group(1)
            in_function = True
            current_insns = []
            current_raw = []
            continue

        endp_match = endp_re.match(line)
        if endp_match and in_function:
            # Build FunctionAsm
            func = FunctionAsm(
                name=_demangle(current_mangled),
                mangled=current_mangled,
                instructions=current_insns,
                raw_lines=current_raw,
            )

            # Extract metadata from instructions
            callee_gprs = set()
            callee_fprs = set()
            for _, _, mnemonic in current_insns:
                # Prologue helper
                save_match = save_re.search(mnemonic)
                if save_match:
                    func.prologue_helper = save_match.group(1)
                    # Extract register range from helper name
                    helper = save_match.group(1)
                    if 'gprlr_' in helper:
                        start_reg = int(helper.split('_')[-1])
                        for r in range(start_reg, 32):
                            callee_gprs.add(f"r{r}")
                    elif 'fpr_' in helper:
                        start_reg = int(helper.split('_')[-1])
                        for r in range(start_reg, 32):
                            callee_fprs.add(f"f{r}")

                # Stack frame
                frame_match = frame_re.search(mnemonic)
                if frame_match:
                    size_str = frame_match.group(1)
                    if size_str.endswith('h'):
                        func.stack_frame_size = int(size_str[:-1], 16)
                    else:
                        func.stack_frame_size = int(size_str)

                # Direct callee-saved stores (when no helper)
                gpr_match = gpr_save_re.search(mnemonic)
                if gpr_match:
                    reg = gpr_match.group(1)
                    rnum = int(reg[1:])
                    if rnum >= 14:  # callee-saved GPR range
                        callee_gprs.add(reg)

                fpr_match = fpr_save_re.search(mnemonic)
                if fpr_match:
                    reg = fpr_match.group(1)
                    fnum = int(reg[1:])
                    if fnum >= 14:  # callee-saved FPR range
                        callee_fprs.add(reg)

            func.callee_saved_gprs = sorted(callee_gprs, key=lambda r: int(r[1:]), reverse=True)
            func.callee_saved_fprs = sorted(callee_fprs, key=lambda r: int(r[1:]), reverse=True)

            functions[current_mangled] = func
            in_function = False
            continue

        if in_function:
            current_raw.append(line)
            insn_match = insn_re.match(line)
            if insn_match:
                offset = int(insn_match.group(1), 16)
                hexbytes = insn_match.group(2)
                mnemonic = insn_match.group(3).strip()
                current_insns.append((offset, hexbytes, mnemonic))

    return functions


def diff_functions(a: FunctionAsm, b: FunctionAsm) -> FunctionDiff:
    """Compute structural diff between two function assemblies."""
    # Detect register swaps by normalizing instruction sequences
    reg_map = {}

    # Compare instruction-by-instruction
    insn_diffs = 0
    min_len = min(len(a.instructions), len(b.instructions))

    for i in range(min_len):
        _, _, ma = a.instructions[i]
        _, _, mb = b.instructions[i]
        if ma != mb:
            insn_diffs += 1
            # Try to detect register swaps
            _detect_reg_swap(ma, mb, reg_map)

    insn_diffs += abs(len(a.instructions) - len(b.instructions))

    # Check if it's purely a register swap (structural match)
    structural = True
    if len(a.instructions) != len(b.instructions):
        structural = False
    else:
        for i in range(len(a.instructions)):
            _, _, ma = a.instructions[i]
            _, _, mb = b.instructions[i]
            normalized_a = _apply_reg_map(ma, reg_map)
            if normalized_a != mb:
                structural = False
                break

    return FunctionDiff(
        register_swaps=reg_map,
        instruction_diffs=insn_diffs,
        structural_match=structural,
        prologue_changed=a.prologue_helper != b.prologue_helper,
        stack_size_changed=a.stack_frame_size != b.stack_frame_size,
        callee_saved_changed=(a.callee_saved_gprs != b.callee_saved_gprs or
                              a.callee_saved_fprs != b.callee_saved_fprs),
    )


def _detect_reg_swap(line_a: str, line_b: str, reg_map: dict):
    """Try to detect register swaps between two instruction lines."""
    reg_re = re.compile(r'\b(r\d+|f\d+|cr\d+)\b')
    regs_a = reg_re.findall(line_a)
    regs_b = reg_re.findall(line_b)

    if len(regs_a) == len(regs_b):
        for ra, rb in zip(regs_a, regs_b):
            if ra != rb:
                if ra in reg_map:
                    if reg_map[ra] != rb:
                        pass  # conflicting mapping
                else:
                    reg_map[ra] = rb


def _apply_reg_map(line: str, reg_map: dict) -> str:
    """Apply register swap map to an instruction line."""
    result = line
    for old, new in sorted(reg_map.items(), key=lambda x: len(x[0]), reverse=True):
        result = re.sub(r'\b' + re.escape(old) + r'\b', f'__{old}__', result)
    for old, new in sorted(reg_map.items(), key=lambda x: len(x[0]), reverse=True):
        result = result.replace(f'__{old}__', new)
    return result


def _demangle(name: str) -> str:
    """Simple MSVC demangling."""
    # Strip leading ? and extract basic name
    if name.startswith('?'):
        parts = name[1:].split('@')
        if len(parts) >= 2:
            return f"{parts[1]}::{parts[0]}" if parts[1] != '' else parts[0]
    return name


# =============================================================================
# Test Suites
# =============================================================================

def suite_regalloc_order() -> list[dict]:
    """Test how declaration order affects callee-saved register assignment."""
    print("\n=== Register Allocation Order ===\n")
    results = []

    # Test 1: Two variables, swap order
    variants = {
        "ab": "extern int get(int); extern void use(int,int);\n"
              "void test() { int a = get(0); int b = get(1); use(a,b); }",
        "ba": "extern int get(int); extern void use(int,int);\n"
              "void test() { int b = get(1); int a = get(0); use(a,b); }",
    }

    baseline_func = None
    for name, source in variants.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        test_func = None
        for mangled, func in funcs.items():
            if 'test' in mangled:
                test_func = func
                break

        if test_func:
            print(f"  Variant {name}:")
            print(f"    Callee-saved GPR: {test_func.callee_saved_gprs}")
            print(f"    Prologue: {test_func.prologue_helper}")
            print(f"    Instructions: {test_func.instruction_count}")
            print(f"    ASM hash: {test_func.asm_hash}")

            if baseline_func is None:
                baseline_func = test_func
            else:
                diff = diff_functions(baseline_func, test_func)
                print(f"    Diff vs baseline: {diff.instruction_diffs} insn changes, "
                      f"structural_match={diff.structural_match}")
                if diff.register_swaps:
                    print(f"    Register swaps: {diff.register_swaps}")

            results.append({
                "variant": name,
                "callee_saved_gprs": test_func.callee_saved_gprs,
                "callee_saved_fprs": test_func.callee_saved_fprs,
                "prologue": test_func.prologue_helper,
                "instructions": test_func.instruction_count,
                "asm_hash": test_func.asm_hash,
            })

    # Test 2: Scaling variable count (2 to 10)
    print("\n  --- Variable count scaling ---")
    for n in range(2, 11):
        decls = "; ".join(f"int v{i} = get({i})" for i in range(n))
        args = ", ".join(f"v{i}" for i in range(n))
        use_params = ", ".join(["int"] * n)
        source = (
            f"extern int get(int);\n"
            f"extern void use({use_params});\n"
            f"void test() {{ {decls}; use({args}); }}"
        )
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                print(f"  N={n}: callee_saved={func.callee_saved_gprs}, "
                      f"prologue={func.prologue_helper}, "
                      f"insns={func.instruction_count}")
                results.append({
                    "test": "var_count",
                    "n": n,
                    "callee_saved_gprs": func.callee_saved_gprs,
                    "prologue": func.prologue_helper,
                    "instructions": func.instruction_count,
                    "asm_hash": func.asm_hash,
                })
                break

    return results


def suite_bsf_threshold() -> list[dict]:
    """Test when graph coloring (BSF) kicks in vs linear scan."""
    print("\n=== BSF Graph Coloring Threshold ===\n")
    results = []

    # Generate functions with N variables that ALL need callee-saved regs
    # Each variable must survive across a call to force callee-saved allocation
    for n in range(3, 16):
        # Create N variables, each used across a function call boundary
        lines = []
        lines.append("extern int get(int);")
        lines.append("extern void sink(int);")
        lines.append("void test() {")
        for i in range(n):
            lines.append(f"    int v{i} = get({i});")
        # Use all variables after all are declared (forces all to be callee-saved)
        for i in range(n):
            lines.append(f"    sink(v{i});")
        lines.append("}")
        source = "\n".join(lines)

        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                # Check if assignment order matches declaration order
                gprs = func.callee_saved_gprs
                expected_order = [f"r{31 - i}" for i in range(len(gprs))]
                matches_linear = gprs == expected_order

                print(f"  N={n}: saved={gprs}, "
                      f"linear_order={'YES' if matches_linear else 'NO'}, "
                      f"prologue={func.prologue_helper}")

                results.append({
                    "n": n,
                    "callee_saved_gprs": gprs,
                    "linear_order": matches_linear,
                    "prologue": func.prologue_helper,
                    "instructions": func.instruction_count,
                    "asm_hash": func.asm_hash,
                })
                break

    return results


def suite_inline_threshold() -> list[dict]:
    """Find the exact inline threshold by varying function body size.

    MSVC always emits COMDAT function bodies even when inlined, so we
    detect inlining by checking if caller contains 'bl callee' (not inlined)
    or if caller's body grew to include callee's ops (inlined).

    Detection: if caller has a 'bl' or 'b' to callee mangled name, NOT inlined.
    """
    print("\n=== Inlining Threshold ===\n")
    results = []

    def _is_inlined(funcs):
        """Check if callee was inlined into caller by looking for bl/b callee."""
        caller_func = None
        callee_func = None
        callee_mangled = None
        for mangled, func in funcs.items():
            if 'caller' in mangled:
                caller_func = func
            if 'callee' in mangled:
                callee_func = func
                callee_mangled = mangled
        if not caller_func:
            return None, None, None

        # Check if caller has a branch to callee
        has_call = False
        if callee_mangled:
            for _, _, mnem in caller_func.instructions:
                if callee_mangled in mnem or ('callee' in mnem and ('bl ' in mnem or mnem.startswith('b '))):
                    has_call = True
                    break
        # Also check raw lines for bl/b to callee
        if not has_call:
            for line in caller_func.raw_lines:
                if 'callee' in line and (' bl ' in line or ' b ' in line):
                    has_call = True
                    break

        inlined = not has_call
        callee_insns = callee_func.instruction_count if callee_func else 0
        return inlined, caller_func, callee_insns

    # Arithmetic chain: each op = ~3 IL tuples. Range up to 60 to find threshold.
    print("  --- Arithmetic chain (add/mul alternating) ---")
    for n in list(range(1, 15)) + list(range(15, 65, 5)):
        ops = []
        for i in range(n):
            if i % 2 == 0:
                ops.append(f"    r = r + {'a' if i % 3 == 0 else 'b'};")
            else:
                ops.append(f"    r = r * {'b' if i % 3 == 0 else 'a'};")
        body = "\n".join(ops)
        source = f"""
int callee(int a, int b) {{
    int r = a;
{body}
    return r;
}}

extern void sink(int);
void caller(int a, int b) {{
    sink(callee(a, b));
}}
"""
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        inlined, caller_func, callee_insns = _is_inlined(funcs)

        if caller_func and inlined is not None:
            print(f"  N={n:3d} ops ({callee_insns:3d} callee insns): "
                  f"caller_insns={caller_func.instruction_count:3d}, "
                  f"inlined={'YES' if inlined else 'NO'}")
            results.append({
                "method": "arith_chain",
                "n": n,
                "callee_instructions": callee_insns,
                "caller_instructions": caller_func.instruction_count,
                "inlined": inlined,
            })

    # If/else chain: branch-heavy code (more IL than arithmetic)
    print("\n  --- If/else chain (branch-heavy) ---")
    for n in list(range(1, 10)) + list(range(10, 35, 5)):
        branches = []
        for i in range(n):
            branches.append(f"    if (a > {i}) r += b; else r -= {i+1};")
        body = "\n".join(branches)
        source = f"""
int callee(int a, int b) {{
    int r = 0;
{body}
    return r;
}}

extern void sink(int);
void caller(int a, int b) {{
    sink(callee(a, b));
}}
"""
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        inlined, caller_func, callee_insns = _is_inlined(funcs)

        if caller_func and inlined is not None:
            print(f"  N={n:3d} branches ({callee_insns:3d} callee insns): "
                  f"caller_insns={caller_func.instruction_count:3d}, "
                  f"inlined={'YES' if inlined else 'NO'}")
            results.append({
                "method": "if_else_chain",
                "n": n,
                "callee_instructions": callee_insns,
                "caller_instructions": caller_func.instruction_count,
                "inlined": inlined,
            })

    return results


def suite_peephole() -> list[dict]:
    """Test peephole pattern triggers."""
    print("\n=== Peephole Patterns ===\n")
    results = []

    # Test 1: NOR peephole (u8 XOR 0xFF)
    print("  --- NOR peephole ---")
    nor_tests = {
        "u8_xor_ff": (
            "extern unsigned char get_u8();\n"
            "extern void sink_u8(unsigned char);\n"
            "void test() { unsigned char x = get_u8(); sink_u8(x ^ 0xFF); }"
        ),
        "u32_xor_ff": (
            "extern unsigned int get_u32();\n"
            "extern void sink_u32(unsigned int);\n"
            "void test() { unsigned int x = get_u32(); sink_u32(x ^ 0xFF); }"
        ),
        "u8_widened_xor_ff": (
            "extern unsigned char get_u8();\n"
            "extern void sink_u32(unsigned int);\n"
            "void test() { unsigned char x = get_u8(); unsigned int w = x; sink_u32(w ^ 0xFF); }"
        ),
        "u8_not": (
            "extern unsigned char get_u8();\n"
            "extern void sink_u8(unsigned char);\n"
            "void test() { unsigned char x = get_u8(); sink_u8(~x); }"
        ),
    }

    for name, source in nor_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                has_nor = any('nor' in m.lower() for _, _, m in func.instructions)
                has_xori = any('xori' in m.lower() for _, _, m in func.instructions)
                insn_list = [m for _, _, m in func.instructions]
                print(f"  {name}: NOR={'YES' if has_nor else 'no'}, "
                      f"XORI={'YES' if has_xori else 'no'}")
                for _, _, m in func.instructions:
                    if any(op in m.lower() for op in ['nor', 'xor', 'not', 'clrlwi']):
                        print(f"    {m}")

                results.append({
                    "test": "nor",
                    "variant": name,
                    "has_nor": has_nor,
                    "has_xori": has_xori,
                    "instructions": insn_list,
                })
                break

    # Test 2: Boolean materialization
    print("\n  --- Boolean materialization ---")
    bool_tests = {
        "branch_and": (
            "extern bool get_a(); extern bool get_b();\n"
            "extern void sink(bool);\n"
            "void test() { bool a = get_a(); bool b = get_b(); sink(a && b); }"
        ),
        "branch_and_cast_bool": (
            "extern bool get_a(); extern int get_x();\n"
            "extern void sink(bool);\n"
            "void test() { bool a = get_a(); int x = get_x(); sink(a && (bool)(x > 1)); }"
        ),
        "bitwise_and": (
            "extern bool get_a(); extern bool get_b();\n"
            "extern void sink(bool);\n"
            "void test() { bool a = get_a(); bool b = get_b(); sink(a & b); }"
        ),
        "compare_gt1_no_cast": (
            "extern bool get_a(); extern int get_x();\n"
            "extern void sink(bool);\n"
            "void test() { bool a = get_a(); int x = get_x(); sink(a && x > 1); }"
        ),
    }

    for name, source in bool_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                has_subfc = any('subfc' in m for _, _, m in func.instructions)
                has_eqv = any('eqv' in m for _, _, m in func.instructions)
                has_branch = any(m.startswith('b') and 'bl' not in m[:3]
                                for _, _, m in func.instructions)
                branchless = has_subfc or has_eqv

                print(f"  {name}: branchless={'YES' if branchless else 'no'}, "
                      f"has_branch={'YES' if has_branch else 'no'}")
                # Show relevant instructions
                for _, _, m in func.instructions:
                    if any(op in m.lower() for op in ['subfc', 'eqv', 'srwi', 'addze',
                                                       'clrlwi', 'beq', 'bne', 'ble', 'bge',
                                                       'cmpwi', 'and.']):
                        print(f"    {m}")

                results.append({
                    "test": "bool_materialize",
                    "variant": name,
                    "branchless": branchless,
                    "has_subfc": has_subfc,
                    "has_eqv": has_eqv,
                    "has_branch": has_branch,
                    "instructions": [m for _, _, m in func.instructions],
                })
                break

    # Test 3: subf. fusion
    print("\n  --- subf. loop condition ---")
    subf_tests = {
        "cmpw_direct": (
            "extern int get_lo(); extern int get_hi(); extern void body(int);\n"
            "void test() { int lo = get_lo(); int hi = get_hi();\n"
            "  while (hi >= lo) { body(hi); hi--; } }"
        ),
        "subf_subtract": (
            "extern int get_lo(); extern int get_hi(); extern void body(int);\n"
            "void test() { int lo = get_lo(); int hi = get_hi();\n"
            "  while (hi - lo >= 0) { body(hi); hi--; } }"
        ),
    }

    for name, source in subf_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                has_subf_dot = any('subf.' in m and 'subfc' not in m
                                   for _, _, m in func.instructions)
                has_cmpw = any('cmpw' in m for _, _, m in func.instructions)

                print(f"  {name}: subf.={'YES' if has_subf_dot else 'no'}, "
                      f"cmpw={'YES' if has_cmpw else 'no'}")
                for _, _, m in func.instructions:
                    if any(op in m for op in ['subf.', 'cmpw', 'bge', 'blt', 'ble', 'bgt']):
                        print(f"    {m}")

                results.append({
                    "test": "subf_fusion",
                    "variant": name,
                    "has_subf_dot": has_subf_dot,
                    "has_cmpw": has_cmpw,
                    "instructions": [m for _, _, m in func.instructions],
                })
                break

    return results


def suite_branch_polarity() -> list[dict]:
    """Test branch polarity (beq vs bne) for if/else constructs."""
    print("\n=== Branch Polarity ===\n")
    results = []

    tests = {
        "if_else_eq": (
            "extern int get(); extern void do_a(); extern void do_b();\n"
            "void test() { if (get() == 0) { do_a(); } else { do_b(); } }"
        ),
        "if_else_ne": (
            "extern int get(); extern void do_a(); extern void do_b();\n"
            "void test() { if (get() != 0) { do_b(); } else { do_a(); } }"
        ),
        "early_return_eq": (
            "extern int get(); extern void do_a(); extern void do_b();\n"
            "void test() { if (get() == 0) { do_a(); return; } do_b(); }"
        ),
        "early_return_ne": (
            "extern int get(); extern void do_a(); extern void do_b();\n"
            "void test() { if (get() != 0) { do_b(); return; } do_a(); }"
        ),
        "nested_if": (
            "extern int get_x(); extern int get_y();\n"
            "extern void do_a(); extern void do_b(); extern void do_c();\n"
            "void test() {\n"
            "    if (get_x() == 0) {\n"
            "        if (get_y() == 0) { do_a(); } else { do_b(); }\n"
            "    } else { do_c(); }\n"
            "}"
        ),
    }

    for name, source in tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                branches = [(m, h) for _, h, m in func.instructions
                            if re.match(r'b(eq|ne|lt|gt|le|ge)', m)]
                print(f"  {name}:")
                for m, h in branches:
                    print(f"    {m}")

                results.append({
                    "variant": name,
                    "branches": [m for m, _ in branches],
                    "instructions": [m for _, _, m in func.instructions],
                })
                break

    return results


def suite_float_precision() -> list[dict]:
    """Test DOUBLETOSINGLE and float literal handling."""
    print("\n=== Float Precision ===\n")
    results = []

    tests = {
        "double_literal": (
            "extern void sink_f(float);\n"
            "void test() { float x = 0.001; sink_f(x); }"
        ),
        "float_literal": (
            "extern void sink_f(float);\n"
            "void test() { float x = 0.001f; sink_f(x); }"
        ),
        "static_const_float": (
            "extern void sink_f(float);\n"
            "void test() { static const float k = 0.001f; sink_f(k); }"
        ),
        "static_const_double_to_float": (
            "extern void sink_f(float);\n"
            "void test() { static const float k = 0.001; sink_f(k); }"
        ),
        "inline_100f": (
            "extern void sink_f(float);\n"
            "void test() { float x = 100.0f; sink_f(x); }"
        ),
        "static_const_100f": (
            "extern void sink_f(float);\n"
            "void test() { static const float k = 100.0f; sink_f(k); }"
        ),
    }

    for name, source in tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                has_lfs = any('lfs' in m for _, _, m in func.instructions)
                has_lfd = any('lfd' in m for _, _, m in func.instructions)
                has_frsp = any('frsp' in m for _, _, m in func.instructions)

                print(f"  {name}: lfs={'YES' if has_lfs else 'no'}, "
                      f"lfd={'YES' if has_lfd else 'no'}, "
                      f"frsp={'YES' if has_frsp else 'no'}")
                for _, _, m in func.instructions:
                    if any(op in m for op in ['lfs', 'lfd', 'frsp', 'stfs']):
                        print(f"    {m}")

                results.append({
                    "variant": name,
                    "has_lfs": has_lfs,
                    "has_lfd": has_lfd,
                    "has_frsp": has_frsp,
                    "callee_saved_gprs": func.callee_saved_gprs,
                    "callee_saved_fprs": func.callee_saved_fprs,
                    "instructions": [m for _, _, m in func.instructions],
                })
                break

    return results


def suite_bool_materialize() -> list[dict]:
    """Comprehensive boolean materialization test — maps source patterns to PPC sequences.

    Tests the 6 categories of boolean instruction selection:
    1. Zero tests (addic/subfe)
    2. Equality against non-zero constant (cntlzw or addic/subfe)
    3. Signed positive test (neg/andc/srwi)
    4. Unsigned ordered comparisons (subfic/subfe)
    5. Signed ordered comparisons (subfc/eqv/srwi/addze)
    6. No materialization (branch-based)
    """
    print("\n=== Boolean Materialization (Comprehensive) ===\n")
    results = []

    tests = {
        # Category 1: Zero tests
        "eq_zero_signed": "int test(int x) { return (x == 0) ? 1 : 0; }",
        "ne_zero_signed": "int test(int x) { return (x != 0) ? 1 : 0; }",
        "eq_zero_ptr": "int test(void* p) { return (p == 0) ? 1 : 0; }",
        "gt_zero_unsigned": "int test(unsigned x) { return (x > 0) ? 1 : 0; }",

        # Category 2: Equality against non-zero
        "eq_one": "int test(int x) { return (x == 1) ? 1 : 0; }",
        "ne_one": "int test(int x) { return (x != 1) ? 1 : 0; }",
        "eq_five": "int test(int x) { return (x == 5) ? 1 : 0; }",

        # Category 3: Signed positive test
        "gt_zero_signed": "int test(int x) { return (x > 0) ? 1 : 0; }",
        "gt_zero_arith": "int test(int x, int y) { return (x > 0) + (y > 0); }",

        # Category 4: Unsigned ordered
        "gt1_unsigned": "int test(unsigned x) { return (x > 1) ? 1 : 0; }",
        "ge2_unsigned": "int test(unsigned x) { return (x >= 2) ? 1 : 0; }",
        "lt2_unsigned": "int test(unsigned x) { return (x < 2) ? 1 : 0; }",
        "gt100_unsigned": "int test(unsigned x) { return (x > 100) ? 1 : 0; }",

        # Category 5: Signed ordered
        "gt1_signed": "int test(int x) { return (x > 1) ? 1 : 0; }",
        "ge2_signed": "int test(int x) { return (x >= 2) ? 1 : 0; }",
        "lt2_signed": "int test(int x) { return (x < 2) ? 1 : 0; }",
        "gt100_signed": "int test(int x) { return (x > 100) ? 1 : 0; }",
        "bool_cast": "int test(int x) { return (int)(bool)(x > 1); }",
        "bool_var": "bool test(int x) { return x > 1; }",

        # Category 6: Branch-based
        "branch_no_cast": (
            "extern int get_a(); extern int get_x();\n"
            "int test() { return get_a() && get_x() > 1; }"
        ),
    }

    # Key PPC instruction markers for each category
    markers = {
        'addic': 'addic',
        'subfe': 'subfe',
        'subfic': 'subfic',
        'subfze': 'subfze',
        'subfc': 'subfc',
        'eqv': 'eqv',
        'srwi': 'srwi',
        'addze': 'addze',
        'neg': 'neg',
        'andc': 'andc',
        'cntlzw': 'cntlzw',
        'rlwinm': 'rlwinm',
        'srawi': 'srawi',
        'adde': 'adde',
        'cmpwi': 'cmpwi',
    }

    for name, source in tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                # Detect which PPC instructions are present
                found = {}
                for mk, pattern in markers.items():
                    found[mk] = any(pattern in m for _, _, m in func.instructions)

                # Classify the pattern category
                if found['addic'] and found['subfe'] and not found['subfic']:
                    if found['cntlzw']:
                        category = "2-eq (cntlzw)"
                    else:
                        category = "1-zero or 2-ne"
                elif found['neg'] and found['andc']:
                    category = "3-sign_bit"
                elif found['subfic'] and found['subfe']:
                    category = "4-unsigned"
                elif found['subfc'] and found['eqv']:
                    category = "5-signed"
                elif found['srawi'] and found['adde']:
                    category = "5-signed_ge"
                elif found['subfc'] and found['subfze']:
                    category = "4-unsigned_ge"
                elif found['cmpwi']:
                    category = "6-branch"
                else:
                    category = "unknown"

                insn_list = [m.strip() for _, _, m in func.instructions
                             if not m.strip().startswith('blr')]

                print(f"  {name:25s} → {category:20s}  "
                      f"({func.instruction_count} insns)")
                # Show the key instructions
                for _, _, m in func.instructions:
                    m = m.strip()
                    if any(mk in m for mk in markers):
                        print(f"    {m}")

                results.append({
                    "variant": name,
                    "category": category,
                    "instruction_count": func.instruction_count,
                    "markers_found": {k: v for k, v in found.items() if v},
                    "instructions": [m.strip() for _, _, m in func.instructions],
                })
                break

    return results


def suite_rlwinm_fusion() -> list[dict]:
    """Test rlwinm fusion behavior: when does G5P10 fuse shift+mask into extrwi/clrlslwi?

    Discovery: ByteGrinder byte-rotation functions showed our compiler fusing
    separate srwi+clrlwi into extrwi, while the target uses separate instructions.
    Same compiler version — so the difference is in the source expression / IL.

    Hypothesis: u8 type causes c1xx to emit CAST(82 12 20) before SHR, which
    causes G5P10 to recognize the narrowed operand and fuse into rlwinm.
    u32/unsigned long with explicit mask defers the clrlwi, preventing fusion.
    """
    print("\n=== rlwinm Fusion ===\n")
    results = []

    # Group 1: Right shift — does operand type affect fusion?
    print("  --- Right shift fusion (srwi vs extrwi) ---")
    shift_tests = {
        # u8 variable: expect extrwi (fused shift+mask)
        "u8_shr2": (
            "extern unsigned char get_u8();\n"
            "extern void sink_u32(unsigned int);\n"
            "void test() { unsigned char b = get_u8(); sink_u32(b >> 2); }"
        ),
        # u32 variable with mask before shift: expect srwi (separate)
        "u32_mask_shr2": (
            "extern unsigned int get_u32();\n"
            "extern void sink_u32(unsigned int);\n"
            "void test() { unsigned int v = get_u32() & 0xFF; sink_u32(v >> 2); }"
        ),
        # u32 variable with mask after shift: expect srwi + clrlwi
        "u32_shr2_mask": (
            "extern unsigned int get_u32();\n"
            "extern void sink_u32(unsigned int);\n"
            "void test() { unsigned int v = get_u32(); sink_u32((v >> 2) & 0x3F); }"
        ),
        # unsigned long: same as u32?
        "ulong_mask_shr2": (
            "extern unsigned long get_ulong();\n"
            "extern void sink_u32(unsigned int);\n"
            "void test() { unsigned long v = get_ulong() & 0xFF; sink_u32(v >> 2); }"
        ),
        # u8 cast at use site (not at declaration)
        "u32_cast_u8_shr2": (
            "extern unsigned int get_u32();\n"
            "extern void sink_u32(unsigned int);\n"
            "void test() { unsigned int v = get_u32(); sink_u32((unsigned char)v >> 2); }"
        ),
        # int (signed) with mask: does signedness affect fusion?
        "int_mask_shr2": (
            "extern int get_int();\n"
            "extern void sink_int(int);\n"
            "void test() { int v = get_int() & 0xFF; sink_int(v >> 2); }"
        ),
    }

    for name, source in shift_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                has_extrwi = any('extrwi' in m.lower() for _, _, m in func.instructions)
                has_srwi = any('srwi' in m.lower() for _, _, m in func.instructions)
                has_clrlwi = any('clrlwi' in m.lower() for _, _, m in func.instructions)
                has_rlwinm = any('rlwinm' in m.lower() for _, _, m in func.instructions)
                insn_list = [m.strip() for _, _, m in func.instructions]
                fused = "FUSED" if has_extrwi or has_rlwinm else "SEPARATE"
                print(f"  {name}: {fused} "
                      f"(extrwi={has_extrwi}, srwi={has_srwi}, "
                      f"clrlwi={has_clrlwi}, rlwinm={has_rlwinm})")
                for _, _, m in func.instructions:
                    m = m.strip()
                    if any(op in m.lower() for op in
                           ['extrwi', 'srwi', 'slwi', 'clrlwi', 'clrlslwi', 'rlwinm']):
                        print(f"    {m}")
                results.append({
                    "test": "right_shift_fusion",
                    "variant": name,
                    "has_extrwi": has_extrwi,
                    "has_srwi": has_srwi,
                    "has_clrlwi": has_clrlwi,
                    "has_rlwinm": has_rlwinm,
                    "fused": has_extrwi or has_rlwinm,
                    "instructions": insn_list,
                })
                break

    # Group 2: Left shift — does operand type affect clrlslwi fusion?
    print("\n  --- Left shift fusion (slwi vs clrlslwi) ---")
    lshift_tests = {
        "u8_shl6": (
            "extern unsigned char get_u8();\n"
            "extern void sink_u32(unsigned int);\n"
            "void test() { unsigned char b = get_u8(); sink_u32(b << 6); }"
        ),
        "u32_mask_shl6": (
            "extern unsigned int get_u32();\n"
            "extern void sink_u32(unsigned int);\n"
            "void test() { unsigned int v = get_u32() & 0xFF; sink_u32(v << 6); }"
        ),
        "u32_shl6_mask": (
            "extern unsigned int get_u32();\n"
            "extern void sink_u32(unsigned int);\n"
            "void test() { unsigned int v = get_u32(); sink_u32((v << 6) & 0xFF); }"
        ),
    }

    for name, source in lshift_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                has_clrlslwi = any('clrlslwi' in m.lower() for _, _, m in func.instructions)
                has_slwi = any('slwi' in m.lower() for _, _, m in func.instructions)
                has_clrlwi = any('clrlwi' in m.lower() for _, _, m in func.instructions)
                has_rlwinm = any('rlwinm' in m.lower() for _, _, m in func.instructions)
                insn_list = [m.strip() for _, _, m in func.instructions]
                fused = "FUSED" if has_clrlslwi or has_rlwinm else "SEPARATE"
                print(f"  {name}: {fused} "
                      f"(clrlslwi={has_clrlslwi}, slwi={has_slwi}, "
                      f"clrlwi={has_clrlwi}, rlwinm={has_rlwinm})")
                for _, _, m in func.instructions:
                    m = m.strip()
                    if any(op in m.lower() for op in
                           ['extrwi', 'srwi', 'slwi', 'clrlwi', 'clrlslwi', 'rlwinm']):
                        print(f"    {m}")
                results.append({
                    "test": "left_shift_fusion",
                    "variant": name,
                    "has_clrlslwi": has_clrlslwi,
                    "has_slwi": has_slwi,
                    "has_clrlwi": has_clrlwi,
                    "has_rlwinm": has_rlwinm,
                    "fused": has_clrlslwi or has_rlwinm,
                    "instructions": insn_list,
                })
                break

    # Group 3: Rotation pattern — u8 vs u32 decomposition
    print("\n  --- Rotation decomposition (u8 vs u32) ---")
    rot_tests = {
        # u8 rotation: both shifts on u8 value
        "u8_rot2": (
            "extern unsigned char get_u8();\n"
            "extern void sink_u8(unsigned char);\n"
            "void test() { unsigned char b = get_u8(); "
            "sink_u8((b >> 2) | (b << 6)); }"
        ),
        # u32 rotation with mask
        "u32_rot2": (
            "extern unsigned int get_u32();\n"
            "extern void sink_u32(unsigned int);\n"
            "void test() { unsigned int v = get_u32() & 0xFF; "
            "unsigned int r = (v >> 2) | (v << 6); sink_u32(r & 0xFF); }"
        ),
        # unsigned long rotation
        "ulong_rot2": (
            "extern unsigned long get_ulong();\n"
            "extern void sink_u32(unsigned int);\n"
            "void test() { unsigned long v = (unsigned char)get_ulong(); "
            "unsigned long r = (v >> 2) | (v << 6); sink_u32((unsigned char)r); }"
        ),
    }

    for name, source in rot_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                has_extrwi = any('extrwi' in m.lower() for _, _, m in func.instructions)
                has_clrlslwi = any('clrlslwi' in m.lower() for _, _, m in func.instructions)
                has_srwi = any('srwi' in m.lower() for _, _, m in func.instructions)
                has_slwi = any('slwi' in m.lower() for _, _, m in func.instructions)
                has_rlwinm = any('rlwinm' in m.lower() for _, _, m in func.instructions)
                insn_list = [m.strip() for _, _, m in func.instructions]
                fused_count = sum([has_extrwi, has_clrlslwi, has_rlwinm])
                separate_count = sum([has_srwi, has_slwi])
                print(f"  {name}: fused_ops={fused_count} separate_ops={separate_count}")
                for _, _, m in func.instructions:
                    m = m.strip()
                    if any(op in m.lower() for op in
                           ['extrwi', 'srwi', 'slwi', 'clrlwi', 'clrlslwi', 'rlwinm']):
                        print(f"    {m}")
                results.append({
                    "test": "rotation",
                    "variant": name,
                    "has_extrwi": has_extrwi,
                    "has_clrlslwi": has_clrlslwi,
                    "has_srwi": has_srwi,
                    "has_slwi": has_slwi,
                    "has_rlwinm": has_rlwinm,
                    "fused_ops": fused_count,
                    "separate_ops": separate_count,
                    "instructions": insn_list,
                })
                break

    # Group 4: u8 mask placement — where does clrlwi appear?
    print("\n  --- u8 mask placement (early vs late) ---")
    mask_tests = {
        # u8 declaration: mask at assignment
        "u8_decl_xor": (
            "extern unsigned char get_u8a();\n"
            "extern unsigned char get_u8b();\n"
            "extern void sink_u8(unsigned char);\n"
            "void test() { unsigned char a = get_u8a(); unsigned char b = get_u8b(); "
            "sink_u8(a ^ b); }"
        ),
        # u32 declaration: mask at end
        "u32_decl_xor": (
            "extern unsigned int get_u32a();\n"
            "extern unsigned int get_u32b();\n"
            "extern void sink_u8(unsigned char);\n"
            "void test() { unsigned int a = get_u32a(); unsigned int b = get_u32b(); "
            "sink_u8((unsigned char)(a ^ b)); }"
        ),
        # unsigned long declaration: mask at end
        "ulong_decl_xor": (
            "extern unsigned long get_ula();\n"
            "extern unsigned long get_ulb();\n"
            "extern void sink_u8(unsigned char);\n"
            "void test() { unsigned long a = get_ula(); unsigned long b = get_ulb(); "
            "sink_u8((unsigned char)(a ^ b)); }"
        ),
    }

    for name, source in mask_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                insn_list = [m.strip() for _, _, m in func.instructions]
                clrlwi_positions = [i for i, (_, _, m) in enumerate(func.instructions)
                                    if 'clrlwi' in m.lower()]
                xor_positions = [i for i, (_, _, m) in enumerate(func.instructions)
                                 if 'xor' in m.lower() and 'xori' not in m.lower()]
                mask_before_xor = any(c < x for c in clrlwi_positions for x in xor_positions)
                mask_after_xor = any(c > x for c in clrlwi_positions for x in xor_positions)
                placement = "EARLY" if mask_before_xor and not mask_after_xor else \
                            "LATE" if mask_after_xor and not mask_before_xor else \
                            "BOTH" if mask_before_xor and mask_after_xor else "NONE"
                print(f"  {name}: mask={placement} "
                      f"(clrlwi@{clrlwi_positions}, xor@{xor_positions})")
                for _, _, m in func.instructions:
                    m = m.strip()
                    if any(op in m.lower() for op in ['clrlwi', 'xor', 'mr']):
                        print(f"    {m}")
                results.append({
                    "test": "mask_placement",
                    "variant": name,
                    "clrlwi_positions": clrlwi_positions,
                    "xor_positions": xor_positions,
                    "mask_placement": placement,
                    "instructions": insn_list,
                })
                break

    return results


def suite_fpr_allocation() -> list[dict]:
    """Test FPR allocation interaction with GPR allocation.

    Questions:
    - Do FPR callee-saved regs follow the same r31-first pattern as GPR?
    - Does mixing float and int variables affect GPR assignment?
    - Does float parameter position affect FPR allocation?
    """
    print("\n=== FPR Allocation Interaction ===\n")
    results = []

    # Test 1: Pure FPR allocation — does f31 get first float?
    print("  --- Pure FPR allocation (scaling float count) ---")
    for n in range(1, 8):
        gets = "; ".join(f"float f{i} = getf({i})" for i in range(n))
        args = ", ".join(f"f{i}" for i in range(n))
        params = ", ".join(["float"] * n)
        source = (
            f"extern float getf(int);\n"
            f"extern void sinkf({params});\n"
            f"void test() {{ {gets}; sinkf({args}); }}"
        )
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                print(f"  N={n}: FPR={func.callee_saved_fprs}, "
                      f"GPR={func.callee_saved_gprs}, "
                      f"prologue={func.prologue_helper}")
                results.append({
                    "test": "pure_fpr_scaling",
                    "n": n,
                    "callee_saved_fprs": func.callee_saved_fprs,
                    "callee_saved_gprs": func.callee_saved_gprs,
                    "prologue": func.prologue_helper,
                    "instructions": func.instruction_count,
                })
                break

    # Test 2: Mixed int+float — does adding floats shift GPR assignments?
    print("\n  --- Mixed int+float variables ---")
    mixed_tests = {
        "2int": (
            "extern int geti(int); extern void sink2(int, int);\n"
            "void test() { int a = geti(0); int b = geti(1); sink2(a, b); }"
        ),
        "2int_1float": (
            "extern int geti(int); extern float getf(int);\n"
            "extern void sink3(int, int, float);\n"
            "void test() { int a = geti(0); int b = geti(1); "
            "float c = getf(2); sink3(a, b, c); }"
        ),
        "1float_2int": (
            "extern int geti(int); extern float getf(int);\n"
            "extern void sink3(float, int, int);\n"
            "void test() { float c = getf(0); int a = geti(1); "
            "int b = geti(2); sink3(c, a, b); }"
        ),
        "interleaved": (
            "extern int geti(int); extern float getf(int);\n"
            "extern void sink4(int, float, int, float);\n"
            "void test() { int a = geti(0); float f1 = getf(1); "
            "int b = geti(2); float f2 = getf(3); sink4(a, f1, b, f2); }"
        ),
    }

    baseline_func = None
    for name, source in mixed_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                print(f"  {name}: GPR={func.callee_saved_gprs} "
                      f"FPR={func.callee_saved_fprs}")
                if baseline_func and name != "2int":
                    diff = diff_functions(baseline_func, func)
                    if diff.register_swaps:
                        print(f"    Swaps vs 2int: {diff.register_swaps}")
                if name == "2int":
                    baseline_func = func
                results.append({
                    "test": "mixed_int_float",
                    "variant": name,
                    "callee_saved_gprs": func.callee_saved_gprs,
                    "callee_saved_fprs": func.callee_saved_fprs,
                    "instructions": func.instruction_count,
                    "asm_hash": func.asm_hash,
                })
                break

    # Test 3: Float parameter vs local — does FPR assignment differ?
    print("\n  --- Float as parameter vs local ---")
    param_tests = {
        "float_param": (
            "extern float getf(int); extern void sinkf2(float, float);\n"
            "void test(float p) { float a = getf(0); sinkf2(p, a); }"
        ),
        "float_local": (
            "extern float getf(int); extern void sinkf2(float, float);\n"
            "void test() { float p = getf(0); float a = getf(1); sinkf2(p, a); }"
        ),
    }

    for name, source in param_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                print(f"  {name}: FPR={func.callee_saved_fprs} "
                      f"GPR={func.callee_saved_gprs}")
                results.append({
                    "test": "float_param_vs_local",
                    "variant": name,
                    "callee_saved_fprs": func.callee_saved_fprs,
                    "callee_saved_gprs": func.callee_saved_gprs,
                    "instructions": func.instruction_count,
                })
                break

    return results


def suite_template_signedness() -> list[dict]:
    """Test template-instantiation signedness — signed vs unsigned type propagation.

    Questions:
    - Do template<typename T> functions get different codegen for signed vs unsigned?
    - Does integer promotion inside templates follow the same rules as direct code?
    - Does explicit cast inside template differ from implicit promotion?
    """
    print("\n=== Template-Instantiation Signedness ===\n")
    results = []

    # Test 1: Simple comparison template — signed vs unsigned instantiation
    print("  --- Template comparison: signed vs unsigned instantiation ---")
    template_tests = {
        "int_gt": (
            "template<typename T> int cmp(T a, T b) { return (a > b) ? 1 : 0; }\n"
            "extern int get_int(); extern void sink(int);\n"
            "void test() { sink(cmp<int>(get_int(), get_int())); }"
        ),
        "uint_gt": (
            "template<typename T> int cmp(T a, T b) { return (a > b) ? 1 : 0; }\n"
            "extern unsigned get_uint(); extern void sink(int);\n"
            "void test() { sink(cmp<unsigned>(get_uint(), get_uint())); }"
        ),
        "short_gt": (
            "template<typename T> int cmp(T a, T b) { return (a > b) ? 1 : 0; }\n"
            "extern short get_short(); extern void sink(int);\n"
            "void test() { sink(cmp<short>(get_short(), get_short())); }"
        ),
        "ushort_gt": (
            "template<typename T> int cmp(T a, T b) { return (a > b) ? 1 : 0; }\n"
            "extern unsigned short get_ushort(); extern void sink(int);\n"
            "void test() { sink(cmp<unsigned short>(get_ushort(), get_ushort())); }"
        ),
    }

    for name, source in template_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                # Look for signed vs unsigned comparison instructions
                has_cmpw = any('cmpw ' in m for _, _, m in func.instructions)
                has_cmplw = any('cmplw' in m for _, _, m in func.instructions)
                has_subfic = any('subfic' in m for _, _, m in func.instructions)
                has_subfc = any('subfc' in m for _, _, m in func.instructions)
                insns = [m.strip() for _, _, m in func.instructions]
                comparison = "signed(cmpw)" if has_cmpw else (
                    "unsigned(cmplw)" if has_cmplw else "other")
                print(f"  {name:12s}: comparison={comparison:20s} "
                      f"subfic={has_subfic} subfc={has_subfc} "
                      f"({func.instruction_count} insns)")
                results.append({
                    "test": "template_comparison",
                    "variant": name,
                    "has_cmpw": has_cmpw,
                    "has_cmplw": has_cmplw,
                    "has_subfic": has_subfic,
                    "has_subfc": has_subfc,
                    "instruction_count": func.instruction_count,
                    "instructions": insns,
                })
                break

    # Test 2: Template arithmetic — does widening differ?
    print("\n  --- Template arithmetic: u8/u16/u32 widening behavior ---")
    arith_tests = {
        "u8_add": (
            "template<typename T> T add(T a, T b) { return a + b; }\n"
            "extern unsigned char get_u8(); extern void sink_u8(unsigned char);\n"
            "void test() { sink_u8(add<unsigned char>(get_u8(), get_u8())); }"
        ),
        "u16_add": (
            "template<typename T> T add(T a, T b) { return a + b; }\n"
            "extern unsigned short get_u16(); extern void sink_u16(unsigned short);\n"
            "void test() { sink_u16(add<unsigned short>(get_u16(), get_u16())); }"
        ),
        "u32_add": (
            "template<typename T> T add(T a, T b) { return a + b; }\n"
            "extern unsigned int get_u32(); extern void sink_u32(unsigned int);\n"
            "void test() { sink_u32(add<unsigned int>(get_u32(), get_u32())); }"
        ),
        "int_add": (
            "template<typename T> T add(T a, T b) { return a + b; }\n"
            "extern int get_int(); extern void sink_int(int);\n"
            "void test() { sink_int(add<int>(get_int(), get_int())); }"
        ),
    }

    for name, source in arith_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                has_clrlwi = any('clrlwi' in m for _, _, m in func.instructions)
                has_extsh = any('extsh' in m for _, _, m in func.instructions)
                insns = [m.strip() for _, _, m in func.instructions]
                print(f"  {name:12s}: clrlwi={has_clrlwi} extsh={has_extsh} "
                      f"({func.instruction_count} insns)")
                results.append({
                    "test": "template_arithmetic",
                    "variant": name,
                    "has_clrlwi": has_clrlwi,
                    "has_extsh": has_extsh,
                    "instruction_count": func.instruction_count,
                    "instructions": insns,
                })
                break

    return results


def suite_cross_call_live_range() -> list[dict]:
    """Test cross-call live-range behavior — when variables survive across calls.

    Questions:
    - Does splitting a live range across more calls increase callee-saved pressure?
    - Does reordering uses around calls change allocation?
    - Does adding a dead variable (unused after call) still consume a callee-saved reg?
    """
    print("\n=== Cross-Call Live Range ===\n")
    results = []

    # Test 1: Variable surviving across N calls
    print("  --- Variable surviving across N calls ---")
    for n in range(1, 7):
        calls = "; ".join(f"call({i})" for i in range(n))
        source = (
            f"extern int getval();\n"
            f"extern void call(int);\n"
            f"extern void use(int);\n"
            f"void test() {{ int v = getval(); {calls}; use(v); }}"
        )
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                print(f"  N={n} calls: GPR={func.callee_saved_gprs} "
                      f"insns={func.instruction_count}")
                results.append({
                    "test": "cross_call_scaling",
                    "n_calls": n,
                    "callee_saved_gprs": func.callee_saved_gprs,
                    "instruction_count": func.instruction_count,
                    "prologue": func.prologue_helper,
                })
                break

    # Test 2: Dead variable after call — does it still get callee-saved?
    print("\n  --- Dead variable after call (not used after) ---")
    dead_tests = {
        "used_after": (
            "extern int getval(); extern void call(); extern void use(int);\n"
            "void test() { int v = getval(); call(); use(v); }"
        ),
        "not_used_after": (
            "extern int getval(); extern void call(); extern void use(int);\n"
            "void test() { int v = getval(); use(v); call(); }"
        ),
        "dead_after_call": (
            "extern int getval(); extern void call(); extern void use(int);\n"
            "void test() { int v = getval(); call(); }"
        ),
    }

    for name, source in dead_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                print(f"  {name}: GPR={func.callee_saved_gprs} "
                      f"insns={func.instruction_count}")
                results.append({
                    "test": "dead_variable",
                    "variant": name,
                    "callee_saved_gprs": func.callee_saved_gprs,
                    "instruction_count": func.instruction_count,
                })
                break

    # Test 3: Two variables, one used early, one late
    print("\n  --- Two variables: early vs late use ---")
    order_tests = {
        "a_early_b_late": (
            "extern int geta(); extern int getb();\n"
            "extern void call(); extern void use(int, int);\n"
            "void test() { int a = geta(); int b = getb(); "
            "call(); use(a, b); }"
        ),
        "b_early_a_late": (
            "extern int geta(); extern int getb();\n"
            "extern void call(); extern void use(int, int);\n"
            "void test() { int b = getb(); int a = geta(); "
            "call(); use(a, b); }"
        ),
        "interleaved_calls": (
            "extern int geta(); extern int getb();\n"
            "extern void call1(); extern void call2();\n"
            "extern void use(int, int);\n"
            "void test() { int a = geta(); call1(); "
            "int b = getb(); call2(); use(a, b); }"
        ),
    }

    baseline_func = None
    for name, source in order_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                print(f"  {name}: GPR={func.callee_saved_gprs} "
                      f"hash={func.asm_hash}")
                if baseline_func is None:
                    baseline_func = func
                else:
                    diff = diff_functions(baseline_func, func)
                    if diff.register_swaps:
                        print(f"    Swaps: {diff.register_swaps}")
                    if diff.instruction_diffs > 0:
                        print(f"    Diffs: {diff.instruction_diffs}")
                results.append({
                    "test": "use_order",
                    "variant": name,
                    "callee_saved_gprs": func.callee_saved_gprs,
                    "instruction_count": func.instruction_count,
                    "asm_hash": func.asm_hash,
                })
                break

    return results


def suite_scope_nesting() -> list[dict]:
    """Test scope nesting — how nested blocks affect register allocation and code layout.

    Questions:
    - Does declaring a variable in an inner scope use fewer callee-saved regs?
    - Does adding a nested scope around code that's already there change codegen?
    - Does deep nesting (3+ levels) trigger different allocation strategies?
    """
    print("\n=== Scope Nesting ===\n")
    results = []

    # Test 1: Variable in outer scope vs inner scope
    print("  --- Variable in outer vs inner scope ---")
    scope_tests = {
        "outer_scope": (
            "extern int get(); extern void call(int); extern void use(int);\n"
            "void test() { int v = get(); call(0); use(v); }"
        ),
        "inner_scope": (
            "extern int get(); extern void call(int); extern void use(int);\n"
            "void test() { { int v = get(); call(0); use(v); } }"
        ),
        "split_inner": (
            "extern int get(); extern void call(int); extern void use(int);\n"
            "void test() { { int v = get(); use(v); } call(0); }"
        ),
    }

    for name, source in scope_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                print(f"  {name}: GPR={func.callee_saved_gprs} "
                      f"insns={func.instruction_count} hash={func.asm_hash}")
                results.append({
                    "test": "scope_placement",
                    "variant": name,
                    "callee_saved_gprs": func.callee_saved_gprs,
                    "instruction_count": func.instruction_count,
                    "asm_hash": func.asm_hash,
                })
                break

    # Test 2: Nesting depth 0-4
    print("\n  --- Nesting depth scaling ---")
    for depth in range(5):
        open_braces = " { " * depth
        close_braces = " } " * depth
        source = (
            f"extern int get(); extern void call(int); extern void use(int);\n"
            f"void test() {{{open_braces} int v = get(); call(0); use(v); "
            f"{close_braces}}}"
        )
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                print(f"  depth={depth}: GPR={func.callee_saved_gprs} "
                      f"insns={func.instruction_count} hash={func.asm_hash}")
                results.append({
                    "test": "nesting_depth",
                    "depth": depth,
                    "callee_saved_gprs": func.callee_saved_gprs,
                    "instruction_count": func.instruction_count,
                    "asm_hash": func.asm_hash,
                })
                break

    # Test 3: Disjoint scopes reusing same register slot
    print("\n  --- Disjoint scopes — can compiler reuse callee-saved slot? ---")
    disjoint_tests = {
        "sequential_scopes": (
            "extern int get(int); extern void call(); extern void use(int);\n"
            "void test() { "
            "{ int a = get(0); call(); use(a); } "
            "{ int b = get(1); call(); use(b); } "
            "}"
        ),
        "merged_scope": (
            "extern int get(int); extern void call(); extern void use(int);\n"
            "void test() { "
            "int a = get(0); call(); use(a); "
            "int b = get(1); call(); use(b); "
            "}"
        ),
        "three_disjoint": (
            "extern int get(int); extern void call(); extern void use(int);\n"
            "void test() { "
            "{ int a = get(0); call(); use(a); } "
            "{ int b = get(1); call(); use(b); } "
            "{ int c = get(2); call(); use(c); } "
            "}"
        ),
    }

    for name, source in disjoint_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                print(f"  {name}: GPR={func.callee_saved_gprs} "
                      f"insns={func.instruction_count} "
                      f"stack={func.stack_frame_size}")
                results.append({
                    "test": "disjoint_scopes",
                    "variant": name,
                    "callee_saved_gprs": func.callee_saved_gprs,
                    "instruction_count": func.instruction_count,
                    "stack_frame_size": func.stack_frame_size,
                    "asm_hash": func.asm_hash,
                })
                break

    return results


def suite_static_local_guard() -> list[dict]:
    """Test static local guard behavior — ??_B combined vs $S separate guards.

    Known issue: target compiler emits one combined ??_B guard for multiple
    static locals; our compiler emits separate $S guards. This suite measures
    the codegen difference and tests edge cases.

    Questions:
    - How does the guard pattern differ for 1 vs 2 vs 3 static locals?
    - Does putting statics in an if-branch vs outer scope change guard layout?
    - Does static const vs static non-const differ?
    """
    print("\n=== Static Local Guard ===\n")
    results = []

    # Test 1: Counting static local guard instructions for N statics
    print("  --- Guard pattern: 1-3 static locals ---")
    for n in range(1, 4):
        inits = "; ".join(
            f'static int s{i} = get({i})' for i in range(n)
        )
        uses = " + ".join(f"s{i}" for i in range(n))
        source = (
            f"extern int get(int);\n"
            f"void test() {{ {inits}; volatile int sink = {uses}; }}"
        )
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                # Count guard-related instructions: lwz/lbz for guard, beq/bne branching
                guard_loads = sum(1 for _, _, m in func.instructions
                                 if 'lbz' in m or ('lwz' in m and '$S' in m))
                guard_branches = sum(1 for _, _, m in func.instructions
                                     if 'bne' in m or 'beq' in m)
                insns = [m.strip() for _, _, m in func.instructions]
                print(f"  N={n}: guard_loads={guard_loads} branches={guard_branches} "
                      f"total_insns={func.instruction_count}")
                results.append({
                    "test": "static_count",
                    "n_statics": n,
                    "guard_loads": guard_loads,
                    "guard_branches": guard_branches,
                    "instruction_count": func.instruction_count,
                    "instructions": insns,
                })
                break

    # Test 2: Static in if-branch vs outer scope
    print("\n  --- Static in if-branch vs outer scope ---")
    branch_tests = {
        "outer_scope": (
            "extern int get(int); extern bool cond();\n"
            "void test() { static int s = get(0); "
            "if (cond()) { volatile int sink = s; } }"
        ),
        "if_branch": (
            "extern int get(int); extern bool cond();\n"
            "void test() { if (cond()) { static int s = get(0); "
            "volatile int sink = s; } }"
        ),
        "two_in_if": (
            "extern int get(int); extern bool cond();\n"
            "void test() { if (cond()) { static int s1 = get(0); "
            "static int s2 = get(1); volatile int sink = s1 + s2; } }"
        ),
    }

    for name, source in branch_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                insns = [m.strip() for _, _, m in func.instructions]
                print(f"  {name}: insns={func.instruction_count} "
                      f"hash={func.asm_hash}")
                # Show guard-related lines
                for _, _, m in func.instructions:
                    m = m.strip()
                    if any(g in m.lower() for g in ['lbz', 'stb', 'bne', 'beq']):
                        print(f"    {m}")
                results.append({
                    "test": "static_branch_placement",
                    "variant": name,
                    "instruction_count": func.instruction_count,
                    "instructions": insns,
                    "asm_hash": func.asm_hash,
                })
                break

    # Test 3: static const vs static non-const
    print("\n  --- static const vs non-const ---")
    const_tests = {
        "static_nonconst": (
            "extern int get();\n"
            "void test() { static int s = get(); volatile int sink = s; }"
        ),
        "static_const": (
            "extern int get();\n"
            "void test() { static const int s = get(); volatile int sink = s; }"
        ),
        "static_const_literal": (
            "void test() { static const int s = 42; volatile int sink = s; }"
        ),
    }

    for name, source in const_tests.items():
        listing = compile_source(source)
        if not listing:
            continue
        funcs = parse_asm_listing(listing)
        for mangled, func in funcs.items():
            if 'test' in mangled:
                insns = [m.strip() for _, _, m in func.instructions]
                has_guard = any('lbz' in m or '$S' in m
                               for _, _, m in func.instructions)
                print(f"  {name}: has_guard={has_guard} "
                      f"insns={func.instruction_count}")
                results.append({
                    "test": "static_const",
                    "variant": name,
                    "has_guard": has_guard,
                    "instruction_count": func.instruction_count,
                    "instructions": insns,
                })
                break

    return results


# =============================================================================
# Main
# =============================================================================

SUITES = {
    "regalloc": suite_regalloc_order,
    "bsf_threshold": suite_bsf_threshold,
    "inline_threshold": suite_inline_threshold,
    "peephole": suite_peephole,
    "branch_polarity": suite_branch_polarity,
    "float_precision": suite_float_precision,
    "bool_materialize": suite_bool_materialize,
    "rlwinm_fusion": suite_rlwinm_fusion,
    "fpr_allocation": suite_fpr_allocation,
    "template_signedness": suite_template_signedness,
    "cross_call_live_range": suite_cross_call_live_range,
    "scope_nesting": suite_scope_nesting,
    "static_local_guard": suite_static_local_guard,
}


def main():
    parser = argparse.ArgumentParser(description="Differential testing for MSVC PPC compiler")
    parser.add_argument("--suite", required=True, choices=list(SUITES.keys()) + ["all"],
                        help="Test suite to run")
    parser.add_argument("--output", type=str, default=None,
                        help="Output JSON file (default: msvc-src/results/<suite>.json)")
    args = parser.parse_args()

    # Verify toolchain
    if not WIBO.exists():
        print(f"ERROR: wibo not found at {WIBO}", file=sys.stderr)
        sys.exit(1)
    if not CL_EXE.exists():
        print(f"ERROR: cl.exe not found at {CL_EXE}", file=sys.stderr)
        sys.exit(1)

    print(f"MSVC PPC Compiler Differential Testing")
    print(f"Compiler: {CL_EXE}")
    print(f"Wibo: {WIBO}")

    suites_to_run = list(SUITES.keys()) if args.suite == "all" else [args.suite]

    all_results = {}
    for suite_name in suites_to_run:
        results = SUITES[suite_name]()
        all_results[suite_name] = results

    # Save results
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    output_path = args.output or str(RESULTS_DIR / f"{args.suite}.json")
    with open(output_path, "w") as f:
        json.dump(all_results, f, indent=2)
    print(f"\nResults saved to {output_path}")


if __name__ == "__main__":
    main()
