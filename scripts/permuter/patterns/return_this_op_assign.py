"""Append `return *this;` to ref-returning `operator=` whose body lacks it.

Win rate: untested as an automated pattern. Manually proven in FileMerger::Merger::operator=
(64.6% -> 100%, commit f1998277) and confirmed at 6 in-tree candidate sites
(Gem.cpp, MatAnim.cpp, Mesh.cpp, DateTime.cpp, Time.cpp x2).

Why: When `T& Class::operator=(...)` lacks a final `return *this;`, MWCC's
ABI doesn't constrain `this` to live in r3 across the body. The register
allocator then steals r3 for temporaries, and `this`-comparisons frequently
land in r4 — cascading r3<->r4 swaps everywhere a member is touched.

Adding `return *this;` re-pins `this` to r3 at exit and the cascade
collapses. The transformation is semantically benign on its own — the
missing return is undefined behavior in C++ (UB by [stmt.return]/2 when the
return type is non-void).

Detection:
    AST  : function_definition whose declarator is a reference_declarator
           (so the return type is `T&`), the function name ends with
           `operator=`, and the body's last meaningful statement is NOT a
           return statement.
    ASM  : conservative — always relevant when the symbol's demangled form
           matches operator=, or when the diff has cmp/mr/addi with r3/r4
           reshuffling. Edit is cheap and safe; better to attempt it than
           gate too tight.

Edit: insert `return *this;` immediately before the closing `}` of the body,
indented to match neighbouring statements.
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Symbol-name hints. MWCC mangles `operator=` as `__as` (assignment-of).
# Mangled examples:
#   __as__3GemFRC3Gem            -> Gem::operator=(const Gem&)
#   __as__Q23Hmx4TimeFRCQ23Hmx4Time
_MANGLED_OP_ASSIGN_RE = re.compile(r"__as__")
_DEMANGLED_OP_ASSIGN_RE = re.compile(r"operator\s*=\s*\(")


class ReturnThisOpAssignPattern(Pattern):
    name = "return_this_op_assign"
    safety_tier = "safe"
    structural_domain = "control_flow"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Liberal gate: any r3/r4 movement is a hint; the edit is cheap so
        # we'd rather try it than miss it. The AST gate in generate() does
        # the heavy filtering — most functions won't even be operator=.
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("mr", "addi", "cmpw", "cmplw", "cmpwi", "cmplwi"):
                return True
            if d.base_opcode in ("mr", "addi", "cmpw", "cmplw", "cmpwi", "cmplwi"):
                return True
        # Any reg-swap is plausibly an r3-pinning cascade
        if diagnosis.reg_swap_pairs:
            return True
        # Otherwise: still cheap; allow when there are any replaces
        return diagnosis.replace_real > 0 or diagnosis.replace_noise > 0

    def context_priority(
        self, diagnosis: Diagnosis, ctx: FunctionContext
    ) -> float:
        # If the symbol screams "operator=", boost priority hard regardless
        # of asm signals — the AST gate is the real test.
        if ctx.symbol and _MANGLED_OP_ASSIGN_RE.search(ctx.symbol):
            return 0.9
        return self.priority(diagnosis)

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Modest priority — only one variant per call, but tightly targeted.
        return 0.4

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        func_node = ctx.func_node
        body_node = ctx.body_node

        # 1) Return type must be a reference: top declarator is reference_declarator.
        if not _has_reference_return(func_node):
            return

        # 2) Function name must end in `operator=`.
        if not _is_op_assign(func_node, source):
            return

        # 3) Skip destructors / copy-ctors defensively (they wouldn't have
        # ref-return + operator= name, but be safe).
        # (No-op — _is_op_assign already requires operator=.)

        # 4) Body's last meaningful statement must NOT be a return.
        last_stmt = _last_meaningful_stmt(body_node)
        if last_stmt is None:
            return
        if last_stmt.type == "return_statement":
            return

        # 4b) Reject the body if it *already contains* any return statement.
        # Cases like `if (...) return *this; else { ...; return *this; }`
        # technically end with an if_statement, but every path returns —
        # appending another `return *this;` after the brace would be dead
        # code and would not move codegen. Scan the whole body subtree.
        if _has_any_return_statement(body_node):
            return

        # 5) Build the insertion: `return *this;` immediately before the
        # closing `}` of body_node. We must handle two body shapes:
        #   (a) multi-line body — `}` sits on its own line; insert a fresh
        #       indented line above it.
        #   (b) one-line body  — `{ stmts; }` on a single line; insert
        #       ` return *this; ` immediately before the `}` (no leading
        #       newline so we don't break the line above).
        closing_brace_offset = body_node.end_byte - 1  # the `}` byte
        if source[closing_brace_offset:closing_brace_offset + 1] != b"}":
            # Body didn't end in `}` (unparseable) — abort.
            return

        # Locate the start of the `}`'s line; check whether anything
        # non-whitespace precedes it on that line. That tells us if we're
        # in the one-line body case.
        line_start = closing_brace_offset
        while line_start > 0 and source[line_start - 1:line_start] != b"\n":
            line_start -= 1
        between = source[line_start:closing_brace_offset]
        brace_is_on_own_line = between.strip() == b""

        ed = SourceEditor(source)
        if brace_is_on_own_line:
            # Multi-line body: insert a new indented statement line above `}`
            indent = _body_statement_indent(body_node, source, last_stmt)
            insertion = indent + b"return *this;\n"
            ed.insert_at(line_start, insertion)
        else:
            # One-line body: insert space-padded statement right before `}`
            # so we stay on the same line. Preserve any pre-existing space.
            pad_left = b"" if (
                source[closing_brace_offset - 1:closing_brace_offset] == b" "
            ) else b" "
            insertion = pad_left + b"return *this; "
            ed.insert_at(closing_brace_offset, insertion)
        try:
            new_source = ed.apply()
        except ValueError:
            return

        yield Variant(
            name="ret_this_op_assign",
            pattern_name=self.name,
            description="Append `return *this;` to ref-returning operator= missing it",
            source=new_source,
            tags=frozenset({"control_flow", "abi_return"}),
        )


# --- helpers ---------------------------------------------------------------


def _has_reference_return(func_node: Node) -> bool:
    """True iff this function_definition's declarator is wrapped in a
    reference_declarator (i.e. return type is `T&`).
    """
    declarator = func_node.child_by_field_name("declarator")
    if declarator is None:
        return False
    return declarator.type == "reference_declarator"


def _is_op_assign(func_node: Node, source: bytes) -> bool:
    """True iff the function's declared name ends with `operator=`.

    Walks the declarator chain to find the name node. Matches:
      - `operator=` (plain)
      - `Class::operator=` (qualified)
      - `Ns::Class::operator=` (deeply qualified)

    Does NOT match `operator==`, `operator!=`, `operator<=`, `operator>=`,
    or `operator=>` (not C++) — we require the name to END in a single `=`
    not preceded by another operator character.
    """
    declarator = func_node.child_by_field_name("declarator")
    if declarator is None:
        return False

    # Unwrap reference / pointer wrappers
    while declarator.type in ("reference_declarator", "pointer_declarator"):
        inner = declarator.child_by_field_name("declarator")
        if inner is None:
            for c in declarator.named_children:
                if c.type in (
                    "function_declarator", "pointer_declarator",
                    "reference_declarator",
                ):
                    inner = c
                    break
        declarator = inner
        if declarator is None:
            return False

    if declarator.type != "function_declarator":
        return False

    name_node = declarator.child_by_field_name("declarator")
    if name_node is None:
        return False

    if name_node.text is None:
        return False
    name_bytes = name_node.text
    # Tree-sitter operator_name nodes carry text like "operator=", and
    # qualified_identifier carries text like "Foo::operator=". Strip
    # whitespace defensively.
    name = name_bytes.decode("utf-8", errors="replace").strip()
    # Must END in `operator=` (exactly), and NOT be operator==, !=, <=, >=, etc.
    if not name.endswith("operator="):
        return False
    # Defensive: reject if there's another comparison char right before the `=`.
    # `operator==` ends in `=` too but doesn't end in `operator=`.
    # Already excluded by the endswith check above (compound operators
    # have a different name body). Belt-and-braces: also reject if the
    # demangled symbol contains `operator==`, `operator!=`, etc.
    return True


def _has_any_return_statement(body_node: Node) -> bool:
    """True iff any descendant of body_node is a return_statement."""
    stack = [body_node]
    while stack:
        n = stack.pop()
        if n.type == "return_statement":
            return True
        stack.extend(n.children)
    return False


def _last_meaningful_stmt(body_node: Node) -> Node | None:
    """Return the last non-comment named child of the body, or None."""
    if body_node is None:
        return None
    for child in reversed(body_node.named_children):
        if child.type == "comment":
            continue
        return child
    return None


def _body_statement_indent(body_node: Node, source: bytes, last_stmt: Node) -> bytes:
    """Best-effort indent for the inserted return statement.

    Prefer the indent of the last existing statement (so the new return
    lines up with its siblings). Fall back to the indent of the body's
    opening brace + 4 spaces.
    """
    if last_stmt is not None:
        return get_indent(source, last_stmt)
    # Fallback: indent of the `{` + 4 spaces
    brace_indent = get_indent(source, body_node)
    return brace_indent + b"    "
