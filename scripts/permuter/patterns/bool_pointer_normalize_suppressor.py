"""bool_pointer_normalize_suppressor — swap pointer-as-int cast styles.

When MSVC encounters a pointer cast to an integer width followed by a
bitwise mask/shift (`(long)ptr & mask`, `(int)ptr | 0x1`, etc.), it
sometimes emits a `cntlzw` / `extrwi` / `extsw` boolean-normalization
sequence that the target binary doesn't have.  Swapping to a
compile-time-equivalent form (``reinterpret_cast<int>(ptr)``,
``(uintptr_t)ptr``, ``(unsigned int)ptr``, ``(unsigned long)ptr``) is
enough to suppress the normalization.

Target shape:  ``obj/Utl::ReloadObjectType`` (93.9%) and similar
functions where a pointer is masked or shifted as an int.

Transformations (forward direction):
    (long)ptr & mask        -> reinterpret_cast<int>(ptr) & mask
    (long)ptr & mask        -> (uintptr_t)ptr & mask
    (long)ptr & mask        -> (unsigned int)ptr & mask
    (long)ptr & mask        -> (unsigned long)ptr & mask

Reverse direction:
    reinterpret_cast<int>(ptr) & mask -> (long)ptr & mask
                                      -> (uintptr_t)ptr & mask
                                      -> (unsigned int)ptr & mask
                                      -> (unsigned long)ptr & mask

Out of scope:  ``volatile`` casts (separate spill-promoting proposal).

Detection signals (relevant()):  ``cntlzw``, ``extrwi`` / ``rlwinm``,
``extsw`` opcodes in ``diff_ops``, or replace clusters of size 2-4.

Priority:  0.5 when signals match.
"""

from __future__ import annotations

import re
from typing import Iterator, Optional

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


# ---------------------------------------------------------------------------
# Detection signals
# ---------------------------------------------------------------------------

# Asm opcodes that suggest a boolean-normalization sequence next to a
# pointer-as-int mask.
_NORMALIZE_OPCODES = {
    "cntlzw",
    "extrwi", "extrwi.",
    "rlwinm", "rlwinm.",
    "extsw", "extsw.",
}

# Replace-cluster size band where bool normalization typically lives.
_REPLACE_CLUSTER_MIN = 2
_REPLACE_CLUSTER_MAX = 4

# Bitwise / shift operators that the pattern looks for next to the cast.
_BITWISE_OPS = {b"&", b"|", b"^", b">>", b"<<"}

# Inbound C-style integer cast types — when one of these wraps a pointer
# expression we emit equivalent alternative cast forms.
_INTEGER_CAST_TYPES: tuple[bytes, ...] = (
    b"long",
    b"int",
    b"unsigned long",
    b"unsigned int",
    b"uintptr_t",
    b"intptr_t",
    b"size_t",
)

# Alternative cast forms emitted for each forward match.  The list is the
# spec's 4 transformations; the generator filters out the inbound's own
# label so the emitted variant always differs from the input.  Each entry
# is the full text that replaces the original ``(type)expr`` form (with
# ``{expr}`` substituted).
_FORWARD_VARIANTS: tuple[tuple[str, str], ...] = (
    ("reinterpret_cast<int>", "reinterpret_cast<int>({expr})"),
    ("(uintptr_t)",           "(uintptr_t){expr}"),
    ("(unsigned int)",        "(unsigned int){expr}"),
    ("(unsigned long)",       "(unsigned long){expr}"),
)

# Reverse direction: from ``reinterpret_cast<int>(expr)`` back to C-style.
_REVERSE_VARIANTS: tuple[tuple[str, str], ...] = (
    ("(long)",                "(long){expr}"),
    ("(uintptr_t)",           "(uintptr_t){expr}"),
    ("(unsigned int)",        "(unsigned int){expr}"),
    ("(unsigned long)",       "(unsigned long){expr}"),
)

# Variant cap per function.
_VARIANT_CAP = 8

