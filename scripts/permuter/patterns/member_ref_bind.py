"""Member reference binding — hoist member/param accesses into local references.

Targets callee-saved register swaps and CR field mismatches caused by the
compiler's register allocation depending on live-range start points.

When a member variable (this->mFoo) or parameter is used multiple times in a
function body, binding it to a local reference shifts its live-range start
point earlier, changing which callee-saved register it gets assigned to.

Transformations:
    mLines.end()          -> auto& _ref0 = mLines; _ref0.end()
    this->mTex && ...     -> auto* _ptr0 = mTex; _ptr0 && ...
    Foo(xfm)              -> const auto& _ref0 = xfm; Foo(_ref0)

This also fixes signed/unsigned comparison mismatches when ObjOwnerPtr<T>
smart pointers are used in && chains — extracting to a raw T* generates
cmplwi (unsigned, cr0) instead of cmpwi (signed, cr6).

Detection signals:
    - Callee-saved GPR swaps (r13-r31)
    - cmpwi/cmplwi replace mismatches (signed vs unsigned null check)
    - CR field differences (cr0 vs cr6)
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent, get_line_start
from ..editor import SourceEditor
from ..extractor import _cached_parse
from ..types import Diagnosis, FunctionContext, Variant

# Member access patterns: this->member or just member (implicit this)
_FIELD_ACCESS_TYPES = {"field_expression", "pointer_expression"}

# Callee-saved GPR range
_CALLEE_SAVED_RE = re.compile(r"r(1[3-9]|2\d|3[01])")


# Cache: (header_path, class_name) -> {member_name: type_text}
_MEMBER_TYPE_CACHE: dict[tuple[str, str], dict[str, bytes]] = {}


def _extract_class_name(func_node: Node, source: bytes) -> str | None:
    """Extract the class name from a function definition like `Foo::Bar()`."""
    decl = func_node.child_by_field_name("declarator")
    while decl is not None:
        # function_declarator wraps the qualified id
        if decl.type == "function_declarator":
            inner = decl.child_by_field_name("declarator")
            if inner is None:
                return None
            text = source[inner.start_byte:inner.end_byte].decode("utf-8", errors="replace")
            if "::" in text:
                # Strip dtor tilde / operator suffixes — keep only ClassName
                qname = text.rsplit("::", 1)[0]
                # Handle nested classes: take innermost qualifier (rightmost ::)
                return qname.rsplit("::", 1)[-1]
            return None
        decl = decl.child_by_field_name("declarator")
    return None


def _find_header_for_class(source_file: Path, class_name: str) -> Path | None:
    """Find header file declaring `class ClassName`. Same-folder lookup only."""
    folder = source_file.parent
    # Common case: ClassName.h next to ClassName.cpp
    candidate = folder / f"{class_name}.h"
    if candidate.exists():
        return candidate
    # File-stem match: source.cpp -> source.h
    stem_header = folder / f"{source_file.stem}.h"
    if stem_header.exists():
        return stem_header
    # Scan same folder for any .h containing `class ClassName`
    needle = f"class {class_name}".encode()
    for h in folder.glob("*.h"):
        try:
            if needle in h.read_bytes():
                return h
        except OSError:
            continue
    return None


def _lookup_member_types(header_path: Path, class_name: str) -> dict[str, bytes]:
    """Parse header, return {member_name: type_text} for class_name.

    Returns empty dict if class not found or parsing fails. The type_text is the
    raw declaration's `type` field, ready to use as `<type> _ref0 = mFoo;`.
    """
    key = (str(header_path), class_name)
    if key in _MEMBER_TYPE_CACHE:
        return _MEMBER_TYPE_CACHE[key]

    out: dict[str, bytes] = {}
    try:
        source = header_path.read_bytes()
    except OSError:
        _MEMBER_TYPE_CACHE[key] = out
        return out

    tree = _cached_parse(source)
    needle = class_name.encode()

    def walk(node: Node) -> None:
        if node.type in ("class_specifier", "struct_specifier"):
            name_node = node.child_by_field_name("name")
            if name_node is not None and source[name_node.start_byte:name_node.end_byte] == needle:
                body = node.child_by_field_name("body")
                if body is not None:
                    _collect_fields(body, source, out)
                return
        for ch in node.children:
            walk(ch)

    walk(tree.root_node)
    _MEMBER_TYPE_CACHE[key] = out
    return out


def _collect_fields(body: Node, source: bytes, out: dict[str, bytes]) -> None:
    """Walk a class body collecting field declarations.

    Skips method declarations (function_declarator) — we only care about data
    members. Continues iterating after a skip so later fields still get picked
    up.
    """
    for child in body.children:
        if child.type != "field_declaration":
            continue
        type_node = child.child_by_field_name("type")
        if type_node is None:
            continue
        # If this field_declaration is actually a method, skip it.
        if any(c.type == "function_declarator" for c in child.children):
            continue
        type_text = source[type_node.start_byte:type_node.end_byte]
        # A field_declaration can declare multiple names: collect each declarator
        for c in child.children:
            if c.type == "field_identifier":
                # type member;
                name = source[c.start_byte:c.end_byte].decode("utf-8", errors="replace")
                out.setdefault(name, type_text)
            elif c.type in ("pointer_declarator", "reference_declarator", "array_declarator"):
                # type *member; / type &member; / type member[N];
                # Walk to inner identifier; prepend the modifier char to type.
                modifier = b""
                inner = c
                while inner.type in ("pointer_declarator", "reference_declarator", "array_declarator"):
                    if inner.type == "pointer_declarator":
                        modifier = b" *" + modifier
                    elif inner.type == "reference_declarator":
                        modifier = b" &" + modifier
                    sub = inner.child_by_field_name("declarator")
                    if sub is None:
                        break
                    inner = sub
                if inner.type == "field_identifier":
                    name = source[inner.start_byte:inner.end_byte].decode("utf-8", errors="replace")
                    out.setdefault(name, type_text + modifier)


class MemberRefBindPattern(Pattern):
    name = "member_ref_bind"
    # Re-enabled (was opt_in due to 212/218 variants failing on mwcc).
    # Now branches on `ctx.compiler_dialect`: mwcc emits a concrete type read
    # from the class's header (`TypeName &_ref0 = mFoo;`); msvc keeps `auto&`.
    # Headers in the same folder as the .cpp are inspected via tree-sitter.

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Callee-saved GPR swaps — this pattern can fix register allocation
        for (r1, r2) in diagnosis.reg_swap_pairs:
            if _CALLEE_SAVED_RE.match(r1) or _CALLEE_SAVED_RE.match(r2):
                return True

        # cmpwi vs cmplwi (signed vs unsigned null check) — ObjOwnerPtr pattern
        for d in diagnosis.diff_ops:
            if (d.target_opcode == "cmplwi" and d.base_opcode == "cmpwi") or \
               (d.target_opcode == "cmpwi" and d.base_opcode == "cmplwi"):
                return True

        # replace with cmpwi/cmplwi differences
        if diagnosis.replace_real > 0:
            return True

        return False

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0

        # Strategy 1: Bind repeated member accesses to local references
        for variant in _bind_member_accesses(ctx, counter):
            yield variant
            counter += 1

        # Strategy 2: Bind parameters to local const references
        for variant in _bind_parameters(ctx, counter):
            yield variant
            counter += 1


def _find_member_uses(body: Node, source: bytes, params: set[str]) -> dict[str, list[Node]]:
    """Find member variable accesses used 2+ times in the function body.

    Detects both:
    - this->mFoo field expressions
    - Plain mFoo identifiers (Hmx/Milo naming convention: members start with m + uppercase)

    Excludes parameter names and local variable declarations.
    """
    uses: dict[str, list[Node]] = {}
    local_decls: set[str] = set()
    _collect_member_accesses(body, source, uses, params, local_decls)
    # Only keep members used 2+ times
    return {k: v for k, v in uses.items() if len(v) >= 2}


# Hmx/Milo member naming: m + uppercase letter (mFoo, mLines, mStream, etc.)
_MEMBER_RE = re.compile(r"^m[A-Z]")


def _collect_member_accesses(
    node: Node, source: bytes, uses: dict[str, list[Node]],
    params: set[str], local_decls: set[str]
) -> None:
    """Recursively collect member variable access nodes.

    Unlike variable extraction, we DO recurse into compound_statements because
    member variables are accessible in all nested scopes. The reference binding
    will be inserted before the first use's containing top-level statement.
    """
    # Track local variable declarations to exclude them
    if node.type == "declaration":
        decl = node.child_by_field_name("declarator")
        if decl is not None:
            name = _decl_name(decl, source)
            if name:
                local_decls.add(name)

    # this->member field expressions
    if node.type == "field_expression":
        arg = node.child_by_field_name("argument")
        if arg is not None:
            arg_text = source[arg.start_byte:arg.end_byte]
            if arg_text in (b"this", b"(*this)") or arg.type == "this":
                member_text = source[node.start_byte:node.end_byte].decode("utf-8", errors="replace")
                uses.setdefault(member_text, []).append(node)

    # Plain identifiers matching member naming convention
    elif node.type == "identifier" and node.text:
        name = node.text.decode("utf-8", errors="replace")
        if _MEMBER_RE.match(name) and name not in params and name not in local_decls:
            # Don't match declaration sites
            parent = node.parent
            if parent is not None and parent.type == "init_declarator":
                decl = parent.child_by_field_name("declarator")
                if decl is not None and decl.id == node.id:
                    # This is a declaration, track it
                    local_decls.add(name)
                    return
            uses.setdefault(name, []).append(node)

    for child in node.children:
        _collect_member_accesses(child, source, uses, params, local_decls)


def _decl_name(decl_node: Node, source: bytes) -> str | None:
    """Extract the identifier name from a declarator node."""
    if decl_node.type == "identifier" and decl_node.text:
        return decl_node.text.decode("utf-8", errors="replace")
    if decl_node.type == "init_declarator":
        inner = decl_node.child_by_field_name("declarator")
        if inner:
            return _decl_name(inner, source)
    if decl_node.type in ("reference_declarator", "pointer_declarator"):
        inner = decl_node.child_by_field_name("declarator")
        if inner:
            return _decl_name(inner, source)
    return None


def _find_identifier_uses(body: Node, name: bytes) -> list[Node]:
    """Find all uses of a specific identifier in the function body."""
    results: list[Node] = []
    _collect_identifier(body, name, results)
    return results


def _collect_identifier(node: Node, name: bytes, results: list[Node]) -> None:
    if node.type == "identifier" and node.text == name:
        # Don't match declaration sites (init_declarator declarator)
        parent = node.parent
        if parent is not None and parent.type == "init_declarator":
            decl = parent.child_by_field_name("declarator")
            if decl is not None and decl.id == node.id:
                return  # This is the declaration, not a use
        results.append(node)

    for child in node.children:
        if child.type != "compound_statement":
            _collect_identifier(child, name, results)


def _get_containing_stmt(node: Node, body: Node) -> Node | None:
    """Walk up from node to find the direct child statement of body."""
    current = node
    while current is not None:
        if current.parent is not None and current.parent.id == body.id:
            return current
        current = current.parent
    return None


def _bind_member_accesses(ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Generate variants that bind member accesses to local references.

    For mwcc (C++98) we look up the member's real type from the class header
    and emit `TypeName &_ref0 = mFoo;` (mwcc rejects `auto`).
    For msvc (C++11+) we use `auto&` / `auto*` — terser and always type-correct.
    """
    source = ctx.file_source
    body = ctx.body_node

    # Collect parameter names to exclude them from member detection
    param_names = {p[0] for p in _extract_params(ctx.func_node, source)}
    member_uses = _find_member_uses(body, source, param_names)

    use_auto = ctx.compiler_dialect == "msvc"

    # For mwcc, look up the enclosing class and its member types.
    member_types: dict[str, bytes] = {}
    if not use_auto:
        class_name = _extract_class_name(ctx.func_node, source)
        if class_name is not None:
            header = _find_header_for_class(ctx.file_path, class_name)
            if header is not None:
                member_types = _lookup_member_types(header, class_name)

    for member_text, nodes in member_uses.items():
        if counter >= 5:  # Limit variants
            break

        # Find the first use and its containing statement
        first_use = nodes[0]
        containing_stmt = _get_containing_stmt(first_use, body)
        if containing_stmt is None:
            continue

        member_bytes = member_text.encode("utf-8")
        var_name = f"_ref{counter}".encode("utf-8")

        indent = get_indent(source, containing_stmt)
        line_start = get_line_start(source, containing_stmt)

        if first_use.type == "field_expression":
            # this->member access — extract the field part
            is_pointer = b"->" in member_bytes
            field = first_use.child_by_field_name("field")
            if field is None:
                continue
            field_name = source[field.start_byte:field.end_byte]
            bare_member = field_name.decode("utf-8", errors="replace")
            if use_auto:
                # msvc / C++11 — terse auto.
                if is_pointer:
                    decl_line = (indent + b"auto* " + var_name + b" = " +
                                 field_name + b";\n")
                else:
                    decl_line = (indent + b"auto& " + var_name + b" = " +
                                 field_name + b";\n")
            else:
                # mwcc — emit concrete type. If we couldn't find it, skip;
                # generating `auto&` would just produce a build failure.
                type_bytes = member_types.get(bare_member)
                if type_bytes is None:
                    continue
                if is_pointer:
                    stripped = type_bytes.rstrip()
                    if stripped.endswith(b"*") or stripped.endswith(b"&"):
                        decl_line = (indent + stripped + b" " + var_name + b" = " +
                                     field_name + b";\n")
                    else:
                        decl_line = (indent + stripped + b" *" + var_name + b" = &" +
                                     field_name + b";\n")
                else:
                    decl_line = (indent + type_bytes + b" &" + var_name + b" = " +
                                 field_name + b";\n")
        elif first_use.type == "identifier":
            # Plain mFoo identifier — bind to typed reference.
            if use_auto:
                decl_line = (indent + b"auto& " + var_name + b" = " +
                             member_bytes + b";\n")
            else:
                type_bytes = member_types.get(member_text)
                if type_bytes is None:
                    continue
                decl_line = (indent + type_bytes + b" &" + var_name + b" = " +
                             member_bytes + b";\n")
        else:
            continue

        # Build editor: insert decl, replace all uses
        ed = SourceEditor(source)
        ed.insert_at(line_start, decl_line)

        # Replace all uses with var_name
        sorted_nodes = sorted(nodes, key=lambda n: n.start_byte, reverse=True)
        for node in sorted_nodes:
            ed.replace_node(node, var_name)

        try:
            new_source = ed.apply()
        except ValueError:
            continue  # Overlapping edits, skip

        is_ref = first_use.type != "field_expression" or b"->" not in member_bytes
        desc = f"Bind {member_text} to local {'reference' if is_ref else 'pointer'} {var_name.decode()}"
        yield Variant(
            name=f"membind_{counter}",
            pattern_name="member_ref_bind",
            description=desc,
            source=new_source,
        )
        counter += 1


