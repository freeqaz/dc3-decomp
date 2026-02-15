#!/usr/bin/env python3
"""Batch pattern scanner for encoding mismatches across the decomp.

Scans all functions in a match% range for known fixable instruction patterns:
  - extrwi vs rlwinm encoding (bool type fix)
  - subic/subfe vs cntlzw/extrwi (boolean negation encoding)
  - clrlwi bool mask (extra truncation instruction)
  - comparison encoding (> 0 vs != 0)

Usage:
    python scripts/batch_pattern_scan.py [--min 90] [--max 99.9] [--limit 200]
    python scripts/batch_pattern_scan.py --unit 'src/system/*' --min 95
    python scripts/batch_pattern_scan.py --pattern extrwi  # filter by pattern type
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

PROJECT_DIR = Path(__file__).resolve().parent.parent
OBJDIFF_CLI = PROJECT_DIR / "bin" / "objdiff-cli"
REPORT_JSON = PROJECT_DIR / "build" / "373307D9" / "report.json"


@dataclass
class PatternHit:
    """A detected encoding pattern in a function."""
    pattern_type: str       # e.g. "extrwi_rlwinm", "bool_negate", "bool_mask", "cmp_encoding"
    indices: list[int]      # instruction indices involved
    target_instrs: list[str]  # target instruction text
    base_instrs: list[str]    # base (our) instruction text
    description: str        # human-readable description
    fixable: bool = True    # whether this is likely fixable from source


@dataclass
class FunctionScan:
    """Scan results for a single function."""
    symbol: str
    demangled: str
    unit: str
    match_percent: float
    total_instructions: int
    patterns: list[PatternHit] = field(default_factory=list)
    error: Optional[str] = None


# ---- Pattern detection on instruction arrays ----

def detect_patterns(instructions: list[dict]) -> list[PatternHit]:
    """Scan an instruction diff array for known encoding patterns."""
    hits: list[PatternHit] = []
    hits.extend(_detect_extrwi_rlwinm(instructions))
    hits.extend(_detect_bool_negate(instructions))
    hits.extend(_detect_bool_mask(instructions))
    hits.extend(_detect_cmp_encoding(instructions))
    hits.extend(_detect_fma_mismatch(instructions))
    return hits


def _get_opcode(instr_side: dict | None) -> str:
    """Extract opcode from a target/base instruction dict."""
    if not instr_side:
        return ""
    return (instr_side.get("opcode") or "").strip()


def _get_args(instr_side: dict | None) -> str:
    """Extract args string from a target/base instruction dict."""
    if not instr_side:
        return ""
    return (instr_side.get("args") or "").strip()


def _get_typed_arg(instr_side: dict | None, index: int) -> str:
    """Get a specific typed argument value by index."""
    if not instr_side:
        return ""
    typed = instr_side.get("typed_args", [])
    if index < len(typed):
        return typed[index].get("value", "")
    return ""


def _fmt_instr(side: dict | None) -> str:
    """Format an instruction side as 'opcode args'."""
    if not side:
        return "(none)"
    op = _get_opcode(side)
    args = _get_args(side)
    return f"{op} {args}".strip()


def _detect_extrwi_rlwinm(instructions: list[dict]) -> list[PatternHit]:
    """Detect extrwi vs rlwinm encoding mismatches.

    extrwi = rlwinm rA, rS, 31, 31, 31 (extract bit to LSB, result 0 or 1)
    rlwinm = rlwinm rA, rS, 0, N, N   (mask bit in place, result 0 or bit-value)

    These show up as 'replace' where one side has rotation=31 and the other has rotation=0.
    Also detectable via the assembler mnemonics: 'extrwi' vs 'rlwinm' or 'clrlwi'.
    """
    hits = []
    for i, instr in enumerate(instructions):
        match_type = instr.get("match_type", "")
        if match_type != "replace":
            continue

        tgt = instr.get("target", {})
        base = instr.get("base", {})
        tgt_op = _get_opcode(tgt)
        base_op = _get_opcode(base)
        tgt_args = _get_args(tgt)
        base_args = _get_args(base)

        # Check for rlwinm vs rlwinm with different rotation
        # Also check extrwi. vs rlwinm. (assembler may use either form)
        is_extrwi_tgt = "extrwi" in tgt_op or _is_rlwinm_extract(tgt_op, tgt_args)
        is_extrwi_base = "extrwi" in base_op or _is_rlwinm_extract(base_op, base_args)
        is_rlwinm_tgt = _is_rlwinm_mask(tgt_op, tgt_args)
        is_rlwinm_base = _is_rlwinm_mask(base_op, base_args)

        if (is_extrwi_tgt and is_rlwinm_base) or (is_rlwinm_tgt and is_extrwi_base):
            direction = "target=extrwi, base=rlwinm" if is_extrwi_tgt else "target=rlwinm, base=extrwi"
            fix = "add bool variable" if is_extrwi_tgt else "remove bool, use inline expression"
            hits.append(PatternHit(
                pattern_type="extrwi_rlwinm",
                indices=[i],
                target_instrs=[_fmt_instr(tgt)],
                base_instrs=[_fmt_instr(base)],
                description=f"extrwi↔rlwinm encoding at idx {i}: {direction}. Fix: {fix}",
                fixable=True,
            ))

        # Also check for extrwi. vs rlwinm. mnemonic-level mismatch
        # (avoid duplicates if already caught above)
        if not (is_extrwi_tgt and is_rlwinm_base) and not (is_rlwinm_tgt and is_extrwi_base):
            tgt_is_extrwi_mnemonic = tgt_op in ("extrwi", "extrwi.")
            base_is_extrwi_mnemonic = base_op in ("extrwi", "extrwi.")
            tgt_is_rlwinm_mnemonic = tgt_op in ("rlwinm", "rlwinm.", "clrlwi", "clrlwi.")
            base_is_rlwinm_mnemonic = base_op in ("rlwinm", "rlwinm.", "clrlwi", "clrlwi.")
            if (tgt_is_extrwi_mnemonic and base_is_rlwinm_mnemonic) or (tgt_is_rlwinm_mnemonic and base_is_extrwi_mnemonic):
                direction = f"target={tgt_op}, base={base_op}"
                fix = "add bool variable" if tgt_is_extrwi_mnemonic else "remove bool, use inline expression"
                hits.append(PatternHit(
                    pattern_type="extrwi_rlwinm",
                    indices=[i],
                    target_instrs=[_fmt_instr(tgt)],
                    base_instrs=[_fmt_instr(base)],
                    description=f"extrwi↔rlwinm encoding at idx {i}: {direction}. Fix: {fix}",
                    fixable=True,
                ))

    return hits


def _is_rlwinm_extract(opcode: str, args: str) -> bool:
    """Check if this is rlwinm with rotation=31, mb=31, me=31 (extract-to-LSB form)."""
    if not opcode.startswith("rlwinm"):
        return False
    # args like "r3, r4, 31, 31, 31" or "r3, r4, 0x1f, 0x1f, 0x1f"
    parts = [p.strip() for p in args.split(",")]
    if len(parts) >= 5:
        try:
            sh = int(parts[2], 0)
            mb = int(parts[3], 0)
            me = int(parts[4], 0)
            return sh == 31 and mb == 31 and me == 31
        except ValueError:
            pass
    return False


def _is_rlwinm_mask(opcode: str, args: str) -> bool:
    """Check if this is rlwinm with rotation=0 (mask-in-place form)."""
    if not opcode.startswith("rlwinm"):
        return False
    parts = [p.strip() for p in args.split(",")]
    if len(parts) >= 5:
        try:
            sh = int(parts[2], 0)
            return sh == 0
        except ValueError:
            pass
    return False


def _detect_bool_negate(instructions: list[dict]) -> list[PatternHit]:
    """Detect boolean negation encoding differences.

    Pattern A (int negation): subic rA, rS, 1 / subfe rA, rA, rA
    Pattern B (bool negation): cntlzw rA, rS / extrwi rA, rA, 1, 26

    These produce different encodings for `!x` depending on whether x is int or bool.
    """
    hits = []
    n = len(instructions)

    for i in range(n - 1):
        instr_a = instructions[i]
        instr_b = instructions[i + 1]

        if instr_a.get("match_type") != "replace" or instr_b.get("match_type") != "replace":
            continue

        tgt_a_op = _get_opcode(instr_a.get("target"))
        tgt_b_op = _get_opcode(instr_b.get("target"))
        base_a_op = _get_opcode(instr_a.get("base"))
        base_b_op = _get_opcode(instr_b.get("base"))

        # Check: target=subic/subfe, base=cntlzw/extrwi (or vice versa)
        tgt_is_subic_subfe = tgt_a_op == "subic" and tgt_b_op == "subfe"
        base_is_subic_subfe = base_a_op == "subic" and base_b_op == "subfe"
        tgt_is_cntlzw_extrwi = tgt_a_op == "cntlzw" and ("extrwi" in tgt_b_op or _is_rlwinm_extract(tgt_b_op, _get_args(instr_b.get("target"))))
        base_is_cntlzw_extrwi = base_a_op == "cntlzw" and ("extrwi" in base_b_op or _is_rlwinm_extract(base_b_op, _get_args(instr_b.get("base"))))

        if (tgt_is_subic_subfe and base_is_cntlzw_extrwi) or (tgt_is_cntlzw_extrwi and base_is_subic_subfe):
            direction = "target=subic/subfe(int), base=cntlzw/extrwi(bool)" if tgt_is_subic_subfe else "target=cntlzw/extrwi(bool), base=subic/subfe(int)"
            hits.append(PatternHit(
                pattern_type="bool_negate",
                indices=[i, i + 1],
                target_instrs=[_fmt_instr(instr_a.get("target")), _fmt_instr(instr_b.get("target"))],
                base_instrs=[_fmt_instr(instr_a.get("base")), _fmt_instr(instr_b.get("base"))],
                description=f"Boolean negation encoding at idx {i}-{i+1}: {direction}. Fix: change return/variable type (int vs bool)",
                fixable=True,
            ))

    return hits


def _detect_bool_mask(instructions: list[dict]) -> list[PatternHit]:
    """Detect extra clrlwi (bool truncation) instructions.

    When our code has an extra `clrlwi rA, rB, 24` that the target doesn't,
    it means we're truncating to bool (byte) unnecessarily. This often appears
    as a delete/insert of `clrlwi rX, rX, 24`.
    """
    hits = []
    for i, instr in enumerate(instructions):
        match_type = instr.get("match_type", "")
        if match_type not in ("insert", "delete"):
            continue

        # For insert: our code (base) has it, target doesn't
        # For delete: target has it, our code doesn't
        if match_type == "insert":
            side = instr.get("base", {})
        else:
            side = instr.get("target", {})

        op = _get_opcode(side)
        args = _get_args(side)

        if op in ("clrlwi", "clrlwi."):
            # clrlwi rA, rB, N — clear left N bits
            parts = [p.strip() for p in args.split(",")]
            if len(parts) >= 3:
                try:
                    shift = int(parts[2], 0)
                    if shift == 24:  # 24-bit clear = byte truncation (bool ABI)
                        who = "base (our code)" if match_type == "insert" else "target"
                        hits.append(PatternHit(
                            pattern_type="bool_mask_24",
                            indices=[i],
                            target_instrs=[_fmt_instr(instr.get("target"))],
                            base_instrs=[_fmt_instr(instr.get("base"))],
                            description=f"Bool byte-truncation (clrlwi 24) at idx {i}: extra in {who}. Fix: use && chain vs if/else returns",
                            fixable=True,
                        ))
                    elif shift == 31:  # 1-bit extract = LSB extraction (& 1)
                        who = "base (our code)" if match_type == "insert" else "target"
                        hits.append(PatternHit(
                            pattern_type="bool_mask_31",
                            indices=[i],
                            target_instrs=[_fmt_instr(instr.get("target"))],
                            base_instrs=[_fmt_instr(instr.get("base"))],
                            description=f"Bool LSB-extract (clrlwi 31) at idx {i}: extra in {who}. Fix: adjust comparison form",
                            fixable=True,
                        ))
                except ValueError:
                    pass

        # Also check rlwinm form: rlwinm rA, rB, 0, 24, 31 or rlwinm rA, rB, 0, 31, 31
        if op in ("rlwinm", "rlwinm."):
            parts = [p.strip() for p in args.split(",")]
            if len(parts) >= 5:
                try:
                    sh = int(parts[2], 0)
                    mb = int(parts[3], 0)
                    me = int(parts[4], 0)
                    if sh == 0 and mb == 24 and me == 31:
                        who = "base (our code)" if match_type == "insert" else "target"
                        hits.append(PatternHit(
                            pattern_type="bool_mask_24",
                            indices=[i],
                            target_instrs=[_fmt_instr(instr.get("target"))],
                            base_instrs=[_fmt_instr(instr.get("base"))],
                            description=f"Bool byte-truncation (rlwinm 0,24,31) at idx {i}: extra in {who}. Fix: use && chain vs if/else returns",
                            fixable=True,
                        ))
                    elif sh == 0 and mb == 31 and me == 31:
                        who = "base (our code)" if match_type == "insert" else "target"
                        hits.append(PatternHit(
                            pattern_type="bool_mask_31",
                            indices=[i],
                            target_instrs=[_fmt_instr(instr.get("target"))],
                            base_instrs=[_fmt_instr(instr.get("base"))],
                            description=f"Bool LSB-extract (rlwinm 0,31,31) at idx {i}: extra in {who}. Fix: adjust comparison form",
                            fixable=True,
                        ))
                except ValueError:
                    pass

    return hits


def _detect_cmp_encoding(instructions: list[dict]) -> list[PatternHit]:
    """Detect comparison encoding differences.

    `x > 0` generates `ble` (branch if less or equal)
    `x != 0` generates `beq` (branch if equal)

    These show up as replace mismatches between branch instructions.
    """
    hits = []
    for i, instr in enumerate(instructions):
        if instr.get("match_type") != "replace":
            continue

        tgt = instr.get("target", {})
        base = instr.get("base", {})
        tgt_op = _get_opcode(tgt)
        base_op = _get_opcode(base)

        # Common pairs: ble↔beq, bge↔bne, bgt↔bne, blt↔beq
        branch_pairs = {
            ("ble", "beq"), ("beq", "ble"),
            ("bge", "bne"), ("bne", "bge"),
            ("bgt", "bne"), ("bne", "bgt"),
            ("blt", "beq"), ("beq", "blt"),
            # With + prediction hints
            ("ble+", "beq+"), ("beq+", "ble+"),
            ("bge+", "bne+"), ("bne+", "bge+"),
            ("bgt+", "bne+"), ("bne+", "bgt+"),
            ("ble-", "beq-"), ("beq-", "ble-"),
            ("bge-", "bne-"), ("bne-", "bge-"),
        }

        pair = (tgt_op, base_op)
        if pair in branch_pairs:
            hits.append(PatternHit(
                pattern_type="cmp_encoding",
                indices=[i],
                target_instrs=[_fmt_instr(tgt)],
                base_instrs=[_fmt_instr(base)],
                description=f"Comparison encoding at idx {i}: target={tgt_op}, base={base_op}. Try > 0 vs != 0",
                fixable=True,
            ))

    return hits


def _detect_fma_mismatch(instructions: list[dict]) -> list[PatternHit]:
    """Detect fmadds vs fmuls+fadds mismatches.

    When target has separate fmuls+fadds but our build generates fmadds (or vice versa),
    this indicates a #pragma fp_contract difference between builds.

    Direction 1: target=fmadds, base=fmuls+fadds → we need #pragma fp_contract(on) (default)
    Direction 2: target=fmuls+fadds, base=fmadds → we need #pragma fp_contract(off)
    """
    hits = []
    fma_ops = {"fmadds", "fmsubs", "fnmadds", "fnmsubs", "fmadd", "fmsub", "fnmadd", "fnmsub"}
    fmul_ops = {"fmuls", "fmul"}
    fadd_ops = {"fadds", "fsubs", "fadd", "fsub"}

    for i, instr in enumerate(instructions):
        mtype = instr.get("match_type", "")
        tgt = instr.get("target", {})
        base = instr.get("base", {})
        tgt_op = _get_opcode(tgt)
        base_op = _get_opcode(base)

        # Case 1: replace where one side is fma and other is fmuls or fadds
        if mtype == "replace":
            if tgt_op in fma_ops and base_op in (fmul_ops | fadd_ops):
                hits.append(PatternHit(
                    pattern_type="fma_mismatch",
                    indices=[i],
                    target_instrs=[_fmt_instr(tgt)],
                    base_instrs=[_fmt_instr(base)],
                    description=f"Target has {tgt_op} but base has {base_op} at idx {i}. "
                                f"Add #pragma fp_contract(on) or restructure expression",
                    fixable=True,
                ))
            elif base_op in fma_ops and tgt_op in (fmul_ops | fadd_ops):
                hits.append(PatternHit(
                    pattern_type="fma_mismatch",
                    indices=[i],
                    target_instrs=[_fmt_instr(tgt)],
                    base_instrs=[_fmt_instr(base)],
                    description=f"Target has {tgt_op} but base has {base_op} at idx {i}. "
                                f"Add #pragma fp_contract(off) to this file",
                    fixable=True,
                ))

        # Case 2: insert/delete pairs - fmadds in one, fmuls/fadds in other
        if mtype == "insert" and base_op in fma_ops:
            # Our code has fmadds but target doesn't
            hits.append(PatternHit(
                pattern_type="fma_mismatch",
                indices=[i],
                target_instrs=["(missing)"],
                base_instrs=[_fmt_instr(base)],
                description=f"Base has {base_op} at idx {i} with no target match. "
                            f"Likely need #pragma fp_contract(off)",
                fixable=True,
            ))
        elif mtype == "delete" and tgt_op in fma_ops:
            # Target has fmadds but we don't
            hits.append(PatternHit(
                pattern_type="fma_mismatch",
                indices=[i],
                target_instrs=[_fmt_instr(tgt)],
                base_instrs=["(missing)"],
                description=f"Target has {tgt_op} at idx {i} with no base match. "
                            f"Likely need #pragma fp_contract(on)",
                fixable=True,
            ))

        # Case 3: diff_arg where both sides are fma-related but different variant
        if mtype == "diff_arg":
            if tgt_op in fma_ops and base_op in fma_ops and tgt_op != base_op:
                hits.append(PatternHit(
                    pattern_type="fma_mismatch",
                    indices=[i],
                    target_instrs=[_fmt_instr(tgt)],
                    base_instrs=[_fmt_instr(base)],
                    description=f"FMA variant mismatch: target={tgt_op} vs base={base_op} at idx {i}",
                    fixable=True,
                ))

    return hits


# ---- Report parsing ----

def load_functions_from_report(report_path: Path, min_pct: float, max_pct: float,
                                unit_filter: str | None = None) -> list[dict]:
    """Load function list from report.json filtered by match%."""
    with open(report_path) as f:
        report = json.load(f)

    results = []
    for unit in report.get("units", []):
        unit_name = unit.get("name", "")
        if unit_filter and unit_filter not in unit_name:
            continue
        for func in unit.get("functions", []):
            pct = func.get("fuzzy_match_percent")
            if pct is None:
                continue
            if min_pct <= pct <= max_pct:
                results.append({
                    "symbol": func["name"],
                    "unit": unit_name,
                    "match_percent": pct,
                    "size": int(func.get("size", 0)),
                })
    return results


# ---- objdiff-cli runner ----

def run_objdiff_json(symbol: str) -> dict | None:
    """Run objdiff-cli and get JSON instruction diff for a function."""
    cmd = [
        str(OBJDIFF_CLI), "diff",
        "-p", str(PROJECT_DIR),
        symbol,
        "--include-instructions",
        "--analyze",
        "-f", "json",
    ]
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=30,
        )
        if result.returncode != 0:
            return None
        return json.loads(result.stdout)
    except (subprocess.TimeoutExpired, json.JSONDecodeError, FileNotFoundError):
        return None


# ---- Main scan logic ----

def scan_function(func_info: dict) -> FunctionScan:
    """Scan a single function for encoding patterns."""
    symbol = func_info["symbol"]
    scan = FunctionScan(
        symbol=symbol,
        demangled="",
        unit=func_info["unit"],
        match_percent=func_info["match_percent"],
        total_instructions=0,
    )

    data = run_objdiff_json(symbol)
    if data is None:
        scan.error = "objdiff failed"
        return scan

    # JSON structure: top-level keys include "instructions", "demangled", etc.
    instructions = data.get("instructions", [])
    scan.demangled = data.get("demangled", symbol)

    scan.total_instructions = len(instructions)
    if instructions:
        scan.patterns = detect_patterns(instructions)

    return scan


def main():
    parser = argparse.ArgumentParser(description="Batch scan for encoding patterns")
    parser.add_argument("--min", type=float, default=90.0, help="Minimum match%% (default: 90)")
    parser.add_argument("--max", type=float, default=99.9, help="Maximum match%% (default: 99.9)")
    parser.add_argument("--limit", type=int, default=200, help="Max functions to scan (default: 200)")
    parser.add_argument("--unit", type=str, default=None, help="Filter by unit name substring")
    parser.add_argument("--pattern", type=str, default=None,
                        help="Filter output by pattern type: extrwi_rlwinm, bool_negate, bool_mask (=24+31), bool_mask_24, bool_mask_31, cmp_encoding")
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    parser.add_argument("--verbose", "-v", action="store_true", help="Show progress")
    args = parser.parse_args()

    if not REPORT_JSON.exists():
        print(f"Error: report.json not found at {REPORT_JSON}", file=sys.stderr)
        print("Run: ninja build/373307D9/report.json", file=sys.stderr)
        sys.exit(1)

    if not OBJDIFF_CLI.exists():
        print(f"Error: objdiff-cli not found at {OBJDIFF_CLI}", file=sys.stderr)
        sys.exit(1)

    # Load functions from report
    if args.verbose:
        print(f"Loading functions from {REPORT_JSON}...", file=sys.stderr)

    functions = load_functions_from_report(REPORT_JSON, args.min, args.max, args.unit)
    functions.sort(key=lambda f: -f["match_percent"])  # highest match first

    if args.limit and len(functions) > args.limit:
        functions = functions[:args.limit]

    if args.verbose:
        print(f"Scanning {len(functions)} functions ({args.min}%-{args.max}%)...", file=sys.stderr)

    # Scan each function
    results: list[FunctionScan] = []
    hits_count = 0

    for idx, func in enumerate(functions):
        if args.verbose:
            pct = func["match_percent"]
            print(f"  [{idx+1}/{len(functions)}] {func['symbol'][:60]}... ({pct:.1f}%)", file=sys.stderr, end="")

        scan = scan_function(func)
        if scan.patterns:
            # Filter by pattern type if requested
            if args.pattern:
                pat = args.pattern
                scan.patterns = [p for p in scan.patterns
                                 if p.pattern_type == pat
                                 or (pat == "bool_mask" and p.pattern_type.startswith("bool_mask_"))]

            if scan.patterns:
                results.append(scan)
                hits_count += len(scan.patterns)
                if args.verbose:
                    print(f" -> {len(scan.patterns)} pattern(s)!", file=sys.stderr)
            elif args.verbose:
                print(" (no matching patterns)", file=sys.stderr)
        else:
            if args.verbose:
                if scan.error:
                    print(f" ERROR: {scan.error}", file=sys.stderr)
                else:
                    print(" (clean)", file=sys.stderr)

    # Output results
    if args.json:
        output = []
        for scan in results:
            output.append({
                "symbol": scan.symbol,
                "demangled": scan.demangled,
                "unit": scan.unit,
                "match_percent": scan.match_percent,
                "total_instructions": scan.total_instructions,
                "patterns": [
                    {
                        "type": p.pattern_type,
                        "indices": p.indices,
                        "target": p.target_instrs,
                        "base": p.base_instrs,
                        "description": p.description,
                        "fixable": p.fixable,
                    }
                    for p in scan.patterns
                ],
            })
        print(json.dumps(output, indent=2))
    else:
        # Human-readable output
        print(f"\n{'='*80}")
        print(f"BATCH PATTERN SCAN RESULTS")
        print(f"Range: {args.min}%-{args.max}% | Scanned: {len(functions)} | Hits: {hits_count}")
        print(f"{'='*80}\n")

        # Group by pattern type
        by_type: dict[str, list[tuple[FunctionScan, PatternHit]]] = {}
        for scan in results:
            for pattern in scan.patterns:
                by_type.setdefault(pattern.pattern_type, []).append((scan, pattern))

        for ptype, items in sorted(by_type.items()):
            fixable = items[0][1].fixable
            fix_label = "FIXABLE" if fixable else "LIKELY UNFIXABLE"
            print(f"\n## {ptype} ({len(items)} hit(s)) [{fix_label}]")
            print(f"{'-'*60}")
            for scan, pattern in items:
                print(f"  {scan.match_percent:5.1f}%  {scan.demangled or scan.symbol}")
                print(f"         Unit: {scan.unit}")
                for idx, (tgt, base) in enumerate(zip(pattern.target_instrs, pattern.base_instrs)):
                    print(f"         [{pattern.indices[idx]:3d}] TGT: {tgt}")
                    print(f"               SRC: {base}")
                print(f"         -> {pattern.description}")
                print()

        if not results:
            print("No encoding patterns found in scanned functions.")

        # Summary
        print(f"\n{'='*80}")
        print(f"SUMMARY")
        print(f"{'='*80}")
        for ptype, items in sorted(by_type.items()):
            fixable_count = sum(1 for _, p in items if p.fixable)
            print(f"  {ptype:20s}: {len(items):3d} hit(s), {fixable_count} fixable")
        print(f"  {'TOTAL':20s}: {hits_count:3d} hit(s)")


if __name__ == "__main__":
    main()
