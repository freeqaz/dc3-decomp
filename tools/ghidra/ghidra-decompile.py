#!/usr/bin/env python3
"""
Decompile a function from the Ghidra project.

Usage:
    ghidra-decompile.py "Game::Poll"
    ghidra-decompile.py 0x82000000
    ghidra-decompile.py "Symbol::Symbol" --json
"""

import argparse
import json
import sys
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from mcp_client import MCPClient, MCPError, DEFAULT_BINARY


def format_decompiled(result: dict) -> str:
    """Format decompiled function output for display."""
    lines = []

    # Function info
    func_name = result.get("function_name", "unknown")
    address = result.get("address", "unknown")
    signature = result.get("signature", "")

    lines.append(f"// Function: {func_name}")
    lines.append(f"// Address: {address}")
    if signature:
        lines.append(f"// Signature: {signature}")
    lines.append("")

    # Decompiled code
    code = result.get("decompiled_code", result.get("code", ""))
    if code:
        lines.append(code)
    else:
        lines.append("// No decompiled code available")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Decompile a function from Ghidra",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s "Game::Poll"         # Decompile by name
  %(prog)s 0x82000000           # Decompile by address
  %(prog)s "Symbol::Symbol" --json  # Output raw JSON
        """
    )
    parser.add_argument(
        "function",
        help="Function name or address (e.g., 'Game::Poll' or '0x82000000')"
    )
    parser.add_argument(
        "--binary", "-b",
        default=DEFAULT_BINARY,
        help=f"Binary name (default: {DEFAULT_BINARY})"
    )
    parser.add_argument(
        "--json", "-j",
        action="store_true",
        help="Output raw JSON instead of formatted code"
    )
    parser.add_argument(
        "--reinit",
        action="store_true",
        help="Force session re-initialization"
    )

    args = parser.parse_args()

    try:
        client = MCPClient(binary=args.binary)
        client.initialize(force=args.reinit)

        result = client.decompile_function(args.function)

        if args.json:
            print(json.dumps(result, indent=2))
        else:
            print(format_decompiled(result))

    except MCPError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)


if __name__ == "__main__":
    main()
