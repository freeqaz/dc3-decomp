"""POD-ness toggle for small structs — add/remove a user ctor to flip the
stlport vector-internal copy path between word copies and typed member copies.

stlport's vector-internal copy templates (``_M_insert_overflow_aux<T>``,
``_M_fill_insert<T>``, ``__copy_ptrs<T*>``) choose between WORD copies
(``lwz``/``stw``) and TYPED member copies (``lfs``/``stfs`` for floats,
``lhz``/``sth`` for u16) based on ``__type_traits<T>::is_POD_type``.

MWCC's POD detection flips on *any* user-declared ctor / dtor / copy op /
virtual. A structurally-POD aggregate (``struct T { int a; float b; };`` with no
user ctor) is ``__true_type`` and gets word-copied; the same struct with an
empty ``T() {}`` becomes ``__false_type`` and gets member-by-member typed copies.

Two transforms (inverses):

1. **add-empty-ctor** — a POD struct gains ``T() {}`` so it becomes non-POD and
   the copy path emits typed loads/stores.
   (Win: ``_M_insert_overflow_aux<GemPlayer::UpcomingFretRelease>`` 94.5->100%.)

2. **remove-dead-ctor** — a struct with a DEAD user ctor (empty body, never
   constructed with args anywhere in the file) loses its ctor(s) so it becomes
   POD and the copy path emits word copies.
   (Win: ``ChordShapeGenerator::Edge`` ``_M_insert_overflow_aux`` 88.15->100%.)

FEASIBILITY / SCOPE
-------------------
The permuter's ``FunctionContext.file_source`` is the raw ``.cpp`` bytes only —
no preprocessor, no ``#include`` expansion (see ``extractor.extract_function``:
``source = file_path.read_bytes()``). It therefore CANNOT see struct definitions
that live in a header. This pattern is deliberately scoped to structs DEFINED
textually in the ``.cpp`` itself (the ``ChordShapeGenerator::Edge`` win was
exactly such a ``.cpp``-local struct). Header-defined structs are out of reach
of the permuter and must be toggled by hand.

The add-direction is comparatively safe (adding an empty ctor never changes
program behaviour). The remove-direction is risky — removing a ctor that is
actually *called* (``T(a, b)`` / ``push_back(T(a, b))``) breaks the build — so it
is gated behind a call-site scan.

The pattern is DEFAULT-ENABLED (``opt_in = False``). A ``_is_value_copied()``
gate in ``generate()`` restricts firing to structs stored BY VALUE in a
container — the only place POD-ness changes codegen. A struct used solely via
pointers (intrusive ``T *mNext`` list) is skipped: toggling its ctor is a no-op
there, and such structs are typically referenced by many functions. This keeps
the per-function-scored climber's measurement equal to the true per-TU effect,
which is what makes default-on safe (verified by a corpus stress-test over 884
files: 0 build-break gate-leaks, 0 parse errors).
"""

from __future__ import annotations

import re
from typing import Iterator, Optional

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


# Word-copy opcodes (POD / bit-copy path).
_WORD_OPS = {"lwz", "stw", "lwzu", "stwu", "lwzx", "stwx"}
# Typed member-copy opcodes (non-POD / memberwise path): floats + u16/u8 halves.
_TYPED_OPS = {
    "lfs", "stfs", "lfd", "stfd",
    "lhz", "sth", "lha", "lhau", "lhzu", "sthu",
    "lbz", "stb", "lbzu", "stbu",
}

# Primitive / well-known POD scalar type names. Pointers (`Foo *`) are POD too
# and handled separately (any type with a trailing `*` is accepted).
_POD_SCALAR_TYPES = {
    "bool", "char", "short", "int", "long", "float", "double",
    "signed", "unsigned", "void",
    "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64",
    "uchar", "ushort", "uint", "ulong", "uint8", "uint16", "uint32", "uint64",
    "int8", "int16", "int32", "int64", "size_t", "ssize_t", "byte", "word",
    "f32", "f64", "wchar_t",
}

