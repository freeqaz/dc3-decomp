"""Iterator address-of style: `&*<expr>` <-> `<expr>`.

Win source: 2026-05-12 upstream merge wave — FitnessCalorieSort::BuildTree
went from 99.5% → 100% by replacing `InsertHeaderRange(&*pBegin, &*pNext)`
with `InsertHeaderRange(begin, it)`.

Even though `&*it` decays to `T*` via `operator*` then `operator&`, MSVC
emits the deref/addr round-trip in IR before optimization, and the
resulting register-allocation choices diverge from a direct iterator pass.

Transformations:
    f(&*it, &*end)  ->  f(it, end)
    f(it)           ->  f(&*it)        (reverse — when target uses the
                                         deref-then-addr-of form)

Detection signals:
    - Real replace mismatches (instruction substitutions)
    - Register swaps near iterator-typed variables passed to a call
    - Argument-shape clusters at call sites

This is a low-cost pattern: cheap to generate, fires only on calls that
contain a `&*<expr>` argument (or, in reverse mode, on calls that pass
identifiers whose name looks iterator-like and could be surrounded with
the address-of/deref combo). Bounded to 6 variants per direction.

Documented in:
    docs/decomp/patterns/harmful-avoid.md (`Iterator Address-Of (&*iter)`)
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


_MAX_VARIANTS_PER_DIRECTION = 6

# Names that look iterator-y enough to consider for the reverse transform
# (`it` -> `&*it`). We deliberately keep this conservative — false positives
# regress match% unnecessarily.
_ITER_NAME_HINTS = (
    "it", "iter", "itr", "begin", "end", "first", "last",
    "cur", "next", "prev",
)


class IterAddressOfPattern(Pattern):
    name = "iter_address_of"
    safety_tier = "moderate"
    structural_domain = "data_flow"
    follow_ups = ("declaration_reorder",)

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Cheap to generate; only fires when sites exist anyway.
        # Treat any "real" diff as relevant — the pattern is gated by
        # the presence of `&*<expr>` (or call-site iterator-like args)
        # in `generate()`, not by diagnosis shape.
        if diagnosis.replace_real > 0:
            return True
        if diagnosis.clusters:
            return True
        if diagnosis.reg_swap_pairs:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Low-but-nonzero — cheap to enumerate, occasional big wins.
        return 0.2

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node

        # Direction 1: &*<expr> -> <expr>
        sites = list(_find_address_of_deref_sites(body))
        counter = 0
        for node, inner_text in sites:
            if counter >= _MAX_VARIANTS_PER_DIRECTION:
                break
            if not ctx.node_in_mismatch_region(node):
                continue
            ed = SourceEditor(source)
            ed.replace_node(node, inner_text)
            try:
                new_source = ed.apply()
            except ValueError:
                continue
            inner_str = inner_text.decode("utf-8", errors="replace")
            yield Variant(
                name=f"iteraddr_drop_{counter}",
                pattern_name=self.name,
                description=f"&*{inner_str} -> {inner_str}",
                source=new_source,
            )
            counter += 1

        # "Drop all at once" variant when there are 2+ sites
        if len(sites) >= 2 and counter < _MAX_VARIANTS_PER_DIRECTION:
            ed = SourceEditor(source)
            for node, inner_text in sites:
                ed.replace_node(node, inner_text)
            try:
                new_source = ed.apply()
                yield Variant(
                    name=f"iteraddr_drop_all_{counter}",
                    pattern_name=self.name,
                    description=f"Drop all {len(sites)} &*<expr> wrappers",
                    source=new_source,
                )
                counter += 1
            except ValueError:
                pass

        # Direction 2 (reverse): wrap iterator-named arg in `&*`
        rev_counter = 0
        for arg_node, arg_text in _find_iter_arg_sites(body, source):
            if rev_counter >= _MAX_VARIANTS_PER_DIRECTION:
                break
            if not ctx.node_in_mismatch_region(arg_node):
                continue
            ed = SourceEditor(source)
            ed.replace_node(arg_node, b"&*" + arg_text)
            try:
                new_source = ed.apply()
            except ValueError:
                continue
            arg_str = arg_text.decode("utf-8", errors="replace")
            yield Variant(
                name=f"iteraddr_wrap_{rev_counter}",
                pattern_name=self.name,
                description=f"{arg_str} -> &*{arg_str}",
                source=new_source,
            )
            rev_counter += 1


def _find_address_of_deref_sites(body: Node) -> Iterator[tuple[Node, bytes]]:
    """Yield (outer_node, inner_text) for each `&*<expr>` occurrence.

    The outer node is the address-of (pointer_expression with `&` op);
    the inner text is the source of the operand of `*` (the iterator/expr).
    """
    for n in walk(body):
        if n.type != "pointer_expression":
            continue
        op = n.child_by_field_name("operator")
        if op is None or op.text != b"&":
            continue
        operand = n.child_by_field_name("argument")
        if operand is None:
            continue
        # operand should itself be `*<inner>`
        if operand.type != "pointer_expression":
            continue
        inner_op = operand.child_by_field_name("operator")
        if inner_op is None or inner_op.text != b"*":
            continue
        inner = operand.child_by_field_name("argument")
        if inner is None:
            continue
        # If the inner is parenthesized (e.g. `&*(it + 1)`), keep the parens
        # so the result still parses correctly when used as an argument.
        inner_text = inner.text  # raw bytes; preserves any parens
        if inner_text is None:
            continue
        yield n, inner_text


def _find_iter_arg_sites(
    body: Node, source: bytes
) -> Iterator[tuple[Node, bytes]]:
    """Yield (arg_node, arg_text) for call args whose names look iterator-y.

    Only fires when the argument is a bare identifier (not a member access,
    not a parenthesized expression). Conservative on purpose.
    """
    for n in walk(body):
        if n.type != "call_expression":
            continue
        args = n.child_by_field_name("arguments")
        if args is None:
            continue
        for arg in args.named_children:
            if arg.type != "identifier":
                continue
            name = arg.text
            if name is None:
                continue
            name_str = name.decode("utf-8", errors="replace")
            if not _looks_like_iterator(name_str):
                continue
            yield arg, name


def _looks_like_iterator(name: str) -> bool:
    if not name:
        return False
    lname = name.lower()
    if lname in _ITER_NAME_HINTS:
        return True
    if "iter" in lname or "itr" in lname:
        return True
    # Names like "it1", "itEnd" — single-letter prefixes are too risky;
    # require at least two letters.
    if lname.startswith("it") and len(lname) >= 3:
        return True
    return False
