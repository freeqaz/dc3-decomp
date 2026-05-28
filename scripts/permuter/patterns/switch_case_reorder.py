"""Reorder source case clauses to match MWCC's target body-emission order.

MWCC emits switch case bodies in **source order**, not in jump-table-index
order. When the source case ordering disagrees with the order the target
compiler emitted, every downstream stack offset, branch target, and register
pick shifts — producing a huge cascade of insert/delete/diff_arg that looks
unfixable but is purely ordering.

See MEMORY: feedback_switch_case_emission_order.md. The canonical win is
SaveLoadManager::Poll 64.1% -> 88.2% (+24pp) from a pure case reorder.

Phases
======

Phase 1 (always runs): brute-force permutation
    For any switch with 3..N independently-terminated cases (every case
    ends in ``break;`` / ``return ...;`` / ``continue;`` / ``goto``, with
    NO fall-through), yield a small budget of permutations:
        * reverse order
        * a handful of single-pair swaps
        * a handful of random permutations
    Behaviour-neutral by construction.

Phase 2 (when target asm is available): asm-guided reorder
    For a switch we can match against the target's jump table, parse
    ``.obj "@NNNNN" ... .rel <function>, .L_<addr>`` blocks from the
    function's ``.s`` file, then sort the source clauses to match the
    body-emission order implied by the jump table. Yields ONE high-signal
    variant.

Safety rules
------------
* Never reorders a switch where any case falls through (no terminator).
* Never reorders if any case body contains a ``case``-relative ``goto``
  (e.g. ``goto play:`` from another case): such gotos rely on layout.
* The ``default:`` clause is kept in its original textual position in
  Phase 1 permutations (most natural for MWCC); in Phase 2 we sort
  default into its asm-derived position too — the asm tells the truth.
* Fall-through groups (``case 3: case 4: body``) are treated as a single
  unit and moved together.

Detection (relevant)
--------------------
* The function has a switch with >=3 cases (cheap AST check; performed in
  ``generate``, so ``relevant`` only checks the diagnosis signal).
* ``branch_polarity``-class signals — branch-opcode mismatches, large
  clusters, replace_real — are all suggestive. We err on the side of
  letting Phase 1 run cheaply.
"""

from __future__ import annotations

import random
import re
from pathlib import Path
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..control_flow import noncomment_named_children
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Branch opcodes whose mismatches frequently signal control-flow shape drift.
_BRANCH_OPCODES = {
    "beq", "bne", "ble", "bgt", "bge", "blt",
    "beq+", "bne+", "ble+", "bgt+", "bge+", "blt+",
    "beq-", "bne-", "ble-", "bgt-", "bge-", "blt-",
    "b", "bl", "lwzx",
}

# Asm parse: jump-table block looks like
#     .obj "@55005", local
#         .rel Poll__15SaveLoadManagerFv, .L_80354438
#         .rel Poll__15SaveLoadManagerFv, .L_80353574
#         ...
#     .endobj "@55005"
_JT_OBJ_OPEN_RE = re.compile(rb'^\s*\.obj\s+"(@\d+)"')
_JT_OBJ_CLOSE_RE = re.compile(rb'^\s*\.endobj')
_JT_REL_RE = re.compile(rb'^\s*\.rel\s+([^,]+),\s*\.L_([0-9a-fA-F]+)')
_FN_OPEN_RE = re.compile(rb'^\s*\.fn\s+(\S+),')
_FN_CLOSE_RE = re.compile(rb'^\s*\.endfn\s+(\S+)')
_FN_REFS_JT_RE = re.compile(rb'"(@\d+)"')

# Cap variants per call.
_MAX_PHASE1_VARIANTS = 6
_MAX_PHASE2_VARIANTS = 1
_MAX_TOTAL_VARIANTS = _MAX_PHASE1_VARIANTS + _MAX_PHASE2_VARIANTS


