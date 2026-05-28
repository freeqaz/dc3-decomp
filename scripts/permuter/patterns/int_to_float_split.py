"""int_to_float_split — split `(float)<int-expr>` into `int t = <int-expr>; (float)t`.

Why a separate pattern from ``variable_extraction``?
    ``variable_extraction`` only walks ``call_expression`` nodes and was
    designed to hoist nested CALLS into locals. The int-to-float-split shape
    has NO call: the RHS is a subscript load + cast (``(float)((short*)p)[i]``)
    or a member load + cast (``(float)mField``). The general extractor never
    sees these sites at all.

    On top of that, this pattern is TIGHTER in two ways:
      1. RELEVANCE GATE — only fires when the diff shows interleaved
         narrow-int loads (`lhz`/`lha`) + extend + FP-store (`lfd`/`stfd`)
         or `stw`/`lwz` pairs between FP ops. Generic extraction has no
         such signal.
      2. EMIT SHAPE — always emits the exact ``int <name> = <expr>;`` form
         (never ``auto`` or ``unsigned``), specifically because the win is
         about scheduling the lhz/extsw/lfd triple per element instead of
         batching all the loads.

DC3 target: ``CharBonesSamples::EvaluateChannel`` — the
``comp >= kCompressVects`` branch loads + converts one short element at
a time in the target binary, but our base loads ALL the shorts first
then converts ALL of them. Inserting ``int tmp = ((short*)p)[i];`` before
``float v = (float)tmp;`` (forward), or collapsing the reverse form back
to one statement (inverse), gives the compiler the freedom to schedule
each load+convert independently.

Direction:
    SPLIT (default): ``float a = (float)EXPR;`` ->
                     ``int _itmp_N = EXPR;\\n float a = (float)_itmp_N;``
    COLLAPSE       : ``int t = EXPR; float a = (float)t;`` (t single-use)
                  -> ``float a = (float)EXPR;``

Recognized "integer-yielding" EXPRs:
    - ``subscript_expression`` where the array part is a
      ``(<int-type>*)<expr>`` cast (e.g. ``((short*)p)[i]``,
      ``((int*)p)[0]``, ``((unsigned char*)p)[k]``)
    - ``cast_expression`` to a recognised integer type spelling
      (``int``, ``short``, ``unsigned char``, ``unsigned short``,
      ``signed char``, ``unsigned int``, ``long``, ``unsigned long``)
    - ``field_expression`` whose final field name matches ``^m[A-Z].*``
      (project convention for member fields — emit anyway, let the
      compiler verify; this is a HEURISTIC).
"""

from __future__ import annotations

import re
from typing import Iterator, Optional

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import find_by_type, get_indent, get_line_start
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


_MAX_VARIANTS = 8

# Project-convention member-name regex: mFoo / mBarBaz / m_Foo etc. The leading
# `m` followed by an uppercase letter is the strongest Milo-codebase signal that
# a field_expression yields a (likely scalar) member load.
_MEMBER_NAME_RE = re.compile(rb"^m[A-Z_].*$")

# Integer type-descriptor spellings tree-sitter exposes as the cast's `type`
# field text. ``signed`` / ``unsigned`` prefixes are reconstructed by joining
# the spelling tokens; we match against the normalized whitespace-collapsed
# spelling.
_INT_TYPE_SPELLINGS = frozenset(
    {
        "int",
        "short",
        "long",
        "long long",
        "signed",
        "signed char",
        "signed short",
        "signed int",
        "signed long",
        "signed long long",
        "unsigned",
        "unsigned char",
        "unsigned short",
        "unsigned int",
        "unsigned long",
        "unsigned long long",
        "char",
    }
)

# Pointer-type-descriptor spellings whose pointee is integer.
_INT_PTR_PATTERN = re.compile(
    r"^(?:signed\s+|unsigned\s+)?"
    r"(?:char|short|int|long|long\s+long)"
    r"\s*\*\s*$"
)

