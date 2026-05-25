"""Accessor outline — detect inlined accessors and generate noinline wrappers.

When the target binary calls an accessor via `bl` but our compiler inlines it
(direct member load), the codegen diverges. The real fix is moving the accessor
body from the header to the .cpp file, which the permuter can't do automatically.

This pattern detects likely inlined accessor patterns and generates variants that
wrap the call/access in a `__declspec(noinline)` forwarding function, forcing
MWCC to emit `bl` to the wrapper.

Context: Proven on UIListSlot::Draw (96.6->100%) by moving DisabledAlphaScale()
and ParentList() from UIListWidget.h to UIListWidget.cpp.

Detection signals:
    - Replace clusters where target has bl (function call) that base lacks
    - Prologue mismatches (inlined accessor changes register pressure)
    - Clusters (inlined code vs call instruction)

Compiler dialect notes:
    - MWCC (C++98) rejects `auto`/`decltype`. We resolve the wrapper's return
      type from the receiver class's header (similar to member_ref_bind /
      value_address_caching) and emit a concrete-typed non-template wrapper.
      If the return type can't be resolved, the variant is skipped — emitting
      `auto`/`decltype` would just produce a build failure (the historical
      71/71 failure mode).
    - MSVC keeps the original `template <class T> auto ... -> decltype(...)`
      form, which the permuter validated previously.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Iterator, Optional

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import find_calls, walk
from ..editor import SourceEditor
from ..extractor import _cached_parse
from ..types import Diagnosis, FunctionContext, Variant
from .member_ref_bind import (
    _extract_class_name,
    _find_header_for_class as _find_header_in_folder,
    _lookup_member_types,
)

# Max variants to generate
_MAX_VARIANTS = 5

# Cache: (source_file_path_str, class_name) -> header_path (or None).
# Source-file-scoped so transitive-include results don't leak between TUs.
_CLASS_HEADER_CACHE: dict[tuple[str, str], Optional[Path]] = {}

# Cache: source-file-or-header -> tuple of include header paths it (transitively
# up to depth) reaches. Used to constrain the cross-folder header search to
# things this TU actually pulls in.
_INCLUDE_CACHE: dict[Path, tuple[Path, ...]] = {}

_INCLUDE_RE = re.compile(rb'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)

# Standard include roots (mirrors the project's -i flags). We resolve
# `#include "foo/bar.h"` against each in order until the file exists.
_INCLUDE_ROOTS = (
    "src",
    "src/system",
    "src/band3",
    "src/network",
    "src/libs",
    "src/sdk",
)


def _project_src_root(source_file: Path) -> Optional[Path]:
    cur = source_file.resolve()
    for parent in cur.parents:
        if parent.name == "src" and parent.is_dir():
            return parent
    return None


def _resolve_include(name: str, src_root: Path) -> Optional[Path]:
    """Find `#include "foo/bar.h"`'s file against the project include roots."""
    project_root = src_root.parent
    for root in _INCLUDE_ROOTS:
        cand = project_root / root / name
        if cand.exists() and cand.is_file():
            return cand.resolve()
    return None


def _transitive_includes(start: Path, max_depth: int = 2) -> tuple[Path, ...]:
    """Return the headers reachable from `start` within `max_depth` hops.

    Bounded by a global per-file cache. The depth bound keeps the scan from
    exploding through SDK/STL headers; depth=2 covers `cpp -> ownheader.h ->
    related_class.h`, which is enough for the receiver-class lookup.
    """
    if start in _INCLUDE_CACHE:
        return _INCLUDE_CACHE[start]
    src_root = _project_src_root(start)
    if src_root is None:
        _INCLUDE_CACHE[start] = ()
        return ()

    visited: set[Path] = set()
    frontier: list[tuple[Path, int]] = [(start, 0)]
    while frontier:
        path, depth = frontier.pop()
        if path in visited:
            continue
        visited.add(path)
        if depth >= max_depth:
            continue
        try:
            content = path.read_bytes()
        except OSError:
            continue
        for m in _INCLUDE_RE.finditer(content):
            inc_name = m.group(1).decode("utf-8", errors="replace")
            # Skip system / SDK headers (they don't declare our game classes).
            if inc_name.startswith("<"):
                continue
            inc_path = _resolve_include(inc_name, src_root)
            if inc_path is not None and inc_path not in visited:
                frontier.append((inc_path, depth + 1))
    visited.discard(start)
    result = tuple(visited)
    _INCLUDE_CACHE[start] = result
    return result


