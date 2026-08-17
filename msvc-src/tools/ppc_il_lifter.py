#!/usr/bin/env python3
"""Lift PPC assembly into IL-like operations for synthesis guidance.

Covers the full codegen surface that matters for decomp matching:

- byte mask / rlwinm fusion (clrlwi, extrwi, clrlslwi, rlwinm)
- shifts and bitwise ops
- compares and conditional branches
- loads/stores (base+offset and indexed)
- calls and returns (direct, indirect, prologue helpers)
- float arithmetic (fadd, fsub, fmul, fdiv, fmadd, etc.)
- multiply/divide (mullw, divw, mulli, etc.)
- sign extension (extsh, extsb)
- switch dispatch (bctr-based patterns)
- virtual call patterns (vtable load + indirect call)
- prologue/epilogue analysis (savegprlr, stack frame)
- condition register ops (cror, crand, crandc)
- bool materialization carry chains

Also builds a control flow graph (CFG) with basic blocks, loop detection,
and derives comprehensive shape facts for the permuter's target_facts layer.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from diff_test import FunctionAsm, parse_asm_listing
from il_annotate import compile_with_listing, format_il_ops
from il_parser import ILFile, capture_il

_IMM_HEX_RE = re.compile(r"^-?[0-9a-f]+h$", re.IGNORECASE)
_COND_LR_BRANCH_RE = re.compile(r"^(beq|bne|ble|bge|blt|bgt)lr$")
_CTR_COND_BRANCH_RE = re.compile(r"^(bdnz|bdz)(eq|ne|lt|gt|le|ge)$")

# ── Instruction tables ──────────────────────────────────────────────

_COND_BRANCHES = {
    "beq": "eq", "bne": "ne", "ble": "le", "bge": "ge",
    "blt": "lt", "bgt": "gt",
}

_LOAD_OPS = {"lwz", "lbz", "lhz", "lha", "lfs", "lfd", "lwzu", "lbzu", "lhzu", "lfsu", "lfdu", "ld", "ldu"}
_STORE_OPS = {"stw", "stb", "sth", "stfs", "stfd", "stwu", "stbu", "sthu", "stfsu", "stfdu", "std", "stdu"}
_LOAD_INDEXED = {"lwzx", "lbzx", "lhzx", "lhax", "lfsx", "lfdx", "lwzux"}
_STORE_INDEXED = {"stwx", "stbx", "sthx", "stfsx", "stfdx"}

_BINARY_ALU = {
    "add": "ADD", "add.": "ADD", "addi": "ADD", "addic": "ADD", "addic.": "ADD",
    "addis": "ADD",
    "subf": "SUB", "subf.": "SUB",
    "and": "AND", "and.": "AND", "andi": "AND", "andi.": "AND", "andis.": "AND",
    "or": "OR", "or.": "OR", "ori": "OR", "oris": "OR",
    "xor": "XOR", "xor.": "XOR", "xori": "XOR", "xoris": "XOR",
    "nand": "NAND", "nor": "NOR", "orc": "ORC",
}

_FLOAT_BINARY = {
    "fadd": "FADD", "fadds": "FADD",
    "fsub": "FSUB", "fsubs": "FSUB",
    "fmul": "FMUL", "fmuls": "FMUL",
    "fdiv": "FDIV", "fdivs": "FDIV",
}

_FLOAT_TERNARY = {
    "fmadd": "FMADD", "fmadds": "FMADD",
    "fmsub": "FMSUB", "fmsubs": "FMSUB",
    "fnmadd": "FNMADD", "fnmadds": "FNMADD",
    "fnmsub": "FNMSUB", "fnmsubs": "FNMSUB",
}

_MUL_DIV = {
    "mullw": "MUL", "mullw.": "MUL", "mulli": "MUL",
    "mulhw": "MUL_HIGH", "mulhwu": "MUL_HIGH_UNSIGNED",
    "divw": "DIV", "divw.": "DIV", "divwu": "DIV_UNSIGNED", "divwu.": "DIV_UNSIGNED",
}

_SAVEGPR_RE = re.compile(r"__savegprlr_(\d+)")
_RESTGPR_RE = re.compile(r"__restgprlr_(\d+)")
_SAVEFPR_RE = re.compile(r"__savefpr_(\d+)")
_RESTFPR_RE = re.compile(r"__restfpr_(\d+)")


# ── Data types ──────────────────────────────────────────────────────

@dataclass(frozen=True)
class LiftedOp:
    """Normalized, intentionally lossy PPC lift."""

    name: str
    dest: str | None = None
    args: tuple[str, ...] = ()
    attrs: dict[str, Any] = field(default_factory=dict)
    source: str | None = None

    def to_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "dest": self.dest,
            "args": list(self.args),
            "attrs": dict(self.attrs),
            "source": self.source,
        }


@dataclass
class BasicBlock:
    """A basic block in the control flow graph."""

    label: str
    ops: list[LiftedOp] = field(default_factory=list)
    successors: list[str] = field(default_factory=list)
    predecessors: list[str] = field(default_factory=list)
    is_entry: bool = False
    is_exit: bool = False

    def to_dict(self) -> dict[str, Any]:
        return {
            "label": self.label,
            "op_count": len(self.ops),
            "op_names": [op.name for op in self.ops],
            "successors": list(self.successors),
            "predecessors": list(self.predecessors),
            "is_entry": self.is_entry,
            "is_exit": self.is_exit,
        }


@dataclass
class ControlFlowGraph:
    """Control flow graph built from lifted PPC ops."""

    blocks: dict[str, BasicBlock] = field(default_factory=dict)
    back_edges: list[tuple[str, str]] = field(default_factory=list)
    loop_headers: list[str] = field(default_factory=list)

    @property
    def block_count(self) -> int:
        return len(self.blocks)

    @property
    def edge_count(self) -> int:
        return sum(len(b.successors) for b in self.blocks.values())

    @property
    def loop_count(self) -> int:
        return len(self.back_edges)

    def nesting_depth(self) -> int:
        """Estimate max loop nesting depth from back-edge structure."""
        if not self.loop_headers:
            return 0
        depth = 1
        for header in self.loop_headers:
            inner_count = 0
            block = self.blocks.get(header)
            if block:
                for succ in block.successors:
                    if succ in self.loop_headers and succ != header:
                        inner_count += 1
            depth = max(depth, 1 + inner_count)
        return depth

    def to_dict(self) -> dict[str, Any]:
        return {
            "block_count": self.block_count,
            "edge_count": self.edge_count,
            "loop_count": self.loop_count,
            "nesting_depth": self.nesting_depth(),
            "back_edges": [list(e) for e in self.back_edges],
            "loop_headers": list(self.loop_headers),
            "blocks": {k: v.to_dict() for k, v in self.blocks.items()},
        }


@dataclass
class PrologueInfo:
    """Prologue/epilogue metadata extracted from lifted ops."""

    callee_saved_gprs: int = 0
    callee_saved_fprs: int = 0
    stack_frame_size: int = 0
    uses_savegpr_helper: bool = False
    uses_savefpr_helper: bool = False
    first_saved_gpr: int = 32  # r32 = none saved
    first_saved_fpr: int = 32

    def to_dict(self) -> dict[str, Any]:
        return {
            "callee_saved_gprs": self.callee_saved_gprs,
            "callee_saved_fprs": self.callee_saved_fprs,
            "stack_frame_size": self.stack_frame_size,
            "uses_savegpr_helper": self.uses_savegpr_helper,
            "uses_savefpr_helper": self.uses_savefpr_helper,
            "first_saved_gpr": self.first_saved_gpr if self.first_saved_gpr < 32 else None,
            "first_saved_fpr": self.first_saved_fpr if self.first_saved_fpr < 32 else None,
        }


@dataclass
class LiftedFunction:
    """Lifted representation of one PPC function."""

    name: str
    ops: list[LiftedOp]
    unsupported: list[str]
    prologue: PrologueInfo = field(default_factory=PrologueInfo)
    cfg: ControlFlowGraph | None = None

    def to_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {
            "name": self.name,
            "ops": [op.to_dict() for op in self.ops],
            "unsupported": list(self.unsupported),
            "prologue": self.prologue.to_dict(),
        }
        if self.cfg:
            d["cfg"] = self.cfg.to_dict()
        return d


# ── Instruction parsing helpers ─────────────────────────────────────

def _split_instruction(text: str) -> tuple[str, list[str]]:
    """Split a mnemonic line into mnemonic + operands."""
    cleaned = text.split(";", 1)[0].strip()
    if not cleaned:
        return "", []
    parts = cleaned.split(None, 1)
    mnemonic = parts[0].lower()
    if len(parts) == 1:
        return mnemonic, []
    operands = [part.strip() for part in parts[1].split(",")]
    return mnemonic, operands


def _parse_immediate(value: str) -> int | None:
    """Parse PPC immediate syntax into an int when possible."""
    text = value.strip().lower()
    if not text:
        return None
    if text.startswith("0x"):
        return int(text, 16)
    if _IMM_HEX_RE.match(text):
        sign = -1 if text.startswith("-") else 1
        digits = text[1:-1] if sign == -1 else text[:-1]
        return sign * int(digits, 16)
    if re.fullmatch(r"-?\d+", text):
        return int(text, 10)
    return None


def _strip_condition_register(operands: list[str]) -> list[str]:
    """Drop leading CR operand when present in compare instructions."""
    if operands and operands[0].lower().startswith("cr"):
        return operands[1:]
    return operands


def _split_branch_target(operands: list[str]) -> tuple[str | None, dict[str, Any]]:
    """Extract optional CR operand and branch target."""
    attrs: dict[str, Any] = {}
    if operands and operands[0].lower().startswith("cr"):
        attrs["cr"] = operands[0]
        if len(operands) >= 2:
            return operands[1], attrs
        return None, attrs
    if operands:
        return operands[0], attrs
    return None, attrs


def _format_op(op: LiftedOp) -> str:
    parts = [op.name]
    if op.dest:
        parts.append(op.dest)
    if op.args:
        parts.append(", ".join(op.args))
    if op.attrs:
        attrs = ", ".join(f"{k}={v}" for k, v in sorted(op.attrs.items()))
        parts.append(f"[{attrs}]")
    return " ".join(parts)


def _mnemonic_of_op(op: LiftedOp) -> str:
    """Recover the PPC mnemonic used to create this lifted op."""
    if not op.source:
        return ""
    mnemonic, _ = _split_instruction(op.source)
    return mnemonic


# ── Single-instruction lifter ───────────────────────────────────────

def lift_instruction(text: str) -> LiftedOp | None:
    """Lift one PPC instruction into an IL-like operation."""
    mnemonic, operands = _split_instruction(text)
    if not mnemonic:
        return None

    # ── Moves and constants ──

    if mnemonic in ("mr", "mr.", "fmr") and len(operands) >= 2:
        attrs = {"record": True} if mnemonic == "mr." else {}
        return LiftedOp("ASSIGN", dest=operands[0], args=(operands[1],), attrs=attrs, source=text)

    if mnemonic in ("li", "lis") and len(operands) >= 2:
        value = _parse_immediate(operands[1])
        attrs: dict[str, Any] = {"constant": value} if value is not None else {"constant_text": operands[1]}
        if mnemonic == "lis":
            attrs["hi16"] = True
        return LiftedOp("CONST", dest=operands[0], attrs=attrs, source=text)

    # ── rlwinm family (byte masks, fused shifts) ──

    if mnemonic in ("clrlwi", "clrlwi.") and len(operands) >= 3:
        shift = _parse_immediate(operands[2])
        attrs = {"mask_shift": shift} if shift is not None else {"mask_shift_text": operands[2]}
        if shift == 24:
            attrs["canonical"] = "byte_mask"
            attrs["mask"] = 0xFF
        return LiftedOp("BYTE_MASK", dest=operands[0], args=(operands[1],), attrs=attrs, source=text)

    if mnemonic == "extrwi" and len(operands) >= 4:
        width = _parse_immediate(operands[2])
        offset = _parse_immediate(operands[3])
        attrs = {"canonical": "rlwinm"}
        if width is not None:
            attrs["width"] = width
        if offset is not None:
            attrs["offset"] = offset
        return LiftedOp("FUSED_SHR_MASK", dest=operands[0], args=(operands[1],), attrs=attrs, source=text)

    if mnemonic == "clrlslwi" and len(operands) >= 4:
        mask_shift = _parse_immediate(operands[2])
        shift = _parse_immediate(operands[3])
        attrs = {"canonical": "rlwinm"}
        if mask_shift is not None:
            attrs["mask_shift"] = mask_shift
        if shift is not None:
            attrs["shift"] = shift
        return LiftedOp("FUSED_SHL_MASK", dest=operands[0], args=(operands[1],), attrs=attrs, source=text)

    if mnemonic in ("srwi", "slwi") and len(operands) >= 3:
        shift = _parse_immediate(operands[2])
        attrs = {"shift": shift} if shift is not None else {"shift_text": operands[2]}
        opname = "SHR" if mnemonic == "srwi" else "SHL"
        return LiftedOp(opname, dest=operands[0], args=(operands[1],), attrs=attrs, source=text)

    if mnemonic in ("srawi", "srawi.") and len(operands) >= 3:
        shift = _parse_immediate(operands[2])
        attrs = {"shift": shift} if shift is not None else {"shift_text": operands[2]}
        if mnemonic.endswith("."):
            attrs["record"] = True
        return LiftedOp("SAR", dest=operands[0], args=(operands[1],), attrs=attrs, source=text)

    if mnemonic in ("srw", "slw") and len(operands) >= 3:
        opname = "SHR" if mnemonic == "srw" else "SHL"
        return LiftedOp(opname, dest=operands[0], args=tuple(operands[1:3]),
                        attrs={"reg_shift": True}, source=text)

    if mnemonic == "sraw" and len(operands) >= 3:
        return LiftedOp("SAR", dest=operands[0], args=tuple(operands[1:3]),
                        attrs={"reg_shift": True}, source=text)

    if mnemonic in ("rlwinm", "rlwinm.") and len(operands) >= 5:
        sh = _parse_immediate(operands[2])
        mb = _parse_immediate(operands[3])
        me = _parse_immediate(operands[4])

        if sh == 0 and mb is not None and me == 31:
            attrs = {"canonical": "byte_mask", "mask_shift": mb}
            if mb == 24:
                attrs["mask"] = 0xFF
            return LiftedOp("BYTE_MASK", dest=operands[0], args=(operands[1],), attrs=attrs, source=text)

        if sh is not None and mb is not None and me == 31:
            width = 32 - mb
            if 0 < width < 32:
                offset = (sh - width) % 32
                return LiftedOp(
                    "FUSED_SHR_MASK", dest=operands[0], args=(operands[1],),
                    attrs={"canonical": "rlwinm", "width": width, "offset": offset},
                    source=text,
                )

        attrs = {"canonical": "rlwinm", "record": mnemonic.endswith(".")}
        for key, imm in (("sh", sh), ("mb", mb), ("me", me)):
            if imm is not None:
                attrs[key] = imm
        for key, operand in zip(("sh", "mb", "me"), operands[2:5]):
            if _parse_immediate(operand) is None:
                attrs[f"{key}_text"] = operand
        return LiftedOp("ROT_MASK", dest=operands[0], args=(operands[1],), attrs=attrs, source=text)

    if mnemonic in ("rlwimi", "rlwimi.") and len(operands) >= 5:
        sh = _parse_immediate(operands[2])
        mb = _parse_immediate(operands[3])
        me = _parse_immediate(operands[4])
        attrs = {"canonical": "rlwimi", "record": mnemonic.endswith(".")}
        for key, imm in (("sh", sh), ("mb", mb), ("me", me)):
            if imm is not None:
                attrs[key] = imm
        return LiftedOp("ROT_INSERT", dest=operands[0], args=(operands[1],), attrs=attrs, source=text)

    # ── Bool carry chain ops ──

    if mnemonic == "neg" and len(operands) >= 2:
        return LiftedOp("NEG", dest=operands[0], args=(operands[1],), source=text)

    if mnemonic == "cntlzw" and len(operands) >= 2:
        return LiftedOp("COUNT_LEADING_ZEROS", dest=operands[0], args=(operands[1],), source=text)

    if mnemonic == "andc" and len(operands) >= 3:
        return LiftedOp("ANDC", dest=operands[0], args=tuple(operands[1:3]), source=text)

    if mnemonic == "eqv" and len(operands) >= 3:
        return LiftedOp("EQV", dest=operands[0], args=tuple(operands[1:3]), source=text)

    if mnemonic == "subfe" and len(operands) >= 3:
        return LiftedOp("SUB_EXTEND", dest=operands[0], args=tuple(operands[1:3]), source=text)

    if mnemonic == "subfc" and len(operands) >= 3:
        return LiftedOp("SUB_CARRY", dest=operands[0], args=tuple(operands[1:3]), source=text)

    if mnemonic == "subfic" and len(operands) >= 3:
        imm = _parse_immediate(operands[2])
        attrs = {"immediate": imm} if imm is not None else {"immediate_text": operands[2]}
        return LiftedOp("SUB_FROM_IMM", dest=operands[0], args=(operands[1],), attrs=attrs, source=text)

    if mnemonic == "addze" and operands:
        if len(operands) >= 2:
            return LiftedOp("ADD_ZERO_EXTEND", dest=operands[0], args=(operands[1],), source=text)
        return LiftedOp("ADD_ZERO_EXTEND", dest=operands[0], source=text)

    if mnemonic == "adde" and len(operands) >= 3:
        return LiftedOp("ADD_EXTEND", dest=operands[0], args=tuple(operands[1:3]), source=text)

    if mnemonic == "subfze" and operands:
        if len(operands) >= 2:
            return LiftedOp("SUBF_ZERO_EXTEND", dest=operands[0], args=(operands[1],), source=text)
        return LiftedOp("SUBF_ZERO_EXTEND", dest=operands[0], source=text)

    # ── Binary ALU ──

    if mnemonic in _BINARY_ALU and len(operands) >= 3:
        opname = _BINARY_ALU[mnemonic]
        attrs = {}
        if mnemonic.endswith("."):
            attrs["record"] = True
        return LiftedOp(opname, dest=operands[0], args=tuple(operands[1:3]), attrs=attrs, source=text)

    if mnemonic == "not" and len(operands) >= 2:
        return LiftedOp("NOT", dest=operands[0], args=(operands[1],), source=text)

    # ── Multiply / Divide ──

    if mnemonic in _MUL_DIV and len(operands) >= 3:
        opname = _MUL_DIV[mnemonic]
        return LiftedOp(opname, dest=operands[0], args=tuple(operands[1:3]), source=text)

    # ── Float arithmetic ──

    if mnemonic in _FLOAT_BINARY and len(operands) >= 3:
        opname = _FLOAT_BINARY[mnemonic]
        attrs = {"single": mnemonic.endswith("s")}
        return LiftedOp(opname, dest=operands[0], args=tuple(operands[1:3]), attrs=attrs, source=text)

    if mnemonic in _FLOAT_TERNARY and len(operands) >= 4:
        opname = _FLOAT_TERNARY[mnemonic]
        attrs = {"single": mnemonic.endswith("s")}
        return LiftedOp(opname, dest=operands[0], args=tuple(operands[1:4]), attrs=attrs, source=text)

    if mnemonic == "fneg" and len(operands) >= 2:
        return LiftedOp("FNEG", dest=operands[0], args=(operands[1],), source=text)

    if mnemonic == "fabs" and len(operands) >= 2:
        return LiftedOp("FABS", dest=operands[0], args=(operands[1],), source=text)

    if mnemonic == "fnabs" and len(operands) >= 2:
        return LiftedOp("FNABS", dest=operands[0], args=(operands[1],), source=text)

    if mnemonic == "frsp" and len(operands) >= 2:
        return LiftedOp("FLOAT_ROUND_SINGLE", dest=operands[0], args=(operands[1],),
                        attrs={"conversion": "double_to_single"}, source=text)

    if mnemonic == "fctiwz" and len(operands) >= 2:
        return LiftedOp("FLOAT_TO_INT", dest=operands[0], args=(operands[1],),
                        attrs={"conversion": "float_to_int_truncate"}, source=text)

    if mnemonic == "fctiw" and len(operands) >= 2:
        return LiftedOp("FLOAT_TO_INT", dest=operands[0], args=(operands[1],),
                        attrs={"conversion": "float_to_int_round"}, source=text)

    if mnemonic == "fsel" and len(operands) >= 4:
        return LiftedOp("FLOAT_SELECT", dest=operands[0], args=tuple(operands[1:4]),
                        attrs={"semantic": "dest = (a >= 0) ? c : b"}, source=text)

    if mnemonic in ("fcmpu", "fcmpo") and len(operands) >= 2:
        compare_ops = _strip_condition_register(operands)
        if len(compare_ops) >= 2:
            return LiftedOp("FCMP", args=tuple(compare_ops[:2]),
                           attrs={"ordered": mnemonic == "fcmpo"}, source=text)

    if mnemonic == "fres" and len(operands) >= 2:
        return LiftedOp("FLOAT_RECIPROCAL_EST", dest=operands[0], args=(operands[1],), source=text)

    if mnemonic == "frsqrte" and len(operands) >= 2:
        return LiftedOp("FLOAT_RSQRT_EST", dest=operands[0], args=(operands[1],), source=text)

    if mnemonic == "stfiwx" and len(operands) >= 3:
        return LiftedOp("STORE_INT_FROM_FLOAT", args=tuple(operands[:3]),
                        attrs={"conversion": "fpr_to_gpr_via_memory"}, source=text)

    # ── Sign extension ──

    if mnemonic in ("extsh", "extsh.") and len(operands) >= 2:
        return LiftedOp("SIGN_EXTEND", dest=operands[0], args=(operands[1],),
                        attrs={"from_bits": 16, "record": mnemonic.endswith(".")}, source=text)

    if mnemonic in ("extsb", "extsb.") and len(operands) >= 2:
        return LiftedOp("SIGN_EXTEND", dest=operands[0], args=(operands[1],),
                        attrs={"from_bits": 8, "record": mnemonic.endswith(".")}, source=text)

    if mnemonic == "extsw" and len(operands) >= 2:
        return LiftedOp("SIGN_EXTEND", dest=operands[0], args=(operands[1],),
                        attrs={"from_bits": 32}, source=text)

    if mnemonic == "fcfid" and len(operands) >= 2:
        return LiftedOp("INT_TO_FLOAT", dest=operands[0], args=(operands[1],),
                        attrs={"conversion": "int64_to_float"}, source=text)

    if mnemonic == "sradi" and len(operands) >= 3:
        shift = _parse_immediate(operands[2])
        attrs = {"shift": shift} if shift is not None else {"shift_text": operands[2]}
        attrs["doubleword"] = True
        return LiftedOp("SAR", dest=operands[0], args=(operands[1],), attrs=attrs, source=text)

    if mnemonic == "rldicl" and len(operands) >= 4:
        sh = _parse_immediate(operands[2])
        mb = _parse_immediate(operands[3])
        attrs = {"canonical": "rldicl", "doubleword": True}
        if sh is not None:
            attrs["sh"] = sh
        if mb is not None:
            attrs["mb"] = mb
        return LiftedOp("ROT_MASK", dest=operands[0], args=(operands[1],), attrs=attrs, source=text)

    # ── Compares and branches ──

    if mnemonic in ("cmpwi", "cmplwi", "cmpw", "cmplw"):
        compare_ops = _strip_condition_register(operands)
        if len(compare_ops) >= 2:
            attrs = {
                "signed": mnemonic in ("cmpwi", "cmpw"),
                "width": "imm" if mnemonic.endswith("i") else "reg",
            }
            return LiftedOp("CMP", args=tuple(compare_ops[:2]), attrs=attrs, source=text)

    m = _COND_LR_BRANCH_RE.match(mnemonic)
    if m:
        _, attrs = _split_branch_target(operands)
        attrs["condition"] = _COND_BRANCHES[m.group(1)]
        attrs["returns"] = True
        return LiftedOp("BRANCH", attrs=attrs, source=text)

    if mnemonic in _COND_BRANCHES:
        target, attrs = _split_branch_target(operands)
        attrs["condition"] = _COND_BRANCHES[mnemonic]
        args = (target,) if target else ()
        return LiftedOp("BRANCH", args=args, attrs=attrs, source=text)

    if mnemonic == "b" and operands:
        return LiftedOp("GOTO", args=(operands[0],), source=text)

    m = _CTR_COND_BRANCH_RE.match(mnemonic)
    if m:
        opname = "LOOP_DECREMENT" if m.group(1) == "bdnz" else "LOOP_EXIT"
        target, attrs = _split_branch_target(operands)
        attrs["condition"] = m.group(2)
        attrs["semantic"] = (
            "ctr -= 1; conditional branch"
            if opname == "LOOP_DECREMENT"
            else "ctr -= 1; exit when condition holds"
        )
        args = (target,) if target else ()
        return LiftedOp(opname, args=args, attrs=attrs, source=text)

    if mnemonic == "bdnz":
        target, attrs = _split_branch_target(operands)
        attrs["semantic"] = "ctr -= 1; if ctr != 0, branch"
        args = (target,) if target else ()
        return LiftedOp("LOOP_DECREMENT", args=args, attrs=attrs, source=text)

    if mnemonic == "bdz":
        target, attrs = _split_branch_target(operands)
        attrs["semantic"] = "ctr -= 1; if ctr == 0, branch"
        args = (target,) if target else ()
        return LiftedOp("LOOP_EXIT", args=args, attrs=attrs, source=text)

    # ── Condition register ops ──

    if mnemonic == "cror" and len(operands) >= 3:
        return LiftedOp("CR_OR", dest=operands[0], args=tuple(operands[1:3]), source=text)

    if mnemonic == "crand" and len(operands) >= 3:
        return LiftedOp("CR_AND", dest=operands[0], args=tuple(operands[1:3]), source=text)

    if mnemonic == "crandc" and len(operands) >= 3:
        return LiftedOp("CR_ANDC", dest=operands[0], args=tuple(operands[1:3]), source=text)

    if mnemonic == "crxor" and len(operands) >= 3:
        return LiftedOp("CR_XOR", dest=operands[0], args=tuple(operands[1:3]), source=text)

    if mnemonic == "mfcr" and operands:
        return LiftedOp("READ_CR", dest=operands[0], source=text)

    if mnemonic == "mtcrf" and len(operands) >= 2:
        return LiftedOp("WRITE_CR", args=tuple(operands[:2]), source=text)

    # ── Indirect dispatch (switch tables, virtual calls) ──

    if mnemonic == "mtctr" and operands:
        return LiftedOp("SET_CTR", args=(operands[0],), source=text)

    if mnemonic == "mfctr" and operands:
        return LiftedOp("READ_CTR", dest=operands[0], source=text)

    if mnemonic == "bctr":
        return LiftedOp("DISPATCH", attrs={"kind": "switch_or_indirect"}, source=text)

    if mnemonic == "bctrl":
        return LiftedOp("INDIRECT_CALL", attrs={"kind": "vtable_or_fptr"}, source=text)

    # ── Memory: base+offset ──

    if mnemonic in _LOAD_OPS and len(operands) >= 2:
        attrs = {"kind": mnemonic}
        if mnemonic.endswith("u"):
            attrs["update"] = True
        return LiftedOp("LOAD", dest=operands[0], args=(operands[1],), attrs=attrs, source=text)

    if mnemonic in _STORE_OPS and len(operands) >= 2:
        attrs = {"kind": mnemonic}
        if mnemonic.endswith("u"):
            attrs["update"] = True
        return LiftedOp("STORE", args=(operands[0], operands[1]), attrs=attrs, source=text)

    # ── Memory: indexed ──

    if mnemonic in _LOAD_INDEXED and len(operands) >= 3:
        attrs = {"kind": mnemonic, "indexed": True}
        if mnemonic.endswith("ux"):
            attrs["update"] = True
        return LiftedOp("LOAD_INDEXED", dest=operands[0], args=tuple(operands[1:3]),
                        attrs=attrs, source=text)

    if mnemonic in _STORE_INDEXED and len(operands) >= 3:
        attrs = {"kind": mnemonic, "indexed": True}
        return LiftedOp("STORE_INDEXED", args=tuple(operands[:3]), attrs=attrs, source=text)

    # ── Calls and returns ──

    if mnemonic == "bl" and operands:
        target = operands[0]

        # Prologue/epilogue helpers
        m = _SAVEGPR_RE.search(target)
        if m:
            reg_num = int(m.group(1))
            return LiftedOp("PROLOGUE_SAVE_GPR", attrs={
                "first_reg": reg_num, "count": 32 - reg_num,
                "helper": target,
            }, source=text)

        m = _RESTGPR_RE.search(target)
        if m:
            reg_num = int(m.group(1))
            return LiftedOp("EPILOGUE_RESTORE_GPR", attrs={
                "first_reg": reg_num, "count": 32 - reg_num,
                "helper": target,
            }, source=text)

        m = _SAVEFPR_RE.search(target)
        if m:
            reg_num = int(m.group(1))
            return LiftedOp("PROLOGUE_SAVE_FPR", attrs={
                "first_reg": reg_num, "count": 32 - reg_num,
                "helper": target,
            }, source=text)

        m = _RESTFPR_RE.search(target)
        if m:
            reg_num = int(m.group(1))
            return LiftedOp("EPILOGUE_RESTORE_FPR", attrs={
                "first_reg": reg_num, "count": 32 - reg_num,
                "helper": target,
            }, source=text)

        return LiftedOp("CALL", args=(target,), source=text)

    if mnemonic == "blr":
        return LiftedOp("RETURN", source=text)

    # ── Link register manipulation ──

    if mnemonic == "mflr" and operands:
        return LiftedOp("SAVE_LR", dest=operands[0], source=text)

    if mnemonic == "mtlr" and operands:
        return LiftedOp("RESTORE_LR", args=(operands[0],), source=text)

    # ── Stack frame setup (stwu r1, -N(r1) handled by STORE above,
    #    but we want to recognize the pattern) ──

    if mnemonic == "nop":
        return LiftedOp("NOP", source=text)

    return LiftedOp("UNSUPPORTED", args=tuple(operands), attrs={"mnemonic": mnemonic}, source=text)


# ── Function-level lifting ──────────────────────────────────────────

def lift_function_asm(function: FunctionAsm) -> LiftedFunction:
    """Lift all instructions in a parsed PPC function."""
    ops: list[LiftedOp] = []
    unsupported: list[str] = []
    for _, _, text in function.instructions:
        lifted = lift_instruction(text)
        if lifted is None:
            continue
        if lifted.name == "UNSUPPORTED":
            unsupported.append(text.strip())
            continue
        ops.append(lifted)

    lifted_func = LiftedFunction(name=function.mangled, ops=ops, unsupported=unsupported)

    # Extract prologue info
    lifted_func.prologue = _extract_prologue(ops, function)

    # Build CFG
    lifted_func.cfg = build_cfg(ops)

    return lifted_func


def _extract_prologue(ops: list[LiftedOp], function: FunctionAsm) -> PrologueInfo:
    """Extract prologue/epilogue metadata from lifted ops and FunctionAsm."""
    info = PrologueInfo()

    # From FunctionAsm (parsed from .cod listing)
    if function.stack_frame_size:
        info.stack_frame_size = function.stack_frame_size
    if function.callee_saved_gprs:
        info.callee_saved_gprs = len(function.callee_saved_gprs)
    if function.callee_saved_fprs:
        info.callee_saved_fprs = len(function.callee_saved_fprs)

    # From lifted ops: savegpr/savefpr helpers
    for op in ops:
        if op.name == "PROLOGUE_SAVE_GPR":
            info.uses_savegpr_helper = True
            reg = op.attrs.get("first_reg", 32)
            info.first_saved_gpr = min(info.first_saved_gpr, reg)
            info.callee_saved_gprs = max(info.callee_saved_gprs, 32 - reg)
        elif op.name == "PROLOGUE_SAVE_FPR":
            info.uses_savefpr_helper = True
            reg = op.attrs.get("first_reg", 32)
            info.first_saved_fpr = min(info.first_saved_fpr, reg)
            info.callee_saved_fprs = max(info.callee_saved_fprs, 32 - reg)

    # Detect stack frame from stwu r1, -N(r1) pattern
    if info.stack_frame_size == 0:
        for op in ops[:10]:  # Only look in first 10 ops
            if op.name == "STORE" and op.source:
                m, operands = _split_instruction(op.source)
                if m == "stwu" and len(operands) >= 2 and "r1" in operands[0]:
                    offset_match = re.search(r"-(\d+)\(", operands[1])
                    if offset_match:
                        info.stack_frame_size = int(offset_match.group(1))
                        break

    return info


# ── CFG construction ────────────────────────────────────────────────

def build_cfg(ops: list[LiftedOp]) -> ControlFlowGraph:
    """Build a control flow graph from lifted ops.

    Identifies basic blocks at branch targets, after branches/calls,
    and detects loop back-edges.
    """
    cfg = ControlFlowGraph()
    if not ops:
        return cfg

    # Pass 1: Identify block boundaries
    # A new block starts: at index 0, after any BRANCH/GOTO/RETURN/DISPATCH,
    # and at any target label referenced by BRANCH/GOTO.
    branch_targets: set[str] = set()
    block_starts: set[int] = {0}

    for i, op in enumerate(ops):
        if op.name in ("BRANCH", "GOTO", "LOOP_DECREMENT", "LOOP_EXIT",
                        "DISPATCH", "RETURN"):
            if i + 1 < len(ops):
                block_starts.add(i + 1)
            # Record branch target for label resolution
            if op.args:
                branch_targets.add(op.args[0])

    # Pass 2: Build blocks
    sorted_starts = sorted(block_starts)
    for idx, start in enumerate(sorted_starts):
        end = sorted_starts[idx + 1] if idx + 1 < len(sorted_starts) else len(ops)
        label = f"bb_{start}"
        block = BasicBlock(label=label, ops=list(ops[start:end]))
        if start == 0:
            block.is_entry = True
        cfg.blocks[label] = block

    # Pass 3: Add edges
    for label, block in cfg.blocks.items():
        if not block.ops:
            continue
        last = block.ops[-1]

        if last.name == "RETURN":
            block.is_exit = True
            continue

        if last.name in ("BRANCH", "LOOP_DECREMENT", "LOOP_EXIT"):
            # Conditional: falls through + branches to target
            _add_fallthrough_edge(cfg, label)
            # We can't resolve label names to block indices without symbol info,
            # but we record the intent
            if last.args:
                block.successors.append(f"target:{last.args[0]}")

        elif last.name == "GOTO":
            if last.args:
                block.successors.append(f"target:{last.args[0]}")

        elif last.name == "DISPATCH":
            block.successors.append("indirect")

        else:
            _add_fallthrough_edge(cfg, label)

    # Pass 4: Detect back-edges (heuristic: successor index < current index)
    block_order = {label: i for i, label in enumerate(cfg.blocks)}
    for label, block in cfg.blocks.items():
        for succ in block.successors:
            if succ.startswith("target:"):
                # Can't resolve symbolic targets, but detect obvious patterns
                pass
            elif succ in block_order and block_order[succ] <= block_order[label]:
                cfg.back_edges.append((label, succ))
                if succ not in cfg.loop_headers:
                    cfg.loop_headers.append(succ)

    # Detect counted loops from LOOP_DECREMENT
    for label, block in cfg.blocks.items():
        for op in block.ops:
            if op.name == "LOOP_DECREMENT":
                if label not in cfg.loop_headers:
                    cfg.loop_headers.append(label)

    return cfg


def _add_fallthrough_edge(cfg: ControlFlowGraph, label: str) -> None:
    """Add an edge from label to the next sequential block."""
    labels = list(cfg.blocks.keys())
    try:
        idx = labels.index(label)
        if idx + 1 < len(labels):
            next_label = labels[idx + 1]
            cfg.blocks[label].successors.append(next_label)
            cfg.blocks[next_label].predecessors.append(label)
    except ValueError:
        pass


# ── Pattern sequence detection ──────────────────────────────────────

def _parse_load_offset(op: LiftedOp) -> int | None:
    """Extract numeric offset from a LOAD op's memory operand (e.g., '0x10(r3)' -> 0x10)."""
    if not op.args:
        return None
    mem = op.args[0]
    # Match "0x10(r3)", "0(r31)" -- and the MASM "60h(r1)" spelling, which is
    # what real MSVC .cod listings actually use. Measured 2026-08-17 over
    # msvc-src/results/branch_polarity.json: 5 occurrences of `NNh(rN)` (e.g.
    # `stwu r1,-60h(r1)`) and ZERO of `0xNN(rN)`. Accepting only the C spelling
    # meant returning None -- "no offset" -- for every hex displacement on the
    # one input shape that is on disk.
    import re
    m = re.match(r"(-?0x[0-9a-fA-F]+|-?[0-9a-fA-F]+h|-?\d+)\(", mem)
    if m:
        val = m.group(1)
        if val.endswith("h"):
            return int(val[:-1], 16)
        return int(val, 16) if val.startswith(("0x", "-0x")) else int(val)
    return None


