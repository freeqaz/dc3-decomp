"""Pointer-iterator unroll enabler.

MWCC's loop-unroll pass requires the loop's induction pointer to be DEAD at
loop exit. When a loop mutates a START pointer that the function still needs
AFTER the loop (an assert, a follow-up call, a member store), the pointer stays
live and the unroll is suppressed — so the target's classic 8-way
``lwz/lwz/lwz/lwz... stw/stw/stw/stw`` block degenerates into a 1-at-a-time
loop in our build.

The fix introduces a fresh disposable local iterator that mutates while the
original START pointer stays put. The unroll pass can then DCE the fresh local
at exit and unroll the body.

Example::

    // BEFORE (no unroll):
    for (int i = vertEnd - vertIt; i > 0; --i, ++vertIt) {
        vertIt->pos.Set(...);
        vertIt->uv.Set(...);
    }
    // ... later references vertIt (the original start) ...

    // AFTER (8-way unroll, the win):
    RndMesh::Vert *v = vertIt;
    for (int i = vertEnd - vertIt; i > 0; --i, ++v) {
        v->pos.Set(...);
        v->uv.Set(...);
    }

Discovered win: ``RndText::ReplaceLineText`` 88.2 -> 99.99% in one edit
(memory: pointer-iter-for-mwcc-unroll).
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

_MAX_VARIANTS = 4

# Opcodes whose tight runs are the signature of an unrolled copy/fill loop.
# When the TARGET has more of these in a delete cluster than we emit, an unroll
# is likely missing.
_UNROLL_OPS = frozenset({"lwz", "stw", "lfs", "stfs", "lhz", "sth", "lbz", "stb", "lwzu", "stwu"})


class PointerIterUnrollPattern(Pattern):
    """Introduce a fresh disposable iterator so MWCC can unroll the loop."""

    name = "pointer_iter_unroll"
    safety_tier = "moderate"
    structural_domain = "data_flow"
    follow_ups = ("loop_var_hoist",)

    # ------------------------------------------------------------------
    # Diagnosis gating
    # ------------------------------------------------------------------

    def relevant(self, diagnosis: Diagnosis) -> bool:
        """True when the diagnosis suggests a missing unroll.

        Signal A: a delete cluster (target-only code) carrying a run of
        load/store opcodes — the unrolled body the target has and we don't.

        Signal B: individual load/store diff_ops where the target side is one
        of the unroll opcodes (we emit a different/looped shape).

        Gated on diff_ops/clusters presence so it isn't always-True.
        """
        # Signal A: delete-heavy cluster with unroll-op content.
        for cluster in diagnosis.clusters:
            if cluster.deletes <= 0:
                continue
            if any(op in _UNROLL_OPS for op in cluster.target_opcodes):
                return True
            # Even without opcode detail, a sizeable delete cluster is a
            # plausible "target emits more body than us" unroll signature.
            if cluster.deletes >= 4 and cluster.deletes > cluster.inserts:
                return True

        # Signal B: load/store diff ops where target uses an unroll opcode.
        for d in diagnosis.diff_ops:
            if d.target_opcode in _UNROLL_OPS and d.target_opcode != d.base_opcode:
                return True

        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # A delete cluster carrying explicit unroll opcodes is the strongest
        # tell; bump priority when we see it.
        for cluster in diagnosis.clusters:
            if cluster.deletes > 0 and any(
                op in _UNROLL_OPS for op in cluster.target_opcodes
            ):
                return 0.7
        return 0.4

    def context_priority(self, diagnosis: Diagnosis, ctx: FunctionContext) -> float:
        base = self.priority(diagnosis)
        if base == 0.0:
            return 0.0
        # Bump when there's actually a pointer-incrementing for-loop to work on.
        for loop in _find_for_loops(ctx.body_node):
            if _pointer_update_var(loop) is not None:
                return min(1.0, base + 0.2)
        return base

    # ------------------------------------------------------------------
    # Generation
    # ------------------------------------------------------------------

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node
        counter = 0

        for loop in _find_for_loops(body):
            if counter >= _MAX_VARIANTS:
                return

            # A for-update may advance several variables (e.g. `i++, p++`).
            # The loop counter (`i`) is dead at exit and irrelevant; we want
            # the increment whose variable is referenced OUTSIDE the loop,
            # because THAT live pointer is what blocks MWCC's unroll.
            ptr_name = None
            update_node = None
            for cand_name, cand_node in _pointer_update_candidates(loop):
                # The pointer must be referenced OUTSIDE the loop — otherwise
                # it's already dead at loop exit and MWCC could already unroll,
                # so the transform would be a no-op (or even hurt by adding a
                # live local).
                if _used_outside_loop(body, loop, cand_name):
                    ptr_name = cand_name
                    update_node = cand_node
                    break
            if ptr_name is None or update_node is None:
                continue

            # The live-after-loop signal is only meaningful when the pointer is
            # declared in an OUTER scope. If it is (re)declared INSIDE the loop
            # (a `for (T *p = x; ...)` initializer, or a shadowing body decl),
            # the "used outside" hits are a DIFFERENT same-named variable. The
            # loop's own induction var would be left in the init/condition and
            # never advanced (we'd increment only the fresh local) -> infinite
            # loop, and the inserted `T *_it = p;` binds to the wrong `p`. Skip.
            if _declared_inside_loop(loop, ptr_name):
                continue

            # The transform leaves the loop CONDITION referencing the original
            # pointer (only the update + body are retargeted to the fresh local,
            # so the trip count stays pinned). That is correct ONLY for a
            # counter-driven loop (`i > 0`) where the pointer is a passenger.
            # If the pointer IS the loop terminator (`p != end`), not advancing
            # it makes the condition invariant -> infinite loop. Skip those.
            if _used_in_condition(loop, ptr_name):
                continue

            # Resolve the pointer's declared type so we can declare the fresh
            # local. Prefer a real type; fall back to __typeof__ only for msvc
            # (C++11). For mwcc (default) we skip when the type is unknown
            # rather than emit uncompilable code.
            ptr_type = _find_pointer_decl_type(body, ptr_name, source)
            if ptr_type is None:
                if ctx.compiler_dialect == "msvc":
                    decl_prefix = b"__typeof__(" + ptr_name.encode() + b") "
                    fresh_decl_lhs = b""
                else:
                    continue
            else:
                decl_prefix = ptr_type + b" *"
                fresh_decl_lhs = b""

            fresh = _fresh_name(body, ptr_name)
            fresh_b = fresh.encode()

            # Collect all in-loop references to ptr_name that should be rewritten
            # to the fresh iterator: the update clause's mutation of the pointer
            # plus every use inside the loop BODY. The loop initializer /
            # condition (e.g. `vertEnd - vertIt`) keep referencing the original
            # so the iteration count is unchanged.
            rewrite_nodes = _loop_body_and_update_refs(loop, ptr_name, update_node)
            if not rewrite_nodes:
                continue

            ed = SourceEditor(source)

            # Insert `T *fresh = ptr;` on its own line just before the loop.
            line_start = _line_start(source, loop.start_byte)
            indent = _line_indent(source, loop.start_byte)
            if ptr_type is None:
                # msvc __typeof__ path
                decl_line = (
                    indent + decl_prefix + fresh_b + b" = " + ptr_name.encode() + b";\n"
                )
            else:
                decl_line = (
                    indent + decl_prefix + fresh_b + b" = " + ptr_name.encode() + b";\n"
                )
            ed.insert_at(line_start, decl_line)

            for node in rewrite_nodes:
                ed.replace_node(node, fresh_b)

            try:
                new_source = ed.apply()
            except ValueError:
                continue
            if new_source == source:
                continue

            yield Variant(
                name=f"ptriter_{counter}",
                pattern_name=self.name,
                description=(
                    f"Introduce fresh iterator '{fresh}' for '{ptr_name}' "
                    f"so MWCC can unroll the loop"
                ),
                source=new_source,
                func_byte_range=ctx.func_byte_range,
                original_source=source,
                tags=frozenset({"pointer_iter_unroll", "loop_unroll"}),
            )
            counter += 1


# ---------------------------------------------------------------------------
# AST helpers
# ---------------------------------------------------------------------------


def _find_for_loops(root: Node) -> Iterator[Node]:
    for n in walk(root):
        if n.type == "for_statement":
            yield n


def _pointer_update_var(loop: Node) -> str | None:
    """First increment candidate's variable name (for priority hints)."""
    for name, _ in _pointer_update_candidates(loop):
        return name
    return None


