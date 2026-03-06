"""Early return merge pattern — combine guard returns into || chain (and reverse).

Multiple `if (cond) return false;` statements generate redundant branch
sequences. Combining them into a single `if (c1 || c2 || c3) return false;`
shares the return target.

Also does the reverse: splits a || chain into sequential guard returns.

Also handles guard-to-conjunction collapse:
    if (!cond) return false; return expr;  ->  return cond && expr;
This was proven to fix MetaPanel::IsLoaded (beq/bne inversions).

Example:
    if (s < f.front) return false;
    if (s < f.back) return false;
    ->
    if (s < f.front || s < f.back) return false;
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


class EarlyReturnMergePattern(Pattern):
    name = "early_return_merge"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        if diagnosis.clusters:
            return True
        # Structural replace mismatches indicate branch structure differences
        if diagnosis.replace_real > 0:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Multiple branch diffs + clusters suggest redundant guard branches
        branch_count = sum(
            1 for d in diagnosis.diff_ops
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES
        )
        if branch_count >= 3 and len(diagnosis.clusters) >= 2:
            return 0.6
        if diagnosis.clusters:
            return 0.3
        return 0.15

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source
        stmts = ctx.statements

        # Ghidra-guided: only generate the direction(s) that match Ghidra's structure
        if ctx.ghidra_ast is not None:
            for variant in self._try_ghidra_guided(ctx, counter):
                yield variant
                counter += 1
            if counter > 0:
                return  # Ghidra guided produced candidates, skip blind

        # Direction 1: Merge consecutive guard returns into || chain
        for variant in _merge_guard_returns(stmts, source, counter):
            yield variant
            counter += 1

        # Direction 2: Split || chain in guard return into separate returns
        for variant in _split_guard_returns(stmts, ctx, counter):
            yield variant
            counter += 1

        # Direction 3: Collapse guard return + final return into && conjunction
        for variant in _guard_to_conjunction(stmts, source, counter):
            yield variant
            counter += 1

        # Direction 4: Expand && conjunction into guard return + final return
        for variant in _conjunction_to_guard(stmts, source, counter):
            yield variant
            counter += 1

    def _try_ghidra_guided(self, ctx: FunctionContext, start_counter: int) -> Iterator[Variant]:
        """Generate only the direction(s) indicated by Ghidra's condition structure."""
        from ..ghidra_ast import extract_condition_structure

        tags = extract_condition_structure(ctx.ghidra_ast)

        source = ctx.file_source
        stmts = ctx.statements
        counter = start_counter

        has_conjunction = "conjunction" in tags
        has_disjunction = "disjunction" in tags
        has_guard_return = "guard_return" in tags
        has_guard_false = "guard_return_false" in tags

        # Check what the source currently has
        source_has_guards = any(
            _extract_guard_return(s, source) is not None for s in stmts
        )
        source_has_or_chain = any(
            s.type == "if_statement" and _stmt_has_or_condition(s, source)
            for s in stmts
        )
        source_has_and_return = any(
            s.type == "return_statement" and _stmt_has_logical_return(s, source, b"&&")
            for s in stmts
        )
        source_has_or_return = any(
            s.type == "return_statement" and _stmt_has_logical_return(s, source, b"||")
            for s in stmts
        )

        generated = False

        if tags:
            # Ghidra has conjunction + source has guard returns -> guard_to_conjunction
            if has_conjunction and source_has_guards:
                for variant in _guard_to_conjunction(stmts, source, counter):
                    yield _tag_variant(variant, counter, "ghidra_retmerge")
                    counter += 1
                    generated = True

            # Ghidra has guard_return + source has && return -> conjunction_to_guard
            if has_guard_return and (source_has_and_return or source_has_or_return):
                for variant in _conjunction_to_guard(stmts, source, counter):
                    yield _tag_variant(variant, counter, "ghidra_retmerge")
                    counter += 1
                    generated = True

            # Ghidra has disjunction + source has separate guards -> merge_guard_returns
            if has_disjunction and source_has_guards and not source_has_or_chain:
                for variant in _merge_guard_returns(stmts, source, counter):
                    yield _tag_variant(variant, counter, "ghidra_retmerge")
                    counter += 1
                    generated = True

            # Ghidra has separate guards + source has || chain -> split_guard_returns
            if has_guard_return and not has_disjunction and source_has_or_chain:
                for variant in _split_guard_returns(stmts, ctx, counter):
                    yield _tag_variant(variant, counter, "ghidra_retmerge")
                    counter += 1
                    generated = True

        # If no signal from condition_structure, try CF skeleton
        if not generated:
            from ..ghidra_ast import extract_control_flow_skeleton

            skeleton = extract_control_flow_skeleton(ctx.ghidra_ast)
            if skeleton:
                guard_pairs = sum(
                    1 for i in range(len(skeleton) - 1)
                    if skeleton[i] == "if" and skeleton[i + 1] == "return"
                )

                # Ghidra shows guard pattern + source has || chains -> split
                if guard_pairs >= 2 and source_has_or_chain:
                    for variant in _split_guard_returns(stmts, ctx, counter):
                        yield _tag_variant(variant, counter, "ghidra_skeleton")
                        counter += 1

                # Ghidra shows few guards + source has many guards -> merge
                elif guard_pairs <= 1 and source_has_guards:
                    for variant in _merge_guard_returns(stmts, source, counter):
                        yield _tag_variant(variant, counter, "ghidra_skeleton")
                        counter += 1


