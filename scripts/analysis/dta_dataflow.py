#!/usr/bin/env python3
"""DTA dataflow analysis framework.

Uses tree-sitter to track DataArray* variables through the codebase,
propagating DTA context (config section + key path) from sources
(SystemConfig, DataReadFile) through assignments, function parameters,
and return values.

Phase 1: Intra-function analysis (variable tracking within each function)
Phase 2: Inter-procedural analysis (context propagation across function calls)

Usage:
  python3 scripts/analysis/dta_dataflow.py                    # full analysis
  python3 scripts/analysis/dta_dataflow.py --trace FILE        # trace one file
  python3 scripts/analysis/dta_dataflow.py --dump-graph        # show call graph
  python3 scripts/analysis/dta_dataflow.py --validate          # validate accesses
"""

from __future__ import annotations

import re
import sys
import os
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

import tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser, Node

# Import DTA hierarchy tools
sys.path.insert(0, str(Path(__file__).parent))
from dta_hierarchy_scan import parse_dta_file, DTANode, DTAHierarchy
from dta_access_audit import (
    _build_key_node_map, get_element_info, classify_atom
)

CPP_LANGUAGE = Language(tscpp.language())
_PARSER = Parser(CPP_LANGUAGE)

SKIP_DIRS = {
    'stlport', 'xdk', 'curl', '.git', 'build', 'orig', 'tools', 'powerpc',
    '__pycache__', 'node_modules', '.gemini', 'jpeg', 'oggvorbis', 'zlib',
}

SOURCE_EXTS = {'.cpp', '.c'}

_func_ast_cache: dict[str, 'Node'] = {}


# --------------------------------------------------------------------------
# Data model
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class DTAContext:
    """What DTA node a DataArray* variable points to."""
    config_section: str           # e.g., "rank", "" if unknown
    key_path: tuple[str, ...]     # e.g., ("tasks", "one_time")

    def child(self, key: str) -> DTAContext:
        """Return context after FindArray(key)."""
        return DTAContext(self.config_section, self.key_path + (key,))

    def __str__(self):
        parts = [self.config_section] + list(self.key_path)
        return ' → '.join(parts)


@dataclass
class AccessInfo:
    """A DTA access (FindArray, Int, Float, etc.) with context."""
    file: str
    line: int
    receiver_var: str
    method: str           # "FindArray", "Int", "Float", "Sym", "Str", "FindStr", etc.
    args: list[str]       # string key or int index
    context: Optional[DTAContext] = None  # resolved DTA context for receiver


@dataclass
class CallSite:
    """A function call where DataArray* is passed as an argument."""
    file: str
    line: int
    callee_name: str
    param_index: int      # which parameter position (0-based)
    context: Optional[DTAContext] = None


@dataclass
class FuncInfo:
    """Summary of a function's DataArray* usage."""
    name: str
    qualified_name: str   # Class::Method
    file: str
    line: int
    da_params: list[int]  # indices of DataArray* parameters
    da_param_names: dict[str, int] = field(default_factory=dict)  # param_name -> param_index
    accesses: list[AccessInfo] = field(default_factory=list)
    outgoing_calls: list[CallSite] = field(default_factory=list)


# --------------------------------------------------------------------------
# Tree-sitter AST helpers
# --------------------------------------------------------------------------

def node_text(node: Node) -> str:
    """Get the text content of a node."""
    return node.text.decode('utf-8')


def find_nodes(node: Node, type_name: str):
    """Find all descendant nodes of a given type."""
    if node.type == type_name:
        yield node
    for child in node.children:
        yield from find_nodes(child, type_name)


def find_first(node: Node, type_name: str) -> Optional[Node]:
    """Find first descendant of a given type."""
    for n in find_nodes(node, type_name):
        return n
    return None


def is_dataarray_type(type_node: Node) -> bool:
    """Check if a type node refers to DataArray."""
    text = node_text(type_node)
    return 'DataArray' in text


def extract_string_literal(node: Node) -> Optional[str]:
    """Extract string value from a string_literal node."""
    text = node_text(node)
    if text.startswith('"') and text.endswith('"'):
        return text[1:-1]
    return None


def get_function_name(func_node: Node) -> tuple[str, str]:
    """Extract (simple_name, qualified_name) from a function definition."""
    declarator = func_node.child_by_field_name('declarator')
    if declarator is None:
        return ('', '')

    # Handle function_declarator and reference_declarator wrappers
    while declarator.type in ('reference_declarator', 'pointer_declarator'):
        declarator = declarator.children[-1]

    if declarator.type == 'function_declarator':
        name_node = declarator.child_by_field_name('declarator')
        if name_node:
            qualified = node_text(name_node)
            # Extract simple name from qualified (Class::Method -> Method)
            simple = qualified.split('::')[-1] if '::' in qualified else qualified
            return (simple, qualified)

    return ('', '')


