"""Commutative expression grouping — regroup chains of commutative operators.

Simple binary swaps (`a + b` -> `b + a`) produce identical code on MSVC PPC.
However, regrouping 3+ term chains changes instruction scheduling:
    (a + b) + c  ->  a + (b + c)   (different register lifetimes)
    (a + b) + c  ->  (c + a) + b   (different evaluation order)

This pattern finds chains of the same commutative operator and generates
regrouping variants by changing associativity and term order.

Example:
    float x = (a + b) + c;
    ->
    float x = a + (b + c);
"""

from __future__ import annotations

from itertools import permutations
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..types import Diagnosis, FunctionContext, Variant

_COMMUTATIVE_OPS = {"+", "*", "&", "|", "^"}


class CommutativeSwapPattern(Pattern):
    name = "commutative_swap"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Skip if function is all noise (no real mismatches)
        if not diagnosis.diff_ops and not diagnosis.clusters:
            return False

        # Only relevant if there are arithmetic opcode mismatches
        arith_opcodes = {
            "add", "addi", "addis", "fadd", "fadds",
            "fmul", "fmuls", "fmadd", "fmadds",
            "and", "andi.", "andis.", "or", "ori", "oris",
            "xor", "xori", "xoris",
        }
        for d in diagnosis.diff_ops:
            if d.target_opcode in arith_opcodes or d.base_opcode in arith_opcodes:
                return True

        # Check for clusters near arithmetic (might indicate expression differences)
        if len(diagnosis.clusters) >= 2:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        return 0.3  # Wins exist but rare

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        for stmt in ctx.statements:
            for variant in _find_chains(stmt, ctx, counter):
                yield variant
                counter += 1


def _collect_chain(node: Node, op: str) -> list[Node] | None:
    """Collect all leaf terms in a chain of the same commutative operator.

    For `(a + b) + c` parsed as binary_expression(binary_expression(a, +, b), +, c),
    returns [a, b, c].
    """
    if node.type != "binary_expression":
        return None

    op_node = node.child_by_field_name("operator")
    if op_node is None or op_node.text is None:
        return None
    if op_node.text.decode("utf-8") != op:
        return None

    left = node.child_by_field_name("left")
    right = node.child_by_field_name("right")
    if left is None or right is None:
        return None

    terms: list[Node] = []

    # Recurse left if same operator
    left_chain = _collect_chain(left, op)
    if left_chain is not None:
        terms.extend(left_chain)
    else:
        terms.append(left)

    # Recurse right if same operator
    right_chain = _collect_chain(right, op)
    if right_chain is not None:
        terms.extend(right_chain)
    else:
        terms.append(right)

    return terms


def _build_left_assoc(term_texts: list[bytes], op: bytes) -> bytes:
    """Build a left-associative expression: ((a op b) op c) op d."""
    result = term_texts[0]
    for t in term_texts[1:]:
        result = b"(" + result + b" " + op + b" " + t + b")"
    return result


def _build_right_assoc(term_texts: list[bytes], op: bytes) -> bytes:
    """Build a right-associative expression: a op (b op (c op d))."""
    result = term_texts[-1]
    for t in reversed(term_texts[:-1]):
        result = b"(" + t + b" " + op + b" " + result + b")"
    return result


def _build_flat(term_texts: list[bytes], op: bytes) -> bytes:
    """Build without extra parens: a op b op c (left-associative by default)."""
    return (b" " + op + b" ").join(term_texts)


