"""Float const static pattern — convert inline float literals to static const.

When the target caches a float literal pool ADDRESS in a callee-saved GPR
(e.g. `lis r29, lbl_...` + `lfs fN, offset, r29`) but our compiler caches
the VALUE in a callee-saved FPR (e.g. `fmr f1, f30`), replacing inline float
literals with `static const float kName = value;` forces address-based access
matching the target.

Detection: prologue has GPR-FPR type mismatch (`__savegprlr_N` vs manual FPR
saves), diff shows `lfs fN, label, rGPR` (target) vs `fmr fN, fFPR` (base).

Proven on ContentLoadingPanel::Poll (84.8% -> 100%).

Unlike float_literal_pressure (which requires >= 2 uses), this pattern works
on single-use float literals too — even one FPR-cached literal causes a
prologue type conflict.

Strategies:
1. Inline float literal -> static const float (GPR address cache)
   - Single literals: one static const per literal
   - Grouped: multiple literals sharing a nearby address base
2. Static const float -> inline literal (reverse, when target wants FPR)
"""

from __future__ import annotations

import re
from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk, get_indent, get_line_start
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

# Float literals to skip — these are often special-cased by the compiler
# and don't benefit from static const extraction.
_SKIP_VALUES = frozenset({
    b"0.0f", b"0.f", b"0.0", b"0.",
    b"1.0f", b"1.f", b"1.0", b"1.",
    b"-0.0f", b"-0.f", b"-1.0f", b"-1.f",
})

# Macro names where float literals should not be touched
_SKIP_MACROS = frozenset({
    b"MILO_ASSERT", b"MILO_WARN", b"MILO_FAIL", b"MILO_LOG",
    b"MILO_NOTIFY", b"MILO_ASSERT_FMT",
})


def _is_inside_macro_call(node: Node, source: bytes) -> bool:
    """Check if a node is inside a known macro call.

    Walks up the AST looking for a call_expression whose function name
    matches one of the skip macros.
    """
    current = node.parent
    while current is not None:
        if current.type == "call_expression":
            func = current.child_by_field_name("function")
            if func is not None:
                func_text = source[func.start_byte:func.end_byte]
                if func_text in _SKIP_MACROS:
                    return True
        current = current.parent
    return False


def _is_inside_initializer(node: Node) -> bool:
    """Check if a node is inside an init_declarator (variable initialization).

    We don't want to replace a literal in `static const float kX = 3.14f;`
    with itself, or replace literals in other variable initializations that
    are already static const.
    """
    current = node.parent
    while current is not None:
        if current.type == "init_declarator":
            # Check if the declaration is static const
            decl = current.parent
            if decl is not None and decl.type == "declaration":
                decl_text = decl.text
                if decl_text is not None and b"static" in decl_text and b"const" in decl_text:
                    return True
        current = current.parent
    return False


def _literal_var_name(literal: bytes, index: int) -> bytes:
    """Generate a variable name from a float literal value."""
    # Strip trailing 'f' and replace '.' with '_'
    val_str = literal.decode().rstrip("f").replace(".", "_").replace("-", "neg")
    # Remove trailing underscores
    val_str = val_str.rstrip("_")
    return f"kFloat{val_str}_{index}".encode()


def _find_float_literals(
    func_node: Node,
    source: bytes,
    ctx: FunctionContext,
) -> list[Node]:
    """Find all float literal nodes in the function body suitable for extraction.

    Filters out:
    - Literals in _SKIP_VALUES (0.0f, 1.0f, etc.)
    - Literals inside MILO_ASSERT and similar macros
    - Literals inside existing static const declarations
    - Literals outside mismatch regions (when attribution is available)
    """
    results: list[Node] = []
    for node in walk(func_node):
        if node.type != "number_literal":
            continue
        if node.text is None:
            continue
        text = node.text
        # Must be a float literal (contains '.' or ends with 'f')
        if b"." not in text and not text.endswith(b"f"):
            continue
        # Skip integer-like floats
        if text in _SKIP_VALUES:
            continue
        # Skip if inside a macro call
        if _is_inside_macro_call(node, source):
            continue
        # Skip if inside an existing static const initializer
        if _is_inside_initializer(node):
            continue
        # Region filter: only consider literals in mismatch regions
        if not ctx.node_in_mismatch_region(node):
            continue
        results.append(node)
    return results


def _find_static_const_floats_in_func(
    func_node: Node, source: bytes
) -> list[tuple[Node, bytes, bytes]]:
    """Find static const float declarations inside the function body.

    Returns list of (declaration_node, var_name, literal_value).
    """
    results: list[tuple[Node, bytes, bytes]] = []
    body = func_node.child_by_field_name("body")
    if body is None:
        return results

    for stmt in body.named_children:
        if stmt.type != "declaration":
            continue
        stmt_text = source[stmt.start_byte:stmt.end_byte]
        if b"static" not in stmt_text or b"const" not in stmt_text or b"float" not in stmt_text:
            continue
        # Parse: static const float NAME = VALUE;
        m = re.match(
            rb"static\s+const\s+float\s+(\w+)\s*=\s*([^;]+);",
            stmt_text.strip(),
        )
        if m:
            var_name = m.group(1)
            value = m.group(2).strip()
            results.append((stmt, var_name, value))
    return results