def _find_header_for_class(source_file: Path, class_name: str) -> Optional[Path]:
    """Find header declaring `class ClassName`, with cross-folder fallback.

    Unlike member_ref_bind's same-folder helper (which is happy to return the
    cpp's stem-matched header regardless of class match — fine when the caller
    only asks about the enclosing class), this one *verifies* that the
    returned header actually declares `class ClassName` or `struct ClassName`.
    Necessary because we lookup arbitrary receiver classes (`RndTransformable`,
    `RndMesh`, etc.) reached only via transitive includes.

    Same-folder fast path first; on miss, walks the `src/` tree once for
    `ClassName.h` and caches the result globally.
    """
    # Match a real class/struct definition (must reach `{` before `;`), not a
    # forward declaration like `class RndTransformable;`. Allows inheritance:
    # `class Foo : public Bar { ... }`.
    name_b = re.escape(class_name.encode())
    declares_re = re.compile(
        rb"\b(?:class|struct)\s+" + name_b + rb"\b[^;{]*\{"
    )

    def declares(path: Path) -> bool:
        try:
            content = path.read_bytes()
        except OSError:
            return False
        return declares_re.search(content) is not None

    # 1. Same-folder fast paths.
    folder = source_file.parent
    candidate = folder / f"{class_name}.h"
    if candidate.exists() and declares(candidate):
        return candidate
    # member_ref_bind's stem-match fallback only makes sense if the file
    # actually declares the requested class.
    stem_header = folder / f"{source_file.stem}.h"
    if stem_header.exists() and declares(stem_header):
        return stem_header
    for h in folder.glob("*.h"):
        if declares(h):
            return h

    # 2. The source file itself — handy for tests / synthetic contexts where
    #    the class is declared inline next to the function under test.
    if source_file.exists() and declares(source_file):
        return source_file

    # 3. Cross-folder fallback (cached). Two probes:
    #    (a) `src/.../ClassName.h` — Hmx classes often live in a same-named
    #        header (RndMesh.h, BandIKEffector.h, etc.).
    #    (b) Transitive includes from the source file — handles classes whose
    #        header name doesn't match (`RndTransformable` lives in `Trans.h`).
    try:
        resolved_str = str(source_file.resolve())
    except (OSError, RuntimeError):
        resolved_str = str(source_file)
    cache_key = (resolved_str, class_name)
    if cache_key in _CLASS_HEADER_CACHE:
        return _CLASS_HEADER_CACHE[cache_key]

    src_root = _project_src_root(source_file)
    if src_root is None:
        _CLASS_HEADER_CACHE[cache_key] = None
        return None

    result: Optional[Path] = None
    for cand in src_root.rglob(f"{class_name}.h"):
        if declares(cand):
            result = cand
            break

    if result is None:
        for inc in _transitive_includes(source_file.resolve()):
            if declares(inc):
                result = inc
                break

    _CLASS_HEADER_CACHE[cache_key] = result
    return result

# Member naming convention (Hmx/Milo): m + uppercase first letter.
_MEMBER_RE = re.compile(r"^m[A-Z]")


# Cache: (header_path, class_name) -> {method_name: return_type_text}
_METHOD_RET_CACHE: dict[tuple[str, str], dict[str, bytes]] = {}


def _lookup_method_return_types(header_path: Path, class_name: str) -> dict[str, bytes]:
    """Parse header, return {method_name: return_type_text} for no-arg methods.

    Only collects nullary (parameterless) method declarations — those are the
    ones the wrapper-outline strategy targets. Includes const-qualified methods.
    Skips constructors/destructors. Skips operator overloads.

    Return types referring to a class nested inside `class_name` are qualified
    automatically (`VertVector` -> `RndMesh::VertVector`), since the wrapper
    we emit at file scope can't resolve the bare nested name.

    Returns an empty dict if the class or header can't be parsed.
    """
    key = (str(header_path), class_name)
    cached = _METHOD_RET_CACHE.get(key)
    if cached is not None:
        return cached

    out: dict[str, bytes] = {}
    try:
        source = header_path.read_bytes()
    except OSError:
        _METHOD_RET_CACHE[key] = out
        return out

    try:
        tree = _cached_parse(source)
    except Exception:
        _METHOD_RET_CACHE[key] = out
        return out

    needle = class_name.encode()
    nested_names: set[str] = set()

    def walk_for_class(node: Node) -> None:
        if node.type in ("class_specifier", "struct_specifier"):
            name_node = node.child_by_field_name("name")
            if (
                name_node is not None
                and source[name_node.start_byte : name_node.end_byte] == needle
            ):
                body = node.child_by_field_name("body")
                if body is not None:
                    _collect_nested_class_names(body, source, nested_names)
                    _collect_no_arg_methods(body, source, out)
                # Don't recurse — class found.
                return
        for ch in node.children:
            walk_for_class(ch)

    walk_for_class(tree.root_node)

    if nested_names:
        out = {
            m: _qualify_nested_type(t, class_name, nested_names)
            for m, t in out.items()
        }

    _METHOD_RET_CACHE[key] = out
    return out


_NESTED_NAMES_CACHE: dict[tuple[str, str], set[str]] = {}


def _collect_nested_class_names_for(
    header_path: Path, class_name: str
) -> set[str]:
    """Top-level wrapper: parse header, return nested class/struct names for
    `class_name`. Cached by (path, class_name).
    """
    key = (str(header_path), class_name)
    cached = _NESTED_NAMES_CACHE.get(key)
    if cached is not None:
        return cached
    out: set[str] = set()
    try:
        source = header_path.read_bytes()
    except OSError:
        _NESTED_NAMES_CACHE[key] = out
        return out
    try:
        tree = _cached_parse(source)
    except Exception:
        _NESTED_NAMES_CACHE[key] = out
        return out
    needle = class_name.encode()

    def walk(n: Node) -> bool:
        if n.type in ("class_specifier", "struct_specifier"):
            name_node = n.child_by_field_name("name")
            if (
                name_node is not None
                and source[name_node.start_byte : name_node.end_byte] == needle
            ):
                body = n.child_by_field_name("body")
                if body is not None:
                    _collect_nested_class_names(body, source, out)
                return True
        for ch in n.children:
            if walk(ch):
                return True
        return False

    walk(tree.root_node)
    _NESTED_NAMES_CACHE[key] = out
    return out


def _collect_nested_class_names(
    body: Node, source: bytes, out: set[str]
) -> None:
    """Collect names of classes/structs/enums *defined* inside a class body.

    Includes typedefs (`typedef Foo Bar;`) so a return type using the typedef
    alias also gets qualified. Skips elaborated-type-specifiers (`class
    PanelDir *mDir;`) — those refer to externally-defined classes, not
    nested ones. Class/struct/enum signal: has a body field.
    """
    for child in body.children:
        candidates: list[tuple[Node, str]] = []
        # Direct child types
        if child.type in ("class_specifier", "struct_specifier", "enum_specifier"):
            candidates.append((child, child.type))
        else:
            for c in child.children:
                if c.type in ("class_specifier", "struct_specifier", "enum_specifier"):
                    candidates.append((c, c.type))
        for cls, ctype in candidates:
            # Real definition requires a body — bare `class Foo *p;` doesn't
            # have one.
            if cls.child_by_field_name("body") is None:
                continue
            name_node = cls.child_by_field_name("name")
            if name_node is not None:
                name = source[name_node.start_byte : name_node.end_byte].decode(
                    "utf-8", errors="replace"
                )
                if name:
                    out.add(name)

        # Typedefs: `typedef Foo Bar;` registers `Bar`.
        if child.type == "type_definition":
            for c in child.named_children:
                if c.type == "type_identifier":
                    name = source[c.start_byte : c.end_byte].decode(
                        "utf-8", errors="replace"
                    )
                    if name:
                        out.add(name)


