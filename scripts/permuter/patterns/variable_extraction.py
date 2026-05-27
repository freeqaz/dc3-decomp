"""Variable extraction pattern — extract inline calls into auto locals.

Win rate: ~42% from attempt database.

Finds call_expression nodes nested inside argument_list, binary_expression,
or condition_clause at depth > 1. Extracts each into an `auto` local variable
declared before the containing statement.

Example:
    MILO_ASSERT(display < mElements.size(), 0x74);
    ->
    auto _tmp0 = mElements.size();
    MILO_ASSERT(display < _tmp0, 0x74);
"""

from __future__ import annotations

import os
import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from .. import clang_types
from ..ast_queries import get_indent, get_line_start
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Node types that indicate a call is nested (not a standalone expression_statement)
_NESTING_TYPES = {
    "argument_list",
    "binary_expression",
    "condition_clause",
    "parenthesized_expression",
    "assignment_expression",
    "return_statement",
}

# All-caps callee == function-like macro by convention (MILO_ASSERT, MILO_WARN,
# REGISTER_OBJ_FACTORY, ...). tree-sitter can't expand macros, so it parses a
# macro invocation as an ordinary call_expression and exposes the macro's
# arguments as ordinary nested expressions — which look extractable but are NOT:
# hoisting a call out of a macro argument either fails to compile (the argument
# is stringized via #x, or the macro re-evaluates / takes the argument by a name
# the hoist breaks) or, even when it compiles, changes the stringized assert text
# so it can never match the target. Either way it's a wasted compile. We treat
# every all-caps-callee ancestor as a macro boundary, with the common MILO
# macros listed explicitly for clarity / belt-and-braces.
_MACRO_CALLEE_RE = re.compile(rb"^[A-Z][A-Z0-9_]{1,}$")
_KNOWN_MACROS = {
    b"MILO_ASSERT", b"MILO_ASSERT_FMT", b"MILO_ASSERT_RANGE",
    b"MILO_ASSERT_RANGE_EQ", b"MILO_WARN", b"MILO_LOG", b"MILO_FAIL",
    b"MILO_FAIL_DTA", b"MILO_NOTIFY", b"MILO_NOTIFY_BETA", b"MILO_NOTIFY_ONCE",
}


