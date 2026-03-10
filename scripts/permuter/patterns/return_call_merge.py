"""Merge/split duplicate return-call patterns in if/else branches.

Win rate: proven on GetGennedFont (88.9% -> 100%).

When two `return func(X)` calls to the same function exist in if/else branches
with different arguments, merging them into a single call with a conditional
variable can match target codegen. The reverse (splitting) is also supported.

Transformations:

    Direction 1 - Merge (two calls -> one):
        if (cond) {                     Type var;
            return func(A);             if (cond) {
        } else {                 ->         var = A;
            return func(B);             } else {
        }                                   var = B;
                                        }
                                        return func(var);

    Direction 2 - Split (one call -> two):
        Type var;                       if (cond) {
        if (cond) {                         return func(A);
            var = A;             ->     } else {
        } else {                            return func(B);
            var = B;                    }
        }
        return func(var);

Detection signals:
    - blt/bge branch mismatches in diff_ops
    - Small clusters (branch restructuring)
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent, walk, node_text
from ..control_flow import else_compound_body, noncomment_named_children
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


class ReturnCallMergePattern(Pattern):
    name = "return_call_merge"
    safety_tier = "moderate"
    structural_domain = "control_flow"
    follow_ups = ("branch_polarity", "declaration_reorder", "early_return_merge")
    cross_unit_modes = ("inline_header",)

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Branch opcode mismatches suggest branch restructuring
        branch_ops = {"blt", "bge", "ble", "bgt", "beq", "bne"}
        for d in diagnosis.diff_ops:
            if d.target_opcode in branch_ops or d.base_opcode in branch_ops:
                return True
        # Small clusters suggest branch differences
        for c in diagnosis.clusters:
            if 2 <= c.size <= 10:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Boost if we see blt/bge specifically (common signal)
        for d in diagnosis.diff_ops:
            if {d.target_opcode, d.base_opcode} & {"blt", "bge"}:
                return 0.5
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0

        # m2c guidance: determine which direction to prefer
        prefer_merge = None  # None = both, True = merge, False = split
        if ctx.m2c_code:
            from ..m2c import extract_return_pattern
            pattern = extract_return_pattern(ctx.m2c_code)
            if pattern == "merged_var":
                prefer_merge = True  # Target uses merged var → try merge
            elif pattern == "split_calls":
                prefer_merge = False  # Target uses split returns → try split

        # Direction 1: Merge — find if/else with matching return calls
        if prefer_merge is not False:
            for variant in _try_merge_all(ctx, counter):
                yield variant
                counter += 1
                if counter >= 6:
                    return

        # Direction 2: Split — find conditional var + single return call
        if prefer_merge is not True:
            for variant in _try_split_all(ctx, counter):
                yield variant
                counter += 1
                if counter >= 6:
                    return


# ---------------------------------------------------------------------------
# Direction 1: Merge two return calls into one
# ---------------------------------------------------------------------------

def _try_merge_all(ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Find if/else where both branches end with return same_func(...)."""
    for node in walk(ctx.body_node):
        if node.type != "if_statement" or counter >= 6:
            continue
        # Region filter: skip if_statements outside mismatch regions
        if not ctx.node_in_mismatch_region(node):
            continue

        for variant in _try_merge_if_else(node, ctx, counter):
            yield variant
            counter += 1