# Approximate byte sizes for the small-struct gate. Conservative: anything not
# listed is treated as 4 bytes (a pointer / int-ish field).
_TYPE_SIZES = {
    "bool": 1, "char": 1, "signed char": 1, "unsigned char": 1,
    "u8": 1, "s8": 1, "uchar": 1, "int8": 1, "uint8": 1, "byte": 1,
    "short": 2, "unsigned short": 2, "u16": 2, "s16": 2, "ushort": 2,
    "int16": 2, "uint16": 2, "word": 2, "wchar_t": 2,
    "int": 4, "unsigned int": 4, "unsigned": 4, "long": 4,
    "unsigned long": 4, "u32": 4, "s32": 4, "uint": 4, "ulong": 4,
    "int32": 4, "uint32": 4, "float": 4, "f32": 4, "size_t": 4, "ssize_t": 4,
    "double": 8, "u64": 8, "s64": 8, "int64": 8, "uint64": 8, "f64": 8,
    "long long": 8, "unsigned long long": 8,
}

# Small-struct upper bound (bytes). Word-vs-typed copy choice only matters for
# structs the compiler can move with a handful of loads/stores.
_MAX_STRUCT_BYTES = 16

# Max variants per generate() call — keep the search budget bounded.
_MAX_VARIANTS = 6


class PodCtorTogglePattern(Pattern):
    name = "pod_ctor_toggle"
    # Default-enabled after a corpus stress-test (884 files): the safety gates
    # held (0 build-break leaks, 0 parse errors) and the apparent per-TU
    # blast-radius risk proved illusory — a struct's name-reference count badly
    # over-estimates its codegen-affected count (adding FreeBlock(){} to
    # MemMgr.cpp moved 0 of 15 referencing functions, because it is never
    # value-copied). The _is_value_copied() gate in generate() restricts firing
    # to structs stored BY VALUE in a container, which is exactly where
    # POD-ness changes codegen — so the per-function-scored climber's
    # measurement equals the true per-TU effect. The remove-direction stays
    # behind its triple gate (dead-body + call-site + out-of-line-def scans).
    opt_in = False
    safety_tier = "moderate"
    # Mutates a struct definition at file scope (outside the target function's
    # byte range), like accessor_outline. Variants intentionally leave
    # func_byte_range / original_source unset so the strict per-function scope
    # check in types.variant_file_updates is skipped (same convention as
    # initializer_literal, which also edits beyond the immediate statement).
    structural_domain = "cross_unit"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        """True when diff_ops show a word-op vs typed-op divergence.

        The POD-vs-typed copy difference shows up as a mismatch where one side
        is a word op (``lwz``/``stw``) and the other is a typed op
        (``lfs``/``stfs``/``lhz``/``sth``). Either polarity is in scope — the
        add-direction fixes base-word/target-typed, the remove-direction fixes
        base-typed/target-word.
        """
        for d in diagnosis.diff_ops:
            t, b = d.target_opcode, d.base_opcode
            if (t in _WORD_OPS and b in _TYPED_OPS) or (
                t in _TYPED_OPS and b in _WORD_OPS
            ):
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        return 0.7 if self.relevant(diagnosis) else 0.0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0

        # Reparse the whole file to find struct/class definitions at any scope.
        # ctx.func_node lives in the same tree; walk from its root.
        root = _root_of(ctx.func_node)
        if root is None:
            return

        structs = _find_cpp_local_structs(root, source)

        # VALUE-COPY GATE: POD-ness only changes codegen for a struct that is
        # value-copied through stlport's POD-branching copy templates
        # (`_M_insert_overflow_aux<T>` etc.), i.e. one stored BY VALUE in a
        # container. A struct used solely via pointers (an intrusive `T *mNext`
        # list) never hits that path, so toggling its ctor is a no-op AND such
        # structs tend to be referenced by many functions (e.g. FreeBlock in
        # MemMgr.cpp: 15 referencing functions, 0 affected by adding a ctor).
        # Gating here keeps the per-function-scored climber's measurement equal
        # to the true (per-TU) effect, which is what makes the pattern safe to
        # run by default.
        structs = [s for s in structs if _is_value_copied(source, s.name)]

        # Polarity hint from the diagnosis: prefer the direction the asm asks
        # for, but still emit both if uncertain.
        prefer_add, prefer_remove = _polarity_hint(ctx.diagnosis)

        for struct in structs:
            if counter >= _MAX_VARIANTS:
                return

            has_ctor = struct.user_ctor_nodes or struct.has_dtor

            if not has_ctor:
                # POD already -> add an empty ctor to force typed copies.
                if prefer_remove and not prefer_add:
                    continue
                variant = _make_add_ctor_variant(source, struct, counter)
                if variant is not None:
                    yield variant
                    counter += 1
            else:
                # Has a user ctor -> consider removing it (POD-ify -> word copies).
                if prefer_add and not prefer_remove:
                    continue
                if not struct.ctors_are_dead:
                    continue
                if _struct_constructed_with_args(source, struct):
                    # A call site constructs T(args) -> removing breaks the build.
                    continue
                if _has_out_of_line_special_member(source, struct):
                    # An out-of-line `T::T(...)` / `T::~T()` definition would be
                    # orphaned if we deleted the in-class declaration.
                    continue
                variant = _make_remove_ctor_variant(source, struct, counter)
                if variant is not None:
                    yield variant
                    counter += 1