def _merge_guard_returns(
    stmts: list[Node], source: bytes, counter: int
) -> Iterator[Variant]:
    """Find consecutive if (cond) return X; and merge into || chain."""
    # Find runs of consecutive guard returns
    i = 0
    while i < len(stmts):
        run_start = i
        return_value = None
        conditions = []

        while i < len(stmts):
            guard = _extract_guard_return(stmts[i], source)
            if guard is None:
                break
            cond_text, ret_text = guard
            if return_value is None:
                return_value = ret_text
            elif ret_text != return_value:
                break
            conditions.append((stmts[i], cond_text))
            i += 1

        if len(conditions) >= 2:
            # Merge all conditions with ||
            first_stmt = conditions[0][0]
            last_stmt = conditions[-1][0]
            indent = get_indent(source, first_stmt)

            merged_cond = b" || ".join(c for _, c in conditions)
            merged = indent + b"if (" + merged_cond + b")\n" + indent + b"    return " + return_value + b";"

            new_source = (
                source[:first_stmt.start_byte]
                + merged
                + source[last_stmt.end_byte:]
            )
            yield Variant(
                name=f"retmerge_{counter}",
                pattern_name="early_return_merge",
                description=f"Merge {len(conditions)} guard returns into || chain",
                source=new_source,
            )
            counter += 1

        if not conditions:
            i += 1


