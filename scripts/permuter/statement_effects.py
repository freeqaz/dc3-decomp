"""Shared statement-effect analysis for reorder-style permuter patterns.

Provides:
- Per-statement reads/writes/call classification (StatementEffects)
- Alias tracking for reference bindings (auto& ref = obj.member)
- Def-use chain construction across statement sequences
- Pairwise reordering safety checks
"""

from __future__ import annotations

from dataclasses import dataclass, field
import re

from tree_sitter import Node

from .ast_queries import identifiers_in, walk

_ASSERT_GUARDS = (b"MILO_ASSERT", b"MILO_FAIL", b"assert(", b"ASSERT(")
_ACCESSOR_CALL_RE = re.compile(
    r"^(Get[A-Z_]\w*|Is[A-Z_]\w*|Has[A-Z_]\w*|size|empty|length|begin|end|front|back|c_str)$"
)
_MUTATOR_CALL_RE = re.compile(
    r"^(Set|Add|Remove|Push|Pop|Append|Insert|Erase|Update|Init|Reset|Release|Destroy|Load|Save|Write)[A-Z_]\w*$"
)
_LOGGING_CALL_RE = re.compile(
    r"^(printf|fprintf|sprintf|snprintf|puts|Log[A-Z_]\w*|Warn[A-Z_]\w*|Notify[A-Z_]\w*|MILO_LOG|MILO_WARN|MILO_NOTIFY)$"
)


@dataclass(frozen=True)
class AliasInfo:
    """Tracks a reference or pointer alias binding."""
    alias_name: str        # The local name (e.g., "ref")
    target: str            # The aliased target (e.g., "obj.member" or "obj")
    target_root: str       # Root identifier (e.g., "obj")
    is_reference: bool     # True for &, False for * (pointer)


@dataclass(frozen=True)
class DefUseEntry:
    """A single definition-use relationship."""
    variable: str
    def_stmt_idx: int      # Statement index where defined
    use_stmt_idx: int      # Statement index where used


@dataclass(frozen=True)
class DefUseChains:
    """Def-use chains for a statement sequence."""
    entries: tuple[DefUseEntry, ...]
    live_ranges: dict[str, tuple[int, int]]  # var -> (first_def, last_use)

    def is_live_between(self, var: str, start: int, end: int) -> bool:
        """Check if variable is live between statement indices [start, end)."""
        rng = self.live_ranges.get(var)
        if rng is None:
            return False
        return rng[0] <= start and rng[1] >= end

    def can_move_past(self, stmt_idx: int, target_idx: int) -> bool:
        """Check if moving statement at stmt_idx past target_idx is safe.

        Safe if no variable defined at stmt_idx is used between stmt_idx
        and target_idx, and no variable used at stmt_idx is defined between.
        """
        lo, hi = min(stmt_idx, target_idx), max(stmt_idx, target_idx)
        for entry in self.entries:
            # Def at stmt_idx, use in between
            if entry.def_stmt_idx == stmt_idx and lo < entry.use_stmt_idx <= hi:
                return False
            # Use at stmt_idx, def in between
            if entry.use_stmt_idx == stmt_idx and lo <= entry.def_stmt_idx < hi:
                return False
        return True


@dataclass(frozen=True)
class StatementEffects:
    """Summary of a statement's observable local effects."""

    reads: frozenset[str]
    writes: frozenset[str]
    has_control_flow: bool
    has_call: bool
    has_direct_call: bool
    call_names: frozenset[str]
    call_kinds: frozenset[str]
    dereferenced_identifiers: frozenset[str]
    has_assert_like_guard: bool
    aliases: tuple[AliasInfo, ...] = ()  # Reference/pointer aliases created


