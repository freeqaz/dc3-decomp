"""mwcc_regorder_probe — probe callee-saved register order via this->member hoisting.

MWCC (CodeWarrior) assigns callee-saved registers (r14-r31 / f14-f31) in roughly
the order that long-lived values first appear. Specifically, the *first* time each
callee-saved register is demanded, the live-variable that lives longest in that
region wins the slot.

For member functions, ``this->mFoo`` accesses near the top of the function body
drive which member names bind to which callee-saved registers.  Reordering the
*sequence* of first ``this->member`` reads changes the binding order and can fix
register-swap mismatches without altering semantics.

This pattern:
1. Collects the first N distinct ``this->member`` accesses in the function body
   (limited to the top portion, where the allocator is most sensitive).
2. Generates permutations of that access sequence by hoisting ``Type& m = this->member;``
   reference declarations to the function top in different orders.
3. Caps permutations at 10 to avoid combinatorial blowup.

Detection gate:
- diagnosis must have callee-saved-only GPR or FPR register swap pairs
  (r14-r31 / f14-f31), OR mixed swaps with callee-saved component.
- Function body must contain at least 3 distinct ``this->member`` accesses.
- Not already saturated with reference bindings (less than half of candidate
  members already bound at top level).

Priority: 0.6 for pure callee-saved-only regswap; 0.3 for mixed.
"""

from __future__ import annotations

import itertools
import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent, get_line_start, walk
from ..editor import SourceEditor
from ..extractor import _cached_parse
from ..types import Diagnosis, FunctionContext, Variant

# Callee-saved GPR range: r14-r31 (MWCC saves from r14, unlike MSVC/GCC which
# use r13+). We match both r1X and r2X and r3[01] to be safe.
_CALLEE_SAVED_GPR_RE = re.compile(r"r(1[4-9]|2\d|3[01])")
_CALLEE_SAVED_FPR_RE = re.compile(r"f(1[4-9]|2\d|3[01])")

# Volatile GPR: r3-r12 (argument / scratch registers)
_VOLATILE_GPR_RE = re.compile(r"r([3-9]|1[0-2])")
_VOLATILE_FPR_RE = re.compile(r"f([0-9]|1[0-3])")

# Maximum number of permutation variants to emit per call
_MAX_VARIANTS = 10

# Maximum number of distinct members to collect for permutation
_MAX_MEMBERS = 5


def _is_callee_saved(reg: str) -> bool:
    """True if *reg* is a callee-saved GPR or FPR."""
    return bool(_CALLEE_SAVED_GPR_RE.fullmatch(reg) or
                _CALLEE_SAVED_FPR_RE.fullmatch(reg))


def _is_volatile(reg: str) -> bool:
    """True if *reg* is a volatile (scratch) GPR or FPR."""
    return bool(_VOLATILE_GPR_RE.fullmatch(reg) or
                _VOLATILE_FPR_RE.fullmatch(reg))


