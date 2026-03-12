"""Tree-sitter based function extraction from C++ source files."""

from __future__ import annotations

import hashlib
import re
from collections import OrderedDict
from pathlib import Path

import tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser, Node, Tree

from .types import FunctionContext, PreprocRegion

CPP_LANGUAGE = Language(tscpp.language())
_PARSER = Parser(CPP_LANGUAGE)

_AST_CACHE_MAX = 50
_ast_cache: OrderedDict[bytes, Tree] = OrderedDict()


def _cached_parse(source: bytes) -> Tree:
    """Parse source bytes with LRU caching keyed by md5 digest."""
    key = hashlib.md5(source).digest()
    if key in _ast_cache:
        _ast_cache.move_to_end(key)
        return _ast_cache[key]
    tree = _PARSER.parse(source)
    _ast_cache[key] = tree
    if len(_ast_cache) > _AST_CACHE_MAX:
        _ast_cache.popitem(last=False)
    return tree


def ast_cache_stats() -> dict:
    """Return current cache size and max capacity."""
    return {"size": len(_ast_cache), "max": _AST_CACHE_MAX}


def ast_cache_clear() -> None:
    """Clear the AST parse cache."""
    _ast_cache.clear()

# Synthetic wrapper for reparsing macro bodies
_SYNTHETIC_PREFIX = b"void _f() {\n"
_SYNTHETIC_SUFFIX = b"\n}\n"

# Method name -> list of (BEGIN_MACRO, END_MACRO) pairs
_MACRO_MAP: dict[str, list[tuple[str, str]]] = {
    "Load":         [("BEGIN_LOADS",           "END_LOADS")],
    "Save":         [("BEGIN_SAVES",           "END_SAVES")],
    "Copy":         [("BEGIN_COPYS",           "END_COPYS")],
    "Handle":       [("BEGIN_HANDLERS",        "END_HANDLERS"),
                     ("BEGIN_CUSTOM_HANDLERS", "END_CUSTOM_HANDLERS")],
    "SyncProperty": [("BEGIN_PROPSYNCS",       "END_PROPSYNCS")],
}


class OffsetNode:
    """Proxy for a tree-sitter Node that shifts byte offsets.

    Wraps an inner node from a synthetic parse and adjusts start_byte/end_byte
    to point into the original file. All other properties are delegated.

    Required for macro extraction: the body is parsed in a synthetic wrapper
    (``void _f() { <body> }``), but patterns need offsets into the original file
    so SourceEditor splices modify the right bytes.
    """

    __slots__ = ("_inner", "_offset")

    def __init__(self, inner: Node, offset: int) -> None:
        self._inner = inner
        self._offset = offset

    @property
    def start_byte(self) -> int:
        return self._inner.start_byte + self._offset

    @property
    def end_byte(self) -> int:
        return self._inner.end_byte + self._offset

    @property
    def type(self) -> str:
        return self._inner.type

    @property
    def text(self) -> bytes | None:
        return self._inner.text

    @property
    def children(self) -> list[OffsetNode]:
        return [OffsetNode(c, self._offset) for c in self._inner.children]

    @property
    def named_children(self) -> list[OffsetNode]:
        return [OffsetNode(c, self._offset) for c in self._inner.named_children]

    @property
    def named_child_count(self) -> int:
        return self._inner.named_child_count

    @property
    def parent(self) -> OffsetNode | None:
        p = self._inner.parent
        return OffsetNode(p, self._offset) if p is not None else None

    @property
    def id(self) -> int:
        return self._inner.id

    def child_by_field_name(self, name: str) -> OffsetNode | None:
        c = self._inner.child_by_field_name(name)
        return OffsetNode(c, self._offset) if c is not None else None

    def __eq__(self, other: object) -> bool:
        if isinstance(other, OffsetNode):
            return self._inner == other._inner
        return NotImplemented

    def __hash__(self) -> int:
        return hash(self._inner.id)


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


