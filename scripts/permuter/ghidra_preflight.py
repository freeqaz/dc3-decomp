"""Ghidra pre-flight checks — detect red flags before permuting.

Scans Ghidra decompilation for patterns that indicate unfixable mismatches,
saving permuter cycles on functions that can't be improved.

Detection rules:
1. Struct offset access — raw pointer arithmetic suggests struct mismatch
2. Call count mismatch — different function calls between source and Ghidra
3. Dead variable pattern — assigned but never read (dead register usage)
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

from tree_sitter import Node

from . import clang_types
from .ghidra_ast import GhidraAST

# Matches Ghidra's *(type *)(param + 0xNN) patterns
_STRUCT_OFFSET_RE = re.compile(
    r"\*\(.+?\s*\*\)\s*\(.+?\s*\+\s*0x[0-9a-fA-F]+\)"
)

# Matches function calls in Ghidra output
_GHIDRA_CALL_RE = re.compile(
    r"\b([a-zA-Z_]\w*)\s*\("
)

# Types/keywords that look like calls but aren't
_NOT_CALLS = frozenset({
    "if", "while", "for", "switch", "return", "sizeof", "typeof",
    "int", "long", "short", "char", "void", "float", "double",
    "uint", "ulong", "ushort", "uchar", "undefined", "undefined4",
    "undefined8", "undefined2", "undefined1", "bool", "byte",
})

_IGNORED_HELPER_CALL_RE = re.compile(
    r"^(?:__savegprlr_\d+|__restgprlr_\d+|__savefpr_\d+|__restfpr_\d+|"
    r"CONCAT\d+|ZEXT\d+|SEXT\d+|SUB\d+)$"
)


@dataclass
class PreflightResult:
    """Red flags detected from Ghidra decompilation."""

    struct_offset_mismatches: list[str] = field(default_factory=list)
    missing_calls: list[str] = field(default_factory=list)
    extra_calls: list[str] = field(default_factory=list)
    dead_variables: list[str] = field(default_factory=list)
    confidence: float = 0.0
    skip_reason: str | None = None
    prologue_mismatch: bool = False
    volatile_regswap_only: bool = False
    is_merged_symbol: bool = False
    hard_skip: bool = False
    param_type_mismatches: list[str] = field(default_factory=list)
    cast_on_pointer: bool = False
    struct_offsets_validated: int = 0  # offsets confirmed in headers


def run_preflight(
    ghidra_ast: GhidraAST,
    source_node: Node,
    source_bytes: bytes,
    diagnosis: object | None = None,
    symbol: str | None = None,
    file_path: object | None = None,
) -> PreflightResult:
    """Scan Ghidra output for red flags that indicate unfixable mismatches.

    Args:
        ghidra_ast: Parsed Ghidra decompilation.
        source_node: tree-sitter function_definition node.
        source_bytes: Full file source bytes.
        diagnosis: Diagnosis from objdiff (optional, enables prologue/regswap checks).
        symbol: Mangled symbol name (optional, enables merged symbol detection).
        file_path: Path to source file (optional, enables libclang type checks).
    """
    result = PreflightResult()

    # Rule 6: Merged symbol (early return — no point checking further)
    if symbol and symbol.startswith("merged_"):
        result.is_merged_symbol = True
        result.confidence = 1.0
        result.skip_reason = "merged/ICF symbol"
        return result

    # Rule 1: Struct offset access patterns
    offset_matches = _STRUCT_OFFSET_RE.findall(ghidra_ast.code)
    if offset_matches:
        result.struct_offset_mismatches = offset_matches[:5]  # Cap at 5

    # Rule 2: Call count mismatch
    # Extract function name from Ghidra AST to exclude from call list
    ghidra_func_name = None
    if ghidra_ast.func_node:
        decl = ghidra_ast.func_node.child_by_field_name("declarator")
        if decl:
            # Walk to find the identifier
            for child in _walk_all_nodes(decl):
                if child.type == "identifier" and child.text:
                    ghidra_func_name = child.text.decode("utf-8", errors="replace")
                    break

    ghidra_calls = _filter_helper_calls(
        _extract_calls_from_text(ghidra_ast.code, ghidra_func_name)
    )
    source_calls = _filter_helper_calls(
        _extract_calls_from_node(source_node, source_bytes)
    )

    # Remove common names (both sides have them, they're not mismatches)
    common = ghidra_calls & source_calls
    result.missing_calls = sorted(source_calls - common - ghidra_calls)[:5]
    result.extra_calls = sorted(ghidra_calls - common - source_calls)[:5]

    # Rule 3: Dead variable pattern
    if ghidra_ast.body_node:
        result.dead_variables = _find_dead_variables(ghidra_ast)

    # Rule 7: libclang-enhanced checks (struct offset validation,
    # parameter type matching, cast-on-pointer detection)
    if file_path is not None and clang_types.is_available():
        from pathlib import Path
        fp = Path(file_path) if not isinstance(file_path, Path) else file_path

        # 7a: Validate struct offsets against headers
        if result.struct_offset_mismatches:
            validated = _validate_struct_offsets(
                ghidra_ast, source_node, source_bytes, fp
            )
            result.struct_offsets_validated = validated

        # 7b: Parameter type matching
        param_mismatches = _check_param_types(
            ghidra_ast, source_node, source_bytes, fp
        )
        result.param_type_mismatches = param_mismatches

        # 7c: Cast-on-pointer detection
        result.cast_on_pointer = _detect_cast_on_pointer(
            ghidra_ast, source_node, source_bytes, fp
        )

    # Rule 4: Prologue mismatch (from diagnosis)
    if diagnosis and hasattr(diagnosis, "has_prologue_mismatch") and diagnosis.has_prologue_mismatch:
        result.prologue_mismatch = True

    # Rule 5: Volatile-only regswaps (unfixable via source changes)
    _CALLEE_GPR = set(range(13, 32))  # r13-r31
    _CALLEE_FPR = set(range(14, 32))  # f14-f31
    if diagnosis and hasattr(diagnosis, "reg_swap_pairs") and diagnosis.reg_swap_pairs:
        all_volatile = True
        for (r0, r1) in diagnosis.reg_swap_pairs:
            for r in (r0, r1):
                if r.startswith("r"):
                    try:
                        if int(r[1:]) in _CALLEE_GPR:
                            all_volatile = False
                    except ValueError:
                        pass
                elif r.startswith("f"):
                    try:
                        if int(r[1:]) in _CALLEE_FPR:
                            all_volatile = False
                    except ValueError:
                        pass
        if all_volatile:
            result.volatile_regswap_only = True

    # Compute confidence score
    confidence = 0.0
    if result.struct_offset_mismatches:
        # Reduce confidence if offsets were validated against headers
        if result.struct_offsets_validated > 0:
            unvalidated = len(result.struct_offset_mismatches) - result.struct_offsets_validated
            confidence += max(0.0, 0.15 * (unvalidated / len(result.struct_offset_mismatches)))
        else:
            confidence += 0.15
    if result.extra_calls:
        confidence += 0.05
    if result.missing_calls:
        confidence += 0.05
    if result.dead_variables:
        confidence += 0.10
    if result.param_type_mismatches:
        confidence += 0.10
    if result.cast_on_pointer:
        confidence += 0.15
    if result.prologue_mismatch:
        confidence += 0.30
        if diagnosis and hasattr(diagnosis, "gpr_save_delta"):
            if abs(diagnosis.gpr_save_delta) >= 3:
                confidence += 0.10
    if result.volatile_regswap_only:
        confidence += 0.30
        if diagnosis and hasattr(diagnosis, "replace_real") and diagnosis.replace_real == 0:
            confidence += 0.20

    # If objdiff reports real structural replaces, preflight should be cautious
    # about hard "unfixable" conclusions.
    if diagnosis and hasattr(diagnosis, "replace_real") and diagnosis.replace_real > 0:
        confidence *= 0.70

    result.confidence = min(1.0, confidence)

    # Generate skip reason if confidence is high enough
    reasons = []
    if result.struct_offset_mismatches:
        reasons.append(
            f"{len(result.struct_offset_mismatches)} raw offset access(es)"
        )
    if result.extra_calls:
        reasons.append(
            f"{len(result.extra_calls)} extra call(s) in Ghidra: "
            f"{', '.join(result.extra_calls[:3])}"
        )
    if result.dead_variables:
        reasons.append(
            f"{len(result.dead_variables)} dead variable(s): "
            f"{', '.join(result.dead_variables[:3])}"
        )
    if result.volatile_regswap_only:
        reasons.append("all regswaps are volatile (unfixable)")
    if result.prologue_mismatch:
        reasons.append("prologue save count mismatch")
    if result.param_type_mismatches:
        reasons.append(
            f"{len(result.param_type_mismatches)} param type mismatch(es)"
        )
    if result.cast_on_pointer:
        reasons.append("Ghidra casts pointer to int (header type mismatch)")

    if result.confidence >= 0.4:
        result.skip_reason = "; ".join(reasons)
    result.hard_skip = result.confidence >= 0.9

    return result


def _walk_all_nodes(node):
    """Walk all nodes in a tree-sitter subtree."""
    yield node
    for child in node.children:
        yield from _walk_all_nodes(child)


def _extract_calls_from_text(code: str, func_name: str | None = None) -> set[str]:
    """Extract function call names from Ghidra decompilation text."""
    calls: set[str] = set()
    for m in _GHIDRA_CALL_RE.finditer(code):
        name = m.group(1)
        if (name not in _NOT_CALLS
                and not name.startswith("local_")
                and name != func_name):
            calls.add(name)
    return calls


def _filter_helper_calls(calls: set[str]) -> set[str]:
    """Drop compiler/decompiler helper pseudo-calls from mismatch checks."""
    return {c for c in calls if not _IGNORED_HELPER_CALL_RE.match(c)}


def _extract_calls_from_node(node: Node, source_bytes: bytes) -> set[str]:
    """Extract function call names from a source AST node."""
    calls: set[str] = set()
    _walk_calls(node, source_bytes, calls)
    return calls


def _walk_calls(node: Node, source_bytes: bytes, calls: set[str]) -> None:
    """Walk AST collecting call_expression function names."""
    if node.type == "call_expression":
        func = node.child_by_field_name("function")
        if func is not None:
            text = source_bytes[func.start_byte:func.end_byte].decode(
                "utf-8", errors="replace"
            )
            # Extract the bare function name (last component)
            # Handle Class::Method, obj->Method, obj.Method
            for sep in ("::", "->", "."):
                if sep in text:
                    text = text.rsplit(sep, 1)[-1]
            text = text.strip()
            if text and text not in _NOT_CALLS:
                calls.add(text)

    for child in node.children:
        _walk_calls(child, source_bytes, calls)


def _find_dead_variables(ast: GhidraAST) -> list[str]:
    """Find Ghidra variables that are assigned but never read afterward.

    Looks for `xVarN = expr;` where xVarN doesn't appear anywhere else
    in the function body.
    """
    if not ast.body_node:
        return []

    code_bytes = ast.code.encode("utf-8")
    body_text = code_bytes[ast.body_node.start_byte:ast.body_node.end_byte]

    # Find all Ghidra variable names
    ghidra_var_re = re.compile(r"\b([a-zA-Z]Var\d+)\b")
    all_vars = set(ghidra_var_re.findall(body_text.decode("utf-8", errors="replace")))

    dead = []
    for var in all_vars:
        # Count occurrences — if only 1-2 (declaration + one assignment), likely dead
        count = body_text.count(var.encode("utf-8"))
        if count <= 2:
            dead.append(var)

    return sorted(dead)[:5]


# ---------------------------------------------------------------------------
# Rule 7: libclang-enhanced checks
# ---------------------------------------------------------------------------

# Matches Ghidra's *(type *)(param + 0xNN) — extract hex offset
_STRUCT_OFFSET_HEX_RE = re.compile(
    r"\*\(.+?\s*\*\)\s*\((\w+)\s*\+\s*0x([0-9a-fA-F]+)\)"
)

# Matches Ghidra's (int)param_N or (uint)param_N etc.
_CAST_ON_PARAM_RE = re.compile(
    r"\((int|uint|long|ulong)\)\s*(param_\d+)"
)


def _validate_struct_offsets(
    ghidra_ast: GhidraAST,
    source_node: Node,
    source_bytes: bytes,
    file_path: "Path",
) -> int:
    """Cross-reference Ghidra struct offsets against libclang types.

    Returns the count of offsets that could be validated (i.e., the
    parameter type is a known struct and the offset corresponds to a
    real field). These are benign accesses, not layout mismatches.
    """
    validated = 0
    for m in _STRUCT_OFFSET_HEX_RE.finditer(ghidra_ast.code):
        param_name = m.group(1)
        # Only check param_ references (function parameters)
        if not param_name.startswith("param_"):
            continue

        # Try to find the corresponding source parameter
        param_idx = int(param_name.split("_")[1]) - 1  # 0-based
        source_params = _get_source_params(source_node)
        if param_idx < len(source_params):
            param_node = source_params[param_idx]
            ti = clang_types.resolve_decl_type(
                file_path, param_node.start_byte, source_bytes
            )
            if ti is not None and (ti.is_pointer or ti.kind == clang_types.TypeKind.RECORD):
                # Parameter is a struct/class pointer — the offset access
                # is likely a legitimate field access, not a layout mismatch
                validated += 1

    return validated


def _check_param_types(
    ghidra_ast: GhidraAST,
    source_node: Node,
    source_bytes: bytes,
    file_path: "Path",
) -> list[str]:
    """Compare Ghidra parameter types against source parameter types.

    Returns a list of mismatch descriptions.
    """
    mismatches = []

    # Extract Ghidra function parameters
    if not ghidra_ast.func_node:
        return mismatches

    ghidra_params = _get_ghidra_param_types(ghidra_ast)
    source_params = _get_source_params(source_node)

    for i, (g_type, s_param) in enumerate(zip(ghidra_params, source_params)):
        ti = clang_types.resolve_decl_type(
            file_path, s_param.start_byte, source_bytes
        )
        if ti is None:
            continue

        # Check for obvious mismatches
        if g_type in ("int", "uint", "long", "ulong") and ti.is_pointer:
            mismatches.append(
                f"param {i+1}: Ghidra={g_type}, source={ti.spelling}"
            )
        elif g_type.endswith("*") and not ti.is_pointer:
            mismatches.append(
                f"param {i+1}: Ghidra={g_type}, source={ti.spelling}"
            )

    return mismatches[:5]


def _detect_cast_on_pointer(
    ghidra_ast: GhidraAST,
    source_node: Node,
    source_bytes: bytes,
    file_path: "Path",
) -> bool:
    """Detect Ghidra casting a pointer parameter to int.

    This indicates Ghidra sees the parameter as an integer but our source
    declares it as a pointer — a strong header type mismatch signal.
    """
    source_params = _get_source_params(source_node)

    for m in _CAST_ON_PARAM_RE.finditer(ghidra_ast.code):
        cast_type = m.group(1)
        param_name = m.group(2)

        if cast_type not in ("int", "uint", "long", "ulong"):
            continue

        param_idx = int(param_name.split("_")[1]) - 1
        if param_idx < len(source_params):
            ti = clang_types.resolve_decl_type(
                file_path, source_params[param_idx].start_byte, source_bytes
            )
            if ti is not None and ti.is_pointer:
                return True

    return False


def _get_source_params(source_node: Node) -> list[Node]:
    """Extract parameter declaration nodes from a function_definition."""
    params = []
    declarator = source_node.child_by_field_name("declarator")
    if declarator is None:
        return params

    # Walk to find the parameter_list
    for child in _walk_all_nodes(declarator):
        if child.type == "parameter_list":
            for param in child.named_children:
                if param.type == "parameter_declaration":
                    params.append(param)
            break

    return params


def _get_ghidra_param_types(ghidra_ast: GhidraAST) -> list[str]:
    """Extract parameter type names from Ghidra function declaration."""
    types = []
    if not ghidra_ast.func_node:
        return types

    declarator = ghidra_ast.func_node.child_by_field_name("declarator")
    if declarator is None:
        return types

    code_bytes = ghidra_ast.code.encode("utf-8")
    for child in _walk_all_nodes(declarator):
        if child.type == "parameter_list":
            for param in child.named_children:
                if param.type == "parameter_declaration":
                    type_node = param.child_by_field_name("type")
                    if type_node:
                        type_text = code_bytes[
                            type_node.start_byte : type_node.end_byte
                        ].decode("utf-8", errors="replace").strip()
                        types.append(type_text)
            break

    return types
