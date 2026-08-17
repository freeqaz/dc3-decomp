#!/usr/bin/env python3
"""
Context collector for pre-computed analysis injection.

Gathers all context that would otherwise waste agent turns:
- objdiff match%, verdict, and suggestions (via incremental build)
- Ghidra decompilation and cross-references
- Previous attempt history
- RB3 reference code (shared Milo engine)
- Cross-reference file written to worktree for agent access

Usage:
    from scripts.orchestrator.context_collector import collect_pre_run_context

    context = collect_pre_run_context(
        symbol="?Load@CharMirror@@UAAXAAVBinStream@@@Z",
        unit="system/char/CharMirror",
        project_dir="/home/free/code/milohax/dc3-decomp",
        worktree_dir="/tmp/test-worktree"
    )

    # context dict keys:
    # - match_percent (float)
    # - verdict (string: AT_LIMIT, LIKELY_FIXABLE, MAYBE_FIXABLE)
    # - key_patterns (list of strings)
    # - suggestions (list)
    # - previous_attempts (formatted string)
    # - previous_attempts_count (int)
    # - decompilation (optional, original C code from Ghidra)
    # - rb3_reference (optional, matching code from RB3 decomp)
    # - xrefs_path_absolute (string, full path)
    # - xrefs_path_relative (string, relative to worktree)
    # - xrefs_preview (string, first 20 lines)
"""

import hashlib
import json
import logging
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

# Project root (dc3-decomp) and sibling repos
_PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
_MILOHAX_DIR = _PROJECT_ROOT.parent

# Default RB3 source path
DEFAULT_RB3_PATH = _MILOHAX_DIR / "rb3" / "src"

# m2c decompiler paths
DEFAULT_M2C_PATH = _MILOHAX_DIR / "m2c" / "m2c.py"
DEFAULT_ASM_TO_M2C = _PROJECT_ROOT / "tools" / "asm_to_m2c.py"
DEFAULT_ASM_DIR = _PROJECT_ROOT / "build" / "373307D9" / "asm"

# New m2c pipeline tools (Phase 1 additions)
DEFAULT_OBJDIFF_TO_M2C = _PROJECT_ROOT / "tools" / "objdiff_to_m2c.py"
DEFAULT_EXPORT_TYPES = _PROJECT_ROOT / "tools" / "ghidra" / "export_types.py"

# =============================================================================
# Token Budget System
# =============================================================================
# Total prompt budget: ~80k tokens max (~240k chars at ~3 chars/token)
# Per-section budget: ~8k tokens max (~24k chars)
#
# Anything exceeding per-section budget gets written to a file in the worktree
# with only a short preview (head -n 30) included inline.
#
# The SDK passes prompts via stdin (not CLI args), so ARG_MAX does not apply.
# Subprocess mode passes prompts as CLI args but Linux ARG_MAX is ~2MB,
# so 240KB is well within limits for both modes.
# =============================================================================

# Token-to-character ratio (conservative estimate)
CHARS_PER_TOKEN = 3

# Budget limits in tokens
TOTAL_PROMPT_TOKEN_BUDGET = 60_000  # ~60k tokens total
SECTION_TOKEN_BUDGET = 6_000        # ~6k tokens per section

# Derived character limits
SECTION_CHAR_BUDGET = SECTION_TOKEN_BUDGET * CHARS_PER_TOKEN  # ~18k chars
TOTAL_CHAR_BUDGET = TOTAL_PROMPT_TOKEN_BUDGET * CHARS_PER_TOKEN  # ~180k chars

# Preview size when content exceeds budget (in lines)
PREVIEW_LINES = 30

# Legacy constants (for backward compat, all now derived from token budget)
MAX_INLINE_LINES = SECTION_CHAR_BUDGET // 80  # ~300 lines at 80 chars/line
M2C_MAX_INLINE_LINES = MAX_INLINE_LINES
OBJDIFF_MAX_PREVIEW_LINES = PREVIEW_LINES
GHIDRA_MAX_INLINE_LINES = MAX_INLINE_LINES

# Configure logging
logger = logging.getLogger(__name__)


# =============================================================================
# A/B Testing Enrichment Assignment
# =============================================================================
# Experiments are deterministically assigned based on symbol hash to ensure
# consistent assignment across runs and enable valid comparisons.


def assign_enrichment_group(symbol: str, experiment: str) -> bool:
    """
    Deterministic 50/50 A/B assignment based on symbol hash.

    The same symbol+experiment pair always gets the same assignment,
    ensuring:
    - Consistent behavior across retries
    - Valid control vs treatment comparison
    - No need to track assignments separately

    Args:
        symbol: Function symbol (mangled name)
        experiment: Experiment identifier (e.g., "diff_patterns", "function_types")

    Returns:
        True if symbol is in treatment group, False if in control group
    """
    # Combine symbol and experiment name for unique per-experiment assignment
    key = f"{symbol}:{experiment}"
    h = hashlib.md5(key.encode()).hexdigest()
    # First hex digit 0-7 = control (False), 8-f = treatment (True)
    return int(h[0], 16) >= 8


def get_enrichment_assignments(symbol: str) -> Dict[str, bool]:
    """
    Get all enrichment assignments for a symbol.

    Returns dict of experiment -> treatment flag for tracking.
    """
    experiments = [
        "diff_patterns",      # Exp 1: Pre-classified diff patterns
        "function_types",     # Exp 2: Type-specific guidance
        "rb2_layouts",        # Exp 3: Pre-computed struct layouts
        "attempt_diffs",      # Exp 4: Previous attempt code diffs
        "matched_siblings",   # Exp 5: 100% matched same-class functions
        "callee_signatures",  # Exp 6: Pre-resolved callee signatures
    ]
    return {exp: assign_enrichment_group(symbol, exp) for exp in experiments}

# Try to import from tools
try:
    from tools.analyze_function import run_objdiff
except ImportError:
    run_objdiff = None

try:
    from tools.ghidra.mcp_client import MCPClient as GhidraMCPClient, MCPError as GhidraMCPError
except ImportError:
    GhidraMCPClient = None
    GhidraMCPError = Exception

# JVM-based Ghidra client — disabled by default (broken/hangs).
# Set GHIDRA_USE_JVM=1 to enable as a fallback when the HTTP service is down.
try:
    from tools.ghidra.direct_client import DirectGhidraClient, DirectGhidraClientError
except ImportError:
    DirectGhidraClient = None
    DirectGhidraClientError = Exception

_USE_JVM_GHIDRA = os.environ.get("GHIDRA_USE_JVM", "0") == "1"


def _jvm_ghidra_decompile(symbol: str, project_dir: str) -> Optional[str]:
    """Try decompiling via the in-process JVM Ghidra client.

    Only runs when GHIDRA_USE_JVM=1.  Returns decompiled code or None.
    """
    if not _USE_JVM_GHIDRA or DirectGhidraClient is None:
        return None
    try:
        binary_path = get_binary_path(project_dir)
        if not binary_path:
            logger.warning("Could not locate binary for Ghidra JVM client")
            return None
        ghidra_project_dir = Path("/tmp/claude/ghidra_projects")
        ghidra_project_dir.mkdir(parents=True, exist_ok=True)
        client = DirectGhidraClient.get_instance(
            binary_path=binary_path,
            project_dir=str(ghidra_project_dir),
            project_name="DC3",
            verbose=False,
        )
        return client.decompile_function(symbol)
    except DirectGhidraClientError as e:
        logger.warning(f"Ghidra JVM decompilation failed: {e}")
    except Exception as e:
        logger.warning(f"Ghidra JVM unexpected error: {e}")
    return None


def _jvm_ghidra_xrefs(symbol: str, project_dir: str) -> Optional[tuple[list, list]]:
    """Try fetching cross-references via the in-process JVM Ghidra client.

    Only runs when GHIDRA_USE_JVM=1.  Returns (callers, callees) or None.
    """
    if not _USE_JVM_GHIDRA or DirectGhidraClient is None:
        return None
    try:
        binary_path = get_binary_path(project_dir)
        if not binary_path:
            logger.warning("Could not locate binary for Ghidra JVM client")
            return None
        ghidra_project_dir = Path("/tmp/claude/ghidra_projects")
        ghidra_project_dir.mkdir(parents=True, exist_ok=True)
        client = DirectGhidraClient.get_instance(
            binary_path=binary_path,
            project_dir=str(ghidra_project_dir),
            project_name="DC3",
            verbose=False,
        )
        callers, callees = client.list_cross_references(symbol)
        return callers, callees
    except DirectGhidraClientError as e:
        logger.warning(f"Ghidra JVM xrefs failed: {e}")
    except Exception as e:
        logger.warning(f"Ghidra JVM xrefs unexpected error: {e}")
    return None


try:
    from scripts.orchestrator.database import get_attempts_for_function, get_function_by_symbol
except ImportError:
    get_attempts_for_function = None
    get_function_by_symbol = None


def truncate_and_offload(
    content: str,
    name: str,
    worktree_dir: str,
    max_chars: int = SECTION_CHAR_BUDGET,
    preview_lines: int = PREVIEW_LINES,
) -> Dict[str, Any]:
    """
    Truncate content if it exceeds the token budget and write full content to file.

    If content is within budget, returns it inline.
    If content exceeds budget, writes to file and returns a preview with file pointer.

    Args:
        content: The content to potentially truncate
        name: Name for the file (e.g., "ghidra_decompilation", "objdiff_output")
        worktree_dir: Worktree directory to write file to
        max_chars: Maximum characters to include inline (default: SECTION_CHAR_BUDGET)
        preview_lines: Number of lines to include in preview (default: PREVIEW_LINES)

    Returns:
        Dict with:
        - inline: str - Content to include in prompt (full or truncated preview)
        - file_path: str - Absolute path to file (or None if inline)
        - file_path_relative: str - Relative path for display (or None if inline)
        - was_truncated: bool - Whether content was truncated
        - original_size: int - Original content size in chars
        - line_count: int - Total line count
    """
    result = {
        "inline": content,
        "file_path": None,
        "file_path_relative": None,
        "was_truncated": False,
        "original_size": len(content),
        "line_count": content.count("\n") + 1,
    }

    # Check if within budget
    if len(content) <= max_chars:
        return result

    # Content exceeds budget - write to file and create preview
    result["was_truncated"] = True

    # Create analysis directory
    analysis_dir = Path(worktree_dir) / "function_analysis"
    analysis_dir.mkdir(exist_ok=True, parents=True)

    # Write full content to file
    safe_name = name.replace("?", "_Q_").replace("@", "_A_").replace("<", "_L_").replace(">", "_R_").replace("/", "_")
    file_path = analysis_dir / f"{safe_name}.txt"

    with open(file_path, "w") as f:
        f.write(content)

    result["file_path"] = str(file_path)
    result["file_path_relative"] = f"function_analysis/{safe_name}.txt"

    # Create preview (first N lines)
    lines = content.split("\n")
    if len(lines) > preview_lines:
        preview = "\n".join(lines[:preview_lines])
        remaining = len(lines) - preview_lines
        result["inline"] = (
            f"{preview}\n\n"
            f"... ({remaining} more lines)\n"
            f"Full content: {result['file_path_relative']}\n"
            f"View with: cat {result['file_path_relative']}"
        )
    else:
        # Edge case: few lines but many chars per line
        result["inline"] = content[:max_chars] + f"\n\n... (truncated)\nFull content: {result['file_path_relative']}"

    logger.debug(
        f"Truncated {name}: {len(content)} chars → {len(result['inline'])} chars, "
        f"wrote to {result['file_path_relative']}"
    )

    return result


def parse_msvc_symbol(symbol: str) -> Tuple[Optional[str], Optional[str]]:
    """
    Parse MSVC mangled symbol to extract class and method names.

    Args:
        symbol: Mangled symbol (e.g., "?PoseMeshes@CharBonesMeshes@@QAAXXZ")

    Returns:
        Tuple of (class_name, method_name) or (None, None) if not parseable
    """
    if not symbol or not symbol.startswith("?"):
        return None, None

    # Constructor: ??0ClassName@@...
    if symbol.startswith("??0"):
        match = re.match(r"\?\?0(\w+)@@", symbol)
        if match:
            class_name = match.group(1)
            return class_name, class_name  # Constructor name = class name

    # Destructor: ??1ClassName@@...
    if symbol.startswith("??1"):
        match = re.match(r"\?\?1(\w+)@@", symbol)
        if match:
            class_name = match.group(1)
            return class_name, f"~{class_name}"

    # Regular method: ?MethodName@ClassName@@...
    parts = symbol[1:].split("@")
    if len(parts) >= 2:
        method_name = parts[0]
        class_name = parts[1]
        return class_name, method_name

    return None, None


def _extract_source_window(
    source: str,
    class_name: str,
    method_name: str,
    context_lines: int = 20,
) -> Tuple[str, int, int, int]:
    """
    Find a function in source code and return a windowed extract with line numbers.

    Args:
        source: Full source file contents
        class_name: Class name (e.g., "CharMirror")
        method_name: Method name (e.g., "Load")
        context_lines: Lines of context before and after function body

    Returns:
        Tuple of (window_text, start_line, end_line, total_lines)
        If function not found, returns (source, 1, total_lines, total_lines) (full file)
    """
    lines = source.split('\n')
    total_lines = len(lines)

    # Build regex: ClassName::MethodName( at any position in a line
    pattern = re.compile(
        rf'\b{re.escape(class_name)}::{re.escape(method_name)}\s*\(',
        re.MULTILINE,
    )
    match = pattern.search(source)
    if not match:
        # Function not found — return full file
        return source, 1, total_lines, total_lines

    # Find which line the match is on
    func_start_offset = match.start()
    func_start_line = source[:func_start_offset].count('\n')  # 0-indexed

    # Find opening brace after match
    brace_pos = source.find('{', match.end())
    if brace_pos == -1:
        return source, 1, total_lines, total_lines

    # Count braces to find matching closing brace
    depth = 1
    pos = brace_pos + 1
    while pos < len(source) and depth > 0:
        ch = source[pos]
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
        pos += 1

    if depth != 0:
        return source, 1, total_lines, total_lines

    # Find which line the closing brace is on
    func_end_line = source[:pos].count('\n')  # 0-indexed

    # Compute window with context
    window_start = max(0, func_start_line - context_lines)
    window_end = min(total_lines - 1, func_end_line + context_lines)

    # Extract the window lines
    window_lines = lines[window_start:window_end + 1]
    window_text = '\n'.join(window_lines)

    # Return 1-indexed line numbers for display
    return window_text, window_start + 1, window_end + 1, total_lines


