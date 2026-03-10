"""Lightweight control-flow graph for function bodies.

Builds basic blocks from tree-sitter statement lists and identifies
control-flow edges.  Provides terminal-block detection, predecessor/
successor queries, and simple dominance checks without full dataflow.

This is deliberately lightweight — it works at the statement level (not
instruction level) and uses tree-sitter node types for block splitting.
It does NOT build a full SSA/dominance tree.

Usage:
    from .cfg import build_cfg, is_terminal_block

    cfg = build_cfg(ctx.body_node, source)
    for block in cfg.blocks:
        if is_terminal_block(block, cfg):
            # block always reaches function exit
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterator

from tree_sitter import Node

from .control_flow import noncomment_named_children


@dataclass
class BasicBlock:
    """A sequence of straight-line statements with no internal branches."""

    index: int
    statements: list[Node]
    successors: list[int] = field(default_factory=list)
    predecessors: list[int] = field(default_factory=list)

    # Block classification
    is_entry: bool = False
    is_exit: bool = False  # Ends with return/unreachable
    is_loop_header: bool = False

    @property
    def has_return(self) -> bool:
        """Whether this block ends with a return statement."""
        if not self.statements:
            return False
        last = self.statements[-1]
        return last.type == "return_statement"

    @property
    def has_break(self) -> bool:
        if not self.statements:
            return False
        last = self.statements[-1]
        return last.type == "break_statement"

    @property
    def first_stmt(self) -> Node | None:
        return self.statements[0] if self.statements else None

    @property
    def last_stmt(self) -> Node | None:
        return self.statements[-1] if self.statements else None


@dataclass
class CFG:
    """Lightweight control-flow graph for a function body."""

    blocks: list[BasicBlock]
    entry: int = 0  # Index of entry block

    def block_for_stmt(self, stmt: Node) -> BasicBlock | None:
        """Find the block containing a given statement."""
        for block in self.blocks:
            if any(s.id == stmt.id for s in block.statements):
                return block
        return None

    def exit_blocks(self) -> list[BasicBlock]:
        """Return all blocks that are function exits."""
        return [b for b in self.blocks if b.is_exit]

    def successors_of(self, block_idx: int) -> list[BasicBlock]:
        """Return successor blocks."""
        block = self.blocks[block_idx]
        return [self.blocks[i] for i in block.successors if i < len(self.blocks)]

    def predecessors_of(self, block_idx: int) -> list[BasicBlock]:
        """Return predecessor blocks."""
        block = self.blocks[block_idx]
        return [self.blocks[i] for i in block.predecessors if i < len(self.blocks)]


# ---------------------------------------------------------------------------
# CFG construction
# ---------------------------------------------------------------------------

_BRANCH_STMTS = frozenset({
    "if_statement", "switch_statement", "for_statement",
    "while_statement", "do_statement",
})

_TERMINAL_STMTS = frozenset({
    "return_statement", "break_statement", "continue_statement",
    "goto_statement",
})


def build_cfg(body_node: Node, source: bytes) -> CFG:
    """Build a lightweight CFG from a function body (compound_statement).

    Splits statements into basic blocks at control-flow boundaries.
    Identifies entry, exit blocks, and successor/predecessor edges.
    """
    stmts = noncomment_named_children(body_node)
    if not stmts:
        entry = BasicBlock(index=0, statements=[], is_entry=True, is_exit=True)
        return CFG(blocks=[entry])

    blocks: list[BasicBlock] = []
    current_stmts: list[Node] = []

    def _flush_block(is_exit: bool = False) -> int:
        """Flush accumulated statements into a new block."""
        idx = len(blocks)
        block = BasicBlock(
            index=idx,
            statements=list(current_stmts),
            is_entry=(idx == 0),
            is_exit=is_exit,
        )
        blocks.append(block)
        current_stmts.clear()
        return idx

    for stmt in stmts:
        if stmt.type in _BRANCH_STMTS:
            # Flush preceding straight-line code
            if current_stmts:
                prev_idx = _flush_block()

            # The branch statement itself is a block
            current_stmts.append(stmt)
            branch_idx = _flush_block()

            # Create blocks for the branch targets
            _build_branch_edges(blocks, branch_idx, stmt, source)

        elif stmt.type in _TERMINAL_STMTS:
            current_stmts.append(stmt)
            _flush_block(is_exit=(stmt.type == "return_statement"))
        else:
            current_stmts.append(stmt)

    # Flush remaining statements
    if current_stmts:
        _flush_block(is_exit=True)  # Implicit return at end of void function

    # Wire sequential fallthrough edges
    for i in range(len(blocks) - 1):
        block = blocks[i]
        # Don't add fallthrough if block ends with terminal
        if block.statements and block.statements[-1].type in _TERMINAL_STMTS:
            continue
        # Don't add duplicate
        if (i + 1) not in block.successors:
            block.successors.append(i + 1)
            blocks[i + 1].predecessors.append(i)

    return CFG(blocks=blocks)


def _build_branch_edges(
    blocks: list[BasicBlock],
    branch_idx: int,
    stmt: Node,
    source: bytes,
) -> None:
    """Add successor/predecessor edges for branch statements."""
    block = blocks[branch_idx]

    if stmt.type == "if_statement":
        consequence = stmt.child_by_field_name("consequence")
        alternative = stmt.child_by_field_name("alternative")

        # True branch
        if consequence:
            true_idx = _add_child_block(blocks, consequence)
            block.successors.append(true_idx)
            blocks[true_idx].predecessors.append(branch_idx)

        # False branch (else)
        if alternative:
            false_idx = _add_child_block(blocks, alternative)
            block.successors.append(false_idx)
            blocks[false_idx].predecessors.append(branch_idx)

    elif stmt.type in ("for_statement", "while_statement", "do_statement"):
        block.is_loop_header = True
        body = stmt.child_by_field_name("body")
        if body:
            loop_idx = _add_child_block(blocks, body)
            block.successors.append(loop_idx)
            blocks[loop_idx].predecessors.append(branch_idx)
            # Back edge from loop body to header
            blocks[loop_idx].successors.append(branch_idx)
            block.predecessors.append(loop_idx)

    elif stmt.type == "switch_statement":
        body = stmt.child_by_field_name("body")
        if body:
            switch_idx = _add_child_block(blocks, body)
            block.successors.append(switch_idx)
            blocks[switch_idx].predecessors.append(branch_idx)


def _add_child_block(blocks: list[BasicBlock], node: Node) -> int:
    """Create a basic block from a compound statement or bare statement."""
    idx = len(blocks)
    if node.type == "compound_statement":
        children = noncomment_named_children(node)
    else:
        children = [node]

    has_return = any(c.type == "return_statement" for c in children)
    block = BasicBlock(
        index=idx,
        statements=children,
        is_exit=has_return,
    )
    blocks.append(block)
    return idx


# ---------------------------------------------------------------------------
# Terminal block analysis
# ---------------------------------------------------------------------------


def is_terminal_block(block: BasicBlock, cfg: CFG) -> bool:
    """Check if a block is terminal — all paths from it reach function exit.

    A block is terminal if:
    - It ends with a return statement, OR
    - All its successors are terminal (recursively)

    This is useful for tail-call analysis: only terminal blocks can have
    their last call converted to a tail call.
    """
    return _is_terminal_cached(block.index, cfg, set())


def _is_terminal_cached(
    block_idx: int, cfg: CFG, visited: set[int]
) -> bool:
    """Recursive terminal check with cycle detection."""
    if block_idx in visited:
        return False  # Loop — not terminal
    visited.add(block_idx)

    block = cfg.blocks[block_idx]

    # Direct exit
    if block.is_exit:
        return True

    # No successors and not exit → dead code, treat as terminal
    if not block.successors:
        return True

    # All successors must be terminal
    return all(
        _is_terminal_cached(s, cfg, visited)
        for s in block.successors
    )


def reaches_exit(block: BasicBlock, cfg: CFG) -> bool:
    """Check if there's any path from block to a function exit."""
    return _reaches_exit_cached(block.index, cfg, set())


