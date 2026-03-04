"""Max/Min to conditional pattern — replace Max()/Min() calls with if-statements.

The compiler generates different code for `Max(a, b)` (function call or
inline fsel) vs `if (a < b) a = b` (explicit branch). Also tries the
reverse direction: explicit conditional to Min()/Max()/Clamp() templates.

Example:
    i1 = Max(i1, 1);
    ->
    if (i1 < 1) i1 = 1;

    // or reverse:
    if (val < 0.0f) val = 0.0f;
    ->
    val = Max(val, 0.0f);
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent
from ..types import Diagnosis, FunctionContext, Variant

_BRANCH_OPCODES = {"beq", "bne", "ble", "bgt", "bge", "blt",
                   "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
                   "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-"}

# Function names to expand
_MAX_NAMES = {b"Max", b"std::max", b"max"}
_MIN_NAMES = {b"Min", b"std::min", b"min"}


class MaxToConditionalPattern(Pattern):
    name = "max_to_conditional"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Relevant when there are branch diffs or clusters
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
            # Also for fsel mismatches
            if "fsel" in d.target_opcode or "fsel" in d.base_opcode:
                return True
        return bool(diagnosis.clusters)

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Strong: fsel in diff — Max/Min directly generates fsel
        for d in diagnosis.diff_ops:
            if "fsel" in d.target_opcode or "fsel" in d.base_opcode:
                return 0.8
        # Medium: branch diffs + clusters — could be Max/Min expansion
        if diagnosis.clusters:
            return 0.3
        return 0.15

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source

        for stmt in ctx.statements:
            # Direction 1: Max(a, b) -> if (a < b) a = b;
            for variant in _expand_max_min(stmt, ctx, counter):
                yield variant
                counter += 1

            # Direction 2: if (x < y) x = y -> x = Max(x, y)
            for variant in _collapse_to_max_min(stmt, ctx, counter):
                yield variant
                counter += 1


def _expand_max_min(
    stmt: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Find Max(a, b) / Min(a, b) calls and expand to if-statements."""
    source = ctx.file_source

    for node in walk(stmt):
        if node.type != "call_expression":
            continue

        func = node.child_by_field_name("function")
        args = node.child_by_field_name("arguments")
        if func is None or args is None:
            continue

        func_name = func.text
        is_max = func_name in _MAX_NAMES
        is_min = func_name in _MIN_NAMES
        if not is_max and not is_min:
            continue

        # Get exactly 2 arguments
        arg_nodes = [c for c in args.named_children if c.type != "comment"]
        if len(arg_nodes) != 2:
            continue

        arg1 = source[arg_nodes[0].start_byte:arg_nodes[0].end_byte]
        arg2 = source[arg_nodes[1].start_byte:arg_nodes[1].end_byte]

        # Find containing assignment: var = Max(a, b)
        parent = node.parent
        if parent is None:
            continue

        if parent.type == "assignment_expression":
            lhs = parent.child_by_field_name("left")
            if lhs is None:
                continue
            lhs_text = source[lhs.start_byte:lhs.end_byte]

            # Find the expression_statement containing this
            grandparent = parent.parent
            if grandparent is None or grandparent.type != "expression_statement":
                continue

            indent = get_indent(source, grandparent)

            if is_max:
                # var = Max(a, b) -> if (a < b) var = b; else var = a;
                # Simpler: if arg1 is lhs, use: if (lhs < arg2) lhs = arg2;
                if arg1 == lhs_text:
                    new_text = b"if (" + arg1 + b" < " + arg2 + b") " + lhs_text + b" = " + arg2 + b";"
                elif arg2 == lhs_text:
                    new_text = b"if (" + arg2 + b" < " + arg1 + b") " + lhs_text + b" = " + arg1 + b";"
                else:
                    # General case
                    new_text = (
                        b"if (" + arg1 + b" < " + arg2 + b") " + lhs_text + b" = " + arg2 + b";\n"
                        + indent + b"else " + lhs_text + b" = " + arg1 + b";"
                    )
            else:
                # var = Min(a, b) -> if (a > b) var = b; else var = a;
                if arg1 == lhs_text:
                    new_text = b"if (" + arg1 + b" > " + arg2 + b") " + lhs_text + b" = " + arg2 + b";"
                elif arg2 == lhs_text:
                    new_text = b"if (" + arg2 + b" > " + arg1 + b") " + lhs_text + b" = " + arg1 + b";"
                else:
                    new_text = (
                        b"if (" + arg1 + b" > " + arg2 + b") " + lhs_text + b" = " + arg2 + b";\n"
                        + indent + b"else " + lhs_text + b" = " + arg1 + b";"
                    )

            new_source = (
                source[:grandparent.start_byte]
                + new_text
                + source[grandparent.end_byte:]
            )
            yield Variant(
                name=f"maxcond_{counter}",
                pattern_name="max_to_conditional",
                description=f"Expand {func_name.decode()}({arg1.decode(errors='replace')}, {arg2.decode(errors='replace')}) to if-statement",
                source=new_source,
            )
            counter += 1

        elif parent.type == "init_declarator":
            # Type var = Max(a, b) -> Type var; if (a < b) var = b; else var = a;
            decl_stmt = parent.parent
            if decl_stmt is None or decl_stmt.type != "declaration":
                continue

            # Get the type specifier
            type_node = None
            for child in decl_stmt.children:
                if child.type in ("type_identifier", "primitive_type", "sized_type_specifier"):
                    type_node = child
                    break
            if type_node is None:
                continue

            # Get var name
            var_name = None
            for child in parent.children:
                if child.type == "identifier":
                    var_name = child.text
                    break
            if var_name is None:
                continue

            type_text = source[type_node.start_byte:type_node.end_byte]
            indent = get_indent(source, decl_stmt)

            if is_max:
                cond_text = (
                    type_text + b" " + var_name + b";\n"
                    + indent + b"if (" + arg1 + b" < " + arg2 + b") " + var_name + b" = " + arg2 + b";\n"
                    + indent + b"else " + var_name + b" = " + arg1 + b";"
                )
            else:
                cond_text = (
                    type_text + b" " + var_name + b";\n"
                    + indent + b"if (" + arg1 + b" > " + arg2 + b") " + var_name + b" = " + arg2 + b";\n"
                    + indent + b"else " + var_name + b" = " + arg1 + b";"
                )

            new_source = (
                source[:decl_stmt.start_byte]
                + cond_text
                + source[decl_stmt.end_byte:]
            )
            yield Variant(
                name=f"maxcond_{counter}",
                pattern_name="max_to_conditional",
                description=f"Expand {func_name.decode()} init to if-statement for {var_name.decode()}",
                source=new_source,
            )
            counter += 1


