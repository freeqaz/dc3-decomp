#!/usr/bin/env python3
"""
MCP Server for DC3 Decomp Orchestrator.

Provides tools for sub-agents to:
- Report task completion results
- Query function database for work targets
- Get previous attempt history
- Lookup RB3 reference implementations
- Run objdiff with smart output handling

Run as: python3 -m scripts.orchestrator.mcp_server --db decomp.db
"""

import argparse
import asyncio
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

# Maximum lines to return inline (larger outputs go to file)
MAX_INLINE_LINES = 500

# MCP protocol imports
try:
    from mcp.server import Server
    from mcp.server.stdio import stdio_server
    from mcp.types import Tool, TextContent
except ImportError:
    print("MCP package not installed. Install with: pip install mcp", file=sys.stderr)
    sys.exit(1)

# Add scripts and project root to path
sys.path.insert(0, str(Path(__file__).parent.parent))
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from orchestrator.database import (
    get_connection,
    get_function_by_symbol,
    query_functions as db_query_functions,
    get_attempts_for_function,
    get_stats,
    record_attempt,
    update_function_status,
    get_file_pair,
    query_file_pairs,
    search_functions_by_name,
)
from orchestrator.rb3_pairing import get_rb3_source_for_unit, find_rb3_file
from orchestrator.rb2_dwarf import RB2DwarfParser, DEFAULT_RB2_DUMP
from tools.struct_db import StructDB
from tools.merged_symbols import MergedSymbolLookup


# Itanium ABI mangled name pattern: MethodName__<N><ClassName><params>
_ITANIUM_PATTERN = re.compile(r'^(.+?)__(\d+)(\w+)')


def _demangle_itanium_to_qualified(symbol: str) -> str | None:
    """Demangle an Itanium-style mangled name to ClassName::MethodName.

    Returns None if the symbol is not Itanium-mangled (e.g. MSVC or already demangled).

    Examples:
        PokeStart__12GlitchFinderFPCcUi... → GlitchFinder::PokeStart
        __ct__12GlitchFinderFv             → GlitchFinder::GlitchFinder
        __dt__12GlitchFinderFv             → GlitchFinder::~GlitchFinder
        SomeFunc__Fv (free function)       → None
    """
    # Skip MSVC mangled or already-demangled names
    if symbol.startswith("?") or "::" in symbol:
        return None

    m = _ITANIUM_PATTERN.match(symbol)
    if not m:
        return None

    method, class_len_str, rest = m.group(1), m.group(2), m.group(3)
    class_len = int(class_len_str)

    if class_len > len(rest) or class_len == 0:
        return None

    class_name = rest[:class_len]

    # Handle ctor/dtor special names
    if method == "__ct":
        method = class_name
    elif method == "__dt":
        method = f"~{class_name}"

    return f"{class_name}::{method}"