def _parse_load_base_reg(op: LiftedOp) -> str | None:
    """Extract base register from a LOAD op's memory operand (e.g., '0x10(r3)' -> 'r3')."""
    if not op.args:
        return None
    mem = op.args[0]
    import re
    # Same three spellings as _parse_load_offset -- see the note there.
    m = re.match(r"-?(?:0x[0-9a-fA-F]+|[0-9a-fA-F]+h|\d+)\((\w+)\)", mem)
    return m.group(1) if m else None


def detect_vtable_dispatch(ops: list[LiftedOp]) -> list[dict[str, Any]]:
    """Detect vtable virtual call patterns in lifted ops.

    Pattern: LOAD vtable (lwz rX, 0(rObj)) -> LOAD slot (lwz rY, N(rX))
             -> SET_CTR rY -> INDIRECT_CALL (bctrl)

    Captures:
      - slot_offset: byte offset into vtable (e.g., 0x10 = 4th slot)
      - receiver_reg: register holding the object pointer
      - vbtable_offset: offset of vtable pointer from object base (usually 0)
    """
    dispatches: list[dict[str, Any]] = []
    for i in range(len(ops) - 1):
        if ops[i].name == "SET_CTR" and i + 1 < len(ops) and ops[i + 1].name in ("INDIRECT_CALL", "DISPATCH"):
            # Look backward for the vtable loads
            vtable_loads = []
            for j in range(max(0, i - 6), i):
                if ops[j].name == "LOAD":
                    vtable_loads.append(j)
            if len(vtable_loads) >= 2:
                # First load: vtable pointer from object (lwz rX, off(rObj))
                vptr_load = ops[vtable_loads[-2]]
                # Last load before SET_CTR: slot from vtable (lwz rY, slot(rX))
                slot_load = ops[vtable_loads[-1]]

                vbtable_offset = _parse_load_offset(vptr_load)
                slot_offset = _parse_load_offset(slot_load)
                receiver_reg = _parse_load_base_reg(vptr_load)

                # Check for vbtable indirection (3+ loads = multiple inheritance)
                has_vbtable_indirection = len(vtable_loads) >= 3

                dispatches.append({
                    "kind": "virtual_tail_dispatch"
                    if ops[i + 1].name == "DISPATCH"
                    else "virtual_dispatch",
                    "op_range": [vtable_loads[0], i + 1],
                    "load_count": len(vtable_loads),
                    "ctr_index": i,
                    "call_index": i + 1,
                    "slot_offset": slot_offset,
                    "vbtable_offset": vbtable_offset,
                    "receiver_reg": receiver_reg,
                    "has_vbtable_indirection": has_vbtable_indirection,
                })
    return dispatches