def _bind_parameters(ctx: FunctionContext, counter: int) -> Iterator[Variant]:
    """Generate variants that bind function parameters to local const references.

    For reference parameters used in call arguments, creating a local alias
    can shift register allocation order.
    """
    source = ctx.file_source
    body = ctx.body_node
    func = ctx.func_node

    # Find parameter declarations
    params = _extract_params(func, source)
    if not params:
        return

    for param_name, param_type, is_ref in params:
        if counter >= 8:  # Limit total variants
            break

        # Only try reference and const-reference parameters
        if not is_ref:
            continue

        # Skip if parameter type is empty (defensive)
        if not param_type.strip():
            continue

        name_bytes = param_name.encode("utf-8")
        uses = _find_identifier_uses(body, name_bytes)
        if len(uses) < 2:
            continue

        # Find the first statement that uses this parameter
        first_use = uses[0]
        containing_stmt = _get_containing_stmt(first_use, body)
        if containing_stmt is None:
            continue

        var_name = f"_ref{counter}".encode("utf-8")
        indent = get_indent(source, containing_stmt)
        line_start = get_line_start(source, containing_stmt)

        # For mwcc: use the parameter's actual type (no `auto`). For msvc:
        # `const auto&` works for any reference parameter.
        if ctx.compiler_dialect == "msvc":
            decl_line = (indent + b"const auto& " + var_name + b" = " +
                         name_bytes + b";\n")
        else:
            # If source already wrote `const T`, our copy preserves that;
            # otherwise we DON'T add const because the param may be written through.
            type_bytes = param_type.encode("utf-8")
            decl_line = (indent + type_bytes + b" &" + var_name + b" = " +
                         name_bytes + b";\n")

        ed = SourceEditor(source)
        ed.insert_at(line_start, decl_line)

        # Replace all uses after the first statement
        sorted_uses = sorted(uses, key=lambda n: n.start_byte, reverse=True)
        for node in sorted_uses:
            ed.replace_node(node, var_name)

        try:
            new_source = ed.apply()
        except ValueError:
            continue

        desc = f"Bind param {param_name} to const ref {var_name.decode()}"
        yield Variant(
            name=f"parambind_{counter}",
            pattern_name="member_ref_bind",
            description=desc,
            source=new_source,
        )
        counter += 1