def find_rb3_reference(
    symbol: str,
    unit: str,
    rb3_path: Path = DEFAULT_RB3_PATH,
) -> str:
    """
    Find matching reference implementation in RB3 decomp.

    RB3 and DC3 share the Milo engine, so many functions are identical.
    This searches for matching class/method in RB3 source.

    Args:
        symbol: Mangled symbol
        unit: Unit path (e.g., "system/char/CharBonesMeshes")
        rb3_path: Path to RB3 source directory

    Returns:
        Matching code snippet or "(not found)" or "(rb3 not available)"
    """
    if not rb3_path.exists():
        return "(RB3 source not available)"

    class_name, method_name = parse_msvc_symbol(symbol)
    if not class_name or not method_name:
        return "(could not parse symbol)"

    # Strategy 1: Try to find matching file based on unit path
    # DC3: system/char/CharBonesMeshes -> RB3: system/char/CharBonesMeshes.cpp
    unit_path = unit.replace("default/", "")  # Remove objdiff prefix if present
    rb3_cpp = rb3_path / f"{unit_path}.cpp"
    rb3_h = rb3_path / f"{unit_path}.h"

    search_files = []
    if rb3_cpp.exists():
        search_files.append(rb3_cpp)
    if rb3_h.exists():
        search_files.append(rb3_h)

    # Strategy 2: Search by class name if direct path not found
    if not search_files:
        # Try to find files matching the class name
        try:
            result = subprocess.run(
                ["find", str(rb3_path), "-name", f"{class_name}.*", "-type", "f"],
                capture_output=True,
                text=True,
                timeout=5,
            )
            if result.stdout.strip():
                for path in result.stdout.strip().split("\n"):
                    if path.endswith(".cpp") or path.endswith(".h"):
                        search_files.append(Path(path))
        except Exception:
            pass

    if not search_files:
        return f"(no matching files for {class_name})"

    # Search for the method in found files
    for file_path in search_files:
        try:
            content = file_path.read_text()

            # Look for the method definition
            # Pattern 1: void ClassName::MethodName(...) {
            pattern1 = rf"[\w\s\*&:<>]+\s+{class_name}::{method_name}\s*\([^)]*\)\s*{{"
            # Pattern 2: For constructors/destructors
            pattern2 = rf"{class_name}::{method_name}\s*\([^)]*\)\s*(?::[^{{]+)?{{"

            for pattern in [pattern1, pattern2]:
                match = re.search(pattern, content, re.MULTILINE)
                if match:
                    # Extract the function - find matching braces
                    start = match.start()
                    brace_start = content.find("{", start)
                    if brace_start == -1:
                        continue

                    # Count braces to find end
                    brace_count = 0
                    end = brace_start
                    for i, char in enumerate(content[brace_start:], brace_start):
                        if char == "{":
                            brace_count += 1
                        elif char == "}":
                            brace_count -= 1
                            if brace_count == 0:
                                end = i + 1
                                break

                    if end > brace_start:
                        # Include some context before the function
                        line_start = content.rfind("\n", 0, start) + 1
                        func_code = content[line_start:end]
                        header = f"// From: {file_path.relative_to(rb3_path.parent.parent)}\n"

                        # Truncate if exceeds section budget (use PREVIEW_LINES * 3 as reasonable limit)
                        lines = func_code.split("\n")
                        if len(func_code) > SECTION_CHAR_BUDGET or len(lines) > PREVIEW_LINES * 3:
                            preview = "\n".join(lines[:PREVIEW_LINES])
                            remaining = len(lines) - PREVIEW_LINES
                            func_code = f"{preview}\n// ... ({remaining} more lines truncated)"

                        return f"{header}{func_code}"

        except Exception as e:
            logger.debug(f"Error reading {file_path}: {e}")
            continue

    # Fallback: grep search
    try:
        result = subprocess.run(
            ["grep", "-rn", "--include=*.cpp", "--include=*.h",
             f"{class_name}::{method_name}", str(rb3_path)],
            capture_output=True,
            text=True,
            timeout=10,
        )
        if result.stdout.strip():
            lines = result.stdout.strip().split("\n")[:10]
            return f"// Grep matches (function body not extracted):\n" + "\n".join(lines)
    except Exception:
        pass

    return f"(no implementation found for {class_name}::{method_name})"


def generate_type_context(
    symbol: str,
    worktree_dir: str,
    export_types_path: Path = DEFAULT_EXPORT_TYPES,
) -> Optional[str]:
    """
    Generate type context header for m2c using Ghidra export_types.py.

    This is non-blocking - returns None on failure and lets m2c run without context.

    Args:
        symbol: Function symbol (mangled)
        worktree_dir: Worktree directory for output file
        export_types_path: Path to export_types.py script

    Returns:
        Path to generated context header file, or None if generation failed
    """
    if not export_types_path.exists():
        logger.debug(f"export_types.py not found at {export_types_path}")
        return None

    # Parse symbol to get function name for export_types
    class_name, method_name = parse_msvc_symbol(symbol)
    if not method_name:
        logger.debug(f"Could not parse symbol for type export: {symbol}")
        return None

    # Build function name for export_types (prefers ClassName::MethodName format)
    func_name = f"{class_name}::{method_name}" if class_name else method_name

    # Create output directory
    analysis_dir = Path(worktree_dir) / "function_analysis"
    analysis_dir.mkdir(exist_ok=True, parents=True)

    # Create safe filename
    safe_name = method_name
    if class_name:
        safe_name = f"{class_name}_{method_name}"
    context_file = analysis_dir / f"m2c_context_{safe_name}.h"

    try:
        cmd = [
            "python3",
            str(export_types_path),
            "--function", func_name,
            "-o", str(context_file),
        ]

        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=60,  # 1 minute timeout for Ghidra operations
        )

        if result.returncode == 0 and context_file.exists():
            logger.info(f"Type context generated: {context_file}")
            return str(context_file)
        else:
            logger.debug(f"export_types failed: {result.stderr[:200] if result.stderr else 'no error output'}")
            return None

    except subprocess.TimeoutExpired:
        logger.debug("export_types timed out")
        return None
    except Exception as e:
        logger.debug(f"export_types error: {e}")
        return None


def run_m2c_from_objdiff_json(
    symbol: str,
    objdiff_json_path: str,
    worktree_dir: str,
    m2c_path: Path = DEFAULT_M2C_PATH,
    objdiff_to_m2c_path: Path = DEFAULT_OBJDIFF_TO_M2C,
    context_file: Optional[str] = None,
    project_dir: Optional[str] = None,
) -> Dict[str, Any]:
    """
    Run m2c using objdiff JSON output (new pipeline with better relocation handling).

    This pipeline:
    1. Reads pre-computed objdiff JSON (with --include-instructions)
    2. Converts to m2c format via objdiff_to_m2c.py (with jump table resolution)
    3. Runs m2c with optional type context

    Args:
        symbol: Mangled symbol
        objdiff_json_path: Path to objdiff JSON output file
        worktree_dir: Worktree directory for output file
        m2c_path: Path to m2c.py
        objdiff_to_m2c_path: Path to objdiff_to_m2c.py converter
        context_file: Optional path to type context header for m2c

    Returns:
        Dict with keys:
        - inline: str - Code to include in prompt
        - file_path: str - Absolute path to output file
        - file_path_relative: str - Relative path for display
        - line_count: int - Number of lines
        - success: bool - Whether m2c succeeded
        - method: str - "objdiff_json" on success
    """
    result = {
        "inline": "(m2c not run)",
        "file_path": "(not written)",
        "file_path_relative": "(not written)",
        "line_count": 0,
        "success": False,
        "method": "none",
    }

    # Validate paths
    if not m2c_path.exists():
        result["inline"] = "(m2c not available)"
        return result

    if not objdiff_to_m2c_path.exists():
        result["inline"] = "(objdiff_to_m2c converter not available)"
        return result

    if not objdiff_json_path or not Path(objdiff_json_path).exists():
        result["inline"] = "(objdiff JSON file not available)"
        return result

    # Parse symbol to get method name for output file
    class_name, method_name = parse_msvc_symbol(symbol)
    if not method_name:
        result["inline"] = "(could not parse symbol for m2c)"
        return result

    try:
        # Read objdiff JSON
        with open(objdiff_json_path, 'r') as f:
            json_content = f.read()

        # The objdiff output may have build messages before the JSON line
        # Find the actual JSON object
        json_line = None
        for line in json_content.split("\n"):
            line = line.strip()
            if line.startswith("{") and ("instructions" in line or "symbol" in line):
                json_line = line
                break

        if not json_line:
            result["inline"] = "(no valid JSON found in objdiff output)"
            return result

        # Step 1: Convert JSON to m2c assembly using objdiff_to_m2c.py
        convert_cmd = ["python3", str(objdiff_to_m2c_path)]
        if project_dir:
            convert_cmd.extend(["--project-dir", project_dir])
        convert_result = subprocess.run(
            convert_cmd,
            input=json_line,
            capture_output=True,
            text=True,
            timeout=30,
        )

        if convert_result.returncode != 0 or not convert_result.stdout.strip():
            error_msg = convert_result.stderr[:200] if convert_result.stderr else "no output"
            result["inline"] = f"(objdiff_to_m2c failed: {error_msg})"
            return result

        converted_asm = convert_result.stdout

        # Step 2: Run m2c on converted assembly
        m2c_cmd = ["python3", str(m2c_path), "-t", "ppc", "--valid-syntax"]

        # Add context file if available
        if context_file and Path(context_file).exists():
            m2c_cmd.extend(["--context", context_file])
            logger.debug(f"Using type context: {context_file}")

        m2c_cmd.append("-")  # Read from stdin

        m2c_result = subprocess.run(
            m2c_cmd,
            input=converted_asm,
            capture_output=True,
            text=True,
            timeout=60,
        )

        output = m2c_result.stdout.strip()

        if m2c_result.returncode != 0 or "Decompilation failure" in output:
            if "Failed to parse instruction" in output:
                for line in output.split("\n"):
                    if "Failed to parse" in line:
                        result["inline"] = f"(m2c: {line.strip()})"
                        return result
            error_msg = m2c_result.stderr[:200] if m2c_result.stderr else output[:200]
            result["inline"] = f"(m2c error: {error_msg})"
            return result

        if not output:
            result["inline"] = "(m2c produced no output)"
            return result

        # Add header noting which pipeline was used
        context_note = f"\n// Type context: {Path(context_file).name}" if context_file else ""
        full_output = (
            f"// m2c decompilation of {symbol}\n"
            f"// Pipeline: objdiff JSON → objdiff_to_m2c.py → m2c{context_note}\n"
            f"// NOTE: This is auto-generated and needs refinement\n\n"
            f"{output}"
        )

        lines = full_output.split("\n")
        result["line_count"] = len(lines)
        result["success"] = True
        result["method"] = "objdiff_json"

        # Write to file
        analysis_dir = Path(worktree_dir) / "function_analysis"
        analysis_dir.mkdir(exist_ok=True, parents=True)

        safe_name = method_name
        if class_name:
            safe_name = f"{class_name}_{method_name}"
        m2c_file = analysis_dir / f"m2c_{safe_name}.cpp"

        with open(m2c_file, 'w') as f:
            f.write(full_output)

        result["file_path"] = str(m2c_file)
        result["file_path_relative"] = f"function_analysis/m2c_{safe_name}.cpp"

        # Include inline if under character budget, otherwise just preview + file reference
        if len(full_output) <= SECTION_CHAR_BUDGET:
            result["inline"] = full_output
        else:
            # Create preview with first PREVIEW_LINES lines
            preview = "\n".join(lines[:PREVIEW_LINES])
            remaining = len(lines) - PREVIEW_LINES
            result["inline"] = (
                f"{preview}\n\n"
                f"// ... ({remaining} more lines)\n"
                f"// Full m2c output: {result['file_path_relative']}\n"
                f"// View with: cat {result['file_path_relative']}"
            )

        return result

    except subprocess.TimeoutExpired:
        result["inline"] = "(m2c pipeline timed out)"
        return result
    except Exception as e:
        logger.warning(f"m2c objdiff pipeline failed: {e}")
        result["inline"] = f"(m2c pipeline error: {e})"
        return result


DEFAULT_PROJECT_DIR = str(_PROJECT_ROOT)