def _find_macro_region(
    source: bytes,
    class_name: str,
    begin_macro: str,
    end_macro: str,
) -> tuple[int, int, int, int] | None:
    """Find a BEGIN_XXX(ClassName) / END_XXX macro region in source bytes.

    Returns ``(macro_start, body_start, body_end, macro_end)`` or ``None``.

    - *macro_start*: byte offset of ``BEGIN_XXX``
    - *body_start*: byte after the newline following ``BEGIN_XXX(...)``
    - *body_end*: byte at start of the ``END_XXX`` line
    - *macro_end*: byte after the ``END_XXX`` line

    END_XXX takes no arguments, so positional matching is used: the first
    END_XXX occurring after the matched BEGIN_XXX is selected.  This handles
    files with multiple macros of the same type for different classes.
    """
    begin_pat = re.compile(
        rb"^" + re.escape(begin_macro.encode()) + rb"\(" + re.escape(class_name.encode()) + rb"\)",
        re.MULTILINE,
    )
    match = begin_pat.search(source)
    if match is None:
        return None

    macro_start = match.start()

    # body_start: byte after the newline following the BEGIN line
    newline_pos = source.find(b"\n", match.end())
    if newline_pos == -1:
        return None
    body_start = newline_pos + 1

    # Find first END_XXX after the BEGIN match (positional, not global)
    end_pat = re.compile(
        rb"^" + re.escape(end_macro.encode()) + rb"\b",
        re.MULTILINE,
    )
    end_match = end_pat.search(source, body_start)
    if end_match is None:
        return None

    body_end = end_match.start()

    # macro_end: byte after the END_XXX line
    end_newline = source.find(b"\n", end_match.end())
    macro_end = (end_newline + 1) if end_newline != -1 else len(source)

    return (macro_start, body_start, body_end, macro_end)


def _try_macro_extraction(
    source: bytes,
    file_path: Path,
    func_name: str,
) -> FunctionContext | None:
    """Try to extract a function defined via BEGIN_XXX/END_XXX macros.

    Parses ``func_name`` as ``Class::Method``, looks up the method in
    ``_MACRO_MAP``, finds the macro region, wraps the body in a synthetic
    function for tree-sitter parsing, and returns OffsetNode-wrapped results.

    Returns ``None`` if the function isn't macro-defined.
    """
    parts = func_name.rsplit("::", 1)
    if len(parts) != 2:
        return None  # Free functions not handled
    class_name, method_name = parts

    macro_pairs = _MACRO_MAP.get(method_name)
    if macro_pairs is None:
        return None

    for begin_macro, end_macro in macro_pairs:
        region = _find_macro_region(source, class_name, begin_macro, end_macro)
        if region is None:
            continue

        macro_start, body_start, body_end, macro_end = region
        body_bytes = source[body_start:body_end]

        # Build synthetic source and parse
        synthetic = _SYNTHETIC_PREFIX + body_bytes + _SYNTHETIC_SUFFIX
        tree = _PARSER.parse(synthetic)

        # Find the function_definition in the synthetic tree
        func_node = None
        for child in tree.root_node.children:
            if child.type == "function_definition":
                func_node = child
                break

        if func_node is None:
            continue

        body_node = func_node.child_by_field_name("body")
        if body_node is None:
            continue

        # Offset maps synthetic byte positions to original file positions
        offset = body_start - len(_SYNTHETIC_PREFIX)

        wrapped_func = OffsetNode(func_node, offset)
        wrapped_body = OffsetNode(body_node, offset)
        statements = [OffsetNode(c, offset) for c in body_node.named_children]

        return FunctionContext(
            file_path=file_path,
            file_source=source,
            func_node=wrapped_func,
            body_node=wrapped_body,
            statements=statements,
            func_byte_range=(macro_start, macro_end),
            preproc_regions=_find_function_preproc_regions(source, (macro_start, macro_end)),
        )

    return None


def _find_macro_function_names(source: bytes) -> list[str]:
    """Find function names defined via BEGIN_XXX macros (for error messages)."""
    names: list[str] = []
    for method_name, pairs in _MACRO_MAP.items():
        for begin_macro, _end in pairs:
            pat = re.compile(
                rb"^" + re.escape(begin_macro.encode()) + rb"\((\w+)\)",
                re.MULTILINE,
            )
            for m in pat.finditer(source):
                cls = m.group(1).decode("utf-8")
                names.append(f"{cls}::{method_name}")
    return names