def detect_inline_wrapper(ops: list[LiftedOp]) -> list[dict[str, Any]]:
    """Detect inline wrapper / trivial forwarding function shapes.

    Wrapper shapes indicate functions that are thin wrappers around another
    call, which the compiler may inline or outline differently between
    target and base.

    Shapes detected:
      - trivial_forwarding: single call + return, no other logic
      - accessor_load: single LOAD + return (member accessor)
      - accessor_store: single STORE (member setter)
      - parameter_passthrough: call where all args come from function params
    """
    wrappers: list[dict[str, Any]] = []

    # Filter out prologue/epilogue ops for analysis
    body_ops = [op for op in ops if op.name not in (
        "PROLOGUE_SAVE", "EPILOGUE_RESTORE", "LINK_RETURN",
        "STACK_ALLOC", "STACK_FREE",
    )]

    if not body_ops:
        return wrappers

    # Trivial forwarding: CALL + RETURN (possibly with ASSIGN for return value)
    non_nop = [op for op in body_ops if op.name != "NOP"]
    if len(non_nop) == 1 and non_nop[0].name == "CALL":
        wrappers.append({
            "kind": "inline_wrapper",
            "category": "trivial_tail_forward",
            "confidence": 0.95,
            "op_range": [0, len(ops) - 1],
            "call_source": non_nop[0].source,
        })
    elif len(non_nop) == 2:
        if non_nop[0].name == "CALL" and non_nop[1].name == "RETURN":
            wrappers.append({
                "kind": "inline_wrapper",
                "category": "trivial_forwarding",
                "confidence": 0.92,
                "op_range": [0, len(ops) - 1],
                "call_source": non_nop[0].source,
            })
        elif non_nop[0].name == "LOAD" and non_nop[1].name == "RETURN":
            offset = _parse_load_offset(non_nop[0])
            wrappers.append({
                "kind": "inline_wrapper",
                "category": "accessor_load",
                "confidence": 0.9,
                "op_range": [0, len(ops) - 1],
                "member_offset": offset,
            })
    elif len(non_nop) == 1 and non_nop[0].name == "STORE":
        wrappers.append({
            "kind": "inline_wrapper",
            "category": "accessor_store",
            "confidence": 0.9,
            "op_range": [0, len(ops) - 1],
        })
    elif 2 <= len(non_nop) <= 4:
        # Check for call + assign + return (return value forwarding)
        calls = [op for op in non_nop if op.name in ("CALL", "INDIRECT_CALL")]
        returns = [op for op in non_nop if op.name == "RETURN"]
        assigns = [op for op in non_nop if op.name == "ASSIGN"]
        if len(calls) == 1 and len(returns) <= 1 and len(assigns) <= 1:
            other = len(non_nop) - len(calls) - len(returns) - len(assigns)
            if other == 0:
                wrappers.append({
                    "kind": "inline_wrapper",
                    "category": "return_forwarding",
                    "confidence": 0.85,
                    "op_range": [0, len(ops) - 1],
                    "call_source": calls[0].source,
                })

    return wrappers