def run_m2c_decompile(
    symbol: str,
    unit: str,
    worktree_dir: str,
    m2c_path: Path = DEFAULT_M2C_PATH,
    asm_to_m2c_path: Path = DEFAULT_ASM_TO_M2C,
    asm_dir: Path = DEFAULT_ASM_DIR,
    objdiff_json_path: Optional[str] = None,
    use_type_context: bool = True,
    project_dir: str = DEFAULT_PROJECT_DIR,
) -> Dict[str, Any]:
    """
    Run m2c decompiler on assembly to generate initial C code.

    Always writes output to a file in the worktree. Returns inline content
    if output is under 1000 lines, otherwise just the file path.

    Implements a fallback chain:
    1. Try objdiff JSON pipeline (better relocation handling) if objdiff_json_path provided
    2. Fall back to asm file pipeline (existing behavior)

    Args:
        symbol: Mangled symbol
        unit: Unit path (e.g., "system/char/CharBonesMeshes")
        worktree_dir: Path to worktree directory for output file
        m2c_path: Path to m2c.py
        asm_to_m2c_path: Path to asm_to_m2c.py converter
        asm_dir: Path to assembly directory
        objdiff_json_path: Path to pre-computed objdiff JSON output (enables new pipeline)
        use_type_context: Whether to generate Ghidra type context (default: True)

    Returns:
        Dict with keys:
        - inline: str - Code to include in prompt (or error/file reference message)
        - file_path: str - Absolute path to output file (or "(not written)")
        - file_path_relative: str - Relative path for display
        - line_count: int - Number of lines in output
        - success: bool - Whether m2c succeeded
        - method: str - Which pipeline was used ("objdiff_json", "asm_file", or "none")
    """
    result = {
        "inline": "(m2c not run)",
        "file_path": "(not written)",
        "file_path_relative": "(not written)",
        "line_count": 0,
        "success": False,
        "method": "none",
    }

    if not m2c_path.exists():
        result["inline"] = "(m2c not available)"
        return result

    # Generate type context if enabled (non-blocking)
    context_file = None
    if use_type_context:
        try:
            context_file = generate_type_context(symbol, worktree_dir)
            if context_file:
                logger.debug(f"Generated type context: {context_file}")
        except Exception as e:
            logger.debug(f"Type context generation failed (continuing without): {e}")

    # Try objdiff JSON pipeline first (new, better relocation handling)
    if objdiff_json_path and Path(objdiff_json_path).exists():
        logger.debug(f"Trying objdiff JSON pipeline with: {objdiff_json_path}")
        try:
            json_result = run_m2c_from_objdiff_json(
                symbol=symbol,
                objdiff_json_path=objdiff_json_path,
                worktree_dir=worktree_dir,
                m2c_path=m2c_path,
                context_file=context_file,
                project_dir=project_dir,
            )
            if json_result.get("success"):
                logger.info(f"m2c succeeded via objdiff JSON pipeline")
                return json_result
            else:
                logger.debug(f"objdiff JSON pipeline failed: {json_result.get('inline')}")
        except Exception as e:
            logger.debug(f"objdiff JSON pipeline error: {e}")

    # Fall back to asm file pipeline (original behavior)
    logger.debug("Falling back to asm file pipeline")

    if not asm_to_m2c_path.exists():
        result["inline"] = "(asm_to_m2c converter not available)"
        return result

    # Find assembly file from unit path
    # Unit: "default/system/char/CharBonesMeshes" or "system/char/CharBonesMeshes"
    unit_clean = unit.replace("default/", "")
    asm_file = asm_dir / f"{unit_clean}.s"

    if not asm_file.exists():
        result["inline"] = f"(assembly file not found: {asm_file})"
        return result

    # Parse symbol to get method name for extraction
    class_name, method_name = parse_msvc_symbol(symbol)
    if not method_name:
        result["inline"] = "(could not parse symbol for m2c)"
        return result

    try:
        # Step 1: Convert assembly using asm_to_m2c.py
        # Try increasingly specific filters to avoid matching wrong functions
        # Order: method@class (MSVC style) -> class_method -> method (least specific)
        convert_result = None
        converted_asm = ""

        filter_patterns = []
        if class_name:
            # Most specific: MSVC-style method@class pattern
            filter_patterns.append(f"{method_name}@{class_name}")
            # Also try class_method format
            filter_patterns.append(f"{class_name}_{method_name}")
        # Least specific fallback
        filter_patterns.append(method_name)

        for pattern in filter_patterns:
            convert_result = subprocess.run(
                ["python3", str(asm_to_m2c_path), str(asm_file), "-f", pattern],
                capture_output=True,
                text=True,
                timeout=30,
            )
            if convert_result.stdout.strip():
                converted_asm = convert_result.stdout
                break

        if not converted_asm:
            result["inline"] = f"(function {method_name} not found in assembly)"
            return result

        # Step 2: Run m2c on converted assembly
        m2c_cmd = ["python3", str(m2c_path), "-t", "ppc", "--valid-syntax"]

        # Add context file if available
        if context_file and Path(context_file).exists():
            m2c_cmd.extend(["--context", context_file])

        m2c_cmd.append("-")  # Read from stdin

        m2c_result = subprocess.run(
            m2c_cmd,
            input=converted_asm,
            capture_output=True,
            text=True,
            timeout=60,
        )

        output = m2c_result.stdout.strip()

        if m2c_result.returncode != 0 or "Decompilation failure" in output:
            # m2c outputs error to stdout as a comment when it fails
            if "Failed to parse instruction" in output:
                # Extract the specific parse error
                lines = output.split("\n")
                for line in lines:
                    if "Failed to parse" in line:
                        result["inline"] = f"(m2c: {line.strip()})"
                        return result
            error_msg = m2c_result.stderr[:200] if m2c_result.stderr else output[:200]
            result["inline"] = f"(m2c error: {error_msg})"
            return result

        if not output:
            result["inline"] = "(m2c produced no output)"
            return result

        # Add header to output
        context_note = f"\n// Type context: {Path(context_file).name}" if context_file else ""
        full_output = (
            f"// m2c decompilation of {symbol}\n"
            f"// Pipeline: asm file → asm_to_m2c.py → m2c{context_note}\n"
            f"// NOTE: This is auto-generated and needs refinement\n\n"
            f"{output}"
        )

        lines = full_output.split("\n")
        result["line_count"] = len(lines)
        result["success"] = True
        result["method"] = "asm_file"

        # Always write to file in worktree
        analysis_dir = Path(worktree_dir) / "function_analysis"
        analysis_dir.mkdir(exist_ok=True, parents=True)

        # Create safe filename from symbol
        safe_name = method_name
        if class_name:
            safe_name = f"{class_name}_{method_name}"
        m2c_file = analysis_dir / f"m2c_{safe_name}.cpp"

        with open(m2c_file, 'w') as f:
            f.write(full_output)

        result["file_path"] = str(m2c_file)
        result["file_path_relative"] = f"function_analysis/m2c_{safe_name}.cpp"

        # Include inline if under character budget, otherwise preview + file reference
        if len(full_output) <= SECTION_CHAR_BUDGET:
            result["inline"] = full_output
        else:
            # Create preview with first PREVIEW_LINES lines
            preview = "\n".join(lines[:PREVIEW_LINES])
            remaining = len(lines) - PREVIEW_LINES
            result["inline"] = (
                f"{preview}\n\n"
                f"// ... ({remaining} more lines)\n"
                f"// Full m2c output: {result['file_path_relative']}\n"
                f"// View with: cat {result['file_path_relative']}"
            )

        return result

    except subprocess.TimeoutExpired:
        result["inline"] = "(m2c timed out)"
        return result
    except Exception as e:
        logger.warning(f"m2c decompilation failed: {e}")
        result["inline"] = f"(m2c error: {e})"
        return result


def extract_key_patterns(objdiff_result: Any) -> List[str]:
    """
    Extract key patterns from objdiff result verdict dict.

    Identifies unfixable patterns like ASSERT_REVS, LTCG, etc. that
    help agent decide whether to attempt edits.

    Args:
        objdiff_result: ObjdiffResult object from run_objdiff()

    Returns:
        List of pattern strings describing fixability
    """
    patterns = []

    if not objdiff_result or not hasattr(objdiff_result, 'verdict'):
        return patterns

    verdict = objdiff_result.verdict
    if not verdict:
        return patterns

    # Extract classification
    classification = verdict.get("classification", "")
    if classification:
        patterns.append(f"Classification: {classification}")

    # Extract analysis patterns (from detailed analysis)
    analysis = objdiff_result.analysis if hasattr(objdiff_result, 'analysis') else None
    if analysis and isinstance(analysis, dict):
        # Check for patterns that indicate fixability issues
        patterns_list = analysis.get("patterns", [])
        if isinstance(patterns_list, list):
            for pattern_obj in patterns_list:
                if isinstance(pattern_obj, dict):
                    pattern_name = pattern_obj.get("pattern", "")
                    fixability = pattern_obj.get("fixability", "")
                    if pattern_name:
                        if fixability:
                            patterns.append(f"{pattern_name} - {fixability}")
                        else:
                            patterns.append(pattern_name)

    # Extract from explanation if present
    explanation = verdict.get("explanation", "")
    if explanation and "ASSERT_REVS" in explanation:
        patterns.append("ASSERT_REVS - instruction scheduling (unfixable)")
    if explanation and "LTCG" in explanation:
        patterns.append("LTCG - link-time optimizations (may be unfixable)")

    return patterns if patterns else ["No specific patterns identified"]


# =============================================================================
# Experiment 1: Enhanced Diff Pattern Classification
# =============================================================================
# Pre-classifies diff patterns to help agents quickly identify fixability.

# Pattern definitions with fixability guidance
DIFF_PATTERNS = {
    # Unfixable patterns - compiler/linker artifacts
    "SCHEDULING": {
        "fixability": "UNFIXABLE",
        "description": "Instruction scheduling differences from compiler optimizations",
        "indicators": [
            r"addi.*reordered",
            r"instruction order differs",
            r"pipeline scheduling",
        ],
        "guidance": "These are compiler optimization choices. Cannot be fixed through source changes.",
    },
    "LINKER_MERGED": {
        "fixability": "UNFIXABLE",
        "description": "Identical call sequences merged by linker",
        "indicators": [
            r"bl .* merged",
            r"identical functions merged",
            r"LTCG",
            r"link-time",
        ],
        "guidance": "LTCG merged identical call patterns. Source-level fix impossible.",
    },
    "ASSERT_REVS": {
        "fixability": "NEAR_UNFIXABLE",
        "description": "ASSERT_REVS/LOAD_REVS macro scheduling differences",
        "indicators": [
            r"gRevs",
            r"ASSERT_REVS",
            r"LOAD_REVS",
            r"version.*scheduling",
        ],
        "guidance": "Expect ~0.8-1% mismatch. Accept 99%+ as functionally complete.",
    },
    # Potentially fixable patterns
    "BOOL_MASK": {
        "fixability": "FIXABLE",
        "description": "Boolean mask operations (rlwinm for bool narrowing)",
        "indicators": [
            r"rlwinm.*,0x1",
            r"bool.*mask",
            r"clrlwi",
        ],
        "guidance": "Try: cast to (unsigned char), use !! operator, or explicit & 0xFF.",
    },
    "REGISTER_SWAP": {
        "fixability": "MAYBE_FIXABLE",
        "description": "Different register allocation",
        "indicators": [
            r"r\d+ vs r\d+",
            r"register.*different",
            r"same instruction.*different register",
        ],
        "guidance": "May be fixable by reordering variable declarations or operations.",
    },
    "STRUCT_OFFSET": {
        "fixability": "FIXABLE",
        "description": "Wrong struct member offset",
        "indicators": [
            r"0x[0-9a-f]+\(r\d+\) vs 0x[0-9a-f]+\(r\d+\)",
            r"offset.*mismatch",
            r"stw.*different offset",
            r"lwz.*different offset",
        ],
        "guidance": "Check struct layout. Use lookup_struct_offset tool to identify field.",
    },
    "BRANCH_CONDITION": {
        "fixability": "FIXABLE",
        "description": "Different branch condition (beq vs bne, etc.)",
        "indicators": [
            r"beq.*bne",
            r"blt.*bge",
            r"condition.*inverted",
        ],
        "guidance": "Fix comparison logic. Common: (x != 0) vs (x > 0) for unsigned.",
    },
    "CONTROL_FLOW": {
        "fixability": "MAYBE_FIXABLE",
        "description": "Different control flow structure",
        "indicators": [
            r"control flow",
            r"branch.*target",
            r"missing.*branch",
        ],
        "guidance": "Restructure if/else, loops, or early returns to match.",
    },
    "STACK_FRAME": {
        "fixability": "MAYBE_FIXABLE",
        "description": "Different stack frame size or layout",
        "indicators": [
            r"stwu.*r1",
            r"stack.*frame",
            r"spill",
        ],
        "guidance": "May be caused by extra local variables or calling convention issues.",
    },
}


# objdiff's own pattern names -> the guidance we have for them. objdiff-cli
# --verdict already classifies; where its name and ours describe the same thing
# we reuse our guidance text, and where it does not we pass objdiff's through.
_OBJDIFF_PATTERN_GUIDANCE = {
    "OFFSET_SWAP": "STRUCT_OFFSET",
    "REGISTER_SWAP": "REGISTER_SWAP",
    "BOOL_MASK": "BOOL_MASK",
    "CONTROL_FLOW": "CONTROL_FLOW",
    "LINKER_MERGED": "LINKER_MERGED",
    "PROLOGUE_MISMATCH": "STACK_FRAME",
}

# objdiff's per-pattern `fixability` -> our four-level vocabulary.
_OBJDIFF_FIXABILITY = {
    "fixable": "FIXABLE",
    "likely_fixable": "FIXABLE",
    "maybe_fixable": "MAYBE_FIXABLE",
    "near_unfixable": "NEAR_UNFIXABLE",
    "unfixable": "UNFIXABLE",
}


def _looks_like_objdiff_json(text: str) -> bool:
    """True if `text` is (the beginning of) objdiff-cli's `-f json` output.

    Deliberately shape-based rather than a json.loads: the caller hands us a
    4KB PREFIX of a 160KB single-line document, so it never parses."""
    t = (text or "").lstrip()
    return t.startswith("{") and '"fuzzy_match_percent"' in t[:2000]


def _classify_from_objdiff_analysis(analysis: Dict[str, Any]) -> List[Dict[str, Any]]:
    """Pattern dicts built from objdiff's OWN structured analysis."""
    detected = []
    for pat in analysis.get("patterns") or []:
        if not isinstance(pat, dict):
            continue
        name = pat.get("pattern") or "UNKNOWN"
        ours = DIFF_PATTERNS.get(_OBJDIFF_PATTERN_GUIDANCE.get(name, ""), {})
        detected.append({
            "pattern": name,
            "fixability": _OBJDIFF_FIXABILITY.get(
                str(pat.get("fixability", "")).lower(),
                ours.get("fixability", "MAYBE_FIXABLE")),
            "description": pat.get("details") or ours.get(
                "description", f"objdiff pattern {name}"),
            "guidance": ours.get("guidance") or pat.get("doc_url")
                        or "See objdiff --verdict output for detail.",
            "source": "objdiff",
        })
    return detected


