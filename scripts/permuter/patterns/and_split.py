"""And-split pattern — split && conditions into nested ifs (and reverse).

The compiler generates different branch structures for:
    if (a && b) { body }
vs
    if (a) { if (b) { body } }

Splitting can fix CONTROL_FLOW diff_ops where the original used nested branches
instead of short-circuit evaluation (or vice versa).

Example:
    if (a && b) { foo(); }
    ->
    if (a) { if (b) { foo(); } }
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent, walk
from ..types import Diagnosis, FunctionContext, Variant

_BRANCH_OPCODES = {"beq", "bne", "ble", "bgt", "bge", "blt",
                   "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
                   "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-"}


class AndSplitPattern(Pattern):
    name = "and_split"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Relevant when control flow differs
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        return bool(diagnosis.clusters)

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strong: multiple branch diffs + clusters (structural control flow change)
        branch_count = sum(
            1 for d in diagnosis.diff_ops
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES
        )
        if branch_count >= 2 and len(diagnosis.clusters) >= 2:
            return 0.7
        if diagnosis.clusters:
            return 0.4
        return 0.2  # branch diffs only — could be simpler polarity flip

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0

        # Ghidra-guided: only generate the direction that matches Ghidra's structure
        if ctx.ghidra_ast is not None:
            for variant in self._try_ghidra_guided(ctx, counter):
                yield variant
                counter += 1
            if counter > 0:
                return  # Ghidra guided produced candidates, skip blind

        for stmt in ctx.statements:
            # Split: if (a && b) -> if (a) { if (b) }
            for variant in _split_and_conditions(stmt, ctx, counter):
                yield variant
                counter += 1
            # Merge: if (a) { if (b) { body } } -> if (a && b) { body }
            for variant in _merge_nested_ifs(stmt, ctx, counter):
                yield variant
                counter += 1

    def _try_ghidra_guided(self, ctx: FunctionContext, start_counter: int) -> Iterator[Variant]:
        """Generate only the direction indicated by Ghidra's condition structure."""
        from ..ghidra_ast import extract_condition_structure

        tags = extract_condition_structure(ctx.ghidra_ast)
        if not tags:
            return

        has_conjunction = "conjunction" in tags
        has_nested_if = "nested_if" in tags

        counter = start_counter

        # Ghidra has && but source has nested-if -> merge only
        # Ghidra has nested-if but source has && -> split only
        # If ambiguous, skip (fall through to blind)
        do_split = False
        do_merge = False

        if has_conjunction and not has_nested_if:
            # Ghidra uses conjunction — source should too; merge nested-ifs
            do_merge = True
        elif has_nested_if and not has_conjunction:
            # Ghidra uses nested-if — source should too; split conjunctions
            do_split = True
        else:
            # Ambiguous or no signal from condition_structure — try CF skeleton
            from ..ghidra_ast import extract_control_flow_skeleton

            skeleton = extract_control_flow_skeleton(ctx.ghidra_ast)
            if not skeleton:
                return

            max_consecutive_ifs = _count_consecutive_ifs(skeleton)
            guard_pairs = _count_guard_return_pairs(skeleton)

            if max_consecutive_ifs >= 2:
                # Ghidra uses nested ifs -> source probably has conjunction -> try split
                do_split = True
            elif guard_pairs >= 2:
                # Ghidra uses guards -> early_return_merge handles this, not us
                return
            else:
                return

        for stmt in ctx.statements:
            if do_split:
                for variant in _split_and_conditions(stmt, ctx, counter):
                    variant = Variant(
                        name=f"ghidra_andsplit_{counter}",
                        pattern_name=variant.pattern_name,
                        description=f"[ghidra] {variant.description}",
                        source=variant.source,
                        tags=variant.tags,
                    )
                    yield variant
                    counter += 1
            if do_merge:
                for variant in _merge_nested_ifs(stmt, ctx, counter):
                    variant = Variant(
                        name=f"ghidra_andsplit_{counter}",
                        pattern_name=variant.pattern_name,
                        description=f"[ghidra] {variant.description}",
                        source=variant.source,
                        tags=variant.tags,
                    )
                    yield variant
                    counter += 1