def _pointer_update_candidates(loop: Node) -> list[tuple[str, Node]]:
    """All (var_name, mutating_node) increment candidates in the for-update.

    Recognizes ``++p``, ``p++`` (update_expression) and ``p += 1``
    (assignment_expression with ``+=``). The update clause may be a single
    expression or a comma_expression advancing several variables; each
    ``mutating_node`` is the exact node whose identifier should be retargeted
    to the fresh local. The caller picks whichever variable is live after the
    loop (the loop counter is dead at exit and ignored).
    """
    update = loop.child_by_field_name("update")
    if update is None:
        return []

    nodes: list[Node] = []
    if update.type == "comma_expression":
        nodes.extend(walk(update))
    else:
        nodes.append(update)

    out: list[tuple[str, Node]] = []
    seen: set[int] = set()
    for node in nodes:
        var = _increment_target(node)
        if var is not None and node.id not in seen:
            out.append((var, node))
            seen.add(node.id)
    return out


def _increment_target(node: Node) -> str | None:
    """If `node` is a ++/-- or += 1 on a bare identifier, return its name.

    We only treat ``++p`` / ``p++`` and ``p += 1`` as pointer-increments; the
    name is heuristically a pointer (validated later by its declaration).
    """
    if node.type == "update_expression":
        op = _operator_text(node)
        if op not in ("++",):
            # ``--p`` on a pointer is rare for forward iteration; accept only ++
            return None
        arg = node.child_by_field_name("argument")
        if arg is not None and arg.type == "identifier" and arg.text:
            return arg.text.decode("utf-8", errors="replace")
        return None

    if node.type == "assignment_expression":
        op = _operator_text(node)
        if op != "+=":
            return None
        left = node.child_by_field_name("left")
        right = node.child_by_field_name("right")
        if left is None or right is None:
            return None
        if left.type != "identifier" or not left.text:
            return None
        # Only the canonical `p += 1` form (single-step advance).
        if right.type == "number_literal" and right.text == b"1":
            return left.text.decode("utf-8", errors="replace")
        return None

    return None