def get_param_types(func_node: Node) -> list[tuple[int, str, bool]]:
    """Get parameter info: [(index, name, is_dataarray), ...]"""
    declarator = func_node.child_by_field_name('declarator')
    if declarator is None:
        return []

    while declarator.type in ('reference_declarator', 'pointer_declarator'):
        declarator = declarator.children[-1]

    if declarator.type != 'function_declarator':
        return []

    params_node = declarator.child_by_field_name('parameters')
    if params_node is None:
        return []

    result = []
    idx = 0
    for child in params_node.children:
        if child.type == 'parameter_declaration':
            type_node = child.child_by_field_name('type')
            decl_node = child.child_by_field_name('declarator')
            is_da = type_node is not None and is_dataarray_type(type_node)
            name = ''
            if decl_node:
                # Strip pointer/reference declarators
                n = decl_node
                while n.type in ('pointer_declarator', 'reference_declarator'):
                    if n.children:
                        n = n.children[-1]
                    else:
                        break
                name = node_text(n)
            result.append((idx, name, is_da))
            idx += 1

    return result


# --------------------------------------------------------------------------
# Intra-function DTA context analysis
# --------------------------------------------------------------------------

def analyze_function(func_node: Node, filepath: str) -> FuncInfo:
    """Analyze a single function for DataArray* flow."""
    simple_name, qualified_name = get_function_name(func_node)
    params = get_param_types(func_node)
    da_param_indices = [idx for idx, name, is_da in params if is_da]
    da_param_names = {name: idx for idx, name, is_da in params if is_da and name}

    func_info = FuncInfo(
        name=simple_name,
        qualified_name=qualified_name,
        file=filepath,
        line=func_node.start_point[0] + 1,
        da_params=da_param_indices,
        da_param_names=da_param_names,
    )

    body = func_node.child_by_field_name('body')
    if body is None:
        return func_info

    # Track variable -> DTAContext mapping
    var_context: dict[str, DTAContext] = {}

    # Initialize param contexts (will be filled by inter-procedural phase)
    for name, idx in da_param_names.items():
        var_context[name] = None  # placeholder

    # Walk all statements in the function body
    _analyze_block(body, var_context, func_info, filepath)

    return func_info


def analyze_function_with_context(func_node, filepath, param_contexts):
    """Re-analyze a function with known parameter contexts."""
    simple_name, qualified_name = get_function_name(func_node)
    params = get_param_types(func_node)
    da_param_indices = [idx for idx, name, is_da in params if is_da]
    da_param_names = {name: idx for idx, name, is_da in params if is_da and name}

    func_info = FuncInfo(
        name=simple_name, qualified_name=qualified_name,
        file=filepath, line=func_node.start_point[0] + 1,
        da_params=da_param_indices, da_param_names=da_param_names,
    )

    body = func_node.child_by_field_name('body')
    if body is None:
        return func_info

    var_context = {}
    for name in da_param_names:
        var_context[name] = param_contexts.get(name)

    _analyze_block(body, var_context, func_info, filepath)
    return func_info


def _collect_static_symbols(block_node: Node) -> dict[str, str]:
    """Scan a block for `static Symbol X("Y")` or `Symbol X("Y")` declarations.
    Returns dict mapping variable name to string value.
    """
    result = {}
    for stmt in block_node.children:
        if stmt.type != 'declaration':
            continue
        text = node_text(stmt)
        # Match: [static] Symbol name("value") or [static] Symbol name("value", ...)
        m = re.search(r'(?:static\s+)?Symbol\s+(\w+)\s*\(\s*"([^"]*)"', text)
        if m:
            result[m.group(1)] = m.group(2)
    return result


def _analyze_block(block_node: Node, var_context: dict, func_info: FuncInfo, filepath: str):
    """Analyze a block of statements for DTA flow."""
    # Build static Symbol lookup table for this block
    symbol_values = _collect_static_symbols(block_node)
    # Store in var_context under a special key so nested calls can access it
    if '_symbol_values' not in var_context:
        var_context['_symbol_values'] = {}
    var_context['_symbol_values'].update(symbol_values)

    for stmt in block_node.children:
        if stmt.type in ('{', '}', 'comment'):
            continue
        _analyze_statement(stmt, var_context, func_info, filepath)


def _analyze_statement(stmt: Node, var_context: dict, func_info: FuncInfo, filepath: str):
    """Analyze a single statement for DTA flow."""
    if stmt.type == 'declaration':
        _analyze_declaration(stmt, var_context, func_info, filepath)
    elif stmt.type == 'expression_statement':
        expr = stmt.children[0] if stmt.children else None
        if expr:
            _analyze_expression(expr, var_context, func_info, filepath)
    elif stmt.type == 'compound_statement':
        _analyze_block(stmt, var_context, func_info, filepath)
    elif stmt.type in ('if_statement', 'while_statement', 'for_statement',
                       'do_statement', 'switch_statement'):
        # Recurse into sub-blocks
        for child in stmt.children:
            if child.type == 'compound_statement':
                _analyze_block(child, var_context, func_info, filepath)
            elif child.type not in ('(', ')', 'if', 'while', 'for', 'do',
                                     'switch', 'else', ';'):
                _analyze_statement(child, var_context, func_info, filepath)
    elif stmt.type == 'return_statement':
        for child in stmt.children:
            if child.type not in ('return', ';'):
                _analyze_expression(child, var_context, func_info, filepath)