# Pointer-naming heuristic: ``*p[A-Z]…``, ``…Ptr``, or ``m[A-Z]…Ptr``.
_PTR_SUFFIX_RE = re.compile(r"(?:.*Ptr|^p[A-Z]\w*|^m[A-Z]\w*Ptr)$")


# ---------------------------------------------------------------------------
# Pattern
# ---------------------------------------------------------------------------

class BoolPointerNormalizeSuppressorPattern(Pattern):
    """Swap ``(long)ptr & mask`` cast styles to suppress cntlzw/extrwi."""

    name = "bool_pointer_normalize_suppressor"
    safety_tier = "conservative"
    structural_domain = "expr_shape"

    # -- relevance / priority -------------------------------------------------

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _NORMALIZE_OPCODES:
                return True
            if d.base_opcode in _NORMALIZE_OPCODES:
                return True
        for cluster in diagnosis.clusters:
            # Replace clusters of size 2-4 often carry the bool-normalize tail.
            if _REPLACE_CLUSTER_MIN <= cluster.size <= _REPLACE_CLUSTER_MAX:
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        return 0.5

    # -- generation -----------------------------------------------------------

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source

        # Dedupe by (start_byte, end_byte, variant_label) so the same source
        # span never produces the same replacement twice.
        seen: set[tuple[int, int, str]] = set()

        for node in walk(ctx.body_node):
            if counter >= _VARIANT_CAP:
                return

            # Forward: C-style cast to integer wrapping a pointer expression,
            # adjacent to a bitwise/shift operator.
            if node.type == "cast_expression":
                forward = _classify_forward(node, source, ctx)
                if forward is not None:
                    cast_label, inner_text = forward
                    for label, template in _FORWARD_VARIANTS:
                        if counter >= _VARIANT_CAP:
                            return
                        if label == cast_label:
                            # Skip the cast we already have.
                            continue
                        key = (node.start_byte, node.end_byte, label)
                        if key in seen:
                            continue
                        seen.add(key)
                        replacement = template.format(expr=inner_text).encode("utf-8")
                        variant = _emit_variant(
                            source, node, replacement, counter,
                            description=f"Swap {cast_label} pointer cast -> {label}",
                        )
                        if variant is None:
                            continue
                        yield variant
                        counter += 1

            # Reverse: reinterpret_cast<int>(ptr) call form back to C-style.
            elif node.type == "call_expression":
                reverse = _classify_reverse(node, source, ctx)
                if reverse is not None:
                    inner_text = reverse
                    for label, template in _REVERSE_VARIANTS:
                        if counter >= _VARIANT_CAP:
                            return
                        key = (node.start_byte, node.end_byte, label)
                        if key in seen:
                            continue
                        seen.add(key)
                        replacement = template.format(expr=inner_text).encode("utf-8")
                        variant = _emit_variant(
                            source, node, replacement, counter,
                            description=f"Swap reinterpret_cast<int> -> {label}",
                        )
                        if variant is None:
                            continue
                        yield variant
                        counter += 1


# ---------------------------------------------------------------------------
# Forward classifier:  ``(int)ptr`` adjacent to a bitwise op
# ---------------------------------------------------------------------------

def _classify_forward(
    node: Node, source: bytes, ctx: FunctionContext,
) -> Optional[tuple[str, str]]:
    """Return ``(cast_label, inner_text)`` for a forward-candidate cast.

    Returns ``None`` when the node is not a forward candidate.  ``cast_label``
    is the human-readable original cast (e.g. ``"(long)"``).
    """
    type_desc = node.child_by_field_name("type")
    value_node = node.child_by_field_name("value")
    if type_desc is None or value_node is None:
        # tree-sitter exposes cast_expression as `(` type_descriptor `)` value
        # but `child_by_field_name` is not always set.  Fall back to walking
        # named children: first named child is the type, second is the value.
        named = [c for c in node.named_children if c.type != "comment"]
        if len(named) < 2:
            return None
        type_desc = named[0]
        value_node = named[1]

    type_text = source[type_desc.start_byte:type_desc.end_byte].strip()
    # Normalise whitespace so we can compare against _INTEGER_CAST_TYPES.
    type_text_norm = b" ".join(type_text.split())
    if type_text_norm not in _INTEGER_CAST_TYPES:
        return None

    if not _is_pointer_expression(value_node, source, ctx):
        return None

    if not _has_bitwise_adjacent(node):
        return None

    inner_text = source[value_node.start_byte:value_node.end_byte].decode(
        "utf-8", errors="replace",
    )
    cast_label = f"({type_text_norm.decode('utf-8', errors='replace')})"
    return (cast_label, inner_text)