def _collapse_to_max_min(
    stmt: Node, ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Find if (a < b) a = b patterns and collapse to Max(a, b) / Min(a, b)."""
    source = ctx.file_source

    for node in walk(stmt):
        if node.type != "if_statement":
            continue

        condition = node.child_by_field_name("condition")
        consequence = node.child_by_field_name("consequence")
        alternative = node.child_by_field_name("alternative")

        if condition is None or consequence is None:
            continue
        # Only simple ifs without else (or with trivial else)
        if alternative is not None:
            continue

        inner = _get_inner_expr(condition)
        if inner is None or inner.type != "binary_expression":
            continue

        op = inner.child_by_field_name("operator")
        left = inner.child_by_field_name("left")
        right = inner.child_by_field_name("right")
        if op is None or left is None or right is None:
            continue

        op_text = op.text
        if op_text not in (b"<", b">", b"<=", b">="):
            continue

        # Get the assignment in the consequence
        assign = _get_sole_assignment(consequence)
        if assign is None:
            continue

        assign_lhs = assign.child_by_field_name("left")
        assign_rhs = assign.child_by_field_name("right")
        if assign_lhs is None or assign_rhs is None:
            continue

        left_text = source[left.start_byte:left.end_byte]
        right_text = source[right.start_byte:right.end_byte]
        lhs_text = source[assign_lhs.start_byte:assign_lhs.end_byte]
        rhs_text = source[assign_rhs.start_byte:assign_rhs.end_byte]

        # Pattern: if (a < b) a = b  ->  a = Max(a, b)
        if op_text in (b"<", b"<=") and lhs_text == left_text and rhs_text == right_text:
            _yield_collapse(source, node, lhs_text, left_text, right_text, b"Max", counter)
            new_source = (
                source[:node.start_byte]
                + lhs_text + b" = Max(" + left_text + b", " + right_text + b");"
                + source[node.end_byte:]
            )
            yield Variant(
                name=f"maxcond_{counter}",
                pattern_name="max_to_conditional",
                description=f"Collapse if ({left_text.decode(errors='replace')} < ...) to Max()",
                source=new_source,
            )
            counter += 1

        # Pattern: if (a > b) a = b  ->  a = Min(a, b)
        elif op_text in (b">", b">=") and lhs_text == left_text and rhs_text == right_text:
            new_source = (
                source[:node.start_byte]
                + lhs_text + b" = Min(" + left_text + b", " + right_text + b");"
                + source[node.end_byte:]
            )
            yield Variant(
                name=f"maxcond_{counter}",
                pattern_name="max_to_conditional",
                description=f"Collapse if ({left_text.decode(errors='replace')} > ...) to Min()",
                source=new_source,
            )
            counter += 1


def _get_inner_expr(condition: Node) -> Node | None:
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None


def _get_sole_assignment(compound_stmt: Node) -> Node | None:
    """Get the single assignment expression from a compound_statement or bare statement."""
    if compound_stmt.type == "compound_statement":
        stmts = [c for c in compound_stmt.named_children if c.type != "comment"]
        if len(stmts) != 1:
            return None
        stmt = stmts[0]
    elif compound_stmt.type == "expression_statement":
        stmt = compound_stmt
    else:
        return None

    if stmt.type == "expression_statement":
        for child in stmt.named_children:
            if child.type == "assignment_expression":
                return child
    return None


def _yield_collapse(*args):
    """Placeholder — actual yield happens in caller."""
    pass
