"""Hoist / permute float operands feeding an FPR-allocation cascade.

On PowerPC a near-100% function frequently diverges only by a floating-point
register (f0-f31) ALLOCATION rotation across an ``lfs / fmuls / fmadds / stfs``
cascade. objdiff reports this as a MULTI-instruction FPR/FPR ``reg_swap`` pair
(``first_idx != last_idx``) — i.e. the same two float registers trade places
across several instructions, with the opcodes otherwise identical. The
permuter's ``is_all_noise()`` gate classifies that class as an unfixable
allocation artifact and short-circuits before any pattern runs.

Two hand-found wins this session prove the class is sometimes fixable by
reshaping *how the float operands enter the cascade*:

* RB3 ``Rot::RotateAboutX`` 99.37 -> 99.8%: hoist the repeated member loads
  ``float xz = min.x.z; float xy = min.x.y;`` to BEFORE the consuming row so
  mwcc evaluates that product first and lands it in the target's f-register.
* RB3 ``Geo::Intersect(Segment,...)`` 99.34 -> 99.6%: pre-negate the plane
  components into the backing locals (``float nd = -n->plane.d;``) and store
  the negated locals directly, so the four ``fneg`` ops schedule d,c,b,a.

This pattern generates BOTH families (capped at ``_MAX_VARIANTS`` total):

* **Family A (hoist):** find 2+ consecutive sibling assignment statements
  whose float-arithmetic RHS share repeated float operand sub-expressions
  (member-load chains like ``min.x.z`` / ``n->plane.d`` / ``a[i].z``). For
  each repeated operand emit ``float <tmp> = <operand>;`` before the FIRST
  consuming statement and replace its occurrences with ``<tmp>``. Also emit a
  small set of orderings of the hoisted decls (identity + reverse).
* **Family B (negate-then-store):** when adjacent stores are
  ``dst = -<float local>;`` backed by existing ``float n = <load>;`` decls,
  fold the negation into the decl (``float n = -<load>;``) and drop it from the
  store (``dst = n;``). Also emit the store-order permutation.

How this differs from neighbouring patterns:

* ``fma_reorder`` handles the SINGLE-instruction commutative operand swap
  (``a*b`` vs ``b*a``); this pattern handles exactly the MULTI-instruction
  class fma_reorder excludes, so they never overlap.
* ``variable_extraction`` extracts *call expressions* into temps; this pattern
  extracts raw float *member/subscript loads* — never calls.
* ``loop_var_hoist`` moves whole EXISTING declarations in/out of loops; this
  pattern SYNTHESISES new float temps from repeated operand expressions and
  folds negation into existing decls.

Idempotence: an operand already bound to a ``float`` local with the identical
expression is skipped, so a re-run on already-hoisted source yields nothing.
"""

from __future__ import annotations

import re
from typing import Iterator, Optional

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent, get_line_start, walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Total variant cap across BOTH families.
_MAX_VARIANTS = 6

# A single float operand "load" we are willing to hoist is a member-access
# chain whose OUTERMOST node is a ``field_expression`` — ``min.x.z`` /
# ``n->plane.d`` (the RB3 win shapes) and also ``a[i].z`` (a field_expression
# whose argument is a subscript). We deliberately do NOT hoist a bare
# ``subscript_expression`` like ``a[1]``: without type info a bare scalar
# subscript is just as likely an integer index (it would fire on integer
# cascades), and the proven wins are all member-load chains. Plain identifiers
# are never hoisted (no codegen benefit, shadowing risk); call expressions are
# variable_extraction's domain.
_OPERAND_LEAF_TYPES = {"field_expression"}

# Float arithmetic operators whose RHS we treat as a cascade contributor.
_FLOAT_ARITH_OPS = {"+", "-", "*", "/"}


def _is_fpr(reg: str) -> bool:
    """True when *reg* names a PowerPC floating-point register (f0-f31)."""
    if not reg or reg[0] not in ("f", "F"):
        return False
    return reg[1:].isdigit()


