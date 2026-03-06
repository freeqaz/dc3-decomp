"""Accessor for Ghidra decompilation cache in decomp.db.

Primary path is read-only cache lookup. Optional fetch-on-miss mode can
decompile via Ghidra MCP and upsert into the cache.

Includes a circuit breaker: after ``GHIDRA_MAX_FAILURES`` consecutive
fetch failures the breaker trips and all subsequent fetch attempts raise
``GhidraCircuitOpen`` immediately, letting batch callers stop gracefully
instead of blocking on repeated timeouts.
"""

from __future__ import annotations

import re
import sqlite3
import sys
from pathlib import Path
from typing import Optional

_DB_PATH = Path(__file__).resolve().parents[2] / "decomp.db"

# ---------------------------------------------------------------------------
# Circuit breaker for Ghidra MCP fetches
# ---------------------------------------------------------------------------
GHIDRA_MAX_FAILURES = 3  # consecutive failures before the breaker trips

_ghidra_consecutive_failures: int = 0
_ghidra_circuit_open: bool = False


class GhidraCircuitOpen(Exception):
    """Raised when the Ghidra circuit breaker has tripped."""
    pass


def ghidra_circuit_tripped() -> bool:
    """Return True if the circuit breaker is open (Ghidra is down)."""
    return _ghidra_circuit_open


def _ghidra_record_success() -> None:
    global _ghidra_consecutive_failures, _ghidra_circuit_open
    _ghidra_consecutive_failures = 0
    # Don't auto-close — once tripped, stay tripped for this process


def _ghidra_record_failure() -> None:
    global _ghidra_consecutive_failures, _ghidra_circuit_open
    _ghidra_consecutive_failures += 1
    if _ghidra_consecutive_failures >= GHIDRA_MAX_FAILURES and not _ghidra_circuit_open:
        _ghidra_circuit_open = True
        print(
            f"  [GHIDRA] Circuit breaker tripped after "
            f"{_ghidra_consecutive_failures} consecutive failures — "
            f"disabling Ghidra fetches for this run",
            file=sys.stderr,
            flush=True,
        )


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
    Records success/failure for the circuit breaker.
    """
    try:
        from tools.ghidra.mcp_client import MCPClient, MCPError
    except Exception:
        _ghidra_record_failure()
        return None

    try:
        client = MCPClient()
        client.initialize()
        result = client.decompile_function(symbol)
    except MCPError:
        _ghidra_record_failure()
        return None
    except Exception:
        _ghidra_record_failure()
        return None

    if not isinstance(result, dict):
        _ghidra_record_failure()
        return None

    code = result.get("code")
    if not isinstance(code, str) or not code:
        _ghidra_record_failure()
        return None

    _ghidra_record_success()

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
    Raises ``GhidraCircuitOpen`` when the circuit breaker has tripped,
    signalling that Ghidra is down and the caller should stop.
    """
    if _ghidra_circuit_open:
        raise GhidraCircuitOpen("Ghidra circuit breaker is open")

    cached = get_decompilation(symbol)
    if cached:
        return cached

    fetched = _decompile_via_ghidra(symbol)
    if _ghidra_circuit_open:
        raise GhidraCircuitOpen("Ghidra circuit breaker tripped during fetch")
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