class StatementEffectAnalyzer:
    """Cacheable per-source analyzer for statement reads/writes and hazards."""

    def __init__(self, source: bytes):
        self.source = source
        self._cache: dict[int, StatementEffects] = {}

    def analyze(self, stmt: Node) -> StatementEffects:
        cached = self._cache.get(stmt.id)
        if cached is not None:
            return cached

        writes: set[str] = set()
        has_control_flow = False
        has_call = False
        has_direct_call = _has_direct_call(stmt)
        call_names: set[str] = set()
        call_kinds: set[str] = set()
        dereferenced_identifiers: set[str] = set()

        for node in walk(stmt):
            if node.type == "assignment_expression":
                left = node.child_by_field_name("left")
                if left is not None:
                    writes.update(_collect_lvalue_roots(left))
            elif node.type == "update_expression":
                arg = node.child_by_field_name("argument")
                if arg is not None:
                    writes.update(_collect_lvalue_roots(arg))
            elif node.type == "declaration":
                declarator = node.child_by_field_name("declarator")
                if declarator is not None:
                    name = _extract_decl_name(declarator)
                    if name:
                        writes.add(name)

            if node.type in (
                "return_statement",
                "break_statement",
                "continue_statement",
                "goto_statement",
            ):
                has_control_flow = True

            if node.type == "call_expression":
                has_call = True
                name = _extract_call_name(node, self.source)
                if name:
                    call_names.add(name)
                    call_kinds.add(_classify_call_name(name))
                else:
                    call_kinds.add("unknown")

            if node.type == "field_expression":
                op = node.child_by_field_name("operator")
                arg = node.child_by_field_name("argument")
                if (
                    op is not None
                    and op.text == b"->"
                    and arg is not None
                    and arg.type == "identifier"
                    and arg.text
                ):
                    dereferenced_identifiers.add(arg.text.decode())

        # Detect alias bindings (auto& ref = obj.member, Type* p = &obj)
        aliases = _detect_aliases(stmt, self.source)

        reads = identifiers_in(stmt) - writes
        text = self.source[stmt.start_byte:stmt.end_byte]
        effects = StatementEffects(
            reads=frozenset(reads),
            writes=frozenset(writes),
            has_control_flow=has_control_flow,
            has_call=has_call,
            has_direct_call=has_direct_call,
            call_names=frozenset(call_names),
            call_kinds=frozenset(call_kinds),
            dereferenced_identifiers=frozenset(dereferenced_identifiers),
            has_assert_like_guard=any(marker in text for marker in _ASSERT_GUARDS),
            aliases=aliases,
        )
        self._cache[stmt.id] = effects
        return effects

    def are_independent(
        self,
        stmt_a: Node,
        stmt_b: Node,
        *,
        allow_call_pair: bool = False,
    ) -> bool:
        """Return True when two statements can be safely reordered by core rules."""
        effects_a = self.analyze(stmt_a)
        effects_b = self.analyze(stmt_b)

        if effects_a.has_control_flow or effects_b.has_control_flow:
            return False

        if effects_a.writes & effects_b.reads:
            return False
        if effects_a.reads & effects_b.writes:
            return False
        if effects_a.writes & effects_b.writes:
            return False

        if not allow_call_pair and effects_a.has_call and effects_b.has_call:
            return False

        return True

    def can_reorder_call_pair(self, stmt_a: Node, stmt_b: Node) -> bool:
        """Return True when a pair of call statements is safe to reorder."""
        effects_a = self.analyze(stmt_a)
        effects_b = self.analyze(stmt_b)

        if not self.are_independent(stmt_a, stmt_b, allow_call_pair=True):
            return False

        blocked_kinds = {"guard", "logging", "mutator"}
        if effects_a.call_kinds & blocked_kinds:
            return False
        if effects_b.call_kinds & blocked_kinds:
            return False

        shared_inputs = effects_a.reads & effects_b.reads
        if shared_inputs:
            if "unknown" in effects_a.call_kinds or "unknown" in effects_b.call_kinds:
                return False

        return True

    def can_reorder_statement_pair(self, stmt_a: Node, stmt_b: Node) -> bool:
        """Return True when two general statements are safe to reorder."""
        effects_a = self.analyze(stmt_a)
        effects_b = self.analyze(stmt_b)

        if not self.are_independent(stmt_a, stmt_b, allow_call_pair=True):
            return False

        if effects_a.has_call and effects_b.has_call:
            return self.can_reorder_call_pair(stmt_a, stmt_b)

        blocked_kinds = {"guard", "logging", "mutator"}
        if effects_a.has_direct_call and effects_a.call_kinds & blocked_kinds and effects_b.writes:
            return False
        if effects_b.has_direct_call and effects_b.call_kinds & blocked_kinds and effects_a.writes:
            return False

        return True


def _extract_decl_name(declarator: Node) -> str | None:
    """Extract the variable name from a declarator node."""
    if declarator.type == "identifier" and declarator.text:
        return declarator.text.decode()

    if declarator.type == "init_declarator":
        inner = declarator.child_by_field_name("declarator")
        if inner is not None:
            return _extract_decl_name(inner)

    for child in declarator.named_children:
        result = _extract_decl_name(child)
        if result:
            return result
    return None