def _qualify_nested_type(
    type_text: bytes, class_name: str, nested_names: set[str]
) -> bytes:
    """If `type_text` references a bare nested-class name, qualify it.

    E.g. `VertVector &` from `class RndMesh { class VertVector {...}; }` ->
    `RndMesh::VertVector &`. Conservatively only rewrites identifiers that
    match the nested set exactly (not partial words).
    """
    if not nested_names:
        return type_text
    text = type_text.decode("utf-8", errors="replace")

    def repl(match: re.Match) -> str:
        word = match.group(0)
        if word in nested_names:
            return f"{class_name}::{word}"
        return word

    new_text = re.sub(r"\b[A-Za-z_]\w*\b", repl, text)
    return new_text.encode("utf-8")


def _collect_no_arg_methods(
    body: Node, source: bytes, out: dict[str, bytes]
) -> None:
    """Walk a class body collecting no-arg method declarations/definitions.

    Records the first occurrence of each name. The recorded type_text is the
    declaration's `type` field plus any leading `const`/`volatile` qualifiers
    that appear as sibling type_qualifier nodes (e.g. `const char *StateName()`
    parses to {qualifier:const, type:char, declarator:*StateName()} — we
    reconstruct `const char *`).
    """
    for child in body.children:
        if child.type not in ("field_declaration", "function_definition"):
            continue
        type_node = child.child_by_field_name("type")
        if type_node is None:
            continue
        decl_node = child.child_by_field_name("declarator")
        if decl_node is None:
            continue

        # Collect leading type_qualifier siblings (const/volatile/etc).
        leading_qualifiers: list[bytes] = []
        for c in child.children:
            if c is type_node:
                break
            if c.type == "type_qualifier":
                leading_qualifiers.append(
                    source[c.start_byte : c.end_byte]
                )

        # Drill down through pointer/reference decorators on the declarator
        # to find the function_declarator. Track decorators to prepend to type.
        # Note: tree-sitter doesn't always attach the inner declarator via the
        # `declarator` field on reference_declarator nodes — fall back to
        # scanning regular children for the next declarator-like node.
        decorators: list[bytes] = []
        cur = decl_node
        while cur is not None and cur.type in (
            "pointer_declarator",
            "reference_declarator",
        ):
            if cur.type == "pointer_declarator":
                decorators.append(b"*")
            else:
                decorators.append(b"&")
            inner = cur.child_by_field_name("declarator")
            if inner is None:
                # Field-name miss — scan for a declarator-shaped child.
                for c in cur.children:
                    if c.type in (
                        "function_declarator",
                        "pointer_declarator",
                        "reference_declarator",
                        "field_identifier",
                        "identifier",
                    ):
                        inner = c
                        break
            if inner is None:
                break
            cur = inner

        if cur is None or cur.type != "function_declarator":
            continue

        # Method name
        name_node = cur.child_by_field_name("declarator")
        if name_node is None or name_node.type != "field_identifier":
            # Skip operator overloads (operator_name), destructors, etc.
            continue
        method_name = source[name_node.start_byte : name_node.end_byte].decode(
            "utf-8", errors="replace"
        )
        # Skip leading-underscore conventions just in case
        if method_name.startswith("operator"):
            continue

        # Must be no-arg (empty parameter_list)
        param_list = None
        for c in cur.children:
            if c.type == "parameter_list":
                param_list = c
                break
        if param_list is None:
            continue
        # named_children: parameter_declaration entries (no `void` marker shows
        # up as a child here for `()` or `(void)`)
        named = [c for c in param_list.named_children if c.type == "parameter_declaration"]
        if named:
            continue

        type_text = source[type_node.start_byte : type_node.end_byte]
        # Prepend leading qualifiers (`const char *` not `char *`).
        if leading_qualifiers:
            type_text = b" ".join(leading_qualifiers) + b" " + type_text
        # Append decorators (e.g. `Transform` + `&` -> `Transform &`)
        if decorators:
            type_text = type_text + b" " + b"".join(reversed(decorators))
        out.setdefault(method_name, type_text)


def _find_no_arg_method_calls(
    body: Node, source: bytes
) -> list[tuple[str, str, Node, bool]]:
    """Find method calls with no arguments: obj.Method() or obj->Method().

    Returns list of (receiver_text, method_name, call_node, is_pointer).
    `receiver_text` is empty when the receiver is `this` (implicit member call
    on the current object) — but in tree-sitter, implicit-this calls don't
    appear as field_expressions, so we won't see them here. They show up as
    plain `call_expression` with an `identifier` function; we ignore those
    because they're harder to detect reliably.
    """
    results: list[tuple[str, str, Node, bool]] = []

    for call_node in find_calls(body):
        func = call_node.child_by_field_name("function")
        args = call_node.child_by_field_name("arguments")
        if func is None or args is None:
            continue

        # Must be a field_expression (obj.Method or obj->Method)
        if func.type != "field_expression":
            continue

        # Check no arguments (only parens, no named children)
        arg_children = [c for c in args.named_children if c.type != "comment"]
        if arg_children:
            continue

        receiver = func.child_by_field_name("argument")
        method = func.child_by_field_name("field")
        if receiver is None or method is None:
            continue

        receiver_text = source[receiver.start_byte : receiver.end_byte].decode(
            "utf-8", errors="replace"
        )
        method_name = source[method.start_byte : method.end_byte].decode(
            "utf-8", errors="replace"
        )

        # Skip STL-style names — these aren't Hmx accessors.
        if method_name in ("begin", "end", "size", "empty", "data", "clear"):
            continue
        # Skip operator overloads
        if method_name.startswith("operator"):
            continue

        # Skip calls on call-result chains: `Foo().Bar()` — too brittle to
        # outline; we can't easily get the receiver type without resolving Foo.
        if receiver.type in ("call_expression",):
            continue

        op_text = source[receiver.end_byte : method.start_byte]
        is_pointer = b"->" in op_text

        results.append((receiver_text, method_name, call_node, is_pointer))

    return results


