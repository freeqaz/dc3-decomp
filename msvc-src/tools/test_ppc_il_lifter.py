"""Tests for the PPC->IL lifter.

Tests instruction lifting, CFG construction, pattern detection, shape facts,
and shape delta computation across all supported instruction families.

Usage:
    python -m pytest msvc-src/tools/test_ppc_il_lifter.py -v
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ppc_il_lifter import (
    BasicBlock,
    ControlFlowGraph,
    LiftedFunction,
    LiftedOp,
    PrologueInfo,
    build_cfg,
    compute_shape_delta,
    derive_shape_facts,
    detect_argument_materialization,
    detect_float_conversion,
    detect_sparse_switch,
    detect_switch_dispatch,
    detect_vtable_dispatch,
    lift_instruction,
    _compute_op_profile,
    _op_category,
    _split_instruction,
    _parse_immediate,
    _parse_load_offset,
    _parse_load_base_reg,
)


# ---------------------------------------------------------------------------
# Instruction parsing helpers
# ---------------------------------------------------------------------------

class TestSplitInstruction(unittest.TestCase):
    def test_simple(self):
        self.assertEqual(_split_instruction("add r3, r4, r5"), ("add", ["r3", "r4", "r5"]))

    def test_comment(self):
        self.assertEqual(_split_instruction("li r3, 0  ; load zero"), ("li", ["r3", "0"]))

    def test_no_operands(self):
        self.assertEqual(_split_instruction("blr"), ("blr", []))

    def test_empty(self):
        self.assertEqual(_split_instruction(""), ("", []))

    def test_case_insensitive(self):
        m, ops = _split_instruction("ADD r3, r4, r5")
        self.assertEqual(m, "add")


class TestParseImmediate(unittest.TestCase):
    def test_decimal(self):
        self.assertEqual(_parse_immediate("42"), 42)

    def test_negative(self):
        self.assertEqual(_parse_immediate("-10"), -10)

    def test_hex_prefix(self):
        self.assertEqual(_parse_immediate("0xff"), 255)

    def test_hex_suffix(self):
        self.assertEqual(_parse_immediate("FFh"), 255)

    def test_hex_suffix_negative(self):
        self.assertEqual(_parse_immediate("-10h"), -16)

    def test_symbol(self):
        self.assertIsNone(_parse_immediate("some_label"))


# ---------------------------------------------------------------------------
# Single-instruction lifting: Original families
# ---------------------------------------------------------------------------

class TestLiftMoves(unittest.TestCase):
    def test_mr(self):
        op = lift_instruction("mr r3, r4")
        self.assertEqual(op.name, "ASSIGN")
        self.assertEqual(op.dest, "r3")
        self.assertEqual(op.args, ("r4",))

    def test_fmr(self):
        op = lift_instruction("fmr f1, f2")
        self.assertEqual(op.name, "ASSIGN")
        self.assertEqual(op.dest, "f1")

    def test_li(self):
        op = lift_instruction("li r3, 0")
        self.assertEqual(op.name, "CONST")
        self.assertEqual(op.attrs["constant"], 0)

    def test_lis(self):
        op = lift_instruction("lis r3, 0x4000")
        self.assertEqual(op.name, "CONST")
        self.assertTrue(op.attrs.get("hi16"))


class TestLiftRlwinm(unittest.TestCase):
    def test_clrlwi_byte(self):
        op = lift_instruction("clrlwi r3, r4, 24")
        self.assertEqual(op.name, "BYTE_MASK")
        self.assertEqual(op.attrs.get("mask"), 0xFF)

    def test_clrlwi_nonbyte(self):
        op = lift_instruction("clrlwi r3, r4, 16")
        self.assertEqual(op.name, "BYTE_MASK")
        self.assertNotIn("mask", op.attrs)

    def test_extrwi(self):
        op = lift_instruction("extrwi r3, r4, 8, 16")
        self.assertEqual(op.name, "FUSED_SHR_MASK")

    def test_clrlslwi(self):
        op = lift_instruction("clrlslwi r3, r4, 24, 2")
        self.assertEqual(op.name, "FUSED_SHL_MASK")

    def test_srwi(self):
        op = lift_instruction("srwi r3, r4, 8")
        self.assertEqual(op.name, "SHR")

    def test_slwi(self):
        op = lift_instruction("slwi r3, r4, 2")
        self.assertEqual(op.name, "SHL")

    def test_srawi(self):
        op = lift_instruction("srawi r3, r4, 31")
        self.assertEqual(op.name, "SAR")

    def test_rlwinm_byte_mask(self):
        op = lift_instruction("rlwinm r3, r4, 0, 24, 31")
        self.assertEqual(op.name, "BYTE_MASK")

    def test_rlwinm_fused(self):
        op = lift_instruction("rlwinm r3, r4, 24, 24, 31")
        self.assertEqual(op.name, "FUSED_SHR_MASK")

    def test_rlwinm_generic(self):
        op = lift_instruction("rlwinm r3, r4, 4, 0, 27")
        self.assertEqual(op.name, "ROT_MASK")

    def test_rlwinm_record(self):
        op = lift_instruction("rlwinm. r3, r4, 4, 0, 27")
        self.assertEqual(op.name, "ROT_MASK")
        self.assertTrue(op.attrs.get("record"))


class TestLiftBoolCarry(unittest.TestCase):
    def test_neg(self):
        op = lift_instruction("neg r3, r4")
        self.assertEqual(op.name, "NEG")

    def test_cntlzw(self):
        op = lift_instruction("cntlzw r3, r4")
        self.assertEqual(op.name, "COUNT_LEADING_ZEROS")

    def test_andc(self):
        op = lift_instruction("andc r3, r4, r5")
        self.assertEqual(op.name, "ANDC")

    def test_eqv(self):
        op = lift_instruction("eqv r3, r4, r5")
        self.assertEqual(op.name, "EQV")

    def test_subfe(self):
        op = lift_instruction("subfe r3, r4, r5")
        self.assertEqual(op.name, "SUB_EXTEND")

    def test_subfc(self):
        op = lift_instruction("subfc r3, r4, r5")
        self.assertEqual(op.name, "SUB_CARRY")

    def test_subfic(self):
        op = lift_instruction("subfic r3, r4, 1")
        self.assertEqual(op.name, "SUB_FROM_IMM")
        self.assertEqual(op.attrs["immediate"], 1)

    def test_addze(self):
        op = lift_instruction("addze r3, r4")
        self.assertEqual(op.name, "ADD_ZERO_EXTEND")

    def test_adde(self):
        op = lift_instruction("adde r3, r4, r5")
        self.assertEqual(op.name, "ADD_EXTEND")

    def test_subfze(self):
        op = lift_instruction("subfze r3, r4")
        self.assertEqual(op.name, "SUBF_ZERO_EXTEND")


class TestLiftALU(unittest.TestCase):
    def test_add(self):
        op = lift_instruction("add r3, r4, r5")
        self.assertEqual(op.name, "ADD")

    def test_addi(self):
        op = lift_instruction("addi r3, r4, 10")
        self.assertEqual(op.name, "ADD")

    def test_subf(self):
        op = lift_instruction("subf r3, r4, r5")
        self.assertEqual(op.name, "SUB")

    def test_subf_record(self):
        op = lift_instruction("subf. r3, r4, r5")
        self.assertEqual(op.name, "SUB")
        self.assertTrue(op.attrs.get("record"))

    def test_and(self):
        op = lift_instruction("and r3, r4, r5")
        self.assertEqual(op.name, "AND")

    def test_andi_dot(self):
        op = lift_instruction("andi. r3, r4, 255")
        self.assertEqual(op.name, "AND")

    def test_or(self):
        op = lift_instruction("or r3, r4, r5")
        self.assertEqual(op.name, "OR")

    def test_xor(self):
        op = lift_instruction("xor r3, r4, r5")
        self.assertEqual(op.name, "XOR")

    def test_not(self):
        op = lift_instruction("not r3, r4")
        self.assertEqual(op.name, "NOT")


class TestLiftCompare(unittest.TestCase):
    def test_cmpwi(self):
        op = lift_instruction("cmpwi r3, 0")
        self.assertEqual(op.name, "CMP")
        self.assertTrue(op.attrs["signed"])
        self.assertEqual(op.attrs["width"], "imm")

    def test_cmplwi(self):
        op = lift_instruction("cmplwi r3, 5")
        self.assertEqual(op.name, "CMP")
        self.assertFalse(op.attrs["signed"])

    def test_cmpw(self):
        op = lift_instruction("cmpw r3, r4")
        self.assertEqual(op.name, "CMP")
        self.assertEqual(op.attrs["width"], "reg")

    def test_cmplw(self):
        op = lift_instruction("cmplw r3, r4")
        self.assertEqual(op.name, "CMP")
        self.assertFalse(op.attrs["signed"])

    def test_cr_stripped(self):
        op = lift_instruction("cmpwi cr0, r3, 0")
        self.assertEqual(op.name, "CMP")
        self.assertEqual(op.args, ("r3", "0"))


class TestLiftBranch(unittest.TestCase):
    def test_beq(self):
        op = lift_instruction("beq label1")
        self.assertEqual(op.name, "BRANCH")
        self.assertEqual(op.attrs["condition"], "eq")

    def test_bne(self):
        op = lift_instruction("bne label2")
        self.assertEqual(op.name, "BRANCH")
        self.assertEqual(op.attrs["condition"], "ne")

    def test_blt(self):
        op = lift_instruction("blt label3")
        self.assertEqual(op.name, "BRANCH")
        self.assertEqual(op.attrs["condition"], "lt")

    def test_beq_with_cr(self):
        """Branch with explicit CR field."""
        op = lift_instruction("beq cr7, label1")
        self.assertEqual(op.name, "BRANCH")
        self.assertEqual(op.attrs["condition"], "eq")
        self.assertEqual(op.attrs.get("cr"), "cr7")

    def test_beqlr(self):
        """Conditional return (beqlr)."""
        op = lift_instruction("beqlr")
        self.assertEqual(op.name, "BRANCH")
        self.assertEqual(op.attrs["condition"], "eq")
        self.assertTrue(op.attrs.get("returns"))

    def test_bnelr(self):
        """Conditional return (bnelr)."""
        op = lift_instruction("bnelr")
        self.assertEqual(op.name, "BRANCH")
        self.assertEqual(op.attrs["condition"], "ne")
        self.assertTrue(op.attrs.get("returns"))

    def test_b_goto(self):
        op = lift_instruction("b some_label")
        self.assertEqual(op.name, "GOTO")

    def test_blr(self):
        op = lift_instruction("blr")
        self.assertEqual(op.name, "RETURN")


class TestLiftMemory(unittest.TestCase):
    def test_lwz(self):
        op = lift_instruction("lwz r3, 0(r4)")
        self.assertEqual(op.name, "LOAD")
        self.assertEqual(op.attrs["kind"], "lwz")

    def test_stw(self):
        op = lift_instruction("stw r3, 0(r4)")
        self.assertEqual(op.name, "STORE")
        self.assertEqual(op.attrs["kind"], "stw")

    def test_lfs(self):
        op = lift_instruction("lfs f1, 0(r3)")
        self.assertEqual(op.name, "LOAD")
        self.assertEqual(op.attrs["kind"], "lfs")

    def test_stfs(self):
        op = lift_instruction("stfs f1, 0(r3)")
        self.assertEqual(op.name, "STORE")

    def test_bl(self):
        op = lift_instruction("bl some_function")
        self.assertEqual(op.name, "CALL")
        self.assertEqual(op.args, ("some_function",))


# ---------------------------------------------------------------------------
# NEW: Float arithmetic
# ---------------------------------------------------------------------------

class TestLiftFloat(unittest.TestCase):
    def test_fadd(self):
        op = lift_instruction("fadd f1, f2, f3")
        self.assertEqual(op.name, "FADD")
        self.assertFalse(op.attrs["single"])

    def test_fadds(self):
        op = lift_instruction("fadds f1, f2, f3")
        self.assertEqual(op.name, "FADD")
        self.assertTrue(op.attrs["single"])

    def test_fsub(self):
        op = lift_instruction("fsub f1, f2, f3")
        self.assertEqual(op.name, "FSUB")

    def test_fmul(self):
        op = lift_instruction("fmul f1, f2, f3")
        self.assertEqual(op.name, "FMUL")

    def test_fdiv(self):
        op = lift_instruction("fdiv f1, f2, f3")
        self.assertEqual(op.name, "FDIV")

    def test_fneg(self):
        op = lift_instruction("fneg f1, f2")
        self.assertEqual(op.name, "FNEG")

    def test_fabs(self):
        op = lift_instruction("fabs f1, f2")
        self.assertEqual(op.name, "FABS")

    def test_fnabs(self):
        op = lift_instruction("fnabs f1, f2")
        self.assertEqual(op.name, "FNABS")

    def test_fmadd(self):
        op = lift_instruction("fmadd f1, f2, f3, f4")
        self.assertEqual(op.name, "FMADD")
        self.assertFalse(op.attrs["single"])

    def test_fmadds(self):
        op = lift_instruction("fmadds f1, f2, f3, f4")
        self.assertEqual(op.name, "FMADD")
        self.assertTrue(op.attrs["single"])

    def test_fmsub(self):
        op = lift_instruction("fmsub f1, f2, f3, f4")
        self.assertEqual(op.name, "FMSUB")

    def test_fnmadd(self):
        op = lift_instruction("fnmadd f1, f2, f3, f4")
        self.assertEqual(op.name, "FNMADD")

    def test_fnmsub(self):
        op = lift_instruction("fnmsub f1, f2, f3, f4")
        self.assertEqual(op.name, "FNMSUB")

    def test_frsp(self):
        op = lift_instruction("frsp f1, f2")
        self.assertEqual(op.name, "FLOAT_ROUND_SINGLE")
        self.assertEqual(op.attrs["conversion"], "double_to_single")

    def test_fctiwz(self):
        op = lift_instruction("fctiwz f1, f2")
        self.assertEqual(op.name, "FLOAT_TO_INT")
        self.assertEqual(op.attrs["conversion"], "float_to_int_truncate")

    def test_fctiw(self):
        op = lift_instruction("fctiw f1, f2")
        self.assertEqual(op.name, "FLOAT_TO_INT")
        self.assertEqual(op.attrs["conversion"], "float_to_int_round")

    def test_fsel(self):
        op = lift_instruction("fsel f1, f2, f3, f4")
        self.assertEqual(op.name, "FLOAT_SELECT")

    def test_fcmpu(self):
        op = lift_instruction("fcmpu cr0, f1, f2")
        self.assertEqual(op.name, "FCMP")
        self.assertFalse(op.attrs["ordered"])

    def test_fcmpo(self):
        op = lift_instruction("fcmpo cr0, f1, f2")
        self.assertEqual(op.name, "FCMP")
        self.assertTrue(op.attrs["ordered"])

    def test_fres(self):
        op = lift_instruction("fres f1, f2")
        self.assertEqual(op.name, "FLOAT_RECIPROCAL_EST")

    def test_frsqrte(self):
        op = lift_instruction("frsqrte f1, f2")
        self.assertEqual(op.name, "FLOAT_RSQRT_EST")

    def test_stfiwx(self):
        op = lift_instruction("stfiwx f1, r0, r3")
        self.assertEqual(op.name, "STORE_INT_FROM_FLOAT")


# ---------------------------------------------------------------------------
# NEW: Multiply / Divide
# ---------------------------------------------------------------------------

class TestLiftMulDiv(unittest.TestCase):
    def test_mullw(self):
        op = lift_instruction("mullw r3, r4, r5")
        self.assertEqual(op.name, "MUL")

    def test_mulli(self):
        op = lift_instruction("mulli r3, r4, 10")
        self.assertEqual(op.name, "MUL")

    def test_mulhw(self):
        op = lift_instruction("mulhw r3, r4, r5")
        self.assertEqual(op.name, "MUL_HIGH")

    def test_mulhwu(self):
        op = lift_instruction("mulhwu r3, r4, r5")
        self.assertEqual(op.name, "MUL_HIGH_UNSIGNED")

    def test_divw(self):
        op = lift_instruction("divw r3, r4, r5")
        self.assertEqual(op.name, "DIV")

    def test_divwu(self):
        op = lift_instruction("divwu r3, r4, r5")
        self.assertEqual(op.name, "DIV_UNSIGNED")


# ---------------------------------------------------------------------------
# NEW: Sign extension
# ---------------------------------------------------------------------------

class TestLiftSignExtend(unittest.TestCase):
    def test_extsh(self):
        op = lift_instruction("extsh r3, r4")
        self.assertEqual(op.name, "SIGN_EXTEND")
        self.assertEqual(op.attrs["from_bits"], 16)

    def test_extsb(self):
        op = lift_instruction("extsb r3, r4")
        self.assertEqual(op.name, "SIGN_EXTEND")
        self.assertEqual(op.attrs["from_bits"], 8)


# ---------------------------------------------------------------------------
# NEW: Indirect dispatch / switch
# ---------------------------------------------------------------------------

class TestLiftDispatch(unittest.TestCase):
    def test_mtctr(self):
        op = lift_instruction("mtctr r3")
        self.assertEqual(op.name, "SET_CTR")

    def test_mfctr(self):
        op = lift_instruction("mfctr r3")
        self.assertEqual(op.name, "READ_CTR")

    def test_bctr(self):
        op = lift_instruction("bctr")
        self.assertEqual(op.name, "DISPATCH")

    def test_bctrl(self):
        op = lift_instruction("bctrl")
        self.assertEqual(op.name, "INDIRECT_CALL")


# ---------------------------------------------------------------------------
# NEW: Condition register ops
# ---------------------------------------------------------------------------

class TestLiftCR(unittest.TestCase):
    def test_cror(self):
        op = lift_instruction("cror 2, 0, 1")
        self.assertEqual(op.name, "CR_OR")

    def test_crand(self):
        op = lift_instruction("crand 2, 0, 1")
        self.assertEqual(op.name, "CR_AND")

    def test_crandc(self):
        op = lift_instruction("crandc 2, 0, 1")
        self.assertEqual(op.name, "CR_ANDC")

    def test_crxor(self):
        op = lift_instruction("crxor 2, 0, 1")
        self.assertEqual(op.name, "CR_XOR")

    def test_mfcr(self):
        op = lift_instruction("mfcr r3")
        self.assertEqual(op.name, "READ_CR")


# ---------------------------------------------------------------------------
# NEW: Loop instructions
# ---------------------------------------------------------------------------

class TestLiftLoops(unittest.TestCase):
    def test_bdnz(self):
        op = lift_instruction("bdnz loop_label")
        self.assertEqual(op.name, "LOOP_DECREMENT")

    def test_bdz(self):
        op = lift_instruction("bdz exit_label")
        self.assertEqual(op.name, "LOOP_EXIT")

    def test_bdnzeq(self):
        """Combined CTR decrement + condition branch."""
        op = lift_instruction("bdnzeq label")
        self.assertEqual(op.name, "LOOP_DECREMENT")
        self.assertEqual(op.attrs.get("condition"), "eq")

    def test_bdzeq(self):
        op = lift_instruction("bdzeq label")
        self.assertEqual(op.name, "LOOP_EXIT")
        self.assertEqual(op.attrs.get("condition"), "eq")


# ---------------------------------------------------------------------------
# NEW: Prologue/epilogue helpers
# ---------------------------------------------------------------------------

class TestLiftPrologue(unittest.TestCase):
    def test_savegprlr(self):
        op = lift_instruction("bl __savegprlr_14")
        self.assertEqual(op.name, "PROLOGUE_SAVE_GPR")
        self.assertEqual(op.attrs["first_reg"], 14)
        self.assertEqual(op.attrs["count"], 18)

    def test_restgprlr(self):
        op = lift_instruction("bl __restgprlr_14")
        self.assertEqual(op.name, "EPILOGUE_RESTORE_GPR")
        self.assertEqual(op.attrs["first_reg"], 14)

    def test_savefpr(self):
        op = lift_instruction("bl __savefpr_20")
        self.assertEqual(op.name, "PROLOGUE_SAVE_FPR")
        self.assertEqual(op.attrs["first_reg"], 20)
        self.assertEqual(op.attrs["count"], 12)

    def test_restfpr(self):
        op = lift_instruction("bl __restfpr_20")
        self.assertEqual(op.name, "EPILOGUE_RESTORE_FPR")

    def test_regular_bl_not_prologue(self):
        op = lift_instruction("bl some_function")
        self.assertEqual(op.name, "CALL")


# ---------------------------------------------------------------------------
# NEW: Indexed memory
# ---------------------------------------------------------------------------

class TestLiftIndexedMemory(unittest.TestCase):
    def test_lwzx(self):
        op = lift_instruction("lwzx r3, r4, r5")
        self.assertEqual(op.name, "LOAD_INDEXED")
        self.assertTrue(op.attrs["indexed"])

    def test_lbzx(self):
        op = lift_instruction("lbzx r3, r4, r5")
        self.assertEqual(op.name, "LOAD_INDEXED")

    def test_stwx(self):
        op = lift_instruction("stwx r3, r4, r5")
        self.assertEqual(op.name, "STORE_INDEXED")

    def test_lfsx(self):
        op = lift_instruction("lfsx f1, r3, r4")
        self.assertEqual(op.name, "LOAD_INDEXED")

    def test_stfsx(self):
        op = lift_instruction("stfsx f1, r3, r4")
        self.assertEqual(op.name, "STORE_INDEXED")


# ---------------------------------------------------------------------------
# NEW: Update-form loads/stores
# ---------------------------------------------------------------------------

class TestLiftUpdateForm(unittest.TestCase):
    def test_lwzu(self):
        op = lift_instruction("lwzu r3, 4(r4)")
        self.assertEqual(op.name, "LOAD")
        self.assertTrue(op.attrs.get("update"))

    def test_stwu(self):
        op = lift_instruction("stwu r1, -80(r1)")
        self.assertEqual(op.name, "STORE")
        self.assertTrue(op.attrs.get("update"))


# ---------------------------------------------------------------------------
# NEW: Link register and nop
# ---------------------------------------------------------------------------

class TestLiftMisc(unittest.TestCase):
    def test_mflr(self):
        op = lift_instruction("mflr r0")
        self.assertEqual(op.name, "SAVE_LR")

    def test_mtlr(self):
        op = lift_instruction("mtlr r0")
        self.assertEqual(op.name, "RESTORE_LR")

    def test_nop(self):
        op = lift_instruction("nop")
        self.assertEqual(op.name, "NOP")

    def test_unsupported(self):
        op = lift_instruction("dcbt r0, r3")
        self.assertEqual(op.name, "UNSUPPORTED")
        self.assertEqual(op.attrs["mnemonic"], "dcbt")


# ---------------------------------------------------------------------------
# NEW: Shift register-form
# ---------------------------------------------------------------------------

class TestLiftRegisterShift(unittest.TestCase):
    def test_srw(self):
        op = lift_instruction("srw r3, r4, r5")
        self.assertEqual(op.name, "SHR")
        self.assertTrue(op.attrs.get("reg_shift"))

    def test_slw(self):
        op = lift_instruction("slw r3, r4, r5")
        self.assertEqual(op.name, "SHL")
        self.assertTrue(op.attrs.get("reg_shift"))

    def test_sraw(self):
        op = lift_instruction("sraw r3, r4, r5")
        self.assertEqual(op.name, "SAR")
        self.assertTrue(op.attrs.get("reg_shift"))


# ---------------------------------------------------------------------------
# NEW: Rotate and insert
# ---------------------------------------------------------------------------

class TestLiftRlwimi(unittest.TestCase):
    def test_rlwimi(self):
        op = lift_instruction("rlwimi r3, r4, 4, 0, 27")
        self.assertEqual(op.name, "ROT_INSERT")
        self.assertEqual(op.attrs["canonical"], "rlwimi")


# ---------------------------------------------------------------------------
# NEW: Additional ALU
# ---------------------------------------------------------------------------

class TestLiftExtendedALU(unittest.TestCase):
    def test_nand(self):
        op = lift_instruction("nand r3, r4, r5")
        self.assertEqual(op.name, "NAND")

    def test_nor(self):
        op = lift_instruction("nor r3, r4, r5")
        self.assertEqual(op.name, "NOR")

    def test_orc(self):
        op = lift_instruction("orc r3, r4, r5")
        self.assertEqual(op.name, "ORC")

    def test_addic_dot(self):
        op = lift_instruction("addic. r3, r4, 0")
        self.assertEqual(op.name, "ADD")
        self.assertTrue(op.attrs.get("record"))

    def test_oris(self):
        op = lift_instruction("oris r3, r4, 0x4000")
        self.assertEqual(op.name, "OR")


# ---------------------------------------------------------------------------
# CFG construction
# ---------------------------------------------------------------------------

class TestBuildCFG(unittest.TestCase):
    def test_empty(self):
        cfg = build_cfg([])
        self.assertEqual(cfg.block_count, 0)

    def test_linear(self):
        ops = [
            LiftedOp("LOAD", dest="r3", args=("0(r4)",)),
            LiftedOp("ADD", dest="r3", args=("r3", "r4")),
            LiftedOp("RETURN"),
        ]
        cfg = build_cfg(ops)
        self.assertEqual(cfg.block_count, 1)

    def test_branch_creates_blocks(self):
        ops = [
            LiftedOp("CMP", args=("r3", "0")),
            LiftedOp("BRANCH", args=("label1",), attrs={"condition": "eq"}),
            LiftedOp("ADD", dest="r3", args=("r3", "r4")),
            LiftedOp("RETURN"),
        ]
        cfg = build_cfg(ops)
        self.assertEqual(cfg.block_count, 2)

    def test_goto_creates_block(self):
        ops = [
            LiftedOp("GOTO", args=("end",)),
            LiftedOp("ADD", dest="r3", args=("r3", "r4")),
            LiftedOp("RETURN"),
        ]
        cfg = build_cfg(ops)
        self.assertEqual(cfg.block_count, 2)

    def test_dispatch_creates_block(self):
        ops = [
            LiftedOp("SET_CTR", args=("r3",)),
            LiftedOp("DISPATCH"),
            LiftedOp("RETURN"),
        ]
        cfg = build_cfg(ops)
        self.assertEqual(cfg.block_count, 2)

    def test_entry_and_exit(self):
        ops = [
            LiftedOp("LOAD", dest="r3", args=("0(r4)",)),
            LiftedOp("RETURN"),
        ]
        cfg = build_cfg(ops)
        block = list(cfg.blocks.values())[0]
        self.assertTrue(block.is_entry)
        self.assertTrue(block.is_exit)

    def test_counted_loop_detected(self):
        ops = [
            LiftedOp("CONST", dest="r3", attrs={"constant": 10}),
            LiftedOp("LOOP_DECREMENT", args=("loop_start",)),
            LiftedOp("RETURN"),
        ]
        cfg = build_cfg(ops)
        self.assertGreater(len(cfg.loop_headers), 0)


# ---------------------------------------------------------------------------
# Pattern detection
# ---------------------------------------------------------------------------

class TestDetectVtableDispatch(unittest.TestCase):
    def test_basic_vcall(self):
        ops = [
            LiftedOp("LOAD", dest="r11", args=("0(r31)",), attrs={"kind": "lwz"}),
            LiftedOp("LOAD", dest="r11", args=("8(r11)",), attrs={"kind": "lwz"}),
            LiftedOp("SET_CTR", args=("r11",)),
            LiftedOp("INDIRECT_CALL"),
        ]
        results = detect_vtable_dispatch(ops)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["kind"], "virtual_dispatch")
        self.assertEqual(results[0]["load_count"], 2)

    def test_no_vcall(self):
        ops = [
            LiftedOp("LOAD", dest="r3", args=("0(r4)",)),
            LiftedOp("CALL", args=("some_func",)),
        ]
        results = detect_vtable_dispatch(ops)
        self.assertEqual(len(results), 0)


class TestDetectSwitchDispatch(unittest.TestCase):
    def test_table_switch(self):
        ops = [
            LiftedOp("CONST", dest="r11", attrs={"constant": 0x100}),
            LiftedOp("LOAD_INDEXED", dest="r11", args=("r3", "r11"), attrs={"kind": "lwzx", "indexed": True}),
            LiftedOp("ADD", dest="r11", args=("r11", "r12")),
            LiftedOp("SET_CTR", args=("r11",)),
            LiftedOp("DISPATCH"),
        ]
        results = detect_switch_dispatch(ops)
        self.assertTrue(any(r["kind"] == "switch_table" for r in results))

    def test_if_chain_switch(self):
        ops = []
        for i in range(5):
            ops.append(LiftedOp("CMP", args=("r3", str(i)), attrs={"signed": True}))
            ops.append(LiftedOp("BRANCH", args=(f"case_{i}",), attrs={"condition": "eq"}))
        ops.append(LiftedOp("RETURN"))
        results = detect_switch_dispatch(ops)
        self.assertTrue(any(r["kind"] == "switch_if_chain" for r in results))
        chain = [r for r in results if r["kind"] == "switch_if_chain"][0]
        self.assertEqual(chain["case_count"], 5)

    def test_no_switch(self):
        ops = [
            LiftedOp("CMP", args=("r3", "0")),
            LiftedOp("BRANCH", args=("label1",)),
            LiftedOp("RETURN"),
        ]
        results = detect_switch_dispatch(ops)
        self.assertEqual(len(results), 0)


class TestDetectFloatConversion(unittest.TestCase):
    def test_fctiwz_stfiwx_lwz(self):
        ops = [
            LiftedOp("FLOAT_TO_INT", dest="f0", args=("f1",)),
            LiftedOp("STORE_INT_FROM_FLOAT", args=("f0", "r0", "r1")),
            LiftedOp("LOAD", dest="r3", args=("0(r1)",), attrs={"kind": "lwz"}),
        ]
        results = detect_float_conversion(ops)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["pattern"], "fctiwz_stfiwx_lwz")


# ---------------------------------------------------------------------------
# Shape facts
# ---------------------------------------------------------------------------

class TestShapeFacts(unittest.TestCase):
    def _make_func(self, ops_list, name="test"):
        func = LiftedFunction(name=name, ops=ops_list, unsupported=[])
        func.prologue = PrologueInfo()
        func.cfg = build_cfg(ops_list)
        return func

    def test_byte_fusion_fused(self):
        func = self._make_func([
            LiftedOp("FUSED_SHR_MASK", dest="r3", args=("r4",)),
        ])
        facts = derive_shape_facts(func)
        byte_facts = [f for f in facts if f["kind"] == "byte_fusion"]
        self.assertTrue(any(f["category"] == "fused_shr_mask" for f in byte_facts))

    def test_byte_fusion_separate(self):
        func = self._make_func([
            LiftedOp("BYTE_MASK", dest="r3", args=("r4",)),
            LiftedOp("SHR", dest="r3", args=("r3",)),
        ])
        facts = derive_shape_facts(func)
        byte_facts = [f for f in facts if f["kind"] == "byte_fusion"]
        self.assertTrue(any(f["category"] == "separate_shift_and_mask" for f in byte_facts))

    def test_bool_zero_test(self):
        func = self._make_func([
            LiftedOp("ADD", source="addic r3, r3, -1"),
            LiftedOp("SUB_EXTEND", source="subfe r3, r3, r3"),
        ])
        facts = derive_shape_facts(func)
        bool_facts = [f for f in facts if f["kind"] == "bool_materialization"]
        self.assertTrue(any(f["category"] == "zero_test" for f in bool_facts))

    def test_switch_table_fact(self):
        ops = [
            LiftedOp("CONST", dest="r11", attrs={"constant": 0x100}),
            LiftedOp("LOAD_INDEXED", dest="r11", args=("r3", "r11"), attrs={"kind": "lwzx", "indexed": True}),
            LiftedOp("ADD", dest="r11", args=("r11", "r12")),
            LiftedOp("SET_CTR", args=("r11",)),
            LiftedOp("DISPATCH"),
        ]
        func = self._make_func(ops)
        facts = derive_shape_facts(func)
        switch_facts = [f for f in facts if f["kind"] == "switch_dispatch"]
        self.assertGreater(len(switch_facts), 0)

    def test_vtable_fact(self):
        ops = [
            LiftedOp("LOAD", dest="r11", args=("0(r31)",), attrs={"kind": "lwz"}),
            LiftedOp("LOAD", dest="r11", args=("8(r11)",), attrs={"kind": "lwz"}),
            LiftedOp("SET_CTR", args=("r11",)),
            LiftedOp("INDIRECT_CALL"),
        ]
        func = self._make_func(ops)
        facts = derive_shape_facts(func)
        vcall_facts = [f for f in facts if f["kind"] == "virtual_dispatch"]
        self.assertGreater(len(vcall_facts), 0)

    def test_prologue_fact(self):
        ops = [
            LiftedOp("PROLOGUE_SAVE_GPR", attrs={"first_reg": 14, "count": 18, "helper": "__savegprlr_14"}),
            LiftedOp("LOAD", dest="r3", args=("0(r4)",)),
            LiftedOp("RETURN"),
        ]
        func = self._make_func(ops)
        func.prologue = PrologueInfo(
            callee_saved_gprs=18, uses_savegpr_helper=True, first_saved_gpr=14,
        )
        facts = derive_shape_facts(func)
        prologue_facts = [f for f in facts if f["kind"] == "prologue_shape"]
        self.assertGreater(len(prologue_facts), 0)
        self.assertEqual(prologue_facts[0]["callee_saved_gprs"], 18)

    def test_cfg_complexity_fact(self):
        ops = [
            LiftedOp("CMP", args=("r3", "0")),
            LiftedOp("BRANCH", args=("label1",), attrs={"condition": "eq"}),
            LiftedOp("ADD", dest="r3", args=("r3", "r4")),
            LiftedOp("RETURN"),
        ]
        func = self._make_func(ops)
        facts = derive_shape_facts(func)
        cfg_facts = [f for f in facts if f["kind"] == "control_flow" and f["category"] == "cfg_complexity"]
        self.assertGreater(len(cfg_facts), 0)
        self.assertEqual(cfg_facts[0]["block_count"], 2)

    def test_operation_profile_fact(self):
        ops = [
            LiftedOp("LOAD", dest="r3", args=("0(r4)",)),
            LiftedOp("ADD", dest="r3", args=("r3", "r4")),
            LiftedOp("CALL", args=("func",)),
            LiftedOp("RETURN"),
        ]
        func = self._make_func(ops)
        facts = derive_shape_facts(func)
        profile_facts = [f for f in facts if f["kind"] == "operation_profile"]
        self.assertEqual(len(profile_facts), 1)
        self.assertEqual(profile_facts[0]["total_ops"], 4)
        self.assertEqual(profile_facts[0]["direct_calls"], 1)
        self.assertEqual(profile_facts[0]["loads"], 1)

    def test_counted_loop_fact(self):
        ops = [
            LiftedOp("CONST", dest="r3", attrs={"constant": 10}),
            LiftedOp("LOOP_DECREMENT", args=("loop_start",)),
            LiftedOp("RETURN"),
        ]
        func = self._make_func(ops)
        facts = derive_shape_facts(func)
        loop_facts = [f for f in facts if f["kind"] == "control_flow" and f["category"] == "counted_loop"]
        self.assertGreater(len(loop_facts), 0)

    def test_fma_fact(self):
        ops = [
            LiftedOp("FMADD", dest="f1", args=("f2", "f3", "f4")),
            LiftedOp("FMSUB", dest="f5", args=("f6", "f7", "f8")),
            LiftedOp("RETURN"),
        ]
        func = self._make_func(ops)
        facts = derive_shape_facts(func)
        fma_facts = [f for f in facts if f["kind"] == "float_fusion"]
        self.assertGreater(len(fma_facts), 0)
        self.assertEqual(fma_facts[0]["count"], 2)

    def test_float_conversion_fact(self):
        ops = [
            LiftedOp("FLOAT_TO_INT", dest="f0", args=("f1",)),
            LiftedOp("STORE_INT_FROM_FLOAT", args=("f0", "r0", "r1")),
            LiftedOp("LOAD", dest="r3", args=("0(r1)",), attrs={"kind": "lwz"}),
            LiftedOp("RETURN"),
        ]
        func = self._make_func(ops)
        facts = derive_shape_facts(func)
        conv_facts = [f for f in facts if f["kind"] == "float_conversion"]
        self.assertGreater(len(conv_facts), 0)


# ---------------------------------------------------------------------------
# Operation categories
# ---------------------------------------------------------------------------

class TestOpCategory(unittest.TestCase):
    def test_arithmetic(self):
        self.assertEqual(_op_category("ADD"), "arithmetic")
        self.assertEqual(_op_category("MUL"), "arithmetic")
        self.assertEqual(_op_category("DIV"), "arithmetic")

    def test_bitwise(self):
        self.assertEqual(_op_category("AND"), "bitwise")
        self.assertEqual(_op_category("SHR"), "bitwise")
        self.assertEqual(_op_category("ROT_MASK"), "bitwise")

    def test_control_flow(self):
        self.assertEqual(_op_category("BRANCH"), "control_flow")
        self.assertEqual(_op_category("GOTO"), "control_flow")
        self.assertEqual(_op_category("DISPATCH"), "control_flow")

    def test_float(self):
        self.assertEqual(_op_category("FADD"), "float")
        self.assertEqual(_op_category("FMADD"), "float")
        self.assertEqual(_op_category("FLOAT_TO_INT"), "float")

    def test_memory(self):
        self.assertEqual(_op_category("LOAD"), "memory")
        self.assertEqual(_op_category("STORE_INDEXED"), "memory")

    def test_call(self):
        self.assertEqual(_op_category("CALL"), "call")
        self.assertEqual(_op_category("INDIRECT_CALL"), "call")

    def test_carry_chain(self):
        self.assertEqual(_op_category("SUB_EXTEND"), "carry_chain")
        self.assertEqual(_op_category("ADD_ZERO_EXTEND"), "carry_chain")

    def test_type_conversion(self):
        self.assertEqual(_op_category("SIGN_EXTEND"), "type_conversion")

    def test_condition_register(self):
        self.assertEqual(_op_category("CR_OR"), "condition_register")
        self.assertEqual(_op_category("READ_CR"), "condition_register")


# ---------------------------------------------------------------------------
# Shape delta computation
# ---------------------------------------------------------------------------

class TestShapeDelta(unittest.TestCase):
    def test_ppc_only(self):
        func = LiftedFunction(
            name="test", unsupported=[],
            ops=[
                LiftedOp("LOAD", dest="r3", args=("0(r4)",)),
                LiftedOp("ADD", dest="r3", args=("r3", "r4")),
                LiftedOp("RETURN"),
            ],
        )
        func.prologue = PrologueInfo()
        delta = compute_shape_delta(func)
        self.assertEqual(delta["ppc_total_ops"], 3)
        self.assertIn("ppc_profile", delta)

    def test_with_il(self):
        func = LiftedFunction(
            name="test", unsupported=[],
            ops=[
                LiftedOp("LOAD", dest="r3", args=("0(r4)",)),
                LiftedOp("ADD", dest="r3", args=("r3", "r4")),
                LiftedOp("RETURN"),
            ],
        )
        func.prologue = PrologueInfo()
        il_ops = [
            {"name": "DEREF", "type": "op"},
            {"name": "ADD", "type": "op"},
            {"name": "RETURN", "type": "return"},
        ]
        delta = compute_shape_delta(func, il_ops)
        self.assertEqual(delta["il_total_ops"], 3)
        self.assertIn("category_deltas", delta)
        self.assertIn("ppc_only_ops", delta)
        self.assertIn("il_only_ops", delta)

    def test_switch_comparison(self):
        func = LiftedFunction(
            name="test", unsupported=[],
            ops=[
                LiftedOp("SET_CTR", args=("r11",)),
                LiftedOp("DISPATCH"),
            ],
        )
        func.prologue = PrologueInfo()
        il_ops = [
            {"name": "SWITCH", "type": "switch"},
            {"name": "SWITCH_TABLE", "type": "switch_table"},
        ]
        delta = compute_shape_delta(func, il_ops)
        self.assertTrue(delta["switch"]["ppc_has_switch"])
        self.assertTrue(delta["switch"]["il_has_switch"])
        self.assertTrue(delta["switch"]["match"])

    def test_vcall_comparison(self):
        func = LiftedFunction(
            name="test", unsupported=[],
            ops=[
                LiftedOp("LOAD", dest="r11", args=("0(r31)",)),
                LiftedOp("LOAD", dest="r12", args=("0x10(r11)",)),
                LiftedOp("SET_CTR", args=("r12",)),
                LiftedOp("INDIRECT_CALL"),
            ],
        )
        func.prologue = PrologueInfo()
        il_ops = [
            {"name": "VCALL_SETUP", "type": "vcall_setup"},
            {"name": "VCALL_BIND", "type": "vcall_bind"},
            {"name": "CALL_EXEC", "type": "call_exec"},
        ]
        delta = compute_shape_delta(func, il_ops)
        self.assertTrue(delta["virtual_call"]["ppc_has_vcall"])
        self.assertTrue(delta["virtual_call"]["il_has_vcall"])

    def test_branch_density_delta(self):
        func = LiftedFunction(
            name="test", unsupported=[],
            ops=[
                LiftedOp("BRANCH", args=("l1",), attrs={"condition": "eq"}),
                LiftedOp("BRANCH", args=("l2",), attrs={"condition": "ne"}),
                LiftedOp("RETURN"),
            ],
        )
        func.prologue = PrologueInfo()
        il_ops = [
            {"name": "COND_BRANCH", "type": "branch"},
            {"name": "RETURN", "type": "return"},
        ]
        delta = compute_shape_delta(func, il_ops)
        self.assertIn("branch_density", delta)
        self.assertEqual(delta["branch_density"]["ppc"], 2)
        self.assertEqual(delta["branch_density"]["il"], 1)


# ---------------------------------------------------------------------------
# Operation profile
# ---------------------------------------------------------------------------

class TestOpProfile(unittest.TestCase):
    def test_basic(self):
        ops = [
            LiftedOp("LOAD", dest="r3"),
            LiftedOp("ADD", dest="r3"),
            LiftedOp("CALL", args=("f",)),
            LiftedOp("INDIRECT_CALL"),
            LiftedOp("BRANCH", args=("l",)),
            LiftedOp("RETURN"),
        ]
        profile = _compute_op_profile(ops)
        self.assertEqual(profile["total_ops"], 6)
        self.assertEqual(profile["direct_calls"], 1)
        self.assertEqual(profile["indirect_calls"], 1)
        self.assertEqual(profile["branches"], 1)
        self.assertEqual(profile["loads"], 1)

    def test_empty(self):
        self.assertEqual(_compute_op_profile([]), {})


# ---------------------------------------------------------------------------
# Prologue info
# ---------------------------------------------------------------------------

class TestPrologueInfo(unittest.TestCase):
    def test_to_dict(self):
        info = PrologueInfo(
            callee_saved_gprs=5, callee_saved_fprs=2,
            stack_frame_size=80, uses_savegpr_helper=True,
            first_saved_gpr=27,
        )
        d = info.to_dict()
        self.assertEqual(d["callee_saved_gprs"], 5)
        self.assertEqual(d["callee_saved_fprs"], 2)
        self.assertEqual(d["stack_frame_size"], 80)
        self.assertTrue(d["uses_savegpr_helper"])
        self.assertEqual(d["first_saved_gpr"], 27)

    def test_no_saved_regs(self):
        info = PrologueInfo()
        d = info.to_dict()
        self.assertIsNone(d["first_saved_gpr"])
        self.assertIsNone(d["first_saved_fpr"])


# ---------------------------------------------------------------------------
# Argument materialization detection
# ---------------------------------------------------------------------------

class TestDetectArgumentMaterialization(unittest.TestCase):
    def test_register_direct(self):
        """Args loaded directly into r3-r10 from memory."""
        ops = [
            LiftedOp("LOAD", dest="r3", args=("0(r31)",), attrs={"kind": "lwz"},
                     source="lwz r3, 0(r31)"),
            LiftedOp("LOAD", dest="r4", args=("4(r31)",), attrs={"kind": "lwz"},
                     source="lwz r4, 4(r31)"),
            LiftedOp("CALL", args=("some_func",)),
        ]
        results = detect_argument_materialization(ops)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["strategy"], "register_direct")
        self.assertEqual(results[0]["arg_count"], 2)
        self.assertEqual(results[0]["call_target"], "some_func")

    def test_pre_computed(self):
        """Args mr'd from callee-saved regs into arg regs."""
        ops = [
            LiftedOp("ASSIGN", dest="r3", args=("r28",), source="mr r3, r28"),
            LiftedOp("ASSIGN", dest="r4", args=("r29",), source="mr r4, r29"),
            LiftedOp("CALL", args=("some_func",)),
        ]
        results = detect_argument_materialization(ops)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["strategy"], "pre_computed")
        self.assertEqual(results[0]["arg_count"], 2)

    def test_stack_spilled(self):
        """Args pushed to stack via stw."""
        ops = [
            LiftedOp("STORE", args=("r5", "8(r1)"), attrs={"kind": "stw"},
                     source="stw r5, 8(r1)"),
            LiftedOp("STORE", args=("r6", "12(r1)"), attrs={"kind": "stw"},
                     source="stw r6, 12(r1)"),
            LiftedOp("CALL", args=("many_args_func",)),
        ]
        results = detect_argument_materialization(ops)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["strategy"], "stack_spilled")

    def test_mixed_strategy(self):
        """Mix of register-direct and pre-computed."""
        ops = [
            LiftedOp("LOAD", dest="r3", args=("0(r31)",), attrs={"kind": "lwz"},
                     source="lwz r3, 0(r31)"),
            LiftedOp("ASSIGN", dest="r4", args=("r28",), source="mr r4, r28"),
            LiftedOp("CALL", args=("mixed_func",)),
        ]
        results = detect_argument_materialization(ops)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["strategy"], "mixed")

    def test_indirect_call(self):
        """Indirect call via bctrl."""
        ops = [
            LiftedOp("CONST", dest="r3", attrs={"constant": 42}, source="li r3, 42"),
            LiftedOp("INDIRECT_CALL", source="bctrl"),
        ]
        results = detect_argument_materialization(ops)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["call_target"], "<indirect>")
        self.assertEqual(results[0]["strategy"], "register_direct")

    def test_no_args(self):
        """Call with no argument setup in window."""
        ops = [
            LiftedOp("ADD", dest="r11", args=("r11", "r12")),
            LiftedOp("CALL", args=("no_arg_func",)),
        ]
        results = detect_argument_materialization(ops)
        self.assertEqual(len(results), 0)

    def test_multiple_calls(self):
        """Multiple calls in function each analyzed independently."""
        ops = [
            LiftedOp("CONST", dest="r3", attrs={"constant": 1}, source="li r3, 1"),
            LiftedOp("CALL", args=("func_a",)),
            # Enough padding so li r3 is out of the 8-instruction window for func_b
            LiftedOp("ADD", dest="r11", args=("r11", "r12")),
            LiftedOp("ADD", dest="r11", args=("r11", "r12")),
            LiftedOp("ADD", dest="r11", args=("r11", "r12")),
            LiftedOp("ADD", dest="r11", args=("r11", "r12")),
            LiftedOp("ADD", dest="r11", args=("r11", "r12")),
            LiftedOp("ADD", dest="r11", args=("r11", "r12")),
            LiftedOp("ADD", dest="r11", args=("r11", "r12")),
            LiftedOp("ASSIGN", dest="r3", args=("r31",), source="mr r3, r31"),
            LiftedOp("CALL", args=("func_b",)),
        ]
        results = detect_argument_materialization(ops)
        self.assertEqual(len(results), 2)
        self.assertEqual(results[0]["call_target"], "func_a")
        self.assertEqual(results[0]["strategy"], "register_direct")
        self.assertEqual(results[1]["call_target"], "func_b")
        self.assertEqual(results[1]["strategy"], "pre_computed")


