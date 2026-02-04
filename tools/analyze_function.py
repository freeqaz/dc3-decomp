#!/usr/bin/env python3
"""
analyze-function: Combined objdiff + Ghidra MCP analysis for decompilation work.

Provides a unified view of:
- objdiff diff analysis (match %, verdict, patterns)
- Ghidra decompilation (pseudo-C of original)
- Cross-references (callers/callees)

Designed for agent-driven decompilation workflows.
"""

import argparse
import json
import os
import subprocess
import sys
import time
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple

import requests

# =============================================================================
# Configuration
# =============================================================================

# Note: New pyghidra-mcp uses FastMCP/Uvicorn on port 8000 (changed from old port 8765)
MCP_URL = "http://127.0.0.1:8000/mcp"
# Binary name is dynamically resolved - pyghidra-mcp uses SHA1 hash suffix
# e.g., "default.xex-997567" instead of just "default.xex"
BINARY_NAME = "default.xex-997567"


# =============================================================================
# Verbose Logging and Health Checks
# =============================================================================

def vprint(msg: str, verbose: bool, prefix: str = "resolve"):
    """Print verbose debug message to stderr."""
    if verbose:
        print(f"[{prefix}] {msg}", file=sys.stderr)


def check_service_health(timeout: int = 5) -> bool:
    """Check if Ghidra service is healthy.

    Args:
        timeout: Timeout in seconds for the health check

    Returns:
        True if service is healthy, False otherwise
    """
    try:
        # Try to call the health check tool
        response = requests.post(
            MCP_URL,
            json={
                "jsonrpc": "2.0",
                "id": 0,
                "method": "tools/call",
                "params": {
                    "name": "get_service_health",
                    "arguments": {}
                }
            },
            headers={
                "Content-Type": "application/json",
                "Accept": "application/json, text/event-stream",
            },
            timeout=timeout,
        )

        if response.status_code == 200:
            return True
    except requests.exceptions.ConnectionError:
        return False
    except (requests.exceptions.Timeout, requests.exceptions.RequestException):
        return False

    return False


SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
PROJECT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
OBJDIFF_CLI = os.path.join(PROJECT_DIR, "bin", "objdiff-cli")
OBJDIFF_TO_M2C = os.path.join(SCRIPT_DIR, "objdiff_to_m2c.py")
M2C_PATH = os.path.expanduser("~/code/milohax/m2c/m2c.py")

# =============================================================================
# Pattern Documentation Mapping
# =============================================================================

# Pattern name → (doc file, anchor, quick tip)
# These link detected patterns to actionable documentation.
#
# objdiff-cli detects 7 patterns (see objdiff-cli/src/cmd/analysis.rs):
#   LINKER_MERGED, BOOL_MASK, REGISTER_SWAP, COMPARISON_STYLE,
#   CONTROL_FLOW, COMMUTATIVE_OP_ORDER, OFFSET_SWAP
#
# The target binary is a DEBUG BUILD (no LTCG). This means:
#   - ICF (Identical COMDAT Folding) is still enabled → LINKER_MERGED applies
#   - No link-time code generation → LTCG patterns should NOT occur
#   - Matching should be achievable for most functions
#
PATTERN_DOCS = {
    # =========================================================================
    # ACTIVE PATTERNS - detected by objdiff-cli (analysis.rs)
    # =========================================================================

    # Verifiable patterns - linker ICF
    "LINKER_MERGED": (
        "verifiable-icf.md",
        "linker-merged-icf",
        "Verify with merged-symbols lookup, then accept as at_limit if correct"
    ),

    # Unfixable/rarely fixable patterns - compiler
    "BOOL_MASK": (
        "fixable-bool-mask.md",
        "",
        "Often fixable: try local bool variable, (bool) cast, or find missing inline — see pattern doc"
    ),
    "REGISTER_SWAP": (
        "unfixable-compiler.md",
        "register-allocation",
        "Try variable declaration reorder (~30% success)"
    ),

    # Fixable patterns - comparison
    "COMPARISON_STYLE": (
        "fixable-comparison.md",
        "comparison-style",
        "Try `> 0` vs `>= 1`, signed vs unsigned casts (~60% success)"
    ),

    # Fixable patterns - control flow
    "CONTROL_FLOW": (
        "fixable-control-flow.md",
        "",
        "Try ternary vs if-else, loop restructuring (~70% success)"
    ),

    # Fixable patterns - operand order
    "COMMUTATIVE_OP_ORDER": (
        "fixable-operators.md",
        "commutative-operand-order",
        "Swap operand order in add/fadd/mul/and/or/xor (~80% success)"
    ),

    # Fixable patterns - struct field order
    "OFFSET_SWAP": (
        "fixable-declarations.md",
        "offset-swap",
        "Check field access order or struct layout (~60% success)"
    ),

    # =========================================================================
    # DISABLED PATTERNS - not currently detected by objdiff-cli
    # =========================================================================
    # These patterns exist in documentation but objdiff-cli doesn't detect them.
    # If objdiff-cli is extended to detect these, uncomment the relevant entries.
    #
    # Additionally, some of these (LTCG_POOLING, FLOAT_POOLING) assume a retail
    # build with Link-Time Code Generation. Our target is a DEBUG BUILD without
    # LTCG, so these patterns likely don't apply. Verify before enabling.
    #
    # --- LTCG-related (likely N/A for debug builds) ---
    # "LTCG_POOLING": (
    #     "verifiable-icf.md",
    #     "ltcgglobal-pooling",
    #     "Accept 0.5-1% gap - link-time optimization"
    # ),
    # "FLOAT_POOLING": (
    #     "verifiable-icf.md",
    #     "float-constant-pooling",
    #     "Accept 1-2 instruction gap - linker float placement"
    # ),
    #
    # --- Compiler patterns (may be added to objdiff-cli later) ---
    # "ASSERT_REVS": (
    #     "unfixable-compiler.md",
    #     "assert_revs-scheduling",
    #     "Accept ~0.8-0.9% gap - instruction scheduling"
    # ),
    # "FMADDS": (
    #     "unfixable-compiler.md",
    #     "fmadds-vs-separate-ops",
    #     "Accept 1-3% gap - compiler optimization choice"
    # ),
    # "COMMUTATIVE_SWAP": (
    #     "unfixable-compiler.md",
    #     "commutative-register-swap",
    #     "Accept as functionally identical"
    # ),
    #
    # --- Comparison patterns (subsumed by COMPARISON_STYLE) ---
    # "UNSIGNED_ZERO": (
    #     "fixable-comparison.md",
    #     "unsigned-zero-comparison",
    #     "Use `> 0` instead of `!= 0` for unsigned (~95% success)"
    # ),
    # "SIGNED_UNSIGNED": (
    #     "fixable-comparison.md",
    #     "signedunsigned-cast",
    #     "Cast to force cmpwi vs cmplwi instruction"
    # ),
    #
    # --- Control flow patterns (subsumed by CONTROL_FLOW) ---
    # "TERNARY": (
    #     "fixable-control-flow.md",
    #     "ternary-vs-if-else",
    #     "Try ternary for simple conditionals (~75% success)"
    # ),
    # "LOOP_STRUCTURE": (
    #     "fixable-control-flow.md",
    #     "loop-structure",
    #     "Try different loop forms (for, while, external init)"
    # ),
}

# Base path for pattern documentation (relative to project root)
PATTERN_DOCS_PATH = "docs/decomp/patterns"


def get_pattern_doc_info(pattern_name: str) -> Optional[Tuple[str, str, str]]:
    """
    Get documentation info for a detected pattern.

    Args:
        pattern_name: Pattern name from objdiff verdict (e.g., "LINKER_MERGED")

    Returns:
        Tuple of (doc_path, anchor, quick_tip) or None if pattern not documented
    """
    info = PATTERN_DOCS.get(pattern_name.upper())
    if info:
        doc_file, anchor, tip = info
        doc_path = f"{PATTERN_DOCS_PATH}/{doc_file}"
        if anchor:
            doc_path += f"#{anchor}"
        return (doc_path, anchor, tip)
    return None


# =============================================================================
# Data Classes
# =============================================================================

@dataclass
class ObjdiffResult:
    """Results from objdiff diff command."""
    symbol: str = ""
    demangled: str = ""
    fuzzy_match_percent: float = 0.0
    target_size: int = 0
    base_size: int = 0
    verdict: Optional[Dict[str, Any]] = None
    analysis: Optional[Dict[str, Any]] = None
    instruction_summary: Optional[Dict[str, Any]] = None
    suggestions: List[str] = field(default_factory=list)
    raw: Dict[str, Any] = field(default_factory=dict)
    error: Optional[str] = None


@dataclass
class GhidraResult:
    """Results from Ghidra MCP queries."""
    decompilation: str = ""
    function_name: str = ""
    address: str = ""
    callers: List[str] = field(default_factory=list)
    callees: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)
    error: Optional[str] = None
    expected_address: Optional[str] = None  # From symbols.txt
    resolved_address: Optional[str] = None  # From Ghidra
    address_warning: Optional[str] = None   # If mismatch


