"""Constraint-directed synthesis — use Ghidra + objdiff to deterministically derive source edits.

Instead of blind pattern search (100+ variants), this extracts constraints from Ghidra
decompilation and objdiff diagnosis, resolves them to deterministic edits, and enumerates
only the remaining free variables (typically 0-3 sign choices) for a total of 1-8 variants.

Architecture:
    Phase 1: extract_constraints()     — Ghidra + objdiff → ConstraintSet
    Phase 2: resolve_to_edits()        — deterministic constraints → source edits
    Phase 3: enumerate_free_variables() — unresolved dims → small cross-product
    Phase 4: synthesize()              — compose edits, produce 1-8 Variant objects
"""

from __future__ import annotations

import sys
from itertools import product

from .editor import SourceEditor
from .ghidra_ast import (
    GhidraAST,
    extract_condition_structure,
    extract_savefpr_count,
    extract_savegpr_count,
    extract_variable_first_use_order,
)
from .ghidra_preflight import run_preflight
from .types import (
    ConstraintSet,
    Diagnosis,
    FunctionContext,
    ResolvedEdit,
    SynthesisResult,
    Variant,
)


def extract_constraints(ctx: FunctionContext) -> ConstraintSet:
    """Phase 1: Extract constraints from Ghidra decompilation and objdiff diagnosis.

    Combines multiple extractors into a unified ConstraintSet.
    """
    cs = ConstraintSet()

    # ------------------------------------------------------------------
    # Ghidra + m2c combined constraints.
    #
    # Ghidra and m2c are two INDEPENDENT decompilations of the same target. We
    # used to treat m2c purely as a fallback (only when Ghidra was absent). But
    # because they are independent views, their *agreement* is a strong signal
    # and their *disagreement* gives two hypotheses both worth trying. So we
    # extract from both and COMBINE rather than fall back.
    # ------------------------------------------------------------------
    if ctx.ghidra_ast is not None:
        cs.ghidra_available = True

    # --- 1. Variable first-use order (for declaration reorder) ---
    # Pull the order from each decompiler that is present, then run them through
    # combine_var_orders to get an agree/disagree/single verdict.
    from .ghidra_var_match import combine_var_orders

    ghidra_var_order = None
    if ctx.ghidra_ast is not None:
        gvo = extract_variable_first_use_order(ctx.ghidra_ast)
        if gvo:
            ghidra_var_order = gvo

    m2c_var_order = None
    if ctx.m2c_code:
        from .m2c import extract_variable_first_use_order_from_text
        mvo = extract_variable_first_use_order_from_text(ctx.m2c_code)
        if mvo:
            m2c_var_order = mvo

    consensus = combine_var_orders(ghidra_var_order, m2c_var_order)
    if consensus.orders:
        # decl_order carries the names of the *preferred* order (first hypothesis).
        # _resolve_decl_order re-derives the full VarInfo orders itself and, on a
        # "disagree" verdict, tries both — see that helper.
        cs.decl_order = [v.name for v in consensus.orders[0]]
        cs.decl_order_verdict = consensus.verdict
        cs.decl_order_high_confidence = consensus.high_confidence

    # --- 2. Control flow structure (conjunction / guard / nested_if) ---
    # Combine guard-shape tags from both decompilers. Tags present in BOTH are
    # high-confidence; tags from either alone are still emitted (additive — never
    # narrows the search). cf_high_confidence is set when the two agree on the
    # full tag set (and at least one tag exists).
    ghidra_cf_tags: list[str] = []
    if ctx.ghidra_ast is not None:
        ghidra_cf_tags = extract_condition_structure(ctx.ghidra_ast)

    m2c_cf_tags: list[str] = []
    if ctx.m2c_code:
        from .m2c import extract_condition_structure_from_text
        m2c_cf_tags = extract_condition_structure_from_text(ctx.m2c_code)

    combined_cf_tags = _combine_cf_tags(ghidra_cf_tags, m2c_cf_tags)
    for i, tag in enumerate(combined_cf_tags):
        cs.cf_directions[i] = tag
    if (ghidra_cf_tags and m2c_cf_tags
            and set(ghidra_cf_tags) == set(m2c_cf_tags)):
        cs.cf_high_confidence = True

    # --- 3. Prologue save counts (Ghidra-only; m2c text lacks __savegprlr_N) ---
    if ctx.ghidra_ast is not None and ctx.ghidra_code:
        cs.target_gpr_saves = extract_savegpr_count(ctx.ghidra_code)
        cs.target_fpr_saves = extract_savefpr_count(ctx.ghidra_code)

    # Diagnosis-based constraints
    if ctx.diagnosis is not None:
        cs.diagnosis_available = True
        diag = ctx.diagnosis

        # 4. Base prologue saves
        cs.base_gpr_saves = diag.base_gpr_saves
        cs.base_fpr_saves = diag.base_fpr_saves

        # 5. Register swap pairs
        cs.swap_pairs = list(diag.reg_swap_pairs.keys()) if diag.reg_swap_pairs else []

        # 6. Sign choices from cmpw/cmplw diff_ops
        for i, dop in enumerate(diag.diff_ops):
            pair = {dop.target_opcode, dop.base_opcode}
            # cmpw = signed, cmplw/cmplwi = unsigned
            if "cmpw" in pair and "cmplw" in pair:
                target_signed = dop.target_opcode == "cmpw"
                cs.sign_choices.append(
                    (i, "signed" if target_signed else "unsigned")
                )
            elif "cmpwi" in pair and "cmplwi" in pair:
                target_signed = dop.target_opcode == "cmpwi"
                cs.sign_choices.append(
                    (i, "signed" if target_signed else "unsigned")
                )

    # 7. Preflight check
    if ctx.ghidra_ast is not None:
        cs.preflight = run_preflight(
            ctx.ghidra_ast, ctx.func_node, ctx.file_source,
            diagnosis=ctx.diagnosis,
            symbol=ctx.symbol,
            file_path=ctx.file_path,
        )

    return cs


