#!/usr/bin/env python3
"""
Debug tool for troubleshooting confused agents.

Provides commands to:
1. Show what an agent would see for a symbol
2. List available tools and their signatures
3. Check worktree health
4. Show agent session logs
5. Simulate MCP tool calls

Usage:
    python3 tools/debug_agent.py show-context "?Symbol@@..."
    python3 tools/debug_agent.py list-tools
    python3 tools/debug_agent.py check-worktree /var/tmp/decomp-agents/agent-0
    python3 tools/debug_agent.py show-logs {session_id}
"""

import argparse
import json
import logging
import subprocess
import sys
from pathlib import Path
from typing import Optional

# Add scripts to path
sys.path.insert(0, str(Path(__file__).parent.parent))

from scripts.orchestrator.database import get_function_by_symbol, DEFAULT_DB_PATH
from scripts.orchestrator.context_collector import collect_pre_run_context
from scripts.orchestrator.core import DEFAULT_LOGS_DIR


def show_context(symbol: str, db_path: str = DEFAULT_DB_PATH) -> None:
    """Show exactly what an agent would see for a symbol."""
    print(f"\n{'='*60}")
    print(f"Agent Context for: {symbol}")
    print(f"{'='*60}\n")

    # Get function from database
    func = get_function_by_symbol(symbol, db_path=db_path)
    if not func:
        print(f"❌ Symbol not found in database: {symbol}")
        return

    print(f"✓ Function found in database:")
    print(f"  - Demangled: {func.get('demangled', 'N/A')}")
    print(f"  - File: {func.get('unit', 'unknown')}")
    print(f"  - Current Match: {func.get('current_percent', 'unimplemented')}%")
    print(f"  - Verdict: {func.get('verdict', 'N/A')}")
    print(f"  - Attempts: {func.get('attempt_count', 0)}")

    # Try to collect pre-run context
    print(f"\n✓ Pre-computed context available:")
    try:
        context = collect_pre_run_context(
            symbol=symbol,
            unit=func.get("unit"),
            project_dir=str(Path.cwd()),
            worktree_dir=str(Path.cwd())
        )
        print(f"  - Match %: {context.get('match_percent', 'N/A')}")
        print(f"  - Verdict: {context.get('verdict', 'N/A')}")
        print(f"  - Key Patterns: {', '.join(context.get('key_patterns', []) or [])}")
        print(f"  - RB3 Reference: {len(context.get('rb3_reference', '')) // 40} lines")
        print(f"  - Ghidra Decompilation: {len(context.get('decompilation', '')) // 40} lines")
        print(f"  - m2c Output: {context.get('m2c_line_count', 0)} lines")
        print(f"  - Previous Attempts: {context.get('previous_attempts_count', 0)}")
    except Exception as e:
        print(f"  ❌ Failed to collect context: {e}")

    print(f"\n{'='*60}\n")


def list_tools() -> None:
    """List available MCP tools and their exact signatures."""
    print(f"\n{'='*60}")
    print("Available MCP Tools (with exact signatures)")
    print(f"{'='*60}\n")

    tools = [
        {
            "name": "mcp__orchestrator__run_objdiff",
            "description": "Build and diff a function",
            "params": {
                "symbol": "function symbol (required)",
                "project_dir": "worktree path (⚠️ CRITICAL - your changes won't be tested without this!)",
                "full_build": "force full rebuild (optional)",
            }
        },
        {
            "name": "mcp__orchestrator__run_analyze_function",
            "description": "Detailed analysis with struct offset resolution",
            "params": {
                "symbol": "function symbol (required)",
                "project_dir": "worktree path (⚠️ CRITICAL - your changes won't be tested without this!)",
                "resolve_offsets": "resolve struct fields (optional, default: true)",
                "output_format": "markdown or json (optional, default: markdown)",
            }
        },
        {
            "name": "mcp__orchestrator__report_result",
            "description": "Report task completion",
            "params": {
                "status": "complete | at_limit | stuck | error (required)",
                "percent": "final match % (required)",
                "notes": "summary of what you tried (required)",
            }
        },
        {
            "name": "mcp__orchestrator__get_attempts",
            "description": "View previous attempt history",
            "params": {
                "symbol": "function symbol (required)",
            }
        },
        {
            "name": "mcp__orchestrator__lookup_rb3",
            "description": "Search RB3 for similar implementation (usually pre-computed)",
            "params": {
                "symbol": "function symbol (required)",
            }
        },
        {
            "name": "mcp__orchestrator__query_functions",
            "description": "Query database for functions matching criteria",
            "params": {
                "min_percent": "minimum match % (optional)",
                "max_percent": "maximum match % (optional)",
                "unit_pattern": "unit glob pattern (optional)",
                "limit": "max results (optional, default: 20)",
            }
        },
    ]

    for i, tool in enumerate(tools, 1):
        print(f"{i}. {tool['name']}")
        print(f"   Description: {tool['description']}")
        print(f"   Parameters:")
        for param, desc in tool['params'].items():
            print(f"     - {param}: {desc}")
        print()