class MwccRegorderProbePattern(Pattern):
    """Probe MWCC callee-saved register order by hoisting this->member accesses."""

    name = "mwcc_regorder_probe"
    safety_tier = "conservative"
    structural_domain = "data_flow"
    follow_ups = ("declaration_reorder", "member_ref_bind")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        """Relevant when callee-saved GPR or FPR swaps are present."""
        has_callee_saved = False
        has_volatile_only = True

        for (r0, r1) in diagnosis.reg_swap_pairs:
            cs0 = _is_callee_saved(r0)
            cs1 = _is_callee_saved(r1)
            if cs0 or cs1:
                has_callee_saved = True
            if not (_is_volatile(r0) and _is_volatile(r1)):
                has_volatile_only = False

        # Require at least one callee-saved swap; reject volatile-only
        return has_callee_saved and not has_volatile_only

    def priority(self, diagnosis: Diagnosis) -> float:
        """0.6 for pure callee-saved, 0.3 for mixed."""
        if not self.relevant(diagnosis):
            return 0.0

        has_volatile = any(
            _is_volatile(r0) or _is_volatile(r1)
            for (r0, r1) in diagnosis.reg_swap_pairs
        )
        return 0.3 if has_volatile else 0.6

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        """Emit permutation variants of top-of-function this->member reference order."""
        # Gate: skip when not relevant (caller may not check relevant() first)
        if ctx.diagnosis is None or not self.relevant(ctx.diagnosis):
            return

        # Step 1: collect distinct this->member access nodes in body order
        members = _collect_this_members(ctx)

        # Need at least 3 distinct members to be worth permuting
        if len(members) < 3:
            return

        # Cap to _MAX_MEMBERS to avoid combinatorial blowup
        members = members[:_MAX_MEMBERS]

        # Step 2: check saturation — if most are already bound, skip
        if _is_saturated(ctx, [m for m, _ in members]):
            return

        # Step 3: look up member types from the class header (mwcc requires concrete types)
        class_name = _extract_class_name(ctx.func_node, ctx.file_source)
        member_types: dict[str, bytes] = {}
        if class_name is not None:
            header = _find_header_for_class(ctx.file_path, class_name)
            if header is not None:
                member_types = _lookup_member_types(header, class_name)

        member_names = [m for m, _ in members]

        # Step 4: find insertion point — beginning of function body
        body = ctx.body_node
        first_stmt = None
        for child in body.named_children:
            if child.type not in ("comment",):
                first_stmt = child
                break
        if first_stmt is None:
            return

        indent = get_indent(ctx.file_source, first_stmt)
        insert_pos = get_line_start(ctx.file_source, first_stmt)

        # Step 5: generate permutations (cap at _MAX_VARIANTS)
        counter = 0
        seen: set[tuple[str, ...]] = set()
        identity = tuple(range(len(member_names)))
        seen.add(identity)

        # Generate all pairwise swaps first (most targeted)
        n = len(member_names)
        swap_orders: list[list[int]] = []

        # Single swaps
        for i in range(n):
            for j in range(i + 1, n):
                order = list(range(n))
                order[i], order[j] = order[j], order[i]
                key = tuple(order)
                if key not in seen:
                    seen.add(key)
                    swap_orders.append(order)

        # Full permutations (for small n, enumerate all)
        import random
        if n <= 4:
            for perm in itertools.permutations(range(n)):
                key = tuple(perm)
                if key not in seen:
                    seen.add(key)
                    swap_orders.append(list(perm))
        else:
            # Sample random permutations for larger n
            attempts = 0
            while len(swap_orders) < _MAX_VARIANTS * 2 and attempts < _MAX_VARIANTS * 20:
                indices = list(range(n))
                random.shuffle(indices)
                key = tuple(indices)
                if key not in seen:
                    seen.add(key)
                    swap_orders.append(indices)
                attempts += 1

        # Emit variants for each ordering, stopping at _MAX_VARIANTS
        for order in swap_orders:
            if counter >= _MAX_VARIANTS:
                break

            reordered_names = [member_names[i] for i in order]

            # Build the reference declarations block in the new order
            decl_lines = _build_ref_decls(
                reordered_names, members, member_types, indent,
                ctx.compiler_dialect
            )
            if not decl_lines:
                continue

            # Emit variant: insert the block at the top of the function body
            ed = SourceEditor(ctx.file_source)
            ed.insert_at(insert_pos, decl_lines)
            try:
                new_source = ed.apply()
            except ValueError:
                continue

            if new_source == ctx.file_source:
                continue

            # Describe which members moved relative to original order
            moved = [
                f"{member_names[i]}->{reordered_names[i]}"
                for i in range(n)
                if reordered_names[i] != member_names[i]
            ]
            desc = f"Regorder probe: hoist this->member refs in order [{', '.join(reordered_names[:4])}]"

            yield Variant(
                name=f"regorder_{counter}",
                pattern_name=self.name,
                description=desc,
                source=new_source,
                tags=frozenset({"regorder_probe", "callee_saved_regswap"}),
            )
            counter += 1


# ---------------------------------------------------------------------------
# AST helpers
# ---------------------------------------------------------------------------

def _collect_this_members(ctx: FunctionContext) -> list[tuple[str, Node]]:
    """Collect distinct this->member accesses in order of first appearance.

    Returns list of (member_name, first_node) pairs, ordered by first occurrence
    in the function body.  Caps at _MAX_MEMBERS.
    """
    source = ctx.file_source
    body = ctx.body_node
    seen_names: list[str] = []
    first_nodes: dict[str, Node] = {}

    for node in walk(body):
        if node.type != "field_expression":
            continue
        # Check if the argument (object) is `this`
        arg = node.child_by_field_name("argument")
        if arg is None:
            continue
        arg_text = source[arg.start_byte:arg.end_byte]
        if arg_text not in (b"this", b"(*this)") and arg.type != "this":
            continue

        # Get the field name
        field = node.child_by_field_name("field")
        if field is None:
            continue
        field_name = source[field.start_byte:field.end_byte].decode("utf-8", errors="replace")

        # Only track plain member names (filter out method calls — they have call parents)
        parent = node.parent
        if parent is not None and parent.type == "call_expression":
            continue

        if field_name not in first_nodes:
            first_nodes[field_name] = node
            seen_names.append(field_name)
            if len(seen_names) >= _MAX_MEMBERS:
                break

    return [(name, first_nodes[name]) for name in seen_names]


def _is_saturated(ctx: FunctionContext, member_names: list[str]) -> bool:
    """Return True if more than half the candidate members are already bound.

    A member is 'already bound' if there exists a top-level declaration at the
    start of the function body that assigns this->member to a local variable.
    """
    if not member_names:
        return False
    bound_count = 0
    # Check top-level declarations in the function body
    for stmt in ctx.statements:
        if stmt.type != "declaration":
            continue
        # Check if the declaration's initializer is a this->member access
        decl = stmt.child_by_field_name("declarator")
        if decl is None:
            continue
        # Unwrap init_declarator
        if decl.type == "init_declarator":
            value = decl.child_by_field_name("value")
            if value is None:
                continue
            # Check if value is a field_expression with this->
            if value.type == "field_expression":
                arg = value.child_by_field_name("argument")
                if arg is not None:
                    arg_text = ctx.file_source[arg.start_byte:arg.end_byte]
                    if arg_text in (b"this", b"(*this)") or arg.type == "this":
                        field = value.child_by_field_name("field")
                        if field is not None:
                            fname = ctx.file_source[field.start_byte:field.end_byte].decode("utf-8", errors="replace")
                            if fname in member_names:
                                bound_count += 1

    # Saturated if more than half are already bound
    return bound_count > len(member_names) // 2