def _split_guard_returns(
    stmts: list[Node], ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Find if (a || b || c) return X; and split into separate guard returns."""
    source = ctx.file_source

    for stmt in stmts:
        if stmt.type != "if_statement":
            continue

        condition = stmt.child_by_field_name("condition")
        consequence = stmt.child_by_field_name("consequence")
        alternative = stmt.child_by_field_name("alternative")

        if condition is None or consequence is None:
            continue
        if alternative is not None:
            continue

        # Check consequence is a return statement
        ret_stmt = _get_sole_return(consequence)
        if ret_stmt is None:
            # Could also be a bare return (no compound_statement)
            if consequence.type == "return_statement":
                ret_stmt = consequence
            else:
                continue

        ret_text = _get_return_value(ret_stmt, source)
        if ret_text is None:
            continue

        # Check condition contains ||
        inner = _get_inner_expr(condition)
        if inner is None:
            continue

        # Collect all || operands
        operands = _collect_or_operands(inner, source)
        if len(operands) < 2:
            continue

        indent = get_indent(source, stmt)
        parts = []
        for op_text in operands:
            parts.append(indent + b"if (" + op_text + b")\n" + indent + b"    return " + ret_text + b";")

        new_source = (
            source[:stmt.start_byte]
            + b"\n".join(parts)
            + source[stmt.end_byte:]
        )
        yield Variant(
            name=f"retmerge_{counter}",
            pattern_name="early_return_merge",
            description=f"Split || chain into {len(operands)} separate guard returns",
            source=new_source,
        )
        counter += 1


def _extract_guard_return(stmt: Node, source: bytes) -> tuple[bytes, bytes] | None:
    """Extract (condition_text, return_value_text) from `if (cond) return val;`."""
    if stmt.type != "if_statement":
        return None

    condition = stmt.child_by_field_name("condition")
    consequence = stmt.child_by_field_name("consequence")
    alternative = stmt.child_by_field_name("alternative")

    if condition is None or consequence is None:
        return None
    if alternative is not None:
        return None

    # Get return statement from consequence
    ret_stmt = None
    if consequence.type == "return_statement":
        ret_stmt = consequence
    elif consequence.type == "compound_statement":
        ret_stmt = _get_sole_return(consequence)
    if ret_stmt is None:
        return None

    ret_text = _get_return_value(ret_stmt, source)
    if ret_text is None:
        return None

    inner = _get_inner_expr(condition)
    if inner is None:
        return None

    cond_text = source[inner.start_byte:inner.end_byte]
    return cond_text, ret_text


def _get_sole_return(compound_stmt: Node) -> Node | None:
    """Get single return statement from compound_statement."""
    if compound_stmt.type != "compound_statement":
        return None
    stmts = [c for c in compound_stmt.named_children if c.type != "comment"]
    if len(stmts) == 1 and stmts[0].type == "return_statement":
        return stmts[0]
    return None


def _get_return_value(ret_stmt: Node, source: bytes) -> bytes | None:
    """Get the return value text from a return_statement."""
    for child in ret_stmt.named_children:
        if child.type != "comment":
            return source[child.start_byte:child.end_byte]
    return None


def _get_inner_expr(condition: Node) -> Node | None:
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None


def _collect_or_operands(node: Node, source: bytes) -> list[bytes]:
    """Recursively collect operands of || chain."""
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op is not None and op.text == b"||":
            left = node.child_by_field_name("left")
            right = node.child_by_field_name("right")
            result = []
            if left:
                result.extend(_collect_or_operands(left, source))
            if right:
                result.extend(_collect_or_operands(right, source))
            return result
    return [source[node.start_byte:node.end_byte]]


def _negate_condition(cond_text: bytes) -> bytes:
    """Negate a condition expression, stripping double negation."""
    stripped = cond_text.strip()
    # !(expr) -> expr
    if stripped.startswith(b"!(") and stripped.endswith(b")"):
        inner = stripped[2:-1]
        # Check balanced parens
        depth = 0
        for ch in inner:
            if ch == ord(b"("):
                depth += 1
            elif ch == ord(b")"):
                depth -= 1
            if depth < 0:
                break
        if depth == 0:
            return inner
    # !expr -> expr (simple identifier)
    if stripped.startswith(b"!") and not stripped.startswith(b"!="):
        return stripped[1:]
    # expr -> !(expr)
    return b"!(" + cond_text + b")"


def _guard_to_conjunction(
    stmts: list[Node], source: bytes, counter: int
) -> Iterator[Variant]:
    """Collapse `if (!cond) return false; return expr;` into `return cond && expr;`.

    Also handles the positive case: `if (cond) return false; return expr;`
    becomes `return !cond && expr;`.
    """
    for i in range(len(stmts) - 1):
        guard = stmts[i]
        final_ret = stmts[i + 1]

        if guard.type != "if_statement" or final_ret.type != "return_statement":
            continue

        # Guard must not have an else clause
        alternative = guard.child_by_field_name("alternative")
        if alternative is not None:
            continue

        # Guard consequence must be a return false/true
        condition = guard.child_by_field_name("condition")
        consequence = guard.child_by_field_name("consequence")
        if condition is None or consequence is None:
            continue

        guard_ret = _extract_guard_return(guard, source)
        if guard_ret is None:
            continue
        guard_cond, guard_ret_val = guard_ret

        # Only handle return false/true (boolean guards)
        guard_ret_stripped = guard_ret_val.strip()
        if guard_ret_stripped not in (b"false", b"true", b"0", b"1"):
            continue

        # Get the final return expression
        final_ret_val = _get_return_value(final_ret, source)
        if final_ret_val is None:
            continue

        indent = get_indent(source, guard)

        if guard_ret_stripped in (b"false", b"0"):
            # if (cond) return false; return expr; -> return !cond && expr;
            negated = _negate_condition(guard_cond)
            new_ret = indent + b"return " + negated + b" && " + final_ret_val + b";"
        else:
            # if (cond) return true; return expr; -> return cond || expr;
            new_ret = indent + b"return " + guard_cond + b" || " + final_ret_val + b";"

        new_source = (
            source[:guard.start_byte]
            + new_ret
            + source[final_ret.end_byte:]
        )
        yield Variant(
            name=f"retmerge_{counter}",
            pattern_name="early_return_merge",
            description="Collapse guard return into && conjunction",
            source=new_source,
        )
        counter += 1


def _conjunction_to_guard(
    stmts: list[Node], source: bytes, counter: int
) -> Iterator[Variant]:
    """Expand `return cond && expr;` into `if (!cond) return false; return expr;`.

    The reverse of guard-to-conjunction.
    """
    for stmt in stmts:
        if stmt.type != "return_statement":
            continue

        # Get the return expression
        ret_expr = None
        for child in stmt.named_children:
            if child.type != "comment":
                ret_expr = child
                break
        if ret_expr is None:
            continue

        # Check if it's a && expression
        if ret_expr.type != "binary_expression":
            continue
        op = ret_expr.child_by_field_name("operator")
        if op is None or op.text not in (b"&&", b"||"):
            continue

        left = ret_expr.child_by_field_name("left")
        right = ret_expr.child_by_field_name("right")
        if left is None or right is None:
            continue

        left_text = source[left.start_byte:left.end_byte]
        right_text = source[right.start_byte:right.end_byte]
        indent = get_indent(source, stmt)

        if op.text == b"&&":
            # return A && B; -> if (!A) return false; return B;
            negated = _negate_condition(left_text)
            new_text = (
                indent + b"if (" + negated + b")\n"
                + indent + b"    return false;\n"
                + indent + b"return " + right_text + b";"
            )
        else:
            # return A || B; -> if (A) return true; return B;
            new_text = (
                indent + b"if (" + left_text + b")\n"
                + indent + b"    return true;\n"
                + indent + b"return " + right_text + b";"
            )

        new_source = (
            source[:stmt.start_byte]
            + new_text
            + source[stmt.end_byte:]
        )
        yield Variant(
            name=f"retmerge_{counter}",
            pattern_name="early_return_merge",
            description=f"Expand {op.text.decode()} into guard return + final return",
            source=new_source,
        )
        counter += 1


def _stmt_has_or_condition(stmt: Node, source: bytes) -> bool:
    """Check if an if_statement has a || in its condition."""
    condition = stmt.child_by_field_name("condition")
    if condition is None:
        return False
    inner = _get_inner_expr(condition)
    if inner is None:
        return False
    return _node_has_op(inner, b"||")


def _stmt_has_logical_return(stmt: Node, source: bytes, op: bytes) -> bool:
    """Check if a return_statement returns a logical expression with the given operator."""
    for child in stmt.named_children:
        if child.type == "comment":
            continue
        if child.type == "binary_expression":
            op_node = child.child_by_field_name("operator")
            if op_node is not None and op_node.text == op:
                return True
        break
    return False


def _node_has_op(node: Node, op: bytes) -> bool:
    """Check if node contains a binary expression with the given operator."""
    if node.type == "binary_expression":
        op_node = node.child_by_field_name("operator")
        if op_node is not None and op_node.text == op:
            return True
    for child in node.children:
        if _node_has_op(child, op):
            return True
    return False


def _tag_variant(variant: Variant, counter: int, prefix: str) -> Variant:
    """Re-tag a variant with a ghidra prefix."""
    return Variant(
        name=f"{prefix}_{counter}",
        pattern_name=variant.pattern_name,
        description=f"[ghidra] {variant.description}",
        source=variant.source,
    )
