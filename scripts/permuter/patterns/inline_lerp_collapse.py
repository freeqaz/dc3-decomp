"""Inline lerp collapse — fuse split-form per-field lerps into inline expressions.

Win rate: 2/2 hand-applied (SpotlightDrawerEntry::Animate 83.8->100%,
AnimateSpotlightDrawerFromPreset 95.2->98.8%).

When a leaf function does N parallel float lerps with intermediate ``dN`` /
``rN`` locals, MWCC's IPA decides each is live across other field operations
and interleaves the field loads in the wrong order. Rewriting each lerp as a
single inline ``lvalue = f * (e.field - tmp) + tmp;`` lets MWCC use a uniform
pipeline (load/compute/store contiguous per field), matching the target.

Before::

    float dX = e.x - tmpX; float rX = f * dX + tmpX; dst.x = rX;
    float dY = e.y - tmpY; float rY = f * dY + tmpY; dst.y = rY;

After::

    dst.x = f * (e.x - tmpX) + tmpX;
    dst.y = f * (e.y - tmpY) + tmpY;

MEMORY ref: feedback_inline_lerp_no_intermediate.

The pattern detects triples of three consecutive statements:

    1. ``T  local_d = <diff_expr>;``                  -- diff/subtract decl
    2. ``T  local_r = <factor> * local_d + <addend>;`` -- fused multiply-add decl
    3. ``<lvalue> = local_r;``                         -- store to member/lvalue

Where ``local_d`` and ``local_r`` are referenced only inside the triple, and a
group of 2+ triples shares the same ``<factor>`` expression.
"""

from __future__ import annotations

import re
from typing import Iterator, List, Optional, Tuple

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Cap the number of variants emitted per call to keep budgets bounded.
_MAX_VARIANTS = 6
# Minimum triples a group needs before we emit a collapse variant.
_MIN_TRIPLES = 2


class InlineLerpCollapsePattern(Pattern):
    name = "inline_lerp_collapse"
    safety_tier = "moderate"
    structural_domain = "expression_shape"
    follow_ups = ("variable_inline", "temp_elimination")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Strong signal: FPR fmuls/fadds/fmadds operand commutativity or
        # ordering mismatches with no per-instruction opcode swap.
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("fmadds", "fmuls", "fadds", "fsubs", "fmsubs"):
                return True
            if d.base_opcode in ("fmadds", "fmuls", "fadds", "fsubs", "fmsubs"):
                return True

        # Callee-saved FPR swaps mean the scheduler chose a different
        # cache-and-reuse plan for the intermediate float locals.
        for (r0, r1) in diagnosis.reg_swap_pairs:
            if (r0.startswith("f") and _is_callee_saved(r0)) or (
                r1.startswith("f") and _is_callee_saved(r1)
            ):
                return True

        # Clusters or prologue mismatches involving FPRs are also a sign.
        if diagnosis.fpr_save_delta != 0:
            return True

        # Pragmatic fallback: still let the AST scan decide. The AST gate
        # (≥2 triples in body) is strong enough on its own.
        return True

    def priority(self, diagnosis: Diagnosis) -> float:
        score = 0.25
        for d in diagnosis.diff_ops:
            if d.target_opcode == d.base_opcode and d.target_opcode in (
                "fmadds", "fmuls", "fadds", "fsubs", "fmsubs",
            ):
                score = max(score, 0.7)
        if diagnosis.fpr_save_delta != 0:
            score = max(score, 0.5)
        return score

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        stmts = ctx.statements

        # First, collect every triple in the top-level statement list.
        triples = _collect_triples(stmts, source)
        if len(triples) < _MIN_TRIPLES:
            # Also try inside any single-block child (e.g. function body's
            # singleton if/for body whose statements weren't promoted up).
            triples = _collect_triples_recursive(ctx.body_node, source)
        if len(triples) < _MIN_TRIPLES:
            return

        # Group triples by their shared factor expression text and locality.
        groups = _group_triples(triples)
        groups = [g for g in groups if len(g) >= _MIN_TRIPLES]
        if not groups:
            return

        counter = 0

        # Emit one variant per group (full collapse).
        for group in groups:
            if counter >= _MAX_VARIANTS:
                break
            variant = _make_collapse_variant(
                ctx, source, group, name_suffix=f"{counter}"
            )
            if variant is not None:
                yield variant
                counter += 1

        # If a group has >=3 triples, optionally yield a partial-collapse
        # variant that collapses only the first N-1 triples — useful if the
        # full collapse over-shoots scheduling.
        for group in groups:
            if counter >= _MAX_VARIANTS:
                break
            if len(group) < 3:
                continue
            partial = group[:-1]
            variant = _make_collapse_variant(
                ctx, source, partial, name_suffix=f"partial_{counter}"
            )
            if variant is not None:
                yield variant
                counter += 1