@dataclass
class M2CResult:
    """Results from m2c decompilation."""
    decompilation: str = ""
    error: Optional[str] = None

@dataclass
class OffsetMismatch:
    """Information about a struct offset mismatch in instructions."""
    index: int  # Instruction index
    opcode: str  # Instruction opcode (lwz, stw, etc.)
    target_offset: int  # Offset in target binary
    base_offset: int  # Offset in base (compiled) binary
    base_register: str  # Register used as base (r3, r31, etc.)
    target_field: Optional[str] = None  # Resolved field name for target offset
    base_field: Optional[str] = None  # Resolved field name for base offset
    target_class: Optional[str] = None  # Class name if resolved
    base_class: Optional[str] = None  # Class name if resolved


@dataclass
class StructOffsetResult:
    """Results from struct offset resolution."""
    mismatches: List[OffsetMismatch] = field(default_factory=list)
    class_hints: List[str] = field(default_factory=list)  # Guessed class names
    suggestions: List[str] = field(default_factory=list)  # Actionable suggestions
    error: Optional[str] = None




@dataclass
class AnalysisResult:
    """Combined analysis result."""
    function_name: str
    objdiff: ObjdiffResult
    ghidra: GhidraResult
    m2c: Optional[M2CResult] = None
    struct_offsets: Optional[StructOffsetResult] = None


# =============================================================================
# MCP Client
# =============================================================================

class MCPClient:
    """Client for Ghidra MCP server (uses Server-Sent Events)."""

    def __init__(self, base_url: str = MCP_URL, quiet: bool = False):
        self.base_url = base_url
        self.session_id: Optional[str] = None
        self._request_id = 0
        self.quiet = quiet
        self._binary_name: Optional[str] = BINARY_NAME
        self._binary_resolved = BINARY_NAME is not None

    @property
    def binary_name(self) -> str:
        """Get binary name, resolving dynamically if needed."""
        if not self._binary_resolved:
            self._resolve_binary()
        return self._binary_name or "/default.xex"  # Fallback

    def _resolve_binary(self) -> None:
        """Resolve binary name by querying list_binaries."""
        if self._binary_resolved:
            return

        try:
            binaries = self.list_binaries()

            # Find binary matching "default.xex" pattern
            # pyghidra-mcp generates names like "default.xex-997567"
            if isinstance(binaries, list):
                for binary in binaries:
                    name = binary.get("name", "") if isinstance(binary, dict) else str(binary)
                    if "default.xex" in name:
                        self._binary_name = name.lstrip("/")
                        self._binary_resolved = True
                        return

                # If no match found, use first available binary
                if binaries:
                    first = binaries[0]
                    name = first.get("name", "") if isinstance(first, dict) else str(first)
                    self._binary_name = name.lstrip("/")

            self._binary_resolved = True

        except Exception:
            # Fall back to default
            self._binary_name = "default.xex-997567"
            self._binary_resolved = True

    def list_binaries(self) -> List[Dict[str, str]]:
        """List available binaries in the Ghidra project."""
        result = self.call_tool("list_project_binaries", {})

        if "error" in result:
            return []

        inner_result = result.get("result", {})

        # Check for tool error
        if inner_result.get("isError"):
            return []

        # Try structuredContent first
        structured = inner_result.get("structuredContent", {})
        if structured and "binaries" in structured:
            return structured["binaries"]

        # Fall back to text parsing
        content = inner_result.get("content", [])
        if not content:
            return []

        text = content[0].get("text", "") if content else ""

        # Try parsing as JSON
        try:
            data = json.loads(text)
            if "binaries" in data:
                return data["binaries"]
            if isinstance(data, list):
                return data
        except json.JSONDecodeError:
            pass

        return []

    def _next_id(self) -> int:
        self._request_id += 1
        return self._request_id

    def _parse_sse(self, text: str) -> Dict[str, Any]:
        """Parse Server-Sent Events response to extract JSON data."""
        for line in text.split("\n"):
            line = line.strip()
            if line.startswith("data:"):
                data_str = line[5:].strip()
                if data_str:
                    return json.loads(data_str)
        return {}

    def initialize(self) -> bool:
        """Initialize MCP session."""
        try:
            response = requests.post(
                self.base_url,
                json={
                    "jsonrpc": "2.0",
                    "id": self._next_id(),
                    "method": "initialize",
                    "params": {
                        "protocolVersion": "2024-11-05",
                        "capabilities": {},
                        "clientInfo": {
                            "name": "analyze-function",
                            "version": "1.0.0"
                        }
                    }
                },
                headers={
                    "Content-Type": "application/json",
                    "Accept": "application/json, text/event-stream"
                },
                timeout=10
            )
            self.session_id = response.headers.get("mcp-session-id")
            return self.session_id is not None
        except requests.exceptions.RequestException as e:
            if not self.quiet:
                print(f"Warning: Could not connect to Ghidra MCP: {e}", file=sys.stderr)
            return False

    def call_tool(self, tool_name: str, arguments: Dict[str, Any]) -> Dict[str, Any]:
        """Call an MCP tool."""
        if not self.session_id:
            raise RuntimeError("MCP session not initialized")

        response = requests.post(
            self.base_url,
            json={
                "jsonrpc": "2.0",
                "id": self._next_id(),
                "method": "tools/call",
                "params": {
                    "name": tool_name,
                    "arguments": arguments
                }
            },
            headers={
                "Content-Type": "application/json",
                "Accept": "application/json, text/event-stream",
                "mcp-session-id": self.session_id
            },
            timeout=30
        )
        # Parse SSE format
        return self._parse_sse(response.text)

    def decompile_function(self, name_or_address: str) -> Tuple[str, str, str]:
        """
        Decompile a function by name or address.
        Returns (decompilation, function_name, address).
        """
        result = self.call_tool("decompile_function", {
            "binary_name": self.binary_name,
            "name": name_or_address
        })

        # Check for JSON-RPC error
        if "error" in result:
            raise ValueError(f"Decompile failed: {result['error']}")

        inner_result = result.get("result", {})

        # Check for tool error (isError flag)
        if inner_result.get("isError"):
            content = inner_result.get("content", [])
            error_text = content[0].get("text", "Unknown error") if content else "Unknown error"
            raise ValueError(error_text)

        # Try structuredContent first
        structured = inner_result.get("structuredContent", {})
        if structured and "code" in structured:
            # Extract function name, removing any address suffix
            # "Function_828654D0-828654d0" -> "Function_828654D0"
            name = structured.get("name", "")
            if "-" in name:
                parts = name.rsplit("-", 1)
                if len(parts) == 2 and all(c in "0123456789abcdefABCDEF" for c in parts[1]):
                    name = parts[0]
            return (
                structured.get("code", ""),
                name,
                structured.get("address", "")
            )

        content = inner_result.get("content", [])
        if not content:
            raise ValueError("No decompilation result")

        # Parse the text content - may be JSON
        text = content[0].get("text", "") if content else ""

        # Try to parse as JSON
        try:
            data = json.loads(text)
            if "code" in data:
                # Extract function name, removing any address suffix
                # "Function_828654D0-828654d0" -> "Function_828654D0"
                name = data.get("name", "")
                if "-" in name and name.split("-")[-1].replace("0x", "").isalnum():
                    # Looks like "FuncName-address" format
                    name = name.rsplit("-", 1)[0]
                return (
                    data.get("code", ""),
                    name,
                    data.get("address", "")
                )
        except json.JSONDecodeError:
            pass

        # Return raw text
        return text, "", ""

    def search_symbols(self, query: str, limit: int = 10) -> List[Dict[str, str]]:
        """Search for symbols by name."""
        result = self.call_tool("search_symbols_by_name", {
            "binary_name": self.binary_name,
            "query": query,
            "limit": limit
        })

        if "error" in result:
            return []

        # Try structuredContent first (newer MCP format)
        structured = result.get("result", {}).get("structuredContent", {})
        if structured and "symbols" in structured:
            return [
                {"name": s.get("name", ""), "address": s.get("address", "")}
                for s in structured["symbols"]
            ]

        # Fall back to parsing text content
        content = result.get("result", {}).get("content", [])
        if not content:
            return []

        text = content[0].get("text", "") if content else ""

        # Try parsing as JSON first
        try:
            data = json.loads(text)
            if "symbols" in data:
                return [
                    {"name": s.get("name", ""), "address": s.get("address", "")}
                    for s in data["symbols"]
                ]
        except json.JSONDecodeError:
            pass

        # Parse line-by-line format
        symbols = []
        for line in text.split("\n"):
            line = line.strip()
            if line and not line.startswith("Found") and not line.startswith("---"):
                if " @ " in line:
                    name, addr = line.rsplit(" @ ", 1)
                    symbols.append({"name": name.strip(), "address": addr.strip()})
                elif line:
                    symbols.append({"name": line, "address": ""})
        return symbols

    def list_cross_references(self, name_or_address: str) -> Tuple[List[str], List[str]]:
        """
        Get cross-references for a function.
        Returns (callers, callees).
        """
        result = self.call_tool("list_cross_references", {
            "binary_name": self.binary_name,
            "name_or_address": name_or_address
        })

        if "error" in result:
            return [], []

        inner_result = result.get("result", {})

        # Check for tool error
        if inner_result.get("isError"):
            return [], []

        # Try structuredContent first
        structured = inner_result.get("structuredContent", {})
        if structured and "cross_references" in structured:
            callers = []
            callees = []
            for xref in structured["cross_references"]:
                direction = xref.get("direction", "inbound")
                func_name = xref.get("function_name", "")
                if func_name:
                    if direction == "inbound":
                        callers.append(func_name)
                    elif direction == "outbound":
                        callees.append(func_name)
            return callers, callees

        # Fall back to text parsing
        content = inner_result.get("content", [])
        if not content:
            return [], []

        text = content[0].get("text", "") if content else ""

        callers = []
        callees = []
        current_section = None

        for line in text.split("\n"):
            line = line.strip()
            if "References TO" in line or "Callers" in line.lower():
                current_section = "callers"
            elif "References FROM" in line or "Callees" in line.lower():
                current_section = "callees"
            elif line.startswith("-") or line.startswith("•"):
                name = line.lstrip("-•").strip()
                if " @ " in name:
                    name = name.split(" @ ")[0].strip()
                if name:
                    if current_section == "callers":
                        callers.append(name)
                    elif current_section == "callees":
                        callees.append(name)

        return callers, callees