def check_worktree(worktree_path: str) -> None:
    """Check worktree health and configuration."""
    path = Path(worktree_path)

    print(f"\n{'='*60}")
    print(f"Worktree Health Check: {path}")
    print(f"{'='*60}\n")

    if not path.exists():
        print(f"❌ Worktree does not exist: {path}")
        return

    print(f"✓ Worktree exists: {path}")

    # Check for required symlinks
    clangd_link = path / ".clangd"
    compile_cmds = path / "compile_commands.json"

    print(f"\nSymlink Check:")
    if clangd_link.is_symlink():
        print(f"  ✓ .clangd symlink exists")
    else:
        print(f"  ⚠️  .clangd symlink missing (may cause false diagnostics)")

    if compile_cmds.is_symlink():
        print(f"  ✓ compile_commands.json symlink exists")
    else:
        print(f"  ⚠️  compile_commands.json missing (build may fail)")

    # Check for key directories
    print(f"\nDirectory Check:")
    for subdir in ["src", "include", "build", "bin"]:
        subpath = path / subdir
        if subpath.exists():
            print(f"  ✓ {subdir}/ exists")
        else:
            print(f"  ⚠️  {subdir}/ missing")

    # Check if we can run objdiff-cli
    print(f"\nBuild Tools Check:")
    objdiff_cli = path / "bin" / "objdiff-cli"
    if objdiff_cli.exists():
        print(f"  ✓ objdiff-cli found")
    else:
        print(f"  ⚠️  objdiff-cli not found (builds will fail)")

    ninja_cmd = path / "build.ninja"
    if ninja_cmd.exists():
        print(f"  ✓ build.ninja exists")
    else:
        print(f"  ⚠️  build.ninja missing (cannot build)")

    print(f"\n{'='*60}\n")


def show_logs(session_id: Optional[str] = None) -> None:
    """Show agent session logs."""
    logs_dir = Path(DEFAULT_LOGS_DIR)

    print(f"\n{'='*60}")
    if session_id:
        print(f"Agent Logs for Session: {session_id}")
    else:
        print(f"All Agent Logs")
    print(f"{'='*60}\n")

    if not logs_dir.exists():
        print(f"No logs directory found at {logs_dir}")
        return

    # Find matching log files
    pattern = f"*{session_id}*.log" if session_id else "*.log"
    log_files = list(logs_dir.glob(pattern))

    if not log_files:
        print(f"No log files found matching: {pattern}")
        return

    print(f"Found {len(log_files)} log files:\n")
    for log_file in sorted(log_files)[:10]:  # Show last 10
        size = log_file.stat().st_size
        size_str = f"{size / 1024:.1f}KB" if size > 1024 else f"{size}B"
        print(f"  📋 {log_file.name} ({size_str})")

    if session_id:
        # Try to show content of most recent matching log
        recent = max(log_files, key=lambda x: x.stat().st_mtime)
        print(f"\n{'─'*60}")
        print(f"Latest log file: {recent.name}")
        print(f"{'─'*60}\n")

        try:
            # Show last 50 lines
            with open(recent) as f:
                lines = f.readlines()
                start = max(0, len(lines) - 50)
                for line in lines[start:]:
                    print(line, end='')
        except Exception as e:
            print(f"Failed to read log: {e}")

    print(f"\n{'='*60}\n")


def call_tool(tool_name: str, **kwargs) -> None:
    """Simulate an MCP tool call."""
    print(f"\n{'='*60}")
    print(f"Simulating Tool Call: {tool_name}")
    print(f"{'='*60}\n")

    print(f"Parameters:")
    for key, value in kwargs.items():
        print(f"  - {key}: {value}")

    print(f"\n⚠️  This is a simulation. Actual tool calls happen in agent context.")
    print(f"To test real tool calls, run orchestrate with --dry-run\n")

    print(f"{'='*60}\n")


def main():
    parser = argparse.ArgumentParser(
        description="Debug tool for troubleshooting agents",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 tools/debug_agent.py show-context "?Symbol@@..."
  python3 tools/debug_agent.py list-tools
  python3 tools/debug_agent.py check-worktree /tmp/claude/decomp-agents/agent-0
  python3 tools/debug_agent.py show-logs session-id
        """
    )

    subparsers = parser.add_subparsers(dest="command", help="Command to run")

    # show-context
    ctx_parser = subparsers.add_parser("show-context", help="Show what agent would see")
    ctx_parser.add_argument("symbol", help="Function symbol")
    ctx_parser.add_argument("--db", default=DEFAULT_DB_PATH, help="Database path")

    # list-tools
    subparsers.add_parser("list-tools", help="List available MCP tools")

    # check-worktree
    wt_parser = subparsers.add_parser("check-worktree", help="Check worktree health")
    wt_parser.add_argument("path", help="Worktree path")

    # show-logs
    log_parser = subparsers.add_parser("show-logs", help="Show agent session logs")
    log_parser.add_argument("session_id", nargs="?", help="Session ID (optional)")

    # call-tool
    tool_parser = subparsers.add_parser("call-tool", help="Simulate MCP tool call")
    tool_parser.add_argument("tool_name", help="Tool name")
    tool_parser.add_argument("--symbol", help="Function symbol")
    tool_parser.add_argument("--project-dir", help="Project directory")
    tool_parser.add_argument("--full-build", action="store_true", help="Force full build")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return

    if args.command == "show-context":
        show_context(args.symbol, args.db)
    elif args.command == "list-tools":
        list_tools()
    elif args.command == "check-worktree":
        check_worktree(args.path)
    elif args.command == "show-logs":
        show_logs(args.session_id)
    elif args.command == "call-tool":
        kwargs = {}
        if args.symbol:
            kwargs["symbol"] = args.symbol
        if args.project_dir:
            kwargs["project_dir"] = args.project_dir
        if args.full_build:
            kwargs["full_build"] = True
        call_tool(args.tool_name, **kwargs)


if __name__ == "__main__":
    main()