def _find_repeated_member_accesses(
    body: Node, source: bytes
) -> dict[str, list[Node]]:
    """Find direct member access expressions (obj->mMember) used 2+ times."""
    accesses: dict[str, list[Node]] = {}

    for node in walk(body):
        if node.type != "field_expression":
            continue

        receiver = node.child_by_field_name("argument")
        field = node.child_by_field_name("field")
        if receiver is None or field is None:
            continue

        field_name = source[field.start_byte : field.end_byte].decode(
            "utf-8", errors="replace"
        )
        if not _MEMBER_RE.match(field_name):
            continue

        op_text = source[receiver.end_byte : field.start_byte]
        if b"->" not in op_text and b"." not in op_text:
            continue

        # Skip if the parent is a call expression (it's a method call, not a
        # data-member access).
        parent = node.parent
        if parent is not None and parent.type == "call_expression":
            parent_func = parent.child_by_field_name("function")
            if parent_func is not None and parent_func.id == node.id:
                continue

        # Skip if receiver is a call result chain — we can't resolve its type.
        if receiver.type == "call_expression":
            continue

        key = source[node.start_byte : node.end_byte].decode(
            "utf-8", errors="replace"
        )
        accesses.setdefault(key, []).append(node)

    return {k: v for k, v in accesses.items() if len(v) >= 2}


# ---------------------------------------------------------------------------
# Receiver type resolution
# ---------------------------------------------------------------------------


def _collect_local_var_types(
    body: Node, source: bytes
) -> dict[str, tuple[bytes, bool]]:
    """Walk the function body and collect local-variable types.

    Returns {var_name: (base_type_bytes, is_pointer)}. References are treated
    as values (is_pointer=False) since they're accessed with `.`, not `->`.
    `base_type_bytes` preserves leading `const`/`volatile` qualifiers.

    Only first-occurrence wins. Empty type strings are not stored.
    """
    out: dict[str, tuple[bytes, bool]] = {}

    def visit(n: Node) -> None:
        if n.type == "declaration":
            type_node = n.child_by_field_name("type")
            if type_node is not None:
                type_text = source[type_node.start_byte : type_node.end_byte]
                # Reconstruct leading qualifiers (`const Foo *p;` -> type
                # field is `Foo`, `const` is a sibling type_qualifier).
                quals: list[bytes] = []
                for c in n.children:
                    if c is type_node:
                        break
                    if c.type == "type_qualifier":
                        quals.append(source[c.start_byte : c.end_byte])
                if quals:
                    type_text = b" ".join(quals) + b" " + type_text
                for child in n.named_children:
                    if child.type != "init_declarator":
                        continue
                    decl = child.child_by_field_name("declarator")
                    if decl is None:
                        continue
                    _record(decl, type_text, source, out)
                # Also handle bare declarators like `Foo x;` (no init).
                for child in n.children:
                    if child.type in (
                        "identifier",
                        "pointer_declarator",
                        "reference_declarator",
                        "array_declarator",
                    ):
                        _record(child, type_text, source, out)
        for c in n.children:
            visit(c)

    visit(body)
    return out


def _record(
    decl: Node, type_text: bytes, source: bytes, out: dict[str, tuple[bytes, bool]]
) -> None:
    """Record (name -> (type, is_pointer)) for a declarator chain."""
    is_pointer = False
    cur = decl
    while cur is not None and cur.type in (
        "pointer_declarator",
        "reference_declarator",
        "array_declarator",
    ):
        if cur.type == "pointer_declarator":
            is_pointer = True
        inner = cur.child_by_field_name("declarator")
        if inner is None:
            break
        cur = inner
    if cur is None:
        return
    if cur.type == "identifier":
        name = source[cur.start_byte : cur.end_byte].decode("utf-8", errors="replace")
        out.setdefault(name, (type_text.strip(), is_pointer))


def _extract_params(
    func_node: Node, source: bytes
) -> dict[str, tuple[bytes, bool]]:
    """{param_name: (type_text, is_pointer_or_ref)}.

    References are accessed with `.`, so is_pointer is False; only true
    pointers get is_pointer=True (so we can call the right wrapper form).
    """
    out: dict[str, tuple[bytes, bool]] = {}
    declarator = func_node.child_by_field_name("declarator")
    if declarator is None:
        return out

    # Drill to parameter_list
    param_list = None
    cur = declarator
    while cur is not None and param_list is None:
        for c in cur.children:
            if c.type == "parameter_list":
                param_list = c
                break
        if param_list is not None:
            break
        cur = cur.child_by_field_name("declarator")
    if param_list is None:
        return out

    for param in param_list.named_children:
        if param.type != "parameter_declaration":
            continue
        type_node = param.child_by_field_name("type")
        decl_node = param.child_by_field_name("declarator")
        if type_node is None or decl_node is None:
            continue
        type_text = source[type_node.start_byte : type_node.end_byte]
        # Reconstruct leading qualifiers (const/volatile).
        quals: list[bytes] = []
        for c in param.children:
            if c is type_node:
                break
            if c.type == "type_qualifier":
                quals.append(source[c.start_byte : c.end_byte])
        if quals:
            type_text = b" ".join(quals) + b" " + type_text

        is_pointer = False
        cur = decl_node
        while cur is not None and cur.type in (
            "pointer_declarator",
            "reference_declarator",
        ):
            if cur.type == "pointer_declarator":
                is_pointer = True
            inner = cur.child_by_field_name("declarator")
            if inner is None:
                break
            cur = inner
        if cur is None or cur.type != "identifier":
            continue
        name = source[cur.start_byte : cur.end_byte].decode("utf-8", errors="replace")
        out.setdefault(name, (type_text.strip(), is_pointer))
    return out