class FprCascadeOperandHoistPattern(Pattern):
    name = "fpr_cascade_operand_hoist"
    safety_tier = "conservative"
    structural_domain = "data_flow"
    follow_ups = ("assignment_reorder", "fma_reorder", "declaration_reorder")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        """True iff there's a MULTI-instruction FPR/FPR reg_swap pair.

        This is exactly the class fma_reorder EXCLUDES (it handles the
        single-instruction commutation), so there's no overlap.
        """
        for (r0, r1), info in diagnosis.reg_swap_pairs.items():
            if _is_fpr(r0) and _is_fpr(r1) and info.first_idx != info.last_idx:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        # Below fma_reorder's 0.75 — this is the rarer, multi-instruction case.
        return 0.55 if self.relevant(diagnosis) else 0.0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        emitted: set[bytes] = set()

        for variant in self._generate_family_a(ctx, counter, emitted):
            yield variant
            counter += 1
            if counter >= _MAX_VARIANTS:
                return

        for variant in self._generate_family_b(ctx, counter, emitted):
            yield variant
            counter += 1
            if counter >= _MAX_VARIANTS:
                return

    # -- Family A: hoist repeated float operand loads -----------------------

    def _generate_family_a(
        self, ctx: FunctionContext, start_counter: int, emitted: set[bytes]
    ) -> Iterator[Variant]:
        source = ctx.file_source
        counter = start_counter

        for compound in _compound_statements(ctx.body_node):
            runs = _find_float_assignment_runs(compound, source)
            for run in runs:
                # Region filter: skip runs entirely outside mismatch regions.
                if not ctx.node_in_mismatch_region(run[0]):
                    continue

                repeated = _repeated_float_operands(run, source)
                if not repeated:
                    continue

                # Drop operands already bound to a float local with same expr
                # (idempotence) — see _already_hoisted.
                repeated = [
                    (expr, occ)
                    for (expr, occ) in repeated
                    if not _already_hoisted(ctx.body_node, expr, source)
                ]
                if not repeated:
                    continue

                first_stmt = run[0]
                indent = get_indent(source, first_stmt)
                line_start = get_line_start(source, first_stmt)

                # Assign a unique temp name per repeated operand.
                used_names: set[str] = set()
                naming: list[tuple[bytes, list[Node], bytes]] = []
                for expr_bytes, occurrences in repeated:
                    tmp = _unique_tmp_name(source, used_names).encode("utf-8")
                    used_names.add(tmp.decode("utf-8"))
                    naming.append((expr_bytes, occurrences, tmp))

                # Two orderings of the hoisted decls: identity and reverse.
                orderings = [("ident", list(naming))]
                if len(naming) >= 2:
                    orderings.append(("rev", list(reversed(naming))))

                for tag, ordered in orderings:
                    decl_block = b"".join(
                        indent + b"float " + tmp + b" = " + expr_bytes + b";\n"
                        for (expr_bytes, _occ, tmp) in ordered
                    )

                    ed = SourceEditor(source)
                    ed.insert_at(line_start, decl_block)
                    for (_expr, occurrences, tmp) in naming:
                        for occ in occurrences:
                            ed.replace_node(occ, tmp)
                    try:
                        new_source = ed.apply()
                    except ValueError:
                        continue
                    if new_source in emitted or new_source == source:
                        continue
                    emitted.add(new_source)

                    names = b", ".join(tmp for (_e, _o, tmp) in ordered)
                    yield Variant(
                        name=f"fprhoist_{counter}_{tag}",
                        pattern_name=self.name,
                        description=(
                            "Hoist repeated float operands ["
                            + names.decode() + f"] before assignment run ({tag})"
                        ),
                        source=new_source,
                        tags=frozenset({"introduced_temp", "fpr_cascade"}),
                    )
                    counter += 1
                    if counter >= _MAX_VARIANTS:
                        return

    # -- Family B: fold negation into backing float decls -------------------

    def _generate_family_b(
        self, ctx: FunctionContext, start_counter: int, emitted: set[bytes]
    ) -> Iterator[Variant]:
        source = ctx.file_source
        counter = start_counter

        for compound in _compound_statements(ctx.body_node):
            groups = _find_negate_store_groups(compound, source)
            for decls, stores in groups:
                if not ctx.node_in_mismatch_region(stores[0]):
                    continue

                # Idempotence: if the backing decls already carry the negation
                # (``float n = -<load>;``) there's nothing to fold.
                if all(_decl_init_is_negated(d) for d, _store in decls.values()):
                    continue

                # Identity store order and the reversed store order.
                orderings = [("ident", list(stores))]
                if len(stores) >= 2:
                    orderings.append(("rev", list(reversed(stores))))

                for tag, ordered_stores in orderings:
                    variant = _build_family_b_variant(
                        source, decls, stores, ordered_stores
                    )
                    if variant is None:
                        continue
                    new_source = variant
                    if new_source in emitted or new_source == source:
                        continue
                    emitted.add(new_source)
                    yield Variant(
                        name=f"fprnegfold_{counter}_{tag}",
                        pattern_name=self.name,
                        description=(
                            "Fold negation into backing float decls; "
                            f"store negated locals ({tag})"
                        ),
                        source=new_source,
                        tags=frozenset({"fpr_cascade", "negate_fold"}),
                    )
                    counter += 1
                    if counter >= _MAX_VARIANTS:
                        return