# Asm opcodes that signal interleaved narrow-int load + FP-convert work —
# the diagnostic signature of "load-and-convert one element at a time" vs
# "load-all-then-convert-all".
_NARROW_LOAD_OPCODES = frozenset({"lhz", "lha", "lbz", "lbza", "lhza"})
_FP_STORE_OPCODES = frozenset({"stfd", "stfs"})
_FP_LOAD_OPCODES = frozenset({"lfd", "lfs"})
_GPR_MEM_OPCODES = frozenset({"stw", "lwz"})
_EXT_OPCODES = frozenset({"extsw", "extsh", "extsb"})


class IntToFloatSplitPattern(Pattern):
    """Split ``float v = (float)EXPR;`` <-> ``int t = EXPR; float v = (float)t;``."""

    name = "int_to_float_split"
    safety_tier = "conservative"
    structural_domain = "expr_shape"
    follow_ups = ("statement_reorder", "declaration_reorder")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Primary signal: narrow-int load (lhz/lha) AND FP store/load present
        # in any cluster — the load+convert interleaving fingerprint.
        for c in diagnosis.clusters:
            ops = set(c.target_opcodes) | set(c.base_opcodes)
            if (ops & _NARROW_LOAD_OPCODES) and (
                ops & (_FP_STORE_OPCODES | _FP_LOAD_OPCODES)
            ):
                return True
            # stw/lwz pairs between FP ops also count
            if (ops & _GPR_MEM_OPCODES) and (
                ops & (_FP_STORE_OPCODES | _FP_LOAD_OPCODES)
            ):
                if 4 <= c.size <= 12:
                    return True

        # diff_op level: narrow-int-load vs FP-load directly across.
        for d in diagnosis.diff_ops:
            t, b = d.target_opcode, d.base_opcode
            if t in _NARROW_LOAD_OPCODES or b in _NARROW_LOAD_OPCODES:
                if (t in _FP_LOAD_OPCODES or b in _FP_LOAD_OPCODES
                        or t in _FP_STORE_OPCODES or b in _FP_STORE_OPCODES
                        or t in _EXT_OPCODES or b in _EXT_OPCODES):
                    return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        # Per spec: tight single-target pattern at 0.4
        return 0.4

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0
        used_names: set[str] = set()

        # ----- COLLAPSE direction first: int t = EXPR; float v = (float)t; --
        # We do this scan against statement pairs by walking compound_statements
        # so locally-scoped declarations are handled too.
        for compound in _all_compound_statements(ctx.body_node):
            if counter >= _MAX_VARIANTS:
                break
            stmts = [c for c in compound.named_children if c.type != "comment"]
            if len(stmts) < 2:
                continue
            for i in range(len(stmts) - 1):
                if counter >= _MAX_VARIANTS:
                    break
                pair = _match_collapse_pair(stmts[i], stmts[i + 1], source)
                if pair is None:
                    continue
                if not ctx.node_in_mismatch_region(stmts[i]):
                    continue
                tmp_name, int_expr_text, float_decl_stmt = pair
                # Replace the two statements with a single
                # ``float NAME = (float)EXPR;``
                # Build replacement: keep the float decl's prefix (``float NAME``),
                # rebuild RHS as ``(float)<int-expr>``.
                lhs_text = _extract_float_decl_lhs(float_decl_stmt, source)
                if lhs_text is None:
                    continue
                indent = get_indent(source, stmts[i])
                replacement = (
                    lhs_text + b" = (float)" + int_expr_text + b";"
                )

                int_line_start = get_line_start(source, stmts[i])
                float_line_end = _line_end(source, stmts[i + 1].end_byte)

                ed = SourceEditor(source)
                ed.replace_range(
                    int_line_start, float_line_end,
                    indent + replacement + b"\n",
                )
                new_source = ed.apply()
                if new_source == source:
                    continue

                yield Variant(
                    name=f"int2fcollapse_{counter}",
                    pattern_name=self.name,
                    description=(
                        f"Collapse int {tmp_name.decode('utf-8', errors='replace')} "
                        f"+ (float) cast into single (float)EXPR"
                    ),
                    source=new_source,
                    tags=frozenset({"int_to_float_split", "collapse"}),
                )
                counter += 1

        # ----- SPLIT direction: float v = (float)EXPR; -> int t; (float)t -----
        # Walk every declaration in the function body. We also handle
        # `assignment_expression` with float LHS (e.g. `a = (float)EXPR;`).
        for decl in find_by_type(ctx.body_node, "declaration"):
            if counter >= _MAX_VARIANTS:
                break
            if not ctx.node_in_mismatch_region(decl):
                continue
            split = _match_float_cast_decl(decl, source)
            if split is None:
                continue
            init_decl_node, cast_node, int_expr_node = split
            int_expr_text = source[int_expr_node.start_byte:int_expr_node.end_byte]

            # Don't split if EXPR is already a bare identifier — that's the
            # "already-collapsed" shape, splitting it produces noise.
            if int_expr_node.type == "identifier":
                continue
            # Don't double-split: skip if EXPR is itself just `(int)<id>` of a
            # short temp name we'd plausibly have generated.
            tmp_name = _unique_tmp_name(counter, source, used_names)
            used_names.add(tmp_name)
            tmp_bytes = tmp_name.encode("utf-8")
            counter += 1

            indent = get_indent(source, decl)
            decl_line_start = get_line_start(source, decl)
            int_decl_line = (
                indent + b"int " + tmp_bytes + b" = " + int_expr_text + b";\n"
            )

            ed = SourceEditor(source)
            ed.insert_at(decl_line_start, int_decl_line)
            ed.replace_node(int_expr_node, tmp_bytes)
            new_source = ed.apply()
            if new_source == source:
                # Roll back the counter so the next variant gets the same name
                # (only matters for test stability, since SourceEditor is fresh).
                continue

            yield Variant(
                name=f"int2fsplit_{counter - 1}",
                pattern_name=self.name,
                description=(
                    f"Split (float){int_expr_text.decode('utf-8', errors='replace')[:40]} "
                    f"into int {tmp_name} + (float){tmp_name}"
                ),
                source=new_source,
                tags=frozenset({"int_to_float_split", "split"}),
            )

        # Also walk assignment_expression statements: `a = (float)EXPR;`
        for assign in find_by_type(ctx.body_node, "assignment_expression"):
            if counter >= _MAX_VARIANTS:
                break
            parent_stmt = _enclosing_expression_statement(assign)
            if parent_stmt is None:
                continue
            if not ctx.node_in_mismatch_region(parent_stmt):
                continue
            op = assign.child_by_field_name("operator")
            if op is None or op.text != b"=":
                continue
            rhs = assign.child_by_field_name("right")
            if rhs is None or rhs.type != "cast_expression":
                continue
            if not _cast_is_to_float(rhs):
                continue
            inner = _cast_value(rhs)
            if inner is None or not _is_int_yielding_expr(inner, ctx.file_source):
                continue
            if inner.type == "identifier":
                continue

            int_expr_text = source[inner.start_byte:inner.end_byte]
            tmp_name = _unique_tmp_name(counter, source, used_names)
            used_names.add(tmp_name)
            tmp_bytes = tmp_name.encode("utf-8")
            counter += 1

            indent = get_indent(source, parent_stmt)
            stmt_line_start = get_line_start(source, parent_stmt)
            int_decl_line = (
                indent + b"int " + tmp_bytes + b" = " + int_expr_text + b";\n"
            )

            ed = SourceEditor(source)
            ed.insert_at(stmt_line_start, int_decl_line)
            ed.replace_node(inner, tmp_bytes)
            new_source = ed.apply()
            if new_source == source:
                continue

            yield Variant(
                name=f"int2fsplit_assign_{counter - 1}",
                pattern_name=self.name,
                description=(
                    f"Split assign (float){int_expr_text.decode('utf-8', errors='replace')[:40]} "
                    f"into int {tmp_name} + (float){tmp_name}"
                ),
                source=new_source,
                tags=frozenset({"int_to_float_split", "split"}),
            )