# ---------------------------------------------------------------------------
# Triple detection
# ---------------------------------------------------------------------------


_CALLEE_SAVED_FPR_RE = re.compile(r"^f(1[4-9]|2\d|3[01])$")


def _is_callee_saved(reg: str) -> bool:
    return bool(_CALLEE_SAVED_FPR_RE.match(reg))


class _Triple:
    """A detected (diff-decl, fma-decl, store) triple."""

    __slots__ = (
        "stmt_indices",
        "stmts",
        "diff_local",
        "fma_local",
        "diff_expr",
        "fma_factor",
        "fma_addend",
        "store_lvalue",
        "decl_type",
    )

    def __init__(
        self,
        stmt_indices: Tuple[int, int, int],
        stmts: Tuple[Node, Node, Node],
        diff_local: bytes,
        fma_local: bytes,
        diff_expr: bytes,
        fma_factor: bytes,
        fma_addend: bytes,
        store_lvalue: bytes,
        decl_type: bytes,
    ) -> None:
        self.stmt_indices = stmt_indices
        self.stmts = stmts
        self.diff_local = diff_local
        self.fma_local = fma_local
        self.diff_expr = diff_expr
        self.fma_factor = fma_factor
        self.fma_addend = fma_addend
        self.store_lvalue = store_lvalue
        self.decl_type = decl_type


def _collect_triples_recursive(
    container: Node, source: bytes
) -> List[_Triple]:
    """Try the top-level statement list, then descend into single-child blocks."""
    direct = _collect_triples(list(container.named_children), source)
    if len(direct) >= _MIN_TRIPLES:
        return direct
    # Descend into compound_statement / for / if bodies.
    best = direct
    for child in container.named_children:
        if child.type in ("compound_statement", "for_statement", "if_statement",
                          "while_statement"):
            sub = _collect_triples_recursive(child, source)
            if len(sub) > len(best):
                best = sub
    return best


def _collect_triples(stmts: List[Node], source: bytes) -> List[_Triple]:
    """Scan a flat statement list for 3-statement lerp triples."""
    triples: List[_Triple] = []
    i = 0
    while i + 2 < len(stmts):
        diff_info = _match_diff_decl(stmts[i], source)
        if diff_info is None:
            i += 1
            continue
        diff_local, diff_expr, decl_type = diff_info

        fma_info = _match_fma_decl(stmts[i + 1], source, diff_local)
        if fma_info is None:
            i += 1
            continue
        fma_local, fma_factor, fma_addend = fma_info

        store_info = _match_store(stmts[i + 2], source, fma_local)
        if store_info is None:
            i += 1
            continue
        store_lvalue = store_info

        # Locality: diff_local must appear only in stmt[i] and stmt[i+1];
        # fma_local must appear only in stmt[i+1] and stmt[i+2].
        if not _is_local_to_range(
            stmts, source, diff_local, allowed=(i, i + 1)
        ):
            i += 1
            continue
        if not _is_local_to_range(
            stmts, source, fma_local, allowed=(i + 1, i + 2)
        ):
            i += 1
            continue

        triples.append(_Triple(
            stmt_indices=(i, i + 1, i + 2),
            stmts=(stmts[i], stmts[i + 1], stmts[i + 2]),
            diff_local=diff_local,
            fma_local=fma_local,
            diff_expr=diff_expr,
            fma_factor=fma_factor,
            fma_addend=fma_addend,
            store_lvalue=store_lvalue,
            decl_type=decl_type,
        ))
        i += 3

    return triples


def _match_diff_decl(
    stmt: Node, source: bytes
) -> Optional[Tuple[bytes, bytes, bytes]]:
    """Match ``T local = <expr>;`` and return (name, init_expr, type).

    The init expression must be a binary_expression (so the value differs
    from a plain identifier — required for the inline form to make sense).
    """
    if stmt.type != "declaration":
        return None
    init_decls = [c for c in stmt.named_children if c.type == "init_declarator"]
    if len(init_decls) != 1:
        return None
    init_decl = init_decls[0]
    declarator = init_decl.child_by_field_name("declarator")
    value = init_decl.child_by_field_name("value")
    if declarator is None or value is None:
        return None
    if declarator.type != "identifier":
        return None
    # Skip pointer/reference declarators — must be a plain value local.
    name_bytes = source[declarator.start_byte:declarator.end_byte]

    # The first non-init_declarator child is the type specifier.
    type_node = None
    for child in stmt.named_children:
        if child.type != "init_declarator":
            type_node = child
            break
    if type_node is None:
        return None
    type_bytes = source[type_node.start_byte:type_node.end_byte]

    # Init must be a binary expression (a-b, a.x - tmp, etc.). A bare
    # identifier or call expression wouldn't benefit from inlining.
    if value.type not in ("binary_expression", "parenthesized_expression"):
        return None

    init_bytes = source[value.start_byte:value.end_byte]
    return name_bytes, init_bytes, type_bytes


