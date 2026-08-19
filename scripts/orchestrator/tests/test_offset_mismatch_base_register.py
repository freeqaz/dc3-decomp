"""`run_objdiff`'s "Offset Mismatches (resolved)" block must read the BASE REGISTER.

THE DEFECT
==========
`_MEM_ARG_RE` captured the base register as group(3) and `_resolve_offset_mismatches`
never read it, so ANY differing offset was resolved against the class struct --
including pure stack slots. The output for a stack pair was byte-identical to a
genuine `this`-relative field difference:

    "Source accesses 'mInputGain' but target accesses 'mReverbMixDb' — wrong field?"

That exact string was the named lead in
`docs/decomp/patterns/rounded-100-hides-real-bugs.md` and sent a lane after a
member of `FxSend` that does not exist.

MEASURED SCOPE, 2026-08-19, against real built objects in this tree:

    CamShot::Load    29 rows emitted, ALL 29 r31-relative
    RndText::Load    25 rows emitted, ALL 25 r31-relative

and in both functions r31 is a frame pointer (`subi r31, r1, 0x470` /
`subi r31, r1, 0x1c0`). 54 of 54 rows were false, while naming plausible members
(`RndText::mScrollOutIndex`, `RndTransformable::mConstraint`).

THREE THINGS THE FIX MUST NOT DO
================================
1. Suppress by register NUMBER. `CharBonesSamples::Save` and `FxSendChorus::Load`
   do `mr r31, r3` -- there r31 IS `this`, and a number-based rule turns a
   false-positive generator into a false-negative one. Detect from the prologue.
2. Match only `addi rN, r1, -<frame>`. objdiff's disassembler spells the real
   prologue `subi r31, r1, 0x470`; an addi-only regex finds zero frame pointers.
3. Treat a volatile register as a frame pointer. `addi r4, r1, 0x58` is one
   call's out-argument address (CharBonesSamples::Save index 10), not a frame
   pointer for the function.

Hermetic: fabricated instruction dicts in objdiff's JSON shape, a temporary
struct DB, no objdiff and no build.
"""
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ORCH = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ORCH.parent))


def _instr(index, opcode, t_args, b_args, match_type="diff_arg", b_opcode=None):
    """One row in objdiff's `instructions` array."""
    return {
        "index": index,
        "match_type": match_type,
        "target": {"opcode": opcode, "args": t_args},
        "base": {"opcode": b_opcode or opcode, "args": b_args},
    }


# A minimal prologue whose frame pointer is r31, in the spelling objdiff emits.
FRAME_PROLOGUE = [
    _instr(0, "mflr", "r12", "r12", match_type="equal"),
    _instr(1, "subi", "r31, r1, 0x1c0", "r31, r1, 0x1b0"),
    _instr(2, "stwu", "r1, -0x1c0(r1)", "r1, -0x1b0(r1)"),
]

# A prologue where r31 is `this`, not a frame pointer (mr r31, r3).
THIS_PROLOGUE = [
    _instr(0, "mflr", "r12", "r12", match_type="equal"),
    _instr(1, "stwu", "r1, -0xc0(r1)", "r1, -0xc0(r1)", match_type="equal"),
    _instr(2, "mr", "r31, r3", "r31, r3", match_type="equal"),
]