def _analyze_declaration(decl: Node, var_context: dict, func_info: FuncInfo, filepath: str):
    """Analyze a variable declaration for DTA assignments."""
    # Look for: DataArray *var = expr;
    for child in decl.children:
        if child.type == 'init_declarator':
            value_node = child.child_by_field_name('value')
            decl_node = child.child_by_field_name('declarator')
            if value_node and decl_node:
                # Get variable name (strip * prefix)
                var_name = node_text(decl_node).lstrip('*').strip()
                ctx = _resolve_expr_context(value_node, var_context, func_info, filepath)
                if ctx is not None:
                    var_context[var_name] = ctx

                # Also record accesses in the initializer
                _record_accesses(value_node, var_context, func_info, filepath)


def _analyze_expression(expr: Node, var_context: dict, func_info: FuncInfo, filepath: str):
    """Analyze an expression for DTA assignments and accesses."""
    if expr.type == 'assignment_expression':
        left = expr.child_by_field_name('left')
        right = expr.child_by_field_name('right')
        if left and right:
            var_name = node_text(left).strip()
            ctx = _resolve_expr_context(right, var_context, func_info, filepath)
            if ctx is not None:
                var_context[var_name] = ctx
            _record_accesses(right, var_context, func_info, filepath)
    else:
        _record_accesses(expr, var_context, func_info, filepath)


def _resolve_expr_context(expr: Node, var_context: dict,
                           func_info: FuncInfo, filepath: str) -> Optional[DTAContext]:
    """Resolve the DTA context of an expression.

    Returns DTAContext if the expression produces a DataArray* with known context.
    """
    text = node_text(expr)

    # SystemConfig() with no args — returns root config
    if re.match(r'SystemConfig\(\s*\)', text):
        return DTAContext(config_section='__ROOT__', key_path=())

    # SystemConfig("section") or SystemConfig("section", "subsection", ...)
    m = re.match(r'SystemConfig\((.+)\)', text, re.DOTALL)
    if m:
        # Extract all string literal arguments
        args_text = m.group(1)
        keys = re.findall(r'"([^"]+)"', args_text)
        if keys:
            return DTAContext(
                config_section=keys[0],
                key_path=tuple(keys[1:])  # additional args are key path
            )
        # Try Symbol variable resolution
        symbol_values = var_context.get('_symbol_values', {})
        # Extract first identifier argument
        arg_m = re.match(r'\s*(\w+)', args_text)
        if arg_m and arg_m.group(1) in symbol_values:
            resolved = symbol_values[arg_m.group(1)]
            return DTAContext(config_section=resolved, key_path=())

    # expr->FindArray("key") or expr->FindArray("key", true/false)
    if expr.type == 'call_expression':
        func_part = expr.child_by_field_name('function')
        args_part = expr.child_by_field_name('arguments')

        if func_part and func_part.type == 'field_expression':
            field_node = func_part.child_by_field_name('field')
            arg_node = func_part.child_by_field_name('argument')

            if field_node and node_text(field_node) == 'FindArray':
                # Get the key argument
                if args_part:
                    key = None
                    for arg_child in args_part.children:
                        if arg_child.type == 'string_literal':
                            key = extract_string_literal(arg_child)
                            break
                    # Fallback: try Symbol variable resolution
                    if key is None:
                        symbol_values = var_context.get('_symbol_values', {})
                        for arg_child in args_part.children:
                            if arg_child.type == 'identifier':
                                sym_name = node_text(arg_child)
                                if sym_name in symbol_values:
                                    key = symbol_values[sym_name]
                                    break
                    if key:
                        # Resolve parent context
                        parent_ctx = None
                        if arg_node:
                            parent_var = node_text(arg_node).strip()
                            parent_ctx = var_context.get(parent_var)
                            if parent_ctx is None:
                                # Try resolving the argument expression
                                parent_ctx = _resolve_expr_context(
                                    arg_node, var_context, func_info, filepath
                                )
                        if parent_ctx:
                            return parent_ctx.child(key)

    # Variable reference
    if expr.type == 'identifier':
        var_name = node_text(expr)
        return var_context.get(var_name)

    # Chained call: expr->FindArray("key")->FindArray("key2")
    if expr.type == 'call_expression':
        func_part = expr.child_by_field_name('function')
        if func_part and func_part.type == 'field_expression':
            arg_node = func_part.child_by_field_name('argument')
            if arg_node:
                # The argument might be another call expression
                return _resolve_expr_context(arg_node, var_context, func_info, filepath)

    return None