def detect_switch_dispatch(ops: list[LiftedOp]) -> list[dict[str, Any]]:
    """Detect switch table dispatch patterns.

    Pattern: LOAD_INDEXED (lwzx from table) -> ADD -> SET_CTR -> DISPATCH (bctr)
    Or: series of CMP + BRANCH pairs (if-else chain for sparse switch)
    """
    dispatches: list[dict[str, Any]] = []

    # Pattern 1: Table-based switch (lwzx + mtctr + bctr)
    for i in range(len(ops)):
        if ops[i].name == "DISPATCH":
            # Look backward for SET_CTR
            for j in range(max(0, i - 5), i):
                if ops[j].name == "SET_CTR":
                    # Look for LOAD_INDEXED before SET_CTR
                    has_indexed = any(
                        ops[k].name == "LOAD_INDEXED"
                        for k in range(max(0, j - 4), j)
                    )
                    if has_indexed:
                        dispatches.append({
                            "kind": "switch_table",
                            "dispatch_index": i,
                            "table_based": has_indexed,
                            "op_range": [max(0, j - 4), i],
                        })
                    break

    # Pattern 2: Dense switch lowered as CTR-stepped case chain.
    for i, op in enumerate(ops):
        if op.name != "SET_CTR":
            continue
        loop_steps = [
            (j, ops[j])
            for j in range(i + 1, min(len(ops), i + 12))
            if ops[j].name in ("LOOP_DECREMENT", "LOOP_EXIT")
        ]
        if len(loop_steps) >= 2:
            dispatches.append({
                "kind": "switch_ctr_chain",
                "dispatch_index": loop_steps[0][0],
                "case_count": len(loop_steps) + 1,
                "op_range": [i, loop_steps[-1][0]],
            })

    # Pattern 3: If-else chain (consecutive CMP + BRANCH pairs)
    cmp_branch_runs = []
    run_start = None
    run_count = 0
    i = 0
    while i < len(ops) - 1:
        if ops[i].name == "CMP" and ops[i + 1].name == "BRANCH":
            if run_start is None:
                run_start = i
            run_count += 1
            i += 2  # Skip past the pair
        else:
            if run_count >= 3:  # 3+ consecutive CMP+BRANCH = likely switch
                cmp_branch_runs.append({
                    "kind": "switch_if_chain",
                    "op_range": [run_start, i - 1],
                    "case_count": run_count,
                })
            run_start = None
            run_count = 0
            i += 1
    if run_count >= 3 and run_start is not None:
        cmp_branch_runs.append({
            "kind": "switch_if_chain",
            "op_range": [run_start, len(ops) - 1],
            "case_count": run_count,
        })

    if not dispatches and not cmp_branch_runs:
        cmp_count = sum(1 for op in ops if op.name == "CMP")
        branch_like_count = sum(
            1
            for op in ops
            if op.name == "BRANCH" or op.name in ("LOOP_DECREMENT", "LOOP_EXIT")
        )
        goto_count = sum(1 for op in ops if op.name == "GOTO")
        if cmp_count >= 2 and (branch_like_count >= 2 or goto_count >= 2):
            cmp_branch_runs.append({
                "kind": "switch_if_chain",
                "op_range": [0, len(ops) - 1],
                "case_count": max(cmp_count, branch_like_count, goto_count),
            })

    dispatches.extend(cmp_branch_runs)
    return dispatches