# =============================================================================
# Build Integration
# =============================================================================

def resolve_unit_from_symbol(unit_path: str) -> str:
    """
    Convert a unit name to an object file path for incremental builds.

    Args:
        unit_path: Unit name like "default/system/char/Character"
                   or just "system/char/Character"

    Returns:
        Object file path like "build/373307D9/src/system/char/Character.obj"

    Examples:
        "default/system/char/Character" -> "build/373307D9/src/system/char/Character.obj"
        "system/char/Character" -> "build/373307D9/src/system/char/Character.obj"
    """
    # Remove "default/" prefix if present
    if unit_path.startswith("default/"):
        unit_path = unit_path[8:]  # len("default/") == 8

    # Build the object file path
    obj_path = f"build/373307D9/src/{unit_path}.obj"
    return obj_path


# =============================================================================
# objdiff Integration
# =============================================================================

def parse_objdiff_ambiguous_matches(stderr: str) -> Tuple[List[str], List[Tuple[str, str]]]:
    """
    Parse ambiguous match suggestions from objdiff stderr.

    objdiff outputs lines like:
      Multiple matches for 'X'. Did you mean:
        public: void __cdecl Foo::Bar(void) (default/unit/path)
        void __cdecl `...`::`dynamic atexit destructor'(void) (default/unit/path)

    Returns tuple of:
      - List of demangled symbol names (without the unit path suffix)
      - List of (demangled_name, unit_path) tuples for constructing commands
    """
    matches = []
    matches_with_units = []
    in_suggestions = False

    for line in stderr.split('\n'):
        stripped = line.strip()

        # Detect start of suggestions
        if "Multiple matches for" in stripped and "Did you mean:" in stripped:
            in_suggestions = True
            continue

        # Stop at the "Failed:" line
        if stripped.startswith("Failed:"):
            break

        # Parse suggestion lines (indented with 2 spaces)
        if in_suggestions and stripped:
            # Each suggestion is: "demangled_name (unit/path)"
            # Extract the demangled name by removing the trailing "(unit/path)"
            if " (" in stripped:
                # Find the last " (" which precedes the unit path
                last_paren = stripped.rfind(" (")
                if last_paren > 0:
                    demangled = stripped[:last_paren].strip()
                    # Extract unit path from parentheses
                    unit_part = stripped[last_paren+2:-1].strip()  # Remove " (" and ")"
                    matches.append(demangled)
                    matches_with_units.append((demangled, unit_part))
            else:
                matches.append(stripped)

    return matches, matches_with_units


def run_objdiff(
    function_name: str,
    project_dir: str,
    unit: Optional[str] = None,
    incremental: bool = True
) -> ObjdiffResult:
    """
    Run objdiff diff and parse results.

    Args:
        function_name: Function name to analyze
        project_dir: Project directory
        unit: Unit name to disambiguate
        incremental: If True, run incremental build. If False, run full build.
    """
    result = ObjdiffResult()

    try:
        # Perform build step
        build_start = time.time()
        if incremental and unit:
            # Incremental build: only build the specific unit
            obj_path = resolve_unit_from_symbol(unit)
            print(f"Building incrementally: {obj_path}", file=sys.stderr)
            build_proc = subprocess.run(
                ["ninja", obj_path],
                cwd=project_dir,
                capture_output=True,
                text=True,
                timeout=120
            )
            if build_proc.returncode != 0:
                print(f"Warning: Incremental build failed: {build_proc.stderr}", file=sys.stderr)
        else:
            # Full build (existing behavior)
            print("Building full project...", file=sys.stderr)
            build_proc = subprocess.run(
                ["ninja"],
                cwd=project_dir,
                capture_output=True,
                text=True,
                timeout=600
            )
            if build_proc.returncode != 0:
                print(f"Warning: Full build failed: {build_proc.stderr}", file=sys.stderr)
        build_time = time.time() - build_start
        print(f"Build completed in {build_time:.1f}s", file=sys.stderr)

        # Run objdiff
        cmd = [
            OBJDIFF_CLI, "diff",
            "-p", project_dir,
            function_name,
            "-f", "json",
            "--verdict"
        ]
        if unit:
            cmd.extend(["-u", unit])

        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=60
        )

        if proc.returncode != 0:
            stderr = proc.stderr.strip()

            # Check for ambiguous symbol error and try to resolve by filtering destructors
            if "Multiple matches for" in stderr and "Did you mean:" in stderr:
                matches, matches_with_units = parse_objdiff_ambiguous_matches(stderr)

                # Filter out destructor symbols
                non_destructors = [m for m in matches if not is_destructor_symbol(m)]
                non_destructors_with_units = [(m, u) for m, u in matches_with_units if not is_destructor_symbol(m)]

                if len(non_destructors) == 1:
                    # Single non-destructor match - retry with that specific symbol
                    # Use the demangled name directly since objdiff accepts it
                    return run_objdiff(non_destructors[0], project_dir, unit, incremental=incremental)
                elif len(non_destructors) > 1:
                    # Multiple non-destructor matches - show copyable commands
                    suggestions = []
                    for demangled, unit_path in non_destructors_with_units[:5]:
                        suggestions.append(f'  ./bin/analyze-function "{function_name}" -u {unit_path}')
                    if len(non_destructors_with_units) > 5:
                        suggestions.append(f"  ... (+{len(non_destructors_with_units) - 5} more)")

                    suggestions_text = "\n".join(suggestions)
                    result.error = (
                        f"Multiple matches for '{function_name}'. Try:\n{suggestions_text}\n\n"
                        f"List all units: ./bin/analyze-function --list-units"
                    )
                    return result
                # else: no non-destructors, fall through to original error

            result.error = stderr or f"objdiff exited with code {proc.returncode}"
            return result

        data = json.loads(proc.stdout)
        result.raw = data
        result.symbol = data.get("symbol", "")
        result.demangled = data.get("demangled", "")
        result.fuzzy_match_percent = data.get("fuzzy_match_percent", 0.0)
        result.target_size = data.get("target_size", 0)
        result.base_size = data.get("base_size", 0)
        result.verdict = data.get("verdict")
        result.analysis = data.get("analysis")
        result.instruction_summary = data.get("instruction_summary")

        # Extract suggestions from verdict
        if result.verdict:
            result.suggestions = result.verdict.get("suggestions", [])

    except subprocess.TimeoutExpired:
        result.error = "objdiff timed out"
    except json.JSONDecodeError as e:
        result.error = f"Failed to parse objdiff output: {e}"
    except FileNotFoundError:
        result.error = f"objdiff-cli not found at {OBJDIFF_CLI}"
    except Exception as e:
        result.error = f"objdiff error: {e}"

    return result



# =============================================================================
# Struct Offset Resolution
# =============================================================================

def parse_offset_from_args(args: str) -> Optional[Tuple[int, str]]:
    """
    Parse an offset and base register from an instruction's args string.

    Handles formats like:
        "r11, 0x2c, r30" -> (0x2c, "r30")
        "r3, -0x8, r1" -> (-0x8, "r1")

    Returns:
        Tuple of (offset, base_register) or None if not a memory access
    """
    import re
    # Pattern for PPC memory access: "dest_reg, offset, base_reg"
    pattern = re.compile(r'r\d+,\s*(-?0x[0-9a-fA-F]+),\s*(r\d+)')
    match = pattern.search(args)
    if match:
        offset_str = match.group(1)
        base_reg = match.group(2)
        try:
            offset = int(offset_str, 16)
            return (offset, base_reg)
        except ValueError:
            pass
    return None