def _record_accesses(expr: Node, var_context: dict,
                      func_info: FuncInfo, filepath: str):
    """Record all DTA access points in an expression."""
    # Find all call expressions in the tree
    for call in find_nodes(expr, 'call_expression'):
        func_part = call.child_by_field_name('function')
        args_part = call.child_by_field_name('arguments')

        if func_part is None or func_part.type != 'field_expression':
            # Check for plain function calls that pass DataArray*
            if func_part and args_part:
                callee_name = node_text(func_part)
                # Record calls that pass DataArray* variables
                real_idx = 0
                for arg_child in args_part.children:
                    if arg_child.type in ('(', ')', ','):
                        continue
                    arg_text = node_text(arg_child).strip()
                    if arg_text in var_context:
                        func_info.outgoing_calls.append(CallSite(
                            file=filepath,
                            line=call.start_point[0] + 1,
                            callee_name=callee_name,
                            param_index=real_idx,
                            context=var_context.get(arg_text),
                        ))
                    real_idx += 1
            continue

        field_node = func_part.child_by_field_name('field')
        arg_node = func_part.child_by_field_name('argument')

        if field_node is None or arg_node is None:
            continue

        method_name = node_text(field_node)
        receiver_text = node_text(arg_node).strip()

        # Record accessor calls: Int(N), Float(N), Sym(N), Str(N)
        if method_name in ('Int', 'Float', 'Sym', 'Str', 'Array'):
            args = []
            if args_part:
                for a in args_part.children:
                    if a.type == 'number_literal':
                        args.append(node_text(a))

            # Resolve receiver context
            receiver_ctx = var_context.get(receiver_text)
            if receiver_ctx is None:
                receiver_ctx = _resolve_expr_context(arg_node, var_context,
                                                      func_info, filepath)

            func_info.accesses.append(AccessInfo(
                file=filepath,
                line=call.start_point[0] + 1,
                receiver_var=receiver_text,
                method=method_name,
                args=args,
                context=receiver_ctx,
            ))

        # Record FindArray/FindStr/FindFloat/FindInt/FindData calls
        elif method_name in ('FindArray', 'FindStr', 'FindFloat', 'FindInt',
                             'FindData', 'FindSym', 'FindVar'):
            args = []
            if args_part:
                for a in args_part.children:
                    if a.type == 'string_literal':
                        s = extract_string_literal(a)
                        if s:
                            args.append(s)
            # Fallback: try Symbol variable resolution for the key
            if not args:
                symbol_values = var_context.get('_symbol_values', {})
                if args_part:
                    for a in args_part.children:
                        if a.type == 'identifier':
                            sym_name = node_text(a)
                            if sym_name in symbol_values:
                                args.append(symbol_values[sym_name])
                                break

            receiver_ctx = var_context.get(receiver_text)
            if receiver_ctx is None:
                receiver_ctx = _resolve_expr_context(arg_node, var_context,
                                                      func_info, filepath)

            func_info.accesses.append(AccessInfo(
                file=filepath,
                line=call.start_point[0] + 1,
                receiver_var=receiver_text,
                method=method_name,
                args=args,
                context=receiver_ctx,
            ))

        else:
            # Track arbitrary method calls that pass DataArray* variables
            if args_part:
                real_idx = 0
                for a in args_part.children:
                    if a.type in ('(', ')', ','):
                        continue
                    arg_text = node_text(a).strip()
                    if arg_text in var_context:
                        func_info.outgoing_calls.append(CallSite(
                            file=filepath,
                            line=call.start_point[0] + 1,
                            callee_name=method_name,
                            param_index=real_idx,
                            context=var_context.get(arg_text),
                        ))
                    real_idx += 1


# --------------------------------------------------------------------------
# File-scoped global and member variable propagation
# --------------------------------------------------------------------------

def _propagate_file_globals(functions: list[FuncInfo], root_node: Node, filepath: str):
    """Propagate DTA context from file-scoped globals to all functions that use them.

    Pattern: Anonymous namespace or file-scope DataArray* globals assigned in Init functions,
    used in other functions in the same file.

    Example from MetagameRank.cpp:
        namespace { DataArray *gOneTimeTasks; }
        void MetagameRank::Init() { gOneTimeTasks = taskArr->FindArray("one_time"); }
        void MetagameRank::Check() { gOneTimeTasks->FindArray("score"); }  // needs context
    """
    # Step 1: Find global DataArray* variable names from AST
    global_names = set()
    for node in root_node.children:
        if node.type == 'declaration':
            text = node_text(node)
            if 'DataArray' in text:
                m = re.search(r'DataArray\s*\*\s*(\w+)', text)
                if m:
                    global_names.add(m.group(1))
        elif node.type == 'namespace_definition':
            # Anonymous namespace: namespace { DataArray *gFoo; }
            body = node.child_by_field_name('body')
            if body:
                for child in body.children:
                    if child.type == 'declaration':
                        text = node_text(child)
                        if 'DataArray' in text:
                            m = re.search(r'DataArray\s*\*\s*(\w+)', text)
                            if m:
                                global_names.add(m.group(1))

    if not global_names:
        return

    # Step 2: Find which functions assign to these globals with resolved context
    global_contexts: dict[str, DTAContext] = {}

    for func in functions:
        ast_node = _func_ast_cache.get(func.qualified_name)
        if ast_node is None:
            continue

        # Quick check: does this function's source mention any global names?
        func_text = node_text(ast_node)
        relevant_globals = [g for g in global_names if g in func_text]
        if not relevant_globals:
            continue

        body = ast_node.child_by_field_name('body')
        if body is None:
            continue

        # Re-analyze to capture global assignments
        var_context: dict[str, Optional[DTAContext]] = {}
        params = get_param_types(ast_node)
        da_param_names = {name: idx for idx, name, is_da in params if is_da and name}
        for name in da_param_names:
            var_context[name] = None

        temp_func = FuncInfo(
            name=func.name, qualified_name=func.qualified_name,
            file=filepath, line=func.line, da_params=func.da_params,
            da_param_names=func.da_param_names,
        )
        _analyze_block(body, var_context, temp_func, filepath)

        # Check if any globals got context
        for gname in relevant_globals:
            if gname in var_context and var_context[gname] is not None:
                global_contexts[gname] = var_context[gname]

    if not global_contexts:
        return

    # Step 3: Re-analyze functions that USE these globals (inject context)
    for func in functions:
        ast_node = _func_ast_cache.get(func.qualified_name)
        if ast_node is None:
            continue

        func_text = node_text(ast_node)
        used_globals = {g: ctx for g, ctx in global_contexts.items() if g in func_text}
        if not used_globals:
            continue

        body = ast_node.child_by_field_name('body')
        if body is None:
            continue

        # Re-analyze with global contexts pre-seeded
        var_context: dict[str, Optional[DTAContext]] = dict(used_globals)
        params = get_param_types(ast_node)
        da_param_names = {name: idx for idx, name, is_da in params if is_da and name}
        for name in da_param_names:
            if name not in var_context:
                var_context[name] = None

        temp_func = FuncInfo(
            name=func.name, qualified_name=func.qualified_name,
            file=filepath, line=func.line, da_params=func.da_params,
            da_param_names=func.da_param_names,
        )
        _analyze_block(body, var_context, temp_func, filepath)

        # Merge newly resolved accesses into the original func
        existing_resolved = {(a.line, a.method, tuple(a.args))
                             for a in func.accesses if a.context is not None}
        for access in temp_func.accesses:
            if access.context is None:
                continue
            key = (access.line, access.method, tuple(access.args))
            if key not in existing_resolved:
                for orig in func.accesses:
                    if (orig.line == access.line and
                        orig.method == access.method and
                        orig.args == access.args and
                        orig.context is None):
                        orig.context = access.context
                        break