def _non_decl_resolved_edits(
    constraints: ConstraintSet, ctx: FunctionContext,
) -> list[ResolvedEdit]:
    """Compute the non-decl-order deterministic edits (cf direction, null guard).

    These are shared across all decl-order hypotheses, so they're computed once
    and recombined per hypothesis in synthesize().
    """
    edits: list[ResolvedEdit] = []

    # Control flow direction (and_split / merge)
    if constraints.cf_directions:
        edits.extend(_resolve_cf_directions(constraints, ctx))

    # Null guard removal
    if constraints.null_checks_to_remove:
        edits.extend(_resolve_null_guards(constraints, ctx))

    return edits


def _resolve_conflicts(edits: list[ResolvedEdit]) -> list[ResolvedEdit]:
    """Sort edits by offset and drop ones overlapping a higher-priority edit."""
    _PRIORITY = {"decl_order": 4, "cf_direction": 3, "expr_shape": 2, "null_guard": 1}
    ordered = sorted(edits, key=lambda e: (e.start, -_PRIORITY.get(e.category, 0)))

    resolved: list[ResolvedEdit] = []
    last_end = -1
    for edit in ordered:
        if edit.start >= last_end:
            resolved.append(edit)
            last_end = edit.end
        # else: overlapping with higher-priority edit, drop
    return resolved


def resolve_to_edits(constraints: ConstraintSet, ctx: FunctionContext) -> list[ResolvedEdit]:
    """Phase 2: Map resolved constraints to deterministic source edits.

    Delegates to existing pattern helpers for each constraint type.
    Returns edits sorted by byte offset, with conflicts resolved by priority.
    Uses the PREFERRED decl-order hypothesis (Ghidra-first on a disagreement);
    the alternative is emitted as an extra variant by synthesize().
    """
    edits: list[ResolvedEdit] = []

    # 1. Declaration reorder (highest priority)
    # Fires when we have a decl_order constraint (from Ghidra and/or m2c)
    # AND either a Ghidra AST or m2c text to drive the var-order extraction in
    # _resolve_decl_order. swap_pairs is no longer required — the C2 fix in
    # ghidra_guided_reorder emits Ghidra-order-driven candidates when swap_pairs
    # is empty.
    if (constraints.decl_order is not None
            and (ctx.ghidra_ast is not None or ctx.m2c_code)):
        edits.extend(_resolve_decl_order(constraints, ctx))

    # 2/3. Control flow direction + null guard removal.
    edits.extend(_non_decl_resolved_edits(constraints, ctx))

    return _resolve_conflicts(edits)