def _extract_params(func_node: Node, source: bytes) -> list[tuple[str, str, bool]]:
    """Extract parameter info: [(name, type_text, is_reference), ...]."""
    params: list[tuple[str, str, bool]] = []

    declarator = func_node.child_by_field_name("declarator")
    if declarator is None:
        return params

    # Find parameter_list
    param_list = None
    for child in declarator.children:
        if child.type == "parameter_list":
            param_list = child
            break
    # Also check nested function_declarator
    if param_list is None:
        for child in declarator.children:
            if child.type == "function_declarator":
                for gc in child.children:
                    if gc.type == "parameter_list":
                        param_list = gc
                        break

    if param_list is None:
        return params

    for param in param_list.named_children:
        if param.type != "parameter_declaration":
            continue

        type_node = param.child_by_field_name("type")
        decl_node = param.child_by_field_name("declarator")
        if type_node is None or decl_node is None:
            continue

        type_text = source[type_node.start_byte:type_node.end_byte].decode("utf-8", errors="replace")

        # Check if reference declarator
        is_ref = decl_node.type == "reference_declarator"

        # Extract the actual identifier name
        name_node = decl_node
        while name_node.type in ("reference_declarator", "pointer_declarator"):
            inner = name_node.child_by_field_name("declarator")
            if inner is None:
                inner = name_node.named_children[-1] if name_node.named_children else None
            if inner is None:
                break
            name_node = inner

        if name_node.type == "identifier" and name_node.text:
            params.append((name_node.text.decode("utf-8"), type_text, is_ref))

    return params