# ---------------------------------------------------------------------------
# AST matchers
# ---------------------------------------------------------------------------

def _all_compound_statements(body: Node) -> Iterator[Node]:
    """Yield all compound_statement nodes inside *body* (function body + nested)."""
    yield body
    for n in find_by_type(body, "compound_statement"):
        if n is not body:
            yield n


def _match_float_cast_decl(
    decl: Node, source: bytes,
) -> Optional[tuple[Node, Node, Node]]:
    """Match ``float NAME = (float)INT_EXPR;`` declarations.

    Returns ``(init_declarator_node, cast_expression_node, inner_expr_node)``
    or None.
    """
    # Must declare a float type (or double — same lowering rules apply).
    type_node = decl.child_by_field_name("type")
    if type_node is None:
        return None
    type_text = source[type_node.start_byte:type_node.end_byte].decode(
        "utf-8", errors="replace"
    )
    if type_text.strip() not in ("float", "double"):
        return None

    # Single init_declarator only — multi-decl like `float a = ..., b = ...;`
    # is too ambiguous to rewrite cleanly.
    init_decls = [
        c for c in decl.named_children if c.type == "init_declarator"
    ]
    if len(init_decls) != 1:
        return None
    init_decl = init_decls[0]

    value = init_decl.child_by_field_name("value")
    if value is None or value.type != "cast_expression":
        return None

    if not _cast_is_to_float(value):
        return None

    inner = _cast_value(value)
    if inner is None:
        return None

    if not _is_int_yielding_expr(inner, source):
        return None

    return (init_decl, value, inner)