def enumerate_free_variables(
    constraints: ConstraintSet, ctx: FunctionContext, max_combos: int = 8,
) -> list[list[ResolvedEdit]]:
    """Phase 3: Enumerate free variable combinations (sign choices).

    Each sign choice has 2 options. If cross-product <= max_combos, enumerate all.
    Otherwise, take the first max_combos combinations.

    Returns list of edit-lists, one per combination.
    """
    if not constraints.sign_choices:
        return []

    # Find comparison expressions in source that might correspond to sign choices
    sign_candidates = _find_sign_comparison_sites(ctx)
    if not sign_candidates:
        return []

    # Map each sign choice to possible edits
    choice_options: list[list[ResolvedEdit]] = []
    for idx, (diff_idx, target_sign) in enumerate(constraints.sign_choices):
        if idx >= len(sign_candidates):
            break
        site = sign_candidates[idx]
        # Generate the edit for the non-default sign
        edit = _make_sign_edit(site, target_sign)
        if edit:
            choice_options.append([edit])

    if not choice_options:
        return []

    # Generate cross-product of present/absent for each edit
    combos: list[list[ResolvedEdit]] = []
    bits = min(len(choice_options), 8)  # Cap at 8 dimensions
    for combo_mask in range(1, 2**bits):
        if len(combos) >= max_combos:
            break
        combo_edits = []
        for i in range(bits):
            if combo_mask & (1 << i):
                combo_edits.extend(choice_options[i])
        combos.append(combo_edits)

    return combos


def synthesize(ctx: FunctionContext) -> SynthesisResult:
    """Phase 4: Full constraint-directed synthesis pipeline.

    Extracts constraints, resolves deterministic edits, enumerates free variables,
    and produces 1-8 variants. Returns SynthesisResult with skip_reason if unfixable.
    """
    constraints = extract_constraints(ctx)

    if constraints.is_provably_unfixable:
        return SynthesisResult(
            constraints=constraints,
            variants=[],
            skip_reason=constraints.skip_reason,
        )

    # Build a deterministic-edit set per decl-order hypothesis.
    #
    # When Ghidra and m2c AGREE (or only one is present) there is a single
    # hypothesis. When they DISAGREE, combine_var_orders yields two — Ghidra's
    # and m2c's orderings — and we synthesize a candidate for BOTH rather than
    # silently preferring one, so the search covers both. The non-decl edits
    # (cf direction, null guards) are shared, recombined per hypothesis.
    non_decl_edits = _non_decl_resolved_edits(constraints, ctx)

    det_edit_sets: list[list[ResolvedEdit]] = []
    if (constraints.decl_order is not None
            and (ctx.ghidra_ast is not None or ctx.m2c_code)):
        for decl_edits in _decl_order_edit_sets(constraints, ctx):
            det_edit_sets.append(_resolve_conflicts(decl_edits + non_decl_edits))

    # Fallback: no decl-order hypotheses (or no decl_order constraint) — still
    # apply the non-decl edits as a single set so cf/null-guard synthesis works.
    if not det_edit_sets:
        det_edit_sets = [_resolve_conflicts(list(non_decl_edits))]

    free_combos = enumerate_free_variables(constraints, ctx)

    variants: list[Variant] = []
    seen_sources: set[bytes] = set()

    # High-confidence tag: Ghidra and m2c agreed on the decl-order (or the cf
    # shape). Downstream ranking / logging can treat tagged variants as the
    # preferred guess — the two independent decompilers concur, so this is the
    # strongest synthesis signal available.
    primary_tags: frozenset[str] = frozenset()
    if constraints.decl_order_high_confidence or constraints.cf_high_confidence:
        primary_tags = frozenset({"ghidra_m2c_agree"})

    def _emit(name: str, description: str, source: bytes,
              tags: frozenset[str] = frozenset()) -> None:
        # Dedup: disagreeing hypotheses can collapse to the same source (e.g. a
        # symmetric 2-var swap), and the free-combo loop can revisit a base.
        if source != ctx.file_source and source not in seen_sources:
            seen_sources.add(source)
            variants.append(Variant(
                name=name,
                pattern_name="constraint_solver",
                description=description,
                source=source,
                tags=tags,
            ))

    # Preferred (primary) edit set keeps the historical synth_0/synth_i naming;
    # the alternative hypothesis (disagreement) is suffixed _altN so logs make
    # the two-hypothesis split visible.
    primary_edits = det_edit_sets[0]
    alt_edit_sets = det_edit_sets[1:]

    if not free_combos:
        _emit("synth_0", f"{len(primary_edits)} resolved constraints",
              _apply_edits(ctx.file_source, primary_edits), primary_tags)
    else:
        for i, free_edits in enumerate(free_combos):
            all_edits = primary_edits + free_edits
            _emit(f"synth_{i}",
                  f"{len(primary_edits)} resolved + {len(free_edits)} free",
                  _apply_edits(ctx.file_source, all_edits), primary_tags)

    # Extra candidate(s) for the disagreeing decl-order hypothesis. Bounded by
    # combine_var_orders to a single alternative (Ghidra + m2c => at most 2
    # hypotheses total), so this adds at most one variant. Tagged distinctly so
    # it is never confused with the high-confidence agree signal.
    for j, alt_edits in enumerate(alt_edit_sets):
        _emit(f"synth_alt{j}",
              f"alt decl-order hypothesis ({constraints.decl_order_verdict}): "
              f"{len(alt_edits)} resolved constraints",
              _apply_edits(ctx.file_source, alt_edits),
              frozenset({"ghidra_m2c_alt"}))

    return SynthesisResult(
        constraints=constraints,
        variants=variants,
        deterministic_edit_count=len(primary_edits),
        free_variable_count=constraints.free_variable_count,
    )


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------


