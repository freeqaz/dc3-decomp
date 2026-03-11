#!/usr/bin/env python3
"""Query the c2.dll Ghidra MCP server.

Usage:
    python3 msvc-src/scripts/c2_query.py decompile 0x10bc6487
    python3 msvc-src/scripts/c2_query.py callgraph 0x10bc6487 calling 2
    python3 msvc-src/scripts/c2_query.py search "color"
    python3 msvc-src/scripts/c2_query.py strings "register"
    python3 msvc-src/scripts/c2_query.py xrefs 0x10bc6487
    python3 msvc-src/scripts/c2_query.py rename 0x10bc6487 color_init
    python3 msvc-src/scripts/c2_query.py exports
    python3 msvc-src/scripts/c2_query.py metadata

Environment:
    C2_GHIDRA_PORT  — server port (default: 8001)
    C2_BINARY_NAME  — binary name in Ghidra project (auto-detected if not set)
"""
from __future__ import annotations

import asyncio
import os
import sys
import json


def _get_port() -> int:
    return int(os.environ.get("C2_GHIDRA_PORT", "8001"))


def _get_binary() -> str:
    return os.environ.get("C2_BINARY_NAME", "")


async def _resolve_binary(session) -> str:
    """Auto-detect the c2.dll binary name from the project."""
    name = _get_binary()
    if name:
        return name
    result = await session.call_tool("list_project_binaries", {})
    for item in result.content:
        text = item.text if hasattr(item, "text") else str(item)
        data = json.loads(text)
        for prog in data.get("programs", []):
            if "c2.dll" in prog.get("name", ""):
                return prog["name"]
    raise RuntimeError("No c2.dll binary found in project. Is the server running?")


async def run(cmd: str, args: list[str]) -> None:
    from mcp import ClientSession
    from mcp.client.sse import sse_client

    port = _get_port()
    url = f"http://127.0.0.1:{port}/sse"

    async with sse_client(url) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            binary = await _resolve_binary(session)

            if cmd == "list-tools":
                tools = await session.list_tools()
                for t in tools.tools:
                    schema = t.inputSchema if hasattr(t, "inputSchema") else {}
                    props = schema.get("properties", {}) if isinstance(schema, dict) else {}
                    print(f"  {t.name}({', '.join(props.keys())})")
                return

            if cmd == "list-binaries":
                result = await session.call_tool("list_project_binaries", {})
            elif cmd == "decompile":
                result = await session.call_tool("decompile_function", {
                    "binary_name": binary,
                    "name_or_address": args[0],
                })
            elif cmd == "search":
                result = await session.call_tool("search_functions_by_name", {
                    "binary_name": binary,
                    "query": args[0],
                })
            elif cmd == "xrefs":
                result = await session.call_tool("list_cross_references", {
                    "binary_name": binary,
                    "name_or_address": args[0],
                })
            elif cmd == "callgraph":
                direction = args[1] if len(args) > 1 else "calling"
                depth = int(args[2]) if len(args) > 2 else 2
                result = await session.call_tool("gen_callgraph", {
                    "binary_name": binary,
                    "name_or_address": args[0],
                    "direction": direction,
                    "depth": depth,
                })
            elif cmd == "exports":
                result = await session.call_tool("list_exports", {
                    "binary_name": binary,
                })
            elif cmd == "strings":
                result = await session.call_tool("search_strings", {
                    "binary_name": binary,
                    "query": args[0],
                })
            elif cmd == "rename":
                result = await session.call_tool("rename_function", {
                    "binary_name": binary,
                    "name_or_address": args[0],
                    "new_name": args[1],
                })
            elif cmd == "read-bytes":
                length = int(args[1]) if len(args) > 1 else 64
                result = await session.call_tool("read_bytes", {
                    "binary_name": binary,
                    "address": args[0],
                    "length": length,
                })
            elif cmd == "metadata":
                result = await session.call_tool("list_project_binary_metadata", {
                    "binary_name": binary,
                })
            elif cmd == "structures":
                result = await session.call_tool("list_structures", {
                    "binary_name": binary,
                })
            else:
                print(f"Unknown command: {cmd}")
                print(__doc__)
                return

            for item in result.content:
                print(item.text if hasattr(item, "text") else str(item))


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    cmd = sys.argv[1]
    args = sys.argv[2:]
    asyncio.run(run(cmd, args))


if __name__ == "__main__":
    main()