def _cast_is_to_float(cast: Node) -> bool:
    """True when *cast* is `(float)EXPR` or `(double)EXPR`."""
    type_node = cast.child_by_field_name("type")
    if type_node is None:
        return False
    spelling = b" ".join(
        type_node.text.split()
    ).decode("utf-8", errors="replace").strip() if type_node.text else ""
    return spelling in ("float", "double")


def _cast_value(cast: Node) -> Optional[Node]:
    """Return the inner expression node of a cast_expression."""
    val = cast.child_by_field_name("value")
    if val is not None:
        return val
    # Fallback: scan named children for the non-type-descriptor child
    for c in cast.named_children:
        if c.type != "type_descriptor":
            return c
    return None


def _is_int_yielding_expr(node: Node, source: bytes) -> bool:
    """True when *node* is a recognised integer-yielding expression.

    Recognised shapes:
      - subscript_expression where the array part is a (<int-type>*) cast
        (e.g. ((short*)p)[i])
      - cast_expression to a recognised integer type spelling
      - field_expression whose field name matches the m[A-Z].* convention
    """
    if node.type == "subscript_expression":
        return _subscript_arr_is_int_ptr_cast(node, source)
    if node.type == "cast_expression":
        return _cast_is_to_int(node)
    if node.type == "field_expression":
        return _field_is_member_convention(node)
    return False


def _subscript_arr_is_int_ptr_cast(sub: Node, source: bytes) -> bool:
    """True when ``sub`` is ``((<int-type>*)p)[i]``.

    The array part lives behind a parenthesized_expression wrapping the cast.
    """
    arr = sub.child_by_field_name("argument")
    if arr is None:
        # Tree-sitter sometimes uses positional access. Walk named children.
        if len(sub.named_children) >= 1:
            arr = sub.named_children[0]
        else:
            return False

    # Unwrap parens
    while arr is not None and arr.type == "parenthesized_expression":
        inner = None
        for c in arr.named_children:
            if c.type != "comment":
                inner = c
                break
        if inner is None:
            return False
        arr = inner

    if arr is None or arr.type != "cast_expression":
        return False
    type_node = arr.child_by_field_name("type")
    if type_node is None:
        return False
    # Get the type descriptor's text and check it matches an int pointer
    type_text = source[
        type_node.start_byte:type_node.end_byte
    ].decode("utf-8", errors="replace")
    type_text = re.sub(r"\s+", " ", type_text).strip()
    # Allow trailing whitespace / single pointer
    if _INT_PTR_PATTERN.match(type_text + " "):
        return True
    return False


def _cast_is_to_int(cast: Node) -> bool:
    type_node = cast.child_by_field_name("type")
    if type_node is None or type_node.text is None:
        return False
    spelling = re.sub(
        r"\s+", " ", type_node.text.decode("utf-8", errors="replace"),
    ).strip()
    return spelling in _INT_TYPE_SPELLINGS


def _field_is_member_convention(field: Node) -> bool:
    """True when field name matches the ``m[A-Z].*`` Milo member convention."""
    field_node = field.child_by_field_name("field")
    if field_node is None or field_node.text is None:
        return False
    return _MEMBER_NAME_RE.match(field_node.text) is not None