def _reaches_exit_cached(
    block_idx: int, cfg: CFG, visited: set[int]
) -> bool:
    if block_idx in visited:
        return False
    visited.add(block_idx)

    block = cfg.blocks[block_idx]
    if block.is_exit:
        return True
    return any(
        _reaches_exit_cached(s, cfg, visited)
        for s in block.successors
    )


def dominates(dominator: BasicBlock, target: BasicBlock, cfg: CFG) -> bool:
    """Check if dominator block dominates target (all paths to target go through dominator).

    Uses simple reachability: if removing dominator disconnects entry from target,
    then dominator dominates target.  This is O(V+E) per query — fine for small
    function CFGs.
    """
    if dominator.index == target.index:
        return True

    # Can we reach target from entry without going through dominator?
    return not _can_reach_without(
        cfg.entry, target.index, dominator.index, cfg, set()
    )


def _can_reach_without(
    current: int, target: int, excluded: int, cfg: CFG, visited: set[int]
) -> bool:
    """Check if target is reachable from current without passing through excluded."""
    if current == target:
        return True
    if current in visited or current == excluded:
        return False
    visited.add(current)
    return any(
        _can_reach_without(s, target, excluded, cfg, visited)
        for s in cfg.blocks[current].successors
    )


# ---------------------------------------------------------------------------
# Statement-level queries using CFG
# ---------------------------------------------------------------------------


