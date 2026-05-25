"""Deep member reference binding — hoist chained pointer dereferences into local references.

Win rate: untested (new pattern).

When a function accesses members through a chain of pointers (e.g., mLipSync->mData),
binding the sub-object to a local reference eliminates repeated double-indirection.
This changes register allocation and instruction scheduling.

This extends member_ref_bind (which handles this->mFoo) to handle:
    obj->mMember        where obj is a member variable (this->mObj->mMember)
    ptr->mContainer[i]  where ptr is a member variable

Transformations:
    mLipSync->mData[cur]     -> auto& _data = mLipSync->mData; _data[cur]
    mObj->mFrames            -> auto _frames = mObj->mFrames; ... _frames ...
    this->mPtr->mVec.size()  -> auto& _vec = mPtr->mVec; _vec.size()

Detection signals:
    - Callee-saved GPR swaps (r13-r31)
    - Repeated lwz chains through the same pointer (double-indirection)
    - Clusters from instruction reordering due to different load patterns
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, get_line_start, node_text
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Callee-saved GPR range
_CALLEE_SAVED_RE = re.compile(r"r(1[3-9]|2\d|3[01])")

# Milo member naming convention: m + uppercase
_MEMBER_RE = re.compile(rb"^m[A-Z]")


class DeepMemberRefBindPattern(Pattern):
    name = "deep_member_ref_bind"
    # opt_in: 63/63 variants failed compile (100%). Multi-level member binding
    # generates incorrect const/reference qualifiers that don't compile.
    opt_in = True

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Callee-saved GPR swaps
        for (r1, r2) in diagnosis.reg_swap_pairs:
            if _CALLEE_SAVED_RE.match(r1) or _CALLEE_SAVED_RE.match(r2):
                return True

        # Clusters suggest instruction reordering from load patterns
        if diagnosis.clusters:
            return True

        # Replace mismatches (broad trigger)
        if diagnosis.replace_real > 0:
            return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        score = 0.0
        # Higher priority with callee-saved swaps
        for (r1, r2) in diagnosis.reg_swap_pairs:
            if _CALLEE_SAVED_RE.match(r1) or _CALLEE_SAVED_RE.match(r2):
                score = max(score, 0.6)
        # Clusters boost
        if diagnosis.clusters:
            score = max(score, 0.5)
        return score if score > 0 else 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        # Find chained pointer accesses: expr->member where expr is itself
        # a member access (this->ptr->member or ptr->member where ptr is mXxx)
        chains = _find_chained_accesses(body, source)

        for chain_text, access_nodes in chains.items():
            if counter >= 6:
                break

            if len(access_nodes) < 2:
                continue

            # Determine reference type and variable name
            ref_name, decl_line, is_container = _make_binding(
                chain_text, counter, source, access_nodes[0]
            )
            if ref_name is None:
                continue

            # Find the first use's containing statement
            first_use = access_nodes[0]
            containing_stmt = _get_containing_stmt(first_use, body)
            if containing_stmt is None:
                continue

            indent = get_indent(source, containing_stmt)
            line_start = get_line_start(source, containing_stmt)

            ed = SourceEditor(source)
            ed.insert_at(line_start, indent + decl_line + b"\n")

            # Replace all occurrences
            sorted_nodes = sorted(access_nodes, key=lambda n: n.start_byte, reverse=True)
            for node in sorted_nodes:
                ed.replace_node(node, ref_name)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            desc = f"Bind {chain_text.decode('utf-8', errors='replace')} to local ref {ref_name.decode()}"
            yield Variant(
                name=f"deepbind_{counter}",
                pattern_name=self.name,
                description=desc,
                source=new_source,
            )
            counter += 1


def _find_chained_accesses(
    body: Node, source: bytes
) -> dict[bytes, list[Node]]:
    """Find chained member accesses like ptr->member used 2+ times.

    Looks for field_expression nodes where the argument is itself a member
    access (field_expression or identifier matching mXxx pattern), creating
    a chain like mObj->mField or this->mObj->mField.
    """
    uses: dict[bytes, list[Node]] = {}

    for node in walk(body):
        if node.type != "field_expression":
            continue

        arg = node.child_by_field_name("argument")
        field = node.child_by_field_name("field")
        if arg is None or field is None:
            continue

        # We want the argument to be a pointer dereference of a member:
        # - identifier matching mXxx (implicit this->mXxx->field)
        # - field_expression with this-> (explicit this->mXxx->field)
        arg_text = node_text(source, arg)

        is_deep = False

        # Case 1: mPtr->mField (implicit this, mPtr is a member)
        if arg.type == "identifier" and _MEMBER_RE.match(arg_text):
            is_deep = True

        # Case 2: this->mPtr->mField (explicit this->)
        elif arg.type == "field_expression":
            inner_arg = arg.child_by_field_name("argument")
            if inner_arg is not None:
                inner_text = node_text(source, inner_arg)
                if inner_text in (b"this", b"(*this)") or inner_arg.type == "this":
                    is_deep = True

        if not is_deep:
            continue

        # Build the full chain text
        full_text = node_text(source, node)

        # Don't include chains that are part of a larger chain
        parent = node.parent
        if parent is not None and parent.type == "field_expression":
            parent_arg = parent.child_by_field_name("argument")
            if parent_arg is not None and parent_arg.id == node.id:
                continue  # This node is the argument of a longer chain

        # Also skip if the chain is a subscript base (we want the whole expr)
        if parent is not None and parent.type == "subscript_expression":
            sub_arg = parent.child_by_field_name("argument")
            if sub_arg is not None and sub_arg.id == node.id:
                # Use the full subscript as the group key? No — we want to
                # bind the container, not the subscript. So this IS the right
                # level to capture.
                pass

        uses.setdefault(full_text, []).append(node)

    # Only keep chains used 2+ times
    return {k: v for k, v in uses.items() if len(v) >= 2}


def _make_binding(
    chain_text: bytes, counter: int, source: bytes, sample_node: Node
) -> tuple[bytes | None, bytes | None, bool]:
    """Create the binding declaration for a chained access.

    Returns (ref_name, decl_line, is_container) or (None, None, False).
    """
    # Extract the field name for the variable name
    field = sample_node.child_by_field_name("field")
    if field is None:
        return None, None, False

    field_text = node_text(source, field)

    # Generate a readable variable name from the field
    # mData -> _data, mFrames -> _frames
    if _MEMBER_RE.match(field_text):
        # Strip 'm' prefix and lowercase first char
        short = field_text[1:2].lower() + field_text[2:]
        ref_name = b"_" + short
    else:
        ref_name = b"_ref" + str(counter).encode()

    # Determine if this is likely a container type (vector, list, etc.)
    # by checking if any use has subscript or .size()/.begin()/.end() calls
    # For now, always use auto& which works for both
    decl_line = b"auto& " + ref_name + b" = " + chain_text + b";"

    return ref_name, decl_line, False


def _get_containing_stmt(node: Node, body: Node) -> Node | None:
    """Walk up from node to find the direct child statement of body."""
    current = node
    while current is not None:
        if current.parent is not None and current.parent.id == body.id:
            return current
        current = current.parent
    return None