def detect_call_shapes(ops: list[LiftedOp]) -> list[dict[str, Any]]:
    """Detect call/return lowering shapes."""
    shapes: list[dict[str, Any]] = []

    direct_calls = [i for i, op in enumerate(ops) if op.name == "CALL"]
    tail_gotos = [
        i for i, op in enumerate(ops)
        if op.name == "GOTO" and op.args and op.args[0].startswith("?")
    ]

    if tail_gotos and tail_gotos[-1] == len(ops) - 1:
        shapes.append({
            "kind": "call_shape",
            "category": "tail_direct_call",
            "confidence": 0.95,
            "op_index": tail_gotos[-1],
            "target": ops[tail_gotos[-1]].args[0],
        })

    if direct_calls and ops and ops[-1].name == "RETURN":
        if len(direct_calls) == 1:
            shapes.append({
                "kind": "call_shape",
                "category": "direct_call_return",
                "confidence": 0.85,
                "op_index": direct_calls[0],
                "target": ops[direct_calls[0]].args[0] if ops[direct_calls[0]].args else None,
            })
        else:
            shapes.append({
                "kind": "call_shape",
                "category": "call_sequence_return",
                "confidence": 0.8,
                "op_range": [direct_calls[0], direct_calls[-1]],
                "call_count": len(direct_calls),
            })

        for idx in direct_calls:
            if idx + 1 >= len(ops):
                continue
            next_op = ops[idx + 1]
            if next_op.name != "ASSIGN" or next_op.args != ("r3",):
                continue
            saved_reg = next_op.dest
            if not saved_reg:
                continue
            restored = any(
                later.name == "ASSIGN"
                and later.dest == "r3"
                and later.args == (saved_reg,)
                for later in ops[idx + 2 :]
            )
            if restored:
                shapes.append({
                    "kind": "call_shape",
                    "category": "cached_return_value",
                    "confidence": 0.9,
                    "op_range": [idx, len(ops) - 1],
                    "saved_reg": saved_reg,
                })
                break

    return shapes