_QUAL_NAME_RE = re.compile(rb"^\s*(?:const\s+)?([A-Za-z_]\w*(?:\s*::\s*[A-Za-z_]\w*)*)")

# Smart-pointer wrappers whose first template parameter is the pointee class.
# `ObjPtr<T, Dir>` / `ObjOwnerPtr<T, Dir>` are accessed with `->` (treated as
# pointer in is_pointer terms). We unwrap to the pointee so method lookup
# targets the right class.
_SMART_PTR_WRAPPERS = (
    "ObjPtr",
    "ObjOwnerPtr",
    "ObjOwnerlessPtr",
    "ObjPtrList",
    "ObjPtrVec",
    "auto_ptr",
    "weak_ptr",
    "shared_ptr",
    "unique_ptr",
)


def _normalize_type_to_class_name(type_text: bytes) -> Optional[tuple[str, str]]:
    """Strip qualifiers/decorations to get a class name we can use.

    Returns (emit_name, lookup_name):
        emit_name   — fully-qualified, ready to use in wrapper signatures
                      (e.g. `Hmx::Object`, `BandDirector::VenueLoader`).
        lookup_name — bare rightmost segment, used for `class XXX.h` /
                      header-by-class-name scans (which match the
                      unqualified `class Foo {` definition).
    Drops leading `const`, trailing `*`/`&`. For Hmx smart-pointer templates
    like `ObjPtr<RndTransformable, ObjectDir>`, returns the pointee class.
    Returns None for primitive / template-only / unresolvable types.
    """
    s = type_text.strip()
    # Drop trailing decorators
    while s and s[-1:] in (b"*", b"&", b" "):
        s = s[:-1]
    # Handle template wrappers — try to unwrap known smart pointers.
    if b"<" in s:
        head_b, _, tail_b = s.partition(b"<")
        head_str = head_b.strip().decode("utf-8", errors="replace")
        bare_head = head_str.rsplit("::", 1)[-1]
        if bare_head in _SMART_PTR_WRAPPERS:
            depth = 0
            first_arg = bytearray()
            for ch in tail_b:
                c = bytes((ch,))
                if c == b"<":
                    depth += 1
                    first_arg += c
                elif c == b">":
                    if depth == 0:
                        break
                    depth -= 1
                    first_arg += c
                elif c == b"," and depth == 0:
                    break
                else:
                    first_arg += c
            inner = bytes(first_arg).strip()
            if inner:
                return _normalize_type_to_class_name(inner)
            return None
        # Plain templated class — we can't emit a valid wrapper signature
        # without preserving the template arguments verbatim, and resolving
        # methods on a templated class is brittle. Skip.
        return None
    m = _QUAL_NAME_RE.match(s)
    if not m:
        return None
    qname = m.group(1).decode("utf-8", errors="replace")
    # Strip whitespace around `::` for the emit form.
    qname = re.sub(r"\s*::\s*", "::", qname)
    bare = qname.rsplit("::", 1)[-1]
    primitives = {
        "void", "bool", "char", "short", "int", "long", "float", "double",
        "signed", "unsigned", "auto", "size_t", "ssize_t", "u8", "u16", "u32",
        "u64", "s8", "s16", "s32", "s64", "uchar", "ushort", "uint", "ulong",
    }
    if bare in primitives:
        return None
    if not bare[:1].isupper():
        return None
    return (qname, bare)


def _resolve_receiver_type(
    receiver_text: str,
    is_pointer: bool,
    ctx: FunctionContext,
    member_types: dict[str, bytes],
    local_types: dict[str, tuple[bytes, bool]],
    param_types: dict[str, tuple[bytes, bool]],
    enclosing_class: Optional[str] = None,
    enclosing_nested_names: Optional[set[str]] = None,
) -> Optional[tuple[tuple[str, str], bool]]:
    """Best-effort resolve the receiver's (class_name, is_const).

    Returns (class_name, is_const) or None when we can't be confident.
    `is_const` is True when the receiver's declared type carries a leading
    `const` qualifier — needed so the emitted wrapper takes `const T*` for
    const-qualified accessor calls. We're conservative: ambiguous receivers
    (sub-expressions, chained calls) get None so the variant is skipped.
    """
    text = receiver_text.strip()
    if not text:
        return None
    # Implicit this is handled by the caller (receiver_text == "this").
    if text == "this":
        cls = _extract_class_name(ctx.func_node, ctx.file_source)
        if cls is None:
            return None
        # `this` is const only inside `const`-qualified methods. Detecting
        # that requires post-declarator scan; out of scope — assume non-const.
        return ((cls, cls), False)
    # Disqualify obviously complex receivers we can't analyze
    if any(c in text for c in "()[]<>+/*-%!|&^~?,") and not text.startswith("*"):
        return None
    # Strip leading `*` deref
    while text.startswith("*"):
        text = text[1:].strip()
    if not text or not re.match(r"^[A-Za-z_]\w*$", text):
        return None

    def resolve(type_text: bytes) -> Optional[tuple[str, bool]]:
        norm = _normalize_type_to_class_name(type_text)
        if norm is None:
            return None
        emit_name, lookup_name = norm
        is_const = type_text.strip().startswith(b"const ")
        # If the (bare) type is a nested class of the enclosing class — the
        # field is declared inside the class body without a qualifier — emit
        # `BandDirector::VenueLoader` instead of `VenueLoader`. The lookup
        # name still uses the bare form so header scans match `class XXX {`.
        if (
            enclosing_class is not None
            and enclosing_nested_names is not None
            and emit_name in enclosing_nested_names
        ):
            emit_name = f"{enclosing_class}::{emit_name}"
        return ((emit_name, lookup_name), is_const)

    # Local variable?
    if text in local_types:
        type_text, _ = local_types[text]
        return resolve(type_text)

    # Parameter?
    if text in param_types:
        type_text, _ = param_types[text]
        return resolve(type_text)

    # Member field of the enclosing class?
    if text in member_types:
        return resolve(member_types[text])

    return None


