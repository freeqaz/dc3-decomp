"""Parse Ghidra decompilation output with tree-sitter-c.

Extracts structural information from Ghidra's C output:
- Variable first-use order
- Expression tree structure
- Control flow skeleton
- Prologue save count (__savegprlr_N calls)

Ghidra output contains types like `undefined8` that aren't valid C, but
tree-sitter-c handles them gracefully (parsing the identifier as a type
specifier, which is good enough for structural analysis).
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Optional

import tree_sitter_c as tsc
from tree_sitter import Language, Node, Parser

C_LANGUAGE = Language(tsc.language())
_PARSER = Parser(C_LANGUAGE)

# Ghidra variable name patterns
_GHIDRA_VAR_RE = re.compile(
    r"^([a-zA-Z])Var(\d+)$"  # iVar2, fVar3, pVar1, uVar4, etc.
)
_GHIDRA_LOCAL_RE = re.compile(
    r"^local_([0-9a-fA-F]+)$"  # local_38, local_1c, etc.
)
_GHIDRA_PARAM_RE = re.compile(
    r"^param_(\d+)$"  # param_1, param_2, etc.
)

# __savegprlr_N pattern in Ghidra output
_SAVEGPRLR_RE = re.compile(r"__savegprlr_(\d+)")
_SAVEFPR_RE = re.compile(r"__savefpr_(\d+)")


@dataclass
class GhidraAST:
    """Parsed Ghidra decompilation."""

    code: str
    tree: object  # tree-sitter Tree
    func_node: Optional[Node] = None
    body_node: Optional[Node] = None
    has_errors: bool = False


@dataclass
class VarInfo:
    """Information about a variable in Ghidra output."""

    name: str
    first_use_line: int  # 0-based line number of first use
    first_use_byte: int  # byte offset of first use
    type_prefix: str  # 'i'=int, 'f'=float, 'p'=pointer, 'u'=unsigned, ''=unknown
    is_param: bool = False
    decl_type: str = ""  # Ghidra's declared type if available


def parse_ghidra(code: str) -> GhidraAST:
    """Parse Ghidra decompilation output into a tree-sitter AST.

    Returns a GhidraAST with the parsed tree and function/body nodes.
    """
    code_bytes = code.encode("utf-8")
    tree = _PARSER.parse(code_bytes)
    root = tree.root_node

    result = GhidraAST(
        code=code,
        tree=tree,
        has_errors=root.has_error,
    )

    # Find the first function definition
    for child in root.children:
        if child.type == "function_definition":
            result.func_node = child
            result.body_node = child.child_by_field_name("body")
            break

    return result


def extract_variable_first_use_order(ast: GhidraAST) -> list[VarInfo]:
    """Extract local variables in first-use order from Ghidra output.

    Returns variables ordered by their first appearance in the function body.
    Parameters are excluded (they appear in the signature, not local scope).
    """
    if not ast.body_node:
        return []

    code_bytes = ast.code.encode("utf-8")

    # First, collect declared local variables
    local_vars: set[str] = set()
    for stmt in ast.body_node.named_children:
        if stmt.type == "declaration":
            _collect_declared_names(stmt, local_vars)

    if not local_vars:
        return []

    # Walk body to find first use of each variable
    first_use: dict[str, tuple[int, int]] = {}  # name -> (byte_offset, line)
    _walk_for_first_use(ast.body_node, local_vars, first_use, code_bytes)

    # Sort by first use position
    result = []
    for name in sorted(first_use, key=lambda n: first_use[n]):
        byte_off, line = first_use[name]
        prefix = _type_prefix(name)
        decl_type = _find_declaration_type(ast.body_node, name, code_bytes)
        result.append(VarInfo(
            name=name,
            first_use_line=line,
            first_use_byte=byte_off,
            type_prefix=prefix,
            decl_type=decl_type,
        ))

    return result


def extract_expression_structure(node: Node, code_bytes: bytes) -> str:
    """Extract a normalized expression structure string from an AST node.

    Returns a string representing the expression shape, ignoring variable
    names and literal values. For example:
        `a - b + c`  -> `(- (+ _ _) _)`
        `a - (b - c)` -> `(- _ (- _ _))`

    This allows structural comparison between Ghidra and source expressions.
    """
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        left = node.child_by_field_name("left")
        right = node.child_by_field_name("right")
        if op and left and right:
            op_text = code_bytes[op.start_byte:op.end_byte].decode()
            left_s = extract_expression_structure(left, code_bytes)
            right_s = extract_expression_structure(right, code_bytes)
            return f"({op_text} {left_s} {right_s})"

    if node.type == "unary_expression":
        op = node.child_by_field_name("operator")
        operand = node.child_by_field_name("argument")
        if op and operand:
            op_text = code_bytes[op.start_byte:op.end_byte].decode()
            operand_s = extract_expression_structure(operand, code_bytes)
            return f"({op_text} {operand_s})"

    if node.type == "parenthesized_expression":
        for child in node.named_children:
            return extract_expression_structure(child, code_bytes)

    if node.type == "cast_expression":
        value = node.child_by_field_name("value")
        if value:
            return extract_expression_structure(value, code_bytes)

    if node.type == "call_expression":
        return "call"

    if node.type in ("number_literal", "float_literal", "true", "false"):
        return "lit"

    if node.type == "identifier":
        return "_"

    if node.type in ("field_expression", "subscript_expression",
                      "pointer_expression"):
        return "_"

    # Fallback
    return "_"


def extract_control_flow_skeleton(ast: GhidraAST) -> list[str]:
    """Extract the control flow skeleton of a function.

    Returns a list of control flow node types in order:
    ['if', 'return', 'if', 'while', 'return']

    Useful for detecting structural differences like guard-vs-conjunction.
    """
    if not ast.body_node:
        return []

    skeleton: list[str] = []
    for stmt in ast.body_node.named_children:
        _collect_cf_nodes(stmt, skeleton, depth=0)
    return skeleton


def extract_savegpr_count(code: str) -> int | None:
    """Extract __savegprlr_N call from Ghidra output.

    Returns the number of saved GPRs (32 - N), or None if not found.
    """
    m = _SAVEGPRLR_RE.search(code)
    if m:
        return 32 - int(m.group(1))
    return None


def extract_savefpr_count(code: str) -> int | None:
    """Extract __savefpr_N call from Ghidra output.

    Returns the number of saved FPRs (32 - N), or None if not found.
    """
    m = _SAVEFPR_RE.search(code)
    if m:
        return 32 - int(m.group(1))
    return None


def extract_condition_structure(ast: GhidraAST) -> list[str]:
    """Extract condition patterns from Ghidra control flow.

    Returns list of tags like:
    - "conjunction" — uses && in a condition
    - "disjunction" — uses || in a condition
    - "nested_if" — has nested if without && (split form)
    - "guard_return" — has if(cond) return; pattern
    - "guard_return_false" — has if(cond) return false; pattern
    """
    if not ast.body_node:
        return []

    code_bytes = ast.code.encode("utf-8")
    tags: list[str] = []
    seen: set[str] = set()

    def _add(tag: str) -> None:
        if tag not in seen:
            seen.add(tag)
            tags.append(tag)

    for node in _walk_all(ast.body_node):
        if node.type == "if_statement":
            condition = node.child_by_field_name("condition")
            consequence = node.child_by_field_name("consequence")
            if condition is None:
                continue

            # Check condition for && or ||
            inner = _get_condition_inner(condition)
            if inner is not None:
                if _has_operator(inner, b"&&"):
                    _add("conjunction")
                if _has_operator(inner, b"||"):
                    _add("disjunction")

            # Check for nested if (split form)
            if consequence is not None and consequence.type == "compound_statement":
                inner_stmts = [c for c in consequence.named_children
                               if c.type != "comment"]
                if (len(inner_stmts) == 1 and inner_stmts[0].type == "if_statement"
                        and inner is not None and not _has_operator(inner, b"&&")):
                    _add("nested_if")

            # Check for guard return pattern
            if consequence is not None:
                ret = _extract_return_from_consequence(consequence)
                if ret is not None:
                    ret_text = code_bytes[ret.start_byte:ret.end_byte].strip()
                    _add("guard_return")
                    if ret_text in (b"return false;", b"return 0;"):
                        _add("guard_return_false")

    return tags


def _walk_all(node: Node):
    """Walk all nodes in the AST."""
    yield node
    for child in node.children:
        yield from _walk_all(child)


def _get_condition_inner(condition: Node) -> Node | None:
    """Get the inner expression from a condition_clause (parenthesized_expression)."""
    for child in condition.named_children:
        if child.type != "comment":
            return child
    return None


def _has_operator(node: Node, op: bytes) -> bool:
    """Check if a node or its children contain a specific binary operator."""
    if node.type == "binary_expression":
        op_node = node.child_by_field_name("operator")
        if op_node is not None and op_node.text == op:
            return True
    for child in node.children:
        if _has_operator(child, op):
            return True
    return False


def _extract_return_from_consequence(consequence: Node) -> Node | None:
    """Extract return statement from if consequence (direct or in compound_statement)."""
    if consequence.type == "return_statement":
        return consequence
    if consequence.type == "compound_statement":
        stmts = [c for c in consequence.named_children if c.type != "comment"]
        if len(stmts) == 1 and stmts[0].type == "return_statement":
            return stmts[0]
    return None


def extract_arithmetic_expressions(ast: GhidraAST) -> list[tuple[Node, str]]:
    """Find arithmetic expressions in Ghidra output and return (node, structure).

    Returns list of (binary_expression node, structure string) for all
    arithmetic expressions (+, -, *, /) in the function body.
    """
    if not ast.body_node:
        return []

    code_bytes = ast.code.encode("utf-8")
    results: list[tuple[Node, str]] = []
    _find_arithmetic(ast.body_node, code_bytes, results)
    return results


# --- Internal helpers ---


def _collect_declared_names(node: Node, names: set[str]) -> None:
    """Collect variable names from a declaration node."""
    declarator = node.child_by_field_name("declarator")
    if declarator is None:
        return

    name = _extract_name_from_declarator(declarator)
    if name and not _GHIDRA_PARAM_RE.match(name):
        names.add(name)


def _extract_name_from_declarator(node: Node) -> str | None:
    """Extract the identifier name from a declarator, unwrapping as needed."""
    if node.type == "identifier" and node.text:
        return node.text.decode("utf-8", errors="replace")

    if node.type == "init_declarator":
        inner = node.child_by_field_name("declarator")
        if inner:
            return _extract_name_from_declarator(inner)

    if node.type in ("pointer_declarator", "array_declarator"):
        inner = node.child_by_field_name("declarator")
        if inner:
            return _extract_name_from_declarator(inner)

    # Walk children as fallback
    for child in node.named_children:
        if child.type == "identifier" and child.text:
            return child.text.decode("utf-8", errors="replace")

    return None


def _walk_for_first_use(
    node: Node,
    targets: set[str],
    first_use: dict[str, tuple[int, int]],
    code_bytes: bytes,
) -> None:
    """Walk AST to find first use of target variable names."""
    if node.type == "identifier" and node.text:
        name = node.text.decode("utf-8", errors="replace")
        if name in targets and name not in first_use:
            line = code_bytes[:node.start_byte].count(b"\n")
            first_use[name] = (node.start_byte, line)

    for child in node.children:
        _walk_for_first_use(child, targets, first_use, code_bytes)


def _type_prefix(name: str) -> str:
    """Extract Ghidra type prefix from variable name."""
    m = _GHIDRA_VAR_RE.match(name)
    if m:
        return m.group(1).lower()
    return ""


def _find_declaration_type(
    body: Node, var_name: str, code_bytes: bytes
) -> str:
    """Find the declared type of a variable in the function body."""
    for stmt in body.named_children:
        if stmt.type != "declaration":
            continue
        declarator = stmt.child_by_field_name("declarator")
        if declarator is None:
            continue
        name = _extract_name_from_declarator(declarator)
        if name == var_name:
            # The type is the type specifier
            type_node = stmt.child_by_field_name("type")
            if type_node:
                return code_bytes[type_node.start_byte:type_node.end_byte].decode(
                    "utf-8", errors="replace"
                )
    return ""


def _collect_cf_nodes(node: Node, skeleton: list[str], depth: int) -> None:
    """Collect control flow node types."""
    if node.type == "if_statement":
        skeleton.append("if")
        # Recurse into consequence and alternative
        consequence = node.child_by_field_name("consequence")
        if consequence:
            for child in consequence.named_children:
                _collect_cf_nodes(child, skeleton, depth + 1)
        alternative = node.child_by_field_name("alternative")
        if alternative:
            skeleton.append("else")
            for child in alternative.named_children:
                _collect_cf_nodes(child, skeleton, depth + 1)
        return

    if node.type == "return_statement":
        skeleton.append("return")
        return

    if node.type == "while_statement":
        skeleton.append("while")
        body = node.child_by_field_name("body")
        if body:
            for child in body.named_children:
                _collect_cf_nodes(child, skeleton, depth + 1)
        return

    if node.type == "for_statement":
        skeleton.append("for")
        body = node.child_by_field_name("body")
        if body:
            for child in body.named_children:
                _collect_cf_nodes(child, skeleton, depth + 1)
        return

    if node.type == "switch_statement":
        skeleton.append("switch")
        return

    # Recurse into compound statements and expression statements
    if node.type in ("compound_statement", "expression_statement"):
        for child in node.named_children:
            _collect_cf_nodes(child, skeleton, depth)


def _find_arithmetic(
    node: Node, code_bytes: bytes, results: list[tuple[Node, str]]
) -> None:
    """Find top-level arithmetic expressions (not nested in other arithmetic)."""
    if node.type == "binary_expression":
        op = node.child_by_field_name("operator")
        if op and op.text in (b"+", b"-", b"*", b"/"):
            structure = extract_expression_structure(node, code_bytes)
            results.append((node, structure))
            return  # Don't recurse into children (they're sub-expressions)

    for child in node.children:
        _find_arithmetic(child, code_bytes, results)