def extract_offset_mismatches(
    objdiff_data: Dict[str, Any]
) -> List[OffsetMismatch]:
    """
    Extract offset mismatches from objdiff instruction data.

    Looks for diff_arg matches where both target and base have memory
    access instructions with different offsets.
    """
    mismatches = []
    instructions = objdiff_data.get('instructions', [])

    # Memory access opcodes
    memory_ops = {
        'lwz', 'lbz', 'lhz', 'lha', 'lfs', 'lfd', 'lmw',
        'stw', 'stb', 'sth', 'stfs', 'stfd', 'stmw',
        'lwzu', 'lbzu', 'lhzu', 'lfsu', 'lfdu',
        'stwu', 'stbu', 'sthu', 'stfsu', 'stfdu',
        'addi', 'subi',  # Also check addi for struct field access
    }

    for instr in instructions:
        match_type = instr.get('match_type', '')
        if match_type != 'diff_arg':
            continue

        target = instr.get('target', {})
        base = instr.get('base', {})

        target_op = target.get('opcode', '')
        base_op = base.get('opcode', '')

        # Both must be memory operations (or same operation)
        if target_op != base_op:
            continue
        if target_op not in memory_ops:
            continue

        target_args = target.get('args', '')
        base_args = base.get('args', '')

        target_parsed = parse_offset_from_args(target_args)
        base_parsed = parse_offset_from_args(base_args)

        if not target_parsed or not base_parsed:
            continue

        target_offset, target_reg = target_parsed
        base_offset, base_reg = base_parsed

        # Check if offsets differ (that's the mismatch we care about)
        if target_offset != base_offset:
            mismatch = OffsetMismatch(
                index=instr.get('index', -1),
                opcode=target_op,
                target_offset=target_offset,
                base_offset=base_offset,
                base_register=target_reg  # Usually same as base_reg
            )
            mismatches.append(mismatch)

    return mismatches


def resolve_struct_offsets(
    mismatches: List[OffsetMismatch],
    class_hints: List[str],
    project_dir: str
) -> StructOffsetResult:
    """
    Resolve struct field names for offset mismatches using struct_db.

    Args:
        mismatches: List of OffsetMismatch objects
        class_hints: Class names to try for lookup (e.g., from function name)
        project_dir: Project directory containing struct_db.sqlite

    Returns:
        StructOffsetResult with resolved field names and suggestions
    """
    result = StructOffsetResult(mismatches=mismatches)

    db_path = os.path.join(project_dir, 'struct_db.sqlite')
    if not os.path.exists(db_path):
        result.error = f'struct_db.sqlite not found. Run: python3 tools/struct_db.py build src/'
        return result

    try:
        # Import struct_db
        import sys
        tools_dir = os.path.join(project_dir, 'tools')
        if tools_dir not in sys.path:
            sys.path.insert(0, tools_dir)
        from struct_db import StructDB

        db = StructDB(db_path)
        db.connect()

        # Try to resolve each mismatch
        for mismatch in mismatches:
            # Try each class hint
            for class_name in class_hints:
                # Resolve target offset
                target_result = db.lookup(class_name, mismatch.target_offset)
                if target_result:
                    cls, member, type_str = target_result
                    mismatch.target_field = member
                    mismatch.target_class = cls

                # Resolve base offset
                base_result = db.lookup(class_name, mismatch.base_offset)
                if base_result:
                    cls, member, type_str = base_result
                    mismatch.base_field = member
                    mismatch.base_class = cls

                # If we found at least one, keep this class hint
                if mismatch.target_field or mismatch.base_field:
                    if class_name not in result.class_hints:
                        result.class_hints.append(class_name)
                    break

        # Generate suggestions based on resolved mismatches
        for mismatch in mismatches:
            if mismatch.target_field and mismatch.base_field:
                suggestion = (
                    f'Offset 0x{mismatch.target_offset:x} ({mismatch.target_class}::{mismatch.target_field}) '
                    f'in target differs from 0x{mismatch.base_offset:x} ({mismatch.base_class}::{mismatch.base_field}) '
                    f'in compiled code. Check struct layout or field order.'
                )
            elif mismatch.target_field:
                suggestion = (
                    f'Target uses offset 0x{mismatch.target_offset:x} ({mismatch.target_class}::{mismatch.target_field}), '
                    f'but compiled uses 0x{mismatch.base_offset:x} (unknown field). '
                    f'Field may be missing or at wrong offset in header.'
                )
            elif mismatch.base_field:
                suggestion = (
                    f'Compiled uses offset 0x{mismatch.base_offset:x} ({mismatch.base_class}::{mismatch.base_field}), '
                    f'but target expects 0x{mismatch.target_offset:x}. '
                    f'Check if struct has additional fields not in header.'
                )
            else:
                suggestion = (
                    f'Offset mismatch at instruction {mismatch.index}: '
                    f'target 0x{mismatch.target_offset:x} vs base 0x{mismatch.base_offset:x}. '
                    f'Unable to resolve field names - check struct annotations.'
                )
            result.suggestions.append(suggestion)

        db.close()

    except Exception as e:
        result.error = f'Error resolving struct offsets: {e}'

    return result


def extract_class_hints_from_demangled(demangled: str) -> List[str]:
    """
    Extract potential class names from a demangled function name.

    Examples:
        "public: virtual void __cdecl CharBone::Copy(...)" -> ["CharBone"]
        "Game::Poll" -> ["Game"]
        "RndTransformable::SetLocalPos" -> ["RndTransformable"]
    """
    import re
    hints = []

    # Pattern for Class::Method
    pattern = re.compile(r'(\w+)::')
    matches = pattern.findall(demangled)

    for match in matches:
        # Skip common prefixes that aren't class names
        if match.lower() in ('public', 'private', 'protected', 'virtual', 'void', 'class', 'struct'):
            continue
        if match not in hints:
            hints.append(match)

    return hints


# =============================================================================
# m2c Integration
# =============================================================================

def run_m2c(
    function_name: str,
    project_dir: str,
    unit: Optional[str] = None,
    context_file: Optional[str] = None,
    decomp_mode: bool = True,
    noise_level: Optional[str] = None,
    show_offsets: bool = False
) -> M2CResult:
    """
    Run m2c decompilation on a function.

    This uses objdiff to get the target binary's disassembly,
    converts it to m2c format, and runs m2c on it.

    Args:
        function_name: Function name to decompile
        project_dir: Project directory
        unit: Unit name to disambiguate
        context_file: Optional path to m2c context file for type info

    Returns:
        M2CResult with decompilation or error
    """
    result = M2CResult()

    try:
        # Step 1: Get objdiff JSON with instructions
        cmd = [
            OBJDIFF_CLI, "diff",
            "-p", project_dir,
            function_name,
            "-f", "json",
            "--include-instructions"
        ]
        if unit:
            cmd.extend(["-u", unit])

        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=60
        )

        if proc.returncode != 0:
            result.error = f"objdiff failed: {proc.stderr.strip()}"
            return result

        objdiff_json = proc.stdout

        # Step 2: Convert to m2c assembly format via objdiff_to_m2c.py
        proc = subprocess.run(
            ["python3", OBJDIFF_TO_M2C],
            input=objdiff_json,
            capture_output=True,
            text=True,
            timeout=30
        )

        if proc.returncode != 0:
            result.error = f"objdiff_to_m2c failed: {proc.stderr.strip()}"
            return result

        m2c_asm = proc.stdout

        if not m2c_asm.strip():
            result.error = "No assembly output from objdiff_to_m2c"
            return result

        # Step 3: Run m2c
        m2c_cmd = ["python3", M2C_PATH, "-t", "ppc"]
        if context_file:
            m2c_cmd.extend(["--context", context_file])
        # Decomp-friendly output flags
        if decomp_mode:
            m2c_cmd.append("--decomp")
        if noise_level:
            m2c_cmd.extend(["--noise", noise_level])
        if show_offsets:
            m2c_cmd.append("--show-offsets")
        m2c_cmd.append("-")  # Read from stdin

        proc = subprocess.run(
            m2c_cmd,
            input=m2c_asm,
            capture_output=True,
            text=True,
            timeout=60
        )

        if proc.returncode != 0:
            result.error = f"m2c failed: {proc.stderr.strip()}"
            return result

        result.decompilation = proc.stdout.strip()

    except subprocess.TimeoutExpired:
        result.error = "m2c pipeline timed out"
    except FileNotFoundError as e:
        result.error = f"Required tool not found: {e}"
    except Exception as e:
        result.error = f"m2c error: {e}"

    return result


# =============================================================================
# Unit Listing
# =============================================================================

