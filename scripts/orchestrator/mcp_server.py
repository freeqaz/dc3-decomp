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


class DecompMCPServer:
    """MCP Server providing decomp orchestration tools."""

    def __init__(self, db_path: str, rb3_path: str | None = None):
        self.db_path = db_path
        self.rb3_path = rb3_path or os.path.expanduser("~/code/milohax/rb3/src")
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
                                "description": "⚠️ CRITICAL: Project directory to build from. MUST pass your worktree directory here to test your changes! If omitted, defaults to main repo and your edits won't be visible in results.",
                            },
                        },
                        "required": ["symbol"],
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
                                "description": "⚠️ CRITICAL: Project directory to build from. MUST pass your worktree directory here to test your changes! If omitted, defaults to main repo.",
                            },
                        },
                        "required": ["symbol"],
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

        # Store attempt in database if symbol is provided
        db_stored = False
        if symbol:
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

        results = db_query_functions(
            pattern=pattern,
            min_percent=min_percent,
            max_percent=max_percent,
            limit=limit,
            db_path=self.db_path,
        )

        if not results:
            return [TextContent(type="text", text="No functions found matching criteria.")]

        # Format results
        output = f"Found {len(results)} functions:\n\n"
        for func in results:
            pct = func.get("current_percent")
            pct_str = f"{pct:.1f}%" if pct is not None else "unimplemented"
            output += f"- `{func['symbol']}` ({func.get('demangled', 'N/A')})\n"
            output += f"  Unit: {func.get('unit', 'unknown')} | Match: {pct_str}\n"

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

        if not symbol:
            return [TextContent(type="text", text="Error: No symbol provided.")]

        if symbol.startswith("merged_"):
            return [TextContent(type="text", text=f"Error: {symbol} is a linker ICF artifact (merged symbol), not a real function. "
                                "Use lookup_merged_symbol to see what real symbols share this address.")]

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

        # Build command with absolute project path
        cmd = [
            str(objdiff_cli),
            "diff",
            "-p", str(project_dir),
            symbol,
            "--build",
            "--verdict",
            "-f", "json",
        ]

        if full_build:
            cmd.append("--full-build")

        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=300,  # 5 minute timeout
                cwd=str(project_dir),
            )

            # Combine stdout and stderr
            output = result.stdout
            if result.stderr:
                output += f"\n\n[stderr]\n{result.stderr}"

            # Check for symbol-not-found errors and suggest alternatives
            if "Symbol not found" in output or "Failed" in output:
                suggestions = self._suggest_similar_symbols(symbol)
                error_msg = output.strip()
                if suggestions:
                    error_msg += "\n\nDid you mean:\n" + "\n".join(
                        f"  - {s}" for s in suggestions
                    )
                return [TextContent(type="text", text=error_msg)]

            # Parse JSON to extract key info for summary
            summary = ""
            data = None
            try:
                data = json.loads(result.stdout)
                # Run enrichment pipeline
                data = self._enrich_objdiff_data(data)

                match_pct = data.get("fuzzy_match_percent", "?")
                verdict = data.get("verdict", {}).get("classification", "UNKNOWN")
                summary = f"**Match: {match_pct}% | Verdict: {verdict}**\n\n"

                # Re-serialize enriched data
                output = json.dumps(data, indent=2)
                if result.stderr:
                    output += f"\n\n[stderr]\n{result.stderr}"
            except (json.JSONDecodeError, KeyError):
                # Not valid JSON, just use raw output
                pass

            # Count lines
            lines = output.split("\n")
            line_count = len(lines)

            if line_count < MAX_INLINE_LINES:
                # Return inline
                return [TextContent(
                    type="text",
                    text=f"{summary}```json\n{output}\n```"
                )]
            else:
                # Write to file in the project directory being tested
                analysis_dir = project_dir / "function_analysis"
                analysis_dir.mkdir(exist_ok=True, parents=True)

                # Sanitize symbol for filename
                safe_symbol = symbol.replace("?", "_Q_").replace("@", "_A_").replace("<", "_L_").replace(">", "_R_")
                output_file = analysis_dir / f"objdiff_{safe_symbol}.json"

                with open(output_file, "w") as f:
                    f.write(output)

                # Return file path with instructions
                return [TextContent(
                    type="text",
                    text=f"""{summary}Output is large ({line_count} lines). Written to file.

**File:** `{output_file.relative_to(project_dir)}`

**To view:**
- Summary: Use the Read tool to read first 100 lines
- Full content: `Read {output_file.relative_to(project_dir)}`
- Quick look: The match% and verdict are shown above

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
                output += f"\n\n[stderr]\n{result.stderr}"

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
    args = parser.parse_args()

    server = DecompMCPServer(db_path=args.db, rb3_path=args.rb3)
    asyncio.run(server.run())


if __name__ == "__main__":
    main()
