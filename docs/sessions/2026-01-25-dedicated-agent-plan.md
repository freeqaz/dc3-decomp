# Multi-Agent Decomp Pipeline

> **Status:** Draft v2 - Agent SDK architecture
> **Goal:** Scalable multi-agent system for automated decompilation

## Overview

A pipeline that:
1. Ingests functions from jess (dtk fork) into a SQLite queue
2. Spawns parallel Claude agents via Agent SDK
3. Each agent works in an isolated worktree
4. Agents report results back to DB via MCP tools
5. Good changes merge to `develop` worktree
6. Periodic full builds catch integration issues

### Design Principles

- **Configurable parallelism** - Start with 1-3 agents, scale to dozens (or Lambda)
- **Isolation** - Each agent gets its own worktree, no stomping
- **Model escalation** - Start cheap (Haiku), escalate on retry (Sonnet → Opus)
- **Coordinated builds** - Agents can request builds; orchestrator batches them
- **Batch-oriented** - "Decompile this folder" as the unit of work

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              Orchestrator                               │
│                           (Python + Agent SDK)                          │
├─────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐   │
│  │   Ingest    │  │   Triage    │  │   Dispatch  │  │   Collect   │   │
│  │  (jess →DB) │  │ (pick model)│  │ (spawn agent│  │  (results)  │   │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘   │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                            SQLite Database                              │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ functions: symbol, unit, percent, verdict, locked_by, model, ... │  │
│  │ attempts: function_id, model, status, start_%, end_%, patch, ... │  │
│  │ batches: id, folder_pattern, status, started_at, finished_at     │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
        ┌───────────────────────┼───────────────────────┐
        ▼                       ▼                       ▼
┌───────────────┐       ┌───────────────┐       ┌───────────────┐
│  Worktree 1   │       │  Worktree 2   │       │  Worktree N   │
│  Agent (Haiku)│       │ Agent (Sonnet)│       │  Agent (Opus) │
│               │       │               │       │               │
│  ┌─────────┐  │       │  ┌─────────┐  │       │  ┌─────────┐  │
│  │ decomp  │  │       │  │ decomp  │  │       │  │ decomp  │  │
│  │   MCP   │◄─┼───────┼──┤   MCP   │◄─┼───────┼──┤   MCP   │  │
│  │ server  │  │       │  │ server  │  │       │  │ server  │  │
│  └─────────┘  │       │  └─────────┘  │       │  └─────────┘  │
└───────┬───────┘       └───────┬───────┘       └───────┬───────┘
        │                       │                       │
        └───────────────────────┼───────────────────────┘
                                ▼
                    ┌───────────────────────┐
                    │   'develop' Worktree  │
                    │   (merge good patches)│
                    └───────────┬───────────┘
                                ▼
                    ┌───────────────────────┐
                    │   Full Build + Test   │
                    │   (periodic / batch)  │
                    └───────────────────────┘
```

---

## Database Schema

```sql
-- Core function tracking
CREATE TABLE functions (
    id INTEGER PRIMARY KEY,
    symbol TEXT NOT NULL UNIQUE,        -- Mangled name (unique key)
    demangled TEXT,                     -- Human-readable name
    unit TEXT,                          -- "src/system/char/Char.cpp"
    size INTEGER,

    -- Current state
    current_percent REAL,
    best_percent REAL,
    verdict TEXT,                       -- COMPLETE, AT_LIMIT, PENDING, etc.

    -- Lock management
    locked_by TEXT,                     -- Agent session ID
    locked_at TIMESTAMP,

    -- Model escalation
    attempt_count INTEGER DEFAULT 0,
    last_model TEXT,                    -- haiku, sonnet, opus
    next_model TEXT,                    -- What to try next

    -- Result
    source_patch TEXT,                  -- The successful diff

    -- Metadata
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Attempt history (for learning / debugging)
CREATE TABLE attempts (
    id INTEGER PRIMARY KEY,
    function_id INTEGER REFERENCES functions(id),
    session_id TEXT,                    -- Agent SDK session ID
    model TEXT,                         -- haiku, sonnet, opus

    -- Timing
    started_at TIMESTAMP,
    finished_at TIMESTAMP,

    -- Result
    exit_status TEXT,                   -- success, stuck, error, timeout
    start_percent REAL,
    end_percent REAL,
    verdict TEXT,

    -- What was tried
    patch TEXT,                         -- The diff produced
    notes TEXT,                         -- Agent's notes
    iterations INTEGER,                 -- How many tool calls

    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Batch tracking
CREATE TABLE batches (
    id INTEGER PRIMARY KEY,
    folder_pattern TEXT,                -- "src/system/char/*"
    status TEXT,                        -- pending, running, complete, failed

    -- Stats
    total_functions INTEGER,
    completed_functions INTEGER,

    -- Timing
    started_at TIMESTAMP,
    finished_at TIMESTAMP,
    committed_at TIMESTAMP,             -- When merged to develop

    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Build coordination
CREATE TABLE builds (
    id INTEGER PRIMARY KEY,
    type TEXT,                          -- "single", "full"
    requested_by TEXT,                  -- Session ID or "orchestrator"
    file_path TEXT,                     -- For single-file builds

    status TEXT,                        -- pending, running, success, failed
    error_message TEXT,

    started_at TIMESTAMP,
    finished_at TIMESTAMP,

    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Indexes for common queries
CREATE INDEX idx_functions_verdict ON functions(verdict);
CREATE INDEX idx_functions_locked ON functions(locked_by);
CREATE INDEX idx_functions_unit ON functions(unit);
CREATE INDEX idx_attempts_function ON attempts(function_id);
CREATE INDEX idx_builds_status ON builds(status);
```

---

## MCP Server (decomp tools)

```python
from claude_agent_sdk import tool, create_sdk_mcp_server
from typing import Any
import sqlite3
import json

def create_decomp_mcp_server(db_path: str, rb3_path: str):
    """Create the decomp MCP server with all tools."""

    db = sqlite3.connect(db_path)

    @tool(
        "report_result",
        "Report task completion. Call this when done working on a function.",
        {
            "status": str,      # "complete", "at_limit", "stuck", "error"
            "percent": float,   # Final match percentage
            "patch_file": str,  # Path to diff file (or empty)
            "notes": str        # Summary of what was tried
        }
    )
    async def report_result(args: dict[str, Any]) -> dict[str, Any]:
        # Orchestrator handles this - signals clean exit
        return {
            "content": [{
                "type": "text",
                "text": f"Result recorded: {args['status']} at {args['percent']}%"
            }],
            "_decomp_exit": True,  # Signal to orchestrator
            "_result": args
        }

    @tool(
        "query_functions",
        "Query the function database for work targets.",
        {
            "min_percent": float,
            "max_percent": float,
            "verdict": str,     # Optional filter
            "unit_pattern": str,  # Optional glob pattern
            "limit": int
        }
    )
    async def query_functions(args: dict[str, Any]) -> dict[str, Any]:
        cursor = db.execute("""
            SELECT symbol, demangled, unit, current_percent, verdict, size
            FROM functions
            WHERE current_percent >= ? AND current_percent <= ?
              AND locked_by IS NULL
              AND (? = '' OR verdict = ?)
              AND (? = '' OR unit GLOB ?)
            ORDER BY current_percent DESC
            LIMIT ?
        """, (
            args.get("min_percent", 0),
            args.get("max_percent", 100),
            args.get("verdict", ""),
            args.get("verdict", ""),
            args.get("unit_pattern", ""),
            args.get("unit_pattern", ""),
            args.get("limit", 20)
        ))

        results = [dict(zip([c[0] for c in cursor.description], row))
                   for row in cursor.fetchall()]

        return {
            "content": [{
                "type": "text",
                "text": json.dumps(results, indent=2)
            }]
        }

    @tool(
        "get_attempts",
        "Get previous attempts for a function to learn from.",
        {"symbol": str}
    )
    async def get_attempts(args: dict[str, Any]) -> dict[str, Any]:
        cursor = db.execute("""
            SELECT a.model, a.exit_status, a.start_percent, a.end_percent,
                   a.verdict, a.notes, a.iterations
            FROM attempts a
            JOIN functions f ON a.function_id = f.id
            WHERE f.symbol = ?
            ORDER BY a.created_at DESC
            LIMIT 10
        """, (args["symbol"],))

        results = [dict(zip([c[0] for c in cursor.description], row))
                   for row in cursor.fetchall()]

        if not results:
            return {"content": [{"type": "text", "text": "No previous attempts."}]}

        return {
            "content": [{
                "type": "text",
                "text": f"Previous attempts:\n{json.dumps(results, indent=2)}"
            }]
        }

    @tool(
        "lookup_rb3",
        "Find similar implementation in RB3 decomp (shared Milo engine).",
        {"symbol": str}
    )
    async def lookup_rb3(args: dict[str, Any]) -> dict[str, Any]:
        import subprocess

        # Search RB3 codebase for similar function
        symbol = args["symbol"]
        # Extract class::method pattern
        if "::" in symbol:
            parts = symbol.split("::")
            search_pattern = parts[-1]  # Method name
        else:
            search_pattern = symbol

        try:
            result = subprocess.run(
                ["grep", "-rn", search_pattern, rb3_path],
                capture_output=True, text=True, timeout=30
            )

            if result.stdout:
                # Limit output
                lines = result.stdout.strip().split("\n")[:20]
                return {
                    "content": [{
                        "type": "text",
                        "text": f"RB3 matches for '{search_pattern}':\n" + "\n".join(lines)
                    }]
                }
            else:
                return {
                    "content": [{
                        "type": "text",
                        "text": f"No RB3 matches found for '{search_pattern}'"
                    }]
                }
        except Exception as e:
            return {
                "content": [{
                    "type": "text",
                    "text": f"RB3 lookup error: {e}"
                }]
            }

    @tool(
        "request_build",
        "Request a build. Returns build status when complete.",
        {
            "type": str,        # "single" or "full"
            "file_path": str    # For single builds
        }
    )
    async def request_build(args: dict[str, Any]) -> dict[str, Any]:
        # Insert build request, orchestrator handles coordination
        cursor = db.execute("""
            INSERT INTO builds (type, file_path, status, requested_by)
            VALUES (?, ?, 'pending', ?)
            RETURNING id
        """, (args["type"], args.get("file_path", ""), "agent"))

        build_id = cursor.fetchone()[0]
        db.commit()

        # In real implementation, this would wait for build completion
        # For now, return the build ID for orchestrator to handle
        return {
            "content": [{
                "type": "text",
                "text": f"Build requested (id={build_id}). Waiting for result..."
            }],
            "_build_request": build_id
        }

    return create_sdk_mcp_server(
        name="decomp",
        version="1.0.0",
        tools=[
            report_result,
            query_functions,
            get_attempts,
            lookup_rb3,
            request_build
        ]
    )
```

---

## Model Selection

```python
def select_model(func: dict) -> str:
    """
    Select model for a function based on its characteristics.

    This function is intentionally simple - tweak as we learn what works.
    """
    attempt_count = func.get("attempt_count", 0)
    percent = func.get("current_percent") or 0
    size = func.get("size") or 0
    verdict = func.get("verdict") or ""

    # First attempt: always Haiku (cheap exploration)
    if attempt_count == 0:
        return "haiku"

    # Escalation based on attempts
    if attempt_count == 1:
        # Second attempt: Sonnet if Haiku didn't complete
        return "sonnet"

    if attempt_count >= 2:
        # Third+ attempt: Opus for stubborn cases
        # But only if we're close (worth the cost)
        if percent >= 90:
            return "opus"
        else:
            # Not close enough to justify Opus, try Sonnet again
            return "sonnet"

    # Fallback
    return "haiku"


def should_retry(func: dict, last_attempt: dict) -> bool:
    """
    Decide if a function should be retried.

    Returns False if we should give up.
    """
    verdict = func.get("verdict", "")
    attempt_count = func.get("attempt_count", 0)

    # Already complete
    if verdict == "COMPLETE":
        return False

    # Hit the limit - no point retrying
    if verdict == "AT_LIMIT":
        return False

    # Too many attempts
    if attempt_count >= 5:
        return False

    # Check if last attempt made progress
    if last_attempt:
        start = last_attempt.get("start_percent", 0)
        end = last_attempt.get("end_percent", 0)

        # No progress after Opus? Give up
        if last_attempt.get("model") == "opus" and end <= start:
            return False

    return True
```

---

## Worktree Management

```python
import subprocess
import os
from pathlib import Path

class WorktreePool:
    """
    Manages a pool of git worktrees for parallel agents.
    """

    def __init__(self, main_repo: Path, pool_dir: Path, pool_size: int = 3):
        self.main_repo = main_repo
        self.pool_dir = pool_dir
        self.pool_size = pool_size
        self.available: list[Path] = []
        self.in_use: dict[str, Path] = {}  # session_id -> worktree path

        # Symlinks to create in each worktree
        self.symlinks = [
            "bin",                      # Tools (objdiff-cli, analyze-function)
            "decomp.db",                # SQLite database
            ".claude",                  # Agent prompts and context
            "compile_commands.json",    # For clangd
            ".clangd",                  # Clangd config
        ]

    def initialize(self):
        """Create the worktree pool."""
        self.pool_dir.mkdir(parents=True, exist_ok=True)

        for i in range(self.pool_size):
            worktree_path = self.pool_dir / f"agent-{i}"

            if not worktree_path.exists():
                # Create worktree from main branch
                subprocess.run([
                    "git", "worktree", "add",
                    str(worktree_path),
                    "HEAD", "--detach"
                ], cwd=self.main_repo, check=True)

                # Create symlinks
                self._setup_symlinks(worktree_path)

            self.available.append(worktree_path)

    def _setup_symlinks(self, worktree_path: Path):
        """Create symlinks to shared resources."""
        for link_name in self.symlinks:
            src = self.main_repo / link_name
            dst = worktree_path / link_name

            if src.exists() and not dst.exists():
                dst.symlink_to(src)

    def acquire(self, session_id: str) -> Path | None:
        """Get a worktree for an agent session."""
        if not self.available:
            return None

        worktree = self.available.pop()
        self.in_use[session_id] = worktree

        # Reset to clean state
        subprocess.run(
            ["git", "checkout", "HEAD", "--force"],
            cwd=worktree, check=True,
            capture_output=True
        )
        subprocess.run(
            ["git", "clean", "-fd"],
            cwd=worktree, check=True,
            capture_output=True
        )

        return worktree

    def release(self, session_id: str):
        """Return a worktree to the pool."""
        if session_id in self.in_use:
            worktree = self.in_use.pop(session_id)
            self.available.append(worktree)

    def extract_patch(self, session_id: str) -> str | None:
        """Extract git diff from a worktree."""
        worktree = self.in_use.get(session_id)
        if not worktree:
            return None

        result = subprocess.run(
            ["git", "diff", "HEAD"],
            cwd=worktree, capture_output=True, text=True
        )

        return result.stdout if result.stdout else None
```

---

## Orchestrator

```python
import asyncio
from claude_agent_sdk import query, ClaudeAgentOptions
from dataclasses import dataclass
from typing import AsyncIterator
import logging

logger = logging.getLogger(__name__)

@dataclass
class AgentConfig:
    max_agents: int = 3
    max_turns: int = 50
    db_path: str = "decomp.db"
    rb3_path: str = "~/code/milohax/rb3/src"
    pool_dir: str = "/tmp/decomp-agents"

class DecompOrchestrator:
    def __init__(self, config: AgentConfig, main_repo: Path):
        self.config = config
        self.main_repo = main_repo
        self.db = sqlite3.connect(config.db_path)
        self.worktree_pool = WorktreePool(
            main_repo,
            Path(config.pool_dir),
            config.max_agents
        )
        self.active_sessions: dict[str, asyncio.Task] = {}
        self.mcp_server = create_decomp_mcp_server(
            config.db_path,
            config.rb3_path
        )

    async def run_batch(self, folder_pattern: str):
        """Run a batch of functions matching the folder pattern."""

        # Create batch record
        cursor = self.db.execute("""
            INSERT INTO batches (folder_pattern, status, started_at)
            VALUES (?, 'running', CURRENT_TIMESTAMP)
            RETURNING id
        """, (folder_pattern,))
        batch_id = cursor.fetchone()[0]
        self.db.commit()

        logger.info(f"Starting batch {batch_id}: {folder_pattern}")

        try:
            while True:
                # Get next function to work on
                func = self._get_next_function(folder_pattern)
                if not func:
                    # Check if any agents still running
                    if not self.active_sessions:
                        break
                    # Wait for an agent to finish
                    await self._wait_for_any_completion()
                    continue

                # Wait for available worktree
                while len(self.active_sessions) >= self.config.max_agents:
                    await self._wait_for_any_completion()

                # Spawn agent
                session_id = f"batch-{batch_id}-{func['id']}"
                task = asyncio.create_task(
                    self._run_agent(session_id, func)
                )
                self.active_sessions[session_id] = task

            # Mark batch complete
            self.db.execute("""
                UPDATE batches SET status = 'complete', finished_at = CURRENT_TIMESTAMP
                WHERE id = ?
            """, (batch_id,))
            self.db.commit()

            # Merge to develop
            await self._merge_to_develop(batch_id)

            # Full build
            await self._run_full_build()

            logger.info(f"Batch {batch_id} complete")

        except Exception as e:
            logger.error(f"Batch {batch_id} failed: {e}")
            self.db.execute("""
                UPDATE batches SET status = 'failed'
                WHERE id = ?
            """, (batch_id,))
            self.db.commit()
            raise

    def _get_next_function(self, folder_pattern: str) -> dict | None:
        """Get next unlocked function to work on."""
        cursor = self.db.execute("""
            SELECT id, symbol, demangled, unit, current_percent,
                   verdict, size, attempt_count
            FROM functions
            WHERE unit GLOB ?
              AND locked_by IS NULL
              AND verdict NOT IN ('COMPLETE', 'AT_LIMIT')
            ORDER BY
                CASE WHEN current_percent IS NULL THEN 1 ELSE 0 END,
                current_percent DESC
            LIMIT 1
        """, (folder_pattern,))

        row = cursor.fetchone()
        if not row:
            return None

        func = dict(zip([c[0] for c in cursor.description], row))

        # Check if should retry
        if func["attempt_count"] > 0:
            last_attempt = self._get_last_attempt(func["id"])
            if not should_retry(func, last_attempt):
                # Mark as done, move on
                self.db.execute("""
                    UPDATE functions SET verdict = 'AT_LIMIT'
                    WHERE id = ?
                """, (func["id"],))
                self.db.commit()
                return self._get_next_function(folder_pattern)

        return func

    async def _run_agent(self, session_id: str, func: dict):
        """Run an agent on a single function."""

        # Lock function
        self.db.execute("""
            UPDATE functions
            SET locked_by = ?, locked_at = CURRENT_TIMESTAMP
            WHERE id = ?
        """, (session_id, func["id"]))
        self.db.commit()

        # Get worktree
        worktree = self.worktree_pool.acquire(session_id)
        if not worktree:
            logger.error(f"No worktree available for {session_id}")
            return

        try:
            # Select model
            model = select_model(func)

            # Build prompt
            prompt = self._build_prompt(func)

            # Run agent
            options = ClaudeAgentOptions(
                model=model,
                cwd=str(worktree),
                allowed_tools=[
                    "Read", "Write", "Edit", "Bash", "Glob", "Grep",
                    "mcp__decomp__*"
                ],
                mcp_servers={"decomp": self.mcp_server},
                max_turns=self.config.max_turns,
                permission_mode="bypassPermissions"
            )

            result = None
            async for message in query(prompt=prompt, options=options):
                # Log progress
                if message.type == "assistant":
                    logger.debug(f"{session_id}: {message}")

                # Capture result
                if message.type == "result":
                    result = message

            # Extract patch
            patch = self.worktree_pool.extract_patch(session_id)

            # Record attempt
            self._record_attempt(session_id, func, model, result, patch)

        finally:
            # Unlock function
            self.db.execute("""
                UPDATE functions SET locked_by = NULL, locked_at = NULL
                WHERE id = ?
            """, (func["id"],))
            self.db.commit()

            # Release worktree
            self.worktree_pool.release(session_id)

    def _build_prompt(self, func: dict) -> str:
        """Build the prompt for an agent."""

        symbol = func["symbol"]
        demangled = func.get("demangled", symbol)
        unit = func.get("unit", "unknown")
        percent = func.get("current_percent", 0)

        return f"""
You are a decompilation agent working on DC3 (Dance Central 3) for Xbox 360.

## Target Function
- **Symbol:** `{symbol}`
- **Name:** `{demangled}`
- **File:** `{unit}`
- **Current Match:** {percent or 'unimplemented'}%

## Workflow

1. **Gather Context**
   Run: `./bin/analyze-function "{symbol}" -f json`
   This gives you Ghidra decompilation, callers/callees, and current status.

2. **Check Previous Attempts** (if any)
   Use the `get_attempts` tool to see what was tried before.

3. **Check RB3 Reference**
   Use the `lookup_rb3` tool - DC3 shares the Milo engine with Rock Band 3.

4. **Implement/Fix the Function**
   - Edit the source file in `{unit}`
   - Keep changes minimal and focused
   - Follow existing code style

5. **Iterate**
   Run: `./bin/objdiff-cli diff -p . "{symbol}" --build --verdict`
   - If LIKELY_FIXABLE: try control flow, variable order, comparison style
   - If MAYBE_FIXABLE: try register allocation tweaks
   - If AT_LIMIT: stop, this is as good as it gets

6. **Report Result**
   When done, call `report_result` with:
   - status: "complete" (100%), "at_limit" (unfixable), "stuck" (need help), "error"
   - percent: final match percentage
   - patch_file: leave empty (orchestrator extracts from git)
   - notes: summary of what you tried

## Known Patterns

- Use `x > 0` instead of `x != 0` for unsigned comparisons
- Use `0` not `0.0f` in initializer lists
- Variable declaration order affects register allocation
- `while` vs `for` loops can have different codegen
- ASSERT_REVS functions expect ~0.8-0.9% mismatch (unfixable)

## Safety

- Only edit files in `{unit}` or closely related headers
- Do not modify MILO_ASSERT calls
- Do not run `git reset` or destructive commands

## Stop Conditions

- verdict == COMPLETE: you're done, 100% match!
- verdict == AT_LIMIT: unfixable (linker-merged, bool masks), accept it
- 20+ iterations without progress: stop and report "stuck"
"""

    def _record_attempt(self, session_id: str, func: dict, model: str,
                        result: Any, patch: str | None):
        """Record an attempt in the database."""

        # Parse result
        status = "error"
        end_percent = func.get("current_percent", 0)
        verdict = func.get("verdict", "")
        notes = ""

        if result and hasattr(result, "subtype"):
            if result.subtype == "success":
                status = "success"
                # Try to parse final state from agent's report_result call
                # (In real implementation, extract from message stream)

        # Insert attempt
        self.db.execute("""
            INSERT INTO attempts
            (function_id, session_id, model, started_at, finished_at,
             exit_status, start_percent, end_percent, verdict, patch, notes)
            VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP, ?, ?, ?, ?, ?, ?)
        """, (
            func["id"], session_id, model,
            func.get("locked_at"),
            status, func.get("current_percent", 0), end_percent,
            verdict, patch, notes
        ))

        # Update function
        self.db.execute("""
            UPDATE functions
            SET current_percent = ?,
                best_percent = MAX(COALESCE(best_percent, 0), ?),
                verdict = ?,
                attempt_count = attempt_count + 1,
                last_model = ?,
                next_model = ?,
                source_patch = CASE WHEN ? = 'COMPLETE' THEN ? ELSE source_patch END,
                updated_at = CURRENT_TIMESTAMP
            WHERE id = ?
        """, (
            end_percent, end_percent, verdict, model,
            select_model({**func, "attempt_count": func["attempt_count"] + 1}),
            verdict, patch,
            func["id"]
        ))

        self.db.commit()

    async def _wait_for_any_completion(self):
        """Wait for any active agent to complete."""
        if not self.active_sessions:
            return

        done, _ = await asyncio.wait(
            self.active_sessions.values(),
            return_when=asyncio.FIRST_COMPLETED
        )

        # Remove completed sessions
        for task in done:
            for session_id, t in list(self.active_sessions.items()):
                if t == task:
                    del self.active_sessions[session_id]
                    break

    async def _merge_to_develop(self, batch_id: int):
        """Merge successful patches to develop worktree."""
        # Implementation: apply patches from completed functions
        pass

    async def _run_full_build(self):
        """Run a full build and queue any failures."""
        # Implementation: ninja && check for errors
        pass
```

---

## Entry Points

### CLI

```python
# scripts/decomp_agent.py
import click
import asyncio
from pathlib import Path

@click.group()
def cli():
    """DC3 Decomp Agent Pipeline"""
    pass

@cli.command()
@click.argument("folder_pattern")
@click.option("--max-agents", default=3, help="Max parallel agents")
@click.option("--db", default="decomp.db", help="Database path")
def batch(folder_pattern: str, max_agents: int, db: str):
    """Run a batch of functions."""
    config = AgentConfig(max_agents=max_agents, db_path=db)
    orchestrator = DecompOrchestrator(config, Path.cwd())
    asyncio.run(orchestrator.run_batch(folder_pattern))

@cli.command()
@click.argument("symbol")
@click.option("--model", default=None, help="Force specific model")
def single(symbol: str, model: str | None):
    """Run a single function."""
    # Implementation: run one agent on one function
    pass

@cli.command()
@click.option("--min-percent", default=0)
@click.option("--max-percent", default=100)
@click.option("--limit", default=20)
def query(min_percent: float, max_percent: float, limit: int):
    """Query functions in database."""
    # Implementation: query and display
    pass

if __name__ == "__main__":
    cli()
```

### Usage

```bash
# Initialize database from jess output
python scripts/decomp_agent.py ingest jess_output.json

# Run a batch
python scripts/decomp_agent.py batch "src/system/char/*" --max-agents 5

# Run single function
python scripts/decomp_agent.py single "CharMirror::Poll"

# Query database
python scripts/decomp_agent.py query --min-percent 90 --max-percent 99
```

---

## Build Coordination

For the coordinated build model (agents request builds, orchestrator batches):

```python
class BuildCoordinator:
    """
    Coordinates builds across multiple agents.

    Agents call request_build(), coordinator batches and executes.
    """

    def __init__(self, db: sqlite3.Connection, build_interval: float = 30.0):
        self.db = db
        self.build_interval = build_interval
        self.pending_builds: list[int] = []
        self._lock = asyncio.Lock()

    async def run(self):
        """Background task to process build requests."""
        while True:
            await asyncio.sleep(self.build_interval)
            await self._process_builds()

    async def _process_builds(self):
        """Process pending build requests."""
        async with self._lock:
            # Get pending builds
            cursor = self.db.execute("""
                SELECT id, type, file_path FROM builds
                WHERE status = 'pending'
                ORDER BY created_at
            """)
            pending = cursor.fetchall()

            if not pending:
                return

            # Decide: full build or batch of single builds
            has_full = any(b[1] == "full" for b in pending)

            if has_full:
                await self._run_full_build(pending)
            else:
                await self._run_incremental_build(pending)

    async def _run_full_build(self, builds: list):
        """Run ninja (full build)."""
        # Mark all as running
        ids = [b[0] for b in builds]
        self.db.execute(f"""
            UPDATE builds SET status = 'running', started_at = CURRENT_TIMESTAMP
            WHERE id IN ({','.join('?' * len(ids))})
        """, ids)
        self.db.commit()

        # Run build
        result = await asyncio.create_subprocess_exec(
            "ninja",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )
        stdout, stderr = await result.communicate()

        # Update status
        status = "success" if result.returncode == 0 else "failed"
        error = stderr.decode() if status == "failed" else None

        self.db.execute(f"""
            UPDATE builds
            SET status = ?, error_message = ?, finished_at = CURRENT_TIMESTAMP
            WHERE id IN ({','.join('?' * len(ids))})
        """, [status, error] + ids)
        self.db.commit()

    async def _run_incremental_build(self, builds: list):
        """Run targeted ninja builds."""
        for build_id, build_type, file_path in builds:
            # Mark running
            self.db.execute("""
                UPDATE builds SET status = 'running', started_at = CURRENT_TIMESTAMP
                WHERE id = ?
            """, (build_id,))
            self.db.commit()

            # Build target
            obj_path = file_path.replace("/src/", "/build/373307D9/src/")
            obj_path = obj_path.rsplit(".", 1)[0] + ".obj"

            result = await asyncio.create_subprocess_exec(
                "ninja", obj_path,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE
            )
            stdout, stderr = await result.communicate()

            # Update status
            status = "success" if result.returncode == 0 else "failed"
            error = stderr.decode() if status == "failed" else None

            self.db.execute("""
                UPDATE builds
                SET status = ?, error_message = ?, finished_at = CURRENT_TIMESTAMP
                WHERE id = ?
            """, (status, error, build_id))
            self.db.commit()
```

---

## Implementation Checklist

### Phase 1: Foundation
- [ ] Create SQLite schema
- [ ] Implement WorktreePool
- [ ] Implement basic MCP server (report_result, query_functions)
- [ ] Test single agent run

### Phase 2: Orchestration
- [ ] Implement DecompOrchestrator
- [ ] Add model selection logic
- [ ] Add retry logic
- [ ] Test batch run with 3 agents

### Phase 3: Integration
- [ ] Implement jess ingestion
- [ ] Implement merge to develop
- [ ] Implement full build validation
- [ ] Add build failure → high-priority queue

### Phase 4: Coordination
- [ ] Implement BuildCoordinator
- [ ] Add request_build MCP tool
- [ ] Test coordinated builds
- [ ] Add periodic full rebuild

### Phase 5: Polish
- [ ] CLI interface
- [ ] Logging and monitoring
- [ ] Error recovery
- [ ] Performance tuning

---

## Future Enhancements

- **Lambda execution** - Spawn agents on AWS Lambda for scale
- **Pattern database** - Learn from successful fixes across functions
- **decomp.me integration** - Share stubborn cases
- **Progress dashboard** - Web UI for monitoring
- **Ghidra MCP** - Direct Ghidra access for semantic search

---

## See Also

- [docs/tools/WORKFLOW.md](tools/WORKFLOW.md) - Current tool usage
- [docs/decomp/SUBAGENT_STRATEGY.md](decomp/SUBAGENT_STRATEGY.md) - Manual parallel agents
- [docs/tools/objdiff/USAGE.md](OBJDIFF_CLI_USAGE.md) - objdiff CLI reference
- [Agent SDK Docs](https://platform.claude.com/docs/en/agent-sdk/overview)
