"""ObjPtr/smart pointer extraction before boolean chains.

Win rate: untested (new pattern, but proven in 3 manual fixes).

When an ObjOwnerPtr<T> or ObjPtr<T> smart pointer is used in a boolean
&&-chain, the compiler generates cmpwi cr6 (signed comparison, deferred CR
field). Extracting the smart pointer into a raw T* local before the chain
causes the compiler to generate cmplwi cr0 (unsigned, immediate branch),
which often matches the target binary.

Transformations:
    if (mTex && mTex->Width()) { ... }
    ->
    RndTex *tex = mTex;
    if (tex && tex->Width()) { ... }

Also handles the related pattern where GetFoo() != nullptr should become
HasFoo() (pointer-to-bool generates cmplwi vs explicit !=nullptr generates
cmpwi).

Detection signals:
    - cmpwi vs cmplwi replace mismatches
    - CR field differences (cr0 vs cr6)
    - BOOL_MASK pattern flag
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, get_line_start
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Member naming convention (mFoo)
_MEMBER_RE = re.compile(r"^m[A-Z]")


class ObjPtrBoolExtractPattern(Pattern):
    name = "objptr_bool_extract"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # cmpwi vs cmplwi mismatches (signed vs unsigned null check)
        for d in diagnosis.diff_ops:
            if (d.target_opcode == "cmplwi" and d.base_opcode == "cmpwi") or \
               (d.target_opcode == "cmpwi" and d.base_opcode == "cmplwi"):
                return True

        # CR field differences
        if diagnosis.replace_real > 0:
            return True

        # Boolean negation pattern
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("beq", "bne") and d.base_opcode in ("beq", "bne"):
                return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        for d in diagnosis.diff_ops:
            if (d.target_opcode == "cmplwi" and d.base_opcode == "cmpwi") or \
               (d.target_opcode == "cmpwi" and d.base_opcode == "cmplwi"):
                return 0.8
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        # Find if-statements with && chains that use member variables
        for variant in _extract_bool_chain_members(ctx, source, body, counter):
            yield variant
            counter += 1
            if counter >= 8:
                return


def _extract_bool_chain_members(
    ctx: FunctionContext, source: bytes, body: Node, counter: int
) -> Iterator[Variant]:
    """Find && chains using member variables and extract them to raw pointers."""
    stmts = ctx.statements

    # Strategy 1: Find && chains in declarations (bool b = mTex && mTex->Width())
    for i, stmt in enumerate(stmts):
        if counter >= 8:
            break

        if stmt.type == "declaration":
            for variant in _handle_bool_decl(stmt, body, source, counter):
                yield variant
                counter += 1
                if counter >= 8:
                    return

    # Strategy 2: Find && chains in if-statement conditions
    for i, stmt in enumerate(stmts):
        if counter >= 8:
            break

        if_nodes = _find_if_statements(stmt)
        for if_node in if_nodes:
            if counter >= 8:
                break

            # Try both field names (tree-sitter grammar versions differ)
            condition = if_node.child_by_field_name("condition")
            if condition is None:
                # Newer grammar uses condition_clause
                for child in if_node.children:
                    if child.type in ("condition_clause", "parenthesized_expression"):
                        condition = child
                        break
            if condition is None:
                continue

            # Get the actual condition expression (unwrap parens/condition_clause)
            cond_expr = condition
            while cond_expr.type in ("parenthesized_expression", "condition_clause"):
                if cond_expr.named_child_count == 1:
                    cond_expr = cond_expr.named_children[0]
                else:
                    break

            # Look for && chains with member variables as first operand
            if cond_expr.type != "binary_expression":
                continue

            op_node = cond_expr.child_by_field_name("operator")
            if op_node is None:
                continue
            op_text = source[op_node.start_byte:op_node.end_byte]
            if op_text != b"&&":
                continue

            left = cond_expr.child_by_field_name("left")
            if left is None:
                continue

            # The left operand of && should be a member variable (mFoo)
            left_text = source[left.start_byte:left.end_byte].decode("utf-8", errors="replace")
            if not _MEMBER_RE.match(left_text):
                continue

            # Check if this member is used with -> in the right side (mFoo->Method())
            # This confirms it's actually a pointer/smart pointer, not a bool/int
            right = cond_expr.child_by_field_name("right")
            if right is None:
                continue

            right_text = source[right.start_byte:right.end_byte].decode("utf-8", errors="replace")
            if (left_text + "->") not in right_text:
                continue

            # Build variant: extract member into raw pointer local
            var_name = f"_ptr{counter}"
            top_stmt = _get_top_stmt(if_node, body) or stmt
            indent = get_indent(source, top_stmt)
            line_start = get_line_start(source, top_stmt)

            member_bytes = left_text.encode("utf-8")
            var_bytes = var_name.encode("utf-8")

            for decl_fmt, desc_suffix in [
                (f"auto *{var_name} = {left_text};\n", "auto*"),
                (f"auto *{var_name} = {left_text}.Ptr();\n", ".Ptr()"),
            ]:
                if counter >= 8:
                    break

                ed = SourceEditor(source)
                ed.insert_at(line_start, indent + decl_fmt.encode("utf-8"))
                _replace_member_in_condition(ed, condition, source, member_bytes, var_bytes)

                consequence = if_node.child_by_field_name("consequence")
                if consequence is not None:
                    _replace_member_in_subtree(ed, consequence, source, member_bytes, var_bytes)

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                yield Variant(
                    name=f"ptrext_{counter}",
                    pattern_name="objptr_bool_extract",
                    description=f"Extract {left_text} via {desc_suffix} before && chain",
                    source=new_source,
                )
                counter += 1


def _handle_bool_decl(
    stmt: Node, body: Node, source: bytes, counter: int
) -> Iterator[Variant]:
    """Handle bool declarations with && chains: bool b = (mTex && mTex->Width())."""
    # Find init_declarator with && in the value
    init_decls = [c for c in stmt.named_children if c.type == "init_declarator"]
    if len(init_decls) != 1:
        return

    value = init_decls[0].child_by_field_name("value")
    if value is None:
        return

    # Unwrap parenthesized expression
    expr = value
    while expr.type == "parenthesized_expression" and expr.named_child_count == 1:
        expr = expr.named_children[0]

    # Must be a && chain
    if expr.type != "binary_expression":
        return
    op_node = expr.child_by_field_name("operator")
    if op_node is None:
        return
    if source[op_node.start_byte:op_node.end_byte] != b"&&":
        return

    # Get leftmost operand of the && chain
    left = expr
    while left.type == "binary_expression":
        inner_op = left.child_by_field_name("operator")
        if inner_op and source[inner_op.start_byte:inner_op.end_byte] == b"&&":
            left = left.child_by_field_name("left")
            if left is None:
                return
        else:
            break

    left_text = source[left.start_byte:left.end_byte].decode("utf-8", errors="replace")
    if not _MEMBER_RE.match(left_text):
        return

    # Check if the member is used with -> in the expression (confirms it's a pointer)
    full_expr_text = source[expr.start_byte:expr.end_byte].decode("utf-8", errors="replace")
    if (left_text + "->") not in full_expr_text:
        return

    # Build variant: insert raw pointer extraction before this statement
    var_name = f"_ptr{counter}"
    indent = get_indent(source, stmt)
    line_start = get_line_start(source, stmt)

    member_bytes = left_text.encode("utf-8")
    var_bytes = var_name.encode("utf-8")

    # Try multiple type forms: auto*, decltype, and Ptr()
    # auto* doesn't work with smart pointers, so also try .Ptr() extraction
    type_variants = [
        (f"auto *{var_name} = {left_text};\n", f"auto* {left_text}"),
        (f"auto *{var_name} = {left_text}.Ptr();\n", f".Ptr() extraction"),
        (f"auto {var_name} = {left_text}.Ptr();\n", f".Ptr() no-pointer"),
    ]

    for decl_fmt, desc_suffix in type_variants:
        if counter >= 8:
            return

        ed = SourceEditor(source)
        decl_line = indent + decl_fmt.encode("utf-8")
        ed.insert_at(line_start, decl_line)

        _replace_member_in_condition(ed, value, source, member_bytes, var_bytes)

        # Also replace in subsequent statements that use this member
        stmt_idx = None
        stmts_list = [c for c in body.named_children]
        for j, s in enumerate(stmts_list):
            if s.id == stmt.id:
                stmt_idx = j
                break
        if stmt_idx is not None:
            for j in range(stmt_idx + 1, len(stmts_list)):
                _replace_member_in_subtree(ed, stmts_list[j], source, member_bytes, var_bytes)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        yield Variant(
            name=f"ptrext_{counter}",
            pattern_name="objptr_bool_extract",
            description=f"Extract {left_text} via {desc_suffix} before && chain",
            source=new_source,
        )
        counter += 1


def _find_if_statements(node: Node) -> list[Node]:
    """Find all if_statement nodes in a subtree."""
    results = []
    for n in walk(node):
        if n.type == "if_statement":
            results.append(n)
    return results


def _get_top_stmt(node: Node, body: Node) -> Node | None:
    """Walk up to find the direct child of body containing this node."""
    current = node
    while current is not None:
        if current.parent is not None and current.parent.id == body.id:
            return current
        current = current.parent
    return None


def _replace_member_in_condition(
    ed: SourceEditor, condition: Node, source: bytes,
    member: bytes, replacement: bytes
) -> None:
    """Replace all occurrences of member identifier in a condition."""
    for n in walk(condition):
        if n.type == "identifier" and source[n.start_byte:n.end_byte] == member:
            ed.replace_node(n, replacement)


def _replace_member_in_subtree(
    ed: SourceEditor, node: Node, source: bytes,
    member: bytes, replacement: bytes
) -> None:
    """Replace member uses in a subtree (e.g., if body)."""
    for n in walk(node):
        if n.type == "identifier" and source[n.start_byte:n.end_byte] == member:
            # Only replace if it's used as a standalone identifier or field access base
            parent = n.parent
            if parent is not None and parent.type == "field_expression":
                arg = parent.child_by_field_name("argument")
                if arg is not None and arg.id == n.id:
                    ed.replace_node(n, replacement)
            elif parent is not None and parent.type == "pointer_expression":
                ed.replace_node(n, replacement)