def _operator_text(node: Node) -> str:
    op = node.child_by_field_name("operator")
    if op is not None and op.text:
        return op.text.decode("utf-8", errors="replace")
    # Fall back to scanning anonymous children for the operator token.
    for child in node.children:
        if not child.is_named and child.text:
            return child.text.decode("utf-8", errors="replace")
    return ""


def _loop_body_and_update_refs(
    loop: Node, ptr_name: str, update_node: Node
) -> list[Node]:
    """All identifier nodes referencing ptr_name to retarget to the fresh var.

    Includes: the increment in the update clause, and every use inside the loop
    body. EXCLUDES the loop initializer and condition so the trip count stays
    pinned to the original start pointer.
    """
    target = ptr_name.encode("utf-8")
    refs: list[Node] = []

    # The mutating identifier in the update clause.
    for n in walk(update_node):
        if n.type == "identifier" and n.text == target:
            refs.append(n)

    # All uses inside the loop body.
    body = loop.child_by_field_name("body")
    if body is not None:
        for n in walk(body):
            if n.type == "identifier" and n.text == target:
                refs.append(n)

    return refs


def _used_outside_loop(func_body: Node, loop: Node, ptr_name: str) -> bool:
    """True if ptr_name is referenced anywhere OUTSIDE `loop`'s subtree.

    Counts uses before AND after the loop (asserts, follow-up calls, member
    stores). The pointer's own declaration counts as an outside reference too,
    but on its own a single decl is not enough — we require at least one
    *non-declaration* use outside the loop.
    """
    target = ptr_name.encode("utf-8")
    loop_start, loop_end = loop.start_byte, loop.end_byte

    for n in walk(func_body):
        if n.type != "identifier" or n.text != target:
            continue
        # Skip references inside the loop subtree.
        if loop_start <= n.start_byte < loop_end:
            continue
        # Skip the declarator of the pointer's own declaration (it's not a use).
        if _is_declarator_identifier(n):
            continue
        return True
    return False


