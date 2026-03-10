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

    # Ghidra-based constraints
    if ctx.ghidra_ast is not None:
        cs.ghidra_available = True
        ast = ctx.ghidra_ast

        # 1. Variable first-use order (for declaration reorder)
        var_order = extract_variable_first_use_order(ast)
        if var_order:
            cs.decl_order = [v.name for v in var_order]

        # 2. Control flow structure (conjunction vs nested_if vs guard)
        cf_tags = extract_condition_structure(ast)
        for i, tag in enumerate(cf_tags):
            cs.cf_directions[i] = tag

        # 3. Prologue save counts
        if ctx.ghidra_code:
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


def resolve_to_edits(constraints: ConstraintSet, ctx: FunctionContext) -> list[ResolvedEdit]:
    """Phase 2: Map resolved constraints to deterministic source edits.

    Delegates to existing pattern helpers for each constraint type.
    Returns edits sorted by byte offset, with conflicts resolved by priority.
    """
    edits: list[ResolvedEdit] = []

    # 1. Declaration reorder (highest priority)
    if (constraints.decl_order is not None
            and constraints.swap_pairs
            and ctx.ghidra_ast is not None):
        decl_edits = _resolve_decl_order(constraints, ctx)
        edits.extend(decl_edits)

    # 2. Control flow direction (and_split / merge)
    if constraints.cf_directions:
        cf_edits = _resolve_cf_directions(constraints, ctx)
        edits.extend(cf_edits)

    # 3. Null guard removal
    if constraints.null_checks_to_remove:
        ng_edits = _resolve_null_guards(constraints, ctx)
        edits.extend(ng_edits)

    # Conflict resolution: sort by start offset, drop overlapping lower-priority edits
    _PRIORITY = {"decl_order": 4, "cf_direction": 3, "expr_shape": 2, "null_guard": 1}
    edits.sort(key=lambda e: (e.start, -_PRIORITY.get(e.category, 0)))

    resolved: list[ResolvedEdit] = []
    last_end = -1
    for edit in edits:
        if edit.start >= last_end:
            resolved.append(edit)
            last_end = edit.end
        # else: overlapping with higher-priority edit, drop

    return resolved


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

    det_edits = resolve_to_edits(constraints, ctx)
    free_combos = enumerate_free_variables(constraints, ctx)

    variants: list[Variant] = []
    if not free_combos:
        # Single composite variant from deterministic edits only
        if det_edits:
            source = _apply_edits(ctx.file_source, det_edits)
            if source != ctx.file_source:
                variants.append(Variant(
                    name="synth_0",
                    pattern_name="constraint_solver",
                    description=f"{len(det_edits)} resolved constraints",
                    source=source,
                ))
    else:
        for i, free_edits in enumerate(free_combos):
            all_edits = det_edits + free_edits
            source = _apply_edits(ctx.file_source, all_edits)
            if source != ctx.file_source:
                variants.append(Variant(
                    name=f"synth_{i}",
                    pattern_name="constraint_solver",
                    description=f"{len(det_edits)} resolved + {len(free_edits)} free",
                    source=source,
                ))

    return SynthesisResult(
        constraints=constraints,
        variants=variants,
        deterministic_edit_count=len(det_edits),
        free_variable_count=constraints.free_variable_count,
    )


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------


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


def _resolve_decl_order(
    constraints: ConstraintSet, ctx: FunctionContext,
) -> list[ResolvedEdit]:
    """Resolve declaration reorder constraint to source edits.

    Uses ghidra_var_match.ghidra_guided_reorder() to compute target orderings,
    then translates the best reorder to byte-level edits.
    """
    try:
        from .ghidra_var_match import ghidra_guided_reorder
        from .ghidra_ast import extract_variable_first_use_order
    except ImportError:
        return []

    ghidra_vars = extract_variable_first_use_order(ctx.ghidra_ast)
    if not ghidra_vars:
        return []

    # Extract source declaration names from the function body
    source_decls = _extract_source_decl_names(ctx)
    if len(source_decls) < 2:
        return []

    swap_pairs = [(str(a), str(b)) for a, b in constraints.swap_pairs]
    candidates = ghidra_guided_reorder(
        ghidra_vars,
        [name for name, _, _ in source_decls],
        swap_pairs,
        constraints.target_gpr_saves,
    )

    if not candidates:
        return []

    # Take the first candidate reorder and convert to byte-level swaps
    target_order = candidates[0]
    source_names = [name for name, _, _ in source_decls]

    # Find which pairs need to swap
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