# ---------------------------------------------------------------------------
# Struct model
# ---------------------------------------------------------------------------


class _StructInfo:
    """Lightweight description of a small .cpp-local struct/class definition."""

    __slots__ = (
        "name",
        "spec_node",
        "body_node",
        "field_nodes",
        "user_ctor_nodes",
        "has_dtor",
        "ctors_are_dead",
        "est_bytes",
    )

    def __init__(
        self,
        name: str,
        spec_node: Node,
        body_node: Node,
    ) -> None:
        self.name = name
        self.spec_node = spec_node
        self.body_node = body_node
        self.field_nodes: list[Node] = []
        self.user_ctor_nodes: list[Node] = []
        self.has_dtor = False
        self.ctors_are_dead = True
        self.est_bytes = 0


def _root_of(node: Node) -> Optional[Node]:
    """Return the tree's root node by walking parents."""
    cur = node
    while cur is not None and cur.parent is not None:
        cur = cur.parent
    return cur


def _find_cpp_local_structs(root: Node, source: bytes) -> list[_StructInfo]:
    """Find small, POD-ish struct/class definitions textually in the source.

    Only matches definitions with a name AND a body (skips forward decls and
    anonymous structs). Filters to "small POD-ish": scalar / float / small
    fixed-array / pointer members only, estimated size <= _MAX_STRUCT_BYTES.
    """
    out: list[_StructInfo] = []
    seen: set[int] = set()

    for node in walk(root):
        if node.type not in ("struct_specifier", "class_specifier"):
            continue
        name_node = node.child_by_field_name("name")
        body_node = node.child_by_field_name("body")
        if name_node is None or body_node is None:
            continue  # forward decl / anonymous / elaborated use
        if node.id in seen:
            continue
        seen.add(node.id)

        # Reject inheritance — a base class can carry non-POD-ness / vtables
        # we can't see, and changing ctors there is unsafe.
        if _has_base_clause(node):
            continue

        name = source[name_node.start_byte : name_node.end_byte].decode(
            "utf-8", errors="replace"
        )
        if not name:
            continue

        info = _StructInfo(name, node, body_node)
        if not _classify_members(info, source):
            continue
        if info.est_bytes == 0 or info.est_bytes > _MAX_STRUCT_BYTES:
            continue
        out.append(info)

    return out


def _has_base_clause(spec_node: Node) -> bool:
    for c in spec_node.children:
        if c.type == "base_class_clause":
            return True
    return False


