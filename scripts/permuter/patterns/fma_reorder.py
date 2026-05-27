"""FMA reorder pattern — reorder fused multiply-add expressions.

The PowerPC compiler generates different FMA instructions depending on
expression structure:
    a + b*c  -> fmadds
    a - b*c  -> fnmsubs (or fmsubs)
    b*c + a  -> fmadds (different register allocation)
    b*c - a  -> fmsubs

Reordering the addend vs multiply can fix FMA opcode mismatches.

Also handles parenthesized expansion, which changes FMA selection by
altering which terms the compiler fuses:
    a - (b - c)  -> c - b + a    (fmsubs/fsubs -> fnmsubs/fadds)
    a - (b * c - d)  -> d - b * c + a
    a + (b - c)  -> a + b - c    (removes unnecessary parens)

This was proven to fix CalcSpline (96% -> 100%) and InterpTangent
(98.1% -> 99.6%).

Example:
    float r = 1.0f - x * y;
    ->
    float r = -(x * y) + 1.0f;
    // or: float r = x * y - 1.0f; (negate sense)

    float r = p3 - (p2 * 3.0f - p1x3m0);
    ->
    float r = p1x3m0 - p2 * 3.0f + p3;

C3 extension — operand commutation (multiply / flat-add):
    The compiler chooses an operand order for commutative float ops that our
    source's textual order does not always match. This surfaces in objdiff NOT
    as an opcode mismatch but as an FPR register-swap confined to a single
    instruction:
        fmuls  f0, f10, f0   (target)   vs   fmuls  f0, f0, f10   (ours)
        fmadds f9, f12, f6, f9 (target) vs   fmadds f9, f6, f12, f9 (ours)
        fadds  f0, f0, f13   (target)   vs   fadds  f0, f13, f0    (ours)
    The fix is to commute the operands of the multiply or the flat add:
        a * b  ->  b * a            (fmuls / fmadds multiplicand swap)
        a + b  ->  b + a            (fadds operand swap, both sides non-multiply)
    The pre-existing reorder/expansion machinery never touches the internal
    multiplicands or a plain add (it only swaps the +/- top-level operands when
    one side is a multiply), so these single-instruction FPR swaps went
    uncovered. Proven on Normalize(Plane)/Normalize(Quat) (one fmuls swap ->
    100%) and the Box::Volume `x*y*z` chain.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..types import Diagnosis, FunctionContext, Variant

_FMA_OPCODES = {"fmadds", "fmsubs", "fnmadds", "fnmsubs",
                "fmadd", "fmsub", "fnmadd", "fnmsub"}
_ADDSUB_OPCODES = {"fadds", "fsubs", "fadd", "fsub"}
# Multiply opcodes — pure commutation candidates (a*b -> b*a). Not in
# diff_ops (objdiff reports the operand difference as a diff_arg reg-swap,
# same opcode both sides), so relevance for these is driven by FPR swap pairs.
_FMUL_OPCODES = {"fmuls", "fmul"}

# Cap on commutation variants per function — keep the synthesis bounded and
# deterministic rather than a blind swap of every product in the body.
_MAX_COMMUTATION_VARIANTS = 4


class FmaReorderPattern(Pattern):
    name = "fma_reorder"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _FMA_OPCODES or d.base_opcode in _FMA_OPCODES:
                return True
            # Also relevant for fadds/fsubs mismatches (paren expansion changes these)
            pair = {d.target_opcode, d.base_opcode}
            if pair & _ADDSUB_OPCODES:
                return True
        # C3: commutation candidates appear as single-instruction FPR swaps,
        # not opcode diffs. A multiply/flat-add operand swap is the likely fix.
        if _has_commutation_swap(diagnosis):
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        for d in diagnosis.diff_ops:
            pair = {d.target_opcode, d.base_opcode}
            if len(pair & _FMA_OPCODES) == 2:
                return 0.9  # one FMA op replaced by another
            if pair & _FMA_OPCODES and pair & _ADDSUB_OPCODES:
                return 0.85  # FMA vs separate add/sub — paren expansion candidate
        # C3: single-instruction FPR swap with no opcode diff — operand
        # commutation. High confidence when it's the dominant remaining issue.
        if _has_commutation_swap(diagnosis):
            return 0.75
        return 0.6

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0

        # Try Ghidra-guided generation first — produces fewer, better variants
        ghidra_produced = False
        if ctx.ghidra_ast is not None:
            for variant in self._try_ghidra_guided(ctx, counter):
                yield variant
                counter += 1
                ghidra_produced = True
            if ghidra_produced:
                return  # Skip blind generation when guided produced candidates

        for stmt in ctx.statements:
            for binop in _find_fma_candidates(stmt):
                for variant in _generate_reorders(binop, ctx, counter):
                    yield variant
                    counter += 1
            for binop in _find_paren_sub_candidates(stmt):
                for variant in _generate_paren_expansions(binop, ctx, counter):
                    yield variant
                    counter += 1

        # C3: operand commutation (a*b -> b*a, flat a+b -> b+a). Bounded and
        # deterministic — one swap per commutable product/sum, capped overall.
        emitted = 0
        for stmt in ctx.statements:
            if emitted >= _MAX_COMMUTATION_VARIANTS:
                break
            for binop in _find_commutable_candidates(stmt):
                if emitted >= _MAX_COMMUTATION_VARIANTS:
                    break
                for variant in _generate_commutations(binop, ctx, counter):
                    yield variant
                    counter += 1
                    emitted += 1

    def _try_ghidra_guided(
        self, ctx: FunctionContext, start_counter: int
    ) -> Iterator[Variant]:
        """Generate expression variants guided by Ghidra's target structure.

        Compares arithmetic expression structure between our source and
        Ghidra's decompilation. When they differ structurally (e.g.
        parenthesized vs flat), generates only the variant that matches
        the target's structure.
        """
        import sys
        from ..ghidra_expr_match import compare_arithmetic_expressions, is_flat_vs_paren

        diffs = compare_arithmetic_expressions(
            ctx.statements, ctx.file_source, ctx.ghidra_ast
        )

        if not diffs:
            return

        counter = start_counter
        for diff in diffs:
            # For flat-vs-paren diffs, use the existing expansion machinery
            if is_flat_vs_paren(diff):
                # The source node has parenthesized structure — expand it
                src_node = diff.source_node
                for variant in _generate_paren_expansions(src_node, ctx, counter):
                    variant.name = f"ghidra_fma_{counter}"
                    variant.description = (
                        f"Ghidra-guided: {diff.source_structure} -> "
                        f"{diff.target_structure}"
                    )
                    yield variant
                    counter += 1
            else:
                # For other structural diffs (e.g. operand swap), try FMA reorders
                src_node = diff.source_node
                for variant in _generate_reorders(src_node, ctx, counter):
                    variant.name = f"ghidra_fma_{counter}"
                    variant.description = (
                        f"Ghidra-guided: {diff.source_structure} -> "
                        f"{diff.target_structure}"
                    )
                    yield variant
                    counter += 1

        if counter > start_counter:
            print(
                f"  Ghidra-guided FMA: {counter - start_counter} variant(s) "
                f"from {len(diffs)} structural diff(s)",
                file=sys.stderr,
            )


def _is_fpr(reg: str) -> bool:
    """True for a PowerPC floating-point register name (f0..f31)."""
    return len(reg) >= 2 and reg[0] == "f" and reg[1:].isdigit()


def _has_commutation_swap(diagnosis: Diagnosis) -> bool:
    """Detect the operand-commutation signature in a diagnosis.

    A commutative float-op operand swap (a*b vs b*a, a+b vs b+a) does NOT
    change the opcode, so objdiff reports it as a `diff_arg` register swap —
    it lands in `reg_swap_pairs`, never in `diff_ops`. The distinguishing
    signature versus a callee-saved variable regswap is:
      * the swapped registers are FPRs (f-prefixed), and
      * the swap is confined to a single instruction (first_idx == last_idx).
    A callee-saved allocation swap instead spans many instructions across the
    function body and is GPR-based; those belong to declaration_reorder.
    """
    for (r0, r1), info in diagnosis.reg_swap_pairs.items():
        if _is_fpr(r0) and _is_fpr(r1) and info.first_idx == info.last_idx:
            return True
    return False


def _find_commutable_candidates(node: Node) -> Iterator[Node]:
    """Find commutative binary_expression nodes worth swapping operands of.

    Two shapes, complementary to _find_fma_candidates (which only handles a
    +/- where one side is already a multiply):
      1. A multiply `a * b`            -> swap to `b * a`  (fmuls / fmadds)
      2. A flat add `a + b` with NEITHER side a multiply -> `b + a` (fadds)

    Plain adds where one side IS a multiply are intentionally left to
    _find_fma_candidates / _generate_reorders so we do not emit duplicates.
    """
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        left = node.child_by_field_name("left")
        right = node.child_by_field_name("right")
        if op is not None and left is not None and right is not None:
            if op.text == b"*":
                yield node
            elif op.text == b"+":
                left_is_mul = (left.type == "binary_expression"
                               and _has_op(left, b"*"))
                right_is_mul = (right.type == "binary_expression"
                                and _has_op(right, b"*"))
                if not left_is_mul and not right_is_mul:
                    yield node

    for child in node.children:
        yield from _find_commutable_candidates(child)


def _parenthesize_if_binary(node: Node, text: bytes) -> bytes:
    """Wrap an operand in parens iff it is an un-parenthesized binary expr.

    Keeps a commuted operand's internal grouping intact so the rewrite stays a
    pure commutation. Already-parenthesized operands and atoms pass through.
    """
    if node.type == "binary_expression":
        return b"(" + text + b")"
    return text


def _generate_commutations(
    binop: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Emit the operand-commuted variant of a multiply or flat add.

    Deterministic: exactly one variant per commutable node — swap left/right
    around the operator. No-op swaps (identical operand text) are skipped.
    """
    source = ctx.file_source
    op_node = binop.child_by_field_name("operator")
    left = binop.child_by_field_name("left")
    right = binop.child_by_field_name("right")
    if op_node is None or left is None or right is None:
        return

    left_text = source[left.start_byte:left.end_byte]
    right_text = source[right.start_byte:right.end_byte]
    if left_text == right_text:
        return  # swapping a*a / a+a changes nothing

    # Preserve grouping when an operand is itself a binary expression, so the
    # rewrite is a pure commutation (a OP b -> b OP a) and never a silent
    # re-association. `(x * y) * z` must become `z * (x * y)`, not `z * x * y`
    # (which would regroup as `(z * x) * y`).
    op_text = op_node.text
    new_left = _parenthesize_if_binary(left, left_text)
    new_right = _parenthesize_if_binary(right, right_text)
    kind = "multiply" if op_text == b"*" else "add"
    new_source = (
        source[:left.start_byte]
        + new_right
        + source[left.end_byte:right.start_byte]
        + new_left
        + source[right.end_byte:]
    )
    yield Variant(
        name=f"fma_{counter}",
        pattern_name="fma_reorder",
        description=(
            f"Commute {kind} operands: "
            f"{left_text.decode('utf-8', errors='replace')[:20]} "
            f"{op_text.decode()} "
            f"{right_text.decode('utf-8', errors='replace')[:20]} -> swapped"
        ),
        source=new_source,
    )

    # Reassociate a left-leaning multiply chain: (a*b)*c -> a*(b*c). Unlike
    # commutation, the compiler does NOT normalize associativity (it changes
    # which product is fused and the rounding order), so this is the lever that
    # can actually move fmuls/fmadds chains. Bounded to this one regrouping.
    if (op_text == b"*" and left.type == "binary_expression"
            and _has_op(left, b"*")):
        inner_l = left.child_by_field_name("left")
        inner_r = left.child_by_field_name("right")
        if inner_l is not None and inner_r is not None:
            a_text = source[inner_l.start_byte:inner_l.end_byte]
            b_text = source[inner_r.start_byte:inner_r.end_byte]
            reassoc = a_text + b" * (" + b_text + b" * " + right_text + b")"
            reassoc_source = (
                source[:left.start_byte]
                + reassoc
                + source[binop.end_byte:]
            )
            yield Variant(
                name=f"fma_{counter}r",
                pattern_name="fma_reorder",
                description=(
                    "Reassociate multiply chain: (a*b)*c -> a*(b*c) "
                    f"[{a_text.decode('utf-8', errors='replace')[:12]}...]"
                ),
                source=reassoc_source,
            )


