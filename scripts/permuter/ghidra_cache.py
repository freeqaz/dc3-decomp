"""Accessor for Ghidra decompilation cache in decomp.db.

Primary path is read-only cache lookup. Optional fetch-on-miss mode can
decompile via Ghidra MCP and upsert into the cache.
"""

from __future__ import annotations

import re
import sqlite3
from pathlib import Path
from typing import Optional

_DB_PATH = Path(__file__).resolve().parents[2] / "decomp.db"


def _connect() -> sqlite3.Connection:
    if not _DB_PATH.exists():
        raise FileNotFoundError(f"decomp.db not found at {_DB_PATH}")
    return sqlite3.connect(str(_DB_PATH), timeout=5)


def _upsert_decompilation(
    symbol: str,
    code: str,
    address: Optional[str] = None,
    signature: Optional[str] = None,
) -> None:
    conn = sqlite3.connect(str(_DB_PATH), timeout=10)
    try:
        conn.execute("PRAGMA busy_timeout = 10000")
        conn.execute(
            """
            INSERT INTO decompilations(symbol, address, code, signature, error, cached_at)
            VALUES(?, ?, ?, ?, NULL, CURRENT_TIMESTAMP)
            ON CONFLICT(symbol) DO UPDATE SET
                address=excluded.address,
                code=excluded.code,
                signature=excluded.signature,
                error=NULL,
                cached_at=CURRENT_TIMESTAMP
            """,
            (symbol, address, code, signature),
        )
        conn.commit()
    finally:
        conn.close()


def _decompile_via_ghidra(symbol: str) -> tuple[str, Optional[str], Optional[str]] | None:
    """Fetch decompilation from running pyghidra-mcp service.

    Returns (code, address, signature) on success, else None.
    """
    try:
        from tools.ghidra.mcp_client import MCPClient, MCPError
    except Exception:
        return None

    try:
        client = MCPClient()
        client.initialize()
        result = client.decompile_function(symbol)
    except MCPError:
        return None
    except Exception:
        return None

    if not isinstance(result, dict):
        return None

    code = result.get("code")
    if not isinstance(code, str) or not code:
        return None

    signature = result.get("signature")
    if signature is not None and not isinstance(signature, str):
        signature = None

    address = None
    name = result.get("name")
    if isinstance(name, str):
        m = re.search(r"-([0-9A-Fa-f]{8,16})$", name)
        if m:
            address = m.group(1).lower()

    return code, address, signature


def get_decompilation(symbol: str) -> str | None:
    """Look up cached Ghidra decompilation by mangled symbol.

    Returns the decompiled C code string, or None if not cached / errored.
    """
    conn = _connect()
    try:
        cur = conn.execute(
            "SELECT code FROM decompilations WHERE symbol = ? AND error IS NULL",
            (symbol,),
        )
        row = cur.fetchone()
        return row[0] if row else None
    finally:
        conn.close()


def get_or_cache_decompilation(symbol: str) -> str | None:
    """Return cached decompilation; fetch+cache from Ghidra on miss.

    This function is best-effort: any fetch/cache failure returns None.
    """
    cached = get_decompilation(symbol)
    if cached:
        return cached

    fetched = _decompile_via_ghidra(symbol)
    if not fetched:
        return None

    code, address, signature = fetched
    try:
        _upsert_decompilation(symbol, code, address=address, signature=signature)
    except Exception:
        # Cache write failure should not block use of fetched decompilation.
        pass
    return code


def get_decompilation_by_address(address: str) -> str | None:
    """Look up cached Ghidra decompilation by address (hex string).

    Returns the decompiled C code string, or None if not cached / errored.
    """
    conn = _connect()
    try:
        cur = conn.execute(
            "SELECT code FROM decompilations WHERE address = ? AND error IS NULL",
            (address,),
        )
        row = cur.fetchone()
        return row[0] if row else None
    finally:
        conn.close()


def get_signature(symbol: str) -> str | None:
    """Look up cached function signature by mangled symbol."""
    conn = _connect()
    try:
        cur = conn.execute(
            "SELECT signature FROM decompilations WHERE symbol = ? AND error IS NULL",
            (symbol,),
        )
        row = cur.fetchone()
        return row[0] if row else None
    finally:
        conn.close()