class SwitchCaseReorderPattern(Pattern):
    name = "switch_case_reorder"
    safety_tier = "moderate"
    structural_domain = "control_flow"
    follow_ups = ("branch_polarity", "declaration_reorder")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Big jump-table state machines tend to surface branch-opcode
        # mismatches and large clusters when ordering drifts.
        for d in diagnosis.diff_ops:
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        if any(c.size >= 4 for c in diagnosis.clusters):
            return True
        if diagnosis.replace_real >= 3:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # When clusters are large the structural-reorder signal is strong.
        big_clusters = sum(1 for c in diagnosis.clusters if c.size >= 6)
        if big_clusters >= 2:
            return 0.7
        if any(c.size >= 4 for c in diagnosis.clusters):
            return 0.5
        return 0.3

    def context_priority(
        self, diagnosis: "Diagnosis", ctx: "FunctionContext"
    ) -> float:
        base = self.priority(diagnosis)
        # If the function actually contains a reorder-able switch, boost.
        if _function_has_reorderable_switch(ctx):
            return max(base, 0.5)
        return base

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0
        rng = random.Random(0xC4521E)  # deterministic — reproducible variants

        # Walk top-level statements (and one level of nesting) to find switches.
        switches = list(_iter_function_switches(ctx.body_node))

        for switch_node in switches:
            if counter >= _MAX_TOTAL_VARIANTS:
                break

            groups = _extract_case_groups(switch_node, source)
            if groups is None:
                continue
            if len(groups) < 3:
                continue

            # ----- Phase 2: asm-guided reorder (only if we can read .s) -----
            asm_order = _phase2_asm_order(ctx, groups, source)
            phase2_yielded = False
            if asm_order is not None and asm_order != list(range(len(groups))):
                ed = SourceEditor(source)
                ok = _apply_reorder(ed, source, groups, asm_order)
                if ok:
                    try:
                        new_source = ed.apply()
                        if new_source != source:
                            yield Variant(
                                name=f"swcase_asm_{counter}",
                                pattern_name=self.name,
                                description=(
                                    f"Reorder {len(groups)} switch cases to "
                                    f"target asm jump-table emission order"
                                ),
                                source=new_source,
                                tags=frozenset({
                                    "reordered_switch_cases",
                                    "asm_guided",
                                }),
                            )
                            counter += 1
                            phase2_yielded = True
                    except ValueError:
                        pass

            # ----- Phase 1: permutation fallback -----
            for variant in _phase1_permutations(
                groups, source, counter, phase2_yielded, rng, self.name
            ):
                yield variant
                counter += 1
                if counter >= _MAX_TOTAL_VARIANTS:
                    break


# ---------------------------------------------------------------------------
# Phase 1: AST-only permutations.
# ---------------------------------------------------------------------------

def _phase1_permutations(
    groups: list["_CaseGroup"],
    source: bytes,
    start_counter: int,
    phase2_yielded: bool,
    rng: random.Random,
    pattern_name: str,
) -> Iterator[Variant]:
    """Yield up to _MAX_PHASE1_VARIANTS permutation variants."""
    n = len(groups)
    indices = list(range(n))
    seen: set[tuple[int, ...]] = {tuple(indices)}

    # Build candidate permutations in priority order.
    candidates: list[tuple[str, list[int]]] = []

    # 1. Reverse order (cheap, often informative).
    rev = list(reversed(indices))
    if tuple(rev) not in seen:
        candidates.append(("reverse", rev))
        seen.add(tuple(rev))

    # 2. Adjacent pair swaps (1-2, 2-3, ...). Limit to a few.
    for i in range(n - 1):
        perm = list(indices)
        perm[i], perm[i + 1] = perm[i + 1], perm[i]
        key = tuple(perm)
        if key not in seen:
            candidates.append((f"swap_{i}_{i + 1}", perm))
            seen.add(key)
        if len(candidates) >= 4:
            break

    # 3. A handful of random permutations.
    for _ in range(16):
        perm = list(indices)
        rng.shuffle(perm)
        key = tuple(perm)
        if key in seen:
            continue
        candidates.append(("rand", perm))
        seen.add(key)
        if len(candidates) >= _MAX_PHASE1_VARIANTS:
            break

    counter = start_counter
    yielded = 0
    for desc, perm in candidates:
        if yielded >= _MAX_PHASE1_VARIANTS:
            break
        if perm == indices:
            continue

        ed = SourceEditor(source)
        if not _apply_reorder(ed, source, groups, perm):
            continue
        try:
            new_source = ed.apply()
        except ValueError:
            continue
        if new_source == source:
            continue

        yield Variant(
            name=f"swcase_{desc}_{counter}",
            pattern_name=pattern_name,
            description=(
                f"Permute {len(groups)} switch cases ({desc})"
                + ("" if not phase2_yielded else " — phase1 fallback")
            ),
            source=new_source,
            tags=frozenset({"reordered_switch_cases"}),
        )
        counter += 1
        yielded += 1


