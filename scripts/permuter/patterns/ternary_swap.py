"""Ternary swap pattern — convert if/else assignments to ternary and vice versa.

The compiler may generate different code for:
    if (cond) { x = a; } else { x = b; }
vs:
    x = cond ? a : b;

Also handles return-ternary:
    return cond ? a : b;
vs:
    if (cond) { return a; } else { return b; }

And condition polarity flips:
    if (cond) { x = a; } else { x = b; }
    ->
    x = !cond ? b : a;

And bare if/return (no else):
    if (cond) return a;
    return b;
    ->
    return cond ? a : b;

Example:
    if (flag) { val = 1; } else { val = 2; }
    ->
    val = flag ? 1 : 2;
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent
from ..types import Diagnosis, FunctionContext, Variant


_BRANCH_OPS = {"beq", "bne", "bge", "ble", "bgt", "blt",
               "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
               "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-"}


def _has_swappable_constructs(body: Node) -> bool:
    """Quick AST check for ternary/if-else patterns worth transforming.

    Returns True if the body contains:
    - if_statement with alternative (else) where both branches have single assignments/returns
    - conditional_expression (ternary) in assignment or return
    - Consecutive if (no else) + return (bare if/return pattern)
    """
    from ..ast_queries import walk

    stmts = list(body.named_children)

    for node in walk(body):
        # Ternary in assignment or return context
        if node.type == "conditional_expression":
            parent = node.parent
            if parent is not None and parent.type in (
                "assignment_expression", "return_statement", "init_declarator",
            ):
                return True

        # if/else with single assignment or return in each branch
        if node.type == "if_statement":
            alternative = node.child_by_field_name("alternative")
            consequence = node.child_by_field_name("consequence")
            if alternative is not None and consequence is not None:
                # Check if both branches are single-statement (assignment or return)
                cons_ok = _is_single_assign_or_return(consequence)
                if cons_ok:
                    # alt body is inside the else clause
                    for child in alternative.children:
                        if child.type == "compound_statement":
                            if _is_single_assign_or_return(child):
                                return True
                            break

    # Check for bare if/return pattern (consecutive stmts)
    for i in range(len(stmts) - 1):
        if_stmt = stmts[i]
        next_stmt = stmts[i + 1]
        if (
            if_stmt.type == "if_statement"
            and next_stmt.type == "return_statement"
            and if_stmt.child_by_field_name("alternative") is None
        ):
            consequence = if_stmt.child_by_field_name("consequence")
            if consequence is not None:
                # Check if consequence has a return
                if _has_return(consequence):
                    return True

    return False


def _is_single_assign_or_return(compound: Node) -> bool:
    """Check if compound_statement has exactly one assignment or return."""
    if compound.type != "compound_statement":
        # Could be a bare return_statement
        return compound.type == "return_statement"
    named = [c for c in compound.named_children]
    if len(named) != 1:
        return False
    stmt = named[0]
    if stmt.type == "return_statement":
        return True
    if stmt.type == "expression_statement":
        for child in stmt.named_children:
            if child.type == "assignment_expression":
                return True
    return False


def _has_return(node: Node) -> bool:
    """Check if node contains a return_statement."""
    if node.type == "return_statement":
        return True
    if node.type == "compound_statement":
        for child in node.named_children:
            if child.type == "return_statement":
                return True
    return False


class TernarySwapPattern(Pattern):
    name = "ternary_swap"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        has_branch_mismatch = any(
            d.target_opcode in _BRANCH_OPS or d.base_opcode in _BRANCH_OPS
            for d in diagnosis.diff_ops
        )
        # Branch opcode mismatch is the primary signal for ternary-vs-if/else
        if has_branch_mismatch:
            return True
        # Small clusters (2-6 insns) are the characteristic ternary signature
        if any(2 <= c.size <= 6 for c in diagnosis.clusters):
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0

        has_branch_mismatch = any(
            d.target_opcode in _BRANCH_OPS or d.base_opcode in _BRANCH_OPS
            for d in diagnosis.diff_ops
        )

        # Small clusters (size 2-6) are the signature of ternary-vs-if/else:
        # one branch + one assignment on each side = 2-6 instruction difference
        small_clusters = [c for c in diagnosis.clusters if 2 <= c.size <= 6]

        # Strong signal: small clusters exist AND branch opcode mismatches present
        if small_clusters and has_branch_mismatch:
            return 1.0

        # Moderate signal: branch mismatches without the characteristic small clusters
        if has_branch_mismatch:
            return 0.5

        # Weak signal: only clusters or reg swaps, no branch evidence
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        # AST preflight: skip if body has no swappable constructs
        if not _has_swappable_constructs(ctx.body_node):
            return

        counter = 0
        for stmt in ctx.statements:
            # Region filter: skip statements outside mismatch regions
            if not ctx.node_in_mismatch_region(stmt):
                continue
            # if/else -> ternary (preferred direction per TECHNICAL_NOTES)
            for variant in _if_to_ternary(stmt, ctx, counter):
                yield variant
                counter += 1

            # if/else -> ternary with polarity flip (!cond ? alt : cons)
            for variant in _if_to_ternary_flipped(stmt, ctx, counter):
                yield variant
                counter += 1

            # ternary -> if/else
            for variant in _ternary_to_if(stmt, ctx, counter):
                yield variant
                counter += 1

            # return ternary <-> if/else return
            for variant in _return_ternary_to_if(stmt, ctx, counter):
                yield variant
                counter += 1

            for variant in _if_return_to_ternary(stmt, ctx, counter):
                yield variant
                counter += 1

            # if/else return with polarity flip
            for variant in _if_return_to_ternary_flipped(stmt, ctx, counter):
                yield variant
                counter += 1

        # bare if/return (no else) -> return ternary
        # Filter statements for region awareness before passing to bare if/return
        region_stmts = [s for s in ctx.statements if ctx.node_in_mismatch_region(s)]
        for variant in _bare_if_return_to_ternary(region_stmts, ctx, counter):
            yield variant
            counter += 1


def _extract_identifier(node: Node) -> str | None:
    """Unwrap pointer_declarator/reference_declarator to get the identifier name."""
    if node.type == "identifier":
        return node.text.decode("utf-8") if node.text else None
    elif node.type in ("pointer_declarator", "reference_declarator"):
        inner = node.child_by_field_name("declarator")
        return _extract_identifier(inner) if inner else None
    return None


def _extract_declarator_text(node: Node) -> str | None:
    """Get the full declarator text including pointer/reference markers."""
    if node.type == "identifier":
        return node.text.decode("utf-8") if node.text else None
    elif node.type in ("pointer_declarator", "reference_declarator"):
        return node.text.decode("utf-8") if node.text else None
    return None


def _if_to_ternary(node: Node, ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Convert if/else with single assignments to ternary."""
    if node.type != "if_statement":
        for child in node.children:
            yield from _if_to_ternary(child, ctx, counter)
        return

    condition = node.child_by_field_name("condition")
    consequence = node.child_by_field_name("consequence")
    alternative = node.child_by_field_name("alternative")

    if condition is None or consequence is None or alternative is None:
        return

    # Extract the single assignment from consequence
    cons_assign = _extract_single_assignment(consequence)
    if cons_assign is None:
        return

    # Extract the single assignment from alternative body
    alt_body = None
    for child in alternative.children:
        if child.type == "compound_statement":
            alt_body = child
            break
        elif child.type == "if_statement":
            return  # else-if, skip

    if alt_body is None:
        return

    alt_assign = _extract_single_assignment(alt_body)
    if alt_assign is None:
        return

    cons_var, cons_val = cons_assign
    alt_var, alt_val = alt_assign

    # Both branches must assign to the same variable
    if cons_var != alt_var:
        return

    # Get condition expression text (inside the parentheses)
    cond_expr = _get_condition_text(condition, ctx)
    if cond_expr is None:
        return

    # Build ternary: var = cond ? cons_val : alt_val;
    indent = get_indent(ctx.file_source, node)
    ternary = (
        indent + cons_var.encode("utf-8")
        + b" = " + cond_expr.encode("utf-8")
        + b" ? " + cons_val.encode("utf-8")
        + b" : " + alt_val.encode("utf-8")
        + b";"
    )

    source = ctx.file_source
    new_source = source[:node.start_byte] + ternary + source[node.end_byte:]

    yield Variant(
        name=f"ternary_{counter}",
        pattern_name="ternary_swap",
        description=f"if/else -> ternary: {cons_var} = ... ? ... : ...",
        source=new_source,
    )