def _match_fma_decl(
    stmt: Node, source: bytes, expected_local: bytes
) -> Optional[Tuple[bytes, bytes, bytes]]:
    """Match ``T fma = <factor> * <expected_local> + <addend>;``.

    Returns (fma_name, factor_text, addend_text). The * and + nesting may
    follow either ``(f * d) + a`` or ``a + (f * d)``.
    """
    if stmt.type != "declaration":
        return None
    init_decls = [c for c in stmt.named_children if c.type == "init_declarator"]
    if len(init_decls) != 1:
        return None
    init_decl = init_decls[0]
    declarator = init_decl.child_by_field_name("declarator")
    value = init_decl.child_by_field_name("value")
    if declarator is None or value is None:
        return None
    if declarator.type != "identifier":
        return None
    name_bytes = source[declarator.start_byte:declarator.end_byte]

    # Outermost expression must be a `+` binary_expression.
    parsed = _parse_fma(value, source, expected_local)
    if parsed is None:
        return None
    factor_bytes, addend_bytes = parsed
    return name_bytes, factor_bytes, addend_bytes


def _parse_fma(
    expr: Node, source: bytes, expected_local: bytes
) -> Optional[Tuple[bytes, bytes]]:
    """Parse ``factor * expected_local + addend`` or its commuted variant.

    Returns (factor_bytes, addend_bytes) on success.
    """
    # Unwrap a parenthesized_expression once.
    if expr.type == "parenthesized_expression":
        inner = None
        for child in expr.named_children:
            inner = child
            break
        if inner is None:
            return None
        expr = inner

    if expr.type != "binary_expression":
        return None

    op = expr.child_by_field_name("operator")
    left = expr.child_by_field_name("left")
    right = expr.child_by_field_name("right")
    if op is None or left is None or right is None:
        return None
    if source[op.start_byte:op.end_byte] != b"+":
        return None

    mul_node, addend = _which_side_is_mul(left, right, source, expected_local)
    if mul_node is None:
        return None

    # Extract factor from the mul_node (it's a `factor * expected_local` form).
    factor = _extract_factor(mul_node, source, expected_local)
    if factor is None:
        return None

    addend_bytes = _strip_parens(source[addend.start_byte:addend.end_byte])
    return factor, addend_bytes


def _which_side_is_mul(
    left: Node, right: Node, source: bytes, expected_local: bytes
) -> Tuple[Optional[Node], Optional[Node]]:
    """Return (mul_side, other_side) — the side that's `factor * expected_local`."""
    for cand, other in ((left, right), (right, left)):
        c = _peel_parens(cand)
        if c.type != "binary_expression":
            continue
        op = c.child_by_field_name("operator")
        if op is None or source[op.start_byte:op.end_byte] != b"*":
            continue
        l = c.child_by_field_name("left")
        r = c.child_by_field_name("right")
        if l is None or r is None:
            continue
        l_text = source[l.start_byte:l.end_byte]
        r_text = source[r.start_byte:r.end_byte]
        if l_text == expected_local or r_text == expected_local:
            return c, other
    return None, None


def _extract_factor(
    mul_node: Node, source: bytes, expected_local: bytes
) -> Optional[bytes]:
    """From a `factor * expected_local` binary_expression, return factor bytes."""
    l = mul_node.child_by_field_name("left")
    r = mul_node.child_by_field_name("right")
    if l is None or r is None:
        return None
    l_text = source[l.start_byte:l.end_byte]
    r_text = source[r.start_byte:r.end_byte]
    if l_text == expected_local:
        return _strip_parens(r_text)
    if r_text == expected_local:
        return _strip_parens(l_text)
    return None


def _peel_parens(node: Node) -> Node:
    while node.type == "parenthesized_expression":
        inner = None
        for child in node.named_children:
            inner = child
            break
        if inner is None:
            return node
        node = inner
    return node


def _strip_parens(text: bytes) -> bytes:
    text = text.strip()
    while text.startswith(b"(") and text.endswith(b")") and _parens_balanced_outer(text):
        text = text[1:-1].strip()
    return text


def _parens_balanced_outer(text: bytes) -> bool:
    """True if the outermost paren pair encloses everything."""
    depth = 0
    for i, ch in enumerate(text):
        if ch == ord("("):
            depth += 1
        elif ch == ord(")"):
            depth -= 1
            if depth == 0 and i != len(text) - 1:
                return False
    return depth == 0


