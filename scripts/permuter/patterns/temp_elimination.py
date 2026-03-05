"""Temp variable elimination — inline single-use locals back into expressions.

Win rate: untested (new pattern).

When a local variable is declared, initialized, and used exactly once, try
eliminating it by substituting the initializer expression at the use site.
This changes register allocation (the value comes from a function return
register or memory load instead of a callee-saved spill), which can fix
commutative operand swaps and callee-saved register swaps.

Also handles Milo-specific iterator helper substitution:
    iterator it = container.end();
    result = --it;
    ->
    result = PrevItr(container.end());

Transformations:
    float norm = LimitAng(x - y);
    mAng = LimitAng(temp + norm);
    ->
    mAng = LimitAng(temp + LimitAng(x - y));

    float temp = mAng;
    result = Foo(temp + x);
    ->
    result = Foo(mAng + x);

Detection signals:
    - Commutative operand swaps (fadds f1,f0 vs f1,f0)
    - Callee-saved GPR/FPR swaps
    - Clusters (insert/delete groups from extra moves)
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, get_line_start
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Callee-saved register pattern
_CALLEE_SAVED_RE = re.compile(r"[rf](1[3-9]|2\d|3[01])")


class TempEliminationPattern(Pattern):
    name = "temp_elimination"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Commutative operand swaps — temp elimination changes operand source
        for d in diagnosis.diff_ops:
            if d.target_opcode == d.base_opcode and d.target_opcode in (
                "fadds", "fmuls", "fmadds", "fmsubs", "fnmsubs", "fnmadds",
                "add", "mullw",
            ):
                return True

        # Callee-saved register swaps
        for (r1, r2) in diagnosis.reg_swap_pairs:
            if _CALLEE_SAVED_RE.match(r1) or _CALLEE_SAVED_RE.match(r2):
                return True

        # Clusters suggest instruction reordering
        if diagnosis.clusters:
            return True

        # Prologue mismatch where target needs fewer vars
        if diagnosis.has_prologue_mismatch and diagnosis.gpr_save_delta < 0:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        base = 1.0 if self.relevant(diagnosis) else 0.0
        # Boost when prologue shows target needs fewer callee-saved regs
        if diagnosis.has_prologue_mismatch and diagnosis.gpr_save_delta < 0:
            base = min(1.0, base + 0.3)
        return base

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        stmts = ctx.statements
        counter = 0

        for i, stmt in enumerate(stmts):
            if counter >= 8:
                break

            # Find declaration statements: Type var = expr;
            decl_info = _extract_single_decl(stmt, source)
            if decl_info is None:
                continue

            var_name, init_expr, decl_start, decl_end = decl_info

            # Count uses of var_name in subsequent statements
            uses = []
            for j in range(i + 1, len(stmts)):
                uses.extend(_find_identifier_uses(stmts[j], var_name))

            # Only eliminate single-use temps
            if len(uses) != 1:
                continue

            use_node = uses[0]

            # Don't inline if the init_expr has side effects that would be
            # reordered past other side-effecting statements
            if _has_side_effects(stmt, source) and i + 1 < len(stmts):
                # Check if there are side-effecting statements between decl and use
                use_stmt_idx = None
                for j in range(i + 1, len(stmts)):
                    if _node_contains(stmts[j], use_node):
                        use_stmt_idx = j
                        break
                if use_stmt_idx is not None and use_stmt_idx > i + 1:
                    # Side effects between decl and use — skip
                    has_side_effects_between = False
                    for k in range(i + 1, use_stmt_idx):
                        if _has_side_effects(stmts[k], source):
                            has_side_effects_between = True
                            break
                    if has_side_effects_between:
                        continue

            # Build the variant: delete declaration, replace use with init_expr
            ed = SourceEditor(source)

            # Delete the declaration line (including trailing newline)
            del_end = decl_end
            while del_end < len(source) and source[del_end:del_end + 1] in (b"\n", b"\r"):
                del_end += 1

            # Also eat leading whitespace on the line
            del_start = decl_start
            while del_start > 0 and source[del_start - 1:del_start] in (b" ", b"\t"):
                del_start -= 1

            ed.delete_range(del_start, del_end)
            ed.replace_node(use_node, init_expr)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            var_str = var_name.decode("utf-8", errors="replace")
            init_str = init_expr.decode("utf-8", errors="replace")
            if len(init_str) > 40:
                init_str = init_str[:37] + "..."
            yield Variant(
                name=f"tmpelim_{counter}",
                pattern_name=self.name,
                description=f"Eliminate temp '{var_str}' = {init_str}",
                source=new_source,
            )
            counter += 1

        # Strategy 2: Iterator helper substitution (Milo-specific)
        # it = container.end(); result = --it; -> result = PrevItr(container.end());
        for variant in _iterator_helper_subst(ctx, source, stmts, counter):
            yield variant
            counter += 1

        # Strategy 3: Eliminate multiple consecutive temps at once
        for variant in _multi_eliminate(ctx, source, stmts, counter):
            yield variant
            counter += 1

        # Strategy 4: Eliminate multi-use value temps (2-3 uses, pure init expr)
        for variant in _multi_use_value_eliminate(ctx, source, stmts, counter):
            yield variant
            counter += 1


def _extract_single_decl(
    stmt: Node, source: bytes
) -> tuple[bytes, bytes, int, int] | None:
    """Extract (var_name, init_expr, start_byte, end_byte) from a declaration.

    Matches patterns like:
        float temp = expr;
        auto x = Foo();
        Type *p = bar;
    """
    if stmt.type != "declaration":
        return None

    # Find the declarator (should have exactly one init_declarator)
    init_decls = [c for c in stmt.named_children if c.type == "init_declarator"]
    if len(init_decls) != 1:
        return None

    init_decl = init_decls[0]
    declarator = init_decl.child_by_field_name("declarator")
    value = init_decl.child_by_field_name("value")

    if declarator is None or value is None:
        return None

    # Get the actual identifier name (unwrap pointer/reference declarators)
    name_node = declarator
    while name_node.type in ("pointer_declarator", "reference_declarator"):
        inner = name_node.child_by_field_name("declarator")
        if inner is None:
            inner = name_node.named_children[-1] if name_node.named_children else None
        if inner is None:
            break
        name_node = inner

    if name_node.type != "identifier" or name_node.text is None:
        return None

    var_name = name_node.text
    init_expr = source[value.start_byte:value.end_byte]

    return var_name, init_expr, stmt.start_byte, stmt.end_byte


def _find_identifier_uses(node: Node, name: bytes) -> list[Node]:
    """Find all uses of an identifier in a subtree."""
    results = []
    for n in walk(node):
        if n.type == "identifier" and n.text == name:
            # Exclude declaration sites
            parent = n.parent
            if parent is not None and parent.type == "init_declarator":
                decl = parent.child_by_field_name("declarator")
                if decl is not None and decl.id == n.id:
                    continue
            results.append(n)
    return results


def _node_contains(parent: Node, child: Node) -> bool:
    """Check if parent node contains child node by byte range."""
    return parent.start_byte <= child.start_byte and child.end_byte <= parent.end_byte


def _has_side_effects(stmt: Node, source: bytes) -> bool:
    """Heuristic: does a statement contain function calls or assignments?"""
    for n in walk(stmt):
        if n.type in ("call_expression", "assignment_expression",
                       "update_expression", "compound_assignment_expr"):
            return True
    return False


def _iterator_helper_subst(
    ctx: FunctionContext, source: bytes, stmts: list[Node], counter: int
) -> Iterator[Variant]:
    """Replace iterator decrement/increment patterns with PrevItr/NextItr helpers.

    Detects:
        Type it = container.end();    Type it = container.begin();
        result = --it;                result = ++it;
    Replaces with:
        result = PrevItr(container.end());
        result = NextItr(container.begin(), 1);
    """
    if counter >= 10:
        return

    for i in range(len(stmts) - 1):
        if counter >= 10:
            break

        stmt_a = stmts[i]
        stmt_b = stmts[i + 1]

        # stmt_a must be a declaration: Type it = expr;
        decl_info = _extract_single_decl(stmt_a, source)
        if decl_info is None:
            continue

        var_name, init_expr, decl_start, decl_end = decl_info

        # Check init_expr ends with .end() or .begin()
        init_str = init_expr.decode("utf-8", errors="replace")
        is_end = init_str.endswith(".end()")
        is_begin = init_str.endswith(".begin()")
        if not is_end and not is_begin:
            continue

        # stmt_b must use --it or ++it
        stmt_b_text = source[stmt_b.start_byte:stmt_b.end_byte].decode("utf-8", errors="replace")
        var_str = var_name.decode("utf-8", errors="replace")

        # Check for prefix decrement: --it used in assignment
        has_decrement = f"--{var_str}" in stmt_b_text
        has_increment = f"++{var_str}" in stmt_b_text

        if not has_decrement and not has_increment:
            continue

        # Verify the variable is only used once in stmt_b (in the --/++ expr)
        uses = _find_identifier_uses(stmt_b, var_name)
        if len(uses) != 1:
            continue

        # Also check it's not used anywhere else
        remaining_uses = []
        for j in range(i + 2, len(stmts)):
            remaining_uses.extend(_find_identifier_uses(stmts[j], var_name))
        if remaining_uses:
            continue

        # Build replacement
        if has_decrement and is_end:
            helper = f"PrevItr({init_str})"
        elif has_increment and is_begin:
            helper = f"NextItr({init_str})"
        else:
            # --begin() or ++end() doesn't make sense
            continue

        # Find the --it or ++it node and its parent expression
        # Replace --var with helper call, delete the declaration
        ed = SourceEditor(source)

        # Delete declaration line
        del_end = decl_end
        while del_end < len(source) and source[del_end:del_end + 1] in (b"\n", b"\r"):
            del_end += 1
        del_start = decl_start
        while del_start > 0 and source[del_start - 1:del_start] in (b" ", b"\t"):
            del_start -= 1
        ed.delete_range(del_start, del_end)

        # Find and replace the --var or ++var in stmt_b
        prefix_op = f"--{var_str}" if has_decrement else f"++{var_str}"
        # Find the update_expression node
        for n in walk(stmt_b):
            if n.type == "update_expression":
                n_text = source[n.start_byte:n.end_byte].decode("utf-8", errors="replace")
                if n_text == prefix_op:
                    ed.replace_node(n, helper.encode("utf-8"))
                    break

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        yield Variant(
            name=f"ithelper_{counter}",
            pattern_name="temp_elimination",
            description=f"Replace {prefix_op} with {helper}",
            source=new_source,
        )
        counter += 1


def _multi_eliminate(
    ctx: FunctionContext, source: bytes, stmts: list[Node], counter: int
) -> Iterator[Variant]:
    """Try eliminating 2-3 consecutive single-use temps at once."""
    if counter >= 8:
        return

    # Find runs of consecutive declarations that are single-use
    i = 0
    while i < len(stmts) - 1:
        run = []
        j = i
        while j < len(stmts):
            decl_info = _extract_single_decl(stmts[j], source)
            if decl_info is None:
                break
            var_name = decl_info[0]
            # Count uses in ALL subsequent statements
            uses = []
            for k in range(j + 1, len(stmts)):
                uses.extend(_find_identifier_uses(stmts[k], var_name))
            if len(uses) != 1:
                break
            run.append((j, decl_info, uses[0]))
            j += 1

        if len(run) >= 2:
            # Try eliminating all temps in the run
            ed = SourceEditor(source)
            descs = []
            valid = True

            for idx, (stmt_idx, (var_name, init_expr, start, end), use_node) in enumerate(run):
                del_end = end
                while del_end < len(source) and source[del_end:del_end + 1] in (b"\n", b"\r"):
                    del_end += 1
                del_start = start
                while del_start > 0 and source[del_start - 1:del_start] in (b" ", b"\t"):
                    del_start -= 1

                ed.delete_range(del_start, del_end)
                ed.replace_node(use_node, init_expr)
                descs.append(var_name.decode("utf-8", errors="replace"))

            try:
                new_source = ed.apply()
            except ValueError:
                valid = False

            if valid:
                yield Variant(
                    name=f"tmpelim_multi_{counter}",
                    pattern_name="temp_elimination",
                    description=f"Eliminate {len(run)} temps: {', '.join(descs)}",
                    source=new_source,
                )
                counter += 1

        i = max(j, i + 1)


def _multi_use_value_eliminate(
    ctx: FunctionContext, source: bytes, stmts: list[Node], counter: int
) -> Iterator[Variant]:
    """Eliminate value-type locals used 2-3 times by substituting the init expression.

    Only safe when:
    - The init expression is pure (no calls, no side effects)
    - No statements with side effects between the declaration and the last use
    - The variable is a plain value type (not a ref/ptr — those are reference_elimination)

    Example:
        int count = mElements.size();   <-- has call, skip
        float x = mFoo;                 <-- pure member read, OK
        bar(x + y);
        baz(x);
        ->
        bar(mFoo + y);
        baz(mFoo);
    """
    if counter >= 10:
        return

    for i, stmt in enumerate(stmts):
        if counter >= 10:
            break

        decl_info = _extract_single_decl(stmt, source)
        if decl_info is None:
            continue

        var_name, init_expr, decl_start, decl_end = decl_info

        # Skip ref/ptr declarators (handled by reference_elimination)
        init_decls = [c for c in stmt.named_children if c.type == "init_declarator"]
        if init_decls:
            declarator = init_decls[0].child_by_field_name("declarator")
            if declarator is not None and declarator.type in (
                "reference_declarator", "pointer_declarator"
            ):
                continue

        # Init expression must be pure (no calls)
        if _has_side_effects(stmt, source):
            continue

        # Count uses
        uses = []
        for j in range(i + 1, len(stmts)):
            uses.extend(_find_identifier_uses(stmts[j], var_name))

        # Only 2-3 uses (single-use already handled, >3 is too aggressive)
        if len(uses) < 2 or len(uses) > 3:
            continue

        # Find the statement index of the last use
        last_use_stmt_idx = None
        for j in range(len(stmts) - 1, i, -1):
            if any(_node_contains(stmts[j], u) for u in uses):
                last_use_stmt_idx = j
                break
        if last_use_stmt_idx is None:
            continue

        # No side-effecting statements between decl and last use
        has_intervening_effects = False
        for k in range(i + 1, last_use_stmt_idx + 1):
            if _has_side_effects(stmts[k], source):
                has_intervening_effects = True
                break
        if has_intervening_effects:
            continue

        # Build variant: delete declaration, replace all uses
        ed = SourceEditor(source)

        del_end = decl_end
        while del_end < len(source) and source[del_end:del_end + 1] in (b"\n", b"\r"):
            del_end += 1
        del_start = decl_start
        while del_start > 0 and source[del_start - 1:del_start] in (b" ", b"\t"):
            del_start -= 1

        ed.delete_range(del_start, del_end)

        for use_node in sorted(uses, key=lambda n: n.start_byte, reverse=True):
            ed.replace_node(use_node, init_expr)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        var_str = var_name.decode("utf-8", errors="replace")
        init_str = init_expr.decode("utf-8", errors="replace")
        if len(init_str) > 40:
            init_str = init_str[:37] + "..."
        yield Variant(
            name=f"tmpelim_multiuse_{counter}",
            pattern_name="temp_elimination",
            description=f"Eliminate multi-use value '{var_str}' ({len(uses)} uses) = {init_str}",
            source=new_source,
        )
        counter += 1