# ---------------------------------------------------------------------------
# Sparse switch detection
# ---------------------------------------------------------------------------

class TestDetectSparseSwitch(unittest.TestCase):
    def test_linear_scan(self):
        """Sequential cmpwi+beq chain = linear scan."""
        ops = []
        for val in range(5):
            ops.append(LiftedOp("CMP", args=("r3", str(val)),
                               attrs={"signed": True, "width": "imm"},
                               source=f"cmpwi r3, {val}"))
            ops.append(LiftedOp("BRANCH", args=(f"case_{val}",),
                               attrs={"condition": "eq"},
                               source=f"beq case_{val}"))
        ops.append(LiftedOp("RETURN"))
        results = detect_sparse_switch(ops)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["strategy"], "linear_scan")
        self.assertEqual(results[0]["compare_count"], 5)
        self.assertEqual(results[0]["depth"], 1)

    def test_binary_search(self):
        """cmpwi+bge/ble forming binary tree = binary search."""
        ops = [
            LiftedOp("CMP", args=("r3", "10"), attrs={"signed": True},
                     source="cmpwi r3, 10"),
            LiftedOp("BRANCH", args=("upper_half",),
                     attrs={"condition": "ge"}, source="bge upper_half"),
            LiftedOp("CMP", args=("r3", "5"), attrs={"signed": True},
                     source="cmpwi r3, 5"),
            LiftedOp("BRANCH", args=("mid_left",),
                     attrs={"condition": "ge"}, source="bge mid_left"),
            LiftedOp("CMP", args=("r3", "2"), attrs={"signed": True},
                     source="cmpwi r3, 2"),
            LiftedOp("BRANCH", args=("leaf",),
                     attrs={"condition": "lt"}, source="blt leaf"),
            LiftedOp("RETURN"),
        ]
        results = detect_sparse_switch(ops)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["strategy"], "binary_search")
        self.assertGreater(results[0]["depth"], 0)

    def test_hybrid(self):
        """Mix of bge and beq = hybrid."""
        ops = [
            LiftedOp("CMP", args=("r3", "10"), attrs={"signed": True},
                     source="cmpwi r3, 10"),
            LiftedOp("BRANCH", args=("upper",),
                     attrs={"condition": "ge"}, source="bge upper"),
            LiftedOp("CMP", args=("r3", "1"), attrs={"signed": True},
                     source="cmpwi r3, 1"),
            LiftedOp("BRANCH", args=("case_1",),
                     attrs={"condition": "eq"}, source="beq case_1"),
            LiftedOp("CMP", args=("r3", "3"), attrs={"signed": True},
                     source="cmpwi r3, 3"),
            LiftedOp("BRANCH", args=("case_3",),
                     attrs={"condition": "eq"}, source="beq case_3"),
            LiftedOp("RETURN"),
        ]
        results = detect_sparse_switch(ops)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["strategy"], "hybrid")

    def test_skip_when_jump_table_present(self):
        """When DISPATCH (bctr) present, skip sparse detection."""
        ops = [
            LiftedOp("CMP", args=("r3", "1"), attrs={"signed": True}),
            LiftedOp("BRANCH", args=("case_1",), attrs={"condition": "eq"}),
            LiftedOp("CMP", args=("r3", "2"), attrs={"signed": True}),
            LiftedOp("BRANCH", args=("case_2",), attrs={"condition": "eq"}),
            LiftedOp("CMP", args=("r3", "3"), attrs={"signed": True}),
            LiftedOp("BRANCH", args=("case_3",), attrs={"condition": "eq"}),
            LiftedOp("DISPATCH"),
        ]
        results = detect_sparse_switch(ops)
        self.assertEqual(len(results), 0)

    def test_too_few_pairs(self):
        """Fewer than 3 CMP+BRANCH pairs = not a switch."""
        ops = [
            LiftedOp("CMP", args=("r3", "0"), attrs={"signed": True}),
            LiftedOp("BRANCH", args=("label1",), attrs={"condition": "eq"}),
            LiftedOp("RETURN"),
        ]
        results = detect_sparse_switch(ops)
        self.assertEqual(len(results), 0)

    def test_estimated_cases_from_values(self):
        """Case count estimated from distinct comparison values."""
        ops = []
        for val in [1, 5, 10, 20]:
            ops.append(LiftedOp("CMP", args=("r3", str(val)),
                               attrs={"signed": True}))
            ops.append(LiftedOp("BRANCH", args=(f"case_{val}",),
                               attrs={"condition": "eq"}))
        ops.append(LiftedOp("RETURN"))
        results = detect_sparse_switch(ops)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]["estimated_cases"], 4)


