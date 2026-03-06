"""Color copy shape pattern — switch between channel-wise and aggregate color assignment.

Useful for matching codegen differences around color save/restore blocks where
source may use:
    dst.red = src.red; dst.green = src.green; dst.blue = src.blue;
while target may use:
    dst = src;
(or the reverse).
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent, walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

_ASSIGN_RE = re.compile(
    r"^\s*(?P<lhs>.+)\.(?P<lch>red|green|blue|alpha)\s*=\s*(?P<rhs>.+)\.(?P<rch>red|green|blue|alpha)\s*$"
)


class ColorCopyShapePattern(Pattern):
    name = "color_copy_shape"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        if diagnosis.replace_real > 0:
            return True
        if diagnosis.clusters:
            return True
        # lfs/stfs/fmr patterns often move with color copy shape changes.
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("lfs", "stfs", "fmr") or d.base_opcode in ("lfs", "stfs", "fmr"):
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        if diagnosis.replace_real > 0:
            return 0.6
        return 0.4

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        counter = 0

        for compound in _find_compound_statements(ctx.body_node):
            stmts = list(compound.named_children)
            for i in range(len(stmts)):
                if counter >= 8:
                    return

                # Strategy A: channel-wise RGB(A) -> aggregate assignment
                if i + 2 < len(stmts):
                    rgb_match = _match_channel_run(stmts, i, source)
                    if rgb_match is not None:
                        lhs_base, rhs_base, channels, start, end = rgb_match
                        indent = get_indent(source, stmts[i])
                        replacement = indent + lhs_base + b" = " + rhs_base + b";\n"

                        ed = SourceEditor(source)
                        ed.replace_range(start, end, replacement)
                        try:
                            new_source = ed.apply()
                        except ValueError:
                            continue

                        ch_desc = "/".join(c.decode("utf-8") for c in channels)
                        yield Variant(
                            name=f"coloragg_{counter}",
                            pattern_name=self.name,
                            description=(
                                f"Collapse channel-wise color copy ({ch_desc}) to aggregate assignment"
                            ),
                            source=new_source,
                        )
                        counter += 1

                # Strategy B: aggregate assignment -> explicit RGB copies
                stmt = stmts[i]
                agg_match = _match_aggregate_color_assign(stmt, source)
                if agg_match is None:
                    continue

                lhs_base, rhs_base = agg_match
                indent = get_indent(source, stmt)
                replacement = (
                    indent + lhs_base + b".red = " + rhs_base + b".red;\n"
                    + indent + lhs_base + b".green = " + rhs_base + b".green;\n"
                    + indent + lhs_base + b".blue = " + rhs_base + b".blue;"
                )

                ed = SourceEditor(source)
                ed.replace_node(stmt, replacement)
                try:
                    new_source = ed.apply()
                except ValueError:
                    continue

                yield Variant(
                    name=f"colorch_{counter}",
                    pattern_name=self.name,
                    description="Expand aggregate color assignment to explicit RGB channel copies",
                    source=new_source,
                )
                counter += 1


def _find_compound_statements(body: Node) -> list[Node]:
    return [n for n in walk(body) if n.type == "compound_statement"]


def _extract_assign(stmt: Node, source: bytes) -> tuple[bytes, bytes] | None:
    if stmt.type != "expression_statement":
        return None
    if not stmt.named_children:
        return None
    expr = stmt.named_children[0]
    if expr.type != "assignment_expression":
        return None
    lhs = expr.child_by_field_name("left")
    rhs = expr.child_by_field_name("right")
    op = expr.child_by_field_name("operator")
    if lhs is None or rhs is None or op is None or op.text != b"=":
        return None
    return source[lhs.start_byte:lhs.end_byte], source[rhs.start_byte:rhs.end_byte]


def _match_channel_run(
    stmts: list[Node], idx: int, source: bytes
) -> tuple[bytes, bytes, list[bytes], int, int] | None:
    run = stmts[idx:idx + 4]
    pairs: list[tuple[bytes, bytes]] = []
    for stmt in run:
        pair = _extract_assign(stmt, source)
        if pair is None:
            break
        pairs.append(pair)

    # Need at least RGB assignments.
    if len(pairs) < 3:
        return None

    channels: list[bytes] = []
    lhs_base: bytes | None = None
    rhs_base: bytes | None = None

    for lhs, rhs in pairs:
        m = _ASSIGN_RE.match((lhs + b" = " + rhs).decode("utf-8", errors="replace"))
        if m is None:
            break
        lch = m.group("lch").encode("utf-8")
        rch = m.group("rch").encode("utf-8")
        if lch != rch:
            break

        cur_lhs = m.group("lhs").encode("utf-8")
        cur_rhs = m.group("rhs").encode("utf-8")
        if lhs_base is None:
            lhs_base = cur_lhs
            rhs_base = cur_rhs
        if cur_lhs != lhs_base or cur_rhs != rhs_base:
            break

        channels.append(lch)

    if len(channels) < 3:
        return None

    wanted = {b"red", b"green", b"blue"}
    if not wanted.issubset(set(channels)):
        return None

    used_count = len(channels)
    start = stmts[idx].start_byte
    end = stmts[idx + used_count - 1].end_byte

    # Include one trailing newline for clean replacement.
    while end < len(source) and source[end:end + 1] in (b"\n", b"\r"):
        end += 1

    return lhs_base, rhs_base, channels, start, end


def _match_aggregate_color_assign(stmt: Node, source: bytes) -> tuple[bytes, bytes] | None:
    pair = _extract_assign(stmt, source)
    if pair is None:
        return None
    lhs, rhs = pair

    # Heuristic to avoid random object assignment expansions.
    lhs_s = lhs.decode("utf-8", errors="replace")
    rhs_s = rhs.decode("utf-8", errors="replace")
    hints = ("Color", "GetColor", "savedColors", "mSavedColor")
    if not any(h in lhs_s for h in hints) and not any(h in rhs_s for h in hints):
        return None

    return lhs, rhs