def detect_argument_materialization(ops: list[LiftedOp]) -> list[dict[str, Any]]:
    """Detect how function arguments are prepared before call sites.

    Classifies argument setup strategy for each bl/bctrl:
      - register_direct: Args loaded directly into r3-r10 from memory/immediates
      - pre_computed: Args computed into callee-saved regs, then mr'd into arg regs
      - stack_spilled: Args pushed to stack via stw rN, offset(r1) before call
      - mixed: Combination of the above strategies
    """
    _ARG_REGS = {f"r{i}" for i in range(3, 11)}
    _CALLEE_SAVED = {f"r{i}" for i in range(13, 32)}

    results: list[dict[str, Any]] = []

    for i, op in enumerate(ops):
        if op.name not in ("CALL", "INDIRECT_CALL"):
            continue

        # Scan backward up to 8 instructions before the call
        window_start = max(0, i - 8)
        window = ops[window_start:i]

        direct_count = 0
        precomputed_count = 0
        spilled_count = 0
        details: list[dict[str, str]] = []

        for w_op in window:
            dest = w_op.dest or ""

            if w_op.name == "ASSIGN" and dest in _ARG_REGS and w_op.args:
                src = w_op.args[0]
                if src in _CALLEE_SAVED:
                    precomputed_count += 1
                    details.append({"reg": dest, "kind": "pre_computed", "src": src})
                else:
                    direct_count += 1
                    details.append({"reg": dest, "kind": "register_direct", "src": src})

            elif w_op.name in ("LOAD", "CONST") and dest in _ARG_REGS:
                direct_count += 1
                details.append({"reg": dest, "kind": "register_direct"})

            elif w_op.name == "STORE" and w_op.source:
                mnemonic, operands = _split_instruction(w_op.source)
                if mnemonic in ("stw", "std") and len(operands) >= 2:
                    mem_arg = operands[1]
                    if "(r1)" in mem_arg or "(sp)" in mem_arg:
                        spilled_count += 1
                        details.append({"reg": operands[0], "kind": "stack_spilled"})

        arg_count = direct_count + precomputed_count + spilled_count
        if arg_count == 0:
            continue

        if spilled_count > 0 and (direct_count > 0 or precomputed_count > 0):
            strategy = "mixed"
        elif spilled_count > 0:
            strategy = "stack_spilled"
        elif precomputed_count > 0 and direct_count == 0:
            strategy = "pre_computed"
        elif precomputed_count > 0:
            strategy = "mixed"
        else:
            strategy = "register_direct"

        call_target = op.args[0] if op.args else "<indirect>"
        results.append({
            "call_target": call_target,
            "arg_count": arg_count,
            "strategy": strategy,
            "details": details,
            "op_index": i,
        })

    return results


def detect_sparse_switch(ops: list[LiftedOp], cfg: ControlFlowGraph | None = None) -> list[dict[str, Any]]:
    """Detect sparse switch lowering strategies beyond jump tables.

    Classifies switch implementation:
      - binary_search: cmpwi+beq/bge/ble forming binary tree (log N compares)
      - linear_scan: Sequential cmpwi+beq chain (N compares)
      - hybrid: Binary search partitioning with small linear scans at leaves
    Jump tables are already detected by detect_switch_dispatch() and skipped here.
    """
    results: list[dict[str, Any]] = []

    # Skip if a jump table is already present (detected separately)
    has_jump_table = any(op.name == "DISPATCH" for op in ops)
    if has_jump_table:
        return results

    # Collect all CMP + conditional branch pairs
    pairs: list[dict[str, Any]] = []
    i = 0
    while i < len(ops) - 1:
        if ops[i].name == "CMP" and ops[i + 1].name == "BRANCH":
            cond = ops[i + 1].attrs.get("condition", "")
            target = ops[i + 1].args[0] if ops[i + 1].args else None
            # Extract comparison value
            cmp_val = None
            if ops[i].args and len(ops[i].args) >= 2:
                cmp_val = _parse_immediate(ops[i].args[1])
            pairs.append({
                "index": i,
                "condition": cond,
                "target": target,
                "cmp_value": cmp_val,
            })
            i += 2
        else:
            i += 1

    if len(pairs) < 3:
        return results

    # Classify: binary search uses inequality branches (bge, ble, bgt, blt)
    # mixed with equality (beq). Linear scan uses only beq.
    eq_count = sum(1 for p in pairs if p["condition"] == "eq")
    ineq_count = sum(1 for p in pairs if p["condition"] in ("ge", "le", "gt", "lt"))

    compare_count = len(pairs)
    depth = 0

    if ineq_count == 0 and eq_count >= 3:
        strategy = "linear_scan"
        depth = 1
    elif ineq_count > 0 and eq_count == 0:
        strategy = "binary_search"
        # Estimate depth from compare count: depth ~= log2(cases)
        import math
        depth = max(1, int(math.log2(max(1, compare_count))))
    elif ineq_count > 0 and eq_count > 0:
        strategy = "hybrid"
        import math
        depth = max(1, int(math.log2(max(1, compare_count))))
    else:
        return results

    # Estimate case count from comparison values when available
    cmp_values = [p["cmp_value"] for p in pairs if p["cmp_value"] is not None]
    estimated_cases = len(set(cmp_values)) if cmp_values else compare_count

    results.append({
        "strategy": strategy,
        "estimated_cases": estimated_cases,
        "compare_count": compare_count,
        "depth": depth,
    })

    return results


def detect_float_conversion(ops: list[LiftedOp]) -> list[dict[str, Any]]:
    """Detect float-to-int conversion sequences.

    Pattern: FLOAT_TO_INT (fctiwz) + STORE_INT_FROM_FLOAT (stfiwx) + LOAD (lwz)
    """
    conversions: list[dict[str, Any]] = []
    for i in range(len(ops)):
        if ops[i].name == "FLOAT_TO_INT":
            # Look ahead for stfiwx + lwz
            has_store = False
            has_load = False
            for j in range(i + 1, min(len(ops), i + 4)):
                if ops[j].name == "STORE_INT_FROM_FLOAT":
                    has_store = True
                elif ops[j].name == "LOAD" and has_store:
                    has_load = True
                    conversions.append({
                        "kind": "float_to_int",
                        "pattern": "fctiwz_stfiwx_lwz" if has_load else "fctiwz_stfiwx",
                        "op_range": [i, j],
                    })
                    break
            if not has_load and has_store:
                conversions.append({
                    "kind": "float_to_int",
                    "pattern": "fctiwz_stfiwx",
                    "op_range": [i, i + 2],
                })
    return conversions


# ── Shape facts derivation ──────────────────────────────────────────

def derive_shape_facts(function: LiftedFunction) -> list[dict[str, Any]]:
    """Derive comprehensive codegen facts from lifted PPC ops."""
    facts: list[dict[str, Any]] = []
    ops = function.ops
    mnemonics = [_mnemonic_of_op(op) for op in ops]

    # ── Byte fusion facts ──
    for idx, op in enumerate(ops):
        if op.name == "FUSED_SHR_MASK":
            facts.append({
                "kind": "byte_fusion",
                "category": "fused_shr_mask",
                "confidence": 0.95,
                "op_index": idx,
            })
        elif op.name == "FUSED_SHL_MASK":
            facts.append({
                "kind": "byte_fusion",
                "category": "fused_shl_mask",
                "confidence": 0.95,
                "op_index": idx,
            })

    if any(op.name == "BYTE_MASK" for op in ops) and \
            any(op.name in ("SHR", "SHL") for op in ops):
        facts.append({
            "kind": "byte_fusion",
            "category": "separate_shift_and_mask",
            "confidence": 0.75,
        })

    # ── Bool materialization patterns ──
    bool_patterns = (
        (("addic", "subfe"), "zero_test", 0.95),
        (("addi", "cntlzw", "rlwinm"), "equality_nonzero", 0.9),
        (("addi", "addic", "subfe"), "inequality_nonzero", 0.9),
        (("neg", "andc", "srwi"), "signed_positive", 0.95),
        (("subfic", "subfe", "clrlwi"), "unsigned_ordered", 0.9),
        (("subfc", "eqv", "srwi", "addze"), "signed_ordered", 0.85),
        (("srawi", "srwi", "subfc", "adde"), "signed_greater_equal", 0.8),
        (("subfc", "subfze"), "unsigned_greater_equal", 0.8),
        (("srawi", "subfc", "adde"), "signed_ge_short", 0.8),
    )

    for pattern, category, confidence in bool_patterns:
        width = len(pattern)
        for start in range(0, len(mnemonics) - width + 1):
            if tuple(mnemonics[start:start + width]) == pattern:
                facts.append({
                    "kind": "bool_materialization",
                    "category": category,
                    "confidence": confidence,
                    "op_range": [start, start + width - 1],
                    "mnemonics": list(pattern),
                })
                break

    # ── Switch dispatch facts ──
    switch_patterns = detect_switch_dispatch(ops)
    for sp in switch_patterns:
        facts.append({
            "kind": "switch_dispatch",
            "category": sp["kind"],
            "confidence": (
                0.92 if sp["kind"] == "switch_table"
                else 0.88 if sp["kind"] == "switch_ctr_chain"
                else 0.72
            ),
            "table_based": sp.get("table_based", False),
            "case_count": sp.get("case_count"),
            "op_range": sp["op_range"],
        })

    # ── Virtual dispatch facts ──
    vtable_patterns = detect_vtable_dispatch(ops)
    for vp in vtable_patterns:
        fact: dict[str, Any] = {
            "kind": "virtual_dispatch",
            "category": (
                "vtable_tail_call"
                if vp["kind"] == "virtual_tail_dispatch"
                else "vtable_call"
            ),
            "confidence": 0.92 if vp["kind"] == "virtual_tail_dispatch" else 0.9,
            "op_range": vp["op_range"],
        }
        if vp.get("slot_offset") is not None:
            fact["slot_offset"] = vp["slot_offset"]
        if vp.get("vbtable_offset") is not None:
            fact["vbtable_offset"] = vp["vbtable_offset"]
        if vp.get("receiver_reg"):
            fact["receiver_reg"] = vp["receiver_reg"]
        if vp.get("has_vbtable_indirection"):
            fact["has_vbtable_indirection"] = True
        facts.append(fact)

    # ── Inline wrapper facts ──
    for wp in detect_inline_wrapper(ops):
        facts.append(wp)

    # ── Call/return shape facts ──
    for cs in detect_call_shapes(ops):
        facts.append(dict(cs))

    # ── Argument materialization facts ──
    for am in detect_argument_materialization(ops):
        facts.append({
            "kind": "argument_materialization",
            "category": am["strategy"],
            "confidence": 0.85,
            "call_target": am["call_target"],
            "arg_count": am["arg_count"],
            "op_index": am["op_index"],
        })

    # ── Sparse switch facts ──
    sparse = detect_sparse_switch(ops, function.cfg)
    for sp in sparse:
        facts.append({
            "kind": "sparse_switch",
            "category": sp["strategy"],
            "confidence": 0.82,
            "estimated_cases": sp["estimated_cases"],
            "compare_count": sp["compare_count"],
            "depth": sp["depth"],
        })

    # ── Float conversion facts ──
    float_conversions = detect_float_conversion(ops)
    for fc in float_conversions:
        facts.append({
            "kind": "float_conversion",
            "category": fc["pattern"],
            "confidence": 0.9,
            "op_range": fc["op_range"],
        })

    # ── Prologue facts ──
    pro = function.prologue
    if pro.callee_saved_gprs > 0 or pro.callee_saved_fprs > 0:
        facts.append({
            "kind": "prologue_shape",
            "category": "register_save",
            "confidence": 0.95,
            "callee_saved_gprs": pro.callee_saved_gprs,
            "callee_saved_fprs": pro.callee_saved_fprs,
            "stack_frame_size": pro.stack_frame_size,
            "uses_savegpr_helper": pro.uses_savegpr_helper,
            "first_saved_gpr": pro.first_saved_gpr if pro.first_saved_gpr < 32 else None,
            "first_saved_fpr": pro.first_saved_fpr if pro.first_saved_fpr < 32 else None,
        })

    # ── CFG facts ──
    if function.cfg and function.cfg.block_count > 1:
        facts.append({
            "kind": "control_flow",
            "category": "cfg_complexity",
            "confidence": 0.85,
            "block_count": function.cfg.block_count,
            "edge_count": function.cfg.edge_count,
            "loop_count": function.cfg.loop_count,
            "nesting_depth": function.cfg.nesting_depth(),
        })

    # ── Aggregate operation profile ──
    op_profile = _compute_op_profile(ops)
    if op_profile:
        facts.append({
            "kind": "operation_profile",
            "category": "aggregate",
            "confidence": 1.0,
            **op_profile,
        })

    # ── Counted loop facts ──
    for idx, op in enumerate(ops):
        if op.name == "LOOP_DECREMENT":
            facts.append({
                "kind": "control_flow",
                "category": "counted_loop",
                "confidence": 0.95,
                "op_index": idx,
            })

    # ── Fused multiply-add facts ──
    fma_count = sum(1 for op in ops if op.name in ("FMADD", "FMSUB", "FNMADD", "FNMSUB"))
    if fma_count > 0:
        facts.append({
            "kind": "float_fusion",
            "category": "fused_multiply_add",
            "confidence": 0.95,
            "count": fma_count,
        })

    return facts


