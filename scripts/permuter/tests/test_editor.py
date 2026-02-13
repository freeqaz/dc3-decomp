"""Tests for SourceEditor."""

from __future__ import annotations

import unittest
from dataclasses import dataclass

from scripts.permuter.editor import SourceEditor


@dataclass
class _FakeNode:
    """Minimal stand-in for tree-sitter Node (start_byte / end_byte)."""
    start_byte: int
    end_byte: int


class TestSourceEditor(unittest.TestCase):

    def test_single_replace_node(self):
        source = b"hello world"
        ed = SourceEditor(source)
        ed.replace_node(_FakeNode(6, 11), b"earth")
        self.assertEqual(ed.apply(), b"hello earth")

    def test_multi_edit_insert_and_replace(self):
        source = b"aaa bbb ccc"
        ed = SourceEditor(source)
        ed.insert_before(_FakeNode(0, 3), b"[")
        ed.replace_node(_FakeNode(4, 7), b"BBB")
        result = ed.apply()
        self.assertEqual(result, b"[aaa BBB ccc")

    def test_overlap_raises_valueerror(self):
        source = b"abcdefgh"
        ed = SourceEditor(source)
        ed.replace_range(2, 5, b"XY")
        ed.replace_range(3, 6, b"ZZ")
        with self.assertRaises(ValueError):
            ed.apply()

    def test_swap_nodes_ab_order(self):
        source = b"aaa bbb"
        a = _FakeNode(0, 3)
        b = _FakeNode(4, 7)
        ed = SourceEditor(source)
        ed.swap_nodes(a, b)
        self.assertEqual(ed.apply(), b"bbb aaa")

    def test_swap_nodes_ba_order(self):
        """Callers pass b before a — swap_nodes sorts internally."""
        source = b"aaa bbb"
        a = _FakeNode(0, 3)
        b = _FakeNode(4, 7)
        ed = SourceEditor(source)
        ed.swap_nodes(b, a)
        self.assertEqual(ed.apply(), b"bbb aaa")

    def test_delete_node(self):
        source = b"hello cruel world"
        ed = SourceEditor(source)
        ed.delete_node(_FakeNode(5, 11))  # " cruel"
        self.assertEqual(ed.apply(), b"hello world")

    def test_empty_edit_list(self):
        source = b"unchanged"
        ed = SourceEditor(source)
        self.assertEqual(ed.apply(), source)

    def test_insert_before(self):
        source = b"world"
        ed = SourceEditor(source)
        ed.insert_before(_FakeNode(0, 5), b"hello ")
        self.assertEqual(ed.apply(), b"hello world")

    def test_insert_after(self):
        source = b"hello"
        ed = SourceEditor(source)
        ed.insert_after(_FakeNode(0, 5), b" world")
        self.assertEqual(ed.apply(), b"hello world")

    def test_insert_at(self):
        source = b"helloworld"
        ed = SourceEditor(source)
        ed.insert_at(5, b" ")
        self.assertEqual(ed.apply(), b"hello world")

    def test_delete_range(self):
        source = b"abcdefgh"
        ed = SourceEditor(source)
        ed.delete_range(2, 5)
        self.assertEqual(ed.apply(), b"abfgh")

    def test_replace_range(self):
        source = b"abcdefgh"
        ed = SourceEditor(source)
        ed.replace_range(2, 5, b"XYZ")
        self.assertEqual(ed.apply(), b"abXYZfgh")

    def test_multiple_zero_width_inserts_at_same_offset(self):
        source = b"ab"
        ed = SourceEditor(source)
        ed.insert_at(1, b"X")
        ed.insert_at(1, b"Y")
        # Both inserts at offset 1 should be allowed
        result = ed.apply()
        # Applied in reverse sort order, so both get inserted
        self.assertIn(b"X", result)
        self.assertIn(b"Y", result)


if __name__ == "__main__":
    unittest.main()