def _split_and_conditions(
    stmt: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Find if (a && b) and split into nested ifs."""
    source = ctx.file_source

    for node in walk(stmt):
        if node.type != "if_statement":
            continue

        condition = node.child_by_field_name("condition")
        consequence = node.child_by_field_name("consequence")
        alternative = node.child_by_field_name("alternative")

        if condition is None or consequence is None:
            continue

        # Get the inner expression from condition_clause
        inner = _get_inner_expr(condition)
        if inner is None or inner.type != "binary_expression":
            continue

        op = inner.child_by_field_name("operator")
        if op is None or op.text != b"&&":
            continue

        left = inner.child_by_field_name("left")
        right = inner.child_by_field_name("right")
        if left is None or right is None:
            continue

        left_text = source[left.start_byte:left.end_byte]
        right_text = source[right.start_byte:right.end_byte]
        cons_text = source[consequence.start_byte:consequence.end_byte]
        indent = get_indent(source, node)

        if alternative is None:
            # Simple case: if (a && b) { body }
            # -> if (a) { if (b) { body } }
            inner_if = indent + b"    " + b"if (" + right_text + b") " + cons_text
            new_body = b"{\n" + inner_if + b"\n" + indent + b"}"

            new_source = (
                source[:condition.start_byte]
                + b"(" + left_text + b")"
                + source[condition.end_byte:consequence.start_byte]
                + new_body
                + source[consequence.end_byte:]
            )
        else:
            # With else: if (a && b) { body } else { alt }
            # -> if (a) { if (b) { body } else { alt } } else { alt }
            alt_body = _get_else_body(alternative, source)
            if alt_body is None:
                continue

            inner_if = (indent + b"    " + b"if (" + right_text + b") "
                       + cons_text + b" else " + alt_body)
            new_body = b"{\n" + inner_if + b"\n" + indent + b"}"

            new_source = (
                source[:condition.start_byte]
                + b"(" + left_text + b")"
                + source[condition.end_byte:consequence.start_byte]
                + new_body
                + b" else " + alt_body
                + source[alternative.end_byte:]
            )

        yield Variant(
            name=f"andsplit_{counter}",
            pattern_name="and_split",
            description=f"Split && into nested if: ({left_text.decode(errors='replace')}) && ({right_text.decode(errors='replace')})",
            source=new_source,
        )
        counter += 1


def _merge_nested_ifs(
    stmt: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Find if (a) { if (b) { body } } and merge into if (a && b) { body }."""
    source = ctx.file_source

    for node in walk(stmt):
        if node.type != "if_statement":
            continue

        condition = node.child_by_field_name("condition")
        consequence = node.child_by_field_name("consequence")
        alternative = node.child_by_field_name("alternative")

        if condition is None or consequence is None:
            continue

        # Check if consequence is { if (b) { body } } with nothing else
        inner_if = _get_sole_inner_if(consequence)
        if inner_if is None:
            continue

        inner_cond = inner_if.child_by_field_name("condition")
        inner_cons = inner_if.child_by_field_name("consequence")
        inner_alt = inner_if.child_by_field_name("alternative")

        if inner_cond is None or inner_cons is None:
            continue

        outer_expr = _get_inner_expr(condition)
        inner_expr = _get_inner_expr(inner_cond)
        if outer_expr is None or inner_expr is None:
            continue

        outer_text = source[outer_expr.start_byte:outer_expr.end_byte]
        inner_text = source[inner_expr.start_byte:inner_expr.end_byte]
        inner_cons_text = source[inner_cons.start_byte:inner_cons.end_byte]

        if inner_alt is not None:
            # Inner has else — only merge if outer also has matching else
            if alternative is None:
                continue

            inner_alt_body = _get_else_body(inner_alt, source)
            outer_alt_body = _get_else_body(alternative, source)

            if inner_alt_body is None or outer_alt_body is None:
                continue

            # Only merge if else bodies are identical (ignoring indentation)
            if _normalize_ws(inner_alt_body) != _normalize_ws(outer_alt_body):
                continue

            # Merge: if (outer && inner) { inner_body } else { alt }
            new_source = (
                source[:condition.start_byte]
                + b"(" + outer_text + b" && " + inner_text + b")"
                + source[condition.end_byte:consequence.start_byte]
                + inner_cons_text
                + b" else " + outer_alt_body
                + source[alternative.end_byte:]
            )
        else:
            # Simple case: no else on either
            if alternative is not None:
                continue

            # Merge: if (outer && inner) { inner_body }
            new_source = (
                source[:condition.start_byte]
                + b"(" + outer_text + b" && " + inner_text + b")"
                + source[condition.end_byte:consequence.start_byte]
                + inner_cons_text
                + source[consequence.end_byte:]
            )

        yield Variant(
            name=f"andsplit_{counter}",
            pattern_name="and_split",
            description=f"Merge nested ifs: ({outer_text.decode(errors='replace')}) && ({inner_text.decode(errors='replace')})",
            source=new_source,
        )
        counter += 1


def _normalize_ws(text: bytes) -> bytes:
    """Normalize whitespace for comparison: collapse runs of spaces/tabs/newlines."""
    return b" ".join(text.split())


def _get_else_body(alternative: Node, source: bytes) -> bytes | None:
    """Extract the body (block or statement) from an else_clause node."""
    for child in alternative.named_children:
        if child.type != "comment":
            return source[child.start_byte:child.end_byte]
    return None


def _get_inner_expr(condition: Node) -> Node | None:
    """Extract the inner expression from a condition_clause."""
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None


def _get_sole_inner_if(compound_stmt: Node) -> Node | None:
    """Check if a compound_statement contains exactly one if_statement."""
    if compound_stmt.type != "compound_statement":
        return None

    stmts = [c for c in compound_stmt.named_children if c.type != "comment"]
    if len(stmts) == 1 and stmts[0].type == "if_statement":
        return stmts[0]
    return None


def _count_consecutive_ifs(skeleton: list[str]) -> int:
    """Count the longest run of consecutive 'if' entries in a CF skeleton."""
    max_run = 0
    current_run = 0
    for item in skeleton:
        if item == "if":
            current_run += 1
            max_run = max(max_run, current_run)
        else:
            current_run = 0
    return max_run


def _count_guard_return_pairs(skeleton: list[str]) -> int:
    """Count 'if' immediately followed by 'return' (guard pattern)."""
    count = 0
    for i in range(len(skeleton) - 1):
        if skeleton[i] == "if" and skeleton[i + 1] == "return":
            count += 1
    return count
