"""Tests for the constrained PPC->IL lifter."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
_TOOLS_DIR = _PROJECT_ROOT / "msvc-src" / "tools"
if str(_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_TOOLS_DIR))

from ppc_il_lifter import (
    LiftedFunction,
    derive_shape_facts,
    lift_function_asm,
    lift_instruction,
)
from diff_test import FunctionAsm


class TestLiftInstruction(unittest.TestCase):
    def test_extrwi_lifts_to_fused_shr_mask(self):
        op = lift_instruction("extrwi r3, r4, 6, 24")
        self.assertIsNotNone(op)
        self.assertEqual(op.name, "FUSED_SHR_MASK")
        self.assertEqual(op.dest, "r3")
        self.assertEqual(op.args, ("r4",))
        self.assertEqual(op.attrs["canonical"], "rlwinm")
        self.assertEqual(op.attrs["width"], 6)
        self.assertEqual(op.attrs["offset"], 24)

    def test_clrlslwi_lifts_to_fused_shl_mask(self):
        op = lift_instruction("clrlslwi r5, r6, 26, 2")
        self.assertIsNotNone(op)
        self.assertEqual(op.name, "FUSED_SHL_MASK")
        self.assertEqual(op.dest, "r5")
        self.assertEqual(op.args, ("r6",))
        self.assertEqual(op.attrs["mask_shift"], 26)
        self.assertEqual(op.attrs["shift"], 2)

    def test_clrlwi_byte_mask_is_canonicalized(self):
        op = lift_instruction("clrlwi r7, r8, 24")
        self.assertIsNotNone(op)
        self.assertEqual(op.name, "BYTE_MASK")
        self.assertEqual(op.attrs["canonical"], "byte_mask")
        self.assertEqual(op.attrs["mask"], 0xFF)

    def test_compare_and_branch_are_lifted(self):
        cmp_op = lift_instruction("cmpwi cr6, r3, 0")
        br_op = lift_instruction("bne $L123")
        self.assertIsNotNone(cmp_op)
        self.assertIsNotNone(br_op)
        self.assertEqual(cmp_op.name, "CMP")
        self.assertEqual(cmp_op.args, ("r3", "0"))
        self.assertTrue(cmp_op.attrs["signed"])
        self.assertEqual(br_op.name, "BRANCH")
        self.assertEqual(br_op.attrs["condition"], "ne")

    def test_rlwinm_alias_is_canonicalized_to_fused_shr_mask(self):
        op = lift_instruction("rlwinm r3, r3, 30, 26, 31")
        self.assertIsNotNone(op)
        self.assertEqual(op.name, "FUSED_SHR_MASK")
        self.assertEqual(op.attrs["width"], 6)
        self.assertEqual(op.attrs["offset"], 24)

    def test_bool_carry_chain_instructions_are_lifted(self):
        addic = lift_instruction("addic r3, r4, -1")
        subfe = lift_instruction("subfe r3, r3, r4")
        cntlzw = lift_instruction("cntlzw r5, r6")
        addze = lift_instruction("addze r7, r8")
        self.assertEqual(addic.name, "ADD")
        self.assertEqual(subfe.name, "SUB_EXTEND")
        self.assertEqual(cntlzw.name, "COUNT_LEADING_ZEROS")
        self.assertEqual(addze.name, "ADD_ZERO_EXTEND")

    def test_ctr_and_branch_return_variants_are_lifted(self):
        bdzne = lift_instruction("bdzne cr6, $LN6@switch_den")
        bnelr = lift_instruction("bnelr cr6")
        self.assertEqual(bdzne.name, "LOOP_EXIT")
        self.assertEqual(bdzne.args, ("$LN6@switch_den",))
        self.assertEqual(bdzne.attrs["condition"], "ne")
        self.assertEqual(bnelr.name, "BRANCH")
        self.assertTrue(bnelr.attrs["returns"])
        self.assertEqual(bnelr.attrs["condition"], "ne")


class TestLiftFunction(unittest.TestCase):
    def test_function_collects_supported_and_unsupported(self):
        func = FunctionAsm(
            name="test",
            mangled="?test@@YAXXZ",
            instructions=[
                (0x10, "5463103a", "extrwi r3, r3, 6, 24"),
                (0x14, "7c000000", "crxor 6, 6, 6"),
                (0x18, "4e800020", "blr"),
            ],
        )
        lifted = lift_function_asm(func)
        self.assertIsInstance(lifted, LiftedFunction)
        self.assertEqual(lifted.name, "?test@@YAXXZ")
        self.assertEqual([op.name for op in lifted.ops], ["FUSED_SHR_MASK", "CR_XOR", "RETURN"])
        self.assertEqual(lifted.unsupported, [])

    def test_derive_shape_facts_for_bool_zero_test(self):
        func = FunctionAsm(
            name="test",
            mangled="?test@@YAHI@Z",
            instructions=[
                (0x10, "3404ffff", "addic r0, r4, -1"),
                (0x14, "7c001910", "subfe r0, r0, r3"),
                (0x18, "4e800020", "blr"),
            ],
        )
        lifted = lift_function_asm(func)
        facts = derive_shape_facts(lifted)
        categories = {fact["category"] for fact in facts if fact["kind"] == "bool_materialization"}
        self.assertIn("zero_test", categories)

    def test_derive_shape_facts_for_signed_positive(self):
        func = FunctionAsm(
            name="test",
            mangled="?test@@YAHH@Z",
            instructions=[
                (0x10, "7c6410d0", "neg r3, r4"),
                (0x14, "7c632078", "andc r3, r3, r4"),
                (0x18, "54630ffe", "srwi r3, r3, 31"),
                (0x1c, "4e800020", "blr"),
            ],
        )
        lifted = lift_function_asm(func)
        facts = derive_shape_facts(lifted)
        categories = {fact["category"] for fact in facts if fact["kind"] == "bool_materialization"}
        self.assertIn("signed_positive", categories)

    def test_derive_shape_facts_for_byte_fusion(self):
        func = FunctionAsm(
            name="test",
            mangled="?test@@YAHI@Z",
            instructions=[
                (0x10, "5463103a", "rlwinm r3, r3, 30, 26, 31"),
                (0x14, "4e800020", "blr"),
            ],
        )
        lifted = lift_function_asm(func)
        facts = derive_shape_facts(lifted)
        categories = {fact["category"] for fact in facts if fact["kind"] == "byte_fusion"}
        self.assertIn("fused_shr_mask", categories)

    def test_derive_shape_facts_for_ctr_switch_chain(self):
        func = FunctionAsm(
            name="test",
            mangled="?switch_dense@@YAHH@Z",
            instructions=[
                (0x10, "2c030005", "cmplwi cr6, r3, 5"),
                (0x14, "4181001c", "bgt cr6, $LN1@switch_den"),
                (0x18, "7c6903a6", "mtctr r3"),
                (0x1c, "2c030000", "cmpwi cr6, r3, 0"),
                (0x20, "4082000c", "bne cr6, $LN2@switch_den"),
                (0x24, "42000010", "bdzne cr6, $LN6@switch_den"),
                (0x28, "42000010", "bdzne cr6, $LN5@switch_den"),
                (0x2c, "4e800020", "blr"),
            ],
        )
        lifted = lift_function_asm(func)
        facts = derive_shape_facts(lifted)
        categories = {fact["category"] for fact in facts if fact["kind"] == "switch_dispatch"}
        self.assertIn("switch_ctr_chain", categories)

    def test_derive_shape_facts_for_switch_if_chain(self):
        func = FunctionAsm(
            name="test",
            mangled="?switch_enum@@YAXI@Z",
            instructions=[
                (0x10, "2c030001", "cmplwi cr6, r3, 1"),
                (0x14, "41800010", "blt cr6, $LN3@switch_enu"),
                (0x18, "41820010", "beq cr6, $LN2@switch_enu"),
                (0x1c, "2c030003", "cmplwi cr6, r3, 3"),
                (0x20, "48000010", "b ?handle_c@@YAXXZ"),
                (0x24, "48000010", "b ?handle_b@@YAXXZ"),
                (0x28, "48000010", "b ?handle_a@@YAXXZ"),
                (0x2c, "4e800020", "blr"),
            ],
        )
        lifted = lift_function_asm(func)
        facts = derive_shape_facts(lifted)
        categories = {fact["category"] for fact in facts if fact["kind"] == "switch_dispatch"}
        self.assertIn("switch_if_chain", categories)

    def test_derive_shape_facts_for_tail_direct_call(self):
        func = FunctionAsm(
            name="test",
            mangled="?call_and_return@@YAHH@Z",
            instructions=[
                (0x10, "38830001", "addi r4, r3, 1"),
                (0x14, "48000000", "b ?plain_func@@YAHHH@Z"),
            ],
        )
        lifted = lift_function_asm(func)
        facts = derive_shape_facts(lifted)
        categories = {fact["category"] for fact in facts if fact["kind"] == "call_shape"}
        self.assertIn("tail_direct_call", categories)

    def test_derive_shape_facts_for_cached_return_value(self):
        func = FunctionAsm(
            name="test",
            mangled="?cached_return@@YAHH@Z",
            instructions=[
                (0x10, "48000000", "bl ?plain_func@@YAHHH@Z"),
                (0x14, "7c7f1b78", "mr r31, r3"),
                (0x18, "48000000", "bl ?void_func@@YAXH@Z"),
                (0x1c, "7fe3fb78", "mr r3, r31"),
                (0x20, "4e800020", "blr"),
            ],
        )
        lifted = lift_function_asm(func)
        facts = derive_shape_facts(lifted)
        categories = {fact["category"] for fact in facts if fact["kind"] == "call_shape"}
        self.assertIn("cached_return_value", categories)
        self.assertIn("call_sequence_return", categories)

    def test_derive_shape_facts_for_virtual_tail_call(self):
        func = FunctionAsm(
            name="test",
            mangled="?virtual_call@@YAHPAUBase@@H@Z",
            instructions=[
                (0x10, "81630000", "lwz r11,0(r3)"),
                (0x14, "814b0000", "lwz r10,0(r11)"),
                (0x18, "7d4903a6", "mtctr r10"),
                (0x1c, "4e800420", "bctr"),
            ],
        )
        lifted = lift_function_asm(func)
        facts = derive_shape_facts(lifted)
        vcall_categories = {fact["category"] for fact in facts if fact["kind"] == "virtual_dispatch"}
        switch_categories = {fact["category"] for fact in facts if fact["kind"] == "switch_dispatch"}
        self.assertIn("vtable_tail_call", vcall_categories)
        self.assertNotIn("switch_table", switch_categories)


if __name__ == "__main__":
    unittest.main()