def classify_diff_patterns(
    objdiff_output: str,
    objdiff_result: Any = None,
    objdiff_json: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    """
    Classify diff patterns from objdiff output (Experiment 1).

    Prefers objdiff's OWN structured analysis (`objdiff_json["analysis"]`,
    produced by `--verdict`). Falls back to the regex table below only for
    genuine TEXT output.

    NEVER runs the regex table against JSON. Measured 2026-08-17 on four real
    dc3 symbols: against the 4KB JSON preview the table fired on 6/6 symbols
    -- including `CopyTypeProperties` at fuzzy 100.00%, where objdiff reports
    zero patterns -- because the indicators were matching objdiff's own schema
    vocabulary. `offset.*mismatch` hit the string
    `OFFSET_SWAP",...,"DYNAMIC_CAST_MISMATCH"` inside the `patterns_checked`
    enum list; `bool.*mask` hit the literal `BOOL_MASK` in that same list;
    `stack.*frame` hit a doc_url. Those are not diff content, and `.` does not
    stop at a newline because the document has none. The table was also
    ANTI-correlated on the one pattern it shares: objdiff reported REGISTER_SWAP
    on 4/6 and the table on 0/6, since the `r<N> vs r<N>` indicator needs a
    " vs " the JSON
    never contains.

    Args:
        objdiff_output: objdiff TEXT output. A JSON prefix is detected and
            refused rather than pattern-matched.
        objdiff_result: Optional ObjdiffResult object for additional data
        objdiff_json: Parsed objdiff-cli `-f json` document. Preferred input.

    Returns:
        Dict with:
        - patterns: List of detected pattern dicts
        - summary: Human-readable summary
        - fixability: Overall fixability assessment
        - guidance: Prioritized list of suggested actions
        - source: "objdiff" | "text-heuristics" | "none"
    """
    result = {
        "patterns": [],
        "summary": "",
        "fixability": "UNKNOWN",
        "guidance": [],
        "source": "none",
    }

    analysis = (objdiff_json or {}).get("analysis") if isinstance(objdiff_json, dict) else None
    if isinstance(analysis, dict):
        detected = _classify_from_objdiff_analysis(analysis)
        result["source"] = "objdiff"
    elif not objdiff_output:
        result["summary"] = "No objdiff output to analyze"
        return result
    elif _looks_like_objdiff_json(objdiff_output):
        # Fail loud rather than fabricate. The caller has the parsed document
        # (run_objdiff_cli returns it as ["json"]) and should pass it.
        result["summary"] = ("objdiff JSON was passed as text and no parsed "
                             "document was supplied -- not classified. The "
                             "regex table below reads TEXT output only.")
        return result
    else:
        output_lower = objdiff_output.lower()
        detected = []
        result["source"] = "text-heuristics"

        # Check each pattern
        for pattern_name, pattern_info in DIFF_PATTERNS.items():
            for indicator in pattern_info["indicators"]:
                if re.search(indicator, output_lower, re.IGNORECASE):
                    detected.append({
                        "pattern": pattern_name,
                        "fixability": pattern_info["fixability"],
                        "description": pattern_info["description"],
                        "guidance": pattern_info["guidance"],
                        "source": "text-heuristics",
                    })
                    break  # Only add each pattern once

    result["patterns"] = detected

    # Determine overall fixability
    if not detected:
        result["fixability"] = "UNKNOWN"
        result["summary"] = "No specific patterns detected. Manual analysis needed."
    else:
        # Priority: UNFIXABLE > NEAR_UNFIXABLE > MAYBE_FIXABLE > FIXABLE
        fixabilities = [p["fixability"] for p in detected]
        if "UNFIXABLE" in fixabilities:
            result["fixability"] = "AT_LIMIT"
            result["summary"] = f"Contains unfixable patterns: {', '.join(p['pattern'] for p in detected if p['fixability'] == 'UNFIXABLE')}"
        elif "NEAR_UNFIXABLE" in fixabilities:
            result["fixability"] = "NEAR_LIMIT"
            result["summary"] = "Contains near-unfixable patterns. 99%+ may be best achievable."
        elif "MAYBE_FIXABLE" in fixabilities:
            result["fixability"] = "MAYBE_FIXABLE"
            result["summary"] = "Contains potentially fixable patterns. Try suggested approaches."
        else:
            result["fixability"] = "LIKELY_FIXABLE"
            result["summary"] = "All detected patterns appear fixable."

    # Generate prioritized guidance
    # Order: FIXABLE first (quick wins), then MAYBE_FIXABLE, skip UNFIXABLE
    for fix_level in ["FIXABLE", "MAYBE_FIXABLE"]:
        for p in detected:
            if p["fixability"] == fix_level:
                result["guidance"].append(f"[{p['pattern']}] {p['guidance']}")

    return result


def format_pattern_classification(classification: Dict[str, Any]) -> str:
    """
    Format pattern classification as a compact prompt section.

    Args:
        classification: Result from classify_diff_patterns()

    Returns:
        Formatted markdown string for prompt injection
    """
    lines = [
        "## Diff Pattern Analysis",
        "",
        f"**Overall Assessment**: {classification['fixability']}",
        f"**Summary**: {classification['summary']}",
        "",
    ]

    if classification["patterns"]:
        lines.append("### Detected Patterns")
        lines.append("")
        lines.append("| Pattern | Fixability | Description |")
        lines.append("|---------|------------|-------------|")
        for p in classification["patterns"]:
            lines.append(f"| {p['pattern']} | {p['fixability']} | {p['description']} |")
        lines.append("")

    if classification["guidance"]:
        lines.append("### Suggested Actions (Priority Order)")
        lines.append("")
        for i, g in enumerate(classification["guidance"], 1):
            lines.append(f"{i}. {g}")
        lines.append("")

    return "\n".join(lines)


# =============================================================================
# Experiment 2: Function Type Templates
# =============================================================================
# Type-specific guidance based on function name patterns (Load/Save/Init/Poll).

FUNCTION_TYPE_TEMPLATES = {
    "LOAD": {
        "patterns": [r"^Load$", r"^Load[A-Z]", r"::Load\("],
        "description": "Binary deserialization function",
        "guidance": """
## Function Type: LOAD (Binary Deserialization)

**Common patterns in Milo engine Load functions:**

1. **Version checking**: `LOAD_REVS(bs)` / `ASSERT_REVS(min, max)`
   - Always check if version macros match expected revision
   - Version mismatches cause instruction scheduling differences (~1%)

2. **Field reads**: Use `bs >> field` operator overloading
   - Order matters: fields read in exact order they were written
   - Check parent class Load() calls first

3. **Conditional reads**: Version-gated data
   ```cpp
   if (gRev >= 2) {
       bs >> mNewField;
   }
   ```

4. **Common issues**:
   - Wrong ASSERT_REVS values (check target binary for actual revision)
   - Missing or extra fields
   - Incorrect type for >> operator

**Expected mismatch**: 0.5-1% for ASSERT_REVS scheduling differences is normal.
""",
    },
    "SAVE": {
        "patterns": [r"^Save$", r"^Save[A-Z]", r"::Save\("],
        "description": "Binary serialization function",
        "guidance": """
## Function Type: SAVE (Binary Serialization)

**Common patterns in Milo engine Save functions:**

1. **Version header**: `SAVE_REVS(bs)` writes revision number
   - Must match the revision in header

2. **Field writes**: Use `bs << field` operator overloading
   - Mirror order of Load() function exactly

3. **Conditional writes**: Match Load() version gates
   ```cpp
   if (gRev >= 2) {
       bs << mNewField;
   }
   ```

4. **Common issues**:
   - Field order mismatch with Load()
   - Missing parent class Save() call
   - Type mismatches in << operator
""",
    },
    "INIT": {
        "patterns": [r"^Init$", r"::Init\(", r"^Initialize"],
        "description": "Initialization function",
        "guidance": """
## Function Type: INIT (Initialization)

**Common patterns in Milo engine Init functions:**

1. **Parent init**: Call parent class Init() first
   - `ParentClass::Init();`

2. **Member initialization**: Set default values
   - Use initializer list in constructor or explicit assignment

3. **Resource setup**: Register callbacks, create objects
   - Check for ObjectDir/DataArray usage

4. **Common issues**:
   - Missing parent Init() call
   - Wrong initialization order
   - Incorrect default values (check DWARF for field defaults)
""",
    },
    "POLL": {
        "patterns": [r"^Poll$", r"::Poll\(", r"^Update$"],
        "description": "Per-frame update function",
        "guidance": """
## Function Type: POLL (Per-Frame Update)

**Common patterns in Milo engine Poll functions:**

1. **Delta time**: Usually receives `float dt` for time-based updates

2. **State updates**: Modify mutable state based on time
   - Animation advancement
   - Physics/position updates

3. **Conditional processing**: Often early-out patterns
   ```cpp
   if (!mEnabled) return;
   ```

4. **Common issues**:
   - Wrong dt multiplication for time-based values
   - Missing parent Poll() call
   - Incorrect conditional checks
""",
    },
    "CONSTRUCTOR": {
        "patterns": [r"^\?\?0", r"^ctor$"],
        "description": "Class constructor",
        "guidance": """
## Function Type: CONSTRUCTOR

**Common patterns in Milo engine constructors:**

1. **Initializer list**: Members initialized before body
   - Order matches declaration order in class

2. **Parent construction**: Base class constructor call
   - `ClassName() : ParentClass() { ... }`

3. **Default values**: Set member initial values
   - Check RB2 DWARF for expected defaults

4. **Common issues**:
   - Wrong initializer list order (compiler warning)
   - Missing parent constructor
   - Incorrect default values
""",
    },
    "DESTRUCTOR": {
        "patterns": [r"^\?\?1", r"^dtor$", r"^~"],
        "description": "Class destructor",
        "guidance": """
## Function Type: DESTRUCTOR

**Common patterns in Milo engine destructors:**

1. **Cleanup order**: Reverse of construction
   - Release resources in reverse order

2. **Parent destructor**: Called automatically (don't call explicitly)

3. **Null checks**: Often check before delete
   ```cpp
   if (mResource) delete mResource;
   ```

4. **Common issues**:
   - Virtual destructor not declared
   - Double-delete of resources
   - Missing cleanup of registered callbacks
""",
    },
    "COPY": {
        "patterns": [r"^Copy$", r"::Copy\("],
        "description": "Deep copy function",
        "guidance": """
## Function Type: COPY

**Common patterns in Milo engine Copy functions:**

1. **Parent copy**: Call parent Copy() first
   - `ParentClass::Copy(src, ...);`

2. **Member copy**: Deep copy all relevant members
   - Check for pointer members that need cloning

3. **Type checking**: Often includes type verification
   - `dynamic_cast` or type field check

4. **Common issues**:
   - Missing parent Copy() call
   - Shallow copy of pointer members
   - Missing type check
""",
    },
}


def classify_function_type(symbol: str, demangled: Optional[str] = None) -> Dict[str, Any]:
    """
    Classify function by type and return type-specific guidance (Experiment 2).

    Args:
        symbol: Mangled symbol
        demangled: Optional demangled name

    Returns:
        Dict with:
        - type: Function type identifier
        - description: Short description
        - guidance: Detailed guidance text
    """
    result = {
        "type": "GENERIC",
        "description": "Generic function",
        "guidance": "",
    }

    # Parse symbol to get method name
    class_name, method_name = parse_msvc_symbol(symbol)

    # Build list of strings to search
    search_targets = []
    if method_name:
        search_targets.append(method_name)  # Just the method name (e.g., "Load")
    if demangled:
        search_targets.append(demangled)
    elif class_name and method_name:
        search_targets.append(f"{class_name}::{method_name}")
    search_targets.append(symbol)  # Also check raw symbol

    if not search_targets:
        return result

    # Check each function type pattern against all search targets
    for type_name, type_info in FUNCTION_TYPE_TEMPLATES.items():
        for pattern in type_info["patterns"]:
            for target in search_targets:
                if re.search(pattern, target, re.IGNORECASE):
                    result["type"] = type_name
                    result["description"] = type_info["description"]
                    result["guidance"] = type_info["guidance"].strip()
                    return result

    return result


def format_function_type_guidance(classification: Dict[str, Any]) -> str:
    """
    Format function type guidance for prompt injection.

    Args:
        classification: Result from classify_function_type()

    Returns:
        Formatted guidance string (or empty if generic)
    """
    if classification["type"] == "GENERIC":
        return ""

    return classification["guidance"]


# =============================================================================
# Experiment 3: RB2 Class Layouts
# =============================================================================
# Pre-computed struct layouts from RB2 DWARF to prevent offset mismatch debugging loops.

# Import RB2 DWARF parser (optional)
try:
    from scripts.orchestrator.rb2_dwarf import lookup_rb2_class, lookup_rb2_offset
except ImportError:
    lookup_rb2_class = None
    lookup_rb2_offset = None


def get_class_layout(class_name: str) -> Dict[str, Any]:
    """
    Get class layout from RB2 DWARF data (Experiment 3).

    Args:
        class_name: Class name to look up

    Returns:
        Dict with:
        - found: bool - whether class was found
        - class_name: str
        - total_size: int
        - members: list of member dicts
        - parents: list of parent class names
    """
    result = {
        "found": False,
        "class_name": class_name,
        "total_size": 0,
        "members": [],
        "parents": [],
    }

    if not lookup_rb2_class:
        result["error"] = "RB2 DWARF parser not available"
        return result

    try:
        class_info = lookup_rb2_class(class_name)
        if not class_info:
            result["error"] = f"Class '{class_name}' not found in RB2 DWARF"
            return result

        result["found"] = True
        result["total_size"] = class_info.get("total_size", 0)
        result["members"] = class_info.get("members", [])
        result["parents"] = class_info.get("parents", [])

    except Exception as e:
        result["error"] = str(e)

    return result


def format_class_layout(layout: Dict[str, Any]) -> str:
    """
    Format class layout as markdown table for prompt injection.

    Args:
        layout: Result from get_class_layout()

    Returns:
        Formatted markdown string
    """
    if not layout["found"]:
        return f"(Class layout not available: {layout.get('error', 'unknown error')})"

    lines = [
        f"## RB2 Class Layout: {layout['class_name']}",
        "",
        f"**Total size**: 0x{layout['total_size']:X} ({layout['total_size']} bytes)",
    ]

    if layout["parents"]:
        lines.append(f"**Parents**: {', '.join(layout['parents'])}")

    lines.append("")

    if layout["members"]:
        lines.append("### Member Offsets")
        lines.append("")
        lines.append("| Offset | Size | Name | Type |")
        lines.append("|--------|------|------|------|")

        for member in layout["members"]:
            offset = member.get("offset", 0)
            size = member.get("size", 0)
            name = member.get("name", "?")
            mtype = member.get("type", "?")
            # Truncate long type names
            if len(mtype) > 40:
                mtype = mtype[:37] + "..."
            lines.append(f"| 0x{offset:X} | 0x{size:X} | {name} | {mtype} |")

        lines.append("")
        lines.append("*Note: Offsets from RB2 DWARF. DC3 offsets may differ slightly.*")
    else:
        lines.append("(No members found)")

    return "\n".join(lines)


# =============================================================================
# Experiment 4: Previous Attempt Diffs
# =============================================================================
# Show actual code diffs from previous attempts to prevent repeating failed approaches.

# Max diff length per attempt to avoid token explosion
MAX_DIFF_CHARS_PER_ATTEMPT = 1500


def get_previous_attempt_diffs(
    symbol: str,
    project_dir: str,
    max_attempts: int = 3,
) -> Dict[str, Any]:
    """
    Get previous attempts with actual code diffs (Experiment 4).

    Args:
        symbol: Function symbol
        project_dir: Project directory
        max_attempts: Maximum number of attempts to include

    Returns:
        Dict with:
        - count: Number of attempts found
        - attempts: List of attempt dicts with diffs
        - summary: Formatted summary string
    """
    result = {
        "count": 0,
        "attempts": [],
        "summary": "(no previous attempts)",
    }

    if not get_function_by_symbol or not get_attempts_for_function:
        result["summary"] = "(database not available)"
        return result

    try:
        db_path = Path(project_dir) / "decomp.db"
        if not db_path.exists():
            return result

        func = get_function_by_symbol(symbol, db_path=str(db_path))
        if not func:
            return result

        # Get attempts with patches
        from scripts.orchestrator.database import get_connection
        conn = get_connection(str(db_path))

        rows = conn.execute(
            """
            SELECT id, model, start_percent, end_percent, exit_status, patch, notes
            FROM attempts
            WHERE function_id = ?
              AND patch IS NOT NULL
              AND patch != ''
            ORDER BY created_at DESC
            LIMIT ?
            """,
            (func["id"], max_attempts),
        ).fetchall()

        if not rows:
            result["summary"] = "(no attempts with patches found)"
            return result

        result["count"] = len(rows)
        attempts = []

        for row in rows:
            attempt = {
                "model": row["model"],
                "start_percent": row["start_percent"] or 0,
                "end_percent": row["end_percent"] or 0,
                "exit_status": row["exit_status"],
                "notes": row["notes"] or "",
                "patch": row["patch"],
            }

            # Truncate patch if too long
            if len(attempt["patch"]) > MAX_DIFF_CHARS_PER_ATTEMPT:
                attempt["patch_truncated"] = True
                attempt["patch"] = attempt["patch"][:MAX_DIFF_CHARS_PER_ATTEMPT] + "\n... (truncated)"
            else:
                attempt["patch_truncated"] = False

            attempts.append(attempt)

        result["attempts"] = attempts

    except Exception as e:
        logger.warning(f"Error getting attempt diffs: {e}")
        result["summary"] = f"(error: {e})"
        return result

    return result


def format_previous_attempt_diffs(attempt_data: Dict[str, Any]) -> str:
    """
    Format previous attempt diffs for prompt injection.

    Args:
        attempt_data: Result from get_previous_attempt_diffs()

    Returns:
        Formatted markdown string
    """
    if attempt_data["count"] == 0:
        return attempt_data["summary"]

    lines = [
        "## Previous Attempt Diffs",
        "",
        f"**Found {attempt_data['count']} previous attempts with code changes.**",
        "",
        "Review these to avoid repeating failed approaches:",
        "",
    ]

    for i, attempt in enumerate(attempt_data["attempts"], 1):
        gain = attempt["end_percent"] - attempt["start_percent"]
        gain_str = f"+{gain:.1f}%" if gain > 0 else f"{gain:.1f}%"

        lines.append(f"### Attempt {i}: {attempt['model']} ({gain_str})")
        lines.append("")
        lines.append(f"- **Result**: {attempt['start_percent']:.1f}% → {attempt['end_percent']:.1f}% ({attempt['exit_status']})")

        if attempt["notes"]:
            notes = attempt["notes"][:200] + "..." if len(attempt["notes"]) > 200 else attempt["notes"]
            lines.append(f"- **Notes**: {notes}")

        lines.append("")
        lines.append("```diff")
        lines.append(attempt["patch"])
        lines.append("```")
        lines.append("")

        if attempt.get("patch_truncated"):
            lines.append("*(diff truncated for token efficiency)*")
            lines.append("")

    lines.append("---")
    lines.append("**Guidance**: Avoid repeating changes that didn't improve match%. Try different approaches.")

    return "\n".join(lines)


# =============================================================================
# Experiment 5: Matched Siblings
# =============================================================================
# Show 100% matched functions from same class as concrete pattern examples.

# Max source patch size to include
MAX_SIBLING_SOURCE_CHARS = 2000


def get_matched_siblings(
    class_name: str,
    current_symbol: str,
    project_dir: str,
    max_siblings: int = 3,
) -> Dict[str, Any]:
    """
    Get 100% matched functions from the same class (Experiment 5).

    Args:
        class_name: Class name to search for siblings
        current_symbol: Current symbol to exclude from results
        project_dir: Project directory
        max_siblings: Maximum siblings to include

    Returns:
        Dict with:
        - count: Number of siblings found
        - siblings: List of sibling dicts
        - summary: Formatted summary string
    """
    result = {
        "count": 0,
        "siblings": [],
        "summary": "(no matched siblings found)",
    }

    if not class_name:
        result["summary"] = "(could not determine class name)"
        return result

    try:
        db_path = Path(project_dir) / "decomp.db"
        if not db_path.exists():
            result["summary"] = "(database not available)"
            return result

        from scripts.orchestrator.database import get_connection
        conn = get_connection(str(db_path))

        # Query for 100% matched functions in same class
        # Use LIKE for class name matching in demangled symbols
        rows = conn.execute(
            """
            SELECT symbol, demangled, current_percent, source_patch
            FROM functions
            WHERE current_percent >= 100
              AND symbol != ?
              AND (demangled LIKE ? OR symbol LIKE ?)
              AND source_patch IS NOT NULL
            ORDER BY LENGTH(source_patch) ASC
            LIMIT ?
            """,
            (current_symbol, f"%{class_name}::%", f"%@{class_name}@%", max_siblings),
        ).fetchall()

        if not rows:
            result["summary"] = f"(no 100% matched siblings found for class {class_name})"
            return result

        siblings = []
        for row in rows:
            sibling = {
                "symbol": row["symbol"],
                "demangled": row["demangled"],
                "percent": row["current_percent"],
                "source_patch": row["source_patch"] or "",
            }

            # Truncate source if too long
            if len(sibling["source_patch"]) > MAX_SIBLING_SOURCE_CHARS:
                sibling["source_truncated"] = True
                sibling["source_patch"] = sibling["source_patch"][:MAX_SIBLING_SOURCE_CHARS] + "\n... (truncated)"
            else:
                sibling["source_truncated"] = False

            siblings.append(sibling)

        result["count"] = len(siblings)
        result["siblings"] = siblings

    except Exception as e:
        logger.warning(f"Error getting matched siblings: {e}")
        result["summary"] = f"(error: {e})"

    return result


def format_matched_siblings(sibling_data: Dict[str, Any]) -> str:
    """
    Format matched siblings for prompt injection.

    Args:
        sibling_data: Result from get_matched_siblings()

    Returns:
        Formatted markdown string
    """
    if sibling_data["count"] == 0:
        return sibling_data["summary"]

    lines = [
        "## Matched Sibling Functions",
        "",
        f"**Found {sibling_data['count']} 100% matched functions from the same class.**",
        "",
        "Use these as reference patterns:",
        "",
    ]

    for i, sibling in enumerate(sibling_data["siblings"], 1):
        demangled = sibling.get("demangled", sibling["symbol"])
        # Extract just the method name for header
        method_name = demangled.split("::")[-1].split("(")[0] if "::" in demangled else demangled

        lines.append(f"### {i}. {method_name}")
        lines.append("")
        lines.append(f"**Symbol**: `{sibling['symbol'][:80]}...`" if len(sibling['symbol']) > 80 else f"**Symbol**: `{sibling['symbol']}`")
        lines.append("")

        if sibling["source_patch"]:
            lines.append("**Implementation**:")
            lines.append("```cpp")
            lines.append(sibling["source_patch"])
            lines.append("```")

            if sibling.get("source_truncated"):
                lines.append("*(truncated for token efficiency)*")
        else:
            lines.append("*(no source patch stored)*")

        lines.append("")

    lines.append("---")
    lines.append("**Guidance**: Follow similar patterns from these matched siblings for class member access, initialization, and API usage.")

    return "\n".join(lines)


# =============================================================================
# Experiment 6: Callee Signatures
# =============================================================================
# Pre-resolved callee signatures to reduce agent lookup turns.
# Extracts callees from objdiff JSON (bl instructions) and resolves signatures.

# Limits for callee signature resolution
MAX_CALLEE_SIGNATURES = 10
MAX_SIGNATURE_CHARS = 200


def extract_callees_from_objdiff(objdiff_json: Dict[str, Any]) -> List[str]:
    """
    Extract called function symbols from objdiff JSON output.

    Parses the instructions array looking for 'bl' (branch-link) opcodes
    which represent function calls on PowerPC.

    Args:
        objdiff_json: Parsed objdiff JSON output with --include-instructions

    Returns:
        List of callee symbols (deduplicated, preserving first occurrence order)
    """
    callees = []
    instructions = objdiff_json.get("instructions", [])

    for instr in instructions:
        target = instr.get("target", {})
        opcode = target.get("opcode", "")

        # bl = branch and link (function call)
        if opcode == "bl":
            # Get symbol from typed_args (preferred) or args
            typed_args = target.get("typed_args", [])
            if typed_args and typed_args[0].get("type") == "Symbol":
                symbol = typed_args[0].get("value", "")
                if symbol:
                    callees.append(symbol)
            else:
                # Fallback to args string
                args = target.get("args", "")
                if args and (args.startswith("?") or args.startswith("_")):
                    callees.append(args)

    # Deduplicate while preserving order
    seen = set()
    result = []
    for c in callees:
        if c not in seen:
            seen.add(c)
            result.append(c)
    return result


def _find_function_line_number(
    class_name: Optional[str],
    method_name: str,
    source_path: Path,
) -> Optional[int]:
    """
    Find the line number where a function is defined in a source file.

    Args:
        class_name: Class name (or None for free functions)
        method_name: Method/function name
        source_path: Path to the source file

    Returns:
        Line number (1-indexed) or None if not found
    """
    if not source_path.exists():
        return None

    try:
        content = source_path.read_text()
        lines = content.split('\n')

        # Build patterns to search for
        patterns = []
        if class_name:
            # Standard method definition: ClassName::MethodName(
            patterns.append(f"{class_name}::{method_name}(")
            # Nested class: OuterClass::InnerClass::MethodName(
            patterns.append(f"::{class_name}::{method_name}(")
        else:
            # Free function: just the name with opening paren
            patterns.append(f" {method_name}(")
            patterns.append(f"\n{method_name}(")

        for i, line in enumerate(lines, 1):
            for pattern in patterns:
                if pattern in line:
                    # Verify it looks like a function definition (not a call)
                    # Definition typically has return type before or is at line start
                    stripped = line.strip()
                    if not stripped.startswith("//") and not stripped.startswith("/*"):
                        return i

        return None
    except Exception:
        return None


def resolve_callee_info(symbol: str, project_dir: str) -> Optional[Dict[str, Any]]:
    """
    Resolve a mangled symbol to detailed callee information.

    Returns signature, match percentage, and source location (if implemented).

    Args:
        symbol: Mangled function symbol
        project_dir: Project directory containing decomp.db and src/

    Returns:
        Dict with:
        - signature: Demangled signature string
        - match_percent: Current match percentage (or None)
        - source_location: "path/file.cpp:line" (or None if not found/not 100%)
        Or None if resolution failed entirely
    """
    # Skip runtime/compiler intrinsics (not useful for decomp context)
    if symbol.startswith("__") and not symbol.startswith("__Z"):
        return None

    result = {
        "signature": None,
        "match_percent": None,
        "source_location": None,
    }

    # Parse the symbol for class/method names (needed for line search)
    class_name, method_name = parse_msvc_symbol(symbol)

    # Try database lookup for demangled name, unit, and match percent
    db_path = Path(project_dir) / "decomp.db"
    unit = None
    if db_path.exists():
        try:
            import sqlite3
            conn = sqlite3.connect(str(db_path))
            conn.execute("PRAGMA journal_mode = WAL")
            conn.row_factory = sqlite3.Row
            row = conn.execute(
                "SELECT demangled, unit, current_percent FROM functions WHERE symbol = ?",
                (symbol,)
            ).fetchone()
            conn.close()

            if row:
                demangled = row["demangled"]
                if demangled and demangled != symbol and not demangled.startswith("?"):
                    result["signature"] = demangled

                result["match_percent"] = row["current_percent"]
                unit = row["unit"]
        except Exception:
            pass

    # If no signature yet, try c++filt
    if not result["signature"]:
        try:
            proc_result = subprocess.run(
                ["c++filt", "-n", symbol],
                capture_output=True,
                text=True,
                timeout=1,
            )
            if proc_result.returncode == 0:
                demangled = proc_result.stdout.strip()
                if demangled and demangled != symbol:
                    result["signature"] = demangled
        except Exception:
            pass

    # If still no signature, use parse_msvc_symbol
    if not result["signature"] and method_name:
        if class_name:
            result["signature"] = f"{class_name}::{method_name}"
        else:
            result["signature"] = method_name

    # If we have no signature at all, return None
    if not result["signature"]:
        return None

    # Try to find source location for 100% matched functions
    if result["match_percent"] == 100.0 and unit and method_name:
        # Convert unit path to source file path
        # unit is like "default/system/char/CharMirror" -> "src/system/char/CharMirror.cpp"
        unit_path = unit
        if unit_path.startswith("default/"):
            unit_path = unit_path[8:]  # Remove "default/" prefix

        source_path = Path(project_dir) / "src" / f"{unit_path}.cpp"
        line_num = _find_function_line_number(class_name, method_name, source_path)

        if line_num:
            # Use relative path for cleaner output
            rel_path = f"src/{unit_path}.cpp"
            result["source_location"] = f"{rel_path}:{line_num}"

    return result


def resolve_signature(symbol: str, project_dir: str) -> Optional[str]:
    """
    Resolve a mangled symbol to its demangled signature.

    Uses tiered lookup:
    1. Database demangled column (fast, pre-computed)
    2. c++filt subprocess (full MSVC demangling)
    3. Fallback to Class::Method from parse_msvc_symbol

    Args:
        symbol: Mangled function symbol
        project_dir: Project directory containing decomp.db

    Returns:
        Demangled signature string, or None if resolution failed
    """
    info = resolve_callee_info(symbol, project_dir)
    return info["signature"] if info else None


def get_callee_signatures(
    symbol: str,
    objdiff_json: Optional[Dict[str, Any]] = None,
    project_dir: str = "",
) -> Dict[str, Any]:
    """
    Get signatures of functions called by this function (Experiment 6).

    Extracts callees from objdiff JSON and resolves their signatures
    to reduce agent lookup iterations during decompilation.

    Args:
        symbol: Function symbol (for logging)
        objdiff_json: Parsed objdiff JSON with --include-instructions
        project_dir: Project directory for database lookup

    Returns:
        Dict with:
        - count: Number of resolved signatures
        - signatures: List of {symbol, signature} dicts
        - summary: Human-readable summary
    """
    result = {"count": 0, "signatures": [], "summary": "(no callees found)"}

    if not objdiff_json:
        result["summary"] = "(objdiff data not available)"
        return result

    callees = extract_callees_from_objdiff(objdiff_json)
    if not callees:
        return result

    # Limit to avoid token bloat
    callees = callees[:MAX_CALLEE_SIGNATURES]
    signatures = []
    implemented_count = 0

    for callee_symbol in callees:
        info = resolve_callee_info(callee_symbol, project_dir)
        if info and info["signature"]:
            sig = info["signature"]
            # Truncate very long signatures
            if len(sig) > MAX_SIGNATURE_CHARS:
                sig = sig[:MAX_SIGNATURE_CHARS] + "..."

            entry = {
                "symbol": callee_symbol,
                "signature": sig,
            }

            # Include match percent and source location if available
            if info["match_percent"] is not None:
                entry["match_percent"] = info["match_percent"]
                if info["match_percent"] == 100.0:
                    implemented_count += 1

            if info["source_location"]:
                entry["source_location"] = info["source_location"]

            signatures.append(entry)

    result["count"] = len(signatures)
    result["signatures"] = signatures
    result["implemented_count"] = implemented_count
    if signatures:
        if implemented_count > 0:
            result["summary"] = f"Found {len(signatures)} callee signatures ({implemented_count} with source locations)"
        else:
            result["summary"] = f"Found {len(signatures)} callee signatures"

    return result


def format_callee_signatures(sig_data: Dict[str, Any]) -> str:
    """
    Format callee signatures for prompt injection.

    Args:
        sig_data: Result from get_callee_signatures()

    Returns:
        Formatted markdown string
    """
    if sig_data["count"] == 0:
        return sig_data["summary"]

    implemented_count = sig_data.get("implemented_count", 0)
    lines = [
        "## Called Function Signatures",
        "",
    ]

    if implemented_count > 0:
        lines.append(f"**Found {sig_data['count']} callees ({implemented_count} with 100% match and source location).**")
    else:
        lines.append(f"**Found {sig_data['count']} callees with resolved signatures.**")
    lines.append("")

    for item in sig_data["signatures"]:
        sig = item["signature"]
        source_loc = item.get("source_location")
        match_pct = item.get("match_percent")

        if source_loc:
            # 100% match with source location - most useful for agent
            lines.append(f"- `{sig}` — **100%** at `{source_loc}`")
        elif match_pct is not None:
            # Known match percentage but no source location
            lines.append(f"- `{sig}` — {match_pct:.0f}%")
        else:
            # Unknown/not in database
            lines.append(f"- `{sig}`")

    lines.extend([
        "",
        "**Tip**: Functions with source locations are fully implemented — read them for reference patterns.",
    ])

    return "\n".join(lines)


def run_objdiff_cli(
    symbol: str,
    project_dir: str,
    worktree_dir: str,
) -> Dict[str, Any]:
    """
    Run objdiff-cli and save output to worktree.

    Runs with --build --verdict and writes JSON output to a file in
    the worktree's function_analysis directory. Returns summary info
    and file path for the agent to reference.

    Args:
        symbol: Function symbol (mangled)
        project_dir: Main project directory (for finding objdiff-cli)
        worktree_dir: Worktree directory (for output file)

    Returns:
        Dict with:
        - success (bool)
        - match_percent (float or None)
        - verdict (str or None)
        - output_file (str, relative path)
        - output_file_absolute (str, absolute path)
        - line_count (int)
        - preview (str, first N lines if small)
        - error (str or None)
    """
    result = {
        "success": False,
        "match_percent": None,
        "verdict": None,
        "output_file": None,
        "output_file_absolute": None,
        "line_count": 0,
        "preview": None,
        "json": None,
        "error": None,
    }

    # Find objdiff-cli
    objdiff_cli = Path(project_dir) / "bin" / "objdiff-cli"
    if not objdiff_cli.exists():
        result["error"] = f"objdiff-cli not found at {objdiff_cli}"
        return result

    # Build command - run from main project dir (where objdiff.json lives)
    # Note: This builds in main repo, not worktree - for pre-computed context only
    # Include --include-instructions for m2c pipeline (objdiff_to_m2c.py)
    cmd = [
        str(objdiff_cli),
        "diff",
        "-p", project_dir,
        symbol,
        "--build",
        "--verdict",
        "--include-instructions",
        "-f", "json",
    ]

    try:
        proc_result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=300,  # 5 minute timeout
            cwd=project_dir,  # Run from main repo where objdiff.json exists
        )

        output = proc_result.stdout
        if proc_result.stderr:
            output += f"\n\n[stderr]\n{proc_result.stderr}"

        # Parse JSON for summary - find the JSON line (objdiff outputs build messages before JSON)
        try:
            json_line = None
            for line in proc_result.stdout.split("\n"):
                line = line.strip()
                if line.startswith("{") and line.endswith("}"):
                    json_line = line
                    break

            if json_line:
                data = json.loads(json_line)
                # Keep the PARSED document: the 4KB `preview` is a prefix of a
                # single-line 160KB JSON blob and cannot be parsed or pattern
                # matched by anything downstream.
                result["json"] = data
                result["match_percent"] = data.get("fuzzy_match_percent")
                verdict_data = data.get("verdict", {})
                if isinstance(verdict_data, dict):
                    result["verdict"] = verdict_data.get("classification", "UNKNOWN")
                result["success"] = True
            else:
                logger.warning("No JSON line found in objdiff output")
                result["success"] = proc_result.returncode == 0
                if not result["success"]:
                    # Capture stderr or first part of stdout as error message
                    err_msg = proc_result.stderr.strip() if proc_result.stderr else proc_result.stdout[:500]
                    result["error"] = f"Build failed: {err_msg}"
        except (json.JSONDecodeError, KeyError) as e:
            logger.warning(f"Could not parse objdiff JSON: {e}")
            # Still save the output even if we can't parse it
            result["success"] = proc_result.returncode == 0
            if not result["success"]:
                result["error"] = f"JSON parse error: {e}"

        # Save to file
        analysis_dir = Path(worktree_dir) / "function_analysis"
        analysis_dir.mkdir(exist_ok=True, parents=True)

        # Sanitize symbol for filename
        safe_symbol = symbol.replace("?", "_Q_").replace("@", "_A_").replace("<", "_L_").replace(">", "_R_")
        output_file = analysis_dir / f"objdiff_{safe_symbol}.json"

        with open(output_file, "w") as f:
            f.write(output)

        result["output_file"] = f"function_analysis/objdiff_{safe_symbol}.json"
        result["output_file_absolute"] = str(output_file)

        # Also save as baseline for diff_inspect compare workflow
        baseline_file = analysis_dir / f"baseline_{safe_symbol}.json"
        shutil.copy2(output_file, baseline_file)
        result["baseline_json_absolute"] = str(baseline_file)

        # Count lines and create preview respecting token budget
        lines = output.split("\n")
        result["line_count"] = len(lines)

        # Truncate preview based on both line count and character budget
        # This ensures we stay within the section token budget.
        # IMPORTANT: objdiff JSON with --include-instructions often produces
        # 1-3 very long lines (100KB+ each), so we MUST enforce a character
        # limit, not just a line limit.
        PREVIEW_CHAR_LIMIT = 4_000  # ~4KB preview is plenty for orientation
        if len(output) <= PREVIEW_CHAR_LIMIT:
            result["preview"] = output
        else:
            # Truncate by characters first, then by lines
            preview_text = output[:PREVIEW_CHAR_LIMIT]
            # Try to break at a line boundary
            last_newline = preview_text.rfind("\n")
            if last_newline > PREVIEW_CHAR_LIMIT // 2:
                preview_text = preview_text[:last_newline]
            remaining_chars = len(output) - len(preview_text)
            result["preview"] = (
                f"{preview_text}\n\n"
                f"... ({remaining_chars} more chars, {len(lines)} total lines)\n"
                f"Full output: {result['output_file']}\n"
                f"View with: cat {result['output_file']}"
            )

        logger.info(f"objdiff output saved: {output_file} ({len(lines)} lines)")

    except subprocess.TimeoutExpired:
        result["error"] = "objdiff timed out after 5 minutes"
    except Exception as e:
        result["error"] = str(e)

    return result