# ---------------------------------------------------------------------------
# Phase 2: asm-guided ordering.
# ---------------------------------------------------------------------------

def _phase2_asm_order(
    ctx: FunctionContext,
    groups: list["_CaseGroup"],
    source: bytes,
) -> list[int] | None:
    """Return a permutation of group indices in target body-emission order.

    Returns None if:
        * We cannot find the unit's .s file
        * We cannot find the function's jump table
        * We cannot confidently align case values to labels
    """
    asm_path = _find_asm_listing(ctx)
    if asm_path is None or not asm_path.exists():
        return None

    symbol = ctx.symbol or ""
    if not symbol:
        return None

    try:
        asm_bytes = asm_path.read_bytes()
    except OSError:
        return None

    jt = _parse_function_jump_table(asm_bytes, symbol)
    if jt is None:
        return None
    # jt is a list of (state_index, label_address_int).

    # Determine the default label — by far the most common destination.
    label_counts: dict[int, int] = {}
    for _, addr in jt:
        label_counts[addr] = label_counts.get(addr, 0) + 1
    if not label_counts:
        return None
    default_label = max(label_counts.items(), key=lambda kv: kv[1])[0]

    # Build state -> label and label -> ordered states.
    label_for_state: dict[int, int] = {state: addr for state, addr in jt}

    # Resolve each source case to its address (lowest, for fall-through groups).
    group_addrs: list[tuple[int, int]] = []  # (group_idx, address)
    for idx, grp in enumerate(groups):
        if grp.is_default:
            group_addrs.append((idx, default_label))
            continue
        addrs: list[int] = []
        for value in grp.values:
            int_val = _parse_int_constant_text(value)
            if int_val is None:
                # Symbolic case value — cannot align to asm. Bail.
                return None
            addr = label_for_state.get(int_val)
            if addr is None:
                # Case value not in jump table (out of range). Treat as default.
                addr = default_label
            addrs.append(addr)
        if not addrs:
            return None
        # A fall-through group should map to one label; use the min as the
        # canonical address (all entries route to the same body anyway).
        group_addrs.append((idx, min(addrs)))

    # Stable sort by address: that IS the target's source-emission order.
    sorted_groups = sorted(group_addrs, key=lambda x: (x[1], x[0]))
    new_order = [idx for idx, _ in sorted_groups]

    # If sorting is a no-op vs. the source order, skip.
    if new_order == list(range(len(groups))):
        return None
    return new_order


def _find_asm_listing(ctx: FunctionContext) -> Path | None:
    """Locate the dtk-format .s file containing the target function.

    Layout: build/<BUILD_ID>/asm/<unit-path>.s mirrors src/<unit-path>.cpp.
    """
    try:
        src_path = ctx.file_path.resolve()
    except OSError:
        return None

    # Walk up to find the 'src' directory marker, then the repo root above it.
    parts = src_path.parts
    if "src" not in parts:
        return None
    src_idx = parts.index("src")
    repo_root = Path(*parts[:src_idx])
    unit_rel = Path(*parts[src_idx + 1:]).with_suffix(".s")

    # Try known build IDs (RB3 = SZBE69_B8, DC3 = 373307D9, others tolerated).
    candidates = [
        repo_root / "build" / "SZBE69_B8" / "asm" / unit_rel,
        repo_root / "build" / "373307D9" / "asm" / unit_rel,
    ]
    # Also: enumerate any build/*/asm if neither default exists (cheap; <10 dirs).
    build_root = repo_root / "build"
    if build_root.is_dir():
        for child in build_root.iterdir():
            asm_dir = child / "asm"
            if asm_dir.is_dir():
                candidates.append(asm_dir / unit_rel)

    for cand in candidates:
        if cand.exists():
            return cand
    return None