def _find_fma_candidates(node: Node) -> Iterator[Node]:
    """Find binary_expression nodes that look like FMA patterns.

    An FMA candidate is a +/- expression where one operand is a * expression.
    """
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op and op.text in (b"+", b"-"):
            left = node.child_by_field_name("left")
            right = node.child_by_field_name("right")
            if left and right:
                left_is_mul = (left.type == "binary_expression" and
                               _has_op(left, b"*"))
                right_is_mul = (right.type == "binary_expression" and
                                _has_op(right, b"*"))
                # At least one side must be a multiply
                if left_is_mul or right_is_mul:
                    yield node

    for child in node.children:
        yield from _find_fma_candidates(child)


def _has_op(node: Node, op: bytes) -> bool:
    """Check if a binary_expression has the given operator."""
    op_node = node.child_by_field_name("operator")
    return op_node is not None and op_node.text == op


def _generate_reorders(
    binop: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate FMA expression reorderings."""
    source = ctx.file_source
    op_node = binop.child_by_field_name("operator")
    left = binop.child_by_field_name("left")
    right = binop.child_by_field_name("right")
    if op_node is None or left is None or right is None:
        return

    op_text = op_node.text
    left_text = source[left.start_byte:left.end_byte]
    right_text = source[right.start_byte:right.end_byte]

    if op_text == b"+":
        # a + b*c -> b*c + a (swap operands of addition)
        new_source = (
            source[:left.start_byte]
            + right_text
            + source[left.end_byte:right.start_byte]
            + left_text
            + source[right.end_byte:]
        )
        yield Variant(
            name=f"fma_{counter}",
            pattern_name="fma_reorder",
            description="Swap addition operands (FMA reorder)",
            source=new_source,
        )

    elif op_text == b"-":
        # a - b*c -> -(b*c) + a  or  -(b*c - a)
        # Try: swap to b*c - a (negate sense)
        # This only works if we also negate, but the compiler may handle it

        # Variant 1: swap operands of subtraction
        new_source = (
            source[:left.start_byte]
            + right_text
            + source[left.end_byte:right.start_byte]
            + left_text
            + source[right.end_byte:]
        )
        yield Variant(
            name=f"fma_{counter}",
            pattern_name="fma_reorder",
            description="Swap subtraction operands (FMA reorder)",
            source=new_source,
        )
        counter += 1

        # Variant 2: negate and rewrite  a - b*c -> -(b*c - a)
        # Wrap the whole expression in negation with swapped operands
        new_expr = b"-(" + right_text + b" - " + left_text + b")"
        new_source = (
            source[:binop.start_byte]
            + new_expr
            + source[binop.end_byte:]
        )
        yield Variant(
            name=f"fma_{counter}",
            pattern_name="fma_reorder",
            description="Negate FMA: a - b*c -> -(b*c - a)",
            source=new_source,
        )


def _find_paren_sub_candidates(node: Node) -> Iterator[Node]:
    """Find a ± (b ± c) patterns where the right operand is parenthesized
    and contains an addition or subtraction.

    Candidates for algebraic expansion:
        a - (b - c)  ->  c - b + a    (proven: CalcSpline, InterpTangent)
        a - (b + c)  ->  a - b - c
        a + (b - c)  ->  a + b - c    (71 instances in codebase)
    Removing/changing parentheses alters FMA fusion decisions.
    """
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op and op.text in (b"-", b"+"):
            right = node.child_by_field_name("right")
            if right is not None:
                # Unwrap parenthesized_expression
                inner = right
                if inner.type == "parenthesized_expression" and inner.named_children:
                    inner = inner.named_children[0]
                # Check if inner contains +/- (worth expanding)
                if inner.type == "binary_expression":
                    inner_op = inner.child_by_field_name("operator")
                    if inner_op and inner_op.text in (b"-", b"+"):
                        yield node

    for child in node.children:
        yield from _find_paren_sub_candidates(child)


def _collect_terms(node: Node, source: bytes, negate: bool = False) -> list[tuple[bytes, bool]]:
    """Flatten a chain of +/- into (term_text, is_negated) pairs.

    For `a - b + c`, returns [(a, False), (b, True), (c, False)].
    The `negate` flag flips all signs (used when distributing a leading minus).
    """
    if node.type == "parenthesized_expression" and node.named_children:
        return _collect_terms(node.named_children[0], source, negate)

    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        left = node.child_by_field_name("left")
        right = node.child_by_field_name("right")
        if op and left and right and op.text in (b"+", b"-"):
            left_terms = _collect_terms(left, source, negate)
            right_negate = negate if op.text == b"+" else (not negate)
            right_terms = _collect_terms(right, source, right_negate)
            return left_terms + right_terms

    text = source[node.start_byte:node.end_byte]
    return [(text, negate)]


def _terms_to_expr(terms: list[tuple[bytes, bool]]) -> bytes:
    """Reassemble (term_text, is_negated) pairs into an expression string."""
    if not terms:
        return b"0"
    parts = []
    for i, (text, neg) in enumerate(terms):
        if i == 0:
            if neg:
                parts.append(b"-" + text)
            else:
                parts.append(text)
        else:
            if neg:
                parts.append(b" - " + text)
            else:
                parts.append(b" + " + text)
    return b"".join(parts)


def _generate_paren_expansions(
    binop: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate algebraic expansions of a - (b - c) patterns.

    Proven fix: a - (b - c) -> c - b + a
    Changes FMA selection from fmsubs/fsubs to fnmsubs/fadds.
    """
    source = ctx.file_source
    op_node = binop.child_by_field_name("operator")
    left = binop.child_by_field_name("left")
    right = binop.child_by_field_name("right")
    if op_node is None or left is None or right is None:
        return

    # Collect terms: left contributes positively, right depends on outer operator
    right_negated = op_node.text == b"-"
    left_terms = _collect_terms(left, source, negate=False)
    right_terms = _collect_terms(right, source, negate=right_negated)
    all_terms = left_terms + right_terms

    if len(all_terms) < 2:
        return

    # Variant 1: reverse order — c - b + a (the proven fix pattern)
    reversed_terms = list(reversed(all_terms))
    reversed_expr = _terms_to_expr(reversed_terms)
    original_expr = source[binop.start_byte:binop.end_byte]
    if reversed_expr != original_expr:
        new_source = (
            source[:binop.start_byte]
            + reversed_expr
            + source[binop.end_byte:]
        )
        yield Variant(
            name=f"fma_{counter}",
            pattern_name="fma_reorder",
            description="Expand paren subtraction (reversed): "
                        f"{original_expr.decode('utf-8', errors='replace')[:40]} -> "
                        f"{reversed_expr.decode('utf-8', errors='replace')[:40]}",
            source=new_source,
        )
        counter += 1

    # Variant 2: flat expansion in original order — a - b + c
    flat_expr = _terms_to_expr(all_terms)
    if flat_expr != original_expr and flat_expr != reversed_expr:
        new_source = (
            source[:binop.start_byte]
            + flat_expr
            + source[binop.end_byte:]
        )
        yield Variant(
            name=f"fma_{counter}",
            pattern_name="fma_reorder",
            description="Expand paren subtraction (flat): "
                        f"{original_expr.decode('utf-8', errors='replace')[:40]} -> "
                        f"{flat_expr.decode('utf-8', errors='replace')[:40]}",
            source=new_source,
        )