# ---------------------------------------------------------------------------
# Reverse classifier:  ``reinterpret_cast<int>(ptr)`` adjacent to bitwise op
# ---------------------------------------------------------------------------

def _classify_reverse(
    node: Node, source: bytes, ctx: FunctionContext,
) -> Optional[str]:
    """Return the inner expression text for a reverse-candidate
    ``reinterpret_cast<int>`` call.  Returns ``None`` when not a candidate.
    """
    func = node.child_by_field_name("function")
    args = node.child_by_field_name("arguments")
    if func is None or args is None:
        return None

    if func.type != "template_function":
        return None

    name = func.child_by_field_name("name")
    if name is None or source[name.start_byte:name.end_byte] != b"reinterpret_cast":
        return None

    # Confirm the template argument is an integer width we care about.
    tmpl_args = func.child_by_field_name("arguments")
    if tmpl_args is None:
        return None
    tmpl_text = b" ".join(
        source[tmpl_args.start_byte:tmpl_args.end_byte].split()
    )
    # Strip the surrounding < >.
    tmpl_inner = tmpl_text.strip(b"<>").strip()
    if tmpl_inner not in _INTEGER_CAST_TYPES:
        return None

    # Single argument expected.
    real_args = [c for c in args.named_children if c.type != "comment"]
    if len(real_args) != 1:
        return None
    arg_node = real_args[0]

    if not _is_pointer_expression(arg_node, source, ctx):
        return None

    if not _has_bitwise_adjacent(node):
        return None

    return source[arg_node.start_byte:arg_node.end_byte].decode(
        "utf-8", errors="replace",
    )


# ---------------------------------------------------------------------------
# Pointer-expression heuristic
# ---------------------------------------------------------------------------

