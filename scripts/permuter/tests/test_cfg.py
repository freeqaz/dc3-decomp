"""Tests for lightweight CFG module."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.cfg import (
    BasicBlock,
    CFG,
    build_cfg,
    dominates,
    get_terminal_blocks,
    is_terminal_block,
    live_variables_at_block_entry,
    reaches_exit,
    stmt_is_in_terminal_position,
)
from scripts.permuter.statement_effects import StatementEffectAnalyzer
from scripts.permuter.tests.conftest import make_context, diag_with_clusters


def _build(source_text: str, func_name: str = "test_func"):
    """Helper: parse source and build CFG from function body."""
    ctx = make_context(source_text, func_name, diag_with_clusters())
    cfg = build_cfg(ctx.body_node, ctx.file_source)
    return ctx, cfg


class TestBuildCFG(unittest.TestCase):
    def test_empty_body(self):
        ctx, cfg = _build("void test_func() {}")
        self.assertEqual(len(cfg.blocks), 1)
        self.assertTrue(cfg.blocks[0].is_entry)
        self.assertTrue(cfg.blocks[0].is_exit)

    def test_straight_line(self):
        ctx, cfg = _build("""\
void test_func(int a, int b) {
    a = 1;
    b = 2;
    return;
}
""")
        # Straight-line code + return → one block (stmts collected then flushed at return)
        self.assertGreaterEqual(len(cfg.blocks), 1)
        # The return statement should be in an exit block
        exit_blocks = cfg.exit_blocks()
        self.assertTrue(len(exit_blocks) >= 1)
        self.assertTrue(any(b.has_return for b in exit_blocks))

    def test_if_creates_branch_blocks(self):
        ctx, cfg = _build("""\
void test_func(int x) {
    if (x > 0) {
        x = 1;
    } else {
        x = 2;
    }
}
""")
        # Should have: entry block with if, true branch block, false branch block
        self.assertGreaterEqual(len(cfg.blocks), 3)
        # The if-statement block should have 2 successors (true + false)
        if_block = None
        for b in cfg.blocks:
            for s in b.statements:
                if s.type == "if_statement":
                    if_block = b
                    break
        self.assertIsNotNone(if_block)
        self.assertEqual(len(if_block.successors), 2)

    def test_loop_creates_back_edge(self):
        ctx, cfg = _build("""\
void test_func(int i) {
    for (i = 0; i < 10; i++) {
        i++;
    }
}
""")
        # Should have a loop header
        loop_headers = [b for b in cfg.blocks if b.is_loop_header]
        self.assertTrue(len(loop_headers) >= 1)
        # Loop body should have back edge to header
        header = loop_headers[0]
        self.assertTrue(
            any(header.index in cfg.blocks[s].successors for s in header.successors)
            or header.index in header.predecessors
        )

    def test_return_creates_exit_block(self):
        ctx, cfg = _build("""\
void test_func(int x) {
    if (x > 0) {
        return;
    }
    x = 1;
}
""")
        exit_blocks = cfg.exit_blocks()
        self.assertTrue(len(exit_blocks) >= 1)

    def test_switch_creates_child_block(self):
        ctx, cfg = _build("""\
void test_func(int x) {
    switch (x) {
    case 0: x = 1; break;
    case 1: x = 2; break;
    }
}
""")
        self.assertGreaterEqual(len(cfg.blocks), 2)


class TestTerminalBlock(unittest.TestCase):
    def test_return_block_is_terminal(self):
        ctx, cfg = _build("""\
void test_func() {
    return;
}
""")
        exit_blocks = cfg.exit_blocks()
        self.assertTrue(len(exit_blocks) >= 1)
        for b in exit_blocks:
            self.assertTrue(is_terminal_block(b, cfg))

    def test_void_fallthrough_is_terminal(self):
        ctx, cfg = _build("""\
void test_func(int x) {
    x = 1;
}
""")
        # Last block in void function should be terminal (implicit return)
        last = cfg.blocks[-1]
        self.assertTrue(is_terminal_block(last, cfg))

    def test_get_terminal_blocks(self):
        ctx, cfg = _build("""\
void test_func(int x) {
    if (x > 0) {
        return;
    }
    x = 1;
}
""")
        terminals = get_terminal_blocks(cfg)
        self.assertTrue(len(terminals) >= 1)


class TestReachesExit(unittest.TestCase):
    def test_entry_reaches_exit(self):
        ctx, cfg = _build("""\
void test_func() {
    return;
}
""")
        self.assertTrue(reaches_exit(cfg.blocks[0], cfg))

    def test_all_blocks_reach_exit_in_simple_func(self):
        ctx, cfg = _build("""\
void test_func(int x) {
    x = 1;
    return;
}
""")
        for b in cfg.blocks:
            self.assertTrue(reaches_exit(b, cfg))


class TestDominates(unittest.TestCase):
    def test_entry_dominates_all(self):
        ctx, cfg = _build("""\
void test_func(int x) {
    x = 1;
    if (x > 0) {
        x = 2;
    }
    return;
}
""")
        entry = cfg.blocks[cfg.entry]
        for b in cfg.blocks:
            self.assertTrue(dominates(entry, b, cfg))

    def test_self_dominance(self):
        ctx, cfg = _build("""\
void test_func() {
    return;
}
""")
        for b in cfg.blocks:
            self.assertTrue(dominates(b, b, cfg))


class TestBlockForStmt(unittest.TestCase):
    def test_finds_statement_block(self):
        ctx, cfg = _build("""\
void test_func(int x) {
    x = 1;
    return;
}
""")
        # First statement should be in some block
        stmt = ctx.statements[0]
        block = cfg.block_for_stmt(stmt)
        self.assertIsNotNone(block)


class TestStmtInTerminalPosition(unittest.TestCase):
    def test_last_return_is_terminal(self):
        ctx, cfg = _build("""\
int test_func(int x) {
    x = 1;
    return x;
}
""")
        # The return statement should be in terminal position
        return_stmt = None
        for s in ctx.statements:
            if s.type == "return_statement":
                return_stmt = s
        self.assertIsNotNone(return_stmt)
        self.assertTrue(stmt_is_in_terminal_position(return_stmt, cfg))


class TestLiveVariablesAtBlockEntry(unittest.TestCase):
    def test_parameter_live_at_entry(self):
        ctx, cfg = _build("""\
void test_func(int x, int y) {
    y = x + 1;
    return;
}
""")
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        entry = cfg.blocks[cfg.entry]
        live = live_variables_at_block_entry(entry, cfg, analyzer)
        # x is read but not written → should be live at entry
        self.assertIn("x", live)

    def test_local_not_live_at_entry(self):
        ctx, cfg = _build("""\
void test_func() {
    int a = 5;
    a = a + 1;
    return;
}
""")
        analyzer = StatementEffectAnalyzer(ctx.file_source)
        entry = cfg.blocks[cfg.entry]
        live = live_variables_at_block_entry(entry, cfg, analyzer)
        # 'a' is defined before use, so not live at entry
        self.assertNotIn("a", live)


if __name__ == "__main__":
    unittest.main()