def _classify_members(info: _StructInfo, source: bytes) -> bool:
    """Populate field/ctor info and check all data members are POD-ish.

    Returns False (reject the struct) if any data member is non-POD-ish
    (template type, by-value class type, reference, bitfield) or if the struct
    has a virtual function / copy-assign operator (already non-POD in a way we
    can't toggle by ctor alone).
    """
    body = info.body_node

    for child in body.children:
        if child.type == "function_definition":
            # Could be a ctor / dtor / operator / method defined inline.
            kind = _member_decl_kind(child, info.name, source)
            if kind == "ctor":
                info.user_ctor_nodes.append(child)
                if not _ctor_body_is_empty(child):
                    info.ctors_are_dead = False
            elif kind == "dtor":
                info.has_dtor = True
                if not _ctor_body_is_empty(child):
                    info.ctors_are_dead = False
            elif kind == "operator_assign" or kind == "virtual":
                return False
            # other methods are fine (don't affect POD-ness)
            continue

        # A no-inline-body member surfaces as either `field_declaration`
        # (data members, method *declarations*) or `declaration` (tree-sitter
        # parses an in-class ctor/dtor declaration like `Edge(u16, u16);` as a
        # plain `declaration`). Handle both.
        if child.type not in ("field_declaration", "declaration"):
            continue

        kind = _member_decl_kind(child, info.name, source)
        if kind == "ctor":
            # A bare declaration (no inline body). It contributes nothing to
            # emit on its own, so it stays "dead" UNLESS a call site constructs
            # T(args) (checked separately) or an out-of-line definition exists
            # (also checked separately, in _has_out_of_line_special_member).
            info.user_ctor_nodes.append(child)
            continue
        if kind == "dtor":
            # Declaration-only dtor: same reasoning as the ctor declaration —
            # harmless to remove unless an out-of-line body exists.
            info.has_dtor = True
            continue
        if kind == "operator_assign" or kind == "virtual":
            return False
        if kind == "method":
            # Method declaration (no body) — doesn't affect POD-ness.
            continue

        # `declaration` nodes that aren't special members are not data fields
        # we model (e.g. typedef/using/static). Reject conservatively only for
        # field_declaration; ignore other declarations.
        if child.type != "field_declaration":
            continue

        # A real data member. Validate it's POD-ish.
        sz = _field_pod_size(child, source)
        if sz is None:
            return False
        info.field_nodes.append(child)
        info.est_bytes += sz

    # Must have at least one data field to be a meaningful copy target.
    return bool(info.field_nodes)


def _member_decl_kind(node: Node, struct_name: str, source: bytes) -> str:
    """Classify a class-body child node.

    Returns one of: "ctor", "dtor", "operator_assign", "virtual", "method",
    "field", "other".
    """
    # virtual specifier anywhere in the declaration prefix -> non-POD.
    for c in node.children:
        if c.type == "virtual" or (
            c.type == "virtual_specifier"
        ):
            return "virtual"
        # tree-sitter often surfaces `virtual` as a bare keyword token.
        if c.type == "storage_class_specifier" and c.text == b"virtual":
            return "virtual"
    # Some grammars expose `virtual` as plain text in the node.
    head = source[node.start_byte : min(node.end_byte, node.start_byte + 16)]
    if head.lstrip().startswith(b"virtual"):
        return "virtual"

    decl = node.child_by_field_name("declarator")
    if decl is None:
        return "other"

    # Drill to the function_declarator if present.
    fdecl = _find_function_declarator(decl)
    name_node = None
    if fdecl is not None:
        name_node = fdecl.child_by_field_name("declarator")
    else:
        name_node = decl

    if name_node is None:
        return "field"

    nt = name_node.type
    if nt == "destructor_name":
        return "dtor"
    if nt == "operator_name":
        op_text = source[name_node.start_byte : name_node.end_byte]
        if op_text.replace(b" ", b"") == b"operator=":
            return "operator_assign"
        return "method"

    name_text = source[name_node.start_byte : name_node.end_byte].decode(
        "utf-8", errors="replace"
    )
    if name_text == struct_name and fdecl is not None:
        return "ctor"
    if name_text == "~" + struct_name:
        return "dtor"
    if fdecl is not None:
        return "method"
    return "field"


def _find_function_declarator(decl: Node) -> Optional[Node]:
    """Drill through pointer/reference decorators to a function_declarator."""
    cur = decl
    while cur is not None:
        if cur.type == "function_declarator":
            return cur
        if cur.type in ("pointer_declarator", "reference_declarator"):
            cur = cur.child_by_field_name("declarator")
            continue
        return None
    return None


def _ctor_body_is_empty(func_node: Node) -> bool:
    """True if a ctor/dtor function_definition has an empty/trivial body.

    Empty = `{}` (no named statements) and no member-initializer list with
    side effects. A field-initializer list is treated as non-trivial (it may
    set defaults the word-copy path would skip), so only a truly empty `{}`
    qualifies as dead.
    """
    # Reject if there's a field_initializer_list (`: a(0), b(1)`).
    for c in func_node.children:
        if c.type == "field_initializer_list":
            return False
    body = func_node.child_by_field_name("body")
    if body is None:
        return False
    named = [c for c in body.named_children if c.type != "comment"]
    return len(named) == 0