def _is_pointer_expression(node: Node, source: bytes, ctx: FunctionContext) -> bool:
    """Heuristic for whether *node* is a pointer-typed expression.

    Without libclang we rely on a small set of conservative shape checks:

    * ``&IDENT``                     — address-of expression
    * ``ptr->member`` / ``*ptr``     — pointer-deref forms
    * Identifier matching the project pointer-naming convention
      (``*Ptr``, ``mFooPtr``, ``pFoo``)
    * Identifier with a same-TU pointer declaration or arrow use
      (`Foo *ident` / ``ident->method``)
    """
    # Reject literals immediately — caller's main concern is suppressing
    # spurious variants for ``(long)0 & mask``-style code.
    if node.type == "number_literal":
        return False
    if node.type == "char_literal" or node.type == "string_literal":
        return False
    if node.type == "true" or node.type == "false":
        return False

    # ``&local`` — address-of expression is always pointer-typed.
    if node.type == "pointer_expression":
        op = node.child_by_field_name("operator")
        if op is not None and op.text == b"&":
            return True
        if op is not None and op.text == b"*":
            # Dereferencing — the result is the pointee, not a pointer.
            # Treat conservatively as "pointer-like context" since the
            # underlying expression is a pointer.
            return True
        # Fallback when field is missing.
        for c in node.children:
            if c.type == "&":
                return True
            if c.type == "*":
                return True
        return False

    # ``ptr->member`` — argument of -> is a pointer, but the full field
    # expression's *type* is the member.  We accept member accesses through
    # ``->`` as pointer-like because the casts compile and the use site is
    # what the heuristic is gating on.
    if node.type == "field_expression":
        arrow = False
        for c in node.children:
            if c.type == "->":
                arrow = True
                break
        if arrow:
            return True
        # Plain `obj.member` — recurse into the argument to see if it's a
        # pointer expression.
        arg = node.child_by_field_name("argument")
        if arg is not None:
            ident_text = source[arg.start_byte:arg.end_byte].decode(
                "utf-8", errors="replace",
            )
            if _ident_is_pointer_like(ident_text, ctx):
                return True
        return False

    # Identifier-based heuristics.
    if node.type == "identifier":
        ident = source[node.start_byte:node.end_byte].decode(
            "utf-8", errors="replace",
        )
        if _PTR_SUFFIX_RE.match(ident):
            return True
        if _ident_is_pointer_like(ident, ctx):
            return True
        return False

    # ``this`` is a pointer.
    if node.type == "this":
        return True

    # ``call()`` returning a pointer — heuristic on the source text.
    if node.type == "call_expression":
        text = source[node.start_byte:node.end_byte].decode(
            "utf-8", errors="replace",
        )
        # Conservative: only treat ``GetXxxPtr()`` / ``XxxPtr()`` shaped calls
        # as pointer-returning.  Avoids over-firing.
        # Extract callee text up to the first '('.
        callee = text.split("(", 1)[0].strip()
        if _PTR_SUFFIX_RE.match(callee):
            return True
        return False

    # ``parenthesized_expression`` — unwrap and recurse.
    if node.type == "parenthesized_expression":
        for c in node.named_children:
            if c.type != "comment":
                return _is_pointer_expression(c, source, ctx)

    return False


def _ident_is_pointer_like(ident: str, ctx: FunctionContext) -> bool:
    """True when ident is declared or used as a pointer in the TU."""
    if not ident:
        return False
    src = ctx.file_source.decode("utf-8", errors="replace")
    esc = re.escape(ident)
    if re.search(rf"\b{esc}\s*->", src):
        return True
    if re.search(rf"[\w>\)]\s*\*\s*{esc}\b", src):
        return True
    return False


# ---------------------------------------------------------------------------
# Bitwise-adjacency check
# ---------------------------------------------------------------------------

def _has_bitwise_adjacent(node: Node) -> bool:
    """True when *node* is an operand of a nearby bitwise/shift binary op.

    Walks up the parent chain through ``parenthesized_expression`` /
    ``cast_expression`` / ``unary_expression`` so that ``((long)ptr) & 3``
    is detected just like the bare form.
    """
    parent = node.parent
    while parent is not None:
        if parent.type == "binary_expression":
            op = parent.child_by_field_name("operator")
            if op is not None and op.text in _BITWISE_OPS:
                return True
            return False
        if parent.type in (
            "parenthesized_expression",
            "cast_expression",
            "unary_expression",
            "compound_literal_expression",
        ):
            parent = parent.parent
            continue
        # Also accept assignment to a known bitwise compound op.
        if parent.type == "assignment_expression":
            op = parent.child_by_field_name("operator")
            if op is not None and op.text in (b"&=", b"|=", b"^=", b">>=", b"<<="):
                return True
            return False
        return False
    return False


# ---------------------------------------------------------------------------
# Variant emission
# ---------------------------------------------------------------------------

def _emit_variant(
    source: bytes,
    node: Node,
    replacement: bytes,
    counter: int,
    *,
    description: str,
) -> Optional[Variant]:
    """Splice *replacement* over *node*'s byte range and wrap as ``Variant``."""
    ed = SourceEditor(source)
    ed.replace_range(node.start_byte, node.end_byte, replacement)
    try:
        new_source = ed.apply()
    except ValueError:
        return None
    return Variant(
        name=f"ptrnorm_{counter}",
        pattern_name="bool_pointer_normalize_suppressor",
        description=description,
        source=new_source,
    )