def _compute_op_profile(ops: list[LiftedOp]) -> dict[str, Any]:
    """Compute aggregate operation counts by category."""
    if not ops:
        return {}

    categories: dict[str, int] = {}
    for op in ops:
        cat = _op_category(op.name)
        categories[cat] = categories.get(cat, 0) + 1

    direct_calls = sum(1 for op in ops if op.name == "CALL")
    indirect_calls = sum(1 for op in ops if op.name == "INDIRECT_CALL")
    branches = sum(1 for op in ops if op.name == "BRANCH")
    gotos = sum(1 for op in ops if op.name == "GOTO")
    loads = sum(1 for op in ops if op.name in ("LOAD", "LOAD_INDEXED"))
    stores = sum(1 for op in ops if op.name in ("STORE", "STORE_INDEXED"))
    float_ops = sum(1 for op in ops if op.name.startswith("F") and op.name not in ("FUSED_SHR_MASK", "FUSED_SHL_MASK"))

    return {
        "total_ops": len(ops),
        "categories": categories,
        "direct_calls": direct_calls,
        "indirect_calls": indirect_calls,
        "branches": branches,
        "gotos": gotos,
        "loads": loads,
        "stores": stores,
        "float_ops": float_ops,
    }


def _op_category(name: str) -> str:
    """Categorize an op name into a broad family."""
    if name in ("ADD", "SUB", "MUL", "DIV", "MUL_HIGH", "MUL_HIGH_UNSIGNED",
                "DIV_UNSIGNED", "NEG"):
        return "arithmetic"
    if name in ("AND", "OR", "XOR", "NOT", "ANDC", "NAND", "NOR", "ORC",
                "EQV", "ROT_MASK", "ROT_INSERT", "BYTE_MASK",
                "FUSED_SHR_MASK", "FUSED_SHL_MASK", "SHR", "SHL", "SAR"):
        return "bitwise"
    if name in ("CMP", "FCMP", "BRANCH", "GOTO", "LOOP_DECREMENT", "LOOP_EXIT",
                "DISPATCH", "RETURN"):
        return "control_flow"
    if name in ("LOAD", "LOAD_INDEXED", "STORE", "STORE_INDEXED",
                "STORE_INT_FROM_FLOAT"):
        return "memory"
    if name in ("CALL", "INDIRECT_CALL", "PROLOGUE_SAVE_GPR", "PROLOGUE_SAVE_FPR",
                "EPILOGUE_RESTORE_GPR", "EPILOGUE_RESTORE_FPR"):
        return "call"
    if name in ("FADD", "FSUB", "FMUL", "FDIV", "FNEG", "FABS", "FNABS",
                "FMADD", "FMSUB", "FNMADD", "FNMSUB", "FLOAT_SELECT",
                "FLOAT_TO_INT", "FLOAT_ROUND_SINGLE",
                "FLOAT_RECIPROCAL_EST", "FLOAT_RSQRT_EST"):
        return "float"
    if name in ("ASSIGN", "CONST", "SAVE_LR", "RESTORE_LR", "SET_CTR",
                "READ_CTR", "NOP"):
        return "data_movement"
    if name in ("SIGN_EXTEND", "COUNT_LEADING_ZEROS"):
        return "type_conversion"
    if name in ("SUB_EXTEND", "SUB_CARRY", "SUB_FROM_IMM", "ADD_ZERO_EXTEND",
                "ADD_EXTEND", "SUBF_ZERO_EXTEND"):
        return "carry_chain"
    if name in ("CR_OR", "CR_AND", "CR_ANDC", "CR_XOR", "READ_CR", "WRITE_CR"):
        return "condition_register"
    return "other"


# ── Shape delta computation ─────────────────────────────────────────