def _field_pod_size(field_node: Node, source: bytes) -> Optional[int]:
    """Estimate the byte size of a data-member declaration, or None if non-POD.

    Accepts scalar/float primitives, pointers (any pointee), and small fixed
    arrays of those. Rejects references, bitfields, template-typed members, and
    by-value class types (uppercase non-primitive type names without a `*`).
    """
    # Bitfields are non-trivial for the word-copy assumption — reject.
    for c in walk(field_node):
        if c.type == "bitfield_clause":
            return None

    type_node = field_node.child_by_field_name("type")
    if type_node is None:
        return None

    # Reconstruct the type text incl. leading qualifiers (const/volatile that
    # appear as separate `type_qualifier` siblings BEFORE the type node). Do NOT
    # collect sized_type_specifier — that already IS the full `unsigned short`
    # type text. Compare nodes by .id (tree-sitter wrappers aren't identity-
    # stable across child_by_field_name vs children iteration).
    quals: list[bytes] = []
    for c in field_node.children:
        if c.id == type_node.id:
            break
        if c.type == "type_qualifier":
            quals.append(source[c.start_byte : c.end_byte])
    type_text = source[type_node.start_byte : type_node.end_byte]
    if quals:
        type_text = b" ".join(quals) + b" " + type_text
    type_str = type_text.decode("utf-8", errors="replace").strip()

    # Templates / nested-qualified class types are out of scope.
    if "<" in type_str:
        return None

    # Examine declarators to detect pointers / arrays / references and the
    # per-declarator multiplicity. A single field_declaration may declare
    # several members (`int a, b;`).
    total = 0
    declared_any = False
    for decl in field_node.children:
        if decl.type not in (
            "field_identifier",
            "pointer_declarator",
            "reference_declarator",
            "array_declarator",
        ):
            continue
        declared_any = True
        sz = _declarator_size(decl, type_str, source)
        if sz is None:
            return None
        total += sz

    if not declared_any:
        # No declarator child surfaced (e.g. anonymous) — reject conservatively.
        return None
    return total


def _declarator_size(
    decl: Node, type_str: str, source: bytes
) -> Optional[int]:
    """Size of one declarator given its base type string."""
    is_pointer = False
    array_count = 1
    cur = decl
    while cur is not None:
        if cur.type == "reference_declarator":
            # References make the struct non-POD.
            return None
        if cur.type == "pointer_declarator":
            is_pointer = True
            cur = cur.child_by_field_name("declarator")
            continue
        if cur.type == "array_declarator":
            size_node = cur.child_by_field_name("size")
            n = _array_extent(size_node, source)
            if n is None:
                return None
            array_count *= n
            cur = cur.child_by_field_name("declarator")
            continue
        break

    if is_pointer:
        base = 4  # 32-bit target — every pointer is 4 bytes.
    else:
        base = _base_type_size(type_str)
        if base is None:
            return None
    return base * array_count


def _array_extent(size_node: Optional[Node], source: bytes) -> Optional[int]:
    """Parse a fixed array extent. Only integer literals are accepted."""
    if size_node is None:
        return None
    text = source[size_node.start_byte : size_node.end_byte].strip()
    if re.fullmatch(rb"\d+", text):
        n = int(text)
        if 0 < n <= 16:  # keep arrays small to stay under the size gate
            return n
    return None


def _base_type_size(type_str: str) -> Optional[int]:
    """Return the size of a non-pointer base type, or None if non-POD."""
    s = type_str.strip()
    # Drop a leading const.
    s = re.sub(r"^const\s+", "", s)
    s = re.sub(r"\s+", " ", s).strip()

    if s in _TYPE_SIZES:
        return _TYPE_SIZES[s]
    if s in _POD_SCALAR_TYPES:
        return _TYPE_SIZES.get(s, 4)

    # Single-token type that isn't a known primitive: if it starts uppercase
    # it's most likely a class/struct value member -> can't assume POD.
    if "::" in s:
        return None
    if " " in s:
        # Multi-word non-primitive (e.g. `class Foo`) -> reject.
        return None
    if s[:1].isupper():
        return None
    # Lowercase unknown single token: could be a project typedef of a scalar
    # (e.g. an enum alias). Be conservative and reject so we never wrongly
    # POD-ify a non-trivial type.
    return None