# ---------------------------------------------------------------------------
# Lightweight AST detector (used by the is_all_noise gate)
# ---------------------------------------------------------------------------

def has_fpr_cascade_hoist_candidate(ctx_or_source) -> bool:
    """Cheap source-shape check: is this function a hoist/negate-fold candidate?

    Accepts either a :class:`FunctionContext` (preferred — uses the parsed
    ``body_node``) or raw ``bytes``/``str`` source (parses a throwaway tree).
    Used by ``is_all_noise(..., fpr_cascade_candidate=...)`` callers to decide
    whether to unlock the multi-instruction FPR-swap fall-through. Intentionally
    conservative: returns True only when there is a repeated float-operand run
    (Family A) or a negate-then-store group (Family B).
    """
    body = _resolve_body_node(ctx_or_source)
    if body is None:
        return False
    source = _resolve_source_bytes(ctx_or_source, body)

    for compound in _compound_statements(body):
        for run in _find_float_assignment_runs(compound, source):
            repeated = _repeated_float_operands(run, source)
            repeated = [
                (e, occ)
                for (e, occ) in repeated
                if not _already_hoisted(body, e, source)
            ]
            if repeated:
                return True
        for decls, _stores in _find_negate_store_groups(compound, source):
            if not all(_decl_init_is_negated(d) for d, _s in decls.values()):
                return True
    return False


def _resolve_body_node(ctx_or_source) -> Optional[Node]:
    body = getattr(ctx_or_source, "body_node", None)
    if body is not None:
        return body
    # Raw source: parse a throwaway tree and grab the first function body.
    if isinstance(ctx_or_source, (bytes, str)):
        from ..extractor import _PARSER
        src = ctx_or_source.encode("utf-8") if isinstance(ctx_or_source, str) \
            else ctx_or_source
        tree = _PARSER.parse(src)
        for node in walk(tree.root_node):
            if node.type == "function_definition":
                b = node.child_by_field_name("body")
                if b is not None:
                    return b
    return None


def _resolve_source_bytes(ctx_or_source, body: Node) -> bytes:
    src = getattr(ctx_or_source, "file_source", None)
    if isinstance(src, bytes):
        return src
    if isinstance(ctx_or_source, bytes):
        return ctx_or_source
    if isinstance(ctx_or_source, str):
        return ctx_or_source.encode("utf-8")
    # Fall back to the body's own text (root-relative offsets still valid).
    return body.text if body.text is not None else b""


# ---------------------------------------------------------------------------
# Shared AST helpers
# ---------------------------------------------------------------------------

def _compound_statements(body_node: Node) -> Iterator[Node]:
    """Yield the function body plus every nested compound_statement."""
    for node in walk(body_node):
        if node.type == "compound_statement":
            yield node


def _find_float_assignment_runs(
    compound: Node, source: bytes
) -> list[list[Node]]:
    """Find runs of 2+ consecutive float-arithmetic assignment statements.

    A qualifying statement is ``LHS = <float arithmetic expr>;`` whose RHS is a
    binary_expression over ``+ - * /`` (the cascade contributors). Runs are
    contiguous direct children of *compound*.
    """
    runs: list[list[Node]] = []
    children = list(compound.named_children)
    i = 0
    while i < len(children):
        if not _is_float_arith_assignment(children[i]):
            i += 1
            continue
        run = [children[i]]
        j = i + 1
        while j < len(children) and _is_float_arith_assignment(children[j]):
            run.append(children[j])
            j += 1
        if len(run) >= 2:
            runs.append(run)
        i = max(j, i + 1)
    return runs