def _used_in_condition(loop: Node, ptr_name: str) -> bool:
    """True if ptr_name appears in the for-loop's condition clause.

    A pointer in the condition is the loop terminator (`p != end`); since the
    transform retargets only the update + body and leaves the condition pinned
    to the original pointer, not advancing it would make the condition
    invariant (infinite loop). Counter-driven loops (`i > 0`, pointer only in
    the update) are unaffected and still fire.
    """
    cond = loop.child_by_field_name("condition")
    if cond is None:
        return False
    target = ptr_name.encode("utf-8")
    for n in walk(cond):
        if n.type == "identifier" and n.text == target:
            return True
    return False


def _declared_inside_loop(loop: Node, ptr_name: str) -> bool:
    """True if ptr_name has a declaration anywhere inside the loop subtree.

    Two unsafe shapes the live-after-loop heuristic otherwise misreads:
      1. the pointer is declared in the for-initializer
         (``for (T *p = x; ...)``) — it is loop-scoped, so any "outside" use is
         a different variable of the same name, and
      2. the pointer is shadowed by an inner declaration in the body.

    In both cases the original ``p`` stays in the loop's init/condition while we
    only advance the fresh local, leaving ``p`` un-incremented (infinite loop),
    and the inserted ``T *_it = p;`` captures the wrong ``p``.
    """
    target = ptr_name.encode("utf-8")
    for n in walk(loop):
        if n.type != "identifier" or n.text != target:
            continue
        if _is_declarator_identifier(n):
            return True
    return False


def _is_declarator_identifier(node: Node) -> bool:
    """True if `node` is the name being declared (not a use)."""
    parent = node.parent
    if parent is None:
        return False
    if parent.type == "pointer_declarator":
        # Walk up: pointer_declarator -> init_declarator/declaration
        gp = parent.parent
        if gp is not None and gp.type in ("init_declarator", "declaration"):
            return True
        # `Foo* p;` — pointer_declarator directly under declaration
        if gp is not None and gp.type == "declaration":
            return True
        if gp is not None and gp.type in ("init_declarator", "declaration"):
            return True
        return True
    if parent.type == "init_declarator":
        decl = parent.child_by_field_name("declarator")
        return decl is not None and decl.id == node.id
    return False


def _find_pointer_decl_type(func_body: Node, ptr_name: str, source: bytes) -> bytes | None:
    """Find the declared base type of pointer `ptr_name`.

    Returns the type bytes WITHOUT the trailing ``*`` (e.g. ``RndMesh::Vert``)
    so the caller can compose ``<type> *fresh = ptr;``. Returns None if the
    declaration can't be located or isn't a single pointer declaration.
    """
    target = ptr_name.encode("utf-8")
    for n in walk(func_body):
        if n.type != "declaration":
            continue
        type_node = n.child_by_field_name("type")
        if type_node is None:
            continue
        # Each declarator in the declaration; we want a pointer_declarator
        # whose innermost identifier matches ptr_name, with exactly one `*`.
        for child in n.named_children:
            decl = child
            init = None
            if decl.type == "init_declarator":
                decl = decl.child_by_field_name("declarator")
            if decl is None:
                continue
            pointer_depth = 0
            cur = decl
            while cur is not None and cur.type == "pointer_declarator":
                pointer_depth += 1
                cur = cur.child_by_field_name("declarator")
            if pointer_depth != 1 or cur is None:
                continue
            if cur.type != "identifier" or cur.text != target:
                continue
            return source[type_node.start_byte:type_node.end_byte]
    return None


def _fresh_name(func_body: Node, ptr_name: str) -> str:
    """Pick a fresh local name not already used in the function body."""
    existing: set[str] = set()
    for n in walk(func_body):
        if n.type == "identifier" and n.text:
            existing.add(n.text.decode("utf-8", errors="replace"))

    base = "_it"
    if base not in existing:
        return base
    i = 0
    while f"_it{i}" in existing:
        i += 1
    return f"_it{i}"


# ---------------------------------------------------------------------------
# Text helpers
# ---------------------------------------------------------------------------


def _line_start(source: bytes, pos: int) -> int:
    while pos > 0 and source[pos - 1:pos] not in (b"\n", b"\r"):
        pos -= 1
    return pos


def _line_indent(source: bytes, pos: int) -> bytes:
    line_start = _line_start(source, pos)
    indent = b""
    for i in range(line_start, pos):
        ch = source[i:i + 1]
        if ch in (b" ", b"\t"):
            indent += ch
        else:
            break
    return indent
