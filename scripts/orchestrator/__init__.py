"""DC3 Decomp Orchestrator - Multi-agent decompilation pipeline."""

from .database import (
    init_database,
    get_connection,
    ingest_report,
    get_next_function,
    query_functions,
    lock_function,
    unlock_function,
    unlock_session,
    record_attempt,
    update_function_status,
    get_function_by_symbol,
    get_last_attempt,
    get_attempts_for_function,
    get_stats,
)

from .worktree_pool import WorktreePool
from .mcp_server import DecompMCPServer
from .reporting import (
    generate_progress_report,
    generate_batch_summary,
    get_recent_attempts,
    get_active_sessions,
    get_unit_summary,
)

__all__ = [
    # Database
    "init_database",
    "get_connection",
    "ingest_report",
    "get_next_function",
    "query_functions",
    "lock_function",
    "unlock_function",
    "unlock_session",
    "record_attempt",
    "update_function_status",
    "get_function_by_symbol",
    "get_last_attempt",
    "get_attempts_for_function",
    "get_stats",
    # Worktree
    "WorktreePool",
    # MCP
    "DecompMCPServer",
    # Reporting
    "generate_progress_report",
    "generate_batch_summary",
    "get_recent_attempts",
    "get_active_sessions",
    "get_unit_summary",
]