def _combine_cf_tags(ghidra_tags: list[str], m2c_tags: list[str]) -> list[str]:
    """Union the control-flow guard-shape tags from Ghidra and m2c.

    Tags present in BOTH decompilers come first (high-confidence — both
    independent views see the same shape), followed by tags unique to either,
    preserving discovery order. This is purely additive: it can only widen the
    set of cf_directions the resolver considers, never drop one a single source
    found. Returns [] when both inputs are empty.
    """
    g = list(dict.fromkeys(ghidra_tags))  # dedup, preserve order
    m = list(dict.fromkeys(m2c_tags))
    if not g and not m:
        return []
    g_set, m_set = set(g), set(m)

    ordered: list[str] = []
    # Agreed tags first (in Ghidra's discovery order, then any m2c-order extras).
    for tag in g:
        if tag in m_set and tag not in ordered:
            ordered.append(tag)
    for tag in m:
        if tag in g_set and tag not in ordered:
            ordered.append(tag)
    # Then single-source tags, Ghidra first.
    for tag in g + m:
        if tag not in ordered:
            ordered.append(tag)
    return ordered


def _apply_edits(source: bytes, edits: list[ResolvedEdit]) -> bytes:
    """Apply a list of ResolvedEdits using SourceEditor."""
    if not edits:
        return source
    editor = SourceEditor(source)
    for edit in edits:
        editor.replace_range(edit.start, edit.end, edit.replacement)
    try:
        return editor.apply()
    except ValueError:
        # Overlapping edits — return unchanged
        return source


def _decl_order_var_hypotheses(ctx: FunctionContext) -> list[list]:
    """Return the variable-order hypotheses to try, most-preferred first.

    Combines Ghidra + m2c via combine_var_orders:
      * agree / single-source -> one hypothesis
      * disagree              -> two hypotheses (Ghidra's order, then m2c's)

    Each hypothesis is a list of VarInfo in first-use order. Returns [] when
    neither decompiler produced a usable order.
    """
    try:
        from .ghidra_var_match import combine_var_orders
        from .ghidra_ast import extract_variable_first_use_order
    except ImportError:
        return []

    ghidra_vars = None
    if ctx.ghidra_ast is not None:
        gvo = extract_variable_first_use_order(ctx.ghidra_ast)
        if gvo:
            ghidra_vars = gvo

    m2c_vars = None
    if ctx.m2c_code:
        from .m2c import extract_variable_first_use_order_from_text
        mvo = extract_variable_first_use_order_from_text(ctx.m2c_code)
        if mvo:
            m2c_vars = mvo

    return combine_var_orders(ghidra_vars, m2c_vars).orders