def _propagate_member_variables(functions: list[FuncInfo], root_node: Node, filepath: str):
    """Propagate DTA context from member variables set in constructors/Init to other methods.

    Pattern: mField = SystemConfig("X")->FindArray("Y") in constructor,
    then mField->FindArray("Z") in other methods.
    """
    # Group functions by class (using qualified name prefix)
    by_class: dict[str, list[FuncInfo]] = defaultdict(list)
    for func in functions:
        if '::' in func.qualified_name:
            class_name = func.qualified_name.rsplit('::', 1)[0]
            by_class[class_name].append(func)

    for class_name, class_funcs in by_class.items():
        # Find constructor and Init methods
        init_funcs = []
        other_funcs = []
        for func in class_funcs:
            method_name = func.qualified_name.rsplit('::', 1)[1]
            simple_class = class_name.rsplit('::', 1)[-1] if '::' in class_name else class_name
            if method_name == simple_class or method_name == 'Init' or method_name == 'PreInit':
                init_funcs.append(func)
            else:
                other_funcs.append(func)

        if not init_funcs or not other_funcs:
            continue

        # Analyze init functions to find member assignments
        member_contexts: dict[str, DTAContext] = {}
        for func in init_funcs:
            ast_node = _func_ast_cache.get(func.qualified_name)
            if ast_node is None:
                continue
            body = ast_node.child_by_field_name('body')
            if body is None:
                continue

            # Re-analyze to capture member assignments (mFoo = ...)
            var_context: dict[str, Optional[DTAContext]] = {}
            params = get_param_types(ast_node)
            da_param_names = {name: idx for idx, name, is_da in params if is_da and name}
            for name in da_param_names:
                var_context[name] = None

            temp_func = FuncInfo(
                name=func.name, qualified_name=func.qualified_name,
                file=filepath, line=func.line, da_params=func.da_params,
                da_param_names=func.da_param_names,
            )
            _analyze_block(body, var_context, temp_func, filepath)

            # Collect member assignments: variables starting with 'm' followed by uppercase
            for var_name, ctx in var_context.items():
                if var_name.startswith('_'):
                    continue  # skip internal keys like _symbol_values
                if ctx is not None and len(var_name) > 1 and var_name[0] == 'm' and var_name[1].isupper():
                    member_contexts[var_name] = ctx

        if not member_contexts:
            continue

        # Re-analyze other methods with member contexts injected
        for func in other_funcs:
            ast_node = _func_ast_cache.get(func.qualified_name)
            if ast_node is None:
                continue

            func_text = node_text(ast_node)
            used_members = {m: ctx for m, ctx in member_contexts.items() if m in func_text}
            if not used_members:
                continue

            body = ast_node.child_by_field_name('body')
            if body is None:
                continue

            var_context: dict[str, Optional[DTAContext]] = dict(used_members)
            params = get_param_types(ast_node)
            da_param_names = {name: idx for idx, name, is_da in params if is_da and name}
            for name in da_param_names:
                if name not in var_context:
                    var_context[name] = None

            temp_func = FuncInfo(
                name=func.name, qualified_name=func.qualified_name,
                file=filepath, line=func.line, da_params=func.da_params,
                da_param_names=func.da_param_names,
            )
            _analyze_block(body, var_context, temp_func, filepath)

            # Merge newly resolved accesses into the original func
            existing_resolved = {(a.line, a.method, tuple(a.args))
                                 for a in func.accesses if a.context is not None}
            for access in temp_func.accesses:
                if access.context is None:
                    continue
                key = (access.line, access.method, tuple(access.args))
                if key not in existing_resolved:
                    for orig in func.accesses:
                        if (orig.line == access.line and
                            orig.method == access.method and
                            orig.args == access.args and
                            orig.context is None):
                            orig.context = access.context
                            break