def _find_function_preproc_regions(
    source: bytes, func_byte_range: tuple[int, int]
) -> list[PreprocRegion]:
    """Find preprocessor regions inside a function's byte range.

    Scans line-by-line for #if/#ifdef/#ifndef/#else/#endif and returns balanced
    regions that begin and end inside func_byte_range.
    """
    start, end = func_byte_range
    if start < 0 or end <= start or end > len(source):
        return []

    func_src = source[start:end]
    stack: list[tuple[int, str, str, bool]] = []  # off, kind, macro, has_else
    regions: list[PreprocRegion] = []

    # Keep trailing newlines so line offsets remain exact.
    lines = func_src.splitlines(keepends=True)
    line_off = 0
    for line in lines:
        stripped = line.lstrip()
        if not stripped.startswith(b"#"):
            line_off += len(line)
            continue

        directive = stripped[1:].strip()
        absolute_off = start + line_off

        if directive.startswith(b"ifdef "):
            macro = directive[6:].split(None, 1)[0].decode("utf-8", errors="replace")
            stack.append((absolute_off, "ifdef", macro, False))
        elif directive.startswith(b"ifndef "):
            macro = directive[7:].split(None, 1)[0].decode("utf-8", errors="replace")
            stack.append((absolute_off, "ifndef", macro, False))
        elif directive.startswith(b"if "):
            expr = directive[3:].decode("utf-8", errors="replace").strip()
            stack.append((absolute_off, "if", expr, False))
        elif directive.startswith(b"else"):
            if stack:
                s, k, m, _ = stack[-1]
                stack[-1] = (s, k, m, True)
        elif directive.startswith(b"endif"):
            if stack:
                s, k, m, has_else = stack.pop()
                # Include this line in the region end
                region_end = absolute_off + len(line)
                regions.append(
                    PreprocRegion(
                        start=s,
                        end=region_end,
                        kind=k,
                        macro=m,
                        has_else=has_else,
                    )
                )

        line_off += len(line)

    regions.sort(key=lambda r: r.start)
    return regions


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

    tree = _cached_parse(new_source)

    for func_node in _find_all_function_defs(tree.root_node):
        name = _get_function_name(func_node)
        if name == func_name:
            body = func_node.child_by_field_name("body")
            if body is None:
                raise ValueError(f"Function {func_name} has no body after reparse")
            statements = [c for c in body.named_children]
            func_range = (func_node.start_byte, func_node.end_byte)
            return FunctionContext(
                file_path=original_ctx.file_path,
                file_source=new_source,
                func_node=func_node,
                body_node=body,
                statements=statements,
                func_byte_range=func_range,
                diagnosis=original_ctx.diagnosis,
                symbol=original_ctx.symbol,
                ghidra_code=original_ctx.ghidra_code,
                ghidra_ast=original_ctx.ghidra_ast,
                m2c_code=original_ctx.m2c_code,
                target_var_order=original_ctx.target_var_order,
                target_gpr_saves=original_ctx.target_gpr_saves,
                asm_listing_path=original_ctx.asm_listing_path,
                preproc_regions=_find_function_preproc_regions(new_source, func_range),
            )

    raise ValueError(
        f"Function '{func_name}' not found after reparse "
        f"(pattern may have produced invalid syntax)"
    )


def _find_all_function_defs(node: Node) -> list[Node]:
    """Recursively find all function_definition nodes in the tree.

    Tree-sitter can misparse macro-heavy C++ (e.g. BEGIN_COPYING_MEMBERS),
    causing real top-level function definitions to appear nested inside
    other nodes. This walks the full tree to find them all.
    """
    result: list[Node] = []
    if node.type == "function_definition":
        result.append(node)
    for child in node.children:
        result.extend(_find_all_function_defs(child))
    return result


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
    tree = _cached_parse(source)

    available: list[str] = []

    # Search all function definitions, including those nested due to macro
    # confusion (tree-sitter can't resolve preprocessor macros, so constructs
    # like BEGIN_COPYING_MEMBERS cause it to swallow subsequent functions as
    # children of an earlier node).
    for func_node in _find_all_function_defs(tree.root_node):
        name = _get_function_name(func_node)
        if name is not None:
            available.append(name)

        if name == func_name:
            body = func_node.child_by_field_name("body")
            if body is None:
                raise ValueError(f"Function {func_name} has no body")

            statements = [c for c in body.named_children]
            func_range = (func_node.start_byte, func_node.end_byte)
            return FunctionContext(
                file_path=file_path,
                file_source=source,
                func_node=func_node,
                body_node=body,
                statements=statements,
                func_byte_range=func_range,
                preproc_regions=_find_function_preproc_regions(source, func_range),
            )

    # Fallback: try macro extraction (BEGIN_LOADS, BEGIN_HANDLERS, etc.)
    result = _try_macro_extraction(source, file_path, func_name)
    if result is not None:
        return result

    macro_names = _find_macro_function_names(source)
    all_names = available + macro_names

    raise ValueError(
        f"Function '{func_name}' not found in {file_path}.\n"
        f"The function may not be implemented yet, or the name may differ from "
        f"the demangled symbol.\n"
        f"Available functions ({len(all_names)}):\n"
        + "\n".join(f"  - {name}" for name in all_names)
    )
