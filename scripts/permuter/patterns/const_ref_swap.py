"""Const reference swap — toggle between copy initialization and const ref binding.

Copy initialization invokes the copy constructor (generating memcpy or
field-by-field copy), while const reference binding creates an alias (one
addi or mr instruction). The compiler generates very different code.

Transformations:
    SongPos tmp = mSongPos;
    ->
    const SongPos& tmp = mSongPos;

    const SongPos& tmp = mSongPos;
    ->
    SongPos tmp = mSongPos;
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Primitive types that should NOT be swapped (copy vs ref makes no codegen
# difference for scalars).
_PRIMITIVE_TYPES = frozenset({
    "int", "float", "bool", "char", "double", "short", "long",
    "unsigned", "signed", "void", "size_t", "uint", "u8", "u16", "u32",
    "u64", "s8", "s16", "s32", "s64", "int8_t", "int16_t", "int32_t",
    "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "intptr_t", "uintptr_t", "ptrdiff_t", "DWORD", "BOOL", "BYTE",
    "WORD",
    # Windows/WinNT typedefs — uppercase but scalar
    "LONGLONG", "ULONGLONG", "LARGE_INTEGER", "ULARGE_INTEGER",
    "HANDLE", "HRESULT", "HWND", "HMODULE", "HINSTANCE", "HKEY",
    "WPARAM", "LPARAM", "LRESULT", "COLORREF",
    "UINT", "ULONG", "USHORT", "INT", "LONG", "CHAR", "WCHAR",
    "LPCSTR", "LPSTR", "LPCWSTR", "LPWSTR", "LPVOID", "LPCVOID",
    "DWORD64", "XUID", "UINT64", "INT64", "UINT32", "INT32",
})

# Well-known Milo/Hmx struct types (always safe to swap).
_KNOWN_STRUCT_TYPES = frozenset({
    "Vector3", "Vector2", "Hmx::Matrix3", "Hmx::Color", "SongPos",
    "Transform", "Hmx::Quat", "Symbol", "String", "FilePath",
    "DataNode", "Message", "Plane", "Sphere", "Box", "Segment",
    "Triangle", "Rect",
})

# Matches an uppercase-starting identifier (heuristic for class/struct types).
_UPPERCASE_START_RE = re.compile(r"^[A-Z]")


def _is_struct_type(type_text: str) -> bool:
    """Return True if *type_text* looks like a struct/class type."""
    # Strip qualifiers
    bare = type_text.replace("const ", "").replace("volatile ", "").strip()

    # Direct match against known types
    if bare in _KNOWN_STRUCT_TYPES:
        return True

    # Skip primitives and auto
    if bare in _PRIMITIVE_TYPES or bare == "auto":
        return False

    # Skip pointer types (int*, char*, etc.)
    if bare.endswith("*"):
        return False

    # Heuristic: starts with uppercase letter -> likely a class/struct
    if _UPPERCASE_START_RE.match(bare):
        return True

    # Qualified names like Hmx::Foo, std::string
    if "::" in bare and _UPPERCASE_START_RE.match(bare.split("::")[-1]):
        return True

    return False


def _is_address_taken(body: Node, var_name: bytes, decl_node: Node) -> bool:
    """Check if *var_name* has its address taken (&var) after its declaration.

    Taking the address of a const-ref variable has different semantics than
    taking the address of a copy — for a const-ref ``r``, ``&r`` gives the
    address of the referent, while for a copy ``v``, ``&v`` gives the address
    of the local copy. Skip when address-of is detected to avoid semantic
    changes.
    """
    after_decl = False
    for n in walk(body):
        if n.id == decl_node.id:
            after_decl = True
            continue
        if not after_decl:
            continue

        # tree-sitter parses `&expr` inside argument lists as
        # `pointer_expression` (not `unary_expression`); check both forms.
        if n.type in ("unary_expression", "pointer_expression"):
            op = n.child_by_field_name("operator")
            arg = n.child_by_field_name("argument")
            if (
                op is not None and op.text == b"&"
                and arg is not None
                and arg.type == "identifier"
                and arg.text == var_name
            ):
                return True

    return False


def _is_modified_after(body: Node, var_name: bytes, decl_node: Node) -> bool:
    """Check if *var_name* is assigned to after its declaration.

    Looks for assignment_expression, update_expression, or
    unary_expression (++/--) targeting the variable.
    """
    after_decl = False
    for n in walk(body):
        if n.id == decl_node.id:
            after_decl = True
            continue
        if not after_decl:
            continue

        if n.type == "assignment_expression":
            lhs = n.child_by_field_name("left")
            if lhs is not None and lhs.type == "identifier" and lhs.text == var_name:
                return True

        if n.type == "update_expression":
            arg = n.child_by_field_name("argument")
            if arg is not None and arg.type == "identifier" and arg.text == var_name:
                return True

        # Compound assignment (+=, -=, etc.)
        if n.type == "compound_assignment_expr":
            lhs = n.child_by_field_name("left")
            if lhs is not None and lhs.type == "identifier" and lhs.text == var_name:
                return True

    return False


class ConstRefSwapPattern(Pattern):
    name = "const_ref_swap"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        return True

    def priority(self, diagnosis: Diagnosis) -> float:
        return 0.4

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        for decl in _find_swappable_decls(body, source):
            if counter >= 8:
                break

            stmt_node, var_name, type_text, is_const_ref, decl_start, decl_end = decl

            # If converting to const ref, verify the variable is not modified
            # and its address is not taken (different semantics for ref vs copy).
            if not is_const_ref:
                if _is_modified_after(body, var_name, stmt_node):
                    continue
                if _is_address_taken(body, var_name, stmt_node):
                    continue

            ed = SourceEditor(source)

            if is_const_ref:
                # const Type& var = expr; -> Type var = expr;
                new_decl = _make_copy_decl(source, stmt_node, type_text, var_name)
            else:
                # Type var = expr; -> const Type& var = expr;
                new_decl = _make_const_ref_decl(source, stmt_node, type_text, var_name)

            if new_decl is None:
                continue

            ed.replace_range(decl_start, decl_end, new_decl)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            direction = "copy->ref" if not is_const_ref else "ref->copy"
            var_str = var_name.decode("utf-8", errors="replace")
            desc = f"Swap {var_str}: {direction} ({type_text})"
            yield Variant(
                name=f"crefswap_{counter}",
                pattern_name=self.name,
                description=desc,
                source=new_source,
            )
            counter += 1


def _find_swappable_decls(
    body: Node, source: bytes
) -> list[tuple[Node, bytes, str, bool, int, int]]:
    """Find declarations eligible for copy <-> const ref swap.

    Returns list of (stmt_node, var_name, type_text, is_const_ref,
    decl_start_byte, decl_end_byte).
    """
    results = []

    for compound in _find_compound_statements(body):
        for stmt in compound.named_children:
            if stmt.type != "declaration":
                continue

            info = _analyze_declaration(stmt, source)
            if info is not None:
                results.append(info)

    return results


def _find_compound_statements(node: Node) -> list[Node]:
    """Find all compound_statement nodes in the tree."""
    results = []
    for n in walk(node):
        if n.type == "compound_statement":
            results.append(n)
    return results


def _analyze_declaration(
    stmt: Node, source: bytes
) -> tuple[Node, bytes, str, bool, int, int] | None:
    """Analyze a declaration node for copy/const-ref swappability.

    Returns (stmt_node, var_name, type_text, is_const_ref,
    start_byte, end_byte) or None if not eligible.
    """
    # Must have exactly one init_declarator child
    init_decls = [c for c in stmt.named_children if c.type == "init_declarator"]
    if len(init_decls) != 1:
        return None

    init_decl = init_decls[0]
    declarator = init_decl.child_by_field_name("declarator")
    value = init_decl.child_by_field_name("value")

    if declarator is None or value is None:
        return None

    # Get type specifier
    type_node = stmt.child_by_field_name("type")
    if type_node is None:
        return None

    type_text = source[type_node.start_byte:type_node.end_byte].decode(
        "utf-8", errors="replace"
    )

    # Determine if this is currently a const ref or a copy
    is_const_ref = False

    if declarator.type == "reference_declarator":
        # Check for const qualifier on the type
        has_const = _has_const_qualifier(stmt, source)
        if has_const:
            is_const_ref = True
        else:
            # Non-const reference — skip (different semantics)
            return None
    elif declarator.type == "pointer_declarator":
        # Pointer — not applicable
        return None
    elif declarator.type in ("identifier", "init_declarator"):
        # Copy initialization — candidate for swap to const ref
        is_const_ref = False
    else:
        return None

    # Extract the variable name
    var_name = _extract_var_name(declarator, source)
    if var_name is None:
        return None

    # Check type eligibility
    bare_type = type_text.replace("const ", "").replace("volatile ", "").strip()
    if not _is_struct_type(bare_type):
        return None

    # For copy→ref direction, the initializer must produce an lvalue (or at
    # least be something MWCC accepts as a const-ref binding target). Several
    # value types are structurally incompatible with const-ref binding:
    #
    #   - argument_list: constructor-call syntax `T v(a, b)` — the `value`
    #     node is the argument_list `(a, b)`. Rewriting as `const T& v(a, b)`
    #     is not valid C++; it always produces a build failure.
    #   - call_expression: function returning by value — technically binds to
    #     const-ref in MWCC, but we skip to avoid unpredictable codegen.
    #   - initializer_list: aggregate `{...}` — not ref-able.
    #   - new_expression: heap allocation, never an lvalue.
    #
    # We only allow the copy→ref swap for value types that are clearly lvalues
    # or simple rvalue expressions (binary, cast, conditional) that MWCC will
    # accept for const-ref binding.
    if not is_const_ref:
        _BLOCKED_VALUE_TYPES = frozenset({
            "argument_list",    # T v(args) — constructor syntax, never valid as const T& v(args)
            "call_expression",  # f() returns temporary — skip to avoid false wins
            "initializer_list", # {a, b, c} — aggregate, not ref-able
            "new_expression",   # new T(...) — pointer result, not applicable
        })
        if value.type in _BLOCKED_VALUE_TYPES:
            return None

        # Reject null/nullptr initializers: `T v = nullptr` for struct types
        # is unusual (usually a pointer typedef); converting to const-ref changes
        # semantics and is likely a type error.
        if value.type in ("null", "nullptr") or (
            value.type == "identifier" and value.text in (b"nullptr", b"NULL")
        ):
            return None

    return (stmt, var_name, bare_type, is_const_ref, stmt.start_byte, stmt.end_byte)


def _has_const_qualifier(decl_node: Node, source: bytes) -> bool:
    """Check if a declaration has a 'const' type qualifier."""
    for child in decl_node.children:
        if child.type == "type_qualifier":
            text = source[child.start_byte:child.end_byte]
            if text == b"const":
                return True
    return False


def _extract_var_name(declarator: Node, source: bytes) -> bytes | None:
    """Extract the identifier name from a declarator node."""
    node = declarator
    while node.type in ("reference_declarator", "pointer_declarator", "init_declarator"):
        inner = node.child_by_field_name("declarator")
        if inner is None:
            # Try last named child as fallback
            if node.named_children:
                inner = node.named_children[-1]
            else:
                return None
        node = inner

    if node.type == "identifier" and node.text:
        return node.text
    return None


def _make_const_ref_decl(
    source: bytes, stmt: Node, type_text: str, var_name: bytes
) -> bytes | None:
    """Build 'const Type& var = expr;' from a copy-init declaration.

    Reconstructs the declaration preserving the initializer expression.
    The returned bytes replace stmt.start_byte..stmt.end_byte, so leading
    whitespace (which lives before start_byte) is preserved automatically.
    """
    # Find the init_declarator to get the value expression
    init_decls = [c for c in stmt.named_children if c.type == "init_declarator"]
    if len(init_decls) != 1:
        return None

    init_decl = init_decls[0]
    value = init_decl.child_by_field_name("value")
    if value is None:
        return None

    value_text = source[value.start_byte:value.end_byte]

    return (
        b"const "
        + type_text.encode("utf-8")
        + b"& "
        + var_name
        + b" = "
        + value_text
        + b";"
    )


def _make_copy_decl(
    source: bytes, stmt: Node, type_text: str, var_name: bytes
) -> bytes | None:
    """Build 'Type var = expr;' from a const-ref declaration.

    Reconstructs the declaration preserving the initializer expression.
    The returned bytes replace stmt.start_byte..stmt.end_byte, so leading
    whitespace is preserved automatically.
    """
    # Find the init_declarator to get the value expression
    init_decls = [c for c in stmt.named_children if c.type == "init_declarator"]
    if len(init_decls) != 1:
        return None

    init_decl = init_decls[0]
    value = init_decl.child_by_field_name("value")
    if value is None:
        return None

    value_text = source[value.start_byte:value.end_byte]

    return (
        type_text.encode("utf-8")
        + b" "
        + var_name
        + b" = "
        + value_text
        + b";"
    )