# --------------------------------------------------------------------------
# File and project analysis
# --------------------------------------------------------------------------

def analyze_file(filepath: str) -> list[FuncInfo]:
    """Analyze all functions in a single file."""
    try:
        with open(filepath, 'rb') as f:
            source = f.read()
    except (IOError, OSError):
        return []

    tree = _PARSER.parse(source)
    root = tree.root_node

    functions = []
    for node in root.children:
        if node.type == 'function_definition':
            func = analyze_function(node, filepath)
            if func.name:  # skip unnamed
                functions.append(func)
                _func_ast_cache[func.qualified_name] = node

    # Phase 2: File-scoped global propagation
    _propagate_file_globals(functions, root, filepath)

    # Phase 3: Member variable propagation
    _propagate_member_variables(functions, root, filepath)

    return functions


def analyze_project(src_dir: str) -> dict[str, list[FuncInfo]]:
    """Analyze all source files in a project."""
    result = {}
    src_path = Path(src_dir)

    for filepath in sorted(src_path.rglob('*.cpp')):
        if any(skip in filepath.parts for skip in SKIP_DIRS):
            continue
        functions = analyze_file(str(filepath))
        if functions:
            result[str(filepath)] = functions

    return result


# --------------------------------------------------------------------------
# Inter-procedural context propagation
# --------------------------------------------------------------------------

def build_call_graph(project: dict[str, list[FuncInfo]]) -> dict[str, list[CallSite]]:
    """Build callee_name -> [CallSite] mapping."""
    graph = defaultdict(list)
    for filepath, functions in project.items():
        for func in functions:
            for call in func.outgoing_calls:
                graph[call.callee_name].append(call)
    return graph


def propagate_contexts(project: dict[str, list[FuncInfo]],
                       call_graph: dict[str, list[CallSite]],
                       max_iterations: int = 3):
    """Propagate DTA contexts across function boundaries."""
    func_lookup = {}
    for filepath, functions in project.items():
        for func in functions:
            func_lookup[func.qualified_name] = func
            if func.name not in func_lookup:
                func_lookup[func.name] = func

    changed = True
    iteration = 0
    while changed and iteration < max_iterations:
        changed = False
        iteration += 1
        resolved_this_iter = 0

        for callee_name, call_sites in list(call_graph.items()):
            func = func_lookup.get(callee_name)
            if func is None or not func.da_params:
                continue

            ast_node = _func_ast_cache.get(func.qualified_name)
            if ast_node is None:
                continue

            # Collect contexts for each parameter from all call sites
            param_context_sets = defaultdict(set)
            for call in call_sites:
                if call.context is None:
                    continue
                for pname, pidx in func.da_param_names.items():
                    if pidx == call.param_index:
                        param_context_sets[pname].add(call.context)

            if not param_context_sets:
                continue

            # Re-analyze with each unique context
            seen_contexts = set()
            for pname, contexts in param_context_sets.items():
                for ctx in contexts:
                    ctx_key = (pname, ctx)
                    if ctx_key in seen_contexts:
                        continue
                    seen_contexts.add(ctx_key)

                    param_ctx = {pname: ctx}
                    for other_name, other_ctxs in param_context_sets.items():
                        if other_name != pname and other_ctxs:
                            param_ctx[other_name] = next(iter(other_ctxs))

                    result = analyze_function_with_context(ast_node, func.file, param_ctx)

                    # Update unresolved accesses with newly resolved context
                    existing_resolved = {(a.line, a.method, tuple(a.args))
                                         for a in func.accesses if a.context is not None}
                    for access in result.accesses:
                        if access.context is None:
                            continue
                        key = (access.line, access.method, tuple(access.args))
                        if key not in existing_resolved:
                            for orig in func.accesses:
                                if (orig.line == access.line and
                                    orig.method == access.method and
                                    orig.args == access.args and
                                    orig.context is None):
                                    orig.context = access.context
                                    resolved_this_iter += 1
                                    changed = True
                                    break

                    # Add new outgoing calls for transitive propagation
                    for call in result.outgoing_calls:
                        if call.context is not None:
                            existing = call_graph.get(call.callee_name, [])
                            if not any(s.context == call.context and
                                       s.param_index == call.param_index
                                       for s in existing):
                                call_graph[call.callee_name].append(call)
                                changed = True

        print(f"  Phase 2 iteration {iteration}: resolved {resolved_this_iter} accesses",
              file=sys.stderr)


# Helper for inter-procedural
def get_param_types_from_func(func: FuncInfo):
    """Reconstruct param info from FuncInfo (simplified)."""
    return [(i, f'param{i}', i in func.da_params) for i in range(max(func.da_params) + 1 if func.da_params else 0)]


# --------------------------------------------------------------------------
# Validation against DTA hierarchy
# --------------------------------------------------------------------------

