"""common_tail_goto_to_duplicate — rewrite a single forward goto whose target
sits in a sibling `else` branch by duplicating the shared tail.

Recognizes the decomp idiom where a deeply-nested inner-if's success path
jumps INTO an else clause to share its tail code, leaving the nested
inner-if's drop-through path to fall to the outer-if's end:

    if (outerCond) {
        ...
        if (innerCond) {
            ...
            goto SHARED_TAIL;       // jumps into the sibling else
        }
        BRANCH_A_TAIL_DROP;         // executes when innerCond is false
    } else {
    SHARED_TAIL:
        SHARED_TAIL_BODY;
    }

Rewrites by duplicating SHARED_TAIL_BODY into the inner-if's true branch and
turning BRANCH_A_TAIL_DROP into the inner-if's else branch:

    if (outerCond) {
        ...
        if (innerCond) {
            ...
            SHARED_TAIL_BODY;       // duplicated
        } else {
            BRANCH_A_TAIL_DROP;     // moved out of dead drop-through
        }
    } else {
        SHARED_TAIL_BODY;           // unchanged
    }

The pattern fires when:
  * An if-statement has BOTH a consequence (the outer body) AND an alternative
    (the else body) at the same scope.
  * The else body's FIRST statement is a `labeled_statement` whose label has
    EXACTLY ONE incoming goto in the function.
  * That one goto sits inside the outer if's body (the true branch), wrapped
    by some nested-if chain (the goto's parent if has no else).
  * Inside the immediate parent if of the goto, the goto is the LAST statement
    (i.e., the goto-bearing if is `if (innerCond) { ...; goto L; }`).
  * The outer-if's body contains the inner-if followed by an optional
    BRANCH_A_TAIL_DROP run of non-control-flow statements (no further gotos /
    labels / returns).

Use case: Award::Configure (the canonical real example) and similar functions
where MWCC's codegen folded one branch's tail into another branch's tail via
a goto-into-else.
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..control_flow import noncomment_named_children
from ..types import Diagnosis, FunctionContext, Variant


class CommonTailGotoToDuplicate(Pattern):
    name = "common_tail_goto_to_duplicate"
    # OPT-IN: duplicating the shared tail body produces extra basic blocks the
    # original goto-fold avoided. Empirically regresses 100% functions —
    # RGTrainerPanel::HandleChordLegend 100->89, OvershellPanel::ResolveSlotStates
    # 100->67. Useful for research, NOT for cleanup at 100% baseline.
    opt_in = True
    safety_tier = "conservative"
    structural_domain = "control_flow"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        return True

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0
        seen_labels: set[bytes] = set()

        # Walk all if-statements in the function body. For each one with both
        # a consequence and an alternative, check whether the else-clause leads
        # with a labeled_statement and whether a single goto from inside the
        # if's true branch targets that label.
        for if_stmt in _walk_if_statements(ctx.body_node):
            alternative = if_stmt.child_by_field_name("alternative")
            consequence = if_stmt.child_by_field_name("consequence")
            if alternative is None or consequence is None:
                continue
            if consequence.type != "compound_statement":
                continue
            # alternative is an else_clause; pull its body (must be compound)
            else_body = _else_body_compound(alternative)
            if else_body is None:
                continue

            else_stmts = noncomment_named_children(else_body)
            if not else_stmts:
                continue
            first = else_stmts[0]
            if first.type != "labeled_statement":
                continue

            lbl_node = first.child_by_field_name("label")
            if lbl_node is None:
                continue
            label_name = source[lbl_node.start_byte:lbl_node.end_byte].strip()
            if label_name in seen_labels:
                continue

            # Verify exactly one incoming goto for this label.
            gotos = list(_find_gotos_to(label_name, ctx.body_node, source))
            if len(gotos) != 1:
                continue
            goto = gotos[0]

            # Verify the goto is inside the consequence (true branch).
            if not _is_descendant(goto, consequence):
                continue

            # Find the innermost if-statement containing the goto. The goto must
            # be the LAST statement of that if's body.
            inner_if = _enclosing_if_with_last_goto(goto, consequence)
            if inner_if is None:
                continue
            # The inner-if must have no alternative (no else clause already).
            if inner_if.child_by_field_name("alternative") is not None:
                continue
            inner_body = inner_if.child_by_field_name("consequence")
            if inner_body is None or inner_body.type != "compound_statement":
                continue

            # Locate the inner-if within its enclosing compound and gather the
            # BRANCH_A_TAIL_DROP statements: any statements between the inner-if
            # and the end of THAT compound. They must be free of top-level
            # control flow (goto/return/label) so we can safely move them into
            # the inner-if's else clause.
            inner_parent_compound, inner_idx = _parent_compound_index(inner_if, consequence)
            if inner_parent_compound is None or inner_idx is None:
                # The inner-if is itself nested inside another scope — refuse.
                continue
            # The enclosing compound must lie within the outer-if's body (we
            # already verified the goto descends from `consequence`); ensure
            # the inner-if's parent compound is `consequence` itself or a
            # descendant of it.
            if inner_parent_compound.id != consequence.id and not _is_descendant(inner_parent_compound, consequence):
                continue

            parent_stmts = noncomment_named_children(inner_parent_compound)
            branch_a_tail_nodes = parent_stmts[inner_idx + 1:]
            if _has_top_level_exit(branch_a_tail_nodes):
                continue

            # Build SHARED_TAIL_BODY text: the body of the labeled_statement's
            # inner statement plus any siblings AFTER the labeled_statement in
            # the else body. Refuse if those siblings contain top-level exits.
            label_body = _labeled_body_node(first)
            label_extras = else_stmts[1:]
            if _has_top_level_exit(label_extras):
                continue
            shared_tail_pieces: list[Node] = []
            if label_body is not None and _is_meaningful(label_body, source):
                shared_tail_pieces.append(label_body)
            shared_tail_pieces.extend(label_extras)
            if not shared_tail_pieces:
                continue

            yield from _emit_variant(
                ctx=ctx,
                source=source,
                inner_if=inner_if,
                inner_body=inner_body,
                goto=goto,
                shared_tail_pieces=shared_tail_pieces,
                branch_a_tail_nodes=branch_a_tail_nodes,
                else_body=else_body,
                else_stmts=else_stmts,
                first_label_stmt=first,
                counter=counter,
                label_name=label_name,
            )
            counter += 1
            seen_labels.add(label_name)


# ---------------------------------------------------------------------------
# Recognizer helpers
# ---------------------------------------------------------------------------

def _walk_if_statements(root: Node) -> Iterator[Node]:
    stack: list[Node] = [root]
    while stack:
        n = stack.pop()
        if n.type == "if_statement":
            yield n
        stack.extend(n.children)


def _find_gotos_to(label: bytes, root: Node, source: bytes) -> Iterator[Node]:
    stack: list[Node] = [root]
    while stack:
        n = stack.pop()
        if n.type == "goto_statement":
            lbl = n.child_by_field_name("label")
            if lbl is not None and source[lbl.start_byte:lbl.end_byte].strip() == label:
                yield n
        stack.extend(n.children)


def _is_descendant(node: Node, ancestor: Node) -> bool:
    cur = node.parent
    while cur is not None:
        if cur.id == ancestor.id:
            return True
        cur = cur.parent
    return False


def _else_body_compound(alternative: Node) -> Node | None:
    """Return the compound_statement directly inside an else clause.

    Tree-sitter shapes the else-clause as a sibling: the `alternative` field of
    an if_statement is either an `else_clause` node wrapping a compound, or the
    raw compound itself depending on grammar version. Handles both.
    """
    if alternative.type == "compound_statement":
        return alternative
    # else_clause: contains `else` keyword and a body
    for child in alternative.children:
        if child.type == "compound_statement":
            return child
        if child.type == "if_statement":
            # An else-if chain — refuse.
            return None
    return None


def _enclosing_if_with_last_goto(goto: Node, scope: Node) -> Node | None:
    """Return the innermost `if_statement` ancestor of `goto` (within `scope`)
    whose body contains the goto as its LAST same-level statement. Returns
    None if the goto is not at the tail of any enclosing if.
    """
    parent = goto.parent
    if parent is None:
        return None
    # Two shapes: goto is direct consequence of an if (rare in MWCC decomp), or
    # goto is the last named child of a compound_statement that is the
    # consequence of an if.
    if parent.type == "if_statement":
        if not _is_descendant(parent, scope):
            return None
        return parent
    if parent.type == "compound_statement":
        siblings = noncomment_named_children(parent)
        if not siblings or siblings[-1].id != goto.id:
            return None
        grand = parent.parent
        if grand is None or grand.type != "if_statement":
            return None
        if not _is_descendant(grand, scope):
            return None
        return grand
    return None


def _parent_compound_index(node: Node, scope: Node) -> tuple[Node | None, int | None]:
    """Find the compound_statement that directly contains `node` and return
    (compound, index_of_node). Returns (None, None) if not contained in any
    compound_statement, or if the compound is outside `scope`.
    """
    parent = node.parent
    if parent is None or parent.type != "compound_statement":
        return None, None
    siblings = noncomment_named_children(parent)
    for i, s in enumerate(siblings):
        if s.id == node.id:
            return parent, i
    return None, None


def _labeled_body_node(labeled: Node) -> Node | None:
    seen_colon = False
    for child in labeled.children:
        if child.type == ":":
            seen_colon = True
            continue
        if seen_colon and child.is_named:
            return child
    return None


def _is_meaningful(body: Node, source: bytes) -> bool:
    if body.type == "expression_statement":
        text = source[body.start_byte:body.end_byte].strip()
        if text == b";":
            return False
    return True


def _has_top_level_exit(stmts: list[Node]) -> bool:
    for s in stmts:
        if s.type in ("goto_statement", "return_statement", "labeled_statement"):
            return True
    return False


def _line_indent(source: bytes, byte_off: int) -> bytes:
    start = source.rfind(b"\n", 0, byte_off)
    start = 0 if start < 0 else start + 1
    out = []
    for b in source[start:byte_off]:
        if b in (0x20, 0x09):
            out.append(b)
        else:
            break
    return bytes(out)


def _line_col(source: bytes, byte_off: int) -> int:
    start = source.rfind(b"\n", 0, byte_off)
    start = 0 if start < 0 else start + 1
    col = 0
    for b in source[start:byte_off]:
        if b == 0x20:
            col += 1
        elif b == 0x09:
            col += 8 - (col % 8)
        else:
            break
    return col


def _reindent(chunk: bytes, indent: bytes, base_col: int) -> bytes:
    lines = chunk.split(b"\n")
    if not lines:
        return chunk
    out_lines: list[bytes] = [indent + lines[0].lstrip()]
    for line in lines[1:]:
        if not line.strip():
            out_lines.append(b"")
            continue
        col = 0
        i = 0
        while i < len(line) and col < base_col:
            b = line[i]
            if b == 0x20:
                col += 1
            elif b == 0x09:
                col += 8 - (col % 8)
            else:
                break
            i += 1
        out_lines.append(indent + line[i:])
    return b"\n".join(out_lines)


# ---------------------------------------------------------------------------
# Emitter
# ---------------------------------------------------------------------------

def _emit_variant(
    *,
    ctx: FunctionContext,
    source: bytes,
    inner_if: Node,
    inner_body: Node,
    goto: Node,
    shared_tail_pieces: list[Node],
    branch_a_tail_nodes: list[Node],
    else_body: Node,
    else_stmts: list[Node],
    first_label_stmt: Node,
    counter: int,
    label_name: bytes,
) -> Iterator[Variant]:
    """Produce the duplicated-tail rewrite as a set of byte-range edits.

    Three edits, applied in reverse byte order:

      1. Strip the leading `LABEL:` prefix from the first stmt in the else
         body so the else reads cleanly (we keep the inner statement body
         and any subsequent else-body siblings unchanged).
      2. Drop the BRANCH_A_TAIL_DROP stmts from the outer-if's body — they
         become the new else of the inner-if.
      3. Replace the goto-bearing if's body so it ends with the duplicated
         SHARED_TAIL_BODY instead of the goto, AND extend the inner-if with
         an `else { BRANCH_A_TAIL_DROP }` clause.
    """
    inner_indent = _line_indent(source, inner_if.start_byte)
    # Use the goto's own line indent as the body indent — that matches the
    # actual indentation style at the goto's nesting level (e.g. preserves
    # tabs/spaces conventions of the source).
    inner_block_indent = _line_indent(source, goto.start_byte)

    # Build text for the SHARED_TAIL_BODY at inner_block_indent.
    shared_tail_chunks: list[bytes] = []
    for n in shared_tail_pieces:
        base_col = _line_col(source, n.start_byte)
        shared_tail_chunks.append(
            _reindent(source[n.start_byte:n.end_byte], inner_block_indent, base_col)
        )
    shared_tail_text = b"\n".join(shared_tail_chunks)
    # The goto's source line already starts at inner_block_indent (its leading
    # whitespace appears BEFORE goto.start_byte, which is the byte boundary we
    # rewrite from). Strip the leading indent on the first line so we don't
    # double-indent it.
    if shared_tail_text.startswith(inner_block_indent):
        shared_tail_text = shared_tail_text[len(inner_block_indent):]

    # Build text for the BRANCH_A_TAIL_DROP if non-empty, indented at inner_block_indent.
    if branch_a_tail_nodes:
        drop_chunks: list[bytes] = []
        for n in branch_a_tail_nodes:
            base_col = _line_col(source, n.start_byte)
            drop_chunks.append(
                _reindent(source[n.start_byte:n.end_byte], inner_block_indent, base_col)
            )
        drop_text = b"\n".join(drop_chunks)
    else:
        drop_text = None

    # ---- Edit (3): rewrite the inner-if and absorb the BRANCH_A_TAIL_DROP. ----
    # Inner body's existing statements (before the trailing goto) stay; we
    # replace just the goto with the shared_tail_text, AND we append an else
    # clause containing the BRANCH_A_TAIL_DROP if any.
    inner_body_stmts = noncomment_named_children(inner_body)
    # The goto must be the last named child of inner_body.
    if not inner_body_stmts or inner_body_stmts[-1].id != goto.id:
        return

    # Goto replacement: rewrite from the goto's start to the closing `}` of
    # the inner_body, then possibly add an else clause.
    inner_close = inner_body.end_byte  # one past the `}` of the inner body

    # Construct the new inner-if's body and optional else clause.
    new_inner_tail = (
        shared_tail_text + b"\n"
        + inner_indent + b"}"
    )
    if drop_text is not None:
        new_inner_tail += b" else {\n" + drop_text + b"\n" + inner_indent + b"}"

    # Build extension range and replacement.
    # Edit: from goto.start_byte to inner_if.end_byte
    inner_if_end = inner_if.end_byte
    edit3_range = (goto.start_byte, inner_if_end)
    edit3_replacement = new_inner_tail

    # ---- Edit (2): drop BRANCH_A_TAIL_DROP from the outer-if's body. ----
    # branch_a_tail_nodes (if any) sit immediately after inner_if in the outer
    # body. Remove them: byte range from inner_if.end_byte to the last drop
    # node's end_byte. We'll preserve the line break and indentation between
    # them naturally by replacing with an empty string.
    if branch_a_tail_nodes:
        # Include leading whitespace before the first drop node so we don't
        # leave an empty indented line.
        first_drop = branch_a_tail_nodes[0]
        last_drop = branch_a_tail_nodes[-1]
        # Trim leading whitespace back to a newline so we collapse the line.
        cut_start = first_drop.start_byte
        # Walk backward through whitespace to (and including) the preceding `\n`.
        while cut_start > 0 and source[cut_start - 1] in (0x20, 0x09):
            cut_start -= 1
        if cut_start > 0 and source[cut_start - 1] == 0x0A:
            cut_start -= 1
        edit2_range = (cut_start, last_drop.end_byte)
        edit2_replacement = b""
    else:
        edit2_range = None
        edit2_replacement = None

    # ---- Edit (1): strip the `LABEL:` prefix from the else body. ----
    # The else body's first stmt is the labeled_statement. We rewrite from
    # the START of the line containing the label to the byte AFTER the colon
    # plus any trailing whitespace/newline before the labeled inner body, so
    # the resulting else opens cleanly into its first real statement.
    label_prefix_end = None
    for child in first_label_stmt.children:
        if child.type == ":":
            label_prefix_end = child.end_byte
            break
    if label_prefix_end is None:
        return
    # Walk back from first_label_stmt.start_byte past any leading whitespace on
    # the same line so we drop the entire `LABEL:` line.
    line_start = source.rfind(b"\n", 0, first_label_stmt.start_byte)
    line_start = 0 if line_start < 0 else line_start + 1
    # Only drop the line if everything between line_start and label_prefix_end
    # is whitespace + statement_identifier + ":".
    line_prefix = source[line_start:first_label_stmt.start_byte]
    if line_prefix.strip() == b"":
        # Also consume the trailing newline so the next line collapses up.
        cut_end = label_prefix_end
        if cut_end < len(source) and source[cut_end] == 0x0A:
            cut_end += 1
        edit1_range = (line_start, cut_end)
    else:
        edit1_range = (first_label_stmt.start_byte, label_prefix_end)
    edit1_replacement = b""

    # Apply edits in reverse byte order so byte offsets stay valid.
    edits: list[tuple[int, int, bytes]] = [
        (edit3_range[0], edit3_range[1], edit3_replacement),
    ]
    if edit2_range is not None:
        edits.append((edit2_range[0], edit2_range[1], edit2_replacement))
    edits.append((edit1_range[0], edit1_range[1], edit1_replacement))

    # Sort descending by start
    edits.sort(key=lambda e: e[0], reverse=True)
    new_source = source
    for start, end, repl in edits:
        new_source = new_source[:start] + repl + new_source[end:]

    yield Variant(
        name=f"common_tail_dup_{counter}",
        pattern_name="common_tail_goto_to_duplicate",
        description=(
            f"Duplicate else-tail `{label_name.decode()}` body into inner-if "
            f"to eliminate a goto-into-else"
        ),
        source=new_source,
    )