def stmt_is_in_terminal_position(stmt: Node, cfg: CFG) -> bool:
    """Check if a statement is in a terminal block and is the last statement.

    This is the primary API for tail_call_reorder: is this statement's
    position suitable for a tail call?
    """
    block = cfg.block_for_stmt(stmt)
    if block is None:
        return False
    if not is_terminal_block(block, cfg):
        return False
    # Must be the last statement in its block
    return block.last_stmt is not None and block.last_stmt.id == stmt.id


def get_terminal_blocks(cfg: CFG) -> list[BasicBlock]:
    """Return all terminal blocks in the CFG."""
    return [b for b in cfg.blocks if is_terminal_block(b, cfg)]


def live_variables_at_block_entry(
    block: BasicBlock, cfg: CFG, analyzer: object
) -> frozenset[str]:
    """Compute variables that are live at block entry.

    A variable is live at block entry if it is read in this block
    (before any write) or live at some successor's entry and not
    written in this block.

    Requires a StatementEffectAnalyzer for reads/writes per statement.
    """
    from .statement_effects import StatementEffectAnalyzer
    if not isinstance(analyzer, StatementEffectAnalyzer):
        return frozenset()

    return _live_at_entry(block.index, cfg, analyzer, {})


def _live_at_entry(
    block_idx: int,
    cfg: CFG,
    analyzer: object,
    cache: dict[int, frozenset[str]],
) -> frozenset[str]:
    """Compute live variables at block entry with memoization."""
    if block_idx in cache:
        return cache[block_idx]

    # Prevent infinite recursion on loops
    cache[block_idx] = frozenset()

    from .statement_effects import StatementEffectAnalyzer
    assert isinstance(analyzer, StatementEffectAnalyzer)

    block = cfg.blocks[block_idx]

    # Compute reads-before-write (upward exposed uses) in this block
    reads_before_write: set[str] = set()
    killed: set[str] = set()
    for stmt in block.statements:
        effects = analyzer.analyze(stmt)
        # Variables read here that haven't been written yet
        reads_before_write |= (effects.reads - killed)
        killed |= effects.writes

    # Live from successors (not killed in this block)
    live_from_succs: set[str] = set()
    for succ_idx in block.successors:
        if succ_idx < len(cfg.blocks):
            succ_live = _live_at_entry(succ_idx, cfg, analyzer, cache)
            live_from_succs |= succ_live

    result = frozenset(reads_before_write | (live_from_succs - killed))
    cache[block_idx] = result
    return result