def _is_value_copied(source: bytes, struct_name: str) -> bool:
    """True if the struct is USED BY VALUE (so its POD-ness can affect codegen).

    POD-ness only changes codegen for a struct that is copied/stored by value.
    A struct used solely through pointers (an intrusive ``T *mNext`` list) never
    hits stlport's ``__type_traits<T>::is_POD_type`` copy branch, so toggling
    its ctor is a no-op — and such pointer-only structs tend to be referenced by
    many functions (the FreeBlock/MemMgr 15-refs / 0-affected case). Excluding
    them keeps the per-function-scored climber honest about the per-TU effect.

    Two by-value signals (either suffices):
      1. container / by-value template argument: ``<T>`` ``<T,`` ``,T>`` ``,T,``
         (a pointer element ``<T*>`` is NOT matched — pointee POD-ness is moot);
      2. a value-typed declaration ``T ident`` (local, member, by-value param /
         return). ``T *p`` / ``T &r`` are NOT matched (pointer/reference don't
         copy), nor is the ctor decl ``T(`` (followed by ``(``, not an ident).

    Conservative on the safe side: a false positive only costs a wasted no-op
    build (the climber rejects the unchanged variant); a false negative just
    declines a struct the pattern probably could not have moved anyway.
    """
    name = re.escape(struct_name.encode())
    by_value_template = re.compile(rb"[<,]\s*(?:const\s+)?" + name + rb"\s*[,>]")
    if by_value_template.search(source):
        return True
    # `T ident` — value-typed declaration. The lookahead for an identifier start
    # excludes `T *p`, `T &r`, and the in-class ctor decl `T(`.
    value_decl = re.compile(rb"\b" + name + rb"\s+[A-Za-z_]")
    return value_decl.search(source) is not None


def _struct_constructed_with_args(source: bytes, info: _StructInfo) -> bool:
    """Scan the file for argument-bearing construction of the struct.

    A textual scan (the file_source is just the .cpp). Two dangerous forms are
    matched:
      1. Functional/temporary cast:  ``Name(<non-empty>)``  — e.g.
         ``push_back(Edge(1, 2))``.
      2. Direct-init declaration:     ``Name ident(<non-empty>)`` — e.g.
         ``Edge e(1, 2);``.
    Either means removing the ctor breaks the build. The struct's own in-body
    ctor declarations (and any out-of-line definitions, handled separately) are
    ignored via a byte-range skip on the struct body.

    Deliberately conservative — false positives only cost us a skipped variant.
    """
    name_b = re.escape(info.name.encode())
    # Form 1: `Name(` immediately (whitespace ok) followed by a non-`)` token.
    func_cast = re.compile(rb"\b" + name_b + rb"\s*\(\s*(?!\))")
    # Form 2: `Name <ident>(` followed by a non-`)` token. The intervening
    # identifier distinguishes a direct-init declaration from a method/field.
    direct_init = re.compile(
        rb"\b" + name_b + rb"\s+[A-Za-z_]\w*\s*\(\s*(?!\))"
    )

    body_lo = info.spec_node.start_byte
    body_hi = info.spec_node.end_byte
    for pat in (func_cast, direct_init):
        for m in pat.finditer(source):
            pos = m.start()
            # Skip the struct's own ctor declarations (inside its body range).
            if body_lo <= pos < body_hi:
                continue
            return True
    return False


def _has_out_of_line_special_member(source: bytes, info: _StructInfo) -> bool:
    """Detect an out-of-line `Name::Name(` or `Name::~Name(` definition.

    Removing the in-class ctor/dtor *declaration* while such an out-of-line
    *definition* still exists in the file would orphan it (compile error). The
    scan ignores text inside the struct body's own byte range.
    """
    name = info.name
    pat = re.compile(
        rb"\b" + re.escape(name.encode()) + rb"\s*::\s*~?" +
        re.escape(name.encode()) + rb"\s*\(",
    )
    for m in pat.finditer(source):
        pos = m.start()
        if info.spec_node.start_byte <= pos < info.spec_node.end_byte:
            continue
        return True
    return False