def get_last_attempt(symbol: str, project_dir: str) -> Tuple[str, int]:
    """
    Get formatted string of previous attempts for a function.

    Reads from database to find all previous attempts on this symbol
    and formats as: "Attempt N: model X, Y% → Z%"

    Args:
        symbol: Function symbol to look up
        project_dir: Project directory (for database location)

    Returns:
        Tuple of (formatted_string, attempt_count)
        - formatted_string: Human-readable summary
        - attempt_count: Number of attempts (0 if none)
    """
    if not get_function_by_symbol or not get_attempts_for_function:
        return "No previous attempts (database not available)", 0

    try:
        # Get database path from project
        db_path = Path(project_dir) / "decomp.db"
        if not db_path.exists():
            return "No previous attempts (first time seeing this function)", 0

        # Look up function by symbol
        func = get_function_by_symbol(symbol, db_path=str(db_path))
        if not func:
            return "No previous attempts (function not in database yet)", 0

        # Get last 5 attempts for more context
        attempts = get_attempts_for_function(func["id"], limit=5, db_path=str(db_path))
        if not attempts:
            return "No previous attempts (function tracked but never attempted)", 0

        # Format attempts (reverse to show oldest first)
        formatted = []
        for i, attempt in enumerate(reversed(attempts), 1):
            model = attempt.get("model", "unknown")
            # Use 'or 0' instead of default param - .get() returns None if key exists with None value
            start = attempt.get("start_percent") or 0
            end = attempt.get("end_percent") or 0
            status = attempt.get("exit_status", "unknown")
            notes = attempt.get("notes", "")

            line = f"Attempt {i}: {model}, {start:.1f}% → {end:.1f}% ({status})"
            if notes:
                # Truncate long notes
                short_notes = notes[:100] + "..." if len(notes) > 100 else notes
                line += f"\n  Notes: {short_notes}"
            formatted.append(line)

        return "\n".join(formatted), len(attempts)

    except Exception as e:
        logger.warning(f"Could not retrieve attempt history for {symbol}: {e}")
        return f"No previous attempts (error: {e})", 0