def validate_accesses(project: dict[str, list[FuncInfo]],
                      hierarchy: DTAHierarchy,
                      main_roots: dict,
                      all_key_nodes: dict) -> list[dict]:
    """Validate all DTA accesses against the hierarchy."""
    findings = []

    for filepath, functions in project.items():
        for func in functions:
            for access in func.accesses:
                finding = _validate_single_access(access, hierarchy, main_roots, all_key_nodes)
                if finding:
                    findings.append(finding)

    return findings


def _validate_single_access(access: AccessInfo, hierarchy: DTAHierarchy,
                              main_roots: dict, all_key_nodes: dict) -> Optional[dict]:
    """Validate a single DTA access."""
    if access.context is None:
        return None  # Can't validate without context

    # Resolve the DTA node for this context
    dta_node = _resolve_context_to_node(access.context, main_roots)
    if dta_node is None:
        return None  # Can't resolve — might be dynamically constructed

    # Validate based on method type
    if access.method in ('FindArray', 'FindStr', 'FindFloat', 'FindInt',
                          'FindData', 'FindSym', 'FindVar'):
        if access.args:
            key = access.args[0]
            # Try finding the key in ANY root's version of this context
            child = _find_key_in_any_root(access.context, key, main_roots)
            if child is None:
                child = dta_node.find_array(key) if dta_node else None
            if child is None:
                # Key not found in any merged config — check hierarchy
                parents = hierarchy.get_parents(key)
                if parents:
                    actual = parents - {'__ROOT__'}
                    # Filter: skip if key IS listed as child of our context section
                    # (indicates a merge issue, not a real bug)
                    ctx_section = access.context.config_section
                    if ctx_section in parents or ctx_section == '__ROOT__':
                        return None  # Key exists in this section in another root
                    return {
                        'file': access.file,
                        'line': access.line,
                        'type': 'wrong_depth',
                        'severity': 'HIGH',
                        'context': str(access.context),
                        'detail': f'{access.method}("{key}") — key exists as child of '
                                  f'{actual}, not at {access.context}',
                    }
                # Key doesn't exist anywhere — skip (might be optional)

    elif access.method in ('Int', 'Float', 'Sym', 'Str', 'Array'):
        if access.args:
            try:
                index = int(access.args[0])
            except ValueError:
                return None

            elem_info = get_element_info(dta_node, index)
            if elem_info is None:
                total = 1 + len(dta_node.children)
                return {
                    'file': access.file,
                    'line': access.line,
                    'type': 'index_oob',
                    'severity': 'HIGH',
                    'context': str(access.context),
                    'detail': f'{access.method}({index}) on [{access.context}] — '
                              f'array has {total} elements (0..{total-1})',
                }

            # Type check
            actual_type, actual_value = elem_info
            expected = {'Int': 'int', 'Float': 'float', 'Sym': 'symbol', 'Str': 'string'}.get(access.method)
            if expected:
                numeric = {'int', 'float'}
                textual = {'symbol', 'string'}
                if (actual_type in numeric and expected in textual) or \
                   (actual_type in textual and expected in numeric):
                    return {
                        'file': access.file,
                        'line': access.line,
                        'type': 'type_mismatch',
                        'severity': 'MEDIUM',
                        'context': str(access.context),
                        'detail': f'{access.method}({index}) on [{access.context}] — '
                                  f'element is {actual_type} ({actual_value}), '
                                  f'accessed as {expected}',
                    }

    return None


def _resolve_context_to_node(ctx: DTAContext, main_roots: dict) -> Optional[DTANode]:
    """Resolve a DTAContext to an actual DTANode.

    Tries all main config roots since the runtime config is a merge of
    default.dta and ham_keep.dta. Returns first successful resolution.
    """
    # No-arg SystemConfig returns the root config
    if ctx.config_section == '__ROOT__':
        for root in main_roots.values():
            node = root
            success = True
            for key in ctx.key_path:
                child = node.find_array(key)
                if child is None:
                    success = False
                    break
                node = child
            if success:
                return node
        return None

    for root in main_roots.values():
        section = root.find_array(ctx.config_section)
        if section is None:
            continue
        node = section
        success = True
        for key in ctx.key_path:
            child = node.find_array(key)
            if child is None:
                success = False
                break
            node = child
        if success:
            return node
    return None