def _ternary_to_if(node: Node, ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Convert ternary assignment to if/else."""
    if node.type != "expression_statement" and node.type != "declaration":
        for child in node.children:
            yield from _ternary_to_if(child, ctx, counter)
        return

    # Find conditional_expression in assignment or declaration context
    ternary_info = _find_ternary_assignment(node, ctx)
    if ternary_info is None:
        return

    var_name, cond_text, true_text, false_text = ternary_info

    indent = get_indent(ctx.file_source, node)
    nl = b"\n"

    # Build if/else block
    if_block = (
        indent + b"if (" + cond_text.encode("utf-8") + b") {" + nl
        + indent + b"    " + var_name.encode("utf-8") + b" = " + true_text.encode("utf-8") + b";" + nl
        + indent + b"} else {" + nl
        + indent + b"    " + var_name.encode("utf-8") + b" = " + false_text.encode("utf-8") + b";" + nl
        + indent + b"}"
    )

    # For declarations, we need to declare the variable first
    source = ctx.file_source
    if node.type == "declaration":
        # Extract type from declaration
        type_node = node.child_by_field_name("type")
        if type_node is None:
            return
        type_text = ctx.source_text(type_node)

        # Get the full declarator text (with * or &) for the declaration
        declarator = node.child_by_field_name("declarator")
        if declarator is None:
            return
        name_node = declarator.child_by_field_name("declarator")
        if name_node is None:
            return
        decl_text = _extract_declarator_text(name_node)
        if decl_text is None:
            return

        decl_line = indent + type_text.encode("utf-8") + b" " + decl_text.encode("utf-8") + b";" + nl
        new_source = source[:node.start_byte] + decl_line + if_block + source[node.end_byte:]
    else:
        new_source = source[:node.start_byte] + if_block + source[node.end_byte:]

    yield Variant(
        name=f"ternary_{counter}",
        pattern_name="ternary_swap",
        description=f"ternary -> if/else: {var_name}",
        source=new_source,
    )


def _return_ternary_to_if(node: Node, ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Convert `return cond ? a : b;` to `if (cond) { return a; } else { return b; }`."""
    if node.type != "return_statement":
        for child in node.children:
            yield from _return_ternary_to_if(child, ctx, counter)
        return

    # Find a conditional_expression as the return value
    ternary = None
    for child in node.named_children:
        if child.type == "conditional_expression":
            ternary = child
            break
    if ternary is None:
        return

    cond = ternary.child_by_field_name("condition")
    cons = ternary.child_by_field_name("consequence")
    alt = ternary.child_by_field_name("alternative")
    if cond is None or cons is None or alt is None:
        return

    cond_text = ctx.source_text(cond)
    cons_text = ctx.source_text(cons)
    alt_text = ctx.source_text(alt)

    indent = get_indent(ctx.file_source, node)
    nl = b"\n"

    if_block = (
        indent + b"if (" + cond_text.encode("utf-8") + b") {" + nl
        + indent + b"    return " + cons_text.encode("utf-8") + b";" + nl
        + indent + b"} else {" + nl
        + indent + b"    return " + alt_text.encode("utf-8") + b";" + nl
        + indent + b"}"
    )

    source = ctx.file_source
    new_source = source[:node.start_byte] + if_block + source[node.end_byte:]

    yield Variant(
        name=f"ternary_{counter}",
        pattern_name="ternary_swap",
        description=f"return ternary -> if/else return",
        source=new_source,
    )


def _if_return_to_ternary(node: Node, ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Convert `if (cond) { return a; } else { return b; }` to `return cond ? a : b;`."""
    if node.type != "if_statement":
        for child in node.children:
            yield from _if_return_to_ternary(child, ctx, counter)
        return

    condition = node.child_by_field_name("condition")
    consequence = node.child_by_field_name("consequence")
    alternative = node.child_by_field_name("alternative")

    if condition is None or consequence is None or alternative is None:
        return

    cons_ret = _extract_single_return(consequence)
    if cons_ret is None:
        return

    # Get alternative body
    alt_body = None
    for child in alternative.children:
        if child.type == "compound_statement":
            alt_body = child
            break
        elif child.type == "if_statement":
            return  # else-if, skip

    if alt_body is None:
        return

    alt_ret = _extract_single_return(alt_body)
    if alt_ret is None:
        return

    cond_expr = _get_condition_text(condition, ctx)
    if cond_expr is None:
        return

    indent = get_indent(ctx.file_source, node)
    ternary = (
        indent + b"return " + cond_expr.encode("utf-8")
        + b" ? " + cons_ret.encode("utf-8")
        + b" : " + alt_ret.encode("utf-8")
        + b";"
    )

    source = ctx.file_source
    new_source = source[:node.start_byte] + ternary + source[node.end_byte:]

    yield Variant(
        name=f"ternary_{counter}",
        pattern_name="ternary_swap",
        description=f"if/else return -> return ternary",
        source=new_source,
    )


def _extract_single_assignment(compound: Node) -> tuple[str, str] | None:
    """Extract (var_name, value_text) from a compound_statement with a single assignment."""
    stmts = [c for c in compound.named_children]
    if len(stmts) != 1:
        return None

    stmt = stmts[0]
    if stmt.type != "expression_statement":
        return None

    # Find assignment expression
    for child in stmt.named_children:
        if child.type == "assignment_expression":
            left = child.child_by_field_name("left")
            right = child.child_by_field_name("right")
            if left is None or right is None:
                return None
            # Accept identifier, field_expression (obj.member), or
            # pointer_expression (obj->member) on the LHS
            if left.type not in ("identifier", "field_expression", "pointer_expression"):
                return None
            var_name = left.text.decode("utf-8") if left.text else None
            val_text = right.text.decode("utf-8") if right.text else None
            if var_name and val_text:
                return var_name, val_text
    return None


def _extract_single_return(compound: Node) -> str | None:
    """Extract the return value text from a compound_statement with a single return."""
    stmts = [c for c in compound.named_children]
    if len(stmts) != 1:
        return None

    stmt = stmts[0]
    if stmt.type != "return_statement":
        return None

    # Get the return value (first named child that isn't a keyword)
    for child in stmt.named_children:
        return child.text.decode("utf-8") if child.text else None

    return None


def _extract_return_value(node: Node) -> str | None:
    """Extract return value from a return_statement or compound with single return."""
    if node.type == "return_statement":
        for child in node.named_children:
            return child.text.decode("utf-8") if child.text else None
        return None
    if node.type == "compound_statement":
        return _extract_single_return(node)
    return None


def _get_condition_text(condition: Node, ctx: FunctionContext) -> str | None:
    """Get the expression text from a condition_clause, stripping outer parens."""
    for child in condition.named_children:
        if child.type != "comment":
            return ctx.source_text(child)
    return None


def _find_ternary_assignment(
    node: Node, ctx: FunctionContext
) -> tuple[str, str, str, str] | None:
    """Find a ternary in assignment context.

    Returns (var_name, condition, true_val, false_val) or None.
    """
    if node.type == "expression_statement":
        for child in node.named_children:
            if child.type == "assignment_expression":
                left = child.child_by_field_name("left")
                right = child.child_by_field_name("right")
                if left is None or right is None:
                    continue
                if left.type not in ("identifier", "field_expression", "pointer_expression"):
                    continue
                if right.type == "conditional_expression":
                    return _parse_ternary(left, right, ctx)

    elif node.type == "declaration":
        declarator = node.child_by_field_name("declarator")
        if declarator is not None and declarator.type == "init_declarator":
            name_node = declarator.child_by_field_name("declarator")
            value_node = declarator.child_by_field_name("value")
            if name_node is not None and value_node is not None:
                if value_node.type == "conditional_expression":
                    var_name = _extract_identifier(name_node)
                    if var_name:
                        cond = value_node.child_by_field_name("condition")
                        cons = value_node.child_by_field_name("consequence")
                        alt = value_node.child_by_field_name("alternative")
                        if cond and cons and alt:
                            return (
                                var_name,
                                ctx.source_text(cond),
                                ctx.source_text(cons),
                                ctx.source_text(alt),
                            )
    return None


def _parse_ternary(
    left: Node, ternary: Node, ctx: FunctionContext
) -> tuple[str, str, str, str] | None:
    """Parse a conditional_expression node."""
    var_name = left.text.decode("utf-8") if left.text else None
    if not var_name:
        return None

    cond = ternary.child_by_field_name("condition")
    cons = ternary.child_by_field_name("consequence")
    alt = ternary.child_by_field_name("alternative")
    if cond is None or cons is None or alt is None:
        return None

    return (
        var_name,
        ctx.source_text(cond),
        ctx.source_text(cons),
        ctx.source_text(alt),
    )


# ---------------------------------------------------------------------------
# Condition negation (shared logic with bool_return_expr)
# ---------------------------------------------------------------------------

_INVERSIONS = {
    b"<": b">=", b">": b"<=", b"<=": b">", b">=": b"<",
    b"==": b"!=", b"!=": b"==",
}


def _negate_condition_bytes(node: Node, source: bytes) -> bytes:
    """Negate a condition expression, preferring operator inversion."""
    text = source[node.start_byte:node.end_byte]

    # Already negated: !!x -> x
    if node.type == "unary_expression":
        op = node.child_by_field_name("operator")
        if op is not None and op.text == b"!":
            arg = node.child_by_field_name("argument")
            if arg is not None:
                return source[arg.start_byte:arg.end_byte]

    # Binary comparison: invert operator
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op is not None and op.text in _INVERSIONS:
            return (
                source[node.start_byte:op.start_byte]
                + _INVERSIONS[op.text]
                + source[op.end_byte:node.end_byte]
            )

    # Fallback: wrap with !()
    return b"!(" + text + b")"


# ---------------------------------------------------------------------------
# Polarity-flipped ternary variants
# ---------------------------------------------------------------------------

def _if_to_ternary_flipped(node: Node, ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Convert if/else with single assignments to ternary with negated condition.

    if (cond) { x = a; } else { x = b; }  ->  x = !cond ? b : a;
    """
    if node.type != "if_statement":
        for child in node.children:
            yield from _if_to_ternary_flipped(child, ctx, counter)
        return

    condition = node.child_by_field_name("condition")
    consequence = node.child_by_field_name("consequence")
    alternative = node.child_by_field_name("alternative")

    if condition is None or consequence is None or alternative is None:
        return

    cons_assign = _extract_single_assignment(consequence)
    if cons_assign is None:
        return

    alt_body = None
    for child in alternative.children:
        if child.type == "compound_statement":
            alt_body = child
            break
        elif child.type == "if_statement":
            return

    if alt_body is None:
        return

    alt_assign = _extract_single_assignment(alt_body)
    if alt_assign is None:
        return

    cons_var, cons_val = cons_assign
    alt_var, alt_val = alt_assign

    if cons_var != alt_var:
        return

    # Get the inner condition node for negation
    inner_cond = None
    for child in condition.named_children:
        if child.type != "comment":
            inner_cond = child
            break
    if inner_cond is None:
        return

    negated = _negate_condition_bytes(inner_cond, ctx.file_source)

    # Build ternary with flipped branches: var = !cond ? alt_val : cons_val;
    indent = get_indent(ctx.file_source, node)
    ternary = (
        indent + cons_var.encode("utf-8")
        + b" = " + negated
        + b" ? " + alt_val.encode("utf-8")
        + b" : " + cons_val.encode("utf-8")
        + b";"
    )

    source = ctx.file_source
    new_source = source[:node.start_byte] + ternary + source[node.end_byte:]

    yield Variant(
        name=f"ternary_{counter}",
        pattern_name="ternary_swap",
        description=f"if/else -> ternary (polarity flip): {cons_var} = !cond ? ... : ...",
        source=new_source,
    )


def _if_return_to_ternary_flipped(node: Node, ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Convert if/else return to ternary with negated condition.

    if (cond) { return a; } else { return b; }  ->  return !cond ? b : a;
    """
    if node.type != "if_statement":
        for child in node.children:
            yield from _if_return_to_ternary_flipped(child, ctx, counter)
        return

    condition = node.child_by_field_name("condition")
    consequence = node.child_by_field_name("consequence")
    alternative = node.child_by_field_name("alternative")

    if condition is None or consequence is None or alternative is None:
        return

    cons_ret = _extract_single_return(consequence)
    if cons_ret is None:
        return

    alt_body = None
    for child in alternative.children:
        if child.type == "compound_statement":
            alt_body = child
            break
        elif child.type == "if_statement":
            return

    if alt_body is None:
        return

    alt_ret = _extract_single_return(alt_body)
    if alt_ret is None:
        return

    inner_cond = None
    for child in condition.named_children:
        if child.type != "comment":
            inner_cond = child
            break
    if inner_cond is None:
        return

    negated = _negate_condition_bytes(inner_cond, ctx.file_source)

    indent = get_indent(ctx.file_source, node)
    ternary = (
        indent + b"return " + negated
        + b" ? " + alt_ret.encode("utf-8")
        + b" : " + cons_ret.encode("utf-8")
        + b";"
    )

    source = ctx.file_source
    new_source = source[:node.start_byte] + ternary + source[node.end_byte:]

    yield Variant(
        name=f"ternary_{counter}",
        pattern_name="ternary_swap",
        description="if/else return -> return ternary (polarity flip)",
        source=new_source,
    )


# ---------------------------------------------------------------------------
# Bare if/return (no else clause)
# ---------------------------------------------------------------------------

def _bare_if_return_to_ternary(
    stmts: list[Node], ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Convert bare if (cond) return a; return b; -> return cond ? a : b;

    Handles consecutive statements where if has no else clause.
    """
    for i in range(len(stmts) - 1):
        if_stmt = stmts[i]
        next_stmt = stmts[i + 1]

        if if_stmt.type != "if_statement":
            continue
        if next_stmt.type != "return_statement":
            continue

        condition = if_stmt.child_by_field_name("condition")
        consequence = if_stmt.child_by_field_name("consequence")
        alternative = if_stmt.child_by_field_name("alternative")

        # Must have NO else clause
        if condition is None or consequence is None or alternative is not None:
            continue

        cons_ret = _extract_return_value(consequence)
        if cons_ret is None:
            continue

        # Get the return value from the next statement
        next_ret_val = None
        for child in next_stmt.named_children:
            if child.type != "comment":
                next_ret_val = child.text.decode("utf-8") if child.text else None
                break
        if next_ret_val is None:
            continue

        cond_expr = _get_condition_text(condition, ctx)
        if cond_expr is None:
            continue

        source = ctx.file_source
        indent = get_indent(source, if_stmt)

        # return cond ? cons_ret : next_ret;
        ternary = (
            indent + b"return " + cond_expr.encode("utf-8")
            + b" ? " + cons_ret.encode("utf-8")
            + b" : " + next_ret_val.encode("utf-8")
            + b";"
        )

        new_source = source[:if_stmt.start_byte] + ternary + source[next_stmt.end_byte:]

        yield Variant(
            name=f"ternary_{counter}",
            pattern_name="ternary_swap",
            description="bare if/return -> return ternary",
            source=new_source,
        )
        counter += 1