def _parse_function_jump_table(
    asm_bytes: bytes, symbol: str
) -> list[tuple[int, int]] | None:
    """Find the .obj "@NNNNN" block referenced by `symbol` and parse it.

    Returns list of (state_index, label_address_int), ordered by state index.
    Returns None if we cannot confidently identify the jump table for symbol.
    """
    sym_b = symbol.encode("ascii", errors="ignore")

    # 1. Locate the function body and collect any "@NNNNN" jump-table names it
    #    references via "@NNNNN"@ha / @l reloc forms.
    fn_jt_names = _collect_jt_names_in_function(asm_bytes, sym_b)
    if not fn_jt_names:
        return None

    # 2. Walk .obj/.endobj blocks; if the block name is in fn_jt_names AND its
    #    rel-entries reference our symbol, parse it out.
    lines = asm_bytes.splitlines()
    best: list[tuple[int, int]] = []
    in_obj = False
    cur_name = b""
    cur_entries: list[int] = []
    cur_refs_symbol = False

    for raw in lines:
        if not in_obj:
            m = _JT_OBJ_OPEN_RE.match(raw)
            if m:
                cur_name = m.group(1)
                cur_entries = []
                cur_refs_symbol = False
                in_obj = (cur_name in fn_jt_names)
            continue

        # in_obj
        if _JT_OBJ_CLOSE_RE.match(raw):
            if cur_refs_symbol and cur_entries:
                # Prefer the longest matching block (state machines have many entries).
                if len(cur_entries) > len(best):
                    best = [(i, addr) for i, addr in enumerate(cur_entries)]
            in_obj = False
            cur_name = b""
            cur_entries = []
            cur_refs_symbol = False
            continue

        m = _JT_REL_RE.match(raw)
        if m:
            ref_sym = m.group(1).strip()
            if ref_sym == sym_b:
                cur_refs_symbol = True
            try:
                addr = int(m.group(2), 16)
            except ValueError:
                continue
            cur_entries.append(addr)

    return best if best else None


def _collect_jt_names_in_function(
    asm_bytes: bytes, sym_b: bytes
) -> set[bytes]:
    """Return the set of "@NNNNN" jump-table names that `sym_b` references."""
    fn_jt_names: set[bytes] = set()
    lines = asm_bytes.splitlines()
    in_fn = False
    for raw in lines:
        if not in_fn:
            m = _FN_OPEN_RE.match(raw)
            if m and m.group(1) == sym_b:
                in_fn = True
            continue
        # in_fn
        m_close = _FN_CLOSE_RE.match(raw)
        if m_close and m_close.group(1) == sym_b:
            break
        for m in _FN_REFS_JT_RE.finditer(raw):
            fn_jt_names.add(m.group(1))
    return fn_jt_names


# ---------------------------------------------------------------------------
# Case group extraction.
# ---------------------------------------------------------------------------

class _CaseGroup:
    """A run of one or more case_statement nodes that share a body.

    A typical case_statement is its own group (one entry).  Fall-through
    groups (``case 3: case 4: body``) are coalesced into a single group
    because they MUST move together.

    Attributes:
        values: list of case-value text (e.g. [b"kS_Start", b"0x42"]). Empty
                for the default group.
        is_default: True if the group is the `default:` clause.
        nodes: the tree-sitter case_statement nodes (in source order).
        start_byte/end_byte: inclusive byte range covering the whole group,
            from the first case_statement's start to the body's end (the body
            lives inside the last case_statement node).
        body_terminator: kind of terminator used by the body — "break",
            "return", "continue", "goto", "throw", or "unknown".
    """

    __slots__ = (
        "values", "is_default", "nodes", "start_byte", "end_byte",
        "body_terminator", "raw_lines_start",
    )

    def __init__(self) -> None:
        self.values: list[bytes] = []
        self.is_default: bool = False
        self.nodes: list[Node] = []
        self.start_byte: int = 0
        self.end_byte: int = 0
        self.body_terminator: str = "unknown"
        self.raw_lines_start: int = 0  # start of the line containing the first case