def list_units(filter_pattern: Optional[str] = None, project_dir: str = PROJECT_DIR) -> List[str]:
    """
    List all available unit names from objdiff.json.

    Args:
        filter_pattern: Optional substring to filter units (case-insensitive)
        project_dir: Project directory containing objdiff.json

    Returns:
        List of unit names, one per line formatted for output
    """
    objdiff_path = os.path.join(project_dir, "objdiff.json")

    try:
        with open(objdiff_path, "r") as f:
            data = json.load(f)
    except (IOError, OSError, json.JSONDecodeError) as e:
        raise ValueError(f"Failed to read {objdiff_path}: {e}")

    units = data.get("units", [])
    if not units:
        raise ValueError("No units found in objdiff.json")

    # Extract unit names
    unit_names = [u.get("name", "") for u in units if u.get("name")]

    # Apply filter if provided
    if filter_pattern:
        filter_lower = filter_pattern.lower()
        unit_names = [u for u in unit_names if filter_lower in u.lower()]

    return unit_names


# =============================================================================
# Stub/Wrong Function Detection
# =============================================================================

def detect_potential_stub(
    decompiled_code: str,
    objdiff_size: int,
) -> List[str]:
    """
    Detect if decompiled code might be a stub or wrong function.

    When Ghidra resolves a symbol, it may return a tiny stub function instead
    of the real implementation. This happens when:
    - The symbol points to a thunk/trampoline
    - Ghidra found the wrong function with similar name
    - The function is actually tiny (rare for >100 byte functions)

    Args:
        decompiled_code: The decompiled C code from Ghidra
        objdiff_size: The base_size from objdiff (expected function size in bytes)

    Returns:
        List of warning messages (empty if no issues detected)
    """
    import re

    warnings = []

    if not decompiled_code or objdiff_size <= 0:
        return warnings

    lines = decompiled_code.strip().split('\n')

    # Count "body lines" - excluding braces, comments, blank lines, signature
    body_lines = 0
    has_savegprlr = False
    return_count = 0
    signature_found = False

    for line in lines:
        stripped = line.strip()

        # Skip empty lines
        if not stripped:
            continue

        # Skip pure brace lines
        if stripped in ('{', '}'):
            continue

        # Skip comments
        if stripped.startswith('//') or stripped.startswith('/*'):
            continue

        # Skip function signature (typically first non-empty, non-brace line)
        # Signature patterns: "void FuncName(...)" or "type FuncName(...) {"
        if '(' in stripped and ')' in stripped and not signature_found:
            # This is likely the signature line
            signature_found = True
            continue

        # Detect __savegprlr prologue-only stubs
        if '__savegprlr' in stripped.lower() or '_savegprlr' in stripped.lower():
            has_savegprlr = True

        # Count return statements
        if 'return' in stripped:
            return_count += 1

        body_lines += 1

    # Check for "only return" pattern - single return statement and maybe a variable decl
    only_return = body_lines <= 2 and return_count >= 1

    # Calculate total character count (excluding whitespace-only content)
    total_chars = len(decompiled_code.strip())

    # Extract function name for exemption checks
    func_name = ""
    for line in lines:
        stripped = line.strip()
        if '(' in stripped and ')' in stripped:
            # Try to extract function name
            # Patterns: "void ClassName::Method(" or "void Method("
            match = re.search(r'(\w+(?:::\w+)?)\s*\(', stripped)
            if match:
                func_name = match.group(1)
            break

    # --- False Positive Prevention ---

    # Exempt constructors if size < 64 bytes
    # Constructor pattern: "ClassName::ClassName(" or just "ClassName("
    is_constructor = False
    if func_name:
        if '::' in func_name:
            parts = func_name.split('::')
            # Check if method name matches class name (constructor)
            if len(parts) >= 2 and parts[-1] == parts[-2]:
                is_constructor = True
        # Also check for ~ClassName (destructor)
        if '~' in func_name:
            is_constructor = True  # Treat destructors similarly

    if is_constructor and objdiff_size < 64:
        return warnings

    # Exempt simple accessors (Get*, Set*) if size < 48 bytes
    if func_name:
        method_name = func_name.split('::')[-1] if '::' in func_name else func_name
        if (method_name.startswith('Get') or method_name.startswith('Set')) and objdiff_size < 48:
            return warnings

    # --- Heuristic Checks ---

    # Check 1: Very few body lines but large expected size
    if body_lines < 5 and objdiff_size > 100:
        warnings.append(
            f"Decompilation has only {body_lines} body line(s) but objdiff reports "
            f"{objdiff_size} bytes - possible stub or wrong function"
        )

    # Check 2: Only return statement but significant size
    if only_return and objdiff_size > 32:
        warnings.append(
            f"Decompilation contains only a return statement but objdiff reports "
            f"{objdiff_size} bytes - likely a stub or thunk"
        )

    # Check 3: Very small character count vs expected size
    if total_chars < 150 and objdiff_size > 200:
        warnings.append(
            f"Decompilation is very short ({total_chars} chars) for a {objdiff_size}-byte "
            f"function - possible wrong function resolved"
        )

    # Check 4: __savegprlr prologue-only stub
    if has_savegprlr and body_lines < 5:
        warnings.append(
            "Decompilation contains __savegprlr prologue but minimal body - "
            "likely a register-saving stub, not the real function"
        )

    return warnings


# =============================================================================
# Symbol Address Lookup
# =============================================================================

def lookup_symbol_address(mangled_name: str, project_dir: str) -> Optional[str]:
    """Look up the absolute address of a symbol from symbols.txt.

    symbols.txt format:
    ?Copy@RndMat@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z = .text:0x826CC098; // ...

    Returns address like "0x826CC098" or None if not found.
    """
    symbols_path = os.path.join(project_dir, "config", "373307D9", "symbols.txt")

    if not os.path.exists(symbols_path):
        return None

    try:
        with open(symbols_path, "r") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("//"):
                    continue

                # Parse: symbol_name = .section:0xADDRESS; // comment
                if " = " not in line:
                    continue

                symbol_part, rest = line.split(" = ", 1)

                # Exact match on symbol name
                if symbol_part.strip() != mangled_name:
                    continue

                # Extract address from ".text:0x826CC098;" or ".rdata:0x82000600;"
                if ":" in rest:
                    section_addr = rest.split(";")[0].strip()
                    if ":" in section_addr:
                        addr = section_addr.split(":")[1].strip()
                        return addr

    except (IOError, OSError):
        pass

    return None


# =============================================================================
# Function Resolution
# =============================================================================

def is_destructor_symbol(symbol_name: str) -> bool:
    """
    Check if a symbol is a destructor or atexit destructor from static locals.

    These symbols are generated by the compiler for cleanup of static local variables
    and typically look like:
      - "dynamic atexit destructor for 'X'"
      - "`dynamic atexit destructor"
      - Mangled versions with ??__F prefix
    """
    lower_name = symbol_name.lower()
    # Check for common destructor patterns
    destructor_patterns = [
        "dynamic atexit destructor",
        "`dynamic atexit destructor",
        "??__f",  # MSVC mangled prefix for dynamic atexit destructors
        "atexit destructor",
    ]
    return any(pattern in lower_name for pattern in destructor_patterns)


def filter_destructor_symbols(symbols: List[Dict[str, str]]) -> List[Dict[str, str]]:
    """
    Filter out destructor symbols from a list, preferring main function symbols.

    If filtering leaves no symbols, returns original list unchanged.
    """
    filtered = [s for s in symbols if not is_destructor_symbol(s.get("name", ""))]
    # Only use filtered list if it still has results
    return filtered if filtered else symbols


