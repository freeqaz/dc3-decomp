"""Optional m2c decompilation support for permuter guidance."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

from .project import get_project_config

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
M2C_PATH = Path.home() / "code" / "milohax" / "m2c" / "m2c.py"
OBJDIFF_TO_M2C_PATH = REPO_ROOT / "tools" / "objdiff_to_m2c.py"
_CALL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
_NOT_CALLS = frozenset({
    "if", "while", "for", "switch", "return", "sizeof", "typeof",
    "int", "long", "short", "char", "void", "float", "double",
    "uint", "ulong", "ushort", "uchar", "bool", "struct",
})

_M2C_CACHE: dict[tuple[str, str | None], str | None] = {}


def get_or_run_m2c(symbol: str, unit: str | None = None) -> str | None:
    """Return cached m2c decompilation text for a symbol, if available."""
    key = (symbol, unit)
    if key in _M2C_CACHE:
        return _M2C_CACHE[key]

    code = _run_m2c(symbol, unit)
    _M2C_CACHE[key] = code
    return code


def _run_m2c(symbol: str, unit: str | None) -> str | None:
    """Run objdiff JSON -> objdiff_to_m2c -> m2c pipeline."""
    project = get_project_config()
    repo_root = project.repo_root
    objdiff_to_m2c = repo_root / "tools" / "objdiff_to_m2c.py"

    if not M2C_PATH.exists() or not objdiff_to_m2c.exists():
        return None

    try:
        objdiff_result = subprocess.run(
            [
                project.objdiff_cli,
                "diff",
                "-p",
                str(repo_root),
                "-f",
                "json",
                "--include-instructions",
                symbol,
            ],
            capture_output=True,
            text=True,
            timeout=60,
            cwd=str(repo_root),
        )
        if objdiff_result.returncode != 0 or not objdiff_result.stdout.strip():
            return None

        json_line = None
        for line in objdiff_result.stdout.splitlines():
            candidate = line.strip()
            if candidate.startswith("{") and (
                '"instructions"' in candidate or '"symbol"' in candidate
            ):
                json_line = candidate
                break
        if not json_line:
            return None

        convert_cmd = ["python3", str(objdiff_to_m2c), "--project-dir", str(repo_root)]
        obj_path = _obj_path_for_unit(unit)
        if obj_path is not None and obj_path.exists():
            convert_cmd.extend(["--obj", str(obj_path)])

        convert_result = subprocess.run(
            convert_cmd,
            input=json_line,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=str(repo_root),
        )
        if convert_result.returncode != 0 or not convert_result.stdout.strip():
            return None

        m2c_result = subprocess.run(
            ["python3", str(M2C_PATH), "-t", project.m2c_target, "--valid-syntax", "-"],
            input=convert_result.stdout,
            capture_output=True,
            text=True,
            timeout=60,
            cwd=str(repo_root),
        )
        if m2c_result.returncode != 0:
            return None

        output = m2c_result.stdout.strip()
        if not output or "Decompilation failure" in output:
            return None
        return output
    except Exception as exc:
        print(f"  m2c: unavailable ({exc})", file=sys.stderr)
        return None


def _obj_path_for_unit(unit: str | None) -> Path | None:
    """Best-effort map from DB unit to built object path."""
    if not unit:
        return None
    project = get_project_config()
    return project.obj_path_for_unit(unit)


def extract_last_call_name(code: str) -> str | None:
    """Extract the last call-like identifier from m2c output."""
    body = code.split("{", 1)[1] if "{" in code else code
    calls: list[str] = []
    for match in _CALL_RE.finditer(body):
        name = match.group(1)
        if name in _NOT_CALLS:
            continue
        calls.append(name)
    if calls:
        return calls[-1]
    return None


# ------------------------------------------------------------------
# Structural extractors for pattern guidance
# ------------------------------------------------------------------

_IF_RE = re.compile(r"\bif\s*\(")
_RETURN_RE = re.compile(r"\breturn\b")
_GUARD_RETURN_RE = re.compile(
    r"\bif\s*\([^)]*\)\s*\{?\s*return\b"
)


def extract_nesting_depth(code: str) -> int:
    """Extract maximum nesting depth from m2c output.

    Counts the deepest chain of nested ``if`` statements by tracking
    brace/keyword depth.  Useful for guard_to_nested to know whether
    the target uses flat guards or deep nesting.

    Returns 0 if no ``if`` statements found, 1 for flat if-chains,
    2+ for nested structures.
    """
    body = code.split("{", 1)[1] if "{" in code else code
    max_depth = 0
    current_depth = 0

    i = 0
    while i < len(body):
        # Check for "if ("
        if body[i:i+2] == "if" and (i + 2 >= len(body) or not body[i+2].isalnum()):
            rest = body[i+2:].lstrip()
            if rest.startswith("("):
                current_depth += 1
                max_depth = max(max_depth, current_depth)
        elif body[i] == "}":
            current_depth = max(0, current_depth - 1)
        i += 1

    return max_depth


def extract_guard_count(code: str) -> int:
    """Count guard-return patterns in m2c output.

    A guard-return is ``if (cond) { return ...; }`` or
    ``if (cond) return ...;`` — an early return guarded by a condition.

    Useful for early_return_merge and guard_to_nested to determine
    whether the target uses guard-style or merged-condition style.
    """
    return len(_GUARD_RETURN_RE.findall(code))


def extract_return_pattern(code: str) -> str:
    """Classify the return structure of m2c output.

    Returns one of:
        "merged_var"   - single return with conditional assignment to temp
        "split_calls"  - multiple returns with different call expressions
        "guard_chain"  - early returns followed by a final return
        "single"       - only one return statement
        "unknown"      - unclassifiable

    Useful for return_call_merge to decide which direction to transform.
    """
    body = code.split("{", 1)[1] if "{" in code else code

    returns = list(_RETURN_RE.finditer(body))
    if len(returns) <= 1:
        return "single"

    guards = extract_guard_count(code)
    if guards >= 2:
        return "guard_chain"

    # Check for temp-variable pattern: "type temp; if (...) temp = ...; else temp = ...; return temp;"
    # Simplified: look for assignment to same var in if/else + final return of that var
    if re.search(r"\btemp\b.*=.*;\s*\}?\s*else\s*\{?\s*\btemp\b.*=", body, re.DOTALL):
        return "merged_var"

    # Multiple returns with calls → split_calls
    call_returns = 0
    for m in returns:
        after = body[m.end():m.end() + 80]
        if _CALL_RE.search(after):
            call_returns += 1
    if call_returns >= 2:
        return "split_calls"

    if guards >= 1:
        return "guard_chain"

    return "unknown"


def extract_call_order(code: str) -> list[str]:
    """Extract function call names in order of appearance.

    Returns deduplicated list preserving first-occurrence order.
    Useful for statement_reorder to detect target's call ordering.
    """
    body = code.split("{", 1)[1] if "{" in code else code
    seen: set[str] = set()
    order: list[str] = []
    for match in _CALL_RE.finditer(body):
        name = match.group(1)
        if name in _NOT_CALLS:
            continue
        if name not in seen:
            seen.add(name)
            order.append(name)
    return order


# ------------------------------------------------------------------
# Variable first-use order (m2c fallback for Ghidra-only path)
# ------------------------------------------------------------------

# Matches typical m2c local-variable declarations:
#   "    s32 temp_r0;"
#   "    f64 var_ft1_2;"
#   "    void *sp8;"
#   "    s32 var_v0;"
#   "    struct Foo *p;"
# Captures (type, ptr_after_type, ptr_before_name, name). The pointer parts
# are joined back into the type for downstream type-prefix classification.
_M2C_DECL_RE = re.compile(
    r"^\s*((?:const\s+|volatile\s+|signed\s+|unsigned\s+|struct\s+|union\s+)?"
    r"[A-Za-z_][\w]*)"          # base type token (group 1)
    r"(\s*\*+)?\s+"             # optional ptr asterisks adjacent to the type (group 2)
    r"(\*+\s*)?"                # optional ptr asterisks adjacent to the name (group 3)
    r"([A-Za-z_]\w*)\s*;\s*$",  # variable name (group 4)
    re.MULTILINE,
)

# m2c local-name conventions (cf. m2c README):
#   sp<N>      stack slot
#   temp_<reg> short-lived temporary in <reg>
#   var_<reg>  longer-lived variable in <reg>
#   arg<N>     argument (parameter — excluded)
# Plus user-named locals when DWARF/PDB info is present.
_M2C_PARAM_PREFIXES = ("arg",)


def _m2c_type_prefix(decl_type: str) -> str:
    """Map an m2c type token to a Ghidra-style type prefix.

    Returns 'f' for float types, 'i' for ints, 'p' for pointers, '' otherwise.
    Used to populate VarInfo.type_prefix when feeding ghidra_guided_reorder.
    """
    t = decl_type.lower().strip()
    if "*" in t:
        return "p"
    if t in ("f32", "f64", "float", "double", "long double"):
        return "f"
    if t in (
        "s8", "s16", "s32", "s64",
        "u8", "u16", "u32", "u64",
        "int", "long", "short", "char",
        "size_t", "ssize_t", "ptrdiff_t",
        "bool", "_bool",
    ):
        return "u" if t.startswith("u") else "i"
    return ""


def extract_variable_first_use_order_from_text(m2c_code: str):
    """Extract local variable first-use order from m2c output text.

    Mirrors `ghidra_ast.extract_variable_first_use_order` but operates on raw
    m2c text (no tree-sitter parse). Returns a list of `VarInfo` ordered by
    first appearance after the declaration preamble — same shape as the Ghidra
    extractor so it can be fed straight into `ghidra_guided_reorder`.

    Used as a redundant source for variable order when Ghidra is unavailable.
    Returns [] on malformed input or when no locals are found.
    """
    # Imported here to avoid a hard dependency on tree-sitter at import time.
    from .ghidra_ast import VarInfo

    if not m2c_code or "{" not in m2c_code:
        return []

    # Body is everything after the first opening brace (the function body).
    body_start = m2c_code.find("{") + 1
    body = m2c_code[body_start:]

    # 1. Collect local declarations from the preamble.
    #    m2c emits all locals up-front before any statements, so the first
    #    non-decl line ends the preamble.
    local_names: list[str] = []
    name_to_type: dict[str, str] = {}
    preamble_end = 0
    for line in body.splitlines(keepends=True):
        stripped = line.strip()
        if not stripped:
            preamble_end += len(line)
            continue
        # Hit a brace, label, or non-decl statement -> preamble done.
        m = _M2C_DECL_RE.match(line)
        if m is None:
            break
        # Stitch the base type and any pointer asterisks back together so
        # _m2c_type_prefix sees `void*` (returns 'p') rather than just `void`.
        base_type = m.group(1).strip()
        ptr_after_type = (m.group(2) or "").strip()
        ptr_before_name = (m.group(3) or "").strip()
        ptr_part = ptr_after_type + ptr_before_name
        decl_type = (base_type + ptr_part).strip()
        name = m.group(4).strip()
        # Skip parameters; they shouldn't appear in the preamble but be safe.
        if any(name.startswith(p) and name[len(p):].isdigit()
               for p in _M2C_PARAM_PREFIXES):
            preamble_end += len(line)
            continue
        local_names.append(name)
        name_to_type[name] = decl_type
        preamble_end += len(line)

    if not local_names:
        return []

    # 2. Walk the rest of the body and record first-use position of each local.
    after_preamble = body[preamble_end:]
    after_preamble_offset = body_start + preamble_end
    targets = set(local_names)
    first_use: dict[str, tuple[int, int]] = {}

    # Identifier-boundary regex: match each occurrence of each target name.
    # Build one alternation to scan once.
    sorted_names = sorted(targets, key=len, reverse=True)
    name_pattern = re.compile(
        r"\b(" + "|".join(re.escape(n) for n in sorted_names) + r")\b"
    )

    for match in name_pattern.finditer(after_preamble):
        name = match.group(1)
        if name in first_use:
            continue
        byte_off = after_preamble_offset + match.start()
        # Line number relative to the full m2c_code (0-based, like Ghidra extractor).
        line_no = m2c_code[:byte_off].count("\n")
        first_use[name] = (byte_off, line_no)
        if len(first_use) == len(targets):
            break

    if not first_use:
        return []

    # 3. Build VarInfo list in first-use order.
    result = []
    for name in sorted(first_use, key=lambda n: first_use[n]):
        byte_off, line_no = first_use[name]
        decl_type = name_to_type.get(name, "")
        result.append(VarInfo(
            name=name,
            first_use_line=line_no,
            first_use_byte=byte_off,
            type_prefix=_m2c_type_prefix(decl_type),
            is_param=False,
            decl_type=decl_type,
        ))

    return result