def _decl_order_edits_for_vars(
    var_order: list, constraints: ConstraintSet, ctx: FunctionContext,
) -> list[ResolvedEdit]:
    """Convert one variable first-use order into byte-level decl-swap edits.

    Uses ghidra_guided_reorder() to turn the (target var order, source decl
    order, swap pairs) into candidate orderings, then realizes the best
    candidate as a sequence of declaration swaps. Returns [] when nothing to do.
    """
    from .ghidra_var_match import ghidra_guided_reorder

    if not var_order:
        return []

    source_decls = _extract_source_decl_names(ctx)
    if len(source_decls) < 2:
        return []

    swap_pairs = [(str(a), str(b)) for a, b in constraints.swap_pairs]
    candidates = ghidra_guided_reorder(
        var_order,
        [name for name, _, _ in source_decls],
        swap_pairs,
        constraints.target_gpr_saves,
    )
    if not candidates:
        return []

    # Take the first candidate reorder and convert to byte-level swaps
    target_order = candidates[0]
    source_names = [name for name, _, _ in source_decls]

    edits: list[ResolvedEdit] = []
    for target_idx, target_name in enumerate(target_order):
        if target_idx >= len(source_names):
            break
        if source_names[target_idx] != target_name:
            # Find where target_name currently is
            for src_idx in range(target_idx + 1, len(source_names)):
                if source_names[src_idx] == target_name:
                    # Swap declarations at target_idx and src_idx
                    _, start_a, end_a = source_decls[target_idx]
                    _, start_b, end_b = source_decls[src_idx]
                    text_a = ctx.file_source[start_a:end_a]
                    text_b = ctx.file_source[start_b:end_b]
                    edits.append(ResolvedEdit(
                        category="decl_order",
                        description=f"swap {source_names[target_idx]} <-> {target_name}",
                        start=start_a, end=end_a,
                        replacement=text_b,
                    ))
                    edits.append(ResolvedEdit(
                        category="decl_order",
                        description=f"swap {source_names[target_idx]} <-> {target_name}",
                        start=start_b, end=end_b,
                        replacement=text_a,
                    ))
                    # Update tracking
                    source_names[target_idx], source_names[src_idx] = (
                        source_names[src_idx], source_names[target_idx]
                    )
                    source_decls[target_idx], source_decls[src_idx] = (
                        source_decls[src_idx], source_decls[target_idx]
                    )
                    break

    return edits


def _decl_order_edit_sets(
    constraints: ConstraintSet, ctx: FunctionContext,
) -> list[list[ResolvedEdit]]:
    """Resolve declaration-reorder edits for EVERY var-order hypothesis.

    Returns one edit-list per hypothesis from combine_var_orders, in
    preference order. When Ghidra and m2c disagree this yields TWO edit-lists
    (one per decompiler's ordering) so the search can try both — turning the
    disagreement into two candidates instead of silently preferring one.
    Empty edit-lists (hypothesis implies no reorder) are dropped.
    """
    hypotheses = _decl_order_var_hypotheses(ctx)
    edit_sets: list[list[ResolvedEdit]] = []
    for var_order in hypotheses:
        edits = _decl_order_edits_for_vars(var_order, constraints, ctx)
        if edits:
            edit_sets.append(edits)
    return edit_sets


def _resolve_decl_order(
    constraints: ConstraintSet, ctx: FunctionContext,
) -> list[ResolvedEdit]:
    """Resolve declaration reorder constraint to source edits (preferred order).

    Combines Ghidra + m2c variable first-use orders (see combine_var_orders):
    when they agree (or only one is present) this returns that single order's
    edits; when they disagree it returns the PREFERRED (Ghidra-first) order's
    edits. The alternative disagreeing hypothesis is surfaced separately by
    synthesize() as an extra variant, so the deterministic edit set stays
    conflict-free while the search still covers both orderings.
    """
    edit_sets = _decl_order_edit_sets(constraints, ctx)
    return edit_sets[0] if edit_sets else []


def _extract_source_decl_names(ctx: FunctionContext) -> list[tuple[str, int, int]]:
    """Extract (name, start_byte, end_byte) for each declaration in the function body."""
    decls: list[tuple[str, int, int]] = []
    for stmt in ctx.statements:
        if stmt.type == "declaration":
            declarator = stmt.child_by_field_name("declarator")
            if declarator is None:
                continue
            name = _get_declarator_name(declarator, ctx.file_source)
            if name:
                decls.append((name, stmt.start_byte, stmt.end_byte))
    return decls