def resolve_function_for_ghidra(mcp: MCPClient, name: str, verbose: bool = False) -> Tuple[str, str]:
    """
    Resolve a function name/address for Ghidra.

    Returns (name, address) tuple. Address is preferred for decompilation
    since Ghidra may not recognize mangled symbol names.

    Strategy:
    1. Try the name directly (works for some names)
    2. Extract method name and search (e.g., "Game::PollShuttle" -> "PollShuttle")
    3. If single match, use its address
    4. If multiple matches, pick best or fail with choices
    """
    vprint(f"Input name: {name}", verbose)

    # First, try direct decompilation
    try:
        mcp.decompile_function(name)
        vprint(f"Direct lookup succeeded for: {name}", verbose)
        return name, ""
    except ValueError:
        vprint("Direct lookup failed, trying search...", verbose)

    # Extract search term and class name
    search_term = name
    class_name = ""
    method_name = ""

    if "::" in name:
        parts = name.split("::")
        class_name = parts[0]
        method_name = parts[-1]
        # Remove any trailing parentheses/params
        if "(" in method_name:
            method_name = method_name.split("(")[0]
        search_term = method_name
        vprint(f"Parsed class={class_name}, method={method_name}", verbose)

    # For Class::Method, try searching for mangled pattern first
    # e.g., "RndMat::Copy" -> search for "Copy@RndMat" (mangled format)
    symbols = []
    if class_name and method_name:
        mangled_pattern = f"{method_name}@{class_name}"
        vprint(f"Searching for mangled pattern: {mangled_pattern}", verbose)
        symbols = mcp.search_symbols(mangled_pattern, limit=10)
        vprint(f"Found {len(symbols)} result(s) for mangled pattern", verbose)

    # Fall back to just method name
    if not symbols:
        vprint(f"Searching for method name: {search_term}", verbose)
        symbols = mcp.search_symbols(search_term, limit=10)
        vprint(f"Found {len(symbols)} result(s) for method name", verbose)

    if not symbols:
        # Try the full name as last resort
        if search_term != name:
            vprint(f"Searching for full name: {name}", verbose)
            symbols = mcp.search_symbols(name, limit=10)
            vprint(f"Found {len(symbols)} result(s) for full name", verbose)
        if not symbols:
            # No results - return original name and let caller handle error
            vprint("No symbols found, returning original name", verbose)
            return name, ""

    # Log all symbols found
    if verbose:
        for sym in symbols:
            addr = sym.get("address", "")
            addr_str = f" @ {addr}" if addr else ""
            vprint(f"  - {sym['name']}{addr_str}", verbose)

    # Filter out destructor symbols (e.g., "dynamic atexit destructor for 'X'")
    # These are generated for static locals and clutter search results
    original_count = len(symbols)
    symbols = filter_destructor_symbols(symbols)
    if len(symbols) < original_count:
        vprint(f"Filtered out {original_count - len(symbols)} destructor symbol(s)", verbose)

    if len(symbols) == 1:
        # Single match - return name and address
        vprint(f"Single match found: {symbols[0]['name']}", verbose)
        return symbols[0]["name"], symbols[0].get("address", "")

    # Multiple matches - check for exact match first
    for sym in symbols:
        if sym["name"] == name:
            vprint(f"Exact match found: {name}", verbose)
            return name, sym.get("address", "")

    # Check if there's a clear best match (contains the search term)
    # For "PollShuttle", we want "?PollShuttle@Game@@" not "?PollShuttleEx@Game@@"
    close_matches = [s for s in symbols if search_term in s["name"]]
    vprint(f"Close matches containing '{search_term}': {len(close_matches)}", verbose)
    if len(close_matches) == 1:
        vprint(f"Single close match: {close_matches[0]['name']}", verbose)
        return close_matches[0]["name"], close_matches[0].get("address", "")

    # If we have the class name, filter by it too (in mangled form)
    if class_name:
        # Mangled names have @ClassName@@ pattern (double @)
        class_matches = [s for s in close_matches if f"@{class_name}@@" in s["name"]]
        vprint(f"Class matches for @{class_name}@@: {len(class_matches)}", verbose)
        if len(class_matches) == 1:
            vprint(f"Single class match: {class_matches[0]['name']}", verbose)
            return class_matches[0]["name"], class_matches[0].get("address", "")
        if class_matches:
            close_matches = class_matches

    # Still ambiguous - raise with choices
    vprint("Ambiguous: multiple matches remain", verbose)
    choices = "\n".join(f"  - {s['name']}" for s in symbols[:5])
    raise ValueError(f"Ambiguous function name '{name}'. Possible matches:\n{choices}")


# =============================================================================
# Output Formatting
# =============================================================================

def format_markdown(result: AnalysisResult) -> str:
    """Format analysis result as markdown."""
    lines = []
    obj = result.objdiff
    ghidra = result.ghidra

    # Title
    display_name = obj.demangled or obj.symbol or result.function_name
    lines.append(f"# Analysis: {display_name}")
    lines.append("")

    # Diff Results Section
    lines.append("## Diff Results")

    if obj.error:
        lines.append(f"**Error**: {obj.error}")
    else:
        lines.append(f"- **Match**: {obj.fuzzy_match_percent:.1f}%")

        if obj.verdict:
            classification = obj.verdict.get("classification", "UNKNOWN")
            lines.append(f"- **Verdict**: {classification}")

        lines.append(f"- **Size**: {obj.target_size} bytes (target) vs {obj.base_size} bytes (base)")

        # Patterns
        if obj.analysis and obj.analysis.get("patterns"):
            lines.append("")
            lines.append("### Patterns Detected")
            for pattern in obj.analysis["patterns"]:
                name = pattern.get("pattern", "UNKNOWN")
                count = pattern.get("instruction_count", 0)
                fixability = pattern.get("fixability", "unknown")
                lines.append(f"- **{name}**: {count} instruction(s) - {fixability}")

                # Add documentation link and quick tip
                doc_info = get_pattern_doc_info(name)
                if doc_info:
                    doc_path, anchor, tip = doc_info
                    lines.append(f"  - [See pattern docs]({doc_path})")
                    lines.append(f"  - {tip}")

                # Pattern details
                details = pattern.get("details", {})
                if "merged_functions" in details:
                    for mf in details["merged_functions"][:3]:
                        lines.append(f"  - `{mf['name']}`: {mf['count']} call(s)")
                if "swaps" in details:
                    for swap in details["swaps"][:3]:
                        lines.append(f"  - {swap}")
                if "branch_diffs" in details:
                    for bd in details["branch_diffs"][:3]:
                        lines.append(f"  - Index {bd.get('index', '?')}: {bd.get('target_op', '?')} vs {bd.get('base_op', '?')}")

        # Suggestions
        if obj.suggestions:
            lines.append("")
            lines.append("### Suggestions")
            for i, suggestion in enumerate(obj.suggestions, 1):
                # Handle suggestion as dict with 'action' field or as plain string
                if isinstance(suggestion, dict):
                    action = suggestion.get("action", str(suggestion))
                else:
                    action = str(suggestion)
                lines.append(f"{i}. {action}")

        # Verdict explanation
        if obj.verdict and obj.verdict.get("explanation"):
            lines.append("")
            lines.append(f"**Explanation**: {obj.verdict['explanation']}")

    lines.append("")

    # Ghidra Decompilation Section
    lines.append("## Ghidra Decompilation")

    # Show address warning if there was a mismatch
    if ghidra.address_warning:
        lines.append(f"**Warning**: {ghidra.address_warning}")
        lines.append("")

    # Show resolution warnings (stub detection)
    if ghidra.warnings:
        lines.append("### Resolution Warnings")
        for warning in ghidra.warnings:
            lines.append(f"- {warning}")
        lines.append("")
        lines.append("**Recommendation**: Verify the correct function was resolved.")
        lines.append("")

    if ghidra.error:
        lines.append(f"**Error**: {ghidra.error}")
        # Suggest m2c as fallback when Ghidra fails
        func_arg = obj.demangled or obj.symbol or result.function_name
        func_arg_escaped = func_arg.replace('"', '\\"')
        lines.append("")
        lines.append("**Alternative**: Try m2c decompilation:")
        lines.append(f'```bash')
        lines.append(f'./tools/decompile.sh "{func_arg_escaped}"')
        lines.append(f'# Or add --m2c flag to this command:')
        lines.append(f'python tools/analyze_function.py "{func_arg_escaped}" --m2c')
        lines.append(f'```')
    elif ghidra.decompilation:
        lines.append("```c")
        lines.append(ghidra.decompilation.strip())
        lines.append("```")
    else:
        lines.append("*No decompilation available*")
        # Suggest m2c as alternative
        func_arg = obj.demangled or obj.symbol or result.function_name
        func_arg_escaped = func_arg.replace('"', '\\"')
        lines.append("")
        lines.append("**Alternative**: Try m2c decompilation:")
        lines.append(f'```bash')
        lines.append(f'./tools/decompile.sh "{func_arg_escaped}"')
        lines.append(f'```')

    lines.append("")

    # m2c Decompilation Section
    if result.m2c is not None:
        lines.append("## m2c Decompilation")

        if result.m2c.error:
            lines.append(f"**Error**: {result.m2c.error}")
        elif result.m2c.decompilation:
            lines.append("```c")
            lines.append(result.m2c.decompilation.strip())
            lines.append("```")
        else:
            lines.append("*No decompilation available*")

        lines.append("")

    # Struct Offset Resolution Section
    if result.struct_offsets is not None:
        offsets = result.struct_offsets
        if offsets.mismatches or offsets.error:
            lines.append("## Struct Offset Analysis")

            if offsets.error:
                lines.append(f"**Error**: {offsets.error}")
            elif offsets.mismatches:
                lines.append(f"Found {len(offsets.mismatches)} offset mismatch(es):")
                lines.append("")

                for mismatch in offsets.mismatches[:10]:  # Limit to first 10
                    target_info = f"0x{mismatch.target_offset:x}"
                    base_info = f"0x{mismatch.base_offset:x}"

                    if mismatch.target_field:
                        target_info = f"{mismatch.target_class}::{mismatch.target_field} (0x{mismatch.target_offset:x})"
                    if mismatch.base_field:
                        base_info = f"{mismatch.base_class}::{mismatch.base_field} (0x{mismatch.base_offset:x})"

                    lines.append(f"- **{mismatch.opcode}** at index {mismatch.index}:")
                    lines.append(f"  - Target: {target_info}")
                    lines.append(f"  - Base: {base_info}")

                if len(offsets.mismatches) > 10:
                    lines.append(f"  - ... (+{len(offsets.mismatches) - 10} more)")

                # Add suggestions
                if offsets.suggestions:
                    lines.append("")
                    lines.append("### Suggestions")
                    for suggestion in offsets.suggestions[:5]:
                        lines.append(f"- {suggestion}")

            lines.append("")

    # Cross References Section
    if ghidra.callers or ghidra.callees:
        lines.append("## Cross References")

        if ghidra.callers:
            caller_list = ", ".join(ghidra.callers[:5])
            if len(ghidra.callers) > 5:
                caller_list += f", ... (+{len(ghidra.callers) - 5} more)"
            lines.append(f"**Called by** ({len(ghidra.callers)}): {caller_list}")

        if ghidra.callees:
            callee_list = ", ".join(ghidra.callees[:5])
            if len(ghidra.callees) > 5:
                callee_list += f", ... (+{len(ghidra.callees) - 5} more)"
            lines.append(f"**Calls** ({len(ghidra.callees)}): {callee_list}")

        lines.append("")

    # Footer with next-step commands
    lines.append("---")
    func_arg = obj.demangled or obj.symbol or result.function_name
    # Escape quotes for shell
    func_arg_escaped = func_arg.replace('"', '\\"')
    lines.append(f'*To generate call graph*: `./bin/ghidra-callgraph "{func_arg_escaped}"`')
    lines.append(f'*To see instruction diff*: `./bin/objdiff-cli diff -p . "{func_arg_escaped}" -f markdown --include-instructions`')

    return "\n".join(lines)