def _extract_case_groups(
    switch_node: Node, source: bytes
) -> list[_CaseGroup] | None:
    """Extract reorderable case groups from a switch_statement.

    Returns None when the switch cannot be safely reordered:
        * No body found
        * Any case has no terminator (fall-through into another case body)
        * Any case body contains a goto whose label is inside another case
    """
    body = switch_node.child_by_field_name("body")
    if body is None or body.type != "compound_statement":
        return None

    case_children = [c for c in noncomment_named_children(body)
                     if c.type == "case_statement"]
    if not case_children:
        return None

    groups: list[_CaseGroup] = []
    current: _CaseGroup | None = None

    for case_node in case_children:
        value_node = case_node.child_by_field_name("value")
        # Body statements = named children minus the value child.
        body_stmts = [
            c for c in noncomment_named_children(case_node)
            if value_node is None or c.id != value_node.id
        ]
        is_default = value_node is None

        if not body_stmts:
            # Fall-through label: open a new group if needed, append this case to it.
            if current is None:
                current = _CaseGroup()
                current.start_byte = _line_start(source, case_node.start_byte)
                current.raw_lines_start = current.start_byte
            current.nodes.append(case_node)
            if is_default:
                current.is_default = True
            else:
                current.values.append(_node_text(source, value_node))
            current.end_byte = case_node.end_byte
            continue

        # Body-bearing case: terminates the (possibly empty) fall-through chain.
        if current is None:
            current = _CaseGroup()
            current.start_byte = _line_start(source, case_node.start_byte)
            current.raw_lines_start = current.start_byte

        current.nodes.append(case_node)
        if is_default:
            current.is_default = True
        else:
            current.values.append(_node_text(source, value_node))

        # Find the body's terminator.
        term = _detect_terminator(body_stmts, source)
        if term == "none":
            # Fall-through INTO the next case — NOT safe to reorder.
            return None
        current.body_terminator = term
        current.end_byte = _line_end_inclusive(source, case_node.end_byte)
        groups.append(current)
        current = None

    if current is not None:
        # Trailing fall-through label without a body — pathological.
        return None

    # Safety: refuse if any body contains a goto to a label inside the switch
    # body but outside its own case (cross-case jumps make reorder unsafe).
    label_owners = _collect_label_owners(groups, source)
    for group in groups:
        for node in group.nodes:
            for goto in _find_gotos(node):
                tgt = _goto_target(goto, source)
                if tgt is None:
                    continue
                owner = label_owners.get(tgt)
                if owner is not None and owner is not group:
                    return None

    return groups


def _detect_terminator(body_stmts: list[Node], source: bytes) -> str:
    """Detect whether the LAST executed statement is a hard terminator.

    Returns one of: "break", "return", "continue", "goto", "throw",
    "unknown", "none".

    "none" means the body falls through (no terminator) — caller MUST refuse.
    """
    # Drill into a trailing compound_statement: a case body like
    #   case 2: { ... break; }
    # has only one body_stmt = the compound_statement.
    last = body_stmts[-1]
    while last.type == "compound_statement":
        inner = noncomment_named_children(last)
        if not inner:
            return "none"
        last = inner[-1]

    t = last.type
    if t == "break_statement":
        return "break"
    if t == "return_statement":
        return "return"
    if t == "continue_statement":
        return "continue"
    if t == "goto_statement":
        return "goto"
    if t == "throw_statement":
        return "throw"
    # If the last statement is an if/while/for/switch, we can't easily prove
    # both branches terminate. Treat as fall-through (refuse).
    return "none"


def _collect_label_owners(
    groups: list[_CaseGroup], source: bytes
) -> dict[bytes, _CaseGroup]:
    """Map each labeled_statement's label text to the group that owns it."""
    owners: dict[bytes, _CaseGroup] = {}
    for group in groups:
        for node in group.nodes:
            for n in walk(node):
                if n.type == "labeled_statement":
                    label = n.child_by_field_name("label")
                    if label is not None:
                        owners[_node_text(source, label)] = group
    return owners


def _find_gotos(node: Node) -> Iterator[Node]:
    for n in walk(node):
        if n.type == "goto_statement":
            yield n