def compute_shape_delta(
    lifted_ppc: LiftedFunction,
    source_il_ops: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """Compute machine-readable shape deltas between lifted PPC and source IL.

    Returns a structured diff describing:
    - Operation count differences by category
    - Control flow shape differences
    - Specific pattern matches/mismatches
    """
    delta: dict[str, Any] = {
        "ppc_total_ops": len(lifted_ppc.ops),
        "ppc_unsupported": len(lifted_ppc.unsupported),
    }

    # PPC-side profile
    ppc_profile = _compute_op_profile(lifted_ppc.ops)
    delta["ppc_profile"] = ppc_profile

    if source_il_ops:
        # Source IL profile
        il_categories: dict[str, int] = {}
        il_names: list[str] = []
        for op in source_il_ops:
            name = op.get("name", "")
            il_names.append(name)
            cat = _il_op_category(name)
            il_categories[cat] = il_categories.get(cat, 0) + 1

        delta["il_total_ops"] = len(source_il_ops)
        delta["il_profile"] = {"categories": il_categories}

        # Category-level differences
        all_cats = set(ppc_profile.get("categories", {}).keys()) | set(il_categories.keys())
        cat_deltas: dict[str, dict[str, int]] = {}
        for cat in sorted(all_cats):
            ppc_count = ppc_profile.get("categories", {}).get(cat, 0)
            il_count = il_categories.get(cat, 0)
            if ppc_count != il_count:
                cat_deltas[cat] = {"ppc": ppc_count, "il": il_count, "delta": ppc_count - il_count}
        delta["category_deltas"] = cat_deltas

        # Specific operation presence comparison
        ppc_op_names = set(op.name for op in lifted_ppc.ops)
        il_op_names = set(il_names)
        delta["ppc_only_ops"] = sorted(ppc_op_names - il_op_names)
        delta["il_only_ops"] = sorted(il_op_names - ppc_op_names)

        # Switch shape comparison
        ppc_has_switch = any(op.name == "DISPATCH" for op in lifted_ppc.ops) or \
                        any(op.name == "SET_CTR" for op in lifted_ppc.ops)
        il_has_switch = "SWITCH" in il_op_names or "SWITCH_TABLE" in il_op_names
        if ppc_has_switch or il_has_switch:
            delta["switch"] = {
                "ppc_has_switch": ppc_has_switch,
                "il_has_switch": il_has_switch,
                "match": ppc_has_switch == il_has_switch,
            }

        # Virtual call comparison
        ppc_has_vcall = bool(detect_vtable_dispatch(lifted_ppc.ops))
        il_has_vcall = "VCALL_SETUP" in il_op_names or "VCALL_BIND" in il_op_names
        if ppc_has_vcall or il_has_vcall:
            delta["virtual_call"] = {
                "ppc_has_vcall": ppc_has_vcall,
                "il_has_vcall": il_has_vcall,
                "match": ppc_has_vcall == il_has_vcall,
            }

        # Branch density comparison
        ppc_branches = ppc_profile.get("branches", 0)
        il_branches = sum(1 for n in il_names if n in ("COND_BRANCH", "GOTO"))
        if ppc_branches != il_branches:
            delta["branch_density"] = {
                "ppc": ppc_branches,
                "il": il_branches,
                "delta": ppc_branches - il_branches,
            }

    return delta


def _il_op_category(name: str) -> str:
    """Categorize an IL operation name into a broad family."""
    if name in ("ADD", "SUB", "MUL", "DIV", "MOD", "NEG"):
        return "arithmetic"
    if name in ("AND", "OR", "XOR", "NOT", "SHL", "SHR"):
        return "bitwise"
    if name in ("EQ", "NE", "LT", "LE", "GT", "GE", "COND_BRANCH", "GOTO",
                "FALLTHROUGH", "LABEL", "SWITCH", "SWITCH_TABLE", "CASE"):
        return "control_flow"
    if name in ("DEREF", "STORE", "MEMBER_PTR", "PTR_ADD"):
        return "memory"
    if name in ("CALL_START", "CALL_EXEC", "VCALL_SETUP", "VCALL_BIND"):
        return "call"
    if name in ("CAST",):
        return "type_conversion"
    if name in ("ASSIGN", "RETURN"):
        return "data_movement"
    if name in ("LOGICAL_NOT", "LOGICAL_OR", "LOGICAL_AND"):
        return "logical"
    if name in ("COMPOUND_ADD", "COMPOUND_SUB"):
        return "compound"
    return "other"


# ── CLI ─────────────────────────────────────────────────────────────

def _match_function(functions: dict[str, Any], needle: str) -> tuple[str, Any] | None:
    """Match a function by exact name first, then substring."""
    if needle in functions:
        return needle, functions[needle]
    for name, value in functions.items():
        if needle in name:
            return name, value
    return None


def _read_listing(listing_path: str) -> str:
    return Path(listing_path).read_text(encoding="utf-8")


def _extract_source_il(source_path: str, output_dir: str, function_name: str) -> tuple[str, list[str]]:
    il_base = capture_il(source_path, output_dir=output_dir)
    if not il_base:
        raise RuntimeError("failed to capture IL bundle")
    il = ILFile(il_base)
    matched = _match_function({func.name: func for func in il.functions}, function_name)
    if not matched:
        raise KeyError(f"function '{function_name}' not found in IL")
    name, func = matched
    return name, format_il_ops(func, il.symbols)


def _extract_lifted_ppc(source_path: str, output_dir: str, function_name: str) -> LiftedFunction:
    listing = compile_with_listing(source_path, output_dir)
    if not listing:
        raise RuntimeError("failed to compile PPC listing")
    functions = parse_asm_listing(listing)
    matched = _match_function(functions, function_name)
    if not matched:
        raise KeyError(f"function '{function_name}' not found in PPC listing")
    _, func = matched
    return lift_function_asm(func)


def cmd_lift_listing(args) -> None:
    listing = _read_listing(args.listing)
    functions = parse_asm_listing(listing)
    matched = _match_function(functions, args.function)
    if not matched:
        raise SystemExit(f"Function '{args.function}' not found in {args.listing}")
    _, func = matched
    lifted = lift_function_asm(func)
    shape_facts = derive_shape_facts(lifted)
    if args.json:
        data = lifted.to_dict()
        data["shape_facts"] = shape_facts
        if args.delta:
            data["shape_delta"] = compute_shape_delta(lifted)
        print(json.dumps(data, indent=2, sort_keys=True))
        return
    print(f"PPC function: {lifted.name}")
    print(f"  Prologue: {lifted.prologue.callee_saved_gprs} GPR, "
          f"{lifted.prologue.callee_saved_fprs} FPR, "
          f"frame={lifted.prologue.stack_frame_size}")
    if lifted.cfg:
        print(f"  CFG: {lifted.cfg.block_count} blocks, "
              f"{lifted.cfg.edge_count} edges, "
              f"{lifted.cfg.loop_count} loops")
    print()
    for op in lifted.ops:
        print(f"  {_format_op(op)}")
    if shape_facts:
        print("\nDerived shape facts:")
        for fact in shape_facts:
            kind = fact.get('kind', '?')
            category = fact.get('category', '?')
            conf = fact.get('confidence', 0.0)
            extras = {k: v for k, v in fact.items() if k not in ('kind', 'category', 'confidence')}
            extra_str = f"  {extras}" if extras else ""
            print(f"  {kind}: {category} (conf={conf:.2f}){extra_str}")
    if lifted.unsupported:
        print(f"\nUnsupported ({len(lifted.unsupported)}):")
        for text in lifted.unsupported:
            print(f"  {text}")


def cmd_compare_source(args) -> None:
    il_name, source_il = _extract_source_il(args.source, args.output_dir, args.function)
    lifted = _extract_lifted_ppc(args.source, args.output_dir, args.function)
    shape_facts = derive_shape_facts(lifted)
    delta = compute_shape_delta(lifted)
    data = {
        "function": il_name,
        "source_il": source_il,
        "lifted_ppc": [op.to_dict() for op in lifted.ops],
        "shape_facts": shape_facts,
        "shape_delta": delta,
        "prologue": lifted.prologue.to_dict(),
        "unsupported_ppc": lifted.unsupported,
    }
    if lifted.cfg:
        data["cfg"] = lifted.cfg.to_dict()
    if args.json:
        print(json.dumps(data, indent=2, sort_keys=True))
        return
    print(f"Function: {il_name}\n")
    print(f"Prologue: {lifted.prologue.callee_saved_gprs} GPR, "
          f"{lifted.prologue.callee_saved_fprs} FPR, "
          f"frame={lifted.prologue.stack_frame_size}")
    if lifted.cfg:
        print(f"CFG: {lifted.cfg.block_count} blocks, "
              f"{lifted.cfg.edge_count} edges, "
              f"{lifted.cfg.loop_count} loops")
    print("\nSource IL:")
    for line in source_il:
        print(f"  {line}")
    print("\nLifted PPC:")
    for op in lifted.ops:
        print(f"  {_format_op(op)}")
    if shape_facts:
        print("\nDerived shape facts:")
        for fact in shape_facts:
            kind = fact.get('kind', '?')
            category = fact.get('category', '?')
            conf = fact.get('confidence', 0.0)
            print(f"  {kind}: {category} (conf={conf:.2f})")
    if delta.get("category_deltas"):
        print("\nShape deltas by category:")
        for cat, d in delta["category_deltas"].items():
            print(f"  {cat}: PPC={d['ppc']} IL={d['il']} delta={d['delta']:+d}")
    if lifted.unsupported:
        print(f"\nUnsupported PPC ({len(lifted.unsupported)}):")
        for text in lifted.unsupported:
            print(f"  {text}")


def cmd_profile(args) -> None:
    """Profile all functions in a listing file."""
    listing = _read_listing(args.listing)
    functions = parse_asm_listing(listing)
    results = []
    for name, func in functions.items():
        lifted = lift_function_asm(func)
        facts = derive_shape_facts(lifted)
        result = {
            "name": name,
            "instruction_count": len(lifted.ops),
            "unsupported_count": len(lifted.unsupported),
            "fact_count": len(facts),
        }
        if lifted.prologue.callee_saved_gprs:
            result["callee_saved_gprs"] = lifted.prologue.callee_saved_gprs
        if lifted.prologue.callee_saved_fprs:
            result["callee_saved_fprs"] = lifted.prologue.callee_saved_fprs
        if lifted.cfg:
            result["blocks"] = lifted.cfg.block_count
            result["loops"] = lifted.cfg.loop_count
        for fact in facts:
            kind = fact.get("kind", "")
            cat = fact.get("category", "")
            result.setdefault("facts", []).append(f"{kind}:{cat}")
        results.append(result)

    if args.json:
        print(json.dumps(results, indent=2, sort_keys=True))
    else:
        for r in results:
            name = r["name"]
            insns = r["instruction_count"]
            unsup = r["unsupported_count"]
            coverage = (insns / (insns + unsup) * 100) if (insns + unsup) > 0 else 0
            blocks = r.get("blocks", 0)
            loops = r.get("loops", 0)
            fact_list = r.get("facts", [])
            facts_str = f"  facts: {', '.join(fact_list)}" if fact_list else ""
            print(f"{name}: {insns} ops ({coverage:.0f}% coverage) "
                  f"{blocks}bb {loops}loop{facts_str}")


def main() -> None:
    parser = argparse.ArgumentParser(description="PPC->IL lifter for synthesis guidance")
    sub = parser.add_subparsers(dest="command")

    p_lift = sub.add_parser("lift-listing", help="Lift one function from a PPC .cod listing")
    p_lift.add_argument("listing", help="Path to .cod/.asm listing")
    p_lift.add_argument("--function", required=True, help="Exact or substring function match")
    p_lift.add_argument("--json", action="store_true", help="Emit JSON")
    p_lift.add_argument("--delta", action="store_true", help="Include shape delta in JSON")

    p_compare = sub.add_parser("compare-source", help="Compare source IL against lifted PPC")
    p_compare.add_argument("source", help="Source file to compile")
    p_compare.add_argument("--function", required=True, help="Exact or substring function match")
    p_compare.add_argument("--output-dir", default="/tmp/claude-1000", help="Working directory")
    p_compare.add_argument("--json", action="store_true", help="Emit JSON")

    p_profile = sub.add_parser("profile", help="Profile all functions in a listing file")
    p_profile.add_argument("listing", help="Path to .cod/.asm listing")
    p_profile.add_argument("--json", action="store_true", help="Emit JSON")

    args = parser.parse_args()
    if args.command == "lift-listing":
        cmd_lift_listing(args)
    elif args.command == "compare-source":
        cmd_compare_source(args)
    elif args.command == "profile":
        cmd_profile(args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
