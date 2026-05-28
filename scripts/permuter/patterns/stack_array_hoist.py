"""Stack array hoist pattern — move large local ARRAYS/STRUCTS between scopes.

Motivating example: ``HamSkeletonConverter::Set`` declared ``worldJoints[
kNumJoints]`` inside an ``if`` block.  MSVC/MWCC's frame allocator places
arrays/structs declared inside conditional blocks at different slots than the
same array declared at function entry.  Moving the declaration up (hoist) or
down (sink) shifts the stack-frame layout and can recover the few bytes /
slot order needed for a 100% match — particularly when the diff signals a
prologue ``stwu`` immediate mismatch or a frame-size delta.

Relationship to ``scope_widening`` / ``scope_narrowing``
-------------------------------------------------------
Both of those patterns also move declarations between scopes, but their
target is *any* simple default-constructed local.  That generality is great
for callee-saved register slot inversions (the WrapText ``Line tmpLine``
case) but it does NOT target stack-frame *size* fixes.

This pattern is intentionally more restrictive:

* Only fires for **ARRAY** declarations (``T name[N]``) OR **non-primitive
  struct/class types** — exactly the variables whose presence/absence on the
  frame moves the ``stwu`` immediate or reshuffles large slot regions.
* Refuses primitive scalar types (``int``, ``float``, ``bool``, ``char``,
  ``short``, ``long``, ``double``, etc.) — those are handled adequately by
  ``scope_widening``/``scope_narrowing``.
* Allows zero/empty brace initializers (``= {}``, ``= {0}``) so it can fire
  on arrays whose initial value matters but isn't runtime-dependent.
* Fires on **frame-size** diagnostic signals (``stwu`` mismatch, large
  negative ``addic`` prologue immediate, frame_size facts) — not on the
  regswap / OFFSET_SWAP signals that gate ``scope_widening``.

In short: ``scope_widening`` is about callee-saved order; this is about
how the array lands in the frame.

Transformations
---------------

* **Hoist** (inner scope -> function scope): the headline case.
* **Sink**  (function scope -> single inner scope): inverse, fires only when
  the array/struct is used in exactly one inner ``if``/``else``/``for``/
  ``while``/``do`` body and nowhere else.

Both directions cap at 6 variants per function.

Safety
------

* No initializer with side-effects (no ``= GetCount()``) — zero / brace init
  / no init are fine.
* Variable's address is not taken outside the candidate scope.
* For sink: the array must NOT appear in the loop/if condition itself, only
  in the body.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import identifiers_in, walk
from ..types import Diagnosis, FunctionContext, Variant


# Compound scope types whose body can host (hoist destination) or originate
# from (hoist source) an array declaration.
_NARROW_SCOPE_TYPES = {
    "for_statement",
    "while_statement",
    "do_statement",
    "if_statement",
    "compound_statement",
}

# Loop/branch scopes (for narrative naming).
_LOOPS = {"for_statement", "while_statement", "do_statement"}

# Primitive scalar type-spec keywords.  A declaration whose type-specifier
# consists ENTIRELY of these tokens (e.g. ``unsigned int`` or ``long long``)
# is rejected by the struct branch — that's scope_widening's domain.
_PRIMITIVE_TOKENS = frozenset({
    "void",
    "bool",
    "char",
    "short",
    "int",
    "long",
    "float",
    "double",
    "signed",
    "unsigned",
    "size_t",
    "ssize_t",
    "ptrdiff_t",
    "intptr_t",
    "uintptr_t",
    # PowerPC fixed-width typedefs commonly used in DC3:
    "u8", "u16", "u32", "u64",
    "s8", "s16", "s32", "s64",
    "uchar", "uint", "ulong", "ushort",
    "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "const", "volatile",
})

# Minimum array element count for an unqualified `T name[N]` decl to be
# considered "large enough" to move the frame.  N may also be an identifier
# (e.g. ``kNumJoints``) — those are always accepted regardless of size.
_MIN_ARRAY_LITERAL = 4

# Variant cap (per pattern requirements).
_MAX_VARIANTS = 6


class StackArrayHoistPattern(Pattern):
    name = "stack_array_hoist"
    safety_tier = "moderate"
    structural_domain = "stack_frame"
    follow_ups = ("scope_widening", "scope_narrowing", "slot_pad")

    # ------------------------------------------------------------------
    # Relevance / priority — gate on frame-size signals, not regswaps.
    # ------------------------------------------------------------------

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Primary signal: a prologue ``stwu`` (same opcode, different immediate)
        # or ``addic`` / ``subi`` r1-arithmetic mismatch.
        for d in diagnosis.diff_ops:
            if d.target_opcode == d.base_opcode and d.target_opcode in (
                "stwu", "addic", "addi", "subi"
            ):
                return True
            # An stwu vs addi or vice-versa is also a frame-shape signal.
            pair = {d.target_opcode, d.base_opcode}
            if "stwu" in pair:
                return True
        # Secondary: clusters near the prologue (low instruction index).
        for c in diagnosis.clusters:
            if c.start_idx <= 4:
                return True
        # Tertiary: target_facts may carry a frame_size delta.
        facts = getattr(diagnosis, "target_facts", None)
        if facts is not None:
            for attr in ("frame_size_delta", "stack_frame_delta", "frame_delta"):
                val = getattr(facts, attr, None)
                if isinstance(val, (int, float)) and val != 0:
                    return True
            tags = getattr(facts, "tags", None) or ()
            if any("frame" in str(t).lower() for t in tags):
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        # Niche but precise.
        if self.relevant(diagnosis):
            return 0.4
        return 0.0

    # ------------------------------------------------------------------
    # Generation
    # ------------------------------------------------------------------

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        source = ctx.file_source

        # ----- HOIST direction: inner -> function scope -----
        for decl_node, target_body, scope_kind in _find_hoist_moves(
            ctx.body_node, source
        ):
            if counter >= _MAX_VARIANTS:
                return
            name = _get_declared_name(decl_node) or "decl"
            kind = _decl_kind_label(decl_node, source)
            new_source = _apply_hoist(source, decl_node, target_body)
            if new_source is None or new_source == source:
                continue
            yield Variant(
                name=f"stack_array_hoist_{counter}_up",
                pattern_name=self.name,
                description=(
                    f"Hoist {kind} '{name}' from {scope_kind} to function scope "
                    f"(frame-slot reordering)"
                ),
                source=new_source,
                func_byte_range=ctx.func_byte_range,
                original_source=source,
                tags=frozenset({"stack_array_hoist", "hoist_up", kind}),
            )
            counter += 1

        # ----- SINK direction: function scope -> single inner scope -----
        for decl_node, target_body, scope_kind in _find_sink_moves(
            ctx.body_node, source
        ):
            if counter >= _MAX_VARIANTS:
                return
            name = _get_declared_name(decl_node) or "decl"
            kind = _decl_kind_label(decl_node, source)
            new_source = _apply_sink(source, decl_node, target_body)
            if new_source is None or new_source == source:
                continue
            yield Variant(
                name=f"stack_array_hoist_{counter}_down",
                pattern_name=self.name,
                description=(
                    f"Sink {kind} '{name}' into {scope_kind} body "
                    f"(frame-slot reordering)"
                ),
                source=new_source,
                func_byte_range=ctx.func_byte_range,
                original_source=source,
                tags=frozenset({"stack_array_hoist", "sink_down", kind}),
            )
            counter += 1


# =====================================================================
# Declaration classification — what makes a decl "array/struct-shaped"?
# =====================================================================

def _decl_kind_label(decl: Node, source: bytes) -> str:
    """Return ``'array'`` or ``'struct'`` for a declaration this pattern
    considers eligible.  Returns ``''`` for primitives / unrecognised shapes.
    """
    if _is_array_decl(decl):
        return "array"
    if _is_struct_like_decl(decl, source):
        return "struct"
    return ""


def _is_array_decl(decl: Node) -> bool:
    """True if the declaration is a fixed-size array ``T name[N]`` with N
    being either a literal >= ``_MIN_ARRAY_LITERAL`` or an identifier
    (treated as a likely ``const`` / ``constexpr``)."""
    if decl.type != "declaration":
        return False
    declarator = decl.child_by_field_name("declarator")
    if declarator is None:
        return False
    # Walk through ``init_declarator`` / pointer wrappers to the array_declarator.
    array_decl = _find_array_declarator(declarator)
    if array_decl is None:
        return False
    size = array_decl.child_by_field_name("size")
    if size is None:
        return False  # ``T name[]`` — incomplete, skip
    if size.type == "number_literal":
        try:
            text = (size.text or b"").decode("utf-8", errors="replace")
            n = int(text, 0)
        except (ValueError, TypeError):
            return False
        return n >= _MIN_ARRAY_LITERAL
    # Identifier / constant expression — accept.
    return True


def _find_array_declarator(node: Node) -> Node | None:
    """Descend through init_declarator / pointer_declarator / reference
    wrappers to find an ``array_declarator`` if present."""
    cur = node
    seen: set[int] = set()
    while cur is not None and cur.id not in seen:
        seen.add(cur.id)
        if cur.type == "array_declarator":
            return cur
        if cur.type == "init_declarator":
            inner = cur.child_by_field_name("declarator")
            if inner is None:
                return None
            cur = inner
            continue
        if cur.type in ("pointer_declarator", "reference_declarator"):
            inner = cur.child_by_field_name("declarator")
            if inner is None:
                return None
            cur = inner
            continue
        # Leaf (identifier / field_identifier / etc.) — no array.
        return None
    return None


def _is_struct_like_decl(decl: Node, source: bytes) -> bool:
    """True if the declaration's type-specifier is a user-defined (non-primitive)
    type identifier.  This covers ``Vector3 v;``, ``Line emptyLine;``,
    ``RndText::Line tmp;``, etc.
    """
    if decl.type != "declaration":
        return False
    # Reject pointer/reference declarators — sizeof differs from the pointee.
    declarator = decl.child_by_field_name("declarator")
    if declarator is not None:
        cur = declarator
        if cur.type == "init_declarator":
            inner = cur.child_by_field_name("declarator")
            if inner is not None:
                cur = inner
        if cur.type in ("pointer_declarator", "reference_declarator"):
            return False
    type_text = _get_type_specifier_text(source, decl)
    if not type_text:
        return False
    text = type_text.decode("utf-8", errors="replace").strip()
    # Strip ``const`` / ``volatile`` / ``static`` / etc.
    tokens = text.replace("&", " ").replace("*", " ").split()
    if not tokens:
        return False
    # Drop modifier tokens to look at the "core" type.
    core = [t for t in tokens if t not in _PRIMITIVE_TOKENS and t not in (
        "static", "extern", "inline", "register", "auto", "mutable",
    )]
    if not core:
        return False  # Entirely primitive — reject.
    # The remaining tokens look like a user-defined type name.  Accept any
    # token that isn't purely a primitive keyword — covers ``Line``,
    # ``Vector3``, ``RndText::Line``, ``std::string``, ``Hmx::Color``, etc.
    for tok in core:
        if tok in _PRIMITIVE_TOKENS:
            continue
        # Identifier-like (starts with letter/underscore) — accept.
        if tok and (tok[0].isalpha() or tok[0] == "_"):
            return True
    return False


def _get_type_specifier_text(source: bytes, decl: Node) -> bytes | None:
    """Bytes of the declaration's type-specifier (everything up to the
    declarator)."""
    if decl.type != "declaration":
        return None
    declarator = decl.child_by_field_name("declarator")
    if declarator is None:
        return None
    return source[decl.start_byte:declarator.start_byte].strip()


# =====================================================================
# HOIST: find array/struct decls in inner scopes that should hoist up.
# =====================================================================

def _find_hoist_moves(
    body_node: Node, source: bytes
) -> list[tuple[Node, Node, str]]:
    """Return list of (decl_node, function_body, scope_kind) for eligible
    hoist candidates."""
    results: list[tuple[Node, Node, str]] = []
    _scan_for_hoist(body_node, body_node, source, results)
    return results


def _scan_for_hoist(
    node: Node,
    outer_compound: Node,
    source: bytes,
    results: list[tuple[Node, Node, str]],
) -> None:
    for child in node.children:
        if child.type in _NARROW_SCOPE_TYPES and child != outer_compound:
            inner_body = _get_scope_body(child)
            if inner_body is not None and inner_body != outer_compound:
                for stmt in inner_body.named_children:
                    if stmt.type != "declaration":
                        continue
                    if not _decl_kind_label(stmt, source):
                        continue
                    if not _is_eligible_initializer(stmt):
                        continue
                    var_name = _get_declared_name(stmt)
                    if var_name is None:
                        continue
                    if _is_address_taken_outside(
                        outer_compound, var_name, source, inner_body
                    ):
                        continue
                    if _has_outer_decl(outer_compound, var_name):
                        continue
                    if not _name_confined_to_scope(
                        outer_compound, inner_body, var_name
                    ):
                        continue
                    results.append((stmt, outer_compound, _scope_kind_for(child)))
            _scan_for_hoist(child, outer_compound, source, results)
        else:
            _scan_for_hoist(child, outer_compound, source, results)


def _is_eligible_initializer(decl: Node) -> bool:
    """True if the decl has no initializer OR has a zero-/brace-init (no
    runtime call expression)."""
    declarator = decl.child_by_field_name("declarator")
    if declarator is None:
        return False
    if declarator.type != "init_declarator":
        return True  # bare declaration, no initializer
    init = declarator.child_by_field_name("value")
    if init is None:
        return True
    # Accept brace-init / initializer-list (e.g. ``= {}``, ``= {0}``,
    # ``= {0, 0, 0}``) and number/character literals.
    if init.type in ("initializer_list", "number_literal", "char_literal",
                     "true", "false", "null"):
        return True
    return False


def _name_confined_to_scope(
    outer_compound: Node, inner_body: Node, var_name: str
) -> bool:
    """True if `var_name` only appears within `inner_body` (no escape).

    Walks ``outer_compound`` and checks every identifier — those that are
    NOT inside ``inner_body`` must not match ``var_name``.
    """
    inner_start = inner_body.start_byte
    inner_end = inner_body.end_byte
    for node in walk(outer_compound):
        if node.type != "identifier":
            continue
        # Inside the inner body?  That's fine.
        if inner_start <= node.start_byte and node.end_byte <= inner_end:
            continue
        if node.text and node.text.decode("utf-8", errors="replace") == var_name:
            return False
    return True


# =====================================================================
# SINK: find function-scope array/struct decls that can sink into a
# single inner scope where they're used exclusively.
# =====================================================================

def _find_sink_moves(
    body_node: Node, source: bytes
) -> list[tuple[Node, Node, str]]:
    results: list[tuple[Node, Node, str]] = []
    stmts = list(body_node.named_children)
    for i, stmt in enumerate(stmts):
        if stmt.type != "declaration":
            continue
        if not _decl_kind_label(stmt, source):
            continue
        if not _is_eligible_initializer(stmt):
            continue
        var_name = _get_declared_name(stmt)
        if var_name is None:
            continue
        if _is_address_taken_in_subtree(body_node, var_name, source):
            continue
        # Find sibling statements that reference var_name.
        use_indices = [
            j for j in range(i + 1, len(stmts))
            if var_name in identifiers_in(stmts[j])
        ]
        if len(use_indices) != 1:
            continue  # used by multiple top-level statements -> don't sink
        target_sibling = stmts[use_indices[0]]
        target_body = _resolve_single_inner_body(target_sibling, var_name)
        if target_body is None:
            continue
        results.append((stmt, target_body, _scope_kind_for(target_sibling)))
    return results


def _resolve_single_inner_body(
    sibling: Node, var_name: str
) -> Node | None:
    """If `sibling` is an if/loop and `var_name` is used in exactly one of
    its bodies (NOT the condition), return that body."""
    if sibling.type == "if_statement":
        cond = sibling.child_by_field_name("condition")
        if cond is not None and var_name in identifiers_in(cond):
            return None
        cons = sibling.child_by_field_name("consequence")
        alt = sibling.child_by_field_name("alternative")
        in_cons = (
            cons is not None
            and cons.type == "compound_statement"
            and var_name in identifiers_in(cons)
        )
        # Extract alt body if present.
        alt_body = None
        in_alt = False
        if alt is not None:
            if alt.type == "compound_statement":
                alt_body = alt
            elif alt.type == "else_clause":
                for ch in alt.children:
                    if ch.type == "compound_statement":
                        alt_body = ch
                        break
            if alt_body is not None and var_name in identifiers_in(alt_body):
                in_alt = True
        if in_cons and not in_alt:
            return cons
        if in_alt and not in_cons:
            return alt_body
        return None
    if sibling.type in _LOOPS:
        # Reject if the var is part of the loop control (init/cond/update).
        for fname in ("initializer", "condition", "update"):
            f = sibling.child_by_field_name(fname)
            if f is not None and var_name in identifiers_in(f):
                return None
        body = sibling.child_by_field_name("body")
        if body is None:
            for ch in sibling.children:
                if ch.type == "compound_statement":
                    body = ch
                    break
        if body is not None and body.type == "compound_statement":
            if var_name in identifiers_in(body):
                return body
    return None


# =====================================================================
# Shared helpers
# =====================================================================

def _get_scope_body(scope_node: Node) -> Node | None:
    if scope_node.type == "compound_statement":
        return scope_node
    body = scope_node.child_by_field_name("body")
    if body is not None and body.type == "compound_statement":
        return body
    for field in ("consequence", "alternative"):
        c = scope_node.child_by_field_name(field)
        if c is None:
            continue
        if c.type == "compound_statement":
            return c
        if c.type == "else_clause":
            for ch in c.children:
                if ch.type == "compound_statement":
                    return ch
    for child in scope_node.children:
        if child.type == "compound_statement":
            return child
    return None


def _scope_kind_for(scope_node: Node) -> str:
    if scope_node.type in _LOOPS:
        return "loop"
    if scope_node.type == "if_statement":
        return "if"
    if scope_node.type == "compound_statement":
        return "block"
    return scope_node.type


def _has_outer_decl(outer_compound: Node, var_name: str) -> bool:
    for child in outer_compound.named_children:
        if child.type == "declaration":
            if _get_declared_name(child) == var_name:
                return True
    return False


def _is_address_taken_outside(
    scope: Node, var_name: str, source: bytes, exclude: Node
) -> bool:
    """True if ``&var_name`` appears anywhere in ``scope`` outside of
    ``exclude``.  We're strict — any ``&`` escapes the inner scope's
    lifetime.
    """
    excl_start = exclude.start_byte
    excl_end = exclude.end_byte
    for node in walk(scope):
        if node.type != "pointer_expression":
            continue
        children = node.children
        if len(children) != 2:
            continue
        op_text = source[children[0].start_byte:children[0].end_byte]
        if op_text != b"&":
            continue
        if children[1].type != "identifier":
            continue
        ident_text = children[1].text
        if not ident_text:
            continue
        if ident_text.decode("utf-8", errors="replace") != var_name:
            continue
        if excl_start <= node.start_byte and node.end_byte <= excl_end:
            continue  # inside the inner body — OK
        return True
    return False


def _is_address_taken_in_subtree(
    scope: Node, var_name: str, source: bytes
) -> bool:
    for node in walk(scope):
        if node.type != "pointer_expression":
            continue
        children = node.children
        if len(children) != 2:
            continue
        op_text = source[children[0].start_byte:children[0].end_byte]
        if op_text != b"&":
            continue
        if children[1].type != "identifier":
            continue
        if not children[1].text:
            continue
        if children[1].text.decode("utf-8", errors="replace") == var_name:
            return True
    return False


def _get_declared_name(decl: Node) -> str | None:
    if decl.type != "declaration":
        return None
    declarator = decl.child_by_field_name("declarator")
    if declarator is None:
        return None
    cur = declarator
    seen: set[int] = set()
    while cur is not None and cur.id not in seen:
        seen.add(cur.id)
        if cur.type == "init_declarator":
            inner = cur.child_by_field_name("declarator")
            if inner is None:
                break
            cur = inner
            continue
        if cur.type in (
            "pointer_declarator", "reference_declarator", "array_declarator",
        ):
            inner = cur.child_by_field_name("declarator")
            if inner is None:
                break
            cur = inner
            continue
        # Leaf identifier (or field_identifier in member-decls).
        if cur.text:
            return cur.text.decode("utf-8", errors="replace")
        return None
    return None


# =====================================================================
# Source-edit application
# =====================================================================

def _apply_hoist(
    source: bytes, decl_node: Node, target_compound: Node
) -> bytes | None:
    """Move the declaration line to the top of ``target_compound``'s body.

    ``target_compound`` is the outer compound (function body), and
    ``decl_node`` is currently inside an inner scope.
    """
    decl_line_start = _line_start(source, decl_node.start_byte)
    decl_line_end = _line_end(source, decl_node.end_byte)
    decl_text = source[decl_node.start_byte:decl_node.end_byte]

    insert_pos = target_compound.start_byte + 1  # past '{'
    if insert_pos < len(source) and source[insert_pos:insert_pos + 1] == b"\n":
        insert_pos += 1
    if insert_pos > decl_line_start:
        return None  # not actually outward

    target_indent = _get_body_indent(source, target_compound)
    new_decl_line = target_indent + decl_text + b"\n"

    return (
        source[:insert_pos]
        + new_decl_line
        + source[insert_pos:decl_line_start]
        + source[decl_line_end:]
    )


def _apply_sink(
    source: bytes, decl_node: Node, target_body: Node
) -> bytes | None:
    """Move the declaration line from function scope into ``target_body``.

    ``decl_node`` is at function scope (a sibling of ``target_body``'s
    enclosing if/loop), so the insertion point is AFTER the decl.
    """
    decl_line_start = _line_start(source, decl_node.start_byte)
    decl_line_end = _line_end(source, decl_node.end_byte)
    decl_text = source[decl_node.start_byte:decl_node.end_byte]

    insert_pos = target_body.start_byte + 1  # past '{'
    if insert_pos < len(source) and source[insert_pos:insert_pos + 1] == b"\n":
        insert_pos += 1
    if insert_pos < decl_line_end:
        return None  # target precedes the decl — wrong direction

    target_indent = _get_body_indent(source, target_body)
    new_decl_line = target_indent + decl_text + b"\n"

    # Remove the decl line first, then insert (adjust position).
    removed = source[:decl_line_start] + source[decl_line_end:]
    shift = decl_line_end - decl_line_start
    adj_insert = insert_pos - shift
    if adj_insert < 0:
        return None
    return removed[:adj_insert] + new_decl_line + removed[adj_insert:]


def _get_body_indent(source: bytes, body_node: Node) -> bytes:
    for child in body_node.named_children:
        pos = child.start_byte
        line_start = _line_start(source, pos)
        indent = b""
        for i in range(line_start, pos):
            ch = source[i:i + 1]
            if ch in (b" ", b"\t"):
                indent += ch
            else:
                break
        return indent
    pos = body_node.start_byte
    line_start = _line_start(source, pos)
    indent = b""
    for i in range(line_start, pos):
        ch = source[i:i + 1]
        if ch in (b" ", b"\t"):
            indent += ch
        else:
            break
    return indent + b"    "


def _line_start(source: bytes, pos: int) -> int:
    while pos > 0 and source[pos - 1:pos] not in (b"\n", b"\r"):
        pos -= 1
    return pos


def _line_end(source: bytes, pos: int) -> int:
    length = len(source)
    while pos < length and source[pos:pos + 1] not in (b"\n", b""):
        pos += 1
    if pos < length and source[pos:pos + 1] == b"\n":
        pos += 1
    return pos