def get_binary_path(project_dir: str) -> Optional[str]:
    """
    Locate the DC3 binary for Ghidra.

    Args:
        project_dir: Project directory

    Returns:
        Path to binary or None if not found
    """
    candidates = [
        Path(project_dir) / "orig" / "373307D9" / "default.xex",
        Path(project_dir) / "DC3" / "default.xex",
        Path(project_dir) / "default.xex",
    ]

    for candidate in candidates:
        if candidate.exists():
            return str(candidate)

    return None


def collect_pre_run_context(
    symbol: str,
    unit: str,
    project_dir: str,
    worktree_dir: str,
    enrichment_overrides: Optional[Dict[str, bool]] = None,
    logger=None,
) -> Dict[str, Any]:
    """
    Collect all context that agent would waste turns computing.

    Runs objdiff with incremental build, queries Ghidra for decompilation
    and cross-references, and gathers previous attempt history. Writes
    cross-reference data to worktree for agent to access.

    Args:
        symbol: Function symbol (mangled, e.g., "?Load@CharMirror@@UAAXAAVBinStream@@@Z")
        unit: Unit name (e.g., "system/char/CharMirror") for incremental build
        project_dir: Project directory (e.g., /home/free/code/milohax/dc3-decomp)
        worktree_dir: Worktree directory for writing xrefs file
        enrichment_overrides: Optional dict to override A/B assignments (for testing)
        logger: Optional logger instance (uses module logger if not provided)

    Returns:
        Dict with keys:
        - match_percent (float)
        - verdict (string: AT_LIMIT, LIKELY_FIXABLE, MAYBE_FIXABLE, UNKNOWN)
        - key_patterns (list of strings)
        - suggestions (list)
        - previous_attempts (formatted string)
        - decompilation (optional, C code or "(unavailable)")
        - xrefs_path_absolute (string, full path to xrefs file)
        - xrefs_path_relative (string, relative to worktree)
        - xrefs_preview (string, first 20 lines of xrefs)
        - enrichment_flags (dict, A/B experiment assignments)
        - pattern_classification (optional, Experiment 1 output)
    """
    log = logger or logging.getLogger(__name__)
    log.info(f"Collecting context for {symbol} in {unit}")

    # Short-circuit merged symbols (ICF artifacts, not real decomp targets)
    if symbol.startswith("merged_"):
        log.info(f"Skipping merged symbol {symbol} (ICF artifact, not actionable)")
        return {
            "match_percent": 0.0,
            "verdict": "AT_LIMIT",
            "key_patterns": ["LINKER_MERGED"],
            "suggestions": ["Merged symbols are linker ICF artifacts. The real functions are "
                            "compiler-generated (template instantiations, deleting destructors) "
                            "and match automatically when the class is implemented correctly."],
            "previous_attempts": "No previous attempts",
            "previous_attempts_count": 0,
            "decompilation": "(unavailable - merged symbol)",
            "source_file_absolute": "(not applicable)",
            "enrichment_flags": {},
        }

    # Get A/B experiment assignments
    enrichment_flags = get_enrichment_assignments(symbol)
    if enrichment_overrides:
        enrichment_flags.update(enrichment_overrides)
    log.info(f"Enrichment assignments: {enrichment_flags}")

    # Compute source file absolute path from unit name - use worktree_dir so agent edits the right file
    # Unit format is "default/system/char/Char" or "system/char/Char"
    # We need to convert to "{worktree_dir}/src/system/char/Char.cpp"
    unit_path = unit.replace("default/", "") if unit.startswith("default/") else unit
    # Add src/ prefix if not already present (database stores paths without src/)
    if not unit_path.startswith("src/"):
        unit_path = f"src/{unit_path}"
    source_file_candidates = [
        Path(worktree_dir) / f"{unit_path}.cpp",
        Path(worktree_dir) / f"{unit_path}.h",
        Path(worktree_dir) / f"{unit_path}.c",
    ]
    source_file_absolute = None
    for candidate in source_file_candidates:
        if candidate.exists():
            source_file_absolute = str(candidate.resolve())
            break
    if not source_file_absolute:
        # If no file found, use the most likely .cpp
        source_file_absolute = str((Path(worktree_dir) / f"{unit_path}.cpp").resolve())

    # Compute header file path (try .h with same base as the source file)
    header_file_absolute = "(no header)"
    header_contents = "(no header found)"
    header_line_count = 0
    source_file_path = Path(source_file_absolute)
    if source_file_path.suffix == '.cpp':
        header_candidate = source_file_path.with_suffix('.h')
        if header_candidate.exists():
            header_file_absolute = str(header_candidate.resolve())
            try:
                raw_header = header_candidate.read_text()
                header_line_count = raw_header.count('\n') + 1
                header_offload = truncate_and_offload(
                    content=raw_header,
                    name=f"header_{header_candidate.stem}",
                    worktree_dir=worktree_dir,
                )
                header_contents = header_offload["inline"]
                log.info(f"Header loaded: {header_file_absolute} ({header_line_count} lines, truncated={header_offload['was_truncated']})")
            except Exception as e:
                log.warning(f"Failed to read header {header_candidate}: {e}")
                header_contents = f"(error reading header: {e})"
        else:
            log.debug(f"No header file found at {header_candidate}")

    # Extract source window around target function
    source_contents = "(not read)"
    source_window_start_line = 0
    source_window_end_line = 0
    source_total_lines = 0
    class_name_for_window, method_name_for_window = parse_msvc_symbol(symbol)
    if Path(source_file_absolute).exists():
        try:
            raw_source = Path(source_file_absolute).read_text()
            source_total_lines = raw_source.count('\n') + 1
            if class_name_for_window and method_name_for_window:
                window_text, start_line, end_line, total = _extract_source_window(
                    raw_source, class_name_for_window, method_name_for_window
                )
                source_window_start_line = start_line
                source_window_end_line = end_line
                source_total_lines = total
                # Apply truncate_and_offload to the window (or full file if extraction failed)
                source_offload = truncate_and_offload(
                    content=window_text,
                    name=f"source_window_{symbol}",
                    worktree_dir=worktree_dir,
                )
                source_contents = source_offload["inline"]
                if start_line == 1 and end_line == total:
                    log.info(f"Source window: full file ({total} lines, function not found by pattern)")
                else:
                    log.info(f"Source window: lines {start_line}-{end_line} of {total}")
            else:
                # Can't parse symbol — fall back to full file with truncation
                source_offload = truncate_and_offload(
                    content=raw_source,
                    name=f"source_full_{symbol}",
                    worktree_dir=worktree_dir,
                )
                source_contents = source_offload["inline"]
                source_window_start_line = 1
                source_window_end_line = source_total_lines
                log.info(f"Source window: full file ({source_total_lines} lines, could not parse symbol)")
        except Exception as e:
            log.warning(f"Failed to read source file {source_file_absolute}: {e}")
            source_contents = f"(error reading source: {e})"

    # Initialize result dict with defaults
    result = {
        "match_percent": 0.0,
        "verdict": "UNKNOWN",
        "key_patterns": [],
        "suggestions": [],
        "previous_attempts": "No previous attempts",
        "previous_attempts_count": 0,
        "decompilation": "(unavailable)",
        "ghidra_file_path_relative": "(not written)",
        "rb3_reference": "(not searched)",
        "rb3_file_path_relative": "(not found)",
        "m2c_decompilation": "(not run yet)",
        "m2c_file_path": "(not written)",
        "m2c_file_path_relative": "(not written)",
        "m2c_line_count": 0,
        "m2c_method": "none",
        "xrefs_path_absolute": "(unavailable)",
        "xrefs_path_relative": "(unavailable)",
        "xrefs_preview": "(unavailable)",
        # Source file absolute path
        "source_file_absolute": source_file_absolute,
        # Header file
        "header_file_absolute": header_file_absolute,
        "header_contents": header_contents,
        "header_line_count": header_line_count,
        # Source window
        "source_contents": source_contents,
        "source_window_start_line": source_window_start_line,
        "source_window_end_line": source_window_end_line,
        "source_total_lines": source_total_lines,
        # Pre-computed objdiff output file
        "objdiff_file": "(unavailable)",
        "objdiff_file_absolute": "(unavailable)",
        "objdiff_line_count": 0,
        "objdiff_preview": "(unavailable)",
        # A/B experiment tracking
        "enrichment_flags": enrichment_flags,
        # Experiment 1: Diff pattern classification (if enabled)
        "pattern_classification": None,
        "pattern_classification_summary": "(not enabled)",
        # Experiment 2: Function type templates (if enabled)
        "function_type": None,
        "function_type_guidance": "(not enabled)",
        # Experiment 3: RB2 class layouts (if enabled)
        "class_layout": None,
        "class_layout_summary": "(not enabled)",
        # Experiment 4: Previous attempt diffs (if enabled)
        "attempt_diffs": None,
        "attempt_diffs_summary": "(not enabled)",
        # Experiment 5: Matched siblings (if enabled)
        "matched_siblings": None,
        "matched_siblings_summary": "(not enabled)",
        # Experiment 6: Callee signatures (if enabled) - stub
        "callee_signatures": None,
        "callee_signatures_summary": "(not enabled)",
    }

    # Parse symbol to get class name (used by multiple experiments)
    class_name, method_name = parse_msvc_symbol(symbol)

    # Experiment 2: Function Type Templates (run early, doesn't depend on objdiff)
    if enrichment_flags.get("function_types"):
        log.info("Classifying function type (Experiment 2)...")
        try:
            func_type = classify_function_type(symbol)
            result["function_type"] = func_type
            result["function_type_guidance"] = format_function_type_guidance(func_type)
            if func_type["type"] != "GENERIC":
                log.info(f"Function type: {func_type['type']} - {func_type['description']}")
            else:
                log.debug("Function type: GENERIC (no specific guidance)")
        except Exception as e:
            log.warning(f"Function type classification failed: {e}")
    else:
        log.debug("Function type templates disabled (control group)")

    # Experiment 3: RB2 Class Layouts
    if enrichment_flags.get("rb2_layouts") and class_name:
        log.info(f"Looking up RB2 class layout for {class_name} (Experiment 3)...")
        try:
            layout = get_class_layout(class_name)
            result["class_layout"] = layout
            if layout["found"]:
                result["class_layout_summary"] = format_class_layout(layout)
                log.info(f"Found RB2 layout: {len(layout['members'])} members, size 0x{layout['total_size']:X}")
            else:
                result["class_layout_summary"] = f"(class {class_name} not found in RB2 DWARF)"
                log.debug(f"RB2 layout: {layout.get('error', 'not found')}")
        except Exception as e:
            log.warning(f"RB2 layout lookup failed: {e}")
            result["class_layout_summary"] = f"(error: {e})"
    else:
        if not enrichment_flags.get("rb2_layouts"):
            log.debug("RB2 class layouts disabled (control group)")
        elif not class_name:
            log.debug("RB2 class layouts: could not parse class name from symbol")

    # 1. Run objdiff with incremental build
    log.info("Running objdiff with incremental build...")
    if run_objdiff:
        try:
            objdiff_result = run_objdiff(
                symbol,
                project_dir=project_dir,
                unit=unit,
                incremental=True,  # Critical for <20s target
            )

            if objdiff_result and not objdiff_result.error:
                result["match_percent"] = objdiff_result.fuzzy_match_percent
                result["key_patterns"] = extract_key_patterns(objdiff_result)
                result["suggestions"] = objdiff_result.suggestions or []

                # Extract verdict classification
                if objdiff_result.verdict:
                    verdict = objdiff_result.verdict.get("classification", "UNKNOWN")
                    result["verdict"] = verdict

                log.info(
                    f"objdiff result: {result['match_percent']:.2f}% match, "
                    f"verdict={result['verdict']}"
                )
            else:
                log.warning(f"objdiff error: {objdiff_result.error if objdiff_result else 'No result'}")
        except Exception as e:
            log.warning(f"objdiff failed: {e}")
    else:
        log.warning("run_objdiff not available")

    # 1b. Also run objdiff-cli to save full JSON output to file
    log.info("Saving full objdiff output to file...")
    try:
        cli_result = run_objdiff_cli(symbol, project_dir, worktree_dir)
        if cli_result["success"]:
            result["objdiff_file"] = cli_result["output_file"]
            result["objdiff_file_absolute"] = cli_result["output_file_absolute"]
            result["objdiff_line_count"] = cli_result["line_count"]
            result["objdiff_preview"] = cli_result["preview"]
            # Use CLI result if Python run_objdiff wasn't available
            if result["match_percent"] == 0.0 and cli_result["match_percent"]:
                result["match_percent"] = cli_result["match_percent"]
            if result["verdict"] == "UNKNOWN" and cli_result["verdict"]:
                result["verdict"] = cli_result["verdict"]
            log.info(f"objdiff saved to {cli_result['output_file']} ({cli_result['line_count']} lines)")

            # Experiment 1: Diff Pattern Classification
            if enrichment_flags.get("diff_patterns"):
                log.info("Running diff pattern classification (Experiment 1)...")
                try:
                    classification = classify_diff_patterns(
                        cli_result.get("preview", ""),
                        objdiff_result=None,  # Could pass objdiff_result if available
                        objdiff_json=cli_result.get("json"),
                    )
                    result["pattern_classification"] = classification
                    result["pattern_classification_summary"] = format_pattern_classification(classification)
                    log.info(f"Pattern classification: {classification['fixability']}")
                except Exception as e:
                    log.warning(f"Pattern classification failed: {e}")
                    result["pattern_classification_summary"] = f"(error: {e})"
            else:
                log.debug("Diff pattern classification disabled (control group)")
        else:
            log.warning(f"objdiff-cli failed: {cli_result['error']}")
    except Exception as e:
        log.warning(f"objdiff-cli failed: {e}")

    # 2. Get previous attempts
    log.info("Retrieving previous attempts...")
    attempts_str, attempts_count = get_last_attempt(symbol, project_dir)
    result["previous_attempts"] = attempts_str
    result["previous_attempts_count"] = attempts_count
    log.info(f"Found {attempts_count} previous attempts")

    # Experiment 4: Previous Attempt Diffs
    if enrichment_flags.get("attempt_diffs") and attempts_count > 0:
        log.info("Retrieving previous attempt diffs (Experiment 4)...")
        try:
            attempt_diffs = get_previous_attempt_diffs(symbol, project_dir)
            result["attempt_diffs"] = attempt_diffs
            if attempt_diffs["count"] > 0:
                result["attempt_diffs_summary"] = format_previous_attempt_diffs(attempt_diffs)
                log.info(f"Found {attempt_diffs['count']} attempts with diffs")
            else:
                result["attempt_diffs_summary"] = "(no attempts with patches found)"
        except Exception as e:
            log.warning(f"Failed to get attempt diffs: {e}")
            result["attempt_diffs_summary"] = f"(error: {e})"
    else:
        if not enrichment_flags.get("attempt_diffs"):
            log.debug("Previous attempt diffs disabled (control group)")
        elif attempts_count == 0:
            log.debug("Previous attempt diffs: no previous attempts")

    # Experiment 5: Matched Siblings
    if enrichment_flags.get("matched_siblings") and class_name:
        log.info(f"Looking for matched siblings in class {class_name} (Experiment 5)...")
        try:
            siblings = get_matched_siblings(class_name, symbol, project_dir)
            result["matched_siblings"] = siblings
            if siblings["count"] > 0:
                result["matched_siblings_summary"] = format_matched_siblings(siblings)
                log.info(f"Found {siblings['count']} matched siblings")
            else:
                result["matched_siblings_summary"] = f"(no 100% matched siblings for {class_name})"
        except Exception as e:
            log.warning(f"Failed to get matched siblings: {e}")
            result["matched_siblings_summary"] = f"(error: {e})"
    else:
        if not enrichment_flags.get("matched_siblings"):
            log.debug("Matched siblings disabled (control group)")
        elif not class_name:
            log.debug("Matched siblings: could not determine class name")

    # Experiment 6: Callee Signatures
    if enrichment_flags.get("callee_signatures"):
        log.info("Getting callee signatures (Experiment 6)...")
        try:
            # Load objdiff JSON from the file we wrote earlier
            objdiff_json = None
            objdiff_file = result.get("objdiff_file_absolute")
            if objdiff_file:
                objdiff_path = Path(objdiff_file)
                if objdiff_path.exists():
                    content = objdiff_path.read_text()
                    # objdiff outputs build messages before JSON, find the JSON line
                    for line in content.split("\n"):
                        line = line.strip()
                        if line.startswith("{"):
                            try:
                                objdiff_json = json.loads(line)
                                break
                            except json.JSONDecodeError:
                                continue

            sig_data = get_callee_signatures(symbol, objdiff_json, project_dir)
            result["callee_signatures"] = sig_data
            result["callee_signatures_summary"] = format_callee_signatures(sig_data)
            log.info(f"Callee signatures: {sig_data['summary']}")
        except Exception as e:
            log.warning(f"Failed to get callee signatures: {e}")
            result["callee_signatures_summary"] = f"(error: {e})"
    else:
        log.debug("Callee signatures disabled (control group)")

    # 3. Get RB3 reference implementation
    log.info("Looking up RB3 reference...")
    try:
        rb3_ref = find_rb3_reference(symbol, unit)
        if not rb3_ref.startswith("("):
            # Always write to file
            rb3_offload = truncate_and_offload(
                content=rb3_ref,
                name=f"rb3_{symbol}",
                worktree_dir=worktree_dir,
            )
            if not rb3_offload["was_truncated"]:
                # truncate_and_offload didn't write a file — do it ourselves
                analysis_dir = Path(worktree_dir) / "function_analysis"
                analysis_dir.mkdir(exist_ok=True, parents=True)
                safe_name = f"rb3_{symbol}".replace("?", "_Q_").replace("@", "_A_").replace("<", "_L_").replace(">", "_R_").replace("/", "_")
                rb3_file = analysis_dir / f"{safe_name}.cpp"
                rb3_file.write_text(rb3_ref)
                rb3_offload["file_path_relative"] = f"function_analysis/{safe_name}.cpp"

            result["rb3_file_path_relative"] = rb3_offload["file_path_relative"]
            match_pct = result.get("match_percent", 0.0)
            if match_pct >= 90.0:
                result["rb3_reference"] = (
                    f"(file-only at {match_pct:.1f}% match)\n"
                    f"View with: cat {rb3_offload['file_path_relative']}"
                )
                log.info(f"RB3 reference file-only (match={match_pct:.1f}%): {rb3_offload['file_path_relative']}")
            else:
                result["rb3_reference"] = rb3_offload["inline"]
                log.info(f"Found RB3 reference: {len(rb3_ref)} chars, file: {rb3_offload['file_path_relative']}")
        else:
            result["rb3_reference"] = rb3_ref
            result["rb3_file_path_relative"] = "(not found)"
            log.info(f"RB3 reference: {rb3_ref}")
    except Exception as e:
        log.warning(f"RB3 lookup failed: {e}")
        result["rb3_reference"] = f"(error: {e})"
        result["rb3_file_path_relative"] = "(error)"

    # 4. Run m2c decompilation (always writes to file, only inline for low-match functions)
    # Pass objdiff JSON path to enable new pipeline with better relocation handling
    log.info("Running m2c decompilation...")
    try:
        m2c_result = run_m2c_decompile(
            symbol=symbol,
            unit=unit,
            worktree_dir=worktree_dir,
            objdiff_json_path=result.get("objdiff_file_absolute"),
            use_type_context=True,
        )
        result["m2c_file_path"] = m2c_result["file_path"]
        result["m2c_file_path_relative"] = m2c_result["file_path_relative"]
        result["m2c_line_count"] = m2c_result["line_count"]
        result["m2c_method"] = m2c_result.get("method", "none")

        # For high-match functions (>=90%), don't inline m2c - just point to the file.
        # The agent can read it if needed, but it's noise in the initial prompt at this stage.
        match_pct = result.get("match_percent", 0.0)
        if match_pct >= 90.0 and m2c_result["success"]:
            result["m2c_decompilation"] = (
                f"(file-only - function is at {match_pct:.1f}% match, m2c output saved to file)\n"
                f"View with: cat {m2c_result['file_path_relative']}"
            )
            log.info(f"m2c written to file only (match={match_pct:.1f}% >= 90%): {m2c_result['file_path_relative']}")
        else:
            result["m2c_decompilation"] = m2c_result["inline"]
            if m2c_result["success"]:
                method = m2c_result.get("method", "unknown")
                log.info(f"m2c decompilation ({method}): {m2c_result['line_count']} lines, written to {m2c_result['file_path_relative']}")
            else:
                log.info(f"m2c decompilation: {m2c_result['inline']}")
    except Exception as e:
        log.warning(f"m2c decompilation failed: {e}")
        result["m2c_decompilation"] = f"(error: {e})"
        result["m2c_file_path"] = "(not written)"
        result["m2c_file_path_relative"] = "(not written)"
        result["m2c_line_count"] = 0
        result["m2c_method"] = "none"

    # 5. Collect from Ghidra cache (or fall back to live Ghidra)
    log.info("Attempting Ghidra decompilation and xrefs...")
    try:
        from scripts.orchestrator.database import get_connection as _get_conn
        from scripts.orchestrator.database import get_decompilation, get_xrefs
        from scripts.orchestrator.database import put_decompilation, put_xrefs

        db_path = Path(project_dir) / "decomp.db"
        cache_conn = _get_conn(str(db_path)) if db_path.exists() else None

        # --- Decompilation: cache-first ---
        decompilation = None
        if cache_conn:
            decompilation = get_decompilation(cache_conn, symbol)
            if decompilation:
                log.info(f"Cache hit: decompilation for {symbol} ({len(decompilation)} chars)")

        if decompilation is None:
            # Cache miss — fall back to live Ghidra (prefer HTTP MCP client)
            log.info(f"Cache miss for decompilation: {symbol}, trying live Ghidra")
            # Try HTTP MCP client first (fast, no JVM startup)
            if GhidraMCPClient is not None:
                try:
                    mcp_client = GhidraMCPClient()
                    mcp_client.initialize()
                    result_data = mcp_client.decompile_function(symbol)
                    if isinstance(result_data, dict) and "code" in result_data:
                        decompilation = result_data["code"]
                    elif isinstance(result_data, str):
                        decompilation = result_data
                    if decompilation:
                        log.info(f"Ghidra HTTP MCP decompilation: {len(decompilation)} chars")
                except GhidraMCPError as e:
                    log.warning(f"Ghidra HTTP decompilation failed, skipping: {e}")
                except Exception as e:
                    log.warning(f"Ghidra HTTP unexpected error, skipping: {e}")

            # Fall back to JVM client (only when GHIDRA_USE_JVM=1)
            if decompilation is None:
                decompilation = _jvm_ghidra_decompile(symbol, project_dir)
                if decompilation:
                    log.info(f"Ghidra JVM decompilation: {len(decompilation)} chars")

            # Write back to cache so future requests are instant
            if decompilation and cache_conn:
                try:
                    put_decompilation(cache_conn, symbol, address=None, code=decompilation)
                    cache_conn.commit()
                    log.info(f"Cached decompilation for {symbol}")
                except Exception as e:
                    log.debug(f"Failed to cache decompilation: {e}")

        if decompilation:
            # Write to file + inline (same logic as before)
            ghidra_offload = truncate_and_offload(
                content=decompilation,
                name=f"ghidra_{symbol}",
                worktree_dir=worktree_dir,
            )
            if not ghidra_offload["was_truncated"]:
                analysis_dir = Path(worktree_dir) / "function_analysis"
                analysis_dir.mkdir(exist_ok=True, parents=True)
                safe_name = f"ghidra_{symbol}".replace("?", "_Q_").replace("@", "_A_").replace("<", "_L_").replace(">", "_R_").replace("/", "_")
                ghidra_file = analysis_dir / f"{safe_name}.c"
                ghidra_file.write_text(decompilation)
                ghidra_offload["file_path_relative"] = f"function_analysis/{safe_name}.c"

            result["ghidra_file_path_relative"] = ghidra_offload["file_path_relative"]
            match_pct = result.get("match_percent", 0.0)
            if match_pct >= 90.0:
                result["decompilation"] = (
                    f"(file-only at {match_pct:.1f}% match)\n"
                    f"View with: cat {ghidra_offload['file_path_relative']}"
                )
                log.info(f"Ghidra decompilation file-only (match={match_pct:.1f}%): {ghidra_offload['file_path_relative']}")
            else:
                result["decompilation"] = ghidra_offload["inline"]
                log.info(f"Ghidra decompilation: {len(decompilation)} chars, file: {ghidra_offload['file_path_relative']}")

        # --- Cross-references: cache-first ---
        callers = None
        callees = None
        if cache_conn:
            cached_xrefs = get_xrefs(cache_conn, symbol)
            if cached_xrefs:
                callers, callees = cached_xrefs
                log.info(f"Cache hit: {len(callers)} callers, {len(callees)} callees")

        if callers is None:
            # Cache miss — fall back to live Ghidra (prefer HTTP MCP client)
            log.info(f"Cache miss for xrefs: {symbol}, trying live Ghidra")
            # Try HTTP MCP client first
            if GhidraMCPClient is not None:
                try:
                    mcp_client = GhidraMCPClient()
                    mcp_client.initialize()
                    xref_data = mcp_client.list_xrefs(symbol)
                    if isinstance(xref_data, dict):
                        # Extract caller/callee names from cross_references list
                        xrefs_list = xref_data.get("cross_references", [])
                        callers = []
                        callees = []
                        for xref in xrefs_list:
                            name = xref.get("function_name") if isinstance(xref, dict) else None
                            if name:
                                callers.append(name)
                        log.info(f"Ghidra HTTP MCP xrefs: {len(callers)} callers")
                except GhidraMCPError as e:
                    log.warning(f"Ghidra HTTP xrefs failed, skipping: {e}")
                except Exception as e:
                    log.warning(f"Ghidra HTTP xrefs unexpected error, skipping: {e}")

            # Fall back to JVM client (only when GHIDRA_USE_JVM=1)
            if callers is None:
                jvm_result = _jvm_ghidra_xrefs(symbol, project_dir)
                if jvm_result is not None:
                    callers, callees = jvm_result
                    log.info(f"Ghidra JVM xrefs: {len(callers)} callers, {len(callees)} callees")

            # Write back to cache
            if callers is not None and cache_conn:
                try:
                    put_xrefs(cache_conn, symbol, address=None, callers=callers, callees=callees or [])
                    cache_conn.commit()
                    log.info(f"Cached xrefs for {symbol}")
                except Exception as e:
                    log.debug(f"Failed to cache xrefs: {e}")

        if callers is not None:
            log.info(f"Found {len(callers)} callers, {len(callees)} callees")

            # Write xrefs to worktree
            analysis_dir = Path(worktree_dir) / "function_analysis"
            analysis_dir.mkdir(exist_ok=True, parents=True)

            xrefs_file = analysis_dir / f"xrefs_{symbol}.txt"
            with open(xrefs_file, 'w') as f:
                f.write(f"Cross-references for {symbol}\n")
                f.write(f"{'='*80}\n\n")
                f.write(f"Callers ({len(callers)} total):\n")
                for caller in callers:
                    f.write(f"  - {caller}\n")
                f.write(f"\nCallees ({len(callees)} total):\n")
                for callee in callees:
                    f.write(f"  - {callee}\n")

            result["xrefs_path_absolute"] = str(xrefs_file)
            result["xrefs_path_relative"] = f"function_analysis/xrefs_{symbol}.txt"

            with open(xrefs_file, 'r') as f:
                lines = f.readlines()[:20]
            result["xrefs_preview"] = ''.join(lines)

            log.info(f"Xrefs written to {xrefs_file}")

    except Exception as e:
        log.warning(f"Ghidra/cache error (continuing without): {e}")

    log.info(f"Context collection complete. Verdict: {result['verdict']}")
    return result