class VariableExtractionPattern(Pattern):
    name = "variable_extraction"
    safety_tier = "conservative"
    structural_domain = "data_flow"
    follow_ups = ("declaration_reorder", "inline_assignment", "statement_reorder")
    cross_unit_modes = ("inline_header",)

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Skip when there are no actionable mismatches (pure noise/unfixable)
        if diagnosis.diff_ops:
            return True
        if diagnosis.clusters:
            return True
        if diagnosis.replace_real > 0:
            return True
        # GPR swaps can sometimes be fixed by variable extraction changing alloc order
        if any(p[0].startswith("r") or p[1].startswith("r")
               for p in diagnosis.reg_swap_pairs):
            return True
        # Unexplained diff_arg might respond to extraction
        unexplained = diagnosis.noise_total - diagnosis.noise_explained
        if unexplained > 0:
            return True
        # Prologue mismatch where target needs more vars
        if diagnosis.has_prologue_mismatch and diagnosis.gpr_save_delta > 0:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        base = 1.0 if self.relevant(diagnosis) else 0.0
        # Boost when prologue shows target needs more callee-saved regs
        if diagnosis.has_prologue_mismatch and diagnosis.gpr_save_delta > 0:
            base = min(1.0, base + 0.3)
        return base

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        # Track names we've already generated in this run
        used_names: set[str] = set()
        # Walk all compound_statements to find extractable calls in their direct children
        for compound, stmt, call_node in _find_extractable_calls(ctx.body_node):
            # Region filter: skip calls outside mismatch regions when data is available
            if not ctx.node_in_mismatch_region(stmt):
                continue
            call_text = ctx.file_source[call_node.start_byte : call_node.end_byte]

            indent = get_indent(ctx.file_source, stmt)
            line_start = get_line_start(ctx.file_source, stmt)

            # Resolve the call's return type once (shared by the untyped-skip
            # decision below and the explicit-type variants further down).
            return_type = _resolve_return_type(call_node, ctx)

            dialect = getattr(ctx, "compiler_dialect", "mwcc")
            # On mwcc (C++98) the untyped form is emitted as `int _tmp = <call>`
            # (the `auto`→int storage-class coercion). When libclang resolves the
            # return type as a NON-int-convertible type (record/class, void,
            # pointer, or float), `int _tmp = <call>` is a hard compile error
            # (record/void/pointer) or a never-matching truncation (float). Those
            # variants always failed to build in the sweep, so skip emitting the
            # untyped form for them — the explicit-type variants below still cover
            # the cases that CAN win. We only skip on a CONFIDENT resolution; when
            # libclang is unavailable or the type is int/bool/enum/unknown we keep
            # the untyped form (no false drops). msvc keeps real `auto`, which
            # deduces correctly, so the skip only applies to the mwcc int form.
            emit_untyped = not (
                dialect != "msvc"
                and return_type is not None
                and _int_decl_is_doomed(return_type)
            )

            if emit_untyped:
                var_name_str = _unique_tmp_name(counter, ctx.file_source, used_names)
                counter = int(var_name_str[4:]) + 1  # advance past the chosen index
                used_names.add(var_name_str)
                var_name = var_name_str.encode("utf-8")

                untyped_kw = b"auto" if dialect == "msvc" else b"int"
                decl_line = indent + untyped_kw + b" " + var_name + b" = " + call_text + b";\n"

                # Use SourceEditor: insert decl at line start, replace call with var_name
                ed = SourceEditor(ctx.file_source)
                ed.insert_at(line_start, decl_line)
                ed.replace_node(call_node, var_name)
                new_source = ed.apply()

                desc = (
                    f"Extract '{call_text.decode('utf-8', errors='replace')}' "
                    f"into auto {var_name.decode()}"
                )
                yield Variant(
                    name=f"varext_{counter - 1}",
                    pattern_name=self.name,
                    description=desc,
                    source=new_source,
                    tags=frozenset({"introduced_temp"}),
                )
            else:
                # Still need a stable, collision-free name for the typed variants.
                var_name_str = _unique_tmp_name(counter, ctx.file_source, used_names)
                counter = int(var_name_str[4:]) + 1
                used_names.add(var_name_str)
                var_name = var_name_str.encode("utf-8")

            # Type-guided variants: use libclang to resolve the call's
            # return type and generate explicit-type alternatives
            for type_spec in _explicit_type_specs_for(return_type):
                typed_decl = (
                    indent + type_spec + b" " + var_name + b" = "
                    + call_text + b";\n"
                )
                ed2 = SourceEditor(ctx.file_source)
                ed2.insert_at(line_start, typed_decl)
                ed2.replace_node(call_node, var_name)
                typed_source = ed2.apply()

                type_str = type_spec.decode()
                yield Variant(
                    name=f"varext_{counter - 1}_typed",
                    pattern_name=self.name,
                    description=(
                        f"Extract '{call_text.decode('utf-8', errors='replace')}' "
                        f"into {type_str} {var_name.decode()}"
                    ),
                    source=typed_source,
                    tags=frozenset({"introduced_temp"}),
                )


def _unique_tmp_name(
    start: int, source: bytes, used_names: set[str]
) -> str:
    """Return a ``_tmpN`` name that doesn't clash with *source* or *used_names*.

    Scans the function source text for word-boundary matches of the candidate
    name (``\\b_tmpN\\b``) and increments N until a collision-free name is found.
    Also avoids names already generated in the current ``generate()`` run.
    """
    source_text = source.decode("utf-8", errors="replace")
    n = start
    while True:
        candidate = f"_tmp{n}"
        if candidate not in used_names and not re.search(
            rf"\b{re.escape(candidate)}\b", source_text
        ):
            return candidate
        n += 1


def _resolve_return_type(call_node: Node, ctx: FunctionContext):
    """Resolve a call's return type via libclang, or None if unavailable.

    Centralizes the libclang lookup so generate() can both decide whether the
    untyped `int _tmp` form is doomed AND build the explicit-type variants from
    a single resolution (one libclang call per site, not two).
    """
    if not clang_types.is_available():
        return None
    return clang_types.resolve_call_return_type(
        ctx.file_path, call_node.start_byte, ctx.file_source
    )