def _get_declarator_name(node, source: bytes) -> str | None:
    """Extract identifier name from a declarator node."""
    if node.type == "identifier" and node.text:
        return node.text.decode("utf-8", errors="replace")
    if node.type == "init_declarator":
        inner = node.child_by_field_name("declarator")
        if inner:
            return _get_declarator_name(inner, source)
    if node.type in ("pointer_declarator", "reference_declarator", "array_declarator"):
        inner = node.child_by_field_name("declarator")
        if inner:
            return _get_declarator_name(inner, source)
    for child in node.named_children:
        if child.type == "identifier" and child.text:
            return child.text.decode("utf-8", errors="replace")
    return None


def _resolve_cf_directions(
    constraints: ConstraintSet, ctx: FunctionContext,
) -> list[ResolvedEdit]:
    """Resolve control flow direction constraints to source edits.

    Compares Ghidra's control flow tags against source structure and generates
    and_split / merge edits where they differ.
    """
    edits: list[ResolvedEdit] = []

    # Get source control flow tags
    from .ghidra_ast import extract_condition_structure as _ec
    if ctx.ghidra_ast is None:
        return []

    ghidra_tags = set(constraints.cf_directions.values())

    # Look for if-statements with && that Ghidra shows as nested_if
    if "nested_if" in ghidra_tags:
        for stmt in ctx.statements:
            edit = _try_and_to_nested(stmt, ctx)
            if edit:
                edits.append(edit)

    # Look for nested ifs that Ghidra shows as conjunction
    if "conjunction" in ghidra_tags:
        for stmt in ctx.statements:
            edit = _try_nested_to_and(stmt, ctx)
            if edit:
                edits.append(edit)

    return edits


def _try_and_to_nested(stmt, ctx: FunctionContext) -> ResolvedEdit | None:
    """Try to split an && condition into nested ifs."""
    if stmt.type != "if_statement":
        return None

    condition = stmt.child_by_field_name("condition")
    consequence = stmt.child_by_field_name("consequence")
    if condition is None or consequence is None:
        return None

    # Find && in condition
    inner = _get_condition_inner(condition)
    if inner is None or inner.type != "binary_expression":
        return None

    op = inner.child_by_field_name("operator")
    if op is None or op.text != b"&&":
        return None

    left = inner.child_by_field_name("left")
    right = inner.child_by_field_name("right")
    if left is None or right is None:
        return None

    # Build nested if
    left_text = ctx.file_source[left.start_byte:left.end_byte]
    right_text = ctx.file_source[right.start_byte:right.end_byte]
    body_text = ctx.file_source[consequence.start_byte:consequence.end_byte]

    # Get indentation
    line_start = ctx.file_source.rfind(b"\n", 0, stmt.start_byte) + 1
    indent = b""
    for ch in ctx.file_source[line_start:stmt.start_byte]:
        if ch in (0x20, 0x09):  # space or tab
            indent += bytes([ch])
        else:
            break

    nested = (
        b"if (" + left_text + b") {\n"
        + indent + b"    if (" + right_text + b") "
        + body_text + b"\n"
        + indent + b"}"
    )

    # Check for else clause
    alternative = stmt.child_by_field_name("alternative")
    if alternative:
        return None  # Don't split if there's an else — too complex

    return ResolvedEdit(
        category="cf_direction",
        description=f"split && into nested ifs",
        start=stmt.start_byte,
        end=stmt.end_byte,
        replacement=nested,
    )