class _Base(unittest.TestCase):
    """Builds a real StructDB the resolver can query."""

    CLASS = "FxSend"
    FIELDS = [
        (0x50, "mInputGain", "float"),
        (0x54, "mReverbMixDb", "float"),
        (0xA8, "mFontMaps", "std::vector<FontMapBase *>"),
        (0x78, "mAltStyle", "ObjPtr<Hmx::Object>"),
    ]

    def setUp(self):
        from orchestrator.mcp_server import DecompMCPServer

        self.tmp = Path(tempfile.mkdtemp())
        self.srv = DecompMCPServer(db_path=str(self.tmp / "decomp.db"))
        self.srv.project_root = self.tmp
        (self.tmp / "struct_db.sqlite").touch()

        fields = dict((off, (self.CLASS, name, typ)) for off, name, typ in self.FIELDS)

        class _FakeDB:
            def __enter__(self_inner):
                return self_inner

            def __exit__(self_inner, *a):
                return False

            def lookup(self_inner, cls, off):
                return fields.get(off)

        self._patch = mock.patch("orchestrator.mcp_server.StructDB",
                                 lambda path: _FakeDB())
        self._patch.start()
        self.addCleanup(self._patch.stop)

    def resolve(self, instructions, demangled=None):
        data = {
            "demangled": demangled or f"public: void __cdecl {self.CLASS}::Load(class BinStream &)",
            "instructions": instructions,
        }
        return self.srv._resolve_offset_mismatches(data)


class TestNegativeControlStackSlots(_Base):
    """An r1/frame-relative pair must NOT be reported as a field difference."""

    def test_r1_relative_pair_is_a_stack_slot(self):
        """The verifier's fabricated pair, verbatim."""
        rows = self.resolve(FRAME_PROLOGUE + [
            _instr(40, "lwz", "r11, 0x54(r1)", "r11, 0x50(r1)"),
        ])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["kind"], "stack-slot")
        self.assertNotIn("fix_hint", rows[0])
        self.assertNotIn("target_field", rows[0])

    def test_frame_pointer_relative_pair_is_a_frame_slot(self):
        """r31 here is `subi r31, r1, 0x1c0` — the RndText::Load shape."""
        rows = self.resolve(FRAME_PROLOGUE + [
            _instr(40, "lwz", "r11, 0x54(r31)", "r11, 0x50(r31)"),
        ])
        self.assertEqual(rows[0]["kind"], "frame-slot")
        self.assertNotIn("fix_hint", rows[0])

    def test_the_exact_false_hint_is_no_longer_producible_from_stack_slots(self):
        """The string that sent a lane after a nonexistent FxSend member."""
        for base in ("r1", "r31"):
            rows = self.resolve(FRAME_PROLOGUE + [
                _instr(40, "lwz", f"r11, 0x54({base})", f"r11, 0x50({base})"),
            ])
            hints = " ".join(r.get("fix_hint", "") for r in rows)
            self.assertNotIn("mReverbMixDb", hints)
            self.assertNotIn("mInputGain", hints)

    def test_stwu_frame_allocation_is_not_a_field(self):
        """`stwu r1, -0x470(r1)` was resolved as a struct field (CamShot idx 6)."""
        rows = self.resolve(FRAME_PROLOGUE + [
            _instr(40, "stwu", "r1, -0x470(r1)", "r1, -0x490(r1)"),
        ])
        self.assertEqual([r for r in rows if r["index"] == 40], [])

    def test_different_base_register_on_each_side_is_not_a_field(self):
        rows = self.resolve(FRAME_PROLOGUE + [
            _instr(40, "lwz", "r11, 0x54(r3)", "r11, 0x50(r30)"),
        ])
        self.assertEqual(rows[0]["kind"], "mixed-base")
        self.assertNotIn("fix_hint", rows[0])