class DecompMCPServer:
    """MCP Server providing decomp orchestration tools."""

    def __init__(self, db_path: str, rb3_path: str | None = None, record_attempts: bool = True):
        self.db_path = db_path
        self.rb3_path = rb3_path or os.path.expanduser("~/code/milohax/rb3/src")
        self.record_attempts = record_attempts
        # Determine project root from script location (more reliable than cwd)
        self.project_root = Path(__file__).resolve().parent.parent.parent
        self.server = Server("decomp")
        # RB2 DWARF parser (lazy loaded)
        self._rb2_parser: RB2DwarfParser | None = None
        self._setup_tools()

    def _get_rb2_parser(self) -> RB2DwarfParser:
        """Get or create RB2 DWARF parser."""
        if self._rb2_parser is None:
            self._rb2_parser = RB2DwarfParser(DEFAULT_RB2_DUMP)
        return self._rb2_parser

    def _setup_tools(self):
        """Register all MCP tools."""

        @self.server.list_tools()
        async def list_tools() -> list[Tool]:
            return [
                Tool(
                    name="report_result",
                    description="Report task completion. Call when done working on a function.",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "symbol": {
                                "type": "string",
                                "description": "Function symbol (mangled name) being reported on",
                            },
                            "status": {
                                "type": "string",
                                "enum": ["complete", "at_limit", "stuck", "error"],
                                "description": "Exit status: complete (100%), at_limit (unfixable), stuck (need help), error",
                            },
                            "percent": {
                                "type": "number",
                                "description": "Final match percentage (0-100)",
                            },
                            "notes": {
                                "type": "string",
                                "description": "Summary of what was tried",
                            },
                            "model": {
                                "type": "string",
                                "description": "Model that worked on this (e.g., 'sonnet', 'haiku', 'opus')",
                            },
                        },
                        "required": ["symbol", "status", "percent", "notes"],
                    },
                ),
                Tool(
                    name="query_functions",
                    description="Query the function database for potential work targets.",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "min_percent": {
                                "type": "number",
                                "description": "Minimum match percentage (default: 0)",
                            },
                            "max_percent": {
                                "type": "number",
                                "description": "Maximum match percentage (default: 100)",
                            },
                            "unit_pattern": {
                                "type": "string",
                                "description": "Glob pattern for unit path (e.g., 'src/system/char/*')",
                            },
                            "limit": {
                                "type": "integer",
                                "description": "Max results to return (default: 20)",
                            },
                            "status": {
                                "type": "string",
                                "description": "Filter by function status: 'workable' (default, excludes complete/at_limit), 'all' (no filtering), 'complete' (only complete), 'at_limit' (only at_limit)",
                                "enum": ["workable", "all", "complete", "at_limit"],
                            },
                        },
                    },
                ),
                Tool(
                    name="get_attempts",
                    description="Get previous attempt history for a function to learn from.",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "symbol": {
                                "type": "string",
                                "description": "Function symbol (mangled name)",
                            },
                        },
                        "required": ["symbol"],
                    },
                ),
                Tool(
                    name="lookup_rb3",
                    description="Search RB3 decomp for similar implementation (shared Milo engine).",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "symbol": {
                                "type": "string",
                                "description": "Function symbol or method name to search for",
                            },
                        },
                        "required": ["symbol"],
                    },
                ),
                Tool(
                    name="run_objdiff",
                    description="Build and diff a function, returning match% and verdict. Handles large output automatically.\n\n⚠️ CRITICAL: Pass project_dir parameter when in a worktree or your edits won't be tested! Without project_dir, the tool tests the main repo code instead of your changes, making edits invisible to match%.",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "symbol": {
                                "type": "string",
                                "description": "Function symbol (mangled name)",
                            },
                            "full_build": {
                                "type": "boolean",
                                "description": "Force full rebuild (slower but more accurate). Default: false (incremental)",
                            },
                            "project_dir": {
                                "type": "string",
                                "description": "Project directory to build from. Pass your worktree directory here to test your changes.",
                            },
                            "context": {
                                "type": "integer",
                                "description": "Show N instructions of context before/after each mismatch (like grep -C). Default: 3.",
                            },
                            "concise": {
                                "type": "boolean",
                                "description": "Concise output: match%, compact summary, patterns, verdict headline. Default: true. Set false for full instruction table + auto-diagnosis.",
                            },
                        },
                        "required": ["symbol", "project_dir"],
                    },
                ),
                Tool(
                    name="run_analyze_function",
                    description="Run enriched function analysis combining objdiff with struct offset resolution. Returns detailed diff with field names for offset mismatches. Detects unfixable patterns (struct offsets, merged calls, etc.).\n\n⚠️ CRITICAL: Pass project_dir parameter when in a worktree or your edits won't be tested!",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "symbol": {
                                "type": "string",
                                "description": "Function symbol (mangled or demangled name)",
                            },
                            "resolve_offsets": {
                                "type": "boolean",
                                "description": "Resolve struct field names for offset mismatches (default: true)",
                            },
                            "output_format": {
                                "type": "string",
                                "enum": ["markdown", "json"],
                                "description": "Output format (default: markdown)",
                            },
                            "project_dir": {
                                "type": "string",
                                "description": "Project directory to build from. Pass your worktree directory here to test your changes.",
                            },
                        },
                        "required": ["symbol", "project_dir"],
                    },
                ),
                Tool(
                    name="run_diff_inspect",
                    description="Deep analysis of WHY a function doesn't match. Provides root cause diagnosis, cluster analysis, register swap detection, offset analysis, replace categorization, and before/after comparison. Use after run_objdiff when you need deeper insight into mismatches.\n\n⚠️ CRITICAL: Pass project_dir parameter when in a worktree!",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "symbol": {
                                "type": "string",
                                "description": "Function symbol (mangled or demangled name)",
                            },
                            "mode": {
                                "type": "string",
                                "enum": ["diagnose", "clusters", "regswaps", "offsets", "replaces", "compare", "save_baseline", "mismatches"],
                                "description": "Analysis mode: diagnose (root cause), clusters (contiguous insert/delete groups), regswaps (register swap pairs), offsets (offset shift histogram), replaces (categorize noise vs real), compare (delta vs baseline), save_baseline (save current state), mismatches (list all mismatched instructions with target/base details)",
                            },
                            "project_dir": {
                                "type": "string",
                                "description": "Project directory to build from. Pass your worktree directory here.",
                            },
                            "baseline_json": {
                                "type": "string",
                                "description": "Optional: path to baseline JSON file for compare mode. If omitted, auto-finds baseline saved by orchestrator.",
                            },
                        },
                        "required": ["symbol", "mode", "project_dir"],
                    },
                ),
                Tool(
                    name="lookup_struct_offset",
                    description="Look up which struct field is at a given offset. Use when objdiff shows offset mismatches like 'stw r10, 0x118(r11)' vs 'stw r10, 0xf4(r11)' to identify which field is being accessed.",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "class_name": {
                                "type": "string",
                                "description": "Class or struct name (e.g., 'Game', 'RndTransformable')",
                            },
                            "offset": {
                                "type": "string",
                                "description": "Offset to look up (hex with 0x prefix or decimal, e.g., '0x48' or '72')",
                            },
                        },
                        "required": ["class_name", "offset"],
                    },
                ),
                Tool(
                    name="struct_info",
                    description="Get detailed information about a class/struct including its members, parent classes, and inheritance chain.",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "class_name": {
                                "type": "string",
                                "description": "Class or struct name to look up",
                            },
                        },
                        "required": ["class_name"],
                    },
                ),
                Tool(
                    name="get_rb3_pair",
                    description="Get RB3 file pairing info for a DC3 unit. Returns compatibility score, function overlap, and optionally the RB3 source code. Use this to leverage RB3 reference implementations for shared Milo engine code.",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "dc3_unit": {
                                "type": "string",
                                "description": "DC3 unit path (e.g., 'default/system/char/CharBones' or 'CharBones')",
                            },
                            "include_source": {
                                "type": "boolean",
                                "description": "Include full RB3 source code in response (default: false for summary only)",
                            },
                        },
                        "required": ["dc3_unit"],
                    },
                ),
                Tool(
                    name="get_rb2_class_info",
                    description="Get class layout from RB2 DWARF dump. Returns member offsets, sizes, and inheritance. Useful for understanding struct layouts when DC3 headers lack offset information.",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "class_name": {
                                "type": "string",
                                "description": "Class or struct name (e.g., 'Character', 'CharCollide', 'RndDir')",
                            },
                            "offset": {
                                "type": "string",
                                "description": "Optional: look up what member is at this offset (hex or decimal)",
                            },
                        },
                        "required": ["class_name"],
                    },
                ),
                Tool(
                    name="lookup_merged_symbol",
                    description="Look up symbols at a merged address. When objdiff shows LINKER_MERGED pattern with 'merged_82331360', use this to find which actual symbols are at that address. ICF (Identical COMDAT Folding) merges functions with identical machine code.",
                    inputSchema={
                        "type": "object",
                        "properties": {
                            "address": {
                                "type": "string",
                                "description": "Address to look up (e.g., '82331360' or 'merged_82331360')",
                            },
                        },
                        "required": ["address"],
                    },
                ),
            ]

        @self.server.call_tool()
        async def call_tool(name: str, arguments: dict) -> list[TextContent]:
            if name == "report_result":
                return await self._report_result(arguments)
            elif name == "query_functions":
                return await self._query_functions(arguments)
            elif name == "get_attempts":
                return await self._get_attempts(arguments)
            elif name == "lookup_rb3":
                return await self._lookup_rb3(arguments)
            elif name == "run_objdiff":
                return await self._run_objdiff(arguments)
            elif name == "run_analyze_function":
                return await self._run_analyze_function(arguments)
            elif name == "run_diff_inspect":
                return await self._run_diff_inspect(arguments)
            elif name == "lookup_struct_offset":
                return await self._lookup_struct_offset(arguments)
            elif name == "struct_info":
                return await self._struct_info(arguments)
            elif name == "get_rb3_pair":
                return await self._get_rb3_pair(arguments)
            elif name == "get_rb2_class_info":
                return await self._get_rb2_class_info(arguments)
            elif name == "lookup_merged_symbol":
                return await self._lookup_merged_symbol(arguments)
            else:
                return [TextContent(type="text", text=f"Unknown tool: {name}")]

    async def _report_result(self, args: dict) -> list[TextContent]:
        """Handle report_result tool call."""
        symbol = args.get("symbol", "")
        status = args.get("status", "unknown")
        percent = args.get("percent", 0)
        notes = args.get("notes", "")
        model = args.get("model", "unknown")

        # Store attempt in database if symbol is provided.
        # When record_attempts is False (orchestrator mode), skip DB writes —
        # the orchestrator records attempts itself after the agent returns,
        # preventing phantom attempts from crashes.
        db_stored = False
        if symbol and self.record_attempts:
            func = get_function_by_symbol(symbol, db_path=self.db_path)
            if func:
                start_percent = func.get("current_percent") or 0

                # Determine verdict from status
                verdict = None
                if status == "at_limit":
                    verdict = "AT_LIMIT"
                elif status == "complete":
                    verdict = "COMPLETE"

                # Record the attempt
                record_attempt(
                    function_id=func["id"],
                    session_id="mcp_direct",
                    model=model,
                    start_percent=start_percent,
                    end_percent=percent,
                    exit_status=status,
                    verdict=verdict,
                    notes=notes,
                    db_path=self.db_path,
                )

                # Update function status
                update_function_status(
                    function_id=func["id"],
                    current_percent=percent,
                    verdict=verdict,
                    db_path=self.db_path,
                )
                db_stored = True

        # Format response that orchestrator can parse
        result = {
            "_decomp_exit": True,  # Signal to orchestrator: agent is done
            "status": status,
            "percent": percent,
            "notes": notes,
        }

        status_msg = f"Result recorded: {status} at {percent}%"
        if db_stored:
            status_msg += " (stored to database)"
        elif symbol:
            status_msg += f" (function not found in database: {symbol})"

        return [
            TextContent(
                type="text",
                text=f"{status_msg}\n\n```json\n{json.dumps(result, indent=2)}\n```",
            )
        ]

    async def _query_functions(self, args: dict) -> list[TextContent]:
        """Handle query_functions tool call."""
        min_percent = args.get("min_percent", 0)
        max_percent = args.get("max_percent", 100)
        pattern = args.get("unit_pattern", "*")
        limit = args.get("limit", 20)
        status = args.get("status", "workable")

        # Map status filter to database query params
        if status == "all":
            exclude_complete = False
            exclude_at_limit = False
            verdict_filter = None
        elif status == "complete":
            exclude_complete = False
            exclude_at_limit = True
            verdict_filter = "COMPLETE"
        elif status == "at_limit":
            exclude_complete = True
            exclude_at_limit = False
            verdict_filter = "AT_LIMIT"
        else:  # "workable" (default)
            exclude_complete = True
            exclude_at_limit = False
            verdict_filter = None

        results = db_query_functions(
            pattern=pattern,
            min_percent=min_percent,
            max_percent=max_percent,
            exclude_complete=exclude_complete,
            exclude_at_limit=exclude_at_limit,
            verdict_filter=verdict_filter,
            limit=limit,
            db_path=self.db_path,
        )

        # When filtering by unit, check if there are hidden functions
        hidden_note = ""
        if status != "all" and pattern != "*":
            all_results = db_query_functions(
                pattern=pattern,
                min_percent=0,
                max_percent=100,
                exclude_complete=False,
                exclude_at_limit=False,
                verdict_filter=None,
                limit=9999,
                max_attempts=None,
                db_path=self.db_path,
            )
            total = len(all_results)
            if total > len(results):
                hidden_note = (
                    f"\n---\n"
                    f"Note: Showing {len(results)} of {total} functions "
                    f"(filtered by status='{status}'). "
                    f"Use status='all' to see all functions in this unit."
                )

        if not results:
            msg = "No functions found matching criteria."
            if hidden_note:
                msg += hidden_note
            return [TextContent(type="text", text=msg)]

        # Format results
        output = f"Found {len(results)} functions:\n\n"
        for func in results:
            pct = func.get("current_percent")
            pct_str = f"{pct:.1f}%" if pct is not None else "unimplemented"
            verdict = func.get("verdict")
            verdict_str = f" | Verdict: {verdict}" if verdict else ""
            output += f"- `{func['symbol']}` ({func.get('demangled', 'N/A')})\n"
            output += f"  Unit: {func.get('unit', 'unknown')} | Match: {pct_str}{verdict_str}\n"

        if hidden_note:
            output += hidden_note

        return [TextContent(type="text", text=output)]

    async def _get_attempts(self, args: dict) -> list[TextContent]:
        """Handle get_attempts tool call."""
        symbol = args.get("symbol", "")

        func = get_function_by_symbol(symbol, db_path=self.db_path)
        if not func:
            return [TextContent(type="text", text=f"Function not found: {symbol}")]

        attempts = get_attempts_for_function(func["id"], limit=10, db_path=self.db_path)

        if not attempts:
            return [TextContent(type="text", text="No previous attempts for this function.")]

        output = f"## Previous Attempts for {symbol}\n\n"
        output += f"**Current Status:** {func.get('current_percent', 'unknown')}% match, Verdict: {func.get('verdict', 'unknown')}\n\n"

        for i, attempt in enumerate(attempts, 1):
            # Use 'or 0' instead of default param - .get() returns None if key exists with None value
            start_pct = attempt.get('start_percent') or 0
            end_pct = attempt.get('end_percent') or 0
            change = end_pct - start_pct
            change_str = f"+{change:.1f}%" if change >= 0 else f"{change:.1f}%"

            # Status interpretation for clarity
            status = attempt.get('exit_status', 'unknown')
            status_emoji = "✓" if status == "complete" else "✗" if status == "error" else "⊘"

            output += f"### Attempt {i}: {status_emoji} {status.upper()}\n"
            output += f"- **Model:** {attempt.get('model', 'unknown')}\n"
            output += f"- **Match:** {start_pct:.1f}% → {end_pct:.1f}% ({change_str})\n"
            if attempt.get("verdict"):
                output += f"- **Verdict:** {attempt['verdict']}\n"
            if attempt.get("iterations"):
                output += f"- **Iterations:** {attempt['iterations']} tool calls\n"
            if attempt.get("notes"):
                # Limit notes length in output
                notes = attempt['notes']
                if len(notes) > 200:
                    notes = notes[:200] + "..."
                output += f"- **Notes:** {notes}\n"
            output += "\n"

        output += "---\n\n**Strategy Tips:**\n"
        output += "- Review what previous attempts tried (notes field)\n"
        output += "- Avoid repeating the same changes\n"
        output += "- Look for patterns in what worked vs what didn't\n"
        output += "- If match% stopped improving, function may be at limit\n"

        return [TextContent(type="text", text=output)]

    # ========================================================================
    # Enrichment pipeline helpers for run_objdiff
    # ========================================================================

    # Regex for parsing PPC memory operands: rX, 0xOFF(rY) or rX, OFF(rY)
    _MEM_ARG_RE = re.compile(r'r(\d+),\s*(-?0x[0-9a-fA-F]+|-?\d+)\(r(\d+)\)')
    # Regex for parsing PPC immediate operands: rX, rY, IMM
    _SHIFT_ARG_RE = re.compile(r'r(\d+),\s*r(\d+),\s*(\d+)')
    # Memory opcodes that access struct fields
    _MEM_OPCODES = frozenset([
        'lwz', 'stw', 'lfs', 'stfs', 'lhz', 'sth', 'lbz', 'stb', 'lfd', 'stfd',
        'lwzu', 'stwu', 'lfsu', 'stfsu', 'lha', 'lhau',
    ])
    # Shift/rotate opcodes
    _SHIFT_OPCODES = frozenset(['slwi', 'srwi', 'slw', 'srw', 'rlwinm'])

    @staticmethod
    def _extract_class_from_demangled(demangled: str) -> str | None:
        """Extract class name from demangled symbol like 'ClassName::Method(...)'."""
        m = re.search(r'(\w+)::\w+\s*\(', demangled)
        return m.group(1) if m else None

    @staticmethod
    def _parse_hex_or_int(s: str) -> int:
        """Parse a string as hex (0x...) or decimal integer."""
        s = s.strip()
        if s.startswith(('0x', '0X', '-0x', '-0X')):
            return int(s, 16)
        return int(s)

    def _resolve_offset_mismatches(self, data: dict) -> list[dict]:
        """
        Scan instruction diffs for memory offset mismatches and resolve
        them to struct field names using StructDB.

        Returns list of offset mismatch records with field names.
        """
        instructions = data.get("instructions") or data.get("mismatch_instructions") or []
        demangled = data.get("demangled", "")
        class_name = self._extract_class_from_demangled(demangled)

        if not class_name and not instructions:
            return []

        struct_db_path = self.project_root / "struct_db.sqlite"
        if not struct_db_path.exists():
            return []

        mismatches = []
        try:
            with StructDB(str(struct_db_path)) as db:
                for instr in instructions:
                    match_type = instr.get("match_type")
                    if match_type != "diff_arg":
                        continue

                    target = instr.get("target", {})
                    base = instr.get("base", {})
                    opcode_t = target.get("opcode", "")
                    opcode_b = base.get("opcode", "")

                    # Both must be memory opcodes
                    if opcode_t not in self._MEM_OPCODES or opcode_b not in self._MEM_OPCODES:
                        continue

                    args_t = target.get("args", "")
                    args_b = base.get("args", "")
                    m_t = self._MEM_ARG_RE.search(args_t)
                    m_b = self._MEM_ARG_RE.search(args_b)

                    if not m_t or not m_b:
                        continue

                    off_t = self._parse_hex_or_int(m_t.group(2))
                    off_b = self._parse_hex_or_int(m_b.group(2))

                    if off_t == off_b:
                        continue  # Same offset, different register — not a struct mismatch

                    # Resolve field names
                    target_field = None
                    base_field = None

                    if class_name:
                        result_t = db.lookup(class_name, off_t)
                        result_b = db.lookup(class_name, off_b)
                        if result_t:
                            target_field = f"{result_t[0]}::{result_t[1]} ({result_t[2]})"
                        if result_b:
                            base_field = f"{result_b[0]}::{result_b[1]} ({result_b[2]})"

                    entry = {
                        "index": instr.get("index"),
                        "opcode": opcode_t,
                        "target_offset": f"0x{off_t:x}",
                        "base_offset": f"0x{off_b:x}",
                    }
                    if target_field:
                        entry["target_field"] = target_field
                    if base_field:
                        entry["base_field"] = base_field
                    if target_field and base_field:
                        entry["fix_hint"] = (
                            f"Source accesses '{base_field.split('::')[-1].split(' (')[0]}' "
                            f"but target accesses '{target_field.split('::')[-1].split(' (')[0]}' — wrong field?"
                        )

                    mismatches.append(entry)
        except Exception:
            pass  # Don't let struct DB errors break objdiff

        return mismatches

    @staticmethod
    def _detect_stack_copy_ref(data: dict) -> list[dict]:
        """
        Detect pass-by-reference via stack copy pattern.

        Target copies a member to stack then passes stack address,
        while base passes the member address directly.
        """
        instructions = data.get("instructions") or data.get("mismatch_instructions") or []
        if not instructions:
            return []

        patterns_found = []

        # Build index of instructions by position for context lookups
        instr_by_idx = {instr.get("index"): instr for instr in instructions}

        # Look for sequences: delete stw/stfs to r1 (stack store) near diff_arg addi with r1
        for instr in instructions:
            match_type = instr.get("match_type")
            target = instr.get("target", {})
            base = instr.get("base", {})

            if match_type != "diff_arg":
                continue

            # Check if this is an addi where target uses r1 (stack) but base doesn't
            if target.get("opcode") != "addi" or base.get("opcode") != "addi":
                continue

            t_args = target.get("args", "")
            b_args = base.get("args", "")

            # Target: addi rN, r1, stackoff (passing stack address)
            # Base: addi rN, rX, offset (passing member address directly)
            if ", r1," in t_args and ", r1," not in b_args:
                idx = instr.get("index", -1)
                # Look for nearby stack stores (delete instructions)
                nearby_stores = []
                for check_idx in range(max(0, idx - 5), idx):
                    nearby = instr_by_idx.get(check_idx)
                    if nearby and nearby.get("match_type") == "delete":
                        t = nearby.get("target", {})
                        op = t.get("opcode", "")
                        args = t.get("args", "")
                        if op in ("stw", "stfs", "sth", "stb", "stfd") and "r1" in args:
                            nearby_stores.append(check_idx)

                if nearby_stores:
                    patterns_found.append({
                        "pattern": "STACK_COPY_REF",
                        "confidence": "high",
                        "fixability": "likely_fixable",
                        "instruction_indices": nearby_stores + [idx],
                        "fix_hint": (
                            "Target copies member to stack before passing as const-ref. "
                            "Fix: assign to a local variable before the call."
                        ),
                    })

        return patterns_found

    @staticmethod
    def _annotate_shift_semantics(data: dict) -> list[dict]:
        """
        Annotate shift instructions with multiplication/division equivalents.

        slwi r10, r11, 3 → "×8", srwi r10, r11, 2 → "÷4"
        """
        instructions = data.get("instructions") or data.get("mismatch_instructions") or []
        annotations = []
        shift_re = re.compile(r'r(\d+),\s*r(\d+),\s*(\d+)')

        for instr in instructions:
            if instr.get("match_type") != "diff_arg":
                continue

            target = instr.get("target", {})
            base = instr.get("base", {})
            t_op = target.get("opcode", "")
            b_op = base.get("opcode", "")

            # At least one side must be a shift opcode
            if t_op not in ('slwi', 'srwi', 'slw', 'srw', 'rlwinm') and \
               b_op not in ('slwi', 'srwi', 'slw', 'srw', 'rlwinm'):
                continue

            def shift_meaning(opcode: str, args: str) -> str | None:
                m = shift_re.search(args)
                if not m:
                    return None
                amount = int(m.group(3))
                if opcode in ('slwi', 'slw'):
                    return f"×{1 << amount}"
                elif opcode in ('srwi', 'srw'):
                    return f"÷{1 << amount}"
                elif opcode == 'rlwinm':
                    return f"rotate/mask by {amount}"
                return None

            t_meaning = shift_meaning(t_op, target.get("args", ""))
            b_meaning = shift_meaning(b_op, base.get("args", ""))

            if t_meaning or b_meaning:
                ann = {
                    "index": instr.get("index"),
                    "target": {"opcode": t_op, "args": target.get("args", "")},
                    "base": {"opcode": b_op, "args": base.get("args", "")},
                    "match_type": "diff_arg",
                }
                parts = []
                if t_meaning:
                    parts.append(f"target: {t_meaning}")
                if b_meaning:
                    parts.append(f"base: {b_meaning}")
                ann["annotation"] = ", ".join(parts)
                annotations.append(ann)

        return annotations

    @staticmethod
    def _refine_register_swap_confidence(data: dict) -> None:
        """
        Refine REGISTER_SWAP pattern confidence based on register types.

        If all swapped registers are floating-point (f0-f31), downgrade
        fixability to unlikely_fixable since FP register allocation is
        rarely controllable from source.

        Mutates data["analysis"]["patterns"] in-place.
        """
        analysis = data.get("analysis", {})
        patterns = analysis.get("patterns", [])

        for pattern in patterns:
            if pattern.get("pattern") != "REGISTER_SWAP":
                continue

            details = pattern.get("details", {})
            swaps = details.get("swaps", [])
            if not swaps:
                continue

            fp_re = re.compile(r'^f\d+$')
            fp_count = 0
            int_count = 0

            for swap in swaps:
                t_reg = swap.get("target_reg", "")
                b_reg = swap.get("base_reg", "")
                if fp_re.match(t_reg) or fp_re.match(b_reg):
                    fp_count += 1
                else:
                    int_count += 1

            # Annotate register type
            if fp_count > 0 and int_count == 0:
                details["register_type"] = "float"
                # Check if there are other fixable patterns
                other_fixable = any(
                    p.get("pattern") != "REGISTER_SWAP"
                    and p.get("fixability") in ("fixable", "likely_fixable", "maybe_fixable")
                    for p in patterns
                )
                if not other_fixable:
                    pattern["fixability"] = "unlikely_fixable"
                    pattern["fix_hint"] = (
                        "FP register allocation — rarely fixable from source. "
                        "Consider accepting as at_limit."
                    )
            elif fp_count > 0 and int_count > 0:
                details["register_type"] = "mixed"
            else:
                details["register_type"] = "integer"

    def _inline_rb3_method_source(self, data: dict) -> dict | None:
        """
        Look up and return RB3 reference source for the method.

        Returns a dict with rb3_reference info including method_source,
        or None if not available.
        """
        demangled = data.get("demangled", "")
        if not demangled:
            return None

        # Extract class and method name
        class_name = self._extract_class_from_demangled(demangled)
        if not class_name:
            return None

        # Extract method name
        m = re.search(r'(\w+)::(\w+)\s*\(', demangled)
        if not m:
            return None
        method_name = m.group(2)

        # Find the unit for this symbol to look up the RB3 pair
        symbol = data.get("symbol", "")
        source_file = data.get("source_file", "")

        # Try to find RB3 file via unit
        rb3_file_path = None
        if source_file:
            # Convert source_file path to unit for file_pair lookup
            unit = source_file.replace("src/", "default/").rsplit(".", 1)[0]
            pair = get_file_pair(unit, db_path=self.db_path)
            if pair and pair.get("rb3_file"):
                rb3_file_path = Path(pair["rb3_file"])

        # Fallback: search by class name
        if not rb3_file_path:
            rb3_file_path = find_rb3_file(class_name, Path(self.rb3_path))

        if not rb3_file_path or not rb3_file_path.exists():
            return None

        try:
            source = rb3_file_path.read_text(errors="replace")
        except Exception:
            return None

        # Extract the specific method source
        method_source = self._extract_method_source(source, class_name, method_name)
        if not method_source:
            return {"available": True, "rb3_file": str(rb3_file_path), "method_found": False}

        # Cap at 60 lines
        lines = method_source.split("\n")
        if len(lines) > 60:
            method_source = "\n".join(lines[:60]) + "\n// ... (truncated)"

        return {
            "available": True,
            "rb3_file": str(rb3_file_path),
            "method_found": True,
            "method_source": method_source,
        }

    @staticmethod
    def _extract_method_source(source: str, class_name: str, method_name: str) -> str | None:
        """
        Extract a method's source code from a C++ file.

        Finds the method definition and extracts through its closing brace.
        """
        # Look for ClassName::MethodName pattern
        # Handle various return types before the class::method
        pattern = re.compile(
            rf'^[^\n]*\b{re.escape(class_name)}::{re.escape(method_name)}\s*\(',
            re.MULTILINE
        )
        match = pattern.search(source)
        if not match:
            return None

        start = match.start()

        # Find opening brace
        brace_pos = source.find('{', match.end())
        if brace_pos == -1:
            return None

        # Count braces to find the matching closing brace
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
            return None

        return source[start:pos].strip()

    def _suggest_similar_symbols(self, symbol: str) -> list[str]:
        """
        When a symbol is not found, suggest similar symbols from the database.

        Returns formatted suggestion strings.
        """
        # Extract search term from mangled symbol
        search_term = symbol
        if "@" in symbol:
            # MSVC mangled: ?MethodName@ClassName@@...
            parts = symbol.split("@")
            method = parts[0].lstrip("?")
            if len(parts) >= 2:
                cls = parts[1]
                search_term = f"{cls}::{method}"
        elif "__" in symbol:
            demangled = _demangle_itanium_to_qualified(symbol)
            if demangled:
                search_term = demangled
        elif "::" in symbol:
            search_term = symbol

        try:
            results = search_functions_by_name(search_term, limit=5, db_path=self.db_path)
            if not results:
                # Try just the method name
                method_only = search_term.split("::")[-1] if "::" in search_term else search_term
                results = search_functions_by_name(method_only, limit=5, db_path=self.db_path)

            suggestions = []
            for r in results:
                pct = r.get("current_percent")
                pct_str = f" ({pct:.1f}%)" if pct is not None else ""
                suggestions.append(f"`{r['symbol']}`{pct_str}")
            return suggestions
        except Exception:
            return []

    def _enrich_objdiff_data(self, data: dict) -> dict:
        """
        Run the full enrichment pipeline on parsed objdiff JSON data.

        Adds: offset_mismatches, shift_annotations, stack_copy_ref patterns,
        RB3 method source, and refined register swap confidence.

        Mutates and returns data.
        """
        # 1. Auto-resolve offset mismatches
        offset_mismatches = self._resolve_offset_mismatches(data)
        if offset_mismatches:
            data["offset_mismatches"] = offset_mismatches

        # 2. Detect stack copy ref pattern
        stack_copy_patterns = self._detect_stack_copy_ref(data)
        if stack_copy_patterns:
            # Add to existing analysis patterns if present
            analysis = data.setdefault("analysis", {})
            patterns = analysis.setdefault("patterns", [])
            patterns.extend(stack_copy_patterns)

        # 3. Annotate shift semantics
        shift_annotations = self._annotate_shift_semantics(data)
        if shift_annotations:
            data["shift_annotations"] = shift_annotations

        # 4. Refine REGISTER_SWAP confidence for FP registers
        self._refine_register_swap_confidence(data)

        # 5. Inline RB3 method source
        rb3_ref = self._inline_rb3_method_source(data)
        if rb3_ref:
            data["rb3_reference"] = rb3_ref

        return data

    def _format_enrichment_sections(self, data: dict, skip_rb3: bool = False) -> str:
        """
        Format enrichment annotations as markdown sections to append to
        the built-in objdiff markdown output.

        Covers: offset mismatches, shift semantics, detected patterns,
        and RB3 reference source.
        """
        lines = []

        # Offset mismatches with resolved field names
        offset_mismatches = data.get("offset_mismatches", [])
        if offset_mismatches:
            lines.append("")
            lines.append("## Offset Mismatches (resolved)")
            lines.append("")
            for om in offset_mismatches:
                idx = om.get("index", "?")
                opcode = om.get("opcode", "?")
                t_off = om.get("target_offset", "?")
                b_off = om.get("base_offset", "?")
                t_field = om.get("target_field", "")
                b_field = om.get("base_field", "")
                hint = om.get("fix_hint", "")
                line = f"- [{idx}] `{opcode}`: target {t_off}"
                if t_field:
                    line += f" ({t_field})"
                line += f" vs base {b_off}"
                if b_field:
                    line += f" ({b_field})"
                if hint:
                    line += f" -- {hint}"
                lines.append(line)

        # Shift annotations
        shift_annotations = data.get("shift_annotations", [])
        if shift_annotations:
            lines.append("")
            lines.append("## Shift Semantics")
            lines.append("")
            for sa in shift_annotations:
                idx = sa.get("index", "?")
                meaning = sa.get("meaning", "?")
                lines.append(f"- [{idx}] {meaning}")

        # Analysis patterns (stack_copy_ref, etc.)
        patterns = data.get("analysis", {}).get("patterns", [])
        if patterns:
            lines.append("")
            lines.append("## Detected Patterns")
            lines.append("")
            for pat in patterns:
                ptype = pat.get("type", "unknown")
                desc = pat.get("description", "")
                fixable = pat.get("fixable", "")
                line = f"- **{ptype}**"
                if desc:
                    line += f": {desc}"
                if fixable:
                    line += f" (fixable: {fixable})"
                lines.append(line)

        # Mismatch preview (adaptive limit based on match %)
        instrs = data.get("instructions", [])
        if instrs:
            mismatches = [ins for ins in instrs if ins.get("match_type") != "equal"]
            if mismatches:
                match_pct = data.get("fuzzy_match_percent", 0)
                total = len(instrs)

                # Adaptive limit
                if match_pct >= 98:
                    limit = len(mismatches)  # show ALL for near-matches
                elif match_pct >= 90:
                    limit = 15
                else:
                    limit = 8

                shown = mismatches[:limit]
                truncated = len(mismatches) > limit

                from diff_inspect import fmt_instr as _fmt_instr, diff_annotation as _diff_annotation

                if truncated:
                    lines.append("")
                    lines.append(f"## Key Mismatches ({len(shown)} of {len(mismatches)} shown)")
                else:
                    lines.append("")
                    lines.append(f"## Mismatches ({len(mismatches)} of {total} instructions)")

                lines.append("")
                for ins in shown:
                    idx = ins.get("index", "?")
                    mt = ins.get("match_type", "?")
                    t = ins.get("target")
                    b = ins.get("base")
                    t_op = t.get("opcode", "?") if t else "---"
                    b_op = b.get("opcode", "?") if b else "---"

                    if mt == "diff_arg":
                        ann = _diff_annotation(ins).strip()
                        lines.append(f"- [{idx}] {mt}: `{t_op}` {ann}")
                    elif mt == "replace":
                        t_str = _fmt_instr(t).strip()
                        b_str = _fmt_instr(b).strip()
                        lines.append(f"- [{idx}] {mt}: `{t_str}` vs `{b_str}`")
                    elif mt in ("insert", "delete"):
                        side = b if mt == "insert" else t
                        s_str = _fmt_instr(side).strip() if side else "---"
                        lines.append(f"- [{idx}] {mt}: `{s_str}`")
                    elif mt == "diff_op":
                        lines.append(f"- [{idx}] {mt}: `{t_op}` vs `{b_op}`")
                    else:
                        lines.append(f"- [{idx}] {mt}")

                if truncated:
                    lines.append("")
                    lines.append('*(Use `run_diff_inspect mode: "mismatches"` for full list)*')

        # RB3 reference (skip in concise mode)
        rb3_ref = data.get("rb3_reference", {})
        if rb3_ref and rb3_ref.get("available") and not skip_rb3:
            lines.append("")
            rb3_file = rb3_ref.get("rb3_file", "?")
            lines.append(f"## RB3 Reference ({rb3_file})")
            if rb3_ref.get("method_found") and rb3_ref.get("method_source"):
                lines.append("")
                lines.append("```cpp")
                lines.append(rb3_ref["method_source"])
                lines.append("```")

        return "\n".join(lines)

    async def _run_objdiff(self, args: dict) -> list[TextContent]:
        """
        Handle run_objdiff tool call.

        Runs objdiff-cli with smart output handling:
        - If output < 500 lines: return inline
        - If output >= 500 lines: write to file and return path + instructions
        """
        symbol = args.get("symbol", "")
        full_build = args.get("full_build", False)
        project_dir_arg = args.get("project_dir", None)
        context = args.get("context", 3)
        concise = args.get("concise", True)

        if not symbol:
            return [TextContent(type="text", text="Error: No symbol provided.")]

        if symbol.startswith("merged_"):
            return [TextContent(type="text", text=f"Error: {symbol} is a linker ICF artifact (merged symbol), not a real function. "
                                "Use lookup_merged_symbol to see what real symbols share this address.")]

        # Auto-demangle Itanium-style names to qualified names objdiff can resolve
        demangled = _demangle_itanium_to_qualified(symbol)
        if demangled is not None:
            symbol = demangled

        # Determine which project directory to use
        # - If agent passes project_dir (e.g., its worktree), use that
        # - Otherwise use main repo (for orchestrator calls or main repo usage)
        if project_dir_arg:
            project_dir = Path(project_dir_arg)
            if not project_dir.exists():
                return [TextContent(
                    type="text",
                    text=f"Error: project_dir does not exist: {project_dir}"
                )]
        else:
            project_dir = self.project_root

        # Find objdiff-cli in the determined project directory
        objdiff_cli = project_dir / "bin" / "objdiff-cli"

        if not objdiff_cli.exists():
            return [TextContent(
                type="text",
                text=f"Error: objdiff-cli not found at {objdiff_cli}"
            )]

        # Common args for both runs
        base_args = [
            str(objdiff_cli),
            "diff",
            "-p", str(project_dir),
            symbol,
            "--verdict",
        ]

        build_flag = ["--build"]
        if full_build:
            build_flag.append("--full-build")

        # --include-instructions only for JSON run (enrichment/m2c pipeline).
        # The markdown run uses --verdict alone which already contains the
        # analysis, patterns, and suggestions without the bulky instruction table.
        json_extra = ["--include-instructions"]
        if context:
            json_extra.extend(["-C", str(context)])

        def _filter_stderr(stderr: str) -> str:
            """Filter ninja progress lines from stderr, return error lines."""
            if not stderr:
                return ""
            lines = stderr.strip().splitlines()
            error_lines = [
                line for line in lines
                if not re.match(r'^\s*\[\d+/\d+\]\s', line)
            ]
            return "\n".join(error_lines)

        try:
            # 1) JSON run (with build) - for enrichment data
            json_cmd = base_args + json_extra + build_flag + ["-f", "json"]
            json_result = subprocess.run(
                json_cmd,
                capture_output=True,
                text=True,
                timeout=300,
                cwd=str(project_dir),
            )

            # Check for errors in JSON output
            json_output = json_result.stdout
            stderr_text = _filter_stderr(json_result.stderr)

            if "Symbol not found" in json_output or "Failed" in json_output:
                suggestions = self._suggest_similar_symbols(symbol)
                error_msg = json_output.strip()
                if stderr_text:
                    error_msg += f"\n\n[stderr]\n{stderr_text}"
                if suggestions:
                    error_msg += "\n\nDid you mean:\n" + "\n".join(
                        f"  - {s}" for s in suggestions
                    )
                return [TextContent(type="text", text=error_msg)]

            # Strip ninja build preamble (e.g. "ninja: no work to do.\n")
            # that --build writes to stdout before the JSON
            _json_start = json_output.find("{")
            if _json_start > 0:
                json_output = json_output[_json_start:]

            # 2) Markdown run (no build, already built) - for display
            # Explicit -f markdown avoids TUI fallback when no TTY is present
            md_cmd = list(base_args) + ["-f", "markdown"]
            if concise:
                md_cmd.append("--concise")
            md_result = subprocess.run(
                md_cmd,
                capture_output=True,
                text=True,
                timeout=60,
                cwd=str(project_dir),
            )
            output = md_result.stdout

            # 3) Enrich from JSON and append enrichment sections
            enrichment = ""
            try:
                data = json.loads(json_output)
                data = self._enrich_objdiff_data(data)
                enrichment = self._format_enrichment_sections(data, skip_rb3=concise)
            except (json.JSONDecodeError, KeyError):
                pass

            if enrichment:
                output += "\n" + enrichment

            # 4) Auto-diagnose when not concise and match < 95%
            if not concise:
                try:
                    parsed = json.loads(json_output)
                    match_pct = parsed.get("fuzzy_match_percent", 100)
                    if match_pct < 95:
                        # Write JSON to temp file for diff_inspect
                        tmp_json = Path(tempfile.mktemp(suffix=".json", dir="/tmp/claude"))
                        tmp_json.parent.mkdir(parents=True, exist_ok=True)
                        with open(tmp_json, "w") as f:
                            f.write(json_output)

                        diff_inspect_script = self.project_root / "scripts" / "diff_inspect.py"
                        if diff_inspect_script.exists():
                            diag_result = subprocess.run(
                                [sys.executable, str(diff_inspect_script), str(tmp_json), "--diagnose"],
                                capture_output=True, text=True,
                                timeout=30,
                            )
                            if diag_result.returncode == 0 and diag_result.stdout.strip():
                                output += "\n\n## Auto-Diagnosis (diff_inspect)\n\n" + diag_result.stdout.strip()

                        # Clean up
                        try:
                            tmp_json.unlink()
                        except OSError:
                            pass
                except (json.JSONDecodeError, KeyError, subprocess.TimeoutExpired, Exception):
                    pass  # Best-effort, never break run_objdiff

            if stderr_text:
                output += f"\n\n[stderr]\n{stderr_text}"

            # Count lines
            lines = output.split("\n")
            line_count = len(lines)

            if line_count < MAX_INLINE_LINES:
                return [TextContent(type="text", text=output)]
            else:
                # Write to file in the project directory being tested
                analysis_dir = project_dir / "function_analysis"
                analysis_dir.mkdir(exist_ok=True, parents=True)

                safe_symbol = symbol.replace("?", "_Q_").replace("@", "_A_").replace("<", "_L_").replace(">", "_R_")
                output_file = analysis_dir / f"objdiff_{safe_symbol}.md"

                with open(output_file, "w") as f:
                    f.write(output)

                # Extract summary from JSON for the inline preview
                summary = ""
                try:
                    data = json.loads(json_output)
                    match_pct = data.get("fuzzy_match_percent", "?")
                    verdict = data.get("verdict", {}).get("classification", "UNKNOWN")
                    summary = f"**Match: {match_pct}% | Verdict: {verdict}**\n\n"
                except (json.JSONDecodeError, KeyError):
                    pass

                return [TextContent(
                    type="text",
                    text=f"""{summary}Output is large ({line_count} lines). Written to file.

**File:** `{output_file.relative_to(project_dir)}`

**When reading this file:**
- Never read the entire file at once.
- First estimate size via bash (`wc -l` / `wc -c`).
- If > 500 lines or > 200KB, read in chunks of 200 lines.
- After each chunk, produce <= 8 bullets summarizing what you learned, then continue.
- Keep each tool result compact; do not emit large verbatim excerpts.

**Next steps:**
1. If verdict is AT_LIMIT: Report with mcp__orchestrator__report_result
2. If LIKELY_FIXABLE/MAYBE_FIXABLE: Make edits and run this tool again
"""
                )]

        except subprocess.TimeoutExpired:
            return [TextContent(type="text", text="Error: objdiff timed out after 5 minutes.")]
        except Exception as e:
            return [TextContent(type="text", text=f"Error running objdiff: {e}")]

    async def _run_analyze_function(self, args: dict) -> list[TextContent]:
        """
        Handle run_analyze_function tool call.

        Runs analyze_function.py which combines objdiff output with struct
        offset resolution from the header database.
        """
        symbol = args.get("symbol", "")
        resolve_offsets = args.get("resolve_offsets", True)
        output_format = args.get("output_format", "markdown")
        project_dir_arg = args.get("project_dir", None)

        if not symbol:
            return [TextContent(type="text", text="Error: No symbol provided.")]

        if symbol.startswith("merged_"):
            return [TextContent(type="text", text=f"Error: {symbol} is a linker ICF artifact (merged symbol), not a real function. "
                                "Use lookup_merged_symbol to see what real symbols share this address.")]

        # Auto-demangle Itanium-style names to qualified names
        demangled = _demangle_itanium_to_qualified(symbol)
        if demangled is not None:
            symbol = demangled

        # Determine which project directory to use
        # - If agent passes project_dir (e.g., its worktree), use that
        # - Otherwise use main repo (for orchestrator calls or main repo usage)
        if project_dir_arg:
            project_dir = Path(project_dir_arg)
            if not project_dir.exists():
                return [TextContent(
                    type="text",
                    text=f"Error: project_dir does not exist: {project_dir}"
                )]
        else:
            project_dir = self.project_root

        # Find analyze-function script in the determined project directory
        analyze_script = project_dir / "bin" / "analyze-function"

        if not analyze_script.exists():
            return [TextContent(
                type="text",
                text=f"Error: analyze-function not found at {analyze_script}"
            )]

        # Build command
        cmd = [str(analyze_script), symbol]

        if resolve_offsets:
            cmd.append("--resolve-offsets")

        if output_format == "json":
            cmd.extend(["-f", "json"])
        else:
            cmd.extend(["-f", "markdown"])

        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=300,  # 5 minute timeout
                cwd=str(project_dir),
            )

            output = result.stdout
            if result.stderr:
                # Filter stderr to only keep errors/warnings, not ninja progress lines
                stderr_lines = result.stderr.strip().splitlines()
                error_lines = [
                    line for line in stderr_lines
                    if not re.match(r'^\s*\[\d+/\d+\]\s', line)
                ]
                if error_lines:
                    output += f"\n\n[stderr]\n" + "\n".join(error_lines)

            if result.returncode != 0:
                return [TextContent(
                    type="text",
                    text=f"Error (exit code {result.returncode}):\n{output}"
                )]

            # Count lines
            lines = output.split("\n")
            line_count = len(lines)

            if line_count < MAX_INLINE_LINES:
                # Return inline
                if output_format == "json":
                    return [TextContent(type="text", text=f"```json\n{output}\n```")]
                else:
                    return [TextContent(type="text", text=output)]
            else:
                # Write to file in the project directory being tested
                analysis_dir = project_dir / "function_analysis"
                analysis_dir.mkdir(exist_ok=True, parents=True)

                safe_symbol = symbol.replace("?", "_Q_").replace("@", "_A_").replace("<", "_L_").replace(">", "_R_")
                ext = "json" if output_format == "json" else "md"
                output_file = analysis_dir / f"analyze_{safe_symbol}.{ext}"

                with open(output_file, "w") as f:
                    f.write(output)

                return [TextContent(
                    type="text",
                    text=f"""Output is large ({line_count} lines). Written to file.

**File:** `{output_file.relative_to(project_dir)}`

Use the Read tool to view: `Read {output_file.relative_to(project_dir)}`
"""
                )]

        except subprocess.TimeoutExpired:
            return [TextContent(type="text", text="Error: analyze-function timed out after 5 minutes.")]
        except Exception as e:
            return [TextContent(type="text", text=f"Error running analyze-function: {e}")]

    async def _run_diff_inspect(self, args: dict) -> list[TextContent]:
        """
        Handle run_diff_inspect tool call.

        Runs diff_inspect.py analysis modes, save_baseline, or compare workflow.
        """
        symbol = args.get("symbol", "")
        mode = args.get("mode", "")
        project_dir_arg = args.get("project_dir", None)
        baseline_json = args.get("baseline_json", None)

        if not symbol:
            return [TextContent(type="text", text="Error: No symbol provided.")]
        if not mode:
            return [TextContent(type="text", text="Error: No mode provided.")]

        valid_modes = {"diagnose", "clusters", "regswaps", "offsets", "replaces", "compare", "save_baseline", "mismatches"}
        if mode not in valid_modes:
            return [TextContent(type="text", text=f"Error: Invalid mode '{mode}'. Valid: {', '.join(sorted(valid_modes))}")]

        # Auto-demangle Itanium-style names
        demangled = _demangle_itanium_to_qualified(symbol)
        if demangled is not None:
            symbol = demangled

        # Require project_dir — no silent fallback to main repo
        if not project_dir_arg:
            return [TextContent(type="text", text="Error: project_dir is required. Pass your worktree directory so builds test your changes.")]
        project_dir = Path(project_dir_arg)
        if not project_dir.exists():
            return [TextContent(type="text", text=f"Error: project_dir does not exist: {project_dir}")]

        # Safe symbol for filenames
        safe_symbol = symbol.replace("?", "_Q_").replace("@", "_A_").replace("<", "_L_").replace(">", "_R_")

        # diff_inspect.py is always in the main repo
        diff_inspect_script = self.project_root / "scripts" / "diff_inspect.py"
        if not diff_inspect_script.exists():
            return [TextContent(type="text", text=f"Error: diff_inspect.py not found at {diff_inspect_script}")]

        # objdiff-cli is always in the main repo bin/
        objdiff_cli = self.project_root / "bin" / "objdiff-cli"

        try:
            # ── save_baseline mode ──
            if mode == "save_baseline":
                if not objdiff_cli.exists():
                    return [TextContent(type="text", text=f"Error: objdiff-cli not found at {objdiff_cli}")]

                # Run objdiff to produce JSON
                cmd = [
                    str(objdiff_cli), "diff",
                    "-p", str(project_dir),
                    symbol,
                    "--include-instructions", "--build", "--incremental",
                    "-f", "json",
                ]
                result = subprocess.run(
                    cmd, capture_output=True, text=True,
                    timeout=300, cwd=str(project_dir),
                )
                if result.returncode != 0:
                    return [TextContent(type="text", text=f"Error running objdiff: {result.stderr or result.stdout}")]

                # Save to baseline path
                analysis_dir = project_dir / "function_analysis"
                analysis_dir.mkdir(exist_ok=True, parents=True)
                baseline_file = analysis_dir / f"baseline_{safe_symbol}.json"
                with open(baseline_file, "w") as f:
                    f.write(result.stdout)

                return [TextContent(type="text", text=f"Baseline saved: `{baseline_file}`")]

            # ── compare mode ──
            elif mode == "compare":
                # Find baseline
                if baseline_json:
                    baseline_path = Path(baseline_json)
                else:
                    baseline_path = project_dir / "function_analysis" / f"baseline_{safe_symbol}.json"

                if not baseline_path.exists():
                    return [TextContent(type="text", text=f"Error: No baseline found at `{baseline_path}`.\n"
                                        "Use `save_baseline` mode first, or pass `baseline_json` parameter.")]

                # Run fresh objdiff to get current JSON
                if not objdiff_cli.exists():
                    return [TextContent(type="text", text=f"Error: objdiff-cli not found at {objdiff_cli}")]

                cmd = [
                    str(objdiff_cli), "diff",
                    "-p", str(project_dir),
                    symbol,
                    "--include-instructions", "--build", "--incremental",
                    "-f", "json",
                ]
                result = subprocess.run(
                    cmd, capture_output=True, text=True,
                    timeout=300, cwd=str(project_dir),
                )
                if result.returncode != 0:
                    return [TextContent(type="text", text=f"Error running objdiff: {result.stderr or result.stdout}")]

                # Write current JSON to temp file
                current_file = Path(tempfile.mktemp(suffix=".json", dir="/tmp/claude"))
                current_file.parent.mkdir(parents=True, exist_ok=True)
                with open(current_file, "w") as f:
                    f.write(result.stdout)

                # Run diff_inspect --compare
                compare_cmd = [
                    sys.executable, str(diff_inspect_script),
                    "--compare", str(baseline_path), str(current_file),
                ]
                compare_result = subprocess.run(
                    compare_cmd, capture_output=True, text=True,
                    timeout=60,
                )

                # Clean up temp file
                try:
                    current_file.unlink()
                except OSError:
                    pass

                output = compare_result.stdout
                if compare_result.stderr:
                    output += f"\n[stderr] {compare_result.stderr.strip()}"
                if compare_result.returncode != 0:
                    return [TextContent(type="text", text=f"Error in compare:\n{output}")]

                return [TextContent(type="text", text=output)]

            # ── mismatches mode (compact table of non-matching instructions) ──
            elif mode == "mismatches":
                if not objdiff_cli.exists():
                    return [TextContent(type="text", text=f"Error: objdiff-cli not found at {objdiff_cli}")]

                # Run objdiff to get JSON with instructions
                cmd = [
                    str(objdiff_cli), "diff",
                    "-p", str(project_dir),
                    symbol,
                    "--include-instructions", "--build", "--incremental",
                    "-f", "json",
                ]
                result = subprocess.run(
                    cmd, capture_output=True, text=True,
                    timeout=300, cwd=str(project_dir),
                )

                stderr_text = result.stderr.strip() if result.stderr else ""
                if result.returncode != 0:
                    return [TextContent(type="text", text=f"Error running objdiff (exit {result.returncode}):\n{result.stdout}\n{stderr_text}")]

                stdout_text = result.stdout
                if "Symbol not found" in stdout_text or "Failed" in stdout_text:
                    error_msg = stdout_text.strip()
                    suggestions = self._suggest_similar_symbols(symbol)
                    if suggestions:
                        error_msg += "\n\nDid you mean:\n" + "\n".join(
                            f"  - {s}" for s in suggestions
                        )
                    return [TextContent(type="text", text=error_msg)]

                # Strip ninja build preamble (e.g. "ninja: no work to do.\n")
                # that --build writes to stdout before the JSON
                json_start = stdout_text.find("{")
                if json_start < 0:
                    return [TextContent(type="text", text=f"No JSON in objdiff output.\n\nstdout: {stdout_text[:500]}\nstderr: {stderr_text[:500]}")]

                try:
                    data = json.loads(stdout_text[json_start:])
                except json.JSONDecodeError as e:
                    return [TextContent(type="text", text=f"Error parsing objdiff JSON: {e}\n\nstdout: {stdout_text[:500]}\nstderr: {stderr_text[:500]}")]

                instrs = data.get("instructions", [])
                if not instrs:
                    return [TextContent(type="text", text="No instructions found in objdiff output.")]

                # Filter non-equal instructions
                mismatches = [ins for ins in instrs if ins.get("match_type") != "equal"]
                total = len(instrs)

                if not mismatches:
                    match_pct = data.get("fuzzy_match_percent", 100)
                    return [TextContent(type="text", text=f"No mismatches — all {total} instructions match ({match_pct}%).")]

                # Cap at 30
                MAX_MISMATCHES = 30
                truncated = len(mismatches) > MAX_MISMATCHES
                shown = mismatches[:MAX_MISMATCHES]

                # Format as compact markdown table
                from diff_inspect import fmt_instr as _fmt_instr, diff_annotation as _diff_annotation

                header = f"## Mismatched Instructions ({len(mismatches)} of {total} total)\n"
                if truncated:
                    header += f"*Showing {MAX_MISMATCHES} of {len(mismatches)} mismatches*\n"

                lines = [header]
                lines.append("| Idx | Type | Target | Base | Note |")
                lines.append("|-----|------|--------|------|------|")

                for ins in shown:
                    idx = ins.get("index", "?")
                    mt = ins.get("match_type", "?")
                    t = ins.get("target")
                    b = ins.get("base")
                    t_str = _fmt_instr(t).strip()
                    b_str = _fmt_instr(b).strip()
                    note = _diff_annotation(ins).strip() if mt == "diff_arg" else ""
                    lines.append(f"| {idx} | {mt} | `{t_str}` | `{b_str}` | {note} |")

                if truncated:
                    lines.append(f"\n*{len(mismatches) - MAX_MISMATCHES} more mismatches not shown.*")

                output = "\n".join(lines)
                return [TextContent(type="text", text=output)]

            # ── analysis modes (diagnose/clusters/regswaps/offsets/replaces) ──
            else:
                cmd = [
                    sys.executable, str(diff_inspect_script),
                    "--symbol", symbol,
                    f"--{mode}",
                    "--project-dir", str(project_dir),
                ]
                result = subprocess.run(
                    cmd, capture_output=True, text=True,
                    timeout=300,
                )

                output = result.stdout
                if result.stderr:
                    # Filter ninja progress lines
                    stderr_lines = result.stderr.strip().splitlines()
                    error_lines = [
                        line for line in stderr_lines
                        if not re.match(r'^\s*\[\d+/\d+\]\s', line)
                        and not line.startswith("Running objdiff for:")
                        and not line.startswith("Output:")
                    ]
                    if error_lines:
                        output += f"\n\n[stderr]\n" + "\n".join(error_lines)

                if result.returncode != 0:
                    return [TextContent(type="text", text=f"Error (exit {result.returncode}):\n{output}")]

                # Handle large output
                lines = output.split("\n")
                if len(lines) < MAX_INLINE_LINES:
                    return [TextContent(type="text", text=output)]
                else:
                    analysis_dir = project_dir / "function_analysis"
                    analysis_dir.mkdir(exist_ok=True, parents=True)
                    output_file = analysis_dir / f"diff_inspect_{mode}_{safe_symbol}.txt"
                    with open(output_file, "w") as f:
                        f.write(output)
                    return [TextContent(type="text", text=f"Output is large ({len(lines)} lines). Written to file.\n\n"
                                        f"**File:** `{output_file.relative_to(project_dir)}`")]

        except subprocess.TimeoutExpired:
            return [TextContent(type="text", text=f"Error: diff_inspect timed out (mode={mode}).")]
        except Exception as e:
            return [TextContent(type="text", text=f"Error running diff_inspect: {e}")]

    async def _lookup_rb3(self, args: dict) -> list[TextContent]:
        """Handle lookup_rb3 tool call."""
        symbol = args.get("symbol", "")

        if not symbol:
            return [TextContent(type="text", text="No symbol provided.")]

        # Extract method name from mangled symbol
        # e.g., "?Poll@CharMirror@@UAEXXZ" → "Poll"
        search_term = symbol
        if "::" in symbol:
            # Try to get method name after ::
            parts = symbol.split("::")
            if parts:
                search_term = parts[-1].split("@")[0]
        elif "@" in symbol:
            # Mangled: ?MethodName@ClassName@@...
            parts = symbol.split("@")
            if len(parts) > 1:
                search_term = parts[0].lstrip("?")

        rb3_path = Path(self.rb3_path)
        if not rb3_path.exists():
            return [
                TextContent(
                    type="text",
                    text=f"RB3 source path not found: {rb3_path}",
                )
            ]

        try:
            result = subprocess.run(
                ["grep", "-rn", "--include=*.cpp", "--include=*.h", search_term, str(rb3_path)],
                capture_output=True,
                text=True,
                timeout=30,
            )

            if not result.stdout.strip():
                return [TextContent(type="text", text=f"No matches found in RB3 for: {search_term}")]

            # Limit output
            lines = result.stdout.strip().split("\n")[:20]
            output = f"RB3 matches for '{search_term}' ({len(lines)} shown):\n\n"
            output += "\n".join(lines)

            if len(result.stdout.strip().split("\n")) > 20:
                output += f"\n\n... and more matches"

            return [TextContent(type="text", text=output)]

        except subprocess.TimeoutExpired:
            return [TextContent(type="text", text="RB3 search timed out.")]
        except Exception as e:
            return [TextContent(type="text", text=f"RB3 search error: {e}")]

    async def _lookup_struct_offset(self, args: dict) -> list[TextContent]:
        """Handle lookup_struct_offset tool call."""
        class_name = args.get("class_name", "")
        offset_str = args.get("offset", "")

        if not class_name or not offset_str:
            return [TextContent(type="text", text="Error: class_name and offset are required.")]

        # Parse offset (hex or decimal)
        try:
            if offset_str.startswith("0x"):
                offset = int(offset_str, 16)
            else:
                offset = int(offset_str)
        except ValueError:
            return [TextContent(type="text", text=f"Error: Invalid offset format: {offset_str}")]

        # Use struct database from project root
        struct_db_path = self.project_root / "struct_db.sqlite"
        if not struct_db_path.exists():
            return [TextContent(
                type="text",
                text=f"Struct database not found at {struct_db_path}.\n\nBuild it with: ./tools/struct_db.py build src/"
            )]

        try:
            with StructDB(str(struct_db_path)) as db:
                result = db.lookup(class_name, offset)

            if result:
                cls_name, member_name, type_str = result
                return [TextContent(
                    type="text",
                    text=f"**{cls_name}::{member_name}** (`{type_str}`) at offset 0x{offset:x}"
                )]
            else:
                return [TextContent(
                    type="text",
                    text=f"No field found at offset 0x{offset:x} in {class_name} or its parent classes."
                )]
        except Exception as e:
            return [TextContent(type="text", text=f"Error looking up offset: {e}")]

    async def _struct_info(self, args: dict) -> list[TextContent]:
        """Handle struct_info tool call."""
        class_name = args.get("class_name", "")

        if not class_name:
            return [TextContent(type="text", text="Error: class_name is required.")]

        # Use struct database from project root
        struct_db_path = self.project_root / "struct_db.sqlite"
        if not struct_db_path.exists():
            return [TextContent(
                type="text",
                text=f"Struct database not found at {struct_db_path}.\n\nBuild it with: ./tools/struct_db.py build src/"
            )]

        try:
            with StructDB(str(struct_db_path)) as db:
                info = db.get_class_info(class_name)

                if not info:
                    return [TextContent(type="text", text=f"Class not found: {class_name}")]

                # Build output
                keyword = "struct" if info['is_struct'] else "class"
                output = f"## {keyword} {info['name']}\n\n"
                output += f"**File:** `{info['file_path']}`\n\n"

                if info['parents']:
                    output += "**Parents:**\n"
                    for parent, is_virtual in info['parents']:
                        v = " (virtual)" if is_virtual else ""
                        output += f"- {parent}{v}\n"
                    output += "\n"

                # Get full inheritance chain
                chain = db.resolve_inheritance_chain(class_name)
                if chain:
                    output += f"**Full inheritance chain:** {' → '.join(chain)}\n\n"

                if info['members']:
                    output += "**Members:**\n"
                    output += "| Offset | Type | Name |\n"
                    output += "|--------|------|------|\n"
                    for m in info['members']:
                        output += f"| 0x{m['offset']:02x} | `{m['type_str']}` | {m['name']} |\n"

                return [TextContent(type="text", text=output)]

        except Exception as e:
            return [TextContent(type="text", text=f"Error getting struct info: {e}")]

    async def _get_rb3_pair(self, args: dict) -> list[TextContent]:
        """Handle get_rb3_pair tool call."""
        dc3_unit = args.get("dc3_unit", "")
        include_source = args.get("include_source", False)

        if not dc3_unit:
            return [TextContent(type="text", text="Error: dc3_unit is required.")]

        # Normalize unit path - try with and without default/ prefix
        pair = get_file_pair(dc3_unit, db_path=self.db_path)
        if not pair and not dc3_unit.startswith("default/"):
            # Try with common prefixes
            for prefix in ["default/system/", "default/"]:
                pair = get_file_pair(prefix + dc3_unit, db_path=self.db_path)
                if pair:
                    break

        # If not in database, try to find directly
        if not pair:
            rb3_file = find_rb3_file(dc3_unit, Path(self.rb3_path))
            if rb3_file:
                pair = {
                    "dc3_unit": dc3_unit,
                    "rb3_file": str(rb3_file),
                    "compatibility_score": None,
                    "function_overlap": None,
                    "dc3_function_count": None,
                    "rb3_function_count": None,
                }

        if not pair:
            return [TextContent(
                type="text",
                text=f"No RB3 pairing found for: {dc3_unit}\n\nRun `./bin/orchestrate rb3-sync` to build the pairing database."
            )]

        # Build output
        output = f"## RB3 Pairing for {pair.get('dc3_unit', dc3_unit)}\n\n"

        if pair.get("rb3_file"):
            output += f"**RB3 File:** `{pair['rb3_file']}`\n"
        else:
            output += "**RB3 File:** Not found\n"

        if pair.get("compatibility_score") is not None:
            score = pair["compatibility_score"]
            output += f"**Compatibility:** {score:.1%}\n"
            output += f"**Function Overlap:** {pair.get('function_overlap', 0)} functions\n"
            output += f"**DC3 Functions:** {pair.get('dc3_function_count', 0)}\n"
            output += f"**RB3 Functions:** {pair.get('rb3_function_count', 0)}\n"

        # Include source if requested
        if include_source and pair.get("rb3_file"):
            rb3_path = Path(pair["rb3_file"])
            if rb3_path.exists():
                try:
                    source = rb3_path.read_text(errors="replace")
                    # Truncate if too long
                    if len(source) > 50000:
                        source = source[:50000] + "\n\n... (truncated, file too large)"
                    output += f"\n### RB3 Source\n\n```cpp\n{source}\n```"
                except Exception as e:
                    output += f"\n\n*Could not read RB3 source: {e}*"

        return [TextContent(type="text", text=output)]

    async def _get_rb2_class_info(self, args: dict) -> list[TextContent]:
        """Handle get_rb2_class_info tool call."""
        class_name = args.get("class_name", "")
        offset_str = args.get("offset")

        if not class_name:
            return [TextContent(type="text", text="Error: class_name is required.")]

        try:
            parser = self._get_rb2_parser()
        except FileNotFoundError as e:
            return [TextContent(
                type="text",
                text=f"RB2 DWARF dump not found: {e}\n\nExpected at: {DEFAULT_RB2_DUMP}"
            )]

        # If offset specified, look up specific member
        if offset_str:
            try:
                if offset_str.startswith("0x"):
                    offset = int(offset_str, 16)
                else:
                    offset = int(offset_str)

                result = parser.get_member_at_offset(class_name, offset)
                if result:
                    output = f"**{result['class']}::{result['member']}**\n\n"
                    output += f"- Type: `{result['type']}`\n"
                    output += f"- Offset: 0x{result['offset']:x}\n"
                    output += f"- Size: {result['size']} bytes\n"
                    if result.get('sub_offset'):
                        output += f"- Sub-offset within member: +0x{result['sub_offset']:x}\n"
                    return [TextContent(type="text", text=output)]
                else:
                    return [TextContent(
                        type="text",
                        text=f"No member found at offset 0x{offset:x} in {class_name}"
                    )]
            except ValueError:
                return [TextContent(type="text", text=f"Invalid offset format: {offset_str}")]

        # Get full class info
        class_info = parser.get_class(class_name)
        if not class_info:
            # Try searching for similar classes
            similar = parser.search_classes(class_name)[:10]
            if similar:
                return [TextContent(
                    type="text",
                    text=f"Class '{class_name}' not found. Similar classes:\n" +
                         "\n".join(f"- {c}" for c in similar)
                )]
            return [TextContent(type="text", text=f"Class not found in RB2 DWARF dump: {class_name}")]

        # Build output
        keyword = class_info['kind']
        output = f"## {keyword} {class_info['name']} (RB2 DWARF)\n\n"
        output += f"**Total Size:** 0x{class_info['total_size']:x} ({class_info['total_size']} bytes)\n\n"

        if class_info['parents']:
            output += "**Parents:** " + ", ".join(class_info['parents']) + "\n\n"

        # Get inheritance chain
        chain = parser.get_inheritance_chain(class_name)
        if len(chain) > 1:
            output += f"**Inheritance Chain:** {' → '.join(chain)}\n\n"

        if class_info['members']:
            output += "**Members:**\n\n"
            output += "| Offset | Size | Type | Name |\n"
            output += "|--------|------|------|------|\n"
            for m in class_info['members']:
                output += f"| 0x{m['offset']:02x} | 0x{m['size']:x} | `{m['type']}` | {m['name']} |\n"
        else:
            output += "*No members defined*\n"

        return [TextContent(type="text", text=output)]

    async def _lookup_merged_symbol(self, args: dict) -> list[TextContent]:
        """Handle lookup_merged_symbol tool call."""
        address = args.get("address", "")

        if not address:
            return [TextContent(type="text", text="Error: address is required.")]

        # Find linker map file
        map_file = self.project_root / "orig" / "373307D9" / "ham_xbox_r.map"
        if not map_file.exists():
            return [TextContent(
                type="text",
                text=f"Linker map file not found at {map_file}"
            )]

        try:
            lookup = MergedSymbolLookup(map_file)
            symbols = lookup.lookup(address)

            if symbols is None:
                return [TextContent(
                    type="text",
                    text=f"No symbols found at address: {address}"
                )]

            # Normalize address for display
            addr_display = address.upper()
            if addr_display.lower().startswith('merged_'):
                addr_display = addr_display[7:]
            addr_display = addr_display.lstrip('0x')

            # Format output
            if len(symbols) == 1:
                output = f"**Address 0x{addr_display}**: 1 symbol (not merged)\n\n"
            else:
                output = f"**Address 0x{addr_display}**: {len(symbols)} symbols merged by ICF\n\n"

            for i, sym in enumerate(symbols, 1):
                mangled = sym['symbol']
                demangled = lookup.demangle(mangled)
                source = sym.get('source', '')

                src_suffix = f" ({source})" if source else ""
                output += f"{i}. **{demangled}**{src_suffix}\n"
                output += f"   - Mangled: `{mangled}`\n"

            # Add interpretation guidance for common patterns
            if len(symbols) > 1:
                output += "\n---\n"
                output += "**ICF Interpretation:**\n"
                # Check for destructor pattern
                has_scalar = any('??_G' in s['symbol'] for s in symbols)
                has_vector = any('??_E' in s['symbol'] for s in symbols)
                if has_scalar and has_vector:
                    output += "- Contains both scalar (`delete obj`) and vector (`delete[] arr`) deleting destructors\n"
                    output += "- These have identical code, so any call to either resolves to this address\n"
                # Check for template pattern
                template_count = sum(1 for s in symbols if '$' in s['symbol'])
                if template_count > 2:
                    output += f"- Contains {template_count} template instantiations with identical code\n"
                    output += "- The compiler generated the same machine code for different types\n"

            return [TextContent(type="text", text=output)]

        except Exception as e:
            return [TextContent(type="text", text=f"Error looking up merged symbol: {e}")]

    async def run(self):
        """Run the MCP server."""
        async with stdio_server() as (read_stream, write_stream):
            await self.server.run(read_stream, write_stream, self.server.create_initialization_options())


def main():
    parser = argparse.ArgumentParser(description="DC3 Decomp MCP Server")
    parser.add_argument("--db", default="decomp.db", help="Database path")
    parser.add_argument("--rb3", default=None, help="RB3 source path")
    parser.add_argument(
        "--no-record-attempts",
        action="store_true",
        help="Don't record attempts in report_result (orchestrator records them after agent returns)",
    )
    args = parser.parse_args()

    server = DecompMCPServer(
        db_path=args.db,
        rb3_path=args.rb3,
        record_attempts=not args.no_record_attempts,
    )
    asyncio.run(server.run())


if __name__ == "__main__":
    main()