def _try_merge_if_else(
    if_node: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Try merging return calls in an if/else pair."""
    source = ctx.file_source

    consequence = if_node.child_by_field_name("consequence")
    alternative = if_node.child_by_field_name("alternative")
    condition = if_node.child_by_field_name("condition")

    if consequence is None or alternative is None or condition is None:
        return

    # Extract return call from consequence
    cons_ret = _extract_return_call(consequence, source)
    if cons_ret is None:
        return

    # Get alternative body
    alt_body = else_compound_body(alternative)
    if alt_body is None:
        return

    # Extract return call from alternative
    alt_ret = _extract_return_call(alt_body, source)
    if alt_ret is None:
        return

    cons_func, cons_args = cons_ret
    alt_func, alt_args = alt_ret

    # Must call the same function
    if cons_func != alt_func:
        return

    # Must have same number of args
    if len(cons_args) != len(alt_args):
        return

    # Find which argument positions differ
    differing = []
    for idx in range(len(cons_args)):
        if cons_args[idx] != alt_args[idx]:
            differing.append(idx)

    # Only handle single-argument differences
    if len(differing) != 1:
        return

    diff_idx = differing[0]
    cons_diff_arg = cons_args[diff_idx]
    alt_diff_arg = alt_args[diff_idx]

    # Infer type for the merged variable
    var_type = _infer_type(cons_diff_arg, alt_diff_arg)
    var_name = b"_merged"

    # Get condition text
    cond_text = _get_condition_text(condition, source)
    if cond_text is None:
        return

    # Build merged args
    merged_args = []
    for idx in range(len(cons_args)):
        if idx == diff_idx:
            merged_args.append(var_name)
        else:
            merged_args.append(cons_args[idx])

    indent = get_indent(source, if_node)
    nl = b"\n"

    # Check if consequence has extra statements before the return
    cons_extra = _get_extra_statements(consequence, source)
    alt_extra = _get_extra_statements(alt_body, source)

    lines = []
    lines.append(indent + var_type + b" " + var_name + b";")
    lines.append(indent + b"if " + cond_text + b" {")
    for extra in cons_extra:
        lines.append(indent + b"    " + extra)
    lines.append(indent + b"    " + var_name + b" = " + cons_diff_arg + b";")
    lines.append(indent + b"} else {")
    for extra in alt_extra:
        lines.append(indent + b"    " + extra)
    lines.append(indent + b"    " + var_name + b" = " + alt_diff_arg + b";")
    lines.append(indent + b"}")
    call = cons_func + b"(" + b", ".join(merged_args) + b")"
    lines.append(indent + b"return " + call + b";")

    merged = nl.join(lines)

    new_source = source[:if_node.start_byte] + merged + source[if_node.end_byte:]

    yield Variant(
        name=f"rcmerge_{counter}",
        pattern_name="return_call_merge",
        description=(
            f"Merge return {cons_func.decode()}() calls "
            f"(arg {diff_idx} differs: {cons_diff_arg.decode()} vs {alt_diff_arg.decode()})"
        ),
        source=new_source,
        tags=frozenset({"merged_return_calls"}),
    )


# ---------------------------------------------------------------------------
# Direction 2: Split a conditional var + return call into two return calls
# ---------------------------------------------------------------------------

def _try_split_all(ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Find pattern: var assignment in if/else, then return func(var)."""
    stmts = ctx.statements
    for i in range(len(stmts) - 1):
        if counter >= 6:
            return

        # Look for if_statement followed by return_statement
        if_stmt = stmts[i]
        ret_stmt = stmts[i + 1]

        if if_stmt.type != "if_statement" or ret_stmt.type != "return_statement":
            continue
        # Region filter: skip pairs outside mismatch regions
        if not ctx.node_in_mismatch_region(if_stmt):
            continue

        for variant in _try_split_pair(if_stmt, ret_stmt, ctx, counter):
            yield variant
            counter += 1

    # Also check: declaration, if_statement, return_statement
    for i in range(len(stmts) - 2):
        if counter >= 6:
            return

        decl_stmt = stmts[i]
        if_stmt = stmts[i + 1]
        ret_stmt = stmts[i + 2]

        if decl_stmt.type != "declaration":
            continue
        if if_stmt.type != "if_statement" or ret_stmt.type != "return_statement":
            continue
        # Region filter: skip triplets outside mismatch regions
        if not ctx.node_in_mismatch_region(if_stmt):
            continue

        for variant in _try_split_with_decl(decl_stmt, if_stmt, ret_stmt, ctx, counter):
            yield variant
            counter += 1


def _try_split_pair(
    if_stmt: Node, ret_stmt: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Split: if (cond) { var = A; } else { var = B; } return func(var);"""
    source = ctx.file_source

    condition = if_stmt.child_by_field_name("condition")
    consequence = if_stmt.child_by_field_name("consequence")
    alternative = if_stmt.child_by_field_name("alternative")

    if condition is None or consequence is None or alternative is None:
        return

    # Extract assignments from both branches
    cons_assign = _extract_single_assignment(consequence, source)
    if cons_assign is None:
        return

    alt_body = else_compound_body(alternative)
    if alt_body is None:
        return

    alt_assign = _extract_single_assignment(alt_body, source)
    if alt_assign is None:
        return

    cons_var, cons_val = cons_assign
    alt_var, alt_val = alt_assign

    # Both must assign to the same variable
    if cons_var != alt_var:
        return

    # Return statement must contain a call using this variable
    ret_call = _extract_return_call_using_var(ret_stmt, source, cons_var)
    if ret_call is None:
        return

    func_name, args, var_idx = ret_call

    cond_text = _get_condition_text(condition, source)
    if cond_text is None:
        return

    indent = get_indent(source, if_stmt)
    nl = b"\n"

    # Build split args for each branch
    cons_call_args = list(args)
    cons_call_args[var_idx] = cons_val
    alt_call_args = list(args)
    alt_call_args[var_idx] = alt_val

    cons_call = func_name + b"(" + b", ".join(cons_call_args) + b")"
    alt_call = func_name + b"(" + b", ".join(alt_call_args) + b")"

    lines = []
    lines.append(indent + b"if " + cond_text + b" {")
    lines.append(indent + b"    return " + cons_call + b";")
    lines.append(indent + b"} else {")
    lines.append(indent + b"    return " + alt_call + b";")
    lines.append(indent + b"}")

    split = nl.join(lines)

    new_source = source[:if_stmt.start_byte] + split + source[ret_stmt.end_byte:]

    yield Variant(
        name=f"rcsplit_{counter}",
        pattern_name="return_call_merge",
        description=f"Split return {func_name.decode()}() into if/else branches",
        source=new_source,
        tags=frozenset({"split_return_calls"}),
    )


def _try_split_with_decl(
    decl_stmt: Node, if_stmt: Node, ret_stmt: Node,
    ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Split with preceding declaration: Type var; if (...) { var = A; } else { var = B; } return func(var);"""
    source = ctx.file_source

    # Extract declared variable name
    decl_name = _extract_decl_var_name(decl_stmt, source)
    if decl_name is None:
        return

    condition = if_stmt.child_by_field_name("condition")
    consequence = if_stmt.child_by_field_name("consequence")
    alternative = if_stmt.child_by_field_name("alternative")

    if condition is None or consequence is None or alternative is None:
        return

    cons_assign = _extract_single_assignment(consequence, source)
    if cons_assign is None:
        return

    alt_body = else_compound_body(alternative)
    if alt_body is None:
        return

    alt_assign = _extract_single_assignment(alt_body, source)
    if alt_assign is None:
        return

    cons_var, cons_val = cons_assign
    alt_var, alt_val = alt_assign

    if cons_var != alt_var or cons_var != decl_name:
        return

    ret_call = _extract_return_call_using_var(ret_stmt, source, cons_var)
    if ret_call is None:
        return

    func_name, args, var_idx = ret_call

    cond_text = _get_condition_text(condition, source)
    if cond_text is None:
        return

    indent = get_indent(source, if_stmt)
    nl = b"\n"

    cons_call_args = list(args)
    cons_call_args[var_idx] = cons_val
    alt_call_args = list(args)
    alt_call_args[var_idx] = alt_val

    cons_call = func_name + b"(" + b", ".join(cons_call_args) + b")"
    alt_call = func_name + b"(" + b", ".join(alt_call_args) + b")"

    lines = []
    lines.append(indent + b"if " + cond_text + b" {")
    lines.append(indent + b"    return " + cons_call + b";")
    lines.append(indent + b"} else {")
    lines.append(indent + b"    return " + alt_call + b";")
    lines.append(indent + b"}")

    split = nl.join(lines)

    # Remove declaration + if + return, replace with split
    new_source = source[:decl_stmt.start_byte] + split + source[ret_stmt.end_byte:]

    yield Variant(
        name=f"rcsplit_{counter}",
        pattern_name="return_call_merge",
        description=f"Split return {func_name.decode()}() into if/else (removing decl)",
        source=new_source,
        tags=frozenset({"split_return_calls"}),
    )


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _extract_return_call(compound: Node, source: bytes) -> tuple[bytes, list[bytes]] | None:
    """Extract (func_name, [arg_texts]) from a compound with a single return-of-call.

    Returns None if the compound doesn't end with `return func(...)`.
    """
    stmts = noncomment_named_children(compound)
    if not stmts:
        return None

    last = stmts[-1]
    if last.type != "return_statement":
        return None

    # Find call_expression in return
    call = None
    for child in last.named_children:
        if child.type == "call_expression":
            call = child
            break
    if call is None:
        return None

    func = call.child_by_field_name("function")
    args_node = call.child_by_field_name("arguments")
    if func is None or args_node is None:
        return None

    func_text = node_text(source, func)
    arg_texts = [node_text(source, a) for a in args_node.named_children]

    return func_text, arg_texts


def _extract_return_call_using_var(
    ret_stmt: Node, source: bytes, var_name: bytes
) -> tuple[bytes, list[bytes], int] | None:
    """Extract (func_name, [arg_texts], var_arg_index) from return func(...var...)."""
    call = None
    for child in ret_stmt.named_children:
        if child.type == "call_expression":
            call = child
            break
    if call is None:
        return None

    func = call.child_by_field_name("function")
    args_node = call.child_by_field_name("arguments")
    if func is None or args_node is None:
        return None

    func_text = node_text(source, func)
    args = args_node.named_children
    arg_texts = [node_text(source, a) for a in args]

    # Find which arg is the variable
    var_indices = [i for i, t in enumerate(arg_texts) if t == var_name]
    if len(var_indices) != 1:
        return None

    return func_text, arg_texts, var_indices[0]


def _extract_single_assignment(
    compound: Node, source: bytes
) -> tuple[bytes, bytes] | None:
    """Extract (var_name, value) from compound with single assignment statement."""
    stmts = noncomment_named_children(compound)
    if len(stmts) != 1:
        return None

    stmt = stmts[0]
    if stmt.type != "expression_statement":
        return None

    for child in stmt.named_children:
        if child.type == "assignment_expression":
            left = child.child_by_field_name("left")
            right = child.child_by_field_name("right")
            if left is None or right is None:
                return None
            return node_text(source, left), node_text(source, right)
    return None


def _extract_decl_var_name(decl: Node, source: bytes) -> bytes | None:
    """Extract variable name from a declaration like `Type var;`."""
    declarator = decl.child_by_field_name("declarator")
    if declarator is None:
        return None

    # init_declarator (has initializer) or plain identifier/pointer_declarator
    if declarator.type == "init_declarator":
        name_node = declarator.child_by_field_name("declarator")
    else:
        name_node = declarator

    if name_node is None:
        return None

    # Unwrap pointer/reference declarators
    while name_node.type in ("pointer_declarator", "reference_declarator"):
        inner = name_node.child_by_field_name("declarator")
        if inner is None:
            break
        name_node = inner

    if name_node.type == "identifier":
        return node_text(source, name_node)
    return None
def _get_condition_text(condition: Node, source: bytes) -> bytes | None:
    """Get condition text including parens from condition_clause."""
    # The condition_clause includes parens: (expr)
    text = node_text(source, condition)
    return text if text else None


def _get_extra_statements(compound: Node, source: bytes) -> list[bytes]:
    """Get statements before the return (if any)."""
    stmts = noncomment_named_children(compound)
    if len(stmts) <= 1:
        return []
    # All but last (which is the return)
    return [node_text(source, s) for s in stmts[:-1]]


def _infer_type(arg_a: bytes, arg_b: bytes) -> bytes:
    """Infer the type of the merged variable from two argument texts."""
    # nullptr or NULL suggests pointer
    if arg_a in (b"nullptr", b"NULL", b"0") or arg_b in (b"nullptr", b"NULL", b"0"):
        # Try to get type from the non-null arg
        other = arg_b if arg_a in (b"nullptr", b"NULL", b"0") else arg_a
        # If it's a cast like (Type*)expr, extract Type*
        if other.startswith(b"(") and b"*)" in other:
            end = other.index(b"*)") + 2
            return other[1:end - 1]
        return b"auto"

    # Integer literals
    if arg_a.isdigit() or arg_b.isdigit():
        return b"int"

    # String literals
    if arg_a.startswith(b'"') or arg_b.startswith(b'"'):
        return b"const char *"

    # Default
    return b"auto"