def _collect_lvalue_roots(node: Node) -> set[str]:
    """Collect conservative root identifiers written by an lvalue expression."""
    if node.type == "identifier" and node.text:
        return {node.text.decode()}

    if node.type == "field_expression":
        arg = node.child_by_field_name("argument")
        return _collect_lvalue_roots(arg) if arg is not None else set()

    if node.type == "subscript_expression":
        arg = node.child_by_field_name("argument")
        if arg is not None:
            return _collect_lvalue_roots(arg)
        if node.named_child_count >= 1:
            return _collect_lvalue_roots(node.named_children[0])
        return set()

    if node.type in (
        "parenthesized_expression",
        "pointer_expression",
        "reference_expression",
        "unary_expression",
        "cast_expression",
        "c_style_cast_expression",
    ):
        arg = (
            node.child_by_field_name("argument")
            or node.child_by_field_name("operand")
            or node.child_by_field_name("value")
        )
        if arg is not None:
            return _collect_lvalue_roots(arg)
        for child in node.named_children:
            roots = _collect_lvalue_roots(child)
            if roots:
                return roots

    return set()


def _extract_call_name(node: Node, source: bytes) -> str | None:
    """Extract a conservative bare name from a call expression."""
    func = node.child_by_field_name("function")
    if func is None:
        return None

    text = source[func.start_byte:func.end_byte].decode("utf-8", errors="replace").strip()
    for sep in ("::", "->", "."):
        if sep in text:
            text = text.rsplit(sep, 1)[-1]
    return text or None


def _has_direct_call(stmt: Node) -> bool:
    """Return True when the statement's top-level effect is a call."""
    if stmt.type == "expression_statement":
        return any(child.type == "call_expression" for child in stmt.named_children)

    if stmt.type == "declaration":
        declarator = stmt.child_by_field_name("declarator")
        if declarator is None:
            return False
        if declarator.type == "init_declarator":
            value = declarator.child_by_field_name("value")
            if value is not None:
                return _unwrap_call_like(value) is not None

    return False


def _unwrap_call_like(node: Node) -> Node | None:
    """Unwrap casts and parens to detect direct call-like initializers."""
    current = node
    while current.type in (
        "parenthesized_expression",
        "cast_expression",
        "c_style_cast_expression",
    ):
        child = current.child_by_field_name("value") or current.child_by_field_name("argument")
        if child is None and current.named_children:
            child = current.named_children[0]
        if child is None:
            return None
        current = child
    if current.type == "call_expression":
        return current
    return None


def _classify_call_name(name: str) -> str:
    """Classify a call name for future reorder heuristics."""
    if name in {"MILO_ASSERT", "MILO_FAIL", "assert", "ASSERT"}:
        return "guard"
    if _LOGGING_CALL_RE.match(name):
        return "logging"
    if _MUTATOR_CALL_RE.match(name):
        return "mutator"
    if _ACCESSOR_CALL_RE.match(name):
        return "accessor"
    return "unknown"


# ---------------------------------------------------------------------------
# Alias detection
# ---------------------------------------------------------------------------

_REF_TYPE_QUALIFIERS = frozenset({
    "auto", "const", "volatile", "static",
})


def _detect_aliases(stmt: Node, source: bytes) -> tuple[AliasInfo, ...]:
    """Detect reference/pointer alias bindings in a declaration statement.

    Recognizes patterns like:
        auto& ref = obj.member;
        const auto& r = vec;
        Type* p = &obj;
        auto* p = ptr->field;
    """
    if stmt.type != "declaration":
        return ()

    # Check if the type specifier or declarator contains & or *
    declarator = stmt.child_by_field_name("declarator")
    if declarator is None or declarator.type != "init_declarator":
        return ()

    inner_decl = declarator.child_by_field_name("declarator")
    value = declarator.child_by_field_name("value")
    if inner_decl is None or value is None:
        return ()

    # Extract the alias name
    alias_name = _extract_decl_name(inner_decl)
    if not alias_name:
        return ()

    # Determine if this is a reference or pointer binding
    is_reference = _decl_is_reference(stmt, inner_decl, source)
    is_pointer = _decl_is_pointer(inner_decl, source) if not is_reference else False

    if not is_reference and not is_pointer:
        return ()

    # Extract the target expression
    target_text = source[value.start_byte:value.end_byte].decode("utf-8", errors="replace").strip()

    # For pointer bindings via &obj, unwrap the address-of
    if is_pointer and target_text.startswith("&"):
        target_text = target_text[1:].strip()

    # Extract root identifier from target
    target_root = _extract_target_root(value, is_pointer)
    if not target_root:
        return ()

    return (AliasInfo(
        alias_name=alias_name,
        target=target_text,
        target_root=target_root,
        is_reference=is_reference,
    ),)