def _is_float_arith_assignment(stmt: Node) -> bool:
    assign = _assignment_in(stmt)
    if assign is None:
        return False
    op = assign.child_by_field_name("operator")
    if op is None or op.text != b"=":
        return False
    right = assign.child_by_field_name("right")
    return right is not None and _contains_float_arith(right)


def _assignment_in(stmt: Node) -> Optional[Node]:
    if stmt.type != "expression_statement":
        return None
    for child in stmt.named_children:
        if child.type == "assignment_expression":
            return child
    return None


def _contains_float_arith(node: Node) -> bool:
    """True if *node* contains a binary_expression over + - * /."""
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op is not None and op.text is not None:
            if op.text.decode("utf-8", "replace") in _FLOAT_ARITH_OPS:
                return True
    for child in node.children:
        if _contains_float_arith(child):
            return True
    return False


def _repeated_float_operands(
    run: list[Node], source: bytes
) -> list[tuple[bytes, list[Node]]]:
    """Return repeated float operand loads across the RHS of a run.

    A float operand load is a ``field_expression`` / ``subscript_expression``
    node (member-load chain). An operand qualifies when its source text appears
    2+ times across the run's RHS expressions (so hoisting it into one local
    changes how mwcc schedules the shared load into an FPR).

    Returns ``[(operand_text, [occurrence_nodes...]), ...]`` ordered by first
    appearance, longest-text-first within a statement so nested chains hoist
    before their prefixes.
    """
    occurrences: dict[bytes, list[Node]] = {}
    order: list[bytes] = []
    for stmt in run:
        assign = _assignment_in(stmt)
        if assign is None:
            continue
        right = assign.child_by_field_name("right")
        if right is None:
            continue
        for leaf in _operand_loads(right):
            text = source[leaf.start_byte:leaf.end_byte]
            if text not in occurrences:
                occurrences[text] = []
                order.append(text)
            occurrences[text].append(leaf)

    repeated = [
        (text, occurrences[text]) for text in order if len(occurrences[text]) >= 2
    ]
    return repeated


def _operand_loads(node: Node) -> Iterator[Node]:
    """Yield top-level float operand load nodes within an expression.

    Stops descending once it yields a load (we hoist the whole chain, not its
    pieces). Does not descend into call_expression arguments — those are
    variable_extraction's domain.
    """
    if node.type in _OPERAND_LEAF_TYPES:
        yield node
        return
    if node.type == "call_expression":
        return
    for child in node.children:
        yield from _operand_loads(child)


def _already_hoisted(body_node: Node, expr: bytes, source: bytes) -> bool:
    """True if *expr* is already bound to a ``float`` local with the same expr.

    Recognises ``float NAME = <expr>;`` and ``float NAME = -<expr>;`` so a
    re-run on already-hoisted (or already negate-folded) source is a no-op.
    """
    for node in walk(body_node):
        if node.type != "declaration":
            continue
        if not _decl_is_float(node, source):
            continue
        for init in _declarator_inits(node):
            init_text = source[init.start_byte:init.end_byte].strip()
            if init_text == expr or init_text == b"-" + expr:
                return True
    return False


def _decl_is_float(decl: Node, source: bytes) -> bool:
    type_node = decl.child_by_field_name("type")
    if type_node is None:
        return False
    return source[type_node.start_byte:type_node.end_byte].strip() == b"float"


def _declarator_inits(decl: Node) -> Iterator[Node]:
    """Yield the initializer value node of each init_declarator in *decl*."""
    for child in decl.named_children:
        if child.type == "init_declarator":
            value = child.child_by_field_name("value")
            if value is not None:
                yield value


def _unique_tmp_name(source: bytes, used: set[str]) -> str:
    """Return a ``_fprN`` name not present in *source* or *used*."""
    text = source.decode("utf-8", errors="replace")
    n = 0
    while True:
        candidate = f"_fpr{n}"
        if candidate not in used and not re.search(
            rf"\b{re.escape(candidate)}\b", text
        ):
            return candidate
        n += 1


# ---------------------------------------------------------------------------
# Family B helpers (negate-then-store)
# ---------------------------------------------------------------------------

