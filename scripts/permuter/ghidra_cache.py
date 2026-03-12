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
import time
from pathlib import Path
from typing import Optional

from .repo_paths import get_decomp_db_path

_DB_PATH = get_decomp_db_path()

# ---------------------------------------------------------------------------
# Circuit breaker for Ghidra MCP fetches
# ---------------------------------------------------------------------------
GHIDRA_MAX_FAILURES = 3  # consecutive failures before the breaker trips

_ghidra_consecutive_failures: int = 0
_ghidra_circuit_open: bool = False
_ghidra_circuit_trip_time: float = 0.0
_ghidra_reset_interval: float = 300.0  # 5 minutes default
_ghidra_backoff_multiplier: float = 1.0


class GhidraCircuitOpen(Exception):
    """Raised when the Ghidra circuit breaker has tripped."""
    pass


def ghidra_circuit_tripped() -> bool:
    """Return True if the circuit breaker is open (Ghidra is down).

    If the breaker is open but enough time has elapsed (based on the reset
    interval and backoff multiplier), returns False to allow a probe attempt.
    """
    if not _ghidra_circuit_open:
        return False
    elapsed = time.time() - _ghidra_circuit_trip_time
    if elapsed >= _ghidra_reset_interval * _ghidra_backoff_multiplier:
        return False  # allow one probe attempt
    return True


def set_ghidra_retry_interval(seconds: float) -> None:
    """Override the base retry interval for circuit breaker auto-reset.

    The actual interval used is ``seconds * backoff_multiplier``.
    """
    global _ghidra_reset_interval
    _ghidra_reset_interval = seconds


def _ghidra_record_success() -> None:
    global _ghidra_consecutive_failures, _ghidra_circuit_open, _ghidra_backoff_multiplier
    _ghidra_consecutive_failures = 0
    if _ghidra_circuit_open:
        _ghidra_circuit_open = False
        _ghidra_backoff_multiplier = 1.0
        print(
            "  [GHIDRA] Circuit breaker recovered — Ghidra is responding again",
            file=sys.stderr,
            flush=True,
        )


def _ghidra_record_failure() -> None:
    global _ghidra_consecutive_failures, _ghidra_circuit_open
    global _ghidra_circuit_trip_time, _ghidra_backoff_multiplier
    _ghidra_consecutive_failures += 1
    if _ghidra_consecutive_failures >= GHIDRA_MAX_FAILURES:
        was_already_open = _ghidra_circuit_open
        _ghidra_circuit_open = True
        _ghidra_circuit_trip_time = time.time()
        if was_already_open:
            # Re-tripped after a probe attempt failed — increase backoff
            _ghidra_backoff_multiplier = min(_ghidra_backoff_multiplier * 2.0, 16.0)
            print(
                f"  [GHIDRA] Circuit breaker re-tripped — "
                f"next retry in {_ghidra_reset_interval * _ghidra_backoff_multiplier:.0f}s",
                file=sys.stderr,
                flush=True,
            )
        else:
            print(
                f"  [GHIDRA] Circuit breaker tripped after "
                f"{_ghidra_consecutive_failures} consecutive failures — "
                f"next retry in {_ghidra_reset_interval * _ghidra_backoff_multiplier:.0f}s",
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
    if ghidra_circuit_tripped():
        raise GhidraCircuitOpen("Ghidra circuit breaker is open")

    cached = get_decompilation(symbol)
    if cached:
        return cached

    fetched = _decompile_via_ghidra(symbol)
    if ghidra_circuit_tripped():
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