def _resolve_method_return_type(
    receiver_class: str,
    method_name: str,
    source_file: Path,
    source_bytes: Optional[bytes] = None,
) -> Optional[bytes]:
    """Look up `receiver_class::method_name()`'s return type.

    First tries the receiver's own header (single-file scan, cross-folder
    fallback). If that misses and `source_bytes` is provided, also tries the
    in-memory source itself — covers tests / synthetic contexts where the
    class is declared inline next to the function under test.
    """
    header = _find_header_for_class(source_file, receiver_class)
    if header is not None:
        methods = _lookup_method_return_types(header, receiver_class)
        rt = methods.get(method_name)
        if rt is not None:
            return rt

    # Fall back to scanning the in-memory source.
    if source_bytes is not None:
        methods = _lookup_method_return_types_in_source(
            source_bytes, receiver_class
        )
        return methods.get(method_name)

    return None


def _lookup_method_return_types_in_source(
    source: bytes, class_name: str
) -> dict[str, bytes]:
    """Same as _lookup_method_return_types but reads from a bytes buffer."""
    out: dict[str, bytes] = {}
    try:
        tree = _cached_parse(source)
    except Exception:
        return out
    needle = class_name.encode()
    nested_names: set[str] = set()

    def walk(node: Node) -> bool:
        if node.type in ("class_specifier", "struct_specifier"):
            name_node = node.child_by_field_name("name")
            if (
                name_node is not None
                and source[name_node.start_byte : name_node.end_byte] == needle
            ):
                body = node.child_by_field_name("body")
                if body is not None:
                    _collect_nested_class_names(body, source, nested_names)
                    _collect_no_arg_methods(body, source, out)
                return True
        for ch in node.children:
            if walk(ch):
                return True
        return False

    walk(tree.root_node)
    if nested_names:
        out = {
            m: _qualify_nested_type(t, class_name, nested_names)
            for m, t in out.items()
        }
    return out


def _looks_like_macro_misparse(ctx: FunctionContext) -> bool:
    """Detect functions whose parse was confused by a preceding macro.

    Tree-sitter mis-parses things like
        END_LOADS
        void Foo::Bar() { ... }
    as a single function_definition with `END_LOADS` as the return type. The
    function's surrounding context (still inside a BEGIN_LOADS macro block,
    in the source layout) is unsafe for our wrapper insertion. Skip these.
    """
    type_node = ctx.func_node.child_by_field_name("type")
    if type_node is None:
        return False
    text = ctx.file_source[type_node.start_byte : type_node.end_byte]
    text_s = text.strip()
    # ALL_UPPER with underscores — strong macro signal. Real C++ types are
    # mixed-case (`Transform`, `Hmx::Object`, `int`).
    if text_s and text_s.replace(b"_", b"").isupper() and b"_" in text_s:
        return True
    # Known macro terminators that occasionally end up in the type slot.
    if text_s in (
        b"END_LOADS", b"END_HANDLERS", b"END_COPYS", b"END_PROPSYNCS",
        b"END_CUSTOM_PROPSYNC", b"END_OBJ_HANDLERS",
    ):
        return True
    return False


def _safe_insert_before_function(ctx: FunctionContext) -> int:
    """Pick a safe byte offset to insert wrapper code before the function.

    For functions parsed normally, `func_node.start_byte` is fine. For
    functions whose preceding source contains a macro that tree-sitter
    mis-parses as part of the return type (e.g. `END_LOADS` immediately
    before `void Class::Method() {...}`), the start_byte lands inside the
    macro and our wrapper would be inserted between `bs >> mFoo;` and
    `END_LOADS`, producing a syntax error.

    Heuristic: scan back from func_node.start_byte for the previous `}`
    that's at the start of a line (a function/macro terminator). Insert
    immediately after it. If none is found in a reasonable window, fall
    back to func_node.start_byte.
    """
    source = ctx.file_source
    start = ctx.func_node.start_byte
    # Look back up to 2 KB for a clear function-level boundary.
    lo = max(0, start - 2048)
    window = source[lo:start]

    # Prefer the last `}` that ends a line (top-level scope close), or the
    # last `END_LOADS`/`END_HANDLERS`/etc macro terminator.
    macros = (b"END_LOADS", b"END_HANDLERS", b"END_COPYS", b"END_PROPSYNCS",
              b"END_CUSTOM_PROPSYNC")
    best = -1
    for tok in (b"}",) + macros:
        idx = window.rfind(tok)
        if idx < 0:
            continue
        # Require it be followed by a newline (end-of-line / standalone token).
        post = window[idx:idx + 20]
        if b"\n" not in post:
            continue
        nl = window.index(b"\n", idx)
        cand = lo + nl + 1
        if cand > best:
            best = cand
    if best >= 0:
        return best
    return start


# ---------------------------------------------------------------------------
# Pattern entry point
# ---------------------------------------------------------------------------


