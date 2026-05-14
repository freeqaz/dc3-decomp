"""Helper-method inlining: replace `obj->IsX()` with the helper's body.

Win source: 2026-05-12 upstream merge wave —
  Challenges::GetGlobalChallengeSongName: 98.2% → 100%
  Challenges::GetDlcChallengeSongID: 97.7% → 100%
  ChallengeHeaderNode::GetPotentialChallengeExp: 98.1% → 100%

When a function is in the 96-99% range and calls a one-line inline helper
defined in a header (e.g. `bool IsHMXChallenge() { return mType >= ...; }`),
manually inlining the helper's body at the call site sometimes shifts
register allocation enough to close the gap, even though the compiler was
already inlining the helper itself.

This pattern:
1. Walks the function body looking for zero-arg method/free-function calls
2. Looks up the callee's body in any included header
3. If the body is a single `return <expr>;` (or an empty body), generates
   a variant that splices the expression at the call site
4. For member calls (`obj->Method()`), rewrites bare `mFoo` member references
   in the body to `obj->mFoo` so the splice still type-checks

Bounded to 6 variants per pass. Skips helpers with multi-statement bodies,
helpers that take arguments, helpers whose body references `this->` chains
beyond simple `mFoo` accesses, and helpers that contain function calls
themselves (those add too much complexity for a one-shot splice).

Documented in:
    docs/decomp/patterns/fixable-control-flow.md (`Manual Helper Inlining`)
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import find_calls, walk
from ..editor import SourceEditor
from ..extractor import _PARSER, _find_all_function_defs, _get_function_name
from ..header_impact import resolve_included_files
from ..types import Diagnosis, FunctionContext, Variant


_MAX_VARIANTS = 6
# Maximum body length we'll splice — keeps the variants readable and
# avoids ballooning the source with multi-line bodies.
_MAX_BODY_BYTES = 200

# Regex for Hmx-style member names that we'll prefix with `obj->`
_MEMBER_RE = re.compile(rb"\bm[A-Z]\w*\b")

# Helpers we don't want to inline even if eligible (STL, common framework)
_SKIP_NAMES = frozenset({
    "begin", "end", "size", "empty", "clear", "front", "back",
    "data", "Length", "length", "c_str", "Str", "Get",
})


class HelperInlinePattern(Pattern):
    name = "helper_inline"
    safety_tier = "moderate"
    structural_domain = "data_flow"
    follow_ups = ("variable_extraction", "declaration_reorder")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Fires on functions in the fine-tuning range (any real diff).
        if diagnosis.replace_real > 0:
            return True
        if diagnosis.clusters:
            return True
        if diagnosis.reg_swap_pairs:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Modest priority — helpful but not always.
        return 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        body = ctx.body_node

        # Build header lookup lazily — most ctx have at least a few sites
        # to consider, so the cost is amortized.
        header_defs = None  # populated on demand

        counter = 0
        for call_node in find_calls(body):
            if counter >= _MAX_VARIANTS:
                break
            if not ctx.node_in_mismatch_region(call_node):
                continue

            callee_info = _classify_call(call_node, source)
            if callee_info is None:
                continue
            method_name, receiver_text, op_text = callee_info
            if method_name in _SKIP_NAMES:
                continue

            # Lazy-load header defs
            if header_defs is None:
                header_defs = _collect_header_function_defs(ctx.file_path)

            # Resolve callee → header function def
            body_expr = _lookup_helper_body(method_name, header_defs)
            if body_expr is None:
                continue
            if len(body_expr) > _MAX_BODY_BYTES:
                continue

            # Substitute member references with obj->member
            substituted = _substitute_members(body_expr, receiver_text, op_text)
            if substituted is None:
                continue

            # Splice into the call site
            ed = SourceEditor(source)
            ed.replace_node(call_node, b"(" + substituted + b")")
            try:
                new_source = ed.apply()
            except ValueError:
                continue

            recv_str = receiver_text.decode("utf-8", errors="replace")
            yield Variant(
                name=f"helperinline_{counter}",
                pattern_name=self.name,
                description=(
                    f"Inline {recv_str}{op_text.decode()}{method_name}() body"
                ),
                source=new_source,
            )
            counter += 1


# ---------------------------------------------------------------------------
# Call classification
# ---------------------------------------------------------------------------


def _classify_call(
    call_node: Node, source: bytes
) -> tuple[str, bytes, bytes] | None:
    """Return (method_name, receiver_text, op_text) for inlinable calls.

    op_text is `b"->"` or `b"."` for member calls, or `b""` for free
    functions (in which case receiver_text is empty).

    Only returns calls with no arguments.
    """
    func = call_node.child_by_field_name("function")
    args = call_node.child_by_field_name("arguments")
    if func is None or args is None:
        return None
    # Skip calls with arguments
    arg_kids = [c for c in args.named_children if c.type != "comment"]
    if arg_kids:
        return None

    if func.type == "identifier":
        # Free function call: Foo()
        name = func.text
        if not name:
            return None
        return name.decode("utf-8", errors="replace"), b"", b""

    if func.type == "field_expression":
        receiver = func.child_by_field_name("argument")
        field = func.child_by_field_name("field")
        if receiver is None or field is None:
            return None
        receiver_text = source[receiver.start_byte:receiver.end_byte]
        method_name = source[field.start_byte:field.end_byte].decode(
            "utf-8", errors="replace"
        )
        # Detect operator
        between = source[receiver.end_byte:field.start_byte]
        op = b"->" if b"->" in between else b"."
        return method_name, receiver_text, op

    return None


# ---------------------------------------------------------------------------
# Header lookup (mirrors noinline_stub.py)
# ---------------------------------------------------------------------------


def _collect_header_function_defs(
    source_path: Path,
) -> dict[str, tuple[Node, Node, bytes]]:
    """Collect (func_node, body_node, header_source) by simple name.

    Returns a name → first-match map. Unqualified names resolve to the
    first Class::Method or free-function with that suffix.
    """
    project_root = _project_root_for(source_path)
    out: dict[str, tuple[Node, Node, bytes]] = {}
    for header_path in resolve_included_files(source_path, project_root):
        if header_path.suffix.lower() not in {".h", ".hh", ".hpp", ".hxx", ".inl"}:
            continue
        try:
            header_source = header_path.read_bytes()
        except OSError:
            continue
        tree = _PARSER.parse(header_source)
        for func_node in _find_all_function_defs(tree.root_node):
            name = _get_function_name(func_node)
            if name is None:
                continue
            body = func_node.child_by_field_name("body")
            if body is None:
                continue
            # Index by simple (last) name component for easy lookup
            simple = name.rsplit("::", 1)[-1]
            out.setdefault(simple, (func_node, body, header_source))
    return out


def _project_root_for(path: Path) -> Path:
    resolved = path.resolve()
    for candidate in (resolved.parent, *resolved.parents):
        if (candidate / ".git").exists():
            return candidate
    return resolved.parent


def _lookup_helper_body(
    method_name: str, header_defs: dict[str, tuple[Node, Node, bytes]]
) -> bytes | None:
    """Return the inlinable expression of `method_name` if eligible.

    Eligible bodies:
        { return <expr>; }   →  <expr>
        { }                  →  (skipped — nothing to splice)

    Skips bodies with calls (other than primary expression operators), bodies
    with control flow, multi-statement bodies, and bodies referencing names
    the substitution can't safely rewrite.
    """
    found = header_defs.get(method_name)
    if found is None:
        return None
    func_node, body_node, header_source = found

    stmts = [c for c in body_node.named_children if c.type != "comment"]
    if len(stmts) != 1:
        return None
    stmt = stmts[0]
    if stmt.type != "return_statement":
        return None

    # Extract the return expression (skip the trailing `;`)
    # return_statement children: 'return', expression, ';'
    expr_node = None
    for child in stmt.named_children:
        if child.type != "comment":
            expr_node = child
            break
    if expr_node is None:
        return None

    # Reject if the expression contains other function calls — they
    # complicate substitution and rarely help (the call we're trying
    # to remove would just become other calls).
    for n in walk(expr_node):
        if n.type == "call_expression":
            return None

    expr_text = header_source[expr_node.start_byte:expr_node.end_byte]
    return expr_text.strip()


# ---------------------------------------------------------------------------
# Member substitution
# ---------------------------------------------------------------------------


def _substitute_members(
    body_expr: bytes, receiver_text: bytes, op_text: bytes
) -> bytes | None:
    """Rewrite bare `mFoo` references to `<receiver><op>mFoo`.

    For free functions (op_text == b""), no substitution is needed —
    just returns the body as-is.

    Returns None if the body has shape we can't safely substitute (e.g.
    contains `this->` references that we'd duplicate).
    """
    if op_text == b"":
        # Free function: no member substitution needed
        return body_expr

    # Reject `this->` references — substituting would yield `obj->this->...`
    if b"this" in body_expr:
        # Only reject if `this` appears as an identifier, not in a string
        # literal or comment. Conservative check:
        if re.search(rb"\bthis\b", body_expr):
            return None

    prefix = receiver_text + op_text

    def _repl(m: re.Match[bytes]) -> bytes:
        return prefix + m.group(0)

    return _MEMBER_RE.sub(_repl, body_expr)