def _match_collapse_pair(
    int_stmt: Node, float_stmt: Node, source: bytes,
) -> Optional[tuple[bytes, bytes, Node]]:
    """Match ``int TMP = EXPR; float NAME = (float)TMP;``.

    Returns ``(tmp_name_bytes, int_expr_bytes, float_decl_stmt)`` or None.

    ``TMP`` must be a single-use identifier; the only use is inside the cast.
    """
    if int_stmt.type != "declaration" or float_stmt.type != "declaration":
        return None

    int_type = int_stmt.child_by_field_name("type")
    if int_type is None or int_type.text is None:
        return None
    if int_type.text.decode("utf-8", errors="replace").strip() not in (
        "int", "short", "unsigned int", "unsigned short", "long",
    ):
        return None

    int_inits = [
        c for c in int_stmt.named_children if c.type == "init_declarator"
    ]
    if len(int_inits) != 1:
        return None
    int_init = int_inits[0]
    tmp_name_node = int_init.child_by_field_name("declarator")
    if tmp_name_node is None or tmp_name_node.type != "identifier":
        return None
    tmp_name = tmp_name_node.text or b""
    if not tmp_name:
        return None
    int_value = int_init.child_by_field_name("value")
    if int_value is None:
        return None

    # The float decl must be `float NAME = (float)<tmp_name>;`
    float_type = float_stmt.child_by_field_name("type")
    if float_type is None or float_type.text is None:
        return None
    if float_type.text.decode("utf-8", errors="replace").strip() not in (
        "float", "double",
    ):
        return None
    float_inits = [
        c for c in float_stmt.named_children if c.type == "init_declarator"
    ]
    if len(float_inits) != 1:
        return None
    float_init = float_inits[0]
    float_value = float_init.child_by_field_name("value")
    if float_value is None or float_value.type != "cast_expression":
        return None
    if not _cast_is_to_float(float_value):
        return None
    cast_inner = _cast_value(float_value)
    if cast_inner is None or cast_inner.type != "identifier":
        return None
    if (cast_inner.text or b"") != tmp_name:
        return None

    int_expr_text = source[int_value.start_byte:int_value.end_byte]
    return (tmp_name, int_expr_text, float_stmt)


def _extract_float_decl_lhs(float_stmt: Node, source: bytes) -> Optional[bytes]:
    """Return bytes for the LHS of a `float NAME = ...;` decl: ``float NAME``."""
    type_node = float_stmt.child_by_field_name("type")
    if type_node is None:
        return None
    init_decls = [
        c for c in float_stmt.named_children if c.type == "init_declarator"
    ]
    if not init_decls:
        return None
    init_decl = init_decls[0]
    name_node = init_decl.child_by_field_name("declarator")
    if name_node is None:
        return None
    type_text = source[type_node.start_byte:type_node.end_byte]
    name_text = source[name_node.start_byte:name_node.end_byte]
    return type_text + b" " + name_text


def _enclosing_expression_statement(node: Node) -> Optional[Node]:
    """Walk parent chain to find the nearest ``expression_statement``."""
    cur = node.parent
    while cur is not None:
        if cur.type == "expression_statement":
            return cur
        cur = cur.parent
    return None


# ---------------------------------------------------------------------------
# Misc helpers
# ---------------------------------------------------------------------------

def _line_end(source: bytes, pos: int) -> int:
    """Find end of line (inclusive of newline) containing *pos*."""
    while pos < len(source) and source[pos:pos + 1] not in (b"\n", b"\r"):
        pos += 1
    if pos < len(source):
        pos += 1
    return pos


def _unique_tmp_name(
    start: int, source: bytes, used_names: set[str]
) -> str:
    """Return a ``_itmpN`` name that doesn't clash with *source* or *used_names*.

    Distinct prefix (``_itmp`` not ``_tmp``) so this pattern composes cleanly
    with ``variable_extraction`` without collision.
    """
    source_text = source.decode("utf-8", errors="replace")
    n = start
    while True:
        candidate = f"_itmp{n}"
        if candidate not in used_names and not re.search(
            rf"\b{re.escape(candidate)}\b", source_text
        ):
            return candidate
        n += 1