def _int_decl_is_doomed(ti) -> bool:
    """True when `int _tmp = <call>;` cannot be a winning extraction.

    On mwcc (C++98) the untyped extraction is spelled `int _tmp = <call>`.
    That is a hard compile error when the call returns a record/class, void,
    or pointer, and a never-matching truncation when it returns a float/double.
    Only int/bool/enum (and unknown) returns are safe to spell `int`, so those
    return False (keep emitting). This is the single biggest BUILD-FAILED bucket
    the stress sweep attributed to variable_extraction.
    """
    TK = clang_types.TypeKind
    if ti.is_pointer or ti.is_float:
        return True
    if ti.kind in (TK.RECORD, TK.VOID):
        return True
    return False


def _explicit_type_specs_for(ti) -> list[bytes]:
    """Build explicit type-specifier bytes from a resolved TypeInfo.

    Returns [] for an unresolved type (ti is None) so callers degrade to the
    untyped path with no false drops.
    """
    if ti is None:
        return []

    specs: list[bytes] = []
    if ti.is_float:
        if ti.spelling == "float":
            specs.append(b"float")
        elif ti.spelling == "double":
            specs.append(b"double")
        else:
            specs.append(b"float")
            specs.append(b"double")
    elif ti.is_signed_int:
        specs.append(b"int")
        specs.append(b"unsigned int")
    elif ti.is_unsigned_int:
        specs.append(b"unsigned int")
        specs.append(b"int")
    elif ti.kind == clang_types.TypeKind.BOOL:
        specs.append(b"bool")
        specs.append(b"int")
    elif ti.is_pointer:
        # Use the actual pointer type spelling
        spelling = ti.spelling.encode("utf-8")
        specs.append(spelling)
    # Don't generate typed variants for record/enum/other — auto is better
    return specs


def _call_priority(call_node: Node) -> int:
    """Score extraction priority for a call node (higher = better candidate).

    Data shows wins come from method chains and complex getter calls,
    not simple expressions. Prioritize accordingly.
    """
    score = 0

    # Method chain: a->b()->c() — high value
    func = call_node.child_by_field_name("function")
    if func is not None and func.type == "field_expression":
        arg = func.child_by_field_name("argument")
        if arg is not None and arg.type == "call_expression":
            score += 30  # Method chain like a->Foo()->Bar()

    # Nested call: f(g(x)) — the inner g(x) is high value
    parent = call_node.parent
    if parent is not None and parent.type == "argument_list":
        score += 20  # Call used as argument to another call

    # Call with arguments (more complex = more likely to benefit)
    args = call_node.child_by_field_name("arguments")
    if args is not None:
        n_args = len(args.named_children)
        score += min(n_args * 5, 15)

    # Arithmetic context: call inside binary_expression
    if parent is not None and parent.type == "binary_expression":
        score += 10

    # Simple getter with no args — lower priority
    if args is not None and len(args.named_children) == 0:
        # Still potentially useful but lower priority
        score += 5

    return score


def _find_extractable_calls(
    body_node: Node,
) -> Iterator[tuple[Node, Node, Node]]:
    """Find (compound_statement, containing_statement, call_node) tuples.

    Walks all compound_statements (function body, loop bodies, if/else bodies)
    and for each direct child statement, finds nested call expressions that
    can be extracted to a variable before that statement.

    Results are sorted by priority (highest first) so the max_variants cap
    keeps the best candidates.
    """
    candidates: list[tuple[int, Node, Node, Node]] = []

    for stmt in body_node.named_children:
        for call_node in _find_nested_calls(stmt):
            pri = _call_priority(call_node)
            candidates.append((pri, body_node, stmt, call_node))

        for compound in _find_compound_children(stmt):
            for pri, comp, st, cn in _find_extractable_calls_scored(compound):
                candidates.append((pri, comp, st, cn))

    # Sort by priority descending
    candidates.sort(key=lambda x: x[0], reverse=True)
    for _, comp, st, cn in candidates:
        yield comp, st, cn


