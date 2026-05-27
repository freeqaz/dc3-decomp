"""Smoke tests for the three new pattern rules:

- abs_empty_else_negate
- store_then_compound_add
- compound_or_widening_drop
"""

from __future__ import annotations

import sys
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.patterns import get_pattern
from scripts.permuter.tests.conftest import (
    _empty_diag,
    diag_with_store_load_ops,
    diag_with_fneg_frsp,
    make_context,
)
from scripts.permuter.types import DiffOp


def _diag_with_clrlwi():
    d = _empty_diag()
    d.diff_ops = [DiffOp(index=5, target_opcode="or", base_opcode="clrlwi")]
    return d


# ---------------------------------------------------------------------------
# abs_empty_else_negate
# ---------------------------------------------------------------------------

class AbsEmptyElseNegateSmoke(unittest.TestCase):
    def setUp(self):
        self.pat = get_pattern("abs_empty_else_negate")

    def test_emits_for_all_four_shapes(self):
        src = textwrap.dedent("""\
        void f() {
            float diff = 1.0f;
            if (diff > 0.0f) {} else { diff = -diff; }
            if (diff >= 0.0f) {} else { diff = -diff; }
            if (diff < 0.0f) { diff = -diff; }
            if (diff <= 0.0f) { diff = -diff; }
        }
        """)
        ctx = make_context(src, "f", diag_with_fneg_frsp())
        variants = list(self.pat.generate(ctx))
        self.assertEqual(len(variants), 4)
        for v in variants:
            self.assertIn(b"= Abs(", v.source)

    def test_skips_non_empty_then(self):
        src = textwrap.dedent("""\
        void f() {
            float diff = 1.0f;
            if (diff > 0.0f) { other(); } else { diff = -diff; }
        }
        """)
        ctx = make_context(src, "f", diag_with_fneg_frsp())
        self.assertEqual(list(self.pat.generate(ctx)), [])

    def test_skips_different_variable(self):
        src = textwrap.dedent("""\
        void f() {
            float a = 1.0f, b = 2.0f;
            if (a > 0.0f) {} else { b = -b; }
        }
        """)
        ctx = make_context(src, "f", diag_with_fneg_frsp())
        self.assertEqual(list(self.pat.generate(ctx)), [])


# ---------------------------------------------------------------------------
# store_then_compound_add
# ---------------------------------------------------------------------------

class StoreThenCompoundAddSmoke(unittest.TestCase):
    def setUp(self):
        self.pat = get_pattern("store_then_compound_add")

    def test_simple_case(self):
        src = textwrap.dedent("""\
        void f() {
            mBeat = newBeat + AlignToBeat(beat);
        }
        """)
        ctx = make_context(src, "f", diag_with_store_load_ops())
        variants = list(self.pat.generate(ctx))
        self.assertEqual(len(variants), 1)
        self.assertIn(b"mBeat = newBeat;", variants[0].source)
        self.assertIn(b"mBeat += AlignToBeat(beat);", variants[0].source)

    def test_skips_when_no_call(self):
        src = textwrap.dedent("""\
        void f() {
            mBeat = a + b;
        }
        """)
        ctx = make_context(src, "f", diag_with_store_load_ops())
        self.assertEqual(list(self.pat.generate(ctx)), [])

    def test_skips_when_both_sides_have_calls(self):
        src = textwrap.dedent("""\
        void f() {
            mBeat = A() + B();
        }
        """)
        ctx = make_context(src, "f", diag_with_store_load_ops())
        self.assertEqual(list(self.pat.generate(ctx)), [])


# ---------------------------------------------------------------------------
# compound_or_widening_drop
# ---------------------------------------------------------------------------

class CompoundOrWideningDropSmoke(unittest.TestCase):
    def setUp(self):
        self.pat = get_pattern("compound_or_widening_drop")

    def test_expands_compound_on_narrow_type(self):
        src = textwrap.dedent("""\
        unsigned short g_unk;
        extern int id;
        void f() {
            g_unk |= id;
        }
        """)
        ctx = make_context(src, "f", _diag_with_clrlwi())
        variants = list(self.pat.generate(ctx))
        self.assertEqual(len(variants), 1)
        self.assertIn(b"g_unk = g_unk | id;", variants[0].source)

    def test_collapses_expanded_form(self):
        src = textwrap.dedent("""\
        unsigned short g_unk;
        extern int id;
        void f() {
            g_unk = g_unk | id;
        }
        """)
        ctx = make_context(src, "f", _diag_with_clrlwi())
        variants = list(self.pat.generate(ctx))
        self.assertEqual(len(variants), 1)
        self.assertIn(b"g_unk |= id;", variants[0].source)

    def test_skips_non_narrow_type(self):
        src = textwrap.dedent("""\
        extern int g_big;
        extern int other;
        void f() {
            g_big |= other;
        }
        """)
        ctx = make_context(src, "f", _diag_with_clrlwi())
        self.assertEqual(list(self.pat.generate(ctx)), [])

    def test_handles_and_xor(self):
        src = textwrap.dedent("""\
        unsigned char flag8;
        extern int mask;
        void f() {
            flag8 &= mask;
        }
        """)
        ctx = make_context(src, "f", _diag_with_clrlwi())
        variants = list(self.pat.generate(ctx))
        self.assertEqual(len(variants), 1)
        self.assertIn(b"flag8 = flag8 & mask;", variants[0].source)


if __name__ == "__main__":
    unittest.main()
