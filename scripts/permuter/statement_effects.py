"""Shared statement-effect analysis for reorder-style permuter patterns."""

from __future__ import annotations

from dataclasses import dataclass
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
