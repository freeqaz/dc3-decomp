"""Tree-sitter based function extraction from C++ source files."""

from __future__ import annotations

from pathlib import Path

import tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser, Node

from .types import FunctionContext

CPP_LANGUAGE = Language(tscpp.language())
_PARSER = Parser(CPP_LANGUAGE)


def _get_function_name(node: Node) -> str | None:
    """Extract the qualified function name from a function_definition node.

    Handles:
    - Class::Method
    - Class::~Class (destructors)
    - free_function
    - namespace::Class::Method
    """
    # Find the declarator chain: function_definition -> function_declarator -> ...
    declarator = node.child_by_field_name("declarator")
    if declarator is None:
        return None

    # Unwrap pointer/reference declarators
    while declarator.type in ("pointer_declarator", "reference_declarator"):
        inner = declarator.child_by_field_name("declarator")
        if inner is None:
            # Fallback: find first named child that is a declarator type
            for c in declarator.named_children:
                if c.type in ("function_declarator", "pointer_declarator",
                              "reference_declarator", "qualified_identifier",
                              "identifier"):
                    inner = c
                    break
        declarator = inner
        if declarator is None:
            return None

    # function_declarator wraps the actual name
    if declarator.type == "function_declarator":
        name_node = declarator.child_by_field_name("declarator")
        if name_node is None:
            return None
    else:
        return None

    # Extract text depending on node type
    if name_node.type == "qualified_identifier":
        return name_node.text.decode("utf-8")
    elif name_node.type == "identifier":
        return name_node.text.decode("utf-8")
    elif name_node.type == "destructor_name":
        return name_node.text.decode("utf-8")
    elif name_node.type == "field_identifier":
        return name_node.text.decode("utf-8")
    elif name_node.type == "operator_name":
        return name_node.text.decode("utf-8")

    return name_node.text.decode("utf-8") if name_node.text else None


def reparse_variant(
    original_ctx: FunctionContext,
    new_source: bytes,
) -> FunctionContext:
    """Re-parse modified source to get fresh AST nodes.

    Finds the same function by name in the re-parsed tree.
    Preserves file_path and diagnosis from the original context.

    Args:
        original_ctx: The original FunctionContext to inherit metadata from.
        new_source: Modified source bytes to re-parse.

    Returns:
        A new FunctionContext with fresh AST nodes.

    Raises:
        ValueError: If the function cannot be found (e.g. syntax error from pattern).
    """
    func_name = _get_function_name(original_ctx.func_node)
    if func_name is None:
        raise ValueError("Cannot determine function name from original context")

    tree = _PARSER.parse(new_source)

    for child in tree.root_node.children:
        if child.type != "function_definition":
            continue
        name = _get_function_name(child)
        if name == func_name:
            body = child.child_by_field_name("body")
            if body is None:
                raise ValueError(f"Function {func_name} has no body after reparse")
            statements = [c for c in body.named_children]
            return FunctionContext(
                file_path=original_ctx.file_path,
                file_source=new_source,
                func_node=child,
                body_node=body,
                statements=statements,
                func_byte_range=(child.start_byte, child.end_byte),
                diagnosis=original_ctx.diagnosis,
            )

    raise ValueError(
        f"Function '{func_name}' not found after reparse "
        f"(pattern may have produced invalid syntax)"
    )


def extract_function(file_path: Path, func_name: str) -> FunctionContext:
    """Extract a function from a C++ source file by its qualified name.

    Args:
        file_path: Path to the .cpp file
        func_name: Qualified function name (e.g. "RndMesh::BurnXfm")

    Returns:
        FunctionContext with parsed function data

    Raises:
        ValueError: If function not found (includes list of available functions)
    """
    source = file_path.read_bytes()
    tree = _PARSER.parse(source)

    available: list[str] = []

    for child in tree.root_node.children:
        if child.type != "function_definition":
            continue

        name = _get_function_name(child)
        if name is not None:
            available.append(name)

        if name == func_name:
            body = child.child_by_field_name("body")
            if body is None:
                raise ValueError(f"Function {func_name} has no body")

            statements = [c for c in body.named_children]
            return FunctionContext(
                file_path=file_path,
                file_source=source,
                func_node=child,
                body_node=body,
                statements=statements,
                func_byte_range=(child.start_byte, child.end_byte),
            )

    raise ValueError(
        f"Function '{func_name}' not found in {file_path}.\n"
        f"Available functions ({len(available)}):\n"
        + "\n".join(f"  - {name}" for name in available)
    )
