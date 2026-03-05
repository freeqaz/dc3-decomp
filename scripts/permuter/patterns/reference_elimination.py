"""Reference elimination — inline multi-use local refs/ptrs back into expressions.

Win rate: untested (new pattern).

When a local reference or pointer variable is declared, initialized from a
subscript or field expression, and used a small number of times (2-5), try
eliminating it by substituting the initializer expression at all use sites.
This changes register allocation (eliminates an address computation stored
in a callee-saved register), which can fix register swaps and instruction
reordering.

This is the *inverse* of member_ref_bind (which adds refs) and handles
multi-use cases that temp_elimination (single-use only) doesn't cover.

Transformations:
    ObjDirPtr<ObjectDir>& oPtr = subDirs[i];
    if (oPtr != NULL)
        MergeObjectsRecurse(oPtr, toDir, filt, false);
    ->
    if (subDirs[i] != NULL)
        MergeObjectsRecurse(subDirs[i], toDir, filt, false);

Detection signals:
    - Callee-saved register swaps (changing ref eliminates address computation)
    - Clusters (instruction reordering from different load patterns)
    - Load ordering mismatches (replace ops involving lwz/stw)
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Callee-saved register pattern
_CALLEE_SAVED_RE = re.compile(r"[rf](1[3-9]|2\d|3[01])")


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

        # Load ordering mismatches
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("lwz", "stw", "lwzx") or \
               d.base_opcode in ("lwz", "stw", "lwzx"):
                return True

        # Prologue mismatch where target needs fewer vars
        if diagnosis.has_prologue_mismatch and diagnosis.gpr_save_delta < 0:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        base = 1.0 if self.relevant(diagnosis) else 0.0
        # Boost when prologue shows target needs fewer callee-saved regs
        if diagnosis.has_prologue_mismatch and diagnosis.gpr_save_delta < 0:
            base = min(1.0, base + 0.3)
        return base

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

                # Only handle multi-use (2-5 uses). Single-use is temp_elimination's job.
                if len(uses) < 2 or len(uses) > 5:
                    continue

                # Don't inline if init expression has side effects
                if _has_call(stmt, source):
                    continue

                # Build the variant: delete declaration, replace all uses with init_expr
                ed = SourceEditor(source)

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

    Matches patterns like:
        Type& ref = expr;
        Type* ptr = expr;
        ObjDirPtr<ObjectDir>& oPtr = subDirs[i];
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
    init_expr = source[value.start_byte:value.end_byte]

    # Only target subscript expressions (arr[i]) and field expressions (obj.field, obj->field)
    # These are the patterns where eliminating the ref changes address computation
    if value.type not in ("subscript_expression", "field_expression",
                          "identifier", "call_expression"):
        return None

    return var_name, init_expr, stmt.start_byte, stmt.end_byte


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


def _has_call(stmt: Node, source: bytes) -> bool:
    """Check if a statement contains a function call (side effects)."""
    for n in walk(stmt):
        if n.type == "call_expression":
            return True
    return False
