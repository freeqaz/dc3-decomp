"""Tests for the slot_pad pattern.

Pure AST/text-level tests. No build/objdiff.

Verifies the pattern correctly inserts a dummy local at function top
to shift slot allocations, sized by `dominant_slot_pair`.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.tests.conftest import (
    _empty_diag,
    make_context,
)
from scripts.permuter.patterns.base import get_pattern


def _variants(source: str, func_name: str = "test_func", diag=None) -> list:
    pat = get_pattern("slot_pad")
    if diag is None:
        diag = _empty_diag()
        # Add a strong slot-inversion signal
        diag.offset_deltas = {192: 87, -192: 52}
    ctx = make_context(source, func_name, diag)
    return list(pat.generate(ctx))


class TestRegistration(unittest.TestCase):
    def test_registered(self):
        pat = get_pattern("slot_pad")
        self.assertEqual(pat.name, "slot_pad")
        self.assertEqual(pat.safety_tier, "moderate")
        self.assertEqual(pat.structural_domain, "data_flow")


class TestRelevance(unittest.TestCase):
    def test_not_relevant_empty(self):
        pat = get_pattern("slot_pad")
        d = _empty_diag()
        self.assertFalse(pat.relevant(d))

    def test_not_relevant_small_offset_swap(self):
        pat = get_pattern("slot_pad")
        d = _empty_diag()
        d.offset_deltas = {192: 5, -192: 5}  # only 10 instructions
        self.assertFalse(pat.relevant(d))

    def test_relevant_large_offset_swap(self):
        pat = get_pattern("slot_pad")
        d = _empty_diag()
        d.offset_deltas = {192: 87, -192: 52}
        self.assertTrue(pat.relevant(d))

    def test_priority_high_for_dominant_swap(self):
        pat = get_pattern("slot_pad")
        d = _empty_diag()
        d.offset_deltas = {192: 87, -192: 52}  # > 100
        self.assertGreaterEqual(pat.priority(d), 0.6)


class TestGenerate(unittest.TestCase):
    def test_emits_two_variants(self):
        src = """\
void test_func() {
    int x = 0;
    DoStuff(x);
}
"""
        variants = _variants(src)
        # Expect char_pad and volatile_int variants
        labels = {v.tags & {"char_pad", "volatile_int"} for v in variants}
        labels = set().union(*labels)
        self.assertIn("char_pad", labels)
        self.assertIn("volatile_int", labels)

    def test_pad_size_from_dominant_slot_pair(self):
        """char_pad's size comes from dominant_slot_pair (default 96)."""
        src = """\
void test_func() {
    int x = 0;
}
"""
        diag = _empty_diag()
        # Slot pair magnitude 192 (= sizeof(Line))
        diag.offset_deltas = {192: 87, -192: 52}
        variants = _variants(src, diag=diag)
        char_pad = next(v for v in variants if "char_pad" in v.tags)
        self.assertIn(
            b"char _slotpad[192]",
            char_pad.source,
            "Pad size should use dominant_slot_pair magnitude",
        )

    def test_pad_inserted_at_function_top(self):
        src = """\
void test_func() {
    int x = 0;
}
"""
        variants = _variants(src)
        char_pad = next(v for v in variants if "char_pad" in v.tags)
        # Pad should appear BEFORE `int x = 0;`
        src_text = char_pad.source.decode()
        pad_idx = src_text.find("_slotpad")
        x_idx = src_text.find("int x = 0;")
        self.assertLess(pad_idx, x_idx)