def main():
    """CLI for testing context_collector."""
    import argparse

    parser = argparse.ArgumentParser(
        description="Collect pre-run context for a function",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --symbol "?Load@CharMirror@@UAAXAAVBinStream@@@Z" \\
           --unit system/char/CharMirror \\
           --project-dir /home/free/code/milohax/dc3-decomp \\
           --worktree-dir /tmp/test-xrefs
        """
    )

    parser.add_argument(
        "--symbol",
        required=True,
        help="Function symbol (mangled)"
    )
    parser.add_argument(
        "--unit",
        required=True,
        help="Unit name for incremental build"
    )
    parser.add_argument(
        "--project-dir",
        required=True,
        help="Project directory"
    )
    parser.add_argument(
        "--worktree-dir",
        required=True,
        help="Worktree directory for xrefs output"
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Enable verbose logging"
    )

    args = parser.parse_args()

    # Setup logging
    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)
    else:
        logging.basicConfig(level=logging.INFO)

    try:
        context = collect_pre_run_context(
            symbol=args.symbol,
            unit=args.unit,
            project_dir=args.project_dir,
            worktree_dir=args.worktree_dir,
        )

        # Pretty print result
        print(json.dumps(context, indent=2))

        # Verify xrefs file was created
        if context.get("xrefs_path_absolute") != "(unavailable)":
            xrefs_path = Path(context["xrefs_path_absolute"])
            if xrefs_path.exists():
                print(f"\n✓ Xrefs file created: {xrefs_path}")
                print(f"  Relative path: {context['xrefs_path_relative']}")
            else:
                print(f"\n✗ Xrefs file NOT found: {xrefs_path}")
                sys.exit(1)

    except Exception as e:
        logger.error(f"Context collection failed: {e}")
        import traceback
        if args.verbose:
            traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