def _polarity_hint(diagnosis: Optional[Diagnosis]) -> tuple[bool, bool]:
    """Decide preferred direction(s) from the diagnosis diff_ops.

    Returns (prefer_add, prefer_remove).
    - base emits word, target emits typed -> we must become non-POD -> ADD ctor.
    - base emits typed, target emits word -> we must become POD -> REMOVE ctor.
    When the signal is mixed or absent, both are allowed.
    """
    if diagnosis is None:
        return (True, True)
    add = False
    remove = False
    for d in diagnosis.diff_ops:
        t, b = d.target_opcode, d.base_opcode
        if b in _WORD_OPS and t in _TYPED_OPS:
            add = True
        elif b in _TYPED_OPS and t in _WORD_OPS:
            remove = True
    if not add and not remove:
        return (True, True)
    return (add, remove)


# ---------------------------------------------------------------------------
# Variant construction
# ---------------------------------------------------------------------------


def _struct_indent(source: bytes, info: _StructInfo) -> bytes:
    """Indentation to use for the inserted ctor line (first field's indent)."""
    if info.field_nodes:
        ref = info.field_nodes[0]
    else:
        ref = info.body_node
    pos = ref.start_byte
    line_start = pos
    while line_start > 0 and source[line_start - 1 : line_start] not in (
        b"\n",
        b"\r",
    ):
        line_start -= 1
    indent = b""
    for i in range(line_start, pos):
        ch = source[i : i + 1]
        if ch in (b" ", b"\t"):
            indent += ch
        else:
            break
    return indent or b"    "


def _make_add_ctor_variant(
    source: bytes, info: _StructInfo, counter: int
) -> Optional[Variant]:
    """Insert an empty ``Name() {}`` ctor as the first member of the body."""
    body = info.body_node
    # Insert right after the opening `{` of the body.
    open_brace = None
    for c in body.children:
        if c.type == "{":
            open_brace = c
            break
    if open_brace is None:
        return None

    indent = _struct_indent(source, info)
    insert_text = b"\n" + indent + info.name.encode() + b"() {}"

    ed = SourceEditor(source)
    ed.insert_at(open_brace.end_byte, insert_text)
    try:
        new_source = ed.apply()
    except ValueError:
        return None

    return Variant(
        name=f"pod_ctor_add_{counter}",
        pattern_name="pod_ctor_toggle",
        description=(
            f"Add empty ctor {info.name}() to break POD-ness "
            f"(force typed member copies)"
        ),
        source=new_source,
        tags=frozenset({"pod_toggle", "pod_add_ctor"}),
    )


def _make_remove_ctor_variant(
    source: bytes, info: _StructInfo, counter: int
) -> Optional[Variant]:
    """Delete all user ctor declarations/definitions from the struct body."""
    ed = SourceEditor(source)
    removed = False
    for ctor in info.user_ctor_nodes:
        # Delete the ctor node plus the rest of its line (trailing `;`/newline)
        # so we don't leave a dangling `;`.
        start = ctor.start_byte
        end = ctor.end_byte
        # Extend to include a trailing semicolon if this was a declaration.
        while end < len(source) and source[end : end + 1] in (b" ", b"\t"):
            end += 1
        if end < len(source) and source[end : end + 1] == b";":
            end += 1
        # Swallow the rest of the (now-blank) line.
        line_start = start
        while line_start > 0 and source[line_start - 1 : line_start] not in (
            b"\n",
            b"\r",
        ):
            line_start -= 1
        prefix = source[line_start:start]
        if prefix.strip() == b"":
            # Whole line was just the ctor — remove the leading whitespace and
            # the trailing newline too.
            start = line_start
            if end < len(source) and source[end : end + 1] == b"\n":
                end += 1
        ed.delete_range(start, end)
        removed = True

    if not removed:
        return None

    try:
        new_source = ed.apply()
    except ValueError:
        return None
    if new_source == source:
        return None

    return Variant(
        name=f"pod_ctor_remove_{counter}",
        pattern_name="pod_ctor_toggle",
        description=(
            f"Remove dead user ctor(s) from {info.name} to make it POD "
            f"(force word copies)"
        ),
        source=new_source,
        tags=frozenset({"pod_toggle", "pod_remove_ctor"}),
    )