class FloatConstStaticPattern(Pattern):
    name = "float_const_static"
    structural_domain = "prologue"
    safety_tier = "normal"
    follow_ups = ("prologue_pressure", "float_literal_pressure")

    def relevant(self, diagnosis: Diagnosis) -> bool:
        # Primary signal: opposite-sign GPR/FPR save deltas
        if diagnosis.has_gpr_fpr_type_conflict:
            return True
        # Secondary: any prologue mismatch with nonzero FPR delta
        if diagnosis.has_prologue_mismatch and diagnosis.fpr_save_delta != 0:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        # Highest priority when GPR-FPR type conflict is present —
        # this is the exact scenario this pattern was designed for.
        if diagnosis.has_gpr_fpr_type_conflict:
            return 0.95
        # Still useful for general prologue FPR delta
        if diagnosis.has_prologue_mismatch and diagnosis.fpr_save_delta != 0:
            return 0.7
        return 0.0

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        diag = ctx.diagnosis
        if diag is None:
            return

        counter = 0

        # Strategy 1: Target needs MORE GPR and FEWER FPR saves —
        # extract inline float literals to static const (GPR address cache)
        if diag.gpr_save_delta > 0 and diag.fpr_save_delta < 0:
            for v in self._literals_to_static(ctx, counter):
                yield v
                counter += 1
                if counter >= 12:
                    return

        # Strategy 2: Target needs FEWER GPR and MORE FPR saves —
        # inline static const floats back to literals (FPR value cache)
        if diag.gpr_save_delta < 0 and diag.fpr_save_delta > 0:
            for v in self._static_to_literals(ctx, counter):
                yield v
                counter += 1
                if counter >= 12:
                    return

        # Strategy 3: For any GPR-FPR type conflict, try both directions
        if diag.has_gpr_fpr_type_conflict and counter == 0:
            for v in self._literals_to_static(ctx, counter):
                yield v
                counter += 1
                if counter >= 8:
                    return
            for v in self._static_to_literals(ctx, counter):
                yield v
                counter += 1
                if counter >= 12:
                    return

        # Strategy 4: Prologue FPR mismatch without clear direction —
        # try extracting any float literals
        if diag.has_prologue_mismatch and diag.fpr_save_delta != 0 and counter == 0:
            for v in self._literals_to_static(ctx, counter):
                yield v
                counter += 1
                if counter >= 6:
                    return

    def _literals_to_static(
        self, ctx: FunctionContext, start: int
    ) -> Iterator[Variant]:
        """Extract inline float literals to static const float declarations.

        Unlike float_literal_pressure which only extracts multi-use literals,
        this pattern extracts single-use literals too, since even one
        FPR-cached literal causes a prologue type conflict.

        Generates variants:
        1. One variant per unique float literal value (individual extraction)
        2. One variant extracting ALL float literals at once (bulk extraction)
        3. Subsets (pairs, triples) of float literals for partial extraction
        """
        source = ctx.file_source
        literals = _find_float_literals(ctx.func_node, source, ctx)
        if not literals:
            return

        counter = start

        # Group by literal value (same value -> same static const)
        by_value: dict[bytes, list[Node]] = {}
        for node in literals:
            text = node.text
            if text is not None:
                by_value.setdefault(text, []).append(node)

        # Determine insertion point: first statement in function body
        body = ctx.func_node.child_by_field_name("body")
        if body is None:
            return
        # Insert after the opening brace of the compound_statement
        # Find the first non-whitespace content after '{'
        insert_byte = body.start_byte + 1  # after '{'
        # Get indentation of the first statement
        if body.named_children:
            first_stmt = body.named_children[0]
            indent = get_indent(source, first_stmt)
        else:
            indent = b"    "

        # Variant: extract ALL float literals at once
        if len(by_value) >= 2:
            ed = SourceEditor(source)
            decl_lines = []
            var_idx = 0
            value_to_name: dict[bytes, bytes] = {}

            for lit_val, nodes in by_value.items():
                var_name = _literal_var_name(lit_val, var_idx)
                value_to_name[lit_val] = var_name
                decl_lines.append(
                    indent + b"static const float " + var_name + b" = " + lit_val + b";\n"
                )
                var_idx += 1

            # Insert all declarations at the top of the function body
            decl_block = b"\n" + b"".join(decl_lines)
            ed.insert_at(insert_byte, decl_block)

            # Replace all literal uses with their variable names
            for lit_val, nodes in by_value.items():
                var_name = value_to_name[lit_val]
                for node in nodes:
                    ed.replace_node(node, var_name)

            try:
                new_source = ed.apply()
                yield Variant(
                    name=f"fcs_all_{counter}",
                    pattern_name=self.name,
                    description=f"Extract all {len(by_value)} float literals to static const",
                    source=new_source,
                )
                counter += 1
            except ValueError:
                pass

        # Variants: extract each unique literal value individually
        var_idx = 0
        for lit_val, nodes in by_value.items():
            if counter - start >= 10:
                break

            var_name = _literal_var_name(lit_val, var_idx)
            var_idx += 1

            ed = SourceEditor(source)
            decl = b"\n" + indent + b"static const float " + var_name + b" = " + lit_val + b";\n"
            ed.insert_at(insert_byte, decl)

            for node in nodes:
                ed.replace_node(node, var_name)

            try:
                new_source = ed.apply()
                uses = len(nodes)
                yield Variant(
                    name=f"fcs_single_{counter}",
                    pattern_name=self.name,
                    description=f"Extract {lit_val.decode()} ({uses}x) to static const {var_name.decode()}",
                    source=new_source,
                )
                counter += 1
            except ValueError:
                continue

        # Variant: extract as file-scope static const (before the function)
        # This is an alternative placement that may produce different codegen.
        if by_value and counter - start < 12:
            func_start = ctx.func_node.start_byte
            # Find line start of the function
            line_start = func_start
            while line_start > 0 and source[line_start - 1:line_start] != b"\n":
                line_start -= 1

            ed = SourceEditor(source)
            decl_lines = []
            var_idx = 0
            value_to_name = {}

            for lit_val, nodes in by_value.items():
                var_name = b"s_kFloat" + _literal_var_name(lit_val, var_idx)[len(b"kFloat"):]
                value_to_name[lit_val] = var_name
                decl_lines.append(
                    b"static const float " + var_name + b" = " + lit_val + b";\n"
                )
                var_idx += 1

            decl_block = b"".join(decl_lines) + b"\n"
            ed.insert_at(line_start, decl_block)

            for lit_val, nodes in by_value.items():
                var_name = value_to_name[lit_val]
                for node in nodes:
                    ed.replace_node(node, var_name)

            try:
                new_source = ed.apply()
                yield Variant(
                    name=f"fcs_filescope_{counter}",
                    pattern_name=self.name,
                    description=f"Extract {len(by_value)} float literals to file-scope static const",
                    source=new_source,
                )
                counter += 1
            except ValueError:
                pass

    def _static_to_literals(
        self, ctx: FunctionContext, start: int
    ) -> Iterator[Variant]:
        """Inline static const float declarations back to literal values.

        When the target caches float values in FPR (not GPR address), existing
        static const floats should be inlined back to literals.

        Searches for:
        1. static const float declarations inside the function body
        2. static const float declarations before the function (file-scope)
        """
        source = ctx.file_source
        counter = start

        # Check in-function static const floats
        statics = _find_static_const_floats_in_func(ctx.func_node, source)
        for decl_node, var_name, value in statics:
            if counter - start >= 6:
                break

            # Ensure value looks like a float literal
            if not value.endswith(b"f"):
                value = value + b"f"

            # Find all uses of this variable in the function body
            uses: list[Node] = []
            for node in walk(ctx.func_node):
                if node.type == "identifier" and node.text == var_name:
                    # Don't replace the declaration itself
                    if node.start_byte >= decl_node.start_byte and node.end_byte <= decl_node.end_byte:
                        continue
                    uses.append(node)

            if not uses:
                continue

            ed = SourceEditor(source)
            # Remove the declaration line
            decl_line_start = get_line_start(source, decl_node)
            # Find end of line (including newline)
            decl_line_end = decl_node.end_byte
            while decl_line_end < len(source) and source[decl_line_end:decl_line_end + 1] != b"\n":
                decl_line_end += 1
            if decl_line_end < len(source):
                decl_line_end += 1  # include the newline
            ed.delete_range(decl_line_start, decl_line_end)

            # Replace all uses with the literal value
            for node in uses:
                ed.replace_node(node, value)

            try:
                new_source = ed.apply()
                yield Variant(
                    name=f"fcs_inline_{counter}",
                    pattern_name=self.name,
                    description=f"Inline static const {var_name.decode()} -> {value.decode()} (FPR val cache)",
                    source=new_source,
                )
                counter += 1
            except ValueError:
                continue

        # Check file-scope static const floats before the function
        func_start = ctx.func_node.start_byte
        pre_func = source[:func_start]

        static_re = re.compile(
            rb"static\s+const\s+float\s+(\w+)\s*=\s*([^;]+);"
        )
        for m in static_re.finditer(pre_func):
            if counter - start >= 10:
                break

            var_name = m.group(1)
            value = m.group(2).strip()
            if not value.endswith(b"f"):
                value = value + b"f"

            # Find uses in the function
            uses = []
            for node in walk(ctx.func_node):
                if node.type == "identifier" and node.text == var_name:
                    if not ctx.node_in_mismatch_region(node):
                        continue
                    uses.append(node)

            if not uses:
                continue

            ed = SourceEditor(source)
            for node in uses:
                ed.replace_node(node, value)

            try:
                new_source = ed.apply()
                yield Variant(
                    name=f"fcs_inline_file_{counter}",
                    pattern_name=self.name,
                    description=f"Inline file-scope {var_name.decode()} -> {value.decode()} (FPR val cache)",
                    source=new_source,
                )
                counter += 1
            except ValueError:
                continue