# ---------------------------------------------------------------------------
# Shape facts for new detectors
# ---------------------------------------------------------------------------

class TestShapeFactsArgumentMaterialization(unittest.TestCase):
    def _make_func(self, ops_list, name="test"):
        func = LiftedFunction(name=name, ops=ops_list, unsupported=[])
        func.prologue = PrologueInfo()
        func.cfg = build_cfg(ops_list)
        return func

    def test_arg_materialization_fact(self):
        ops = [
            LiftedOp("ASSIGN", dest="r3", args=("r28",), source="mr r3, r28"),
            LiftedOp("CALL", args=("some_func",)),
        ]
        func = self._make_func(ops)
        facts = derive_shape_facts(func)
        am_facts = [f for f in facts if f["kind"] == "argument_materialization"]
        self.assertGreater(len(am_facts), 0)
        self.assertEqual(am_facts[0]["category"], "pre_computed")


class TestShapeFactsSparseSwitch(unittest.TestCase):
    def _make_func(self, ops_list, name="test"):
        func = LiftedFunction(name=name, ops=ops_list, unsupported=[])
        func.prologue = PrologueInfo()
        func.cfg = build_cfg(ops_list)
        return func

    def test_sparse_switch_fact(self):
        ops = []
        for val in range(4):
            ops.append(LiftedOp("CMP", args=("r3", str(val)),
                               attrs={"signed": True}))
            ops.append(LiftedOp("BRANCH", args=(f"case_{val}",),
                               attrs={"condition": "eq"}))
        ops.append(LiftedOp("RETURN"))
        func = self._make_func(ops)
        facts = derive_shape_facts(func)
        sparse_facts = [f for f in facts if f["kind"] == "sparse_switch"]
        self.assertGreater(len(sparse_facts), 0)
        self.assertEqual(sparse_facts[0]["category"], "linear_scan")