def _build_ref_decls(
    ordered_names: list[str],
    members: list[tuple[str, Node]],
    member_types: dict[str, bytes],
    indent: bytes,
    compiler_dialect: str,
) -> bytes:
    """Build a block of reference declarations for the given member order.

    For mwcc (C++98): emit ``TypeName& _mX = this->mX;`` using concrete types.
    For msvc (C++11): emit ``auto& _mX = this->mX;``.

    Returns empty bytes if any required type is missing (mwcc only).
    """
    lines: list[bytes] = []
    use_auto = (compiler_dialect == "msvc")

    for name in ordered_names:
        # Generate a local variable name: prefix m -> _m  (e.g. mFoo -> _mFoo)
        if name.startswith("m"):
            local_name = ("_" + name).encode("utf-8")
        else:
            local_name = (f"_ref_{name}").encode("utf-8")

        name_bytes = name.encode("utf-8")

        if use_auto:
            line = indent + b"auto& " + local_name + b" = this->" + name_bytes + b";\n"
        else:
            type_bytes = member_types.get(name)
            if type_bytes is None:
                # Can't emit concrete type — skip this variant
                return b""
            # Detect if member is a pointer (type ends with *)
            stripped = type_bytes.rstrip()
            if stripped.endswith(b"*"):
                line = indent + stripped + b" " + local_name + b" = this->" + name_bytes + b";\n"
            else:
                line = indent + type_bytes + b"& " + local_name + b" = this->" + name_bytes + b";\n"
        lines.append(line)

    return b"".join(lines)


# ---------------------------------------------------------------------------
# Header / type lookup (mirrors member_ref_bind helpers)
# ---------------------------------------------------------------------------

_MEMBER_TYPE_CACHE: dict[tuple[str, str], dict[str, bytes]] = {}


def _extract_class_name(func_node: Node, source: bytes) -> str | None:
    """Extract the class name from a method definition ``Foo::Bar()``."""
    decl = func_node.child_by_field_name("declarator")
    while decl is not None:
        if decl.type == "function_declarator":
            inner = decl.child_by_field_name("declarator")
            if inner is None:
                return None
            text = source[inner.start_byte:inner.end_byte].decode("utf-8", errors="replace")
            if "::" in text:
                qname = text.rsplit("::", 1)[0]
                return qname.rsplit("::", 1)[-1]
            return None
        decl = decl.child_by_field_name("declarator")
    return None


def _find_header_for_class(source_file, class_name: str):
    """Find header file declaring ``class ClassName`` (same-folder lookup)."""
    from pathlib import Path
    folder = Path(source_file).parent
    candidate = folder / f"{class_name}.h"
    if candidate.exists():
        return candidate
    stem_header = folder / f"{Path(source_file).stem}.h"
    if stem_header.exists():
        return stem_header
    needle = f"class {class_name}".encode()
    for h in folder.glob("*.h"):
        try:
            if needle in h.read_bytes():
                return h
        except OSError:
            continue
    return None


def _lookup_member_types(header_path, class_name: str) -> dict[str, bytes]:
    """Parse header, return {member_name: type_text} for class_name."""
    from pathlib import Path
    key = (str(header_path), class_name)
    if key in _MEMBER_TYPE_CACHE:
        return _MEMBER_TYPE_CACHE[key]

    out: dict[str, bytes] = {}
    try:
        source = Path(header_path).read_bytes()
    except OSError:
        _MEMBER_TYPE_CACHE[key] = out
        return out

    tree = _cached_parse(source)
    needle = class_name.encode()

    def _walk_for_class(node: Node) -> None:
        if node.type in ("class_specifier", "struct_specifier"):
            name_node = node.child_by_field_name("name")
            if name_node is not None and source[name_node.start_byte:name_node.end_byte] == needle:
                body = node.child_by_field_name("body")
                if body is not None:
                    _collect_field_types(body, source, out)
                return
        for ch in node.children:
            _walk_for_class(ch)

    _walk_for_class(tree.root_node)
    _MEMBER_TYPE_CACHE[key] = out
    return out


def _collect_field_types(body: Node, source: bytes, out: dict[str, bytes]) -> None:
    """Walk class body collecting field declarations."""
    for child in body.children:
        if child.type != "field_declaration":
            continue
        type_node = child.child_by_field_name("type")
        if type_node is None:
            continue
        # Skip method declarations
        if any(c.type == "function_declarator" for c in child.children):
            continue
        type_text = source[type_node.start_byte:type_node.end_byte]
        for c in child.children:
            if c.type == "field_identifier":
                name = source[c.start_byte:c.end_byte].decode("utf-8", errors="replace")
                out.setdefault(name, type_text)
            elif c.type in ("pointer_declarator", "reference_declarator", "array_declarator"):
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
