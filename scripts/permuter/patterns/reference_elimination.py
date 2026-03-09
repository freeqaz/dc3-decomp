"""Reference elimination — inline local reference bindings back into member accesses.

This is the INVERSE of member_ref_bind. Where member_ref_bind hoists member
accesses into local references, this pattern removes reference bindings and
inlines the member access expression directly at every use site.

Transformations:
    auto& ref = mMember;         ->  mMember.DoThing();
    ref.DoThing();                   mMember.OtherThing();
    ref.OtherThing();

    auto& _ref0 = mNavItems[i];  ->  mNavItems[i].mFormatArgs->Release();
    _ref0.mFormatArgs->Release();    mNavItems[i].mFormatArgs = a->Array(3);
    _ref0.mFormatArgs = a->Array(3);

    Type& r = this->mFoo;       ->   this->mFoo.Bar();
    r.Bar();

Detection signals:
    - Callee-saved register swaps (eliminating ref changes address computation)
    - Clusters (instruction reordering from different load patterns)
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Callee-saved register pattern (GPR r13-r31, FPR f14-f31)
_CALLEE_SAVED_RE = re.compile(r"[rf](1[3-9]|2\d|3[01])")

# Hmx/Milo member naming: m + uppercase letter (mFoo, mLines, mStream, etc.)
_MEMBER_RE = re.compile(r"^m[A-Z]")

# _refN naming pattern from member_ref_bind
_REF_VAR_RE = re.compile(r"^_ref\d+$")


class ReferenceEliminationPattern(Pattern):
    name = "reference_elimination"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Callee-saved register swaps
        for (r1, r2) in diagnosis.reg_swap_pairs:
            if _CALLEE_SAVED_RE.match(r1) or _CALLEE_SAVED_RE.match(r2):
                return True

        # Clusters suggest instruction reordering
        if diagnosis.clusters:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        return 0.6

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0

        # Walk all compound_statements (including nested for/while/if bodies)
        for compound in _find_compound_statements(ctx.body_node):
            if counter >= 6:
                break

            stmts = list(compound.named_children)
            for i, stmt in enumerate(stmts):
                if counter >= 6:
                    break

                decl_info = _extract_ref_decl(stmt, source)
                if decl_info is None:
                    continue

                var_name, init_expr, decl_start, decl_end = decl_info

                # Count uses in subsequent sibling statements
                uses = []
                for j in range(i + 1, len(stmts)):
                    uses.extend(_find_identifier_uses(stmts[j], var_name))

                if len(uses) < 1:
                    continue

                # Don't inline if any use is in an address-of expression
                if _has_address_of_use(uses):
                    continue

                # Build the variant: delete declaration, replace all uses with init_expr
                ed = SourceEditor(source)

                # Delete the entire declaration line (including leading whitespace and trailing newline)
                del_end = decl_end
                while del_end < len(source) and source[del_end:del_end + 1] in (b"\n", b"\r"):
                    del_end += 1
                del_start = decl_start
                while del_start > 0 and source[del_start - 1:del_start] in (b" ", b"\t"):
                    del_start -= 1

                ed.delete_range(del_start, del_end)

                for use_node in sorted(uses, key=lambda n: n.start_byte, reverse=True):
                    ed.replace_node(use_node, init_expr)

                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                var_str = var_name.decode("utf-8", errors="replace")
                init_str = init_expr.decode("utf-8", errors="replace")
                if len(init_str) > 40:
                    init_str = init_str[:37] + "..."
                yield Variant(
                    name=f"refelim_{counter}",
                    pattern_name=self.name,
                    description=f"Eliminate ref '{var_str}' ({len(uses)} uses) = {init_str}",
                    source=new_source,
                )
                counter += 1


def _find_compound_statements(body: Node) -> list[Node]:
    """Find all compound_statement nodes (including the body itself and nested ones)."""
    results = []
    for n in walk(body):
        if n.type == "compound_statement":
            results.append(n)
    return results


def _extract_ref_decl(
    stmt: Node, source: bytes
) -> tuple[bytes, bytes, int, int] | None:
    """Extract (var_name, init_expr, start_byte, end_byte) from a ref/ptr declaration.

    Only matches declarations where:
    - The declarator is a reference (Type& or auto&)
    - The initializer is a member access (mFoo, mFoo[i], this->mBar, obj->mBar, etc.)
    - The initializer has no side effects (no function calls)

    Matches patterns like:
        auto& ref = mMember;
        auto& _ref0 = mNavItems[index];
        Type& r = this->mFoo;
        ObjDirPtr<ObjectDir>& oPtr = mSubDirs[i];
    """
    if stmt.type != "declaration":
        return None

    # Find the declarator (should have exactly one init_declarator)
    init_decls = [c for c in stmt.named_children if c.type == "init_declarator"]
    if len(init_decls) != 1:
        return None

    init_decl = init_decls[0]
    declarator = init_decl.child_by_field_name("declarator")
    value = init_decl.child_by_field_name("value")

    if declarator is None or value is None:
        return None

    # Must be a reference or pointer declarator
    if declarator.type not in ("reference_declarator", "pointer_declarator"):
        return None

    # Get the actual identifier name
    name_node = declarator
    while name_node.type in ("pointer_declarator", "reference_declarator"):
        inner = name_node.child_by_field_name("declarator")
        if inner is None:
            inner = name_node.named_children[-1] if name_node.named_children else None
        if inner is None:
            break
        name_node = inner

    if name_node.type != "identifier" or name_node.text is None:
        return None

    var_name = name_node.text

    # Don't eliminate if the initializer has side effects (function calls)
    if _has_call_in_node(value):
        return None

    # Validate that the initializer is a member access or parameter-derived expression
    if not _is_eliminable_expr(value, source):
        return None

    init_expr = source[value.start_byte:value.end_byte]

    return var_name, init_expr, stmt.start_byte, stmt.end_byte


def _is_eliminable_expr(node: Node, source: bytes) -> bool:
    """Check if a node is an expression safe to inline (no side effects).

    Accepts member accesses AND parameter-derived subscripts/field accesses.
    The key requirement is: no function calls (side effects), and the expression
    must be a simple access pattern that can be safely duplicated at each use site.
    """
    # Function calls have side effects — skip
    if _has_call_in_node(node):
        return False

    # Accept any identifier, field access, subscript, or pointer expression
    if node.type in ("identifier", "field_expression", "subscript_expression",
                      "pointer_expression"):
        return True

    return False


def _is_member_access(node: Node, source: bytes) -> bool:
    """Check if a node is a member access expression.

    Accepts:
    - Plain identifiers starting with m + uppercase (mFoo, mLines, etc.)
    - Field expressions with this-> (this->mFoo, this->mBar)
    - Subscript expressions whose object is a member access (mFoo[i], this->mBar[j])
    - Field expressions on member accesses (mFoo.bar, mFoo->bar)
    """
    if node.type == "identifier" and node.text:
        name = node.text.decode("utf-8", errors="replace")
        return bool(_MEMBER_RE.match(name))

    if node.type == "field_expression":
        arg = node.child_by_field_name("argument")
        if arg is not None:
            arg_text = source[arg.start_byte:arg.end_byte]
            # this->mFoo or (*this).mFoo
            if arg_text in (b"this", b"(*this)") or arg.type == "this":
                return True
            # member.field or member->field (recursive check on argument)
            return _is_member_access(arg, source)
        return False

    if node.type == "subscript_expression":
        # mFoo[i] — check the array/object part
        arg = node.child_by_field_name("argument")
        if arg is not None:
            return _is_member_access(arg, source)
        # Fallback: check first named child
        if node.named_children:
            return _is_member_access(node.named_children[0], source)
        return False

    return False


def _find_identifier_uses(node: Node, name: bytes) -> list[Node]:
    """Find all uses of an identifier in a subtree."""
    results = []
    for n in walk(node):
        if n.type == "identifier" and n.text == name:
            # Exclude declaration sites
            parent = n.parent
            if parent is not None and parent.type == "init_declarator":
                decl = parent.child_by_field_name("declarator")
                if decl is not None and decl.id == n.id:
                    continue
            results.append(n)
    return results


def _has_call_in_node(node: Node) -> bool:
    """Check if a node subtree contains a function call (side effects)."""
    for n in walk(node):
        if n.type == "call_expression":
            return True
    return False


def _has_address_of_use(uses: list[Node]) -> bool:
    """Check if any use of the variable is in an address-of expression (&ref).

    tree-sitter parses &ref as a pointer_expression (not unary_expression).
    """
    for use_node in uses:
        parent = use_node.parent
        if parent is not None and parent.type == "pointer_expression":
            op = parent.child_by_field_name("operator")
            if op is not None and op.text == b"&":
                return True
    return False
