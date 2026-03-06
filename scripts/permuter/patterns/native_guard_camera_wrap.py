"""Native guard camera wrap pattern — normalize inline UI camera select/restore blocks.

Opt-in domain rule for Milo UI/text rendering functions. Rewrites an inline
camera switch/restore sequence to shared helper calls:

    RndCam *savedCam = RndCam::Current();
    RndCam *uiCam = TheUI ? TheUI->GetCam() : nullptr;
    if (uiCam && uiCam != savedCam) uiCam->Select();
    ...
    if (savedCam && savedCam != RndCam::Current()) savedCam->Select();

into:

    RndCam *savedCam = SelectTextRenderCam();
    ...
    RestoreTextRenderCam(savedCam);
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import get_indent
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant


class NativeGuardCameraWrapPattern(Pattern):
    name = "native_guard_camera_wrap"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        if diagnosis.clusters:
            return True
        for d in diagnosis.diff_ops:
            if d.target_opcode.startswith("b") or d.base_opcode.startswith("b"):
                return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        return 0.5 if self.relevant(diagnosis) else 0.0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        source = ctx.file_source
        # Don't emit helper-call variants unless helpers exist in the current TU.
        if (b"SelectTextRenderCam(" not in source
                or b"RestoreTextRenderCam(" not in source):
            return

        stmts = list(ctx.statements)
        if len(stmts) < 4:
            return

        for i in range(len(stmts) - 2):
            saved_decl = stmts[i]
            ui_decl = stmts[i + 1]
            select_if = stmts[i + 2]

            if not _is_saved_cam_decl(saved_decl, source):
                continue
            if not _is_ui_cam_decl(ui_decl, source):
                continue
            if not _is_select_if(select_if, source):
                continue

            restore_idx = _find_restore_if(stmts, i + 3, source)
            if restore_idx is None:
                continue

            restore_if = stmts[restore_idx]
            indent = get_indent(source, saved_decl)

            ed = SourceEditor(source)
            head_repl = indent + b"RndCam *savedCam = SelectTextRenderCam();\n"
            ed.replace_range(saved_decl.start_byte, select_if.end_byte, head_repl)

            tail_repl = indent + b"RestoreTextRenderCam(savedCam);"
            ed.replace_node(restore_if, tail_repl)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            yield Variant(
                name="camwrap_0",
                pattern_name=self.name,
                description="Replace inline UI camera select/restore with helper calls",
                source=new_source,
            )
            return


def _stmt_text(stmt: Node, source: bytes) -> bytes:
    return source[stmt.start_byte:stmt.end_byte]


def _is_saved_cam_decl(stmt: Node, source: bytes) -> bool:
    if stmt.type != "declaration":
        return False
    txt = _stmt_text(stmt, source)
    return b"savedCam" in txt and b"RndCam::Current()" in txt


def _is_ui_cam_decl(stmt: Node, source: bytes) -> bool:
    if stmt.type != "declaration":
        return False
    txt = _stmt_text(stmt, source)
    return b"uiCam" in txt and b"TheUI" in txt and b"GetCam" in txt


def _is_select_if(stmt: Node, source: bytes) -> bool:
    if stmt.type != "if_statement":
        return False
    txt = _stmt_text(stmt, source)
    return b"uiCam" in txt and b"savedCam" in txt and b"Select" in txt


def _find_restore_if(stmts: list[Node], start: int, source: bytes) -> int | None:
    for idx in range(start, len(stmts)):
        stmt = stmts[idx]
        if stmt.type != "if_statement":
            continue
        txt = _stmt_text(stmt, source)
        if b"savedCam" in txt and b"RndCam::Current()" in txt and b"savedCam->Select" in txt:
            return idx
    return None
