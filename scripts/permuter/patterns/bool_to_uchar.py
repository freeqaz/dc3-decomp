"""Bool to unsigned char — change bool locals to unsigned char.

Win rate: untested (proven in manual fix for CharLipSyncDriver::Poll).

When bool local variables are used in integer comparisons or stored to
struct members, changing to unsigned char can fix comparison codegen
differences (cmpw vs cmplwi).

Transformations:
    bool skipOverride = false;     -> unsigned char skipOverride = 0;
    if (...) skipOverride = true;  -> if (...) skipOverride = 1;
    bool cached = expr;            -> unsigned char cached = (unsigned char)(expr);

Detection signals:
    - cmpw vs cmplwi mismatches
    - beq/bne branch differences
    - Replace mismatches (different comparison instructions)
"""

from __future__ import annotations

from typing import Iterator

from tree_sitter import Node

from .base import Pattern
from ..ast_queries import walk
from ..editor import SourceEditor
from ..types import Diagnosis, FunctionContext, Variant

_CMP_OPCODES = {"cmpw", "cmplw", "cmpwi", "cmplwi"}
_BRANCH_OPCODES = {"beq", "bne"}


class BoolToUcharPattern(Pattern):
    name = "bool_to_uchar"

    def relevant(self, diagnosis: Diagnosis) -> bool:
        for d in diagnosis.diff_ops:
            if d.target_opcode in _CMP_OPCODES or d.base_opcode in _CMP_OPCODES:
                return True
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                return True
        if diagnosis.replace_real > 0:
            return True
        return False

    def priority(self, diagnosis: Diagnosis) -> float:
        if not self.relevant(diagnosis):
            return 0.0
        score = 0.0
        for d in diagnosis.diff_ops:
            pair = {d.target_opcode, d.base_opcode}
            # Cross-mismatch between signed and unsigned compare — strong signal
            if pair & {"cmpw", "cmplw"} and len(pair & {"cmpw", "cmplw"}) == 2:
                return 0.5
            if pair & {"cmpwi", "cmplwi"} and len(pair & {"cmpwi", "cmplwi"}) == 2:
                return 0.5
            if d.target_opcode in _BRANCH_OPCODES or d.base_opcode in _BRANCH_OPCODES:
                score = max(score, 0.3)
        return score if score > 0.0 else 0.3

    def generate(self, ctx: FunctionContext) -> Iterator[Variant]:
        counter = 0
        # Find all bool variable declarations in the function body
        for decl_info in _find_bool_declarations(ctx.body_node, ctx):
            var_name = decl_info["name"]
            type_node = decl_info["type_node"]
            init_node = decl_info["init_node"]  # may be None

            ed = SourceEditor(ctx.file_source)

            # 1. Replace 'bool' type with 'unsigned char'
            ed.replace_node(type_node, b"unsigned char")

            # 2. Replace initializer if present
            if init_node is not None:
                init_text = ctx.file_source[init_node.start_byte:init_node.end_byte]
                stripped = init_text.strip()
                if stripped == b"true":
                    ed.replace_node(init_node, b"1")
                elif stripped == b"false":
                    ed.replace_node(init_node, b"0")
                else:
                    # Complex expression — wrap in (unsigned char)(...)
                    ed.replace_node(
                        init_node,
                        b"(unsigned char)(" + init_text + b")",
                    )

            # 3. Find all assignments of true/false to this variable in the body
            for assign_info in _find_bool_assignments(ctx.body_node, var_name, ctx):
                rhs_node = assign_info["rhs_node"]
                rhs_text = ctx.file_source[rhs_node.start_byte:rhs_node.end_byte].strip()
                if rhs_text == b"true":
                    ed.replace_node(rhs_node, b"1")
                elif rhs_text == b"false":
                    ed.replace_node(rhs_node, b"0")

            try:
                new_source = ed.apply()
            except ValueError:
                continue

            yield Variant(
                name=f"bool_to_uchar_{counter}",
                pattern_name=self.name,
                description=f"Change bool {var_name} to unsigned char",
                source=new_source,
            )
            counter += 1


def _find_bool_declarations(
    body: Node, ctx: FunctionContext
) -> Iterator[dict]:
    """Find 'bool varname' declarations in the function body."""
    for node in walk(body):
        if node.type != "declaration":
            continue

        # Get the type specifier
        type_node = node.child_by_field_name("type")
        if type_node is None:
            continue
        type_text = ctx.file_source[type_node.start_byte:type_node.end_byte].strip()
        if type_text != b"bool":
            continue

        # Get the declarator (may be init_declarator or plain identifier)
        declarator = node.child_by_field_name("declarator")
        if declarator is None:
            continue

        if declarator.type == "init_declarator":
            name_node = declarator.child_by_field_name("declarator")
            value_node = declarator.child_by_field_name("value")
            if name_node is None:
                continue
            var_name = ctx.file_source[
                name_node.start_byte:name_node.end_byte
            ].decode("utf-8")
            yield {
                "name": var_name,
                "type_node": type_node,
                "init_node": value_node,
            }
        elif declarator.type == "identifier":
            var_name = ctx.file_source[
                declarator.start_byte:declarator.end_byte
            ].decode("utf-8")
            yield {
                "name": var_name,
                "type_node": type_node,
                "init_node": None,
            }


def _find_bool_assignments(
    body: Node, var_name: str, ctx: FunctionContext
) -> Iterator[dict]:
    """Find assignments like 'varname = true' or 'varname = false' in the body."""
    for node in walk(body):
        if node.type != "assignment_expression":
            continue

        left = node.child_by_field_name("left")
        right = node.child_by_field_name("right")
        if left is None or right is None:
            continue

        left_text = ctx.file_source[left.start_byte:left.end_byte].decode("utf-8").strip()
        if left_text != var_name:
            continue

        right_text = ctx.file_source[right.start_byte:right.end_byte].strip()
        if right_text in (b"true", b"false"):
            yield {"rhs_node": right}