def _find_key_in_any_root(ctx: DTAContext, key: str, main_roots: dict) -> Optional[DTANode]:
    """Find a child key under any root's resolution of the context.

    Since configs are merged at runtime, a key might exist in one root
    but not another.
    """
    # Handle __ROOT__ config section (no-arg SystemConfig)
    if ctx.config_section == '__ROOT__':
        for root in main_roots.values():
            node = root
            for path_key in ctx.key_path:
                child = node.find_array(path_key)
                if child is None:
                    node = None
                    break
                node = child
            if node is not None:
                child = node.find_array(key)
                if child is not None:
                    return child
        return None

    for root in main_roots.values():
        section = root.find_array(ctx.config_section)
        if section is None:
            continue
        node = section
        for path_key in ctx.key_path:
            child = node.find_array(path_key)
            if child is None:
                node = None
                break
            node = child
        if node is not None:
            child = node.find_array(key)
            if child is not None:
                return child
    return None


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main():
    import argparse
    parser = argparse.ArgumentParser(description='DTA dataflow analysis')
    parser.add_argument('--src-dir', default='src/',
                        help='Source directory')
    parser.add_argument('--trace', type=str,
                        help='Trace a specific file')
    parser.add_argument('--validate', action='store_true', default=True,
                        help='Validate accesses against DTA hierarchy')
    parser.add_argument('--dump-graph', action='store_true',
                        help='Show call graph')
    parser.add_argument('--stats', action='store_true',
                        help='Show analysis statistics')
    parser.add_argument('--json', action='store_true',
                        help='Output as JSON')
    args = parser.parse_args()

    # Load DTA hierarchy
    main_configs = {
        'ham': 'orig-assets/extracted/config/ham_keep.dta',
        'default': 'orig-assets/extracted/(..)/(..)/system/run/config/default.dta',
    }
    main_roots = {}
    hierarchy = DTAHierarchy()
    for name, cfg in main_configs.items():
        p = Path(cfg)
        if p.exists():
            root = parse_dta_file(str(p))
            if root:
                main_roots[cfg] = root
                hierarchy.add_file(p)
    for f in Path('orig-assets/extracted').rglob('*.dta'):
        if str(f) not in hierarchy.roots:
            hierarchy.add_file(f)

    all_key_nodes = _build_key_node_map(main_roots)
    print(f"DTA: {len(hierarchy.key_parents)} keys, {len(all_key_nodes)} node entries",
          file=sys.stderr)

    # Analyze source
    if args.trace:
        project = {args.trace: analyze_file(args.trace)}
    else:
        project = analyze_project(args.src_dir)

    # Collect stats
    total_funcs = sum(len(funcs) for funcs in project.values())
    total_da_funcs = sum(1 for funcs in project.values()
                         for f in funcs if f.da_params)
    total_accesses = sum(len(f.accesses) for funcs in project.values() for f in funcs)
    resolved_accesses = sum(1 for funcs in project.values()
                            for f in funcs for a in f.accesses if a.context)
    total_calls = sum(len(f.outgoing_calls) for funcs in project.values() for f in funcs)

    print(f"Analyzed {len(project)} files, {total_funcs} functions",
          file=sys.stderr)
    print(f"  DataArray* params: {total_da_funcs} functions",
          file=sys.stderr)
    print(f"  DTA accesses: {total_accesses} total, {resolved_accesses} with context "
          f"({resolved_accesses*100//max(total_accesses,1)}%)",
          file=sys.stderr)
    print(f"  Outgoing DA calls: {total_calls}", file=sys.stderr)

    # Phase 2: Inter-procedural context propagation
    if not args.stats and not args.dump_graph:
        call_graph = build_call_graph(project)
        propagate_contexts(project, call_graph)
        resolved_accesses = sum(1 for funcs in project.values()
                                for f in funcs for a in f.accesses if a.context)
        print(f"  After Phase 2: {resolved_accesses} with context "
              f"({resolved_accesses*100//max(total_accesses,1)}%)", file=sys.stderr)

    if args.stats:
        # Show detailed stats
        print(f"\n=== Functions with most resolved accesses ===")
        ranked = []
        for filepath, functions in project.items():
            for func in functions:
                resolved = sum(1 for a in func.accesses if a.context)
                if resolved > 0:
                    ranked.append((resolved, len(func.accesses), func))
        ranked.sort(key=lambda x: (x[0], x[1]), reverse=True)
        for resolved, total, func in ranked[:20]:
            print(f"  {resolved}/{total} resolved  {func.qualified_name} ({func.file}:{func.line})")

        # Show context distribution
        print(f"\n=== DTA context distribution ===")
        ctx_counts = defaultdict(int)
        for funcs in project.values():
            for func in funcs:
                for access in func.accesses:
                    if access.context:
                        ctx_counts[access.context.config_section] += 1
        for section, count in sorted(ctx_counts.items(), key=lambda x: -x[1]):
            print(f"  {section}: {count} accesses")
        return

    if args.dump_graph:
        call_graph = build_call_graph(project)
        print(f"\n=== Call graph ({len(call_graph)} callees) ===")
        for callee, sites in sorted(call_graph.items(), key=lambda x: -len(x[1])):
            if len(sites) >= 2:
                contexts = [str(s.context) for s in sites if s.context]
                print(f"  {callee}: {len(sites)} call sites"
                      f"{' — contexts: ' + ', '.join(set(contexts)) if contexts else ''}")
        return

    # Validate
    if args.validate:
        findings = validate_accesses(project, hierarchy, main_roots, all_key_nodes)

        if not findings:
            print("No DTA access issues found.")
            return

        if args.json:
            import json
            print(json.dumps(findings, indent=2))
            return

        by_type = defaultdict(list)
        for f in findings:
            by_type[f['type']].append(f)

        for bug_type, items in sorted(by_type.items()):
            severity = items[0]['severity']
            print(f"\n=== {bug_type.upper()} [{severity}] ({len(items)} findings) ===\n")
            for item in items:
                print(f"  {item['file']}:{item['line']}")
                print(f"    Context: {item['context']}")
                print(f"    {item['detail']}")
                print()

        total = sum(len(v) for v in by_type.values())
        print(f"Total: {total} findings")


if __name__ == '__main__':
    main()