class AccessorOutlinePattern(Pattern):
    name = "accessor_outline"
    # Re-enabled (was opt_in due to 71/71 variants failing on mwcc).
    # The historical failure was wrappers emitting `auto` / `decltype`, which
    # MWCC (C++98) rejects as illegal storage class. We now resolve the
    # concrete return / field type from the receiver class's header (same
    # approach as member_ref_bind / value_address_caching) and emit a
    # concrete-typed non-template wrapper on mwcc. If type resolution fails,
    # the variant is skipped rather than emitted.
    safety_tier = "normal"
    structural_domain = "cross_unit"
    follow_ups = ("declaration_reorder", "value_address_caching")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Replace clusters where target has bl (function call) pattern
        for d in diagnosis.diff_ops:
            if d.target_opcode == "bl" and d.base_opcode != "bl":
                return True

        # Clusters suggest structural differences (inlined vs outlined)
        if diagnosis.clusters:
            return True

        # Prologue mismatch — inlined code changes callee-saved pressure
        if diagnosis.has_prologue_mismatch:
            return True

        # Real structural replaces
        if diagnosis.replace_real > 0:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        score = 0.4
        # Strong signal: target has bl that base doesn't (accessor outlined in target)
        for d in diagnosis.diff_ops:
            if d.target_opcode == "bl" and d.base_opcode != "bl":
                score = 0.9
                break
        # Prologue mismatch boosts
        if diagnosis.has_prologue_mismatch:
            score = min(1.0, score + 0.2)
        return score

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        # Skip functions whose tree-sitter parse was confused by a preceding
        # macro (e.g. END_LOADS). Inserting wrappers in these is unsafe.
        if _looks_like_macro_misparse(ctx):
            return

        counter = 0

        # Strategy 1: outline no-arg method calls.
        for variant in _noinline_wrapper_variants(ctx, counter):
            yield variant
            counter += 1
            if counter >= _MAX_VARIANTS:
                return

        # Strategy 2: outline direct member accesses used 2+ times.
        for variant in _volatile_indirection_variants(ctx, counter):
            yield variant
            counter += 1
            if counter >= _MAX_VARIANTS:
                return


# ---------------------------------------------------------------------------
# Strategy 1: noinline wrapper for accessor-style calls
# ---------------------------------------------------------------------------