def _match_store(
    stmt: Node, source: bytes, expected_local: bytes
) -> Optional[bytes]:
    """Match ``<lvalue> = expected_local;`` and return lvalue bytes."""
    if stmt.type != "expression_statement":
        return None
    expr = None
    for child in stmt.named_children:
        if child.type == "assignment_expression":
            expr = child
            break
    if expr is None:
        return None
    op = expr.child_by_field_name("operator")
    if op is None or source[op.start_byte:op.end_byte] != b"=":
        return None
    left = expr.child_by_field_name("left")
    right = expr.child_by_field_name("right")
    if left is None or right is None:
        return None
    if source[right.start_byte:right.end_byte] != expected_local:
        return None
    return source[left.start_byte:left.end_byte]


def _is_local_to_range(
    stmts: List[Node], source: bytes, name: bytes, allowed: Tuple[int, int]
) -> bool:
    """True if ``name`` appears only inside stmts whose index is in `allowed`."""
    for i, stmt in enumerate(stmts):
        if i in allowed:
            continue
        for node in walk(stmt):
            if node.type == "identifier" and source[node.start_byte:node.end_byte] == name:
                return False
    return True


# ---------------------------------------------------------------------------
# Group + emit
# ---------------------------------------------------------------------------


def _group_triples(triples: List[_Triple]) -> List[List[_Triple]]:
    """Cluster contiguous triples that share the same factor expression."""
    groups: List[List[_Triple]] = []
    current: List[_Triple] = []
    prev_end_idx: int = -10
    prev_factor: Optional[bytes] = None
    for t in triples:
        contiguous = (t.stmt_indices[0] == prev_end_idx + 1)
        same_factor = (t.fma_factor == prev_factor)
        if current and contiguous and same_factor:
            current.append(t)
        else:
            if current:
                groups.append(current)
            current = [t]
        prev_end_idx = t.stmt_indices[2]
        prev_factor = t.fma_factor
    if current:
        groups.append(current)
    return groups


def _make_collapse_variant(
    ctx: FunctionContext,
    source: bytes,
    group: List[_Triple],
    name_suffix: str,
) -> Optional[Variant]:
    """Build a variant collapsing all triples in *group* into inline form."""
    ed = SourceEditor(source)

    first_stmt = group[0].stmts[0]
    last_stmt = group[-1].stmts[2]

    # Determine indentation from the line of the first statement.
    indent = _indent_of(source, first_stmt.start_byte)

    # Build the replacement block: one collapsed line per triple.
    lines: List[bytes] = []
    for t in group:
        diff = _strip_parens(t.diff_expr)
        addend = t.fma_addend
        factor = t.fma_factor
        lvalue = t.store_lvalue
        # Always wrap diff in parentheses: it's a subtract/binary expression.
        line = (
            indent + lvalue + b" = " + factor + b" * (" + diff + b") + "
            + addend + b";"
        )
        lines.append(line)

    replacement = b"\n".join(lines)

    # Replace the byte span from the start of the first decl's line to the
    # end of the last store statement.
    start_byte = _line_start(source, first_stmt.start_byte)
    end_byte = last_stmt.end_byte

    ed.replace_range(start_byte, end_byte, _strip_indent_prefix(replacement, indent))

    try:
        new_source = ed.apply()
    except ValueError:
        return None

    if new_source == source:
        return None

    n_fields = len(group)
    desc = (
        f"Collapse {n_fields} split-form lerp triples into inline f*(e-t)+t"
    )
    return Variant(
        name=f"lerpcoll_{name_suffix}",
        pattern_name="inline_lerp_collapse",
        description=desc,
        source=new_source,
        func_byte_range=ctx.func_byte_range,
        original_source=source,
    )


def _strip_indent_prefix(text: bytes, indent: bytes) -> bytes:
    """The first line of *text* already includes indent — strip it once.

    We then re-emit relative to ``start_byte`` (which is line_start), so the
    first line's leading indent is absorbed by start_byte's position.
    """
    # The replacement covers from line_start; we need the leading indent
    # included for line 1 (it would be present in the original) and for
    # subsequent lines via the join above. Since we built lines using
    # `indent + ...` for every line, the resulting bytes are already
    # correctly indented for substitution at line_start.
    return text


def _line_start(source: bytes, pos: int) -> int:
    while pos > 0 and source[pos - 1:pos] not in (b"\n", b"\r"):
        pos -= 1
    return pos


def _indent_of(source: bytes, pos: int) -> bytes:
    start = _line_start(source, pos)
    out = b""
    for i in range(start, pos):
        ch = source[i:i + 1]
        if ch in (b" ", b"\t"):
            out += ch
        else:
            break
    return out