def _try_nested_to_and(stmt, ctx: FunctionContext) -> ResolvedEdit | None:
    """Try to merge nested ifs into a single && condition."""
    if stmt.type != "if_statement":
        return None

    condition = stmt.child_by_field_name("condition")
    consequence = stmt.child_by_field_name("consequence")
    alternative = stmt.child_by_field_name("alternative")
    if condition is None or consequence is None or alternative is not None:
        return None  # Don't merge if outer has else

    # Check consequence is compound with single if-statement inside
    if consequence.type != "compound_statement":
        return None

    inner_stmts = [c for c in consequence.named_children if c.type != "comment"]
    if len(inner_stmts) != 1 or inner_stmts[0].type != "if_statement":
        return None

    inner_if = inner_stmts[0]
    inner_cond = inner_if.child_by_field_name("condition")
    inner_body = inner_if.child_by_field_name("consequence")
    inner_alt = inner_if.child_by_field_name("alternative")
    if inner_cond is None or inner_body is None or inner_alt is not None:
        return None  # Don't merge if inner has else

    outer_cond_inner = _get_condition_inner(condition)
    inner_cond_inner = _get_condition_inner(inner_cond)
    if outer_cond_inner is None or inner_cond_inner is None:
        return None

    outer_text = ctx.file_source[outer_cond_inner.start_byte:outer_cond_inner.end_byte]
    inner_text = ctx.file_source[inner_cond_inner.start_byte:inner_cond_inner.end_byte]
    body_text = ctx.file_source[inner_body.start_byte:inner_body.end_byte]

    merged = b"if (" + outer_text + b" && " + inner_text + b") " + body_text

    return ResolvedEdit(
        category="cf_direction",
        description="merge nested ifs into &&",
        start=stmt.start_byte,
        end=stmt.end_byte,
        replacement=merged,
    )


def _get_condition_inner(condition):
    """Get the inner expression from a parenthesized condition."""
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None


def _resolve_null_guards(
    constraints: ConstraintSet, ctx: FunctionContext,
) -> list[ResolvedEdit]:
    """Resolve null guard removal constraints to source edits."""
    # This is a placeholder — null_checks_to_remove indices would need
    # to be populated by a separate analysis pass comparing Ghidra call sites
    # against source guard patterns. For now, return empty.
    return []


def _find_sign_comparison_sites(ctx: FunctionContext) -> list[dict]:
    """Find comparison expressions in source that could be sign-dependent.

    Returns list of dicts with keys: start, end, text, comparison_op
    """
    sites: list[dict] = []

    def _walk(node):
        if node.type == "binary_expression":
            op = node.child_by_field_name("operator")
            if op and op.text in (b">", b">=", b"<", b"<=", b"!=", b"=="):
                # Check if one side is a literal 0
                left = node.child_by_field_name("left")
                right = node.child_by_field_name("right")
                if left and right:
                    lt = ctx.file_source[left.start_byte:left.end_byte]
                    rt = ctx.file_source[right.start_byte:right.end_byte]
                    if lt.strip() == b"0" or rt.strip() == b"0":
                        sites.append({
                            "start": node.start_byte,
                            "end": node.end_byte,
                            "text": ctx.file_source[node.start_byte:node.end_byte],
                            "op": op.text,
                            "left_start": left.start_byte,
                            "left_end": left.end_byte,
                            "right_start": right.start_byte,
                            "right_end": right.end_byte,
                        })
        for child in node.children:
            _walk(child)

    _walk(ctx.func_node)
    return sites


def _make_sign_edit(site: dict, target_sign: str) -> ResolvedEdit | None:
    """Make an edit to change a comparison's signedness.

    For unsigned zero comparisons: `x != 0` -> `x > 0` (or vice versa).
    Pattern: `x > 0` generates `ble` (unsigned), `x != 0` generates `beq`.
    """
    op = site["op"]
    text = site["text"]

    if target_sign == "unsigned":
        # Target wants unsigned comparison (ble/bgt pattern)
        if op == b"!=":
            # x != 0 -> x > 0
            new_text = text.replace(b"!= 0", b"> 0").replace(b"!=0", b"> 0")
            if new_text != text:
                return ResolvedEdit(
                    category="sign_choice",
                    description=f"!= 0 -> > 0 (unsigned)",
                    start=site["start"],
                    end=site["end"],
                    replacement=new_text,
                )
    elif target_sign == "signed":
        # Target wants signed comparison (beq/bne pattern)
        if op == b">":
            # x > 0 -> x != 0
            new_text = text.replace(b"> 0", b"!= 0").replace(b">0", b"!= 0")
            if new_text != text:
                return ResolvedEdit(
                    category="sign_choice",
                    description=f"> 0 -> != 0 (signed)",
                    start=site["start"],
                    end=site["end"],
                    replacement=new_text,
                )

    return None
