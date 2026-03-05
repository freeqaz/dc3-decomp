"""Prologue pressure pattern — manipulate callee-saved register count.

When target and base have different prologue save counts (e.g., __savegprlr_24
vs __savegprlr_27), the decomp uses a different number of callee-saved registers.
This pattern generates variants that change register pressure to match.

Strategies:
1. Hoist loop subexpressions into named variables (increases pressure)
2. Split pointer variables used across call boundaries (increases pressure)
3. Add volatile-read pressure variables before calls (increases pressure)
4. Inline single-use variables to shorten live ranges (decreases pressure)
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, find_calls, get_indent, get_line_start, identifiers_in
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


class ProloguePressurePattern(Pattern):
    name = "prologue_pressure"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        return diagnosis.has_prologue_mismatch

    def priority(self, diagnosis: Diagnosis) -> float:
        if not diagnosis.has_prologue_mismatch:
            return 0.0
        delta = abs(diagnosis.gpr_save_delta) + abs(diagnosis.fpr_save_delta)
        if delta >= 3:
            return 0.95
        if delta >= 1:
            return 0.8
        return 0.0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        diag = ctx.diagnosis
        if diag is None or not diag.has_prologue_mismatch:
            return

        counter = 0
        gpr_delta = diag.gpr_save_delta  # positive = target needs more GPRs

        if gpr_delta > 0:
            # Target needs MORE callee-saved regs — increase pressure
            for v in self._hoist_loop_exprs(ctx, counter):
                yield v
                counter += 1
                if counter >= 12:
                    return

            for v in self._split_multi_use_ptrs(ctx, counter):
                yield v
                counter += 1
                if counter >= 12:
                    return

            for v in self._extract_call_args(ctx, counter):
                yield v
                counter += 1
                if counter >= 12:
                    return

        elif gpr_delta < 0:
            # Target needs FEWER callee-saved regs — decrease pressure
            for v in self._inline_to_reduce_pressure(ctx, counter):
                yield v
                counter += 1
                if counter >= 12:
                    return

    # -- Strategy 1: Hoist loop subexpressions into named variables -----------

    def _hoist_loop_exprs(
        self, ctx: FunctionContext, start: int
    ) -> Iterator[Variant]:
        """Extract repeated subexpressions from loop bodies into pre-loop locals."""
        source = ctx.file_source
        counter = start

        for stmt in ctx.statements:
            if counter - start >= 6:
                break

            loop_body = _get_loop_body(stmt)
            if loop_body is None:
                continue

            # Find call expressions inside the loop body
            calls = list(find_calls(loop_body))
            if not calls:
                continue

            # Find field_expression accesses (obj->member, obj.member) in loop body
            field_exprs = [n for n in walk(loop_body) if n.type == "field_expression"]

            # Hoist field accesses that appear multiple times
            seen_texts: dict[bytes, list[Node]] = {}
            for fe in field_exprs:
                # Skip if this field_expression is the function target of a call
                # (e.g., mat->NextPass in mat->NextPass() — hoisting just the name is invalid)
                if _is_call_target(fe):
                    continue
                text = source[fe.start_byte:fe.end_byte]
                # Skip if it contains a call (side-effecting)
                if any(n.type == "call_expression" for n in walk(fe)):
                    continue
                seen_texts.setdefault(text, []).append(fe)

            for text, nodes in seen_texts.items():
                if len(nodes) < 2:
                    continue
                if counter - start >= 6:
                    break

                indent = get_indent(source, stmt)
                var_name = f"_hoisted{counter}".encode()
                decl = indent + b"auto " + var_name + b" = " + text + b";\n"

                ed = SourceEditor(source)
                line_start = get_line_start(source, stmt)
                ed.insert_at(line_start, decl)

                # Replace all occurrences in the loop body
                for node in nodes:
                    ed.replace_node(node, var_name)

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                text_str = text.decode("utf-8", errors="replace")
                if len(text_str) > 40:
                    text_str = text_str[:37] + "..."
                yield Variant(
                    name=f"prolpres_hoist_{counter}",
                    pattern_name=self.name,
                    description=f"Hoist '{text_str}' ({len(nodes)}x) before loop",
                    source=new_source,
                )
                counter += 1

    # -- Strategy 2: Split pointer variables used across call boundaries ------

    def _split_multi_use_ptrs(
        self, ctx: FunctionContext, start: int
    ) -> Iterator[Variant]:
        """Split pointer variables used in multiple call arguments into separate locals."""
        source = ctx.file_source
        stmts = ctx.statements
        counter = start

        for i, stmt in enumerate(stmts):
            if counter - start >= 4:
                break
            if stmt.type != "declaration":
                continue

            # Check if this declares a pointer type
            decl_text = source[stmt.start_byte:stmt.end_byte]
            if b"*" not in decl_text:
                continue

            # Get the variable name
            name = _get_declared_identifier(stmt)
            if name is None:
                continue

            # Find uses of this variable in call arguments across subsequent stmts
            call_uses: list[tuple[Node, Node]] = []  # (call_node, identifier_node)
            for j in range(i + 1, len(stmts)):
                for call_node in find_calls(stmts[j]):
                    args = call_node.child_by_field_name("arguments")
                    if args is None:
                        continue
                    for n in walk(args):
                        if n.type == "identifier" and n.text == name.encode():
                            call_uses.append((call_node, n))

            # Only split if used in 2+ different call sites
            if len(call_uses) < 2:
                continue

            # Create a split: second use gets a new variable
            second_call, second_use = call_uses[1]
            indent = get_indent(source, stmt)

            # Find the statement containing the second call
            target_stmt = None
            for j in range(i + 1, len(stmts)):
                if stmts[j].start_byte <= second_call.start_byte < stmts[j].end_byte:
                    target_stmt = stmts[j]
                    break

            if target_stmt is None:
                continue

            var_name = f"_split{counter}".encode()
            line_start = get_line_start(source, target_stmt)
            split_indent = get_indent(source, target_stmt)
            decl_line = split_indent + b"auto " + var_name + b" = " + name.encode() + b";\n"

            ed = SourceEditor(source)
            ed.insert_at(line_start, decl_line)
            ed.replace_node(second_use, var_name)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            yield Variant(
                name=f"prolpres_split_{counter}",
                pattern_name=self.name,
                description=f"Split ptr '{name}' for separate call-site use",
                source=new_source,
            )
            counter += 1

    # -- Strategy 3: Extract call arguments to extend live ranges --------------

    def _extract_call_args(
        self, ctx: FunctionContext, start: int
    ) -> Iterator[Variant]:
        """Extract non-trivial call arguments into named locals before the call.

        Unlike dead pressure variables, these genuinely change register allocation
        by creating a new named variable whose live range spans the call. This
        forces the compiler to allocate a callee-saved register for the extracted
        value, increasing pressure to match the target prologue.

        Only targets top-level statements (not nested scopes) and avoids
        extracting simple identifiers or literals.
        """
        source = ctx.file_source
        stmts = ctx.statements
        counter = start

        for i, stmt in enumerate(stmts):
            if counter - start >= 6:
                break

            # Find call expressions in this top-level statement
            for call_node in find_calls(stmt):
                if counter - start >= 6:
                    break

                args = call_node.child_by_field_name("arguments")
                if args is None:
                    continue

                for arg in args.named_children:
                    if counter - start >= 6:
                        break

                    # Skip trivial or problematic expressions
                    if arg.type in ("identifier", "number_literal", "string_literal",
                                    "true", "false", "null", "this",
                                    # Calls reorder side effects; casts are type-only
                                    "call_expression", "cast_expression",
                                    "parenthesized_expression",
                                    # Unary ops are too simple
                                    "unary_expression", "pointer_expression"):
                        continue

                    arg_text = source[arg.start_byte:arg.end_byte]

                    # Skip very short expressions (just a var name or cast)
                    if len(arg_text) < 5:
                        continue

                    indent = get_indent(source, stmt)
                    var_name = f"_arg{counter}".encode()
                    line_start = get_line_start(source, stmt)
                    decl = indent + b"auto " + var_name + b" = " + arg_text + b";\n"

                    ed = SourceEditor(source)
                    ed.insert_at(line_start, decl)
                    ed.replace_node(arg, var_name)

                    try:
                        new_source = ed.apply()
                    except ValueError:
                        continue

                    text_str = arg_text.decode("utf-8", errors="replace")
                    if len(text_str) > 40:
                        text_str = text_str[:37] + "..."
                    yield Variant(
                        name=f"prolpres_arg_{counter}",
                        pattern_name=self.name,
                        description=f"Extract call arg '{text_str}' to extend live range",
                        source=new_source,
                    )
                    counter += 1

    # -- Strategy 4: Inline variables to reduce pressure ----------------------

    def _inline_to_reduce_pressure(
        self, ctx: FunctionContext, start: int
    ) -> Iterator[Variant]:
        """Inline variables that have short live ranges to reduce callee-saved usage."""
        source = ctx.file_source
        stmts = ctx.statements
        counter = start

        for i, stmt in enumerate(stmts):
            if counter - start >= 6:
                break
            if stmt.type != "declaration":
                continue

            # Extract declaration info
            init_decls = [c for c in stmt.named_children if c.type == "init_declarator"]
            if len(init_decls) != 1:
                continue

            init_decl = init_decls[0]
            declarator = init_decl.child_by_field_name("declarator")
            value = init_decl.child_by_field_name("value")
            if declarator is None or value is None:
                continue

            # Get identifier
            name_node = declarator
            while name_node.type in ("pointer_declarator", "reference_declarator"):
                inner = name_node.child_by_field_name("declarator")
                if inner is None:
                    break
                name_node = inner
            if name_node.type != "identifier" or name_node.text is None:
                continue

            var_name = name_node.text
            init_expr = source[value.start_byte:value.end_byte]

            # Find all uses in subsequent statements
            uses = []
            for j in range(i + 1, len(stmts)):
                for n in walk(stmts[j]):
                    if n.type == "identifier" and n.text == var_name:
                        parent = n.parent
                        if parent and parent.type == "init_declarator":
                            decl = parent.child_by_field_name("declarator")
                            if decl and decl.id == n.id:
                                continue
                        uses.append((j, n))

            # Inline if used 1-2 times and within a reasonable distance
            if not uses or len(uses) > 2:
                continue
            max_dist = max(j for j, _ in uses) - i
            if max_dist > 5:
                continue

            # Skip if init has real side effects and multiple uses.
            # Cast expressions (dynamic_cast, static_cast, etc.) are pure despite
            # being parsed as call_expression by tree-sitter.
            if len(uses) > 1 and _has_real_side_effects(value, source):
                continue

            ed = SourceEditor(source)

            # Delete the declaration line
            del_start = stmt.start_byte
            del_end = stmt.end_byte
            while del_end < len(source) and source[del_end:del_end + 1] in (b"\n", b"\r"):
                del_end += 1
            while del_start > 0 and source[del_start - 1:del_start] in (b" ", b"\t"):
                del_start -= 1
            ed.delete_range(del_start, del_end)

            # Replace all uses with init expression (parenthesized if needed)
            needs_parens = len(uses) > 1 or _needs_parens(value)
            replacement = b"(" + init_expr + b")" if needs_parens else init_expr
            for _, use_node in uses:
                ed.replace_node(use_node, replacement)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            var_str = var_name.decode("utf-8", errors="replace")
            yield Variant(
                name=f"prolpres_inline_{counter}",
                pattern_name=self.name,
                description=f"Inline '{var_str}' to reduce register pressure",
                source=new_source,
            )
            counter += 1


# -- Helpers ------------------------------------------------------------------

def _is_call_target(node: Node) -> bool:
    """Check if this node is the function name of a call_expression.

    E.g., in `mat->NextPass()`, the field_expression `mat->NextPass` is the
    call target. Hoisting it alone (without the `()`) produces invalid code.
    """
    parent = node.parent
    if parent is not None and parent.type == "call_expression":
        func = parent.child_by_field_name("function")
        if func is not None and func.id == node.id:
            return True
    return False


def _get_loop_body(stmt: Node) -> Node | None:
    """Get the compound_statement body of a loop statement."""
    if stmt.type in ("for_statement", "while_statement", "do_statement"):
        body = stmt.child_by_field_name("body")
        if body is not None and body.type == "compound_statement":
            return body
    return None


def _get_declared_identifier(decl: Node) -> str | None:
    """Extract the identifier name from a declaration node."""
    declarator = decl.child_by_field_name("declarator")
    if declarator is None:
        return None
    if declarator.type == "init_declarator":
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            declarator = inner
    while declarator.type in ("pointer_declarator", "reference_declarator"):
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            declarator = inner
        else:
            break
    if declarator.type == "identifier" and declarator.text:
        return declarator.text.decode("utf-8", errors="replace")
    return None


_CAST_KEYWORDS = frozenset({
    b"dynamic_cast", b"static_cast", b"reinterpret_cast", b"const_cast",
})


def _has_real_side_effects(value_node: Node, source: bytes) -> bool:
    """Check if expression has real side effects (not just casts)."""
    for n in walk(value_node):
        if n.type == "call_expression":
            # Check if it's a C++ cast (dynamic_cast<T>(x)) — these are pure
            func = n.child_by_field_name("function")
            if func is not None:
                func_text = source[func.start_byte:func.end_byte]
                # dynamic_cast<Type*> etc — func includes the template part
                base = func_text.split(b"<")[0].strip()
                if base in _CAST_KEYWORDS:
                    continue
            return True
    return False


def _needs_parens(value_node: Node) -> bool:
    """Check if an expression needs parentheses when substituted inline."""
    return value_node.type in (
        "binary_expression",
        "conditional_expression",
        "assignment_expression",
        "comma_expression",
    )
