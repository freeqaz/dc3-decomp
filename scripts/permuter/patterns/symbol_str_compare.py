"""Add .Str() / .mStr to Symbol operands in direct comparisons (non-MILO).

Win rate: untested (new pattern, proven in StoreOfferProvider::BuildList 90.1->97.9%
and VocalTrackDir::ApplyArrowStyle).

When a Symbol is compared with `==` or `!=` to gNullStr or another Symbol, MWCC
generates a call to `Symbol::operator==(const char*)` which does a strcmp.  The
target binary instead does a raw `cmplw` pointer compare.

Appending `.Str()` (or accessing `.mStr`) makes both sides `const char*`, so the
compiler emits a direct pointer comparison instead.

Example:
    sym != gNullStr          ->  sym.Str() != gNullStr
    symA == symB             ->  symA.Str() == symB.Str()
    sym == gNullStr          ->  sym.mStr == gNullStr
    (also: const Symbol& sym vs plain Symbol sym)

Relation to milo_str_conv: `milo_str_conv` adds .Str() inside MILO macros
(MILO_ASSERT / MILO_WARN / etc.) to fix MakeString<Symbol> vs MakeString<char*>
template instantiation mismatches.  THIS pattern targets plain `==` / `!=`
comparisons OUTSIDE MILO macros — a completely different syntactic location.

Detection signals:
    - bl strcmp / bl __eq in target that our base emits as cmplw (or vice versa)
    - replace_real > 0 (call vs direct compare)
    - diff_ops with bl (unexpected function call in target for a compare)
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, find_comparisons
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# MILO macros — comparisons inside these are handled by milo_str_conv, not us
_MILO_MACROS = {
    b"MILO_NOTIFY", b"MILO_WARN", b"MILO_LOG", b"MILO_FAIL",
    b"MILO_ASSERT", b"MILO_ASSERT_FMT", b"MILO_NOTIFY_ONCE",
}

# bl-target names that indicate Symbol/strcmp equality. Per
# feedback_symbol_ptr_compare.md and feedback_strcmp_bool_materialization.md,
# only these are the canonical signal — a generic ``bl`` mismatch is too broad.
# Includes both the raw libc strcmp and known Symbol::operator== mangles.
_SYMBOL_EQ_BL_TARGETS = frozenset({
    "strcmp",
    # Symbol::operator==(const Symbol&) and char* overloads (MWCC mangling)
    "__eq__6SymbolFRC6Symbol",
    "__eq__6SymbolFPCc",
    "__ne__6SymbolFRC6Symbol",
    "__ne__6SymbolFPCc",
    "eq__6SymbolFPCc",
    "eq__6SymbolFRC6Symbol",
    # Symbol::Str() — also indicates Symbol-handling site
    "Str__C6SymbolFv",
})

# Known null Symbol names
_NULL_SYMBOL_NAMES = {b"gNullStr", b"Symbol()", b"kNoStr", b"kNullStr"}

# Comparison operators we care about
_CMP_OPS = {"==", "!="}

# Identifiers that are never Symbol values
_KNOWN_NON_SYMBOL: frozenset[bytes] = frozenset({
    b"nullptr", b"NULL", b"true", b"false",
    b"this", b"i", b"j", b"n", b"idx", b"len",
    b"size", b"count", b"ret", b"result",
})

# Non-Symbol C++ type keywords that indicate a non-Symbol declaration
_NON_SYMBOL_TYPE_RE = re.compile(
    r"\b(int|unsigned|bool|float|double|char|short|long|void|size_t"
    r"|uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t"
    r"|uintptr_t|intptr_t|ptrdiff_t)\b"
)


def _ident_declared_as_symbol(ident: str, tu_source: str) -> bool:
    """Return True when *ident* is textually declared as Symbol in the TU.

    Positive signals (any one is enough):
    1. ``Symbol ident`` — local/param/member declaration.
    2. ``const Symbol& ident`` or ``const Symbol ident`` — reference param.
    3. ``Symbol& ident`` — reference param.
    4. ``ident.Str()`` or ``ident.mStr`` usage elsewhere in TU (proven Symbol).
    """
    esc = re.escape(ident)
    # Declaration: "Symbol ident" or "Symbol& ident" or "const Symbol[&] ident"
    if re.search(rf"\bSymbol\s*&?\s*{esc}\b", tu_source):
        return True
    if re.search(rf"\bconst\s+Symbol\s*&?\s*{esc}\b", tu_source):
        return True
    # Cross-TU usage: ident.Str() or ident.mStr proves it's a Symbol
    if re.search(rf"\b{esc}\s*\.\s*Str\s*\(\s*\)", tu_source):
        return True
    if re.search(rf"\b{esc}\s*\.\s*mStr\b", tu_source):
        return True
    return False


def _ident_declared_as_non_symbol(ident: str, tu_source: str) -> bool:
    """Return True when *ident* is textually declared as a non-Symbol type in the TU.

    Checks for declarations like ``int ident``, ``bool ident``, ``float ident``,
    pointer declarations ``Type *ident``, or arrow usage ``ident->`` (pointer).
    False positives (e.g. matching inside a comment) are acceptable — they only
    cause a skipped variant.
    """
    esc = re.escape(ident)
    # Arrow usage: ident is a pointer
    if re.search(rf"\b{esc}\s*->", tu_source):
        return True
    # Pointer declaration: "Type *ident" or "Type* ident"
    if re.search(rf"[\w>\)]\s*\*\s*{esc}\b", tu_source):
        return True
    # Non-Symbol primitive-type declaration: "int ident" / "bool ident" / etc.
    m = re.search(rf"\b(\w[\w:]*)\s+{esc}\b", tu_source)
    if m:
        declared_type = m.group(1)
        if _NON_SYMBOL_TYPE_RE.search(declared_type):
            return True
    return False


class SymbolStrComparePattern(Pattern):
    name = "symbol_str_compare"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Sharpened gate: only fire when the specific bl-target symbol is one
        # we recognize as strcmp or Symbol equality (see _SYMBOL_EQ_BL_TARGETS).
        # A generic ``bl`` mismatch is far too broad — the previous gate
        # triggered on EVERY call mismatch in the function.
        for d in diagnosis.diff_ops:
            if d.target_opcode == "bl" and _is_symbol_eq_target(d.target_arg):
                if d.base_opcode in ("cmplw", "cmplwi", "cmpw", "cmpwi"):
                    return True
                # Allow paired with another bl that ISN'T a Symbol-eq (we
                # emit a strcmp but target uses a direct cmplw nearby).
                return True
            if d.base_opcode == "bl" and _is_symbol_eq_target(d.base_arg):
                if d.target_opcode in ("cmplw", "cmplwi", "cmpw", "cmpwi"):
                    return True
                return True
        # Clusters with bl whose argument is a Symbol-eq target also count
        for cluster in diagnosis.clusters:
            if "bl" not in cluster.target_opcodes and "bl" not in cluster.base_opcodes:
                continue
            # We don't have per-cluster bl-target info; fall back to checking
            # any diff_op bl with a Symbol-eq target. Already handled above.
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        for d in diagnosis.diff_ops:
            if d.target_opcode == "bl" and _is_symbol_eq_target(d.target_arg):
                if d.base_opcode in ("cmplw", "cmplwi"):
                    return 0.8
                return 0.5
            if d.base_opcode == "bl" and _is_symbol_eq_target(d.base_arg):
                if d.target_opcode in ("cmplw", "cmplwi"):
                    return 0.8
                return 0.5
        return 0.0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        # Decode TU source once for TU-level grep heuristics
        tu_source = source.decode("utf-8", errors="replace")

        # Find all == / != comparisons in the function body
        for cmp_node in find_comparisons(body, {"==", "!="}):
            if counter >= 8:
                break

            # Skip comparisons inside MILO macros (handled by milo_str_conv)
            if _inside_milo_macro(cmp_node, source):
                continue

            left = cmp_node.child_by_field_name("left")
            right = cmp_node.child_by_field_name("right")
            if left is None or right is None:
                continue

            left_text = source[left.start_byte:left.end_byte]
            right_text = source[right.start_byte:right.end_byte]

            # Determine which operands look like Symbol values
            left_is_sym = _looks_like_symbol(left, left_text, tu_source)
            right_is_sym = _looks_like_symbol(right, right_text, tu_source)

            if not left_is_sym and not right_is_sym:
                continue

            # Already has .Str() / .mStr?  Skip.
            if _already_str(left, source) and _already_str(right, source):
                continue

            # Generate variant: .Str() on each Symbol operand
            variants_to_try: list[tuple[str, list[tuple[Node, bytes]]]] = []

            # Build combinations: .Str() and .mStr
            if left_is_sym and not _already_str(left, source):
                if right_is_sym and not _already_str(right, source):
                    # Both sides are Symbol — try all combos
                    variants_to_try.append((
                        "both_str",
                        [(left, b".Str()"), (right, b".Str()")]
                    ))
                    variants_to_try.append((
                        "both_mstr",
                        [(left, b".mStr"), (right, b".mStr")]
                    ))
                    variants_to_try.append((
                        "left_str",
                        [(left, b".Str()")]
                    ))
                else:
                    variants_to_try.append((
                        "left_str",
                        [(left, b".Str()")]
                    ))
                    variants_to_try.append((
                        "left_mstr",
                        [(left, b".mStr")]
                    ))
            elif right_is_sym and not _already_str(right, source):
                variants_to_try.append((
                    "right_str",
                    [(right, b".Str()")]
                ))
                variants_to_try.append((
                    "right_mstr",
                    [(right, b".mStr")]
                ))

            for suffix, edits_list in variants_to_try:
                if counter >= 8:
                    break

                ed = SourceEditor(source)
                descs = []
                for node, accessor in edits_list:
                    ed.insert_at(node.end_byte, accessor)
                    node_str = source[node.start_byte:node.end_byte].decode(
                        "utf-8", errors="replace"
                    )
                    acc_str = accessor.decode("utf-8", errors="replace")
                    descs.append(f"{node_str}{acc_str}")

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                yield Variant(
                    name=f"symcmp_{counter}",
                    pattern_name=self.name,
                    description=f"Symbol compare: {', '.join(descs)}",
                    source=new_source,
                )
                counter += 1


def _is_symbol_eq_target(arg: str) -> bool:
    """Return True when a ``bl``-target argument names a Symbol/strcmp equality.

    Conservative: requires an exact match against ``_SYMBOL_EQ_BL_TARGETS``.
    Tolerates a leading ``@`` (objdiff sometimes prefixes mangled names).
    """
    if not arg:
        return False
    cleaned = arg.lstrip("@").rstrip(",")
    return cleaned in _SYMBOL_EQ_BL_TARGETS


def _inside_milo_macro(node: Node, source: bytes) -> bool:
    """Return True if this node is inside a MILO macro call."""
    current = node.parent
    while current is not None:
        if current.type == "call_expression":
            func = current.child_by_field_name("function")
            if func is not None:
                func_text = source[func.start_byte:func.end_byte]
                if func_text in _MILO_MACROS:
                    return True
        current = current.parent
    return False


def _looks_like_symbol(node: Node, text: bytes, tu_source: str) -> bool:
    """Heuristic: does this node look like a Symbol VALUE (not a char*)?

    We need to distinguish Symbol objects (which have .Str()/.mStr) from plain
    char* constants like gNullStr, integers, enums, and pointers.  Only Symbol
    objects need conversion.

    This function uses a POSITIVE-SIGNAL gate: an operand is only accepted
    as a Symbol when at least one strong positive signal is present.  The old
    "accept unknown identifiers as potential Symbols" logic caused a 92%
    build-failure rate by emitting `int_var.Str()` on integer/enum/pointer types.

    Positive signals (require at least one):
    1. Already has .Str() or .mStr — was proven a Symbol.
    2. Known Symbol-returning call (ClassName(), GetName(), GetSymbol(), etc.).
    3. Identifier declared as ``Symbol ident`` / ``const Symbol& ident`` in TU.
    4. Identifier used elsewhere in TU as ``ident.Str()`` or ``ident.mStr``.
    5. For field_expression: the field name itself passes a positive Symbol check.

    Explicitly EXCLUDED (always return False):
    - Known char* sentinels (gNullStr, kNullStr, etc.)
    - String literals
    - nullptr / NULL / 0
    - Identifiers declared as int/bool/float/pointer in TU
    - Identifiers that appear with arrow usage (ident->member) — they're pointers
    - Unknown identifiers without any positive signal (fail-closed)
    """
    # Known char* sentinels — do NOT add .Str() to these
    if text in _NULL_SYMBOL_NAMES:
        return False

    # Null pointer literals
    if text in (b"nullptr", b"NULL", b"0"):
        return False

    # String literals
    if node.type == "string_literal":
        return False

    # Already has .Str() or .mStr accessor
    if text.endswith(b".Str()") or text.endswith(b".mStr"):
        return True

    # A call that returns Symbol
    if node.type == "call_expression":
        func = node.child_by_field_name("function")
        if func is not None:
            method = _extract_method_name(func, node.start_byte)
            if method in (b"ClassName", b"StaticClassName", b"GetName",
                          b"GetSymbol", b"GetType"):
                return True
        return False  # Unknown calls — don't assume Symbol

    # Field expression (this->mSym, obj->mSym, obj.mSym) — check field name
    if node.type == "field_expression":
        field = node.child_by_field_name("field")
        if field is None:
            return False
        field_name = field.text
        if not field_name:
            return False
        field_str = field_name.decode("utf-8", errors="replace")
        # Require positive Symbol evidence for the field name in the TU
        return _ident_declared_as_symbol(field_str, tu_source)

    # Plain identifier — require POSITIVE Symbol evidence from TU
    if node.type == "identifier":
        if text in _KNOWN_NON_SYMBOL:
            return False

        ident = text.decode("utf-8", errors="replace")

        # Hard reject: declared as a non-Symbol type or used as a pointer
        if _ident_declared_as_non_symbol(ident, tu_source):
            return False

        # Accept only when we have positive Symbol evidence
        if _ident_declared_as_symbol(ident, tu_source):
            return True

        # No positive evidence — fail closed to avoid build errors
        return False

    return False


def _extract_method_name(func_node: Node, _start: int) -> bytes:
    """Get the rightmost identifier of a possibly-qualified function reference."""
    if func_node.type == "identifier":
        return func_node.text or b""
    if func_node.type == "field_expression":
        field = func_node.child_by_field_name("field")
        if field is not None:
            return field.text or b""
    if func_node.type == "qualified_identifier":
        name = func_node.child_by_field_name("name")
        if name is not None:
            return name.text or b""
    return b""


def _already_str(node: Node, source: bytes) -> bool:
    """Check if a node already IS or HAS .Str() / .mStr applied.

    Covers two cases:
    1. The node itself is a call_expression `foo.Str()` (the comparison operand
       IS the already-converted form).
    2. The node's parent is a field_expression or call_expression via .Str/.mStr
       (the node is the object of a .Str() call).
    """
    # Case 1: the node itself is a .Str() call or .mStr field access
    if node.type == "call_expression":
        func = node.child_by_field_name("function")
        if func is not None and func.type == "field_expression":
            field = func.child_by_field_name("field")
            if field is not None:
                field_text = source[field.start_byte:field.end_byte]
                if field_text == b"Str":
                    return True

    if node.type == "field_expression":
        field = node.child_by_field_name("field")
        if field is not None:
            field_text = source[field.start_byte:field.end_byte]
            if field_text in (b"mStr", b"Str"):
                return True

    # Case 2: the node's parent already chains .Str() / .mStr on it
    parent = node.parent
    if parent is None:
        return False

    if parent.type == "field_expression":
        field = parent.child_by_field_name("field")
        if field is not None:
            field_text = source[field.start_byte:field.end_byte]
            if field_text in (b"mStr", b"Str"):
                return True

    if parent.type == "call_expression":
        func = parent.child_by_field_name("function")
        if func is not None and func.type == "field_expression":
            field = func.child_by_field_name("field")
            if field is not None:
                field_text = source[field.start_byte:field.end_byte]
                if field_text == b"Str":
                    return True

    return False