def format_json(result: AnalysisResult) -> str:
    """Format analysis result as JSON."""
    obj = result.objdiff
    ghidra = result.ghidra

    # Enrich analysis patterns with documentation info
    analysis_with_docs = obj.analysis
    if analysis_with_docs and analysis_with_docs.get("patterns"):
        enriched_patterns = []
        for pattern in analysis_with_docs["patterns"]:
            pattern_copy = dict(pattern)
            doc_info = get_pattern_doc_info(pattern.get("pattern", ""))
            if doc_info:
                doc_path, anchor, tip = doc_info
                pattern_copy["doc_path"] = doc_path
                pattern_copy["quick_tip"] = tip
            enriched_patterns.append(pattern_copy)
        analysis_with_docs = dict(analysis_with_docs)
        analysis_with_docs["patterns"] = enriched_patterns

    output = {
        "function": result.function_name,
        "demangled": obj.demangled or None,
        "symbol": obj.symbol or None,
        "objdiff": {
            "fuzzy_match_percent": obj.fuzzy_match_percent,
            "target_size": obj.target_size,
            "base_size": obj.base_size,
            "verdict": obj.verdict,
            "analysis": analysis_with_docs,
            "instruction_summary": obj.instruction_summary,
            "suggestions": obj.suggestions,
            "error": obj.error
        },
        "ghidra": {
            "decompilation": ghidra.decompilation or None,
            "callers": ghidra.callers,
            "callees": ghidra.callees,
            "warnings": ghidra.warnings,
            "expected_address": ghidra.expected_address,
            "resolved_address": ghidra.resolved_address,
            "address_warning": ghidra.address_warning,
            "error": ghidra.error
        },
        "commands": {
            "callgraph": f'./bin/ghidra-callgraph "{result.function_name}"',
            "instruction_diff": f'./bin/objdiff-cli diff -p . "{result.function_name}" -f json --include-instructions'
        }
    }

    # Add m2c results if present
    if result.m2c is not None:
        output["m2c"] = {
            "decompilation": result.m2c.decompilation or None,
            "error": result.m2c.error
        }

    # Add struct offset results if present
    if result.struct_offsets is not None:
        offsets = result.struct_offsets
        output["struct_offsets"] = {
            "mismatches": [
                {
                    "index": m.index,
                    "opcode": m.opcode,
                    "target_offset": m.target_offset,
                    "base_offset": m.base_offset,
                    "base_register": m.base_register,
                    "target_field": m.target_field,
                    "base_field": m.base_field,
                    "target_class": m.target_class,
                    "base_class": m.base_class
                }
                for m in offsets.mismatches
            ],
            "class_hints": offsets.class_hints,
            "suggestions": offsets.suggestions,
            "error": offsets.error
        }

    return json.dumps(output, indent=2)


# =============================================================================
# Main Entry Point
# =============================================================================

def analyze_function(
    function_name: str,
    project_dir: str = PROJECT_DIR,
    include_xrefs: bool = True,
    output_format: str = "markdown",
    unit: Optional[str] = None,
    quiet: bool = False,
    verbose: bool = False,
    incremental: bool = True,
    include_m2c: bool = True,
    m2c_context: Optional[str] = None,
    resolve_offsets: bool = False,
    m2c_decomp_mode: bool = True,
    m2c_noise_level: Optional[str] = None,
    m2c_show_offsets: bool = False
) -> str:
    """
    Analyze a function combining objdiff and Ghidra data.

    Args:
        function_name: Function name (demangled like "Game::Poll" or mangled)
        project_dir: Project directory for objdiff
        include_xrefs: Whether to include cross-references
        output_format: "markdown" or "json"
        unit: Unit name within project (to disambiguate duplicate symbols)
        quiet: Suppress Ghidra connection warnings
        verbose: Print debug info for symbol resolution
        incremental: Use incremental build (default). Set to False for full build.
        include_m2c: Whether to run m2c decompilation
        m2c_context: Optional path to m2c context file for type info

    Returns:
        Formatted analysis string
    """
    vprint(f"Starting analysis for: {function_name}", verbose, prefix="analyze")

    # Initialize results
    objdiff_result = ObjdiffResult()
    ghidra_result = GhidraResult()

    # Run objdiff
    objdiff_result = run_objdiff(function_name, project_dir, unit=unit, incremental=incremental)

    vprint(f"objdiff symbol: {objdiff_result.symbol}", verbose, prefix="analyze")
    vprint(f"objdiff demangled: {objdiff_result.demangled}", verbose, prefix="analyze")

    # Determine the best name to use for Ghidra
    # Priority: mangled symbol > user-provided name > extract from demangled
    # Ghidra uses mangled names like "?PollShuttle@Game@@AAAMXZ"
    ghidra_name = function_name
    if objdiff_result.symbol:
        # Use the mangled symbol directly - this is what Ghidra has
        ghidra_name = objdiff_result.symbol
    elif objdiff_result.demangled:
        # Extract just the function name without visibility modifiers
        # "public: void __cdecl Game::Poll(void)" -> "Game::Poll"
        # This is used as a fallback for search
        demangled = objdiff_result.demangled
        if "::" in demangled:
            import re
            match = re.search(r'(\w+(?:::\w+)+)\s*\(', demangled)
            if match:
                ghidra_name = match.group(1)

    vprint(f"Selected ghidra_name: {ghidra_name}", verbose, prefix="analyze")

    # Connect to Ghidra MCP
    mcp = MCPClient(quiet=quiet)
    mcp_connected = mcp.initialize()
    vprint(f"MCP connection: {'connected' if mcp_connected else 'failed'}", verbose, prefix="analyze")

    if mcp_connected:
        try:
            # Resolve the function name/address in Ghidra
            resolved_name, resolved_addr = resolve_function_for_ghidra(mcp, ghidra_name, verbose=verbose)
            ghidra_result.function_name = resolved_name
            ghidra_result.resolved_address = resolved_addr

            # Look up expected address from symbols.txt using the resolved mangled name
            # This must happen AFTER Ghidra resolution because objdiff may return
            # a demangled name (like "RndMat::Copy") but symbols.txt uses mangled names
            # (like "?Copy@RndMat@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z")
            expected_addr = None
            # Try the resolved name first (should be mangled from Ghidra search)
            if resolved_name:
                expected_addr = lookup_symbol_address(resolved_name, project_dir)
                vprint(f"Expected address lookup with resolved_name '{resolved_name}': {expected_addr}", verbose, prefix="analyze")
            # Fall back to objdiff symbol if resolved name didn't work
            if not expected_addr and objdiff_result.symbol:
                expected_addr = lookup_symbol_address(objdiff_result.symbol, project_dir)
                vprint(f"Expected address lookup with objdiff symbol '{objdiff_result.symbol}': {expected_addr}", verbose, prefix="analyze")
            ghidra_result.expected_address = expected_addr

            # Verify address matches expected address from symbols.txt
            decompile_target = resolved_addr if resolved_addr else resolved_name
            # Fallback to expected address from symbols.txt when Ghidra resolution found no address
            if not resolved_addr and expected_addr:
                decompile_target = expected_addr
            if expected_addr and resolved_addr:
                # Normalize for comparison (lowercase, strip 0x)
                exp_norm = expected_addr.lower().replace("0x", "")
                res_norm = resolved_addr.lower().replace("0x", "")

                if exp_norm != res_norm:
                    ghidra_result.address_warning = (
                        f"Address mismatch: Ghidra resolved to {resolved_addr} "
                        f"but expected {expected_addr} from symbols.txt"
                    )
                    # Use expected address for decompilation
                    decompile_target = expected_addr

            # Get decompilation - prefer address since Ghidra may not recognize mangled names
            decompile, ghidra_func_name, addr = mcp.decompile_function(decompile_target)
            ghidra_result.decompilation = decompile
            if addr:
                ghidra_result.address = addr

            # Detect potential stub/wrong function
            if ghidra_result.decompilation and objdiff_result.base_size:
                stub_warnings = detect_potential_stub(
                    ghidra_result.decompilation,
                    objdiff_result.base_size
                )
                ghidra_result.warnings = stub_warnings

            # Get cross-references if requested
            if include_xrefs:
                # Use Ghidra's function name (like "Function_828654D0") for xrefs
                # This avoids ambiguity issues with addresses
                xref_target = ghidra_func_name if ghidra_func_name else resolved_name
                callers, callees = mcp.list_cross_references(xref_target)
                ghidra_result.callers = callers
                ghidra_result.callees = callees

        except ValueError as e:
            ghidra_result.error = str(e)
    else:
        ghidra_result.error = "Could not connect to Ghidra MCP server"

    # Run m2c decompilation if requested
    m2c_result = None
    if include_m2c:
        vprint("Running m2c decompilation...", verbose, prefix="analyze")
        m2c_result = run_m2c(
            function_name=function_name,
            project_dir=project_dir,
            unit=unit,
            context_file=m2c_context,
            decomp_mode=m2c_decomp_mode,
            noise_level=m2c_noise_level,
            show_offsets=m2c_show_offsets
        )
        if m2c_result.error:
            vprint(f"m2c error: {m2c_result.error}", verbose, prefix="analyze")
        else:
            vprint("m2c decompilation complete", verbose, prefix="analyze")

    # Resolve struct offsets if we have instructions
    struct_offset_result = None
    if resolve_offsets and objdiff_result.raw.get('instructions'):
        vprint("Resolving struct offsets...", verbose, prefix="analyze")

        # Extract offset mismatches from instructions
        mismatches = extract_offset_mismatches(objdiff_result.raw)

        if mismatches:
            # Get class hints from demangled name
            class_hints = []
            if objdiff_result.demangled:
                class_hints = extract_class_hints_from_demangled(objdiff_result.demangled)
            elif function_name and '::' in function_name:
                # Extract class from function name like "Game::Poll"
                class_hints = [function_name.split('::')[0]]

            vprint(f"Found {len(mismatches)} offset mismatch(es), class hints: {class_hints}", verbose, prefix="analyze")

            # Resolve using struct_db
            struct_offset_result = resolve_struct_offsets(mismatches, class_hints, project_dir)

            if struct_offset_result.error:
                vprint(f"Struct resolution error: {struct_offset_result.error}", verbose, prefix="analyze")
            else:
                vprint(f"Resolved {len([m for m in mismatches if m.target_field or m.base_field])} field name(s)", verbose, prefix="analyze")
        else:
            vprint("No offset mismatches found", verbose, prefix="analyze")

    # Build combined result
    result = AnalysisResult(
        function_name=function_name,
        objdiff=objdiff_result,
        ghidra=ghidra_result,
        m2c=m2c_result,
        struct_offsets=struct_offset_result
    )

    # Format output
    if output_format == "json":
        return format_json(result)
    else:
        return format_markdown(result)