def _find_chains(
    node: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Find chains of 3+ commutative terms and generate regrouping variants."""
    source = ctx.file_source

    if node.type == "binary_expression":
        op_node = node.child_by_field_name("operator")
        if op_node and op_node.text:
            op_str = op_node.text.decode("utf-8")
            if op_str in _COMMUTATIVE_OPS:
                terms = _collect_chain(node, op_str)
                if terms is not None and len(terms) >= 3:  # Skip 2-term: swapping is useless on MSVC PPC
                    # Check this is the topmost node of the chain (parent isn't same op)
                    parent = node.parent
                    if parent is not None and parent.type == "binary_expression":
                        parent_op = parent.child_by_field_name("operator")
                        if parent_op and parent_op.text and parent_op.text.decode("utf-8") == op_str:
                            # Not the top of the chain, skip (parent will handle it)
                            # Still recurse children in case there are nested different-op chains
                            for child in node.children:
                                yield from _find_chains(child, ctx, counter)
                            return

                    op_bytes = op_str.encode("utf-8")
                    term_texts = [source[t.start_byte:t.end_byte] for t in terms]
                    original_text = source[node.start_byte:node.end_byte]

                    seen: set[bytes] = {original_text}
                    variants_generated = 0
                    max_variants = 10

                    # 1. Right-associative grouping
                    right_assoc = _build_right_assoc(term_texts, op_bytes)
                    if right_assoc not in seen:
                        seen.add(right_assoc)
                        new_source = source[:node.start_byte] + right_assoc + source[node.end_byte:]
                        yield Variant(
                            name=f"commgroup_{counter}",
                            pattern_name="commutative_swap",
                            description=f"regroup {op_str}: right-associative",
                            source=new_source,
                        )
                        counter += 1
                        variants_generated += 1

                    # 2. Reversed terms, left-associative
                    rev_texts = list(reversed(term_texts))
                    rev_left = _build_flat(rev_texts, op_bytes)
                    if rev_left not in seen:
                        seen.add(rev_left)
                        new_source = source[:node.start_byte] + rev_left + source[node.end_byte:]
                        yield Variant(
                            name=f"commgroup_{counter}",
                            pattern_name="commutative_swap",
                            description=f"regroup {op_str}: reversed terms",
                            source=new_source,
                        )
                        counter += 1
                        variants_generated += 1

                    # 3. Reversed terms, right-associative
                    rev_right = _build_right_assoc(rev_texts, op_bytes)
                    if rev_right not in seen:
                        seen.add(rev_right)
                        new_source = source[:node.start_byte] + rev_right + source[node.end_byte:]
                        yield Variant(
                            name=f"commgroup_{counter}",
                            pattern_name="commutative_swap",
                            description=f"regroup {op_str}: reversed right-assoc",
                            source=new_source,
                        )
                        counter += 1
                        variants_generated += 1

                    # 4. Pairwise swaps of adjacent terms with different groupings
                    n = len(term_texts)
                    if n <= 5:
                        # Generate select permutations (not all n!)
                        for perm in _select_permutations(term_texts, max_variants - variants_generated):
                            perm_list = list(perm)

                            # Left-associative (flat)
                            flat = _build_flat(perm_list, op_bytes)
                            if flat not in seen:
                                seen.add(flat)
                                new_source = source[:node.start_byte] + flat + source[node.end_byte:]
                                yield Variant(
                                    name=f"commgroup_{counter}",
                                    pattern_name="commutative_swap",
                                    description=f"regroup {op_str}: permuted flat",
                                    source=new_source,
                                )
                                counter += 1
                                variants_generated += 1

                            if variants_generated >= max_variants:
                                break

                            # Right-associative for same perm
                            ra = _build_right_assoc(perm_list, op_bytes)
                            if ra not in seen:
                                seen.add(ra)
                                new_source = source[:node.start_byte] + ra + source[node.end_byte:]
                                yield Variant(
                                    name=f"commgroup_{counter}",
                                    pattern_name="commutative_swap",
                                    description=f"regroup {op_str}: permuted right-assoc",
                                    source=new_source,
                                )
                                counter += 1
                                variants_generated += 1

                            if variants_generated >= max_variants:
                                break

                    # Don't recurse into children of this chain — we handled the whole thing
                    return

    # Recurse into children
    for child in node.children:
        yield from _find_chains(child, ctx, counter)


def _select_permutations(
    terms: list[bytes], max_count: int
) -> Iterator[tuple[bytes, ...]]:
    """Yield a selection of interesting permutations (adjacent swaps first)."""
    n = len(terms)
    seen: set[tuple[bytes, ...]] = {tuple(terms)}

    # Adjacent swaps
    for i in range(n - 1):
        perm = list(terms)
        perm[i], perm[i + 1] = perm[i + 1], perm[i]
        t = tuple(perm)
        if t not in seen:
            seen.add(t)
            yield t
            if len(seen) - 1 >= max_count:
                return

    # Rotate by 1
    rotated = tuple(terms[1:] + terms[:1])
    if rotated not in seen:
        seen.add(rotated)
        yield rotated
        if len(seen) - 1 >= max_count:
            return

    # For small chains, try remaining permutations
    if n <= 4:
        for perm in permutations(terms):
            t = tuple(perm)
            if t not in seen:
                seen.add(t)
                yield t
                if len(seen) - 1 >= max_count:
                    return
