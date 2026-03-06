"""Float literal pressure pattern — swap between inline and static const float.

MSVC PPC handles float literals differently based on their storage:
- Inline literal (100.0f) → compiler caches the VALUE in a callee-saved FPR (fr30)
- static const float → compiler caches the ADDRESS in a callee-saved GPR (r31),
  reloading the value from memory each time it's needed

This affects prologue save counts: FPR caching uses an FPR save slot,
GPR address caching uses a GPR save slot. When the target and base have
opposite-sign GPR/FPR deltas, this pattern tries swapping the approach.

Example:
    // FPR cache (inline literal):
    if (x > 100.0f) x = 100.0f;  → stfd fr30 in prologue

    // GPR cache (static const):
    static const float kLimit = 100.0f;
    if (x > kLimit) x = kLimit;   → std r29 in prologue
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, get_line_start
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Match float literal patterns in source
_FLOAT_LITERAL_RE = re.compile(rb"\b(\d+\.(?:\d+)?f)\b")


class FloatLiteralPressurePattern(Pattern):
    name = "float_literal_pressure"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Primarily relevant when there's a GPR-FPR type conflict
        if diagnosis.has_gpr_fpr_type_conflict:
            return True
        # Also relevant for any prologue mismatch with FPR delta
        if diagnosis.has_prologue_mismatch and diagnosis.fpr_save_delta != 0:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if diagnosis.has_gpr_fpr_type_conflict:
            return 0.9
        if diagnosis.has_prologue_mismatch and diagnosis.fpr_save_delta != 0:
            return 0.6
        return 0.0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        diag = ctx.diagnosis
        if diag is None:
            return

        counter = 0

        # Strategy 1: If target needs MORE GPR and FEWER FPR saves,
        # extract inline float literals to static const (GPR address cache)
        if diag.gpr_save_delta > 0 and diag.fpr_save_delta < 0:
            for v in self._inline_to_static_const(ctx, counter):
                yield v
                counter += 1
                if counter >= 8:
                    return

        # Strategy 2: If target needs FEWER GPR and MORE FPR saves,
        # inline static const floats back to literals (FPR value cache)
        if diag.gpr_save_delta < 0 and diag.fpr_save_delta > 0:
            for v in self._static_const_to_inline(ctx, counter):
                yield v
                counter += 1
                if counter >= 8:
                    return

        # Strategy 3: For any prologue mismatch, try both directions
        if diag.has_prologue_mismatch and counter == 0:
            for v in self._inline_to_static_const(ctx, counter):
                yield v
                counter += 1
                if counter >= 6:
                    return
            for v in self._static_const_to_inline(ctx, counter):
                yield v
                counter += 1
                if counter >= 8:
                    return

    def _inline_to_static_const(
        self, ctx: FunctionContext, start: int
    ) -> Iterator[Variant]:
        """Extract repeated inline float literals to file-scope static const."""
        source = ctx.file_source
        counter = start

        # Find float literals used multiple times in the function body
        literal_uses: dict[bytes, list[Node]] = {}
        for node in walk(ctx.func_node):
            if node.type == "number_literal" and node.text is not None:
                text = node.text
                if text.endswith(b"f") and b"." in text:
                    literal_uses.setdefault(text, []).append(node)

        for literal, nodes in literal_uses.items():
            if len(nodes) < 2:
                continue
            if counter - start >= 6:
                break

            # Generate a descriptive name from the literal value
            val_str = literal.decode().rstrip("f").replace(".", "_")
            var_name = f"_kFloat{val_str}".encode()

            # Insert static const before the function
            func_start = ctx.func_node.start_byte
            # Find the line start of the function
            line_start = func_start
            while line_start > 0 and source[line_start - 1:line_start] != b"\n":
                line_start -= 1

            static_decl = b"static const float " + var_name + b" = " + literal + b";\n"

            ed = SourceEditor(source)
            ed.insert_at(line_start, static_decl)

            # Replace all uses in the function
            for node in nodes:
                ed.replace_node(node, var_name)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            yield Variant(
                name=f"fltpres_static_{counter}",
                pattern_name=self.name,
                description=f"Extract {literal.decode()} ({len(nodes)}x) to static const (GPR addr cache)",
                source=new_source,
            )
            counter += 1

    def _static_const_to_inline(
        self, ctx: FunctionContext, start: int
    ) -> Iterator[Variant]:
        """Inline file-scope static const floats back to literal values.

        Searches for references to identifiers that resolve to static const
        float variables declared outside the function.
        """
        source = ctx.file_source
        counter = start

        # Find const float declarations before the function
        func_start = ctx.func_node.start_byte
        pre_func = source[:func_start]

        # Look for "static const float NAME = VALUE;" patterns
        static_re = re.compile(
            rb"static\s+const\s+float\s+(\w+)\s*=\s*(\d+\.[\d]*f?)\s*;"
        )
        statics: dict[bytes, bytes] = {}
        for m in static_re.finditer(pre_func):
            name = m.group(1)
            value = m.group(2)
            if not value.endswith(b"f"):
                value += b"f"
            statics[name] = value

        if not statics:
            return

        # For each static const, find uses in the function and inline them
        for var_name, literal in statics.items():
            if counter - start >= 6:
                break

            uses = []
            for node in walk(ctx.func_node):
                if node.type == "identifier" and node.text == var_name:
                    uses.append(node)

            if len(uses) < 2:
                continue

            ed = SourceEditor(source)
            for node in uses:
                ed.replace_node(node, literal)

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            yield Variant(
                name=f"fltpres_inline_{counter}",
                pattern_name=self.name,
                description=f"Inline static const {var_name.decode()} → {literal.decode()} (FPR val cache)",
                source=new_source,
            )
            counter += 1