def _find_negate_store_groups(
    compound: Node, source: bytes
) -> list[tuple[dict[bytes, tuple[Node, Node]], list[Node]]]:
    """Find ``float n = <load>;`` decls followed by ``dst = -n;`` store runs.

    Returns ``[(decls, stores), ...]`` where ``decls`` maps the negated local
    name -> (declaration_node, init_declarator_node) and ``stores`` is the run
    of ``dst = -<local>;`` statements (each negating a distinct backing local).
    """
    children = list(compound.named_children)
    groups = []

    # First pass: collect float decls keyed by name -> (decl_node, init_node).
    decl_map: dict[bytes, tuple[Node, Node]] = {}
    for child in children:
        if child.type != "declaration" or not _decl_is_float(child, source):
            continue
        for idecl in child.named_children:
            if idecl.type != "init_declarator":
                continue
            name = idecl.child_by_field_name("declarator")
            if name is None:
                continue
            decl_map[source[name.start_byte:name.end_byte]] = (child, idecl)

    if not decl_map:
        return groups

    # Second pass: find runs of ``dst = -<known float local>;`` stores.
    i = 0
    while i < len(children):
        store_run: list[Node] = []
        used_decls: dict[bytes, tuple[Node, Node]] = {}
        j = i
        while j < len(children):
            local = _negated_local_store(children[j], source)
            if local is None or local not in decl_map:
                break
            if local in used_decls:  # don't negate the same local twice
                break
            used_decls[local] = decl_map[local]
            store_run.append(children[j])
            j += 1
        if len(store_run) >= 2:
            groups.append((used_decls, store_run))
        i = max(j, i + 1)

    return groups


def _negated_local_store(stmt: Node, source: bytes) -> Optional[bytes]:
    """If *stmt* is ``dst = -<identifier>;`` return the identifier bytes."""
    assign = _assignment_in(stmt)
    if assign is None:
        return None
    op = assign.child_by_field_name("operator")
    if op is None or op.text != b"=":
        return None
    right = assign.child_by_field_name("right")
    if right is None or right.type != "unary_expression":
        return None
    uop = right.child_by_field_name("operator")
    if uop is None or uop.text != b"-":
        return None
    arg = right.child_by_field_name("argument")
    if arg is None or arg.type != "identifier":
        return None
    return source[arg.start_byte:arg.end_byte]


def _decl_init_is_negated(init_decl: Node) -> bool:
    """True if an init_declarator's value is a unary ``-`` (already folded)."""
    value = init_decl.child_by_field_name("value")
    if value is None or value.type != "unary_expression":
        return False
    uop = value.child_by_field_name("operator")
    return uop is not None and uop.text == b"-"


def _build_family_b_variant(
    source: bytes,
    decls: dict[bytes, tuple[Node, Node]],
    stores: list[Node],
    ordered_stores: list[Node],
) -> Optional[bytes]:
    """Produce the negate-folded variant source, or None if not buildable.

    For each backing decl ``float n = E;`` rewrite to ``float n = -E;`` and
    rewrite each store ``dst = -n;`` to ``dst = n;`` (placed in
    ``ordered_stores`` order at the original store-line positions).
    """
    ed = SourceEditor(source)

    # Fold negation into each backing decl's initializer.
    for name, (_decl, idecl) in decls.items():
        value = idecl.child_by_field_name("value")
        if value is None:
            return None
        if _decl_init_is_negated(idecl):
            continue  # already folded — leave as-is
        new_init = b"-" + source[value.start_byte:value.end_byte]
        ed.replace_node(value, new_init)

    # Rewrite the store lines: drop the negation, write the (possibly
    # reordered) sequence back into the original line positions.
    store_lines = []
    ranges = []
    for stmt in stores:
        start = get_line_start(source, stmt)
        end = _line_end(source, stmt.end_byte)
        ranges.append((start, end))
    for stmt in ordered_stores:
        local = _negated_local_store(stmt, source)
        if local is None:
            return None
        assign = _assignment_in(stmt)
        left = assign.child_by_field_name("left")
        indent = get_indent(source, stmt)
        dst = source[left.start_byte:left.end_byte]
        store_lines.append(indent + dst + b" = " + local + b";\n")

    for (start, end), line in zip(ranges, store_lines):
        ed.replace_range(start, end, line)

    try:
        return ed.apply()
    except ValueError:
        return None


def _line_end(source: bytes, pos: int) -> int:
    while pos < len(source) and source[pos:pos + 1] not in (b"\n", b"\r"):
        pos += 1
    if pos < len(source):
        pos += 1
    return pos