def _find_extractable_calls_scored(
    body_node: Node,
) -> list[tuple[int, Node, Node, Node]]:
    """Internal scored version for recursion."""
    candidates: list[tuple[int, Node, Node, Node]] = []
    for stmt in body_node.named_children:
        for call_node in _find_nested_calls(stmt):
            pri = _call_priority(call_node)
            candidates.append((pri, body_node, stmt, call_node))
        for compound in _find_compound_children(stmt):
            candidates.extend(_find_extractable_calls_scored(compound))
    return candidates


def _find_compound_children(node: Node) -> Iterator[Node]:
    """Find compound_statement children (loop/if/else bodies)."""
    for child in node.children:
        if child.type == "compound_statement":
            yield child
        elif child.type in ("if_statement", "else_clause", "for_statement",
                            "while_statement", "do_statement", "switch_statement"):
            yield from _find_compound_children(child)


def _callee_text(call_node: Node) -> bytes:
    """Return the callee identifier bytes for a call_expression, or b"".

    Reads the raw bytes of the ``function`` field. For ``MILO_ASSERT(...)`` this
    is ``b"MILO_ASSERT"``; for ``a->b()`` it is ``b"a->b"`` (which never matches
    the macro regex, so member calls are unaffected).
    """
    func = call_node.child_by_field_name("function")
    if func is None or func.text is None:
        return b""
    return func.text


def _is_macro_call(call_node: Node) -> bool:
    """True when *call_node* is (what tree-sitter parsed as) a macro invocation.

    A macro is identified by an all-caps callee identifier (the project's
    function-like-macro convention) or membership in the known MILO macro set.
    """
    callee = _callee_text(call_node)
    if not callee:
        return False
    return callee in _KNOWN_MACROS or _MACRO_CALLEE_RE.match(callee) is not None


def _macro_arg_filter_enabled() -> bool:
    """Whether the macro-argument extraction filter is active (default ON).

    A call inside a macro argument can never produce a winning extraction (it
    fails to build, or the macro stringizes the argument so the result can't
    match the target). The filter is therefore correct-by-default. The env
    escape hatch exists only for A/B measurement / debugging.
    """
    return os.environ.get(
        "PERMUTER_VAREXT_MACRO_FILTER", "1"
    ).strip().lower() in ("1", "true", "yes", "on")


def _inside_macro_argument(call_node: Node) -> bool:
    """True when *call_node* sits inside a macro invocation's argument list.

    Walks ancestors to the root; if any enclosing call_expression is a macro
    invocation the call is a doomed extraction site (see _MACRO_CALLEE_RE
    comment). The ancestor chain inside a single statement is short, so the
    walk is cheap.
    """
    if not _macro_arg_filter_enabled():
        return False
    current = call_node.parent
    while current is not None:
        if current.type == "call_expression" and _is_macro_call(current):
            return True
        current = current.parent
    return False


def _find_nested_calls(node: Node, depth: int = 0) -> Iterator[Node]:
    """Find call_expression nodes nested inside other expressions.

    Only yields calls where the call is inside a nesting context (argument,
    binary expression, condition), not standalone call statements.
    Does NOT recurse into compound_statement children (those are handled
    by _find_extractable_calls to maintain proper scoping).
    """
    if node.type == "call_expression" and depth > 0:
        parent = node.parent
        if parent is not None and parent.type in _NESTING_TYPES:
            # Never hoist a call out of a macro argument — it cannot win
            # (build failure or unmatchable stringized text). This single
            # filter removed the largest BUILD-FAILED bucket in the sweep.
            if not _inside_macro_argument(node):
                yield node
            return  # Don't recurse deeper into this call's own args

    next_depth = depth
    if node.type in _NESTING_TYPES or node.type == "call_expression":
        next_depth = depth + 1

    for child in node.children:
        # Don't cross compound_statement boundaries — inner scopes
        # are handled by _find_extractable_calls recursion
        if child.type == "compound_statement":
            continue
        yield from _find_nested_calls(child, next_depth)