def _goto_target(goto_node: Node, source: bytes) -> bytes | None:
    label = goto_node.child_by_field_name("label")
    if label is None:
        # Older grammars: scan named children for a statement_identifier.
        for c in goto_node.named_children:
            if c.type in ("statement_identifier", "identifier"):
                label = c
                break
    if label is None:
        return None
    return _node_text(source, label)


# ---------------------------------------------------------------------------
# Reorder application.
# ---------------------------------------------------------------------------

def _apply_reorder(
    ed: SourceEditor,
    source: bytes,
    groups: list[_CaseGroup],
    new_order: list[int],
) -> bool:
    """Rewrite the switch body so groups appear in `new_order`.

    Each group is replaced as a contiguous byte range, in-place: the i-th
    group's range gets the text of the new_order[i]-th group. Whitespace
    BETWEEN groups is left untouched (it'll just join the new neighbors).
    """
    if len(new_order) != len(groups):
        return False
    if sorted(new_order) != list(range(len(groups))):
        return False

    # Extract original texts.
    texts = [source[g.start_byte:g.end_byte] for g in groups]
    for i, g in enumerate(groups):
        new_text = texts[new_order[i]]
        if new_text == source[g.start_byte:g.end_byte]:
            continue
        ed.replace_range(g.start_byte, g.end_byte, new_text)
    return True


# ---------------------------------------------------------------------------
# AST helpers.
# ---------------------------------------------------------------------------

def _iter_function_switches(body: Node) -> Iterator[Node]:
    """Yield switch_statement nodes inside a function body.

    Walks the whole subtree (nested switches included).
    """
    for n in walk(body):
        if n.type == "switch_statement":
            yield n


def _function_has_reorderable_switch(ctx: FunctionContext) -> bool:
    """Cheap check: does the function contain ANY switch with >=3 cases?"""
    for sw in _iter_function_switches(ctx.body_node):
        body = sw.child_by_field_name("body")
        if body is None:
            continue
        case_count = sum(
            1 for c in noncomment_named_children(body) if c.type == "case_statement"
        )
        if case_count >= 3:
            return True
    return False


def _node_text(source: bytes, node: Node) -> bytes:
    return source[node.start_byte:node.end_byte]


def _line_start(source: bytes, pos: int) -> int:
    while pos > 0 and source[pos - 1:pos] not in (b"\n", b"\r"):
        pos -= 1
    return pos


def _line_end_inclusive(source: bytes, pos: int) -> int:
    """Return the offset just after the newline at the end of pos's line."""
    while pos < len(source) and source[pos:pos + 1] not in (b"\n", b"\r"):
        pos += 1
    if pos < len(source) and source[pos:pos + 1] == b"\r":
        pos += 1
    if pos < len(source) and source[pos:pos + 1] == b"\n":
        pos += 1
    return pos


def _parse_int_constant_text(text: bytes) -> int | None:
    """Parse a case-value text into an int.

    Handles:
        decimal (42), hex (0x2A), octal (052), char ('A'), and simple
        ``(SomeEnum)42`` / ``(SomeEnum)0x42`` casts. Symbolic enum
        identifiers (``kS_Start``) return None — the asm-guided path bails
        cleanly when a value can't be resolved.
    """
    s = text.strip().decode("utf-8", errors="replace")
    # Strip enclosing parens.
    while s.startswith("(") and s.endswith(")"):
        s = s[1:-1].strip()
    # Strip C-style cast: (Foo)value
    if s.startswith("(") and ")" in s:
        end = s.index(")")
        s = s[end + 1:].strip()

    # Strip enclosing parens AGAIN (e.g. ``((Foo)5)``).
    while s.startswith("(") and s.endswith(")"):
        s = s[1:-1].strip()

    # Char literal.
    if len(s) >= 3 and s.startswith("'") and s.endswith("'"):
        inner = s[1:-1]
        if len(inner) == 1:
            return ord(inner)
        if inner.startswith("\\") and len(inner) == 2:
            esc = {
                "n": 10, "t": 9, "r": 13, "0": 0, "\\": 92,
                "'": 39, '"': 34,
            }.get(inner[1])
            return esc

    # Number literal.
    try:
        return int(s, 0)
    except ValueError:
        return None