def _decl_is_reference(stmt: Node, inner_decl: Node, source: bytes) -> bool:
    """Check if declaration uses reference binding (& in type or declarator)."""
    # Check for reference_declarator wrapping
    if inner_decl.type == "reference_declarator":
        return True

    # Check the type specifier text for &
    type_node = stmt.child_by_field_name("type")
    if type_node is not None:
        type_text = source[type_node.start_byte:type_node.end_byte].decode("utf-8", errors="replace")
        if "&" in type_text:
            return True

    # Check for & between type and declarator in the raw text
    stmt_text = source[stmt.start_byte:stmt.end_byte].decode("utf-8", errors="replace")
    if "&" in stmt_text and "*" not in stmt_text:
        return True

    return False


def _decl_is_pointer(inner_decl: Node, source: bytes) -> bool:
    """Check if declaration uses pointer binding (* in declarator)."""
    if inner_decl.type == "pointer_declarator":
        return True
    return False


def _extract_target_root(value_node: Node, is_pointer: bool) -> str | None:
    """Extract the root identifier from an alias target expression."""
    node = value_node

    # Unwrap address-of for pointer bindings
    if node.type == "pointer_expression":
        arg = node.child_by_field_name("argument")
        if arg is not None:
            node = arg

    # Unwrap parentheses
    while node.type == "parenthesized_expression" and node.named_children:
        node = node.named_children[0]

    # Direct identifier
    if node.type == "identifier" and node.text:
        return node.text.decode()

    # Field expression (obj.member or obj->member)
    if node.type == "field_expression":
        arg = node.child_by_field_name("argument")
        if arg is not None:
            return _extract_target_root(arg, False)

    # Subscript expression (arr[i])
    if node.type == "subscript_expression":
        arg = node.child_by_field_name("argument")
        if arg is not None:
            return _extract_target_root(arg, False)

    return None


# ---------------------------------------------------------------------------
# Def-use chain construction
# ---------------------------------------------------------------------------


def build_def_use_chains(
    stmts: list[Node],
    analyzer: StatementEffectAnalyzer,
) -> DefUseChains:
    """Build def-use chains across a flat statement sequence.

    For each variable, tracks which statement defines it and which later
    statements use it.  This enables safe reordering checks beyond simple
    pairwise independence.
    """
    # Pass 1: collect per-statement reads/writes
    stmt_effects = [(i, analyzer.analyze(s)) for i, s in enumerate(stmts)]

    # Pass 2: for each variable, find definitions and uses
    # A "definition" is any statement that writes the variable.
    # A "use" is any statement that reads the variable.
    defs: dict[str, list[int]] = {}  # var -> [stmt indices that define it]
    uses: dict[str, list[int]] = {}  # var -> [stmt indices that use it]

    for idx, effects in stmt_effects:
        for w in effects.writes:
            defs.setdefault(w, []).append(idx)
        for r in effects.reads:
            uses.setdefault(r, []).append(idx)
        # Aliases create implicit reads of the target root
        for alias in effects.aliases:
            uses.setdefault(alias.target_root, []).append(idx)

    # Pass 3: build def-use entries
    # For each definition, find all subsequent uses (before the next definition)
    entries: list[DefUseEntry] = []
    all_vars = set(defs.keys()) | set(uses.keys())

    for var in all_vars:
        var_defs = sorted(defs.get(var, []))
        var_uses = sorted(uses.get(var, []))

        for d_idx in var_defs:
            # Find next definition (if any) to bound the reach
            next_def = None
            for nd in var_defs:
                if nd > d_idx:
                    next_def = nd
                    break

            # Link to all uses between this def and the next def
            for u_idx in var_uses:
                if u_idx <= d_idx:
                    continue
                if next_def is not None and u_idx >= next_def:
                    continue
                entries.append(DefUseEntry(
                    variable=var,
                    def_stmt_idx=d_idx,
                    use_stmt_idx=u_idx,
                ))

        # Variables used without a prior definition (parameters, globals)
        # are "live-in" — create entries from a virtual def at -1
        if var_uses and (not var_defs or var_uses[0] < var_defs[0]):
            first_def = var_defs[0] if var_defs else len(stmts)
            for u_idx in var_uses:
                if u_idx >= first_def:
                    break
                entries.append(DefUseEntry(
                    variable=var,
                    def_stmt_idx=-1,
                    use_stmt_idx=u_idx,
                ))

    # Pass 4: compute live ranges (first_def, last_use)
    live_ranges: dict[str, tuple[int, int]] = {}
    for var in all_vars:
        var_defs = defs.get(var, [])
        var_uses = uses.get(var, [])
        all_touches = var_defs + var_uses
        if all_touches:
            first = min(all_touches)
            last = max(all_touches)
            live_ranges[var] = (first, last)

    return DefUseChains(
        entries=tuple(entries),
        live_ranges=live_ranges,
    )