def _noinline_wrapper_variants(
    ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate noinline wrapper variants for no-argument method calls.

    For mwcc, looks up the method's return type and receiver class via the
    headers next to the .cpp; emits a concrete-typed wrapper. Skips when
    resolution fails. For msvc, falls back to the template / decltype form.
    """
    source = ctx.file_source
    body = ctx.body_node
    func_node = ctx.func_node

    calls = _find_no_arg_method_calls(body, source)
    if not calls:
        return

    use_auto = ctx.compiler_dialect == "msvc"

    # Build receiver-type lookup tables once per call to generate().
    param_types = _extract_params(func_node, source)
    local_types = _collect_local_var_types(body, source)

    # Member-field types for the enclosing class (for receivers that are
    # plain `mFoo` identifiers).
    enclosing_class = _extract_class_name(func_node, source)
    member_types: dict[str, bytes] = {}
    enclosing_nested_names: set[str] = set()
    if enclosing_class is not None:
        own_header = _find_header_for_class(ctx.file_path, enclosing_class)
        if own_header is not None:
            member_types = _lookup_member_types(own_header, enclosing_class)
            enclosing_nested_names = _collect_nested_class_names_for(
                own_header, enclosing_class
            )

    seen_methods: set[str] = set()

    for receiver_text, method_name, call_node, is_pointer in calls:
        if counter >= _MAX_VARIANTS:
            return
        if method_name in seen_methods:
            continue
        seen_methods.add(method_name)

        # Pre-compute matching call sites; if any share the same method but a
        # different receiver class, skip — we'd need overloaded wrappers.
        matching_calls = [c for c in calls if c[1] == method_name]

        # Resolve receiver (class_name, is_const) for each call. Must collapse
        # to a single concrete pair; otherwise skip.
        # Each entry is ((emit_name, lookup_name), is_const).
        receiver_pairs: set[tuple[tuple[str, str], bool]] = set()
        had_unknown = False
        for r_text, _, _, _ in matching_calls:
            rc = _resolve_receiver_type(
                r_text, is_pointer, ctx, member_types, local_types, param_types,
                enclosing_class=enclosing_class,
                enclosing_nested_names=enclosing_nested_names,
            )
            if rc is None:
                had_unknown = True
                continue
            receiver_pairs.add(rc)

        if not use_auto:
            if had_unknown or len(receiver_pairs) != 1:
                continue
            ((receiver_emit, receiver_lookup), receiver_is_const) = next(iter(receiver_pairs))
            return_type = _resolve_method_return_type(
                receiver_lookup,
                method_name,
                ctx.file_path,
                source_bytes=source,
            )
            if return_type is None:
                continue
            return_type_str = return_type.decode("utf-8", errors="replace").strip()
            wrapper_const_qual = "const " if receiver_is_const else ""
            receiver_class = receiver_emit

        wrapper_name = f"_outline_{method_name}"

        if use_auto:
            # msvc — preserve the original template form.
            wrapper = (
                f"template <class _T>\n"
                f"__declspec(noinline) auto {wrapper_name}(_T* _obj)"
                f" -> decltype(_obj->{method_name}()) {{\n"
                f"    return _obj->{method_name}();\n"
                f"}}\n\n"
            ).encode("utf-8")
        else:
            # mwcc — concrete-typed wrapper. Drop `static` (MWCC errors out
            # with "illegal use of function qualifier(s)" for some receiver
            # types when `static __declspec(noinline)` is combined); rely on
            # the unique `_outline_<Method>` mangle for collision safety.
            # Preserve the receiver's const qualifier so const-callsites
            # compile.
            wrapper = (
                f"__declspec(noinline) {return_type_str} {wrapper_name}"
                f"({wrapper_const_qual}{receiver_class}* _obj) {{\n"
                f"    return _obj->{method_name}();\n"
                f"}}\n\n"
            ).encode("utf-8")

        ed = SourceEditor(source)
        insert_pos = _safe_insert_before_function(ctx)
        ed.insert_at(insert_pos, wrapper)

        # Replace each call site.
        replaced_any = False
        for recv, _, cn, ptr in sorted(
            matching_calls, key=lambda x: x[2].start_byte, reverse=True
        ):
            func_expr = cn.child_by_field_name("function")
            if func_expr is None:
                continue
            func_text = source[func_expr.start_byte : func_expr.end_byte]
            if b"->" in func_text:
                replacement = f"{wrapper_name}({recv})".encode("utf-8")
            else:
                replacement = f"{wrapper_name}(&{recv})".encode("utf-8")
            ed.replace_node(cn, replacement)
            replaced_any = True

        if not replaced_any:
            continue

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        yield Variant(
            name=f"accessor_outline_{counter}",
            pattern_name="accessor_outline",
            description=(
                f"Outline accessor {method_name}() via noinline wrapper "
                f"({len(matching_calls)} call sites)"
            ),
            source=new_source,
            tags=frozenset({"outlined_accessor"}),
        )
        counter += 1


# ---------------------------------------------------------------------------
# Strategy 2: noinline getter for repeated direct member accesses
# ---------------------------------------------------------------------------


def _volatile_indirection_variants(
    ctx: FunctionContext, counter: int
) -> Iterator[Variant]:
    """Generate noinline getter variants for repeated direct member accesses.

    For mwcc, looks up the field's concrete type via the receiver's header.
    For msvc, falls back to the template / decltype form.

    NOTE: This strategy is msvc-only. On mwcc, two issues make it too noisy:
    1. Field types frequently use macros (`VECTOR_SIZE_LARGE`), nested-class
       names, or anonymous-namespace types that we'd need a full preprocessor
       to expand. Type-resolution failure rate is much higher than for
       method return types.
    2. Even when it compiles, replacing a direct member load with an
       artificial bl-to-thunk rarely matches the target — the target's
       inlined accessor (the thing we're trying to mimic) is an explicit
       method call, not a direct member load. Strategy 1 covers the
       productive case; this one mostly produces noise.
    """
    source = ctx.file_source
    body = ctx.body_node
    func_node = ctx.func_node

    repeated = _find_repeated_member_accesses(body, source)
    if not repeated:
        return

    use_auto = ctx.compiler_dialect == "msvc"
    if not use_auto:
        return

    param_types = _extract_params(func_node, source)
    local_types = _collect_local_var_types(body, source)
    enclosing_class = _extract_class_name(func_node, source)
    own_member_types: dict[str, bytes] = {}
    if enclosing_class is not None:
        own_header = _find_header_for_class(ctx.file_path, enclosing_class)
        if own_header is not None:
            own_member_types = _lookup_member_types(own_header, enclosing_class)

    for access_key, nodes in repeated.items():
        if counter >= _MAX_VARIANTS:
            return

        first = nodes[0]
        receiver = first.child_by_field_name("argument")
        field = first.child_by_field_name("field")
        if receiver is None or field is None:
            continue

        field_name = source[field.start_byte : field.end_byte].decode(
            "utf-8", errors="replace"
        )
        receiver_text = source[receiver.start_byte : receiver.end_byte].decode(
            "utf-8", errors="replace"
        )
        op_text = source[receiver.end_byte : field.start_byte]
        is_pointer = b"->" in op_text

        wrapper_func_name = f"_get_{field_name}"

        if use_auto:
            if is_pointer:
                wrapper = (
                    f"template <class _T>\n"
                    f"__declspec(noinline) auto {wrapper_func_name}(_T* _obj)"
                    f" -> decltype(_obj->{field_name}) {{\n"
                    f"    return _obj->{field_name};\n"
                    f"}}\n\n"
                ).encode("utf-8")
            else:
                wrapper = (
                    f"template <class _T>\n"
                    f"__declspec(noinline) auto {wrapper_func_name}(_T& _obj)"
                    f" -> decltype(_obj.{field_name}) {{\n"
                    f"    return _obj.{field_name};\n"
                    f"}}\n\n"
                ).encode("utf-8")
        else:
            # mwcc — resolve receiver class, then field type on it.
            receiver_class = _resolve_receiver_type(
                receiver_text,
                is_pointer,
                ctx,
                own_member_types,
                local_types,
                param_types,
            )
            if receiver_class is None:
                continue
            header = _find_header_for_class(ctx.file_path, receiver_class)
            if header is None:
                continue
            recv_members = _lookup_member_types(header, receiver_class)
            field_type = recv_members.get(field_name)
            if field_type is None:
                continue
            field_type_str = field_type.decode("utf-8", errors="replace").strip()
            # Return by reference so the getter is equivalent to direct access.
            if is_pointer:
                wrapper = (
                    f"static __declspec(noinline) {field_type_str}& "
                    f"{wrapper_func_name}({receiver_class}* _obj) {{\n"
                    f"    return _obj->{field_name};\n"
                    f"}}\n\n"
                ).encode("utf-8")
            else:
                wrapper = (
                    f"static __declspec(noinline) {field_type_str}& "
                    f"{wrapper_func_name}({receiver_class}& _obj) {{\n"
                    f"    return _obj.{field_name};\n"
                    f"}}\n\n"
                ).encode("utf-8")

        ed = SourceEditor(source)
        insert_pos = _safe_insert_before_function(ctx)
        ed.insert_at(insert_pos, wrapper)

        for node in sorted(nodes, key=lambda n: n.start_byte, reverse=True):
            recv_node = node.child_by_field_name("argument")
            if recv_node is None:
                continue
            recv_text = source[recv_node.start_byte : recv_node.end_byte].decode(
                "utf-8", errors="replace"
            )
            replacement = f"{wrapper_func_name}({recv_text})".encode("utf-8")
            ed.replace_node(node, replacement)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        yield Variant(
            name=f"accessor_outline_{counter}",
            pattern_name="accessor_outline",
            description=(
                f"Outline member access {access_key} via noinline getter "
                f"({len(nodes)} uses)"
            ),
            source=new_source,
            tags=frozenset({"outlined_accessor"}),
        )
        counter += 1