class TestMasmMemoryOperandSpelling(unittest.TestCase):
    """`_parse_load_offset` / `_parse_load_base_reg` accepted only the C hex
    spelling (`0x60(r1)`) and decimal. Real MSVC .cod listings use MASM:
    measured 2026-08-17 over `msvc-src/results/branch_polarity.json`, 5
    occurrences of `NNh(rN)` -- including `stwu r1,-60h(r1)` -- and ZERO of
    `0xNN(rN)`. On the one input shape that is actually on disk, every hex
    displacement parsed as None, which reads downstream as "no offset" rather
    than as "could not parse".

    `lift_instruction` takes instruction TEXT and has no production caller
    today (only this file imports the module), so this is latent -- fixed now
    for the same reason: a text parser that cannot read the real spelling is a
    parser that ships broken."""

    def _op(self, mem):
        return LiftedOp("LOAD", dest="r3", args=(mem,))

    def test_masm_hex_displacement(self):
        self.assertEqual(_parse_load_offset(self._op("60h(r1)")), 0x60)
        self.assertEqual(_parse_load_offset(self._op("0Ch(r30)")), 0x0C)

    def test_negative_masm_hex_displacement(self):
        # the exact operand on disk: `stwu r1,-60h(r1)`
        self.assertEqual(_parse_load_offset(self._op("-60h(r1)")), -0x60)

    def test_base_register_survives_the_masm_spelling(self):
        self.assertEqual(_parse_load_base_reg(self._op("60h(r1)")), "r1")
        self.assertEqual(_parse_load_base_reg(self._op("0Ch(r30)")), "r30")

    def test_the_older_spellings_are_unchanged(self):
        for mem, off, base in (("0x10(r3)", 0x10, "r3"),
                               ("0(r31)", 0, "r31"),
                               ("-0x8(r1)", -0x8, "r1"),
                               ("-8(r1)", -8, "r1")):
            self.assertEqual(_parse_load_offset(self._op(mem)), off, mem)
            self.assertEqual(_parse_load_base_reg(self._op(mem)), base, mem)

    def test_a_non_operand_still_yields_None(self):
        self.assertIsNone(_parse_load_offset(self._op("r3")))
        self.assertIsNone(_parse_load_base_reg(self._op("r3")))


if __name__ == "__main__":
    unittest.main()