class TestPositiveControlRealFields(_Base):
    """The other direction: a genuine `this`-relative difference must survive."""

    def test_this_relative_pair_is_still_reported_with_a_hint(self):
        rows = self.resolve(THIS_PROLOGUE + [
            _instr(40, "lwz", "r11, 0x54(r3)", "r11, 0x50(r3)"),
        ])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["kind"], "object-field")
        self.assertEqual(rows[0]["target_field"], "FxSend::mReverbMixDb (float)")
        self.assertEqual(rows[0]["base_field"], "FxSend::mInputGain (float)")
        self.assertIn("mInputGain", rows[0]["fix_hint"])
        self.assertIn("mReverbMixDb", rows[0]["fix_hint"])

    def test_r31_holding_this_is_not_suppressed(self):
        """`mr r31, r3` — the FxSendChorus/CharBonesSamples shape. Suppressing
        r31 by number would silently lose this."""
        rows = self.resolve(THIS_PROLOGUE + [
            _instr(40, "lwz", "r11, 0x54(r31)", "r11, 0x50(r31)"),
        ])
        self.assertEqual(rows[0]["kind"], "object-field")
        self.assertIn("fix_hint", rows[0])

    def test_volatile_r1_derived_register_does_not_poison_the_function(self):
        """`addi r4, r1, 0x58` is one call's out-arg address, not a frame
        pointer; r31=`this` accesses must still resolve."""
        rows = self.resolve(THIS_PROLOGUE + [
            _instr(10, "addi", "r4, r1, 0x58", "r4, r1, 0x58", match_type="equal"),
            _instr(40, "lwz", "r11, 0x54(r31)", "r11, 0x50(r31)"),
        ])
        self.assertEqual([r["kind"] for r in rows], ["object-field"])


class TestPrologueDetection(_Base):
    def test_subi_spelling_is_recognised(self):
        """objdiff prints `subi r31, r1, 0x470`, never `addi r31, r1, -0x470`."""
        regs = self.srv._frame_pointer_regs(FRAME_PROLOGUE, "target")
        self.assertEqual(regs, {31})

    def test_mr_from_r3_is_not_a_frame_pointer(self):
        regs = self.srv._frame_pointer_regs(THIS_PROLOGUE, "target")
        self.assertEqual(regs, set())

    def test_volatile_registers_are_never_frame_pointers(self):
        instrs = [_instr(10, "addi", "r4, r1, 0x58", "r4, r1, 0x58", match_type="equal")]
        self.assertEqual(self.srv._frame_pointer_regs(instrs, "target"), set())

    def test_each_side_is_detected_separately(self):
        instrs = [_instr(1, "subi", "r31, r1, 0x1c0", "r30, r1, 0x1b0")]
        self.assertEqual(self.srv._frame_pointer_regs(instrs, "target"), {31})
        self.assertEqual(self.srv._frame_pointer_regs(instrs, "base"), {30})

    def test_missing_prologue_yields_unverified_not_a_confident_field(self):
        """With full_listing=false the window may start mid-function; frame-pointer
        status is then UNKNOWN. Reporting it as a confident field is the same
        error in a new place."""
        rows = self.resolve([
            _instr(400, "lwz", "r11, 0x54(r31)", "r11, 0x50(r31)"),
        ])
        self.assertEqual(rows[0]["kind"], "unverified")
        self.assertNotIn("fix_hint", rows[0])

    def test_prologue_visible_requires_index_zero(self):
        self.assertTrue(self.srv._prologue_visible(FRAME_PROLOGUE))
        self.assertFalse(self.srv._prologue_visible([_instr(400, "lwz", "a", "b")]))
        self.assertFalse(self.srv._prologue_visible([]))


class TestFormattingDefects(_Base):
    def test_negative_offsets_format_as_valid_hex(self):
        """`f"0x{-0x470:x}"` produced the non-literal `0x-470`."""
        self.assertEqual(self.srv._fmt_offset(-0x470), "-0x470")
        self.assertEqual(self.srv._fmt_offset(0x54), "0x54")

    def test_field_name_survives_a_qualified_template_type(self):
        """The shipped hint derived the member name by splitting the FORMATTED
        string on '::' and ' (', so `FxSend::mAltStyle (ObjPtr<Hmx::Object>)`
        came out as the field name "Object>)"."""
        rows = self.resolve(THIS_PROLOGUE + [
            _instr(40, "stw", "r11, 0xa8(r3)", "r11, 0x78(r3)"),
        ])
        hint = rows[0]["fix_hint"]
        self.assertIn("'mAltStyle'", hint)
        self.assertIn("'mFontMaps'", hint)
        self.assertNotIn("Object>)", hint)


if __name__ == "__main__":
    unittest.main()