def main():
    parser = argparse.ArgumentParser(
        description="Analyze a function with combined objdiff + Ghidra data",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s "Game::Poll"                                        # Markdown output (includes m2c with --decomp by default)
  %(prog)s "Game::Poll" -f json                                # JSON output
  %(prog)s "?Poll@Game@@QAAXXZ"                                # Use mangled name
  %(prog)s "Game::Poll" --no-xrefs                             # Skip cross-references
  %(prog)s "Game::Poll" --no-m2c                               # Skip m2c decompilation
  %(prog)s "Object::Load" -u default/system/game               # Disambiguate with unit name
  %(prog)s "Character::Poll" -u default/system/char/Character # Incremental build for single file (~1s)
  %(prog)s "Character::Poll" -u default/system/char/Character --full-build  # Full build (~88s)
  %(prog)s "Game::Poll" -q                                     # Suppress Ghidra warnings
  %(prog)s "Game::Poll" -v                                     # Debug symbol resolution
  %(prog)s --list-units                                        # List all available units
  %(prog)s --list-units character                              # Filter units by name pattern
  %(prog)s "Game::Poll" --m2c-context types.ctx                # Use m2c with type context
  %(prog)s "Game::Poll" --m2c-noise=minimal                    # Comment out artifact declarations
  %(prog)s "Game::Poll" --m2c-no-decomp                        # Disable decomp-friendly output
        """
    )

    parser.add_argument(
        "function",
        nargs="?",
        default=None,
        help="Function name (demangled like 'Game::Poll' or mangled)"
    )
    parser.add_argument(
        "-f", "--format",
        choices=["markdown", "json"],
        default="markdown",
        help="Output format (default: markdown)"
    )
    parser.add_argument(
        "-p", "--project",
        default=PROJECT_DIR,
        help=f"Project directory (default: {PROJECT_DIR})"
    )
    parser.add_argument(
        "--no-xrefs",
        action="store_true",
        help="Skip cross-reference lookup"
    )
    parser.add_argument(
        "-u", "--unit",
        help="Unit name within project (to disambiguate duplicate symbols)"
    )
    parser.add_argument(
        "-q", "--quiet",
        action="store_true",
        help="Suppress Ghidra connection warnings"
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Print debug info for symbol resolution"
    )
    parser.add_argument(
        "-o", "--output",
        help="Output file (default: stdout)"
    )
    parser.add_argument(
        "--list-units",
        nargs="?",
        const="",
        metavar="FILTER",
        help="List all available units (optionally filter by name pattern)"
    )
    parser.add_argument(
        "--full-build",
        action="store_true",
        help="Use full build instead of incremental (slower but verifies entire project)"
    )
    parser.add_argument(
        "--no-m2c",
        action="store_true",
        help="Skip m2c decompilation (included by default)"
    )
    parser.add_argument(
        "--m2c-context",
        metavar="FILE",
        help="Path to m2c context file for type info"
    )
    parser.add_argument(
        "--m2c-no-decomp",
        action="store_true",
        help="Disable m2c --decomp flag (artifact annotations + offsets)"
    )
    parser.add_argument(
        "--m2c-noise",
        choices=["full", "low", "minimal"],
        help="m2c noise level: full (default), low (artifact comments), minimal (commented out)"
    )
    parser.add_argument(
        "--m2c-show-offsets",
        action="store_true",
        help="Show struct field offsets in m2c output"
    )

    parser.add_argument("--resolve-offsets", action="store_true", help="Resolve struct field names for offset mismatches")
    args = parser.parse_args()

    # Handle --list-units early
    if args.list_units is not None:
        try:
            units = list_units(filter_pattern=args.list_units if args.list_units else None, project_dir=args.project)
            for unit in units:
                print(unit)
            sys.exit(0)
        except ValueError as e:
            print(f"Error: {e}", file=sys.stderr)
            sys.exit(1)

    # Check that function name is provided if not using --list-units
    if args.function is None:
        parser.print_help()
        sys.exit(1)

    # Check service health before attempting analysis (informational only)
    ghidra_available = True
    if not args.quiet:
        vprint("Checking Ghidra service health...", args.verbose, "health")
        if not check_service_health(timeout=3):
            ghidra_available = False
            vprint("Warning: Ghidra service not available, analysis will be objdiff-only", args.verbose, "health")
            vprint("To enable Ghidra: ./tools/ghidra/pyghidra-service.sh start", args.verbose, "health")

    try:
        output = analyze_function(
            function_name=args.function,
            project_dir=args.project,
            include_xrefs=not args.no_xrefs,
            output_format=args.format,
            unit=args.unit,
            quiet=args.quiet,
            verbose=args.verbose,
            incremental=not args.full_build,
            include_m2c=not args.no_m2c,
            m2c_context=args.m2c_context,
            resolve_offsets=args.resolve_offsets,
            m2c_decomp_mode=not args.m2c_no_decomp,
            m2c_noise_level=args.m2c_noise,
            m2c_show_offsets=args.m2c_show_offsets
        )

        if args.output:
            with open(args.output, "w") as f:
                f.write(output)
        else:
            print(output)

    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)


if __name__ == "__main__":
    main()
