"""Core orchestrator for DC3 decompilation agents.

Manages spawning agents, tracking progress, and coordinating work.
"""

import asyncio
import json
import logging
import logging.handlers
import os
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from typing import Any, Optional

# SDK integration toggle (default: use SDK)
USE_SDK = os.getenv("ORCHESTRATOR_USE_SDK", "true").lower() == "true"

# Import SDK types conditionally
try:
    from claude_code_sdk import (
        query as sdk_query,
        ClaudeCodeOptions,
        AssistantMessage,
        UserMessage,
        ResultMessage,
        ToolUseBlock,
        ToolResultBlock,
        TextBlock,
        CLINotFoundError,
        ProcessError,
    )
    from claude_code_sdk.types import McpStdioServerConfig
    SDK_AVAILABLE = True
except ImportError:
    SDK_AVAILABLE = False


def get_oauth_token() -> Optional[str]:
    """Read OAuth token from Claude CLI credentials file.

    Note: Only used for subprocess mode. SDK auto-detects credentials.
    """
    creds_path = Path.home() / ".claude" / ".credentials.json"
    if not creds_path.exists():
        return None
    try:
        with open(creds_path) as f:
            creds = json.load(f)
        return creds.get("claudeAiOauth", {}).get("accessToken")
    except (json.JSONDecodeError, IOError):
        return None


from .config import (
    get_backend,
    _get_openrouter_enabled,
    _get_openrouter_api_key,
    _get_openrouter_base_url,
    get_token_budget,
    requires_openrouter,
)
from .context_collector import collect_pre_run_context, TOTAL_CHAR_BUDGET, SECTION_CHAR_BUDGET
from .database import (
    get_connection,
    get_function_by_symbol,
    get_next_function,
    query_functions,
    query_batch_stats,
    lock_function,
    unlock_function,
    unlock_session,
    record_attempt,
    update_function_status,
    get_last_attempt,
    get_stats,
    DEFAULT_DB_PATH,
)
from .worktree_pool import WorktreePool
from .model_selection import select_model, should_retry, get_escalation_reason, get_model_id
from .patch_applier import PatchApplier
from .rb3_pairing import get_rb3_source_for_unit


# Default paths
DEFAULT_POOL_DIR = Path("/tmp/claude/decomp-agents")
DEFAULT_RB3_PATH = Path.home() / "code/milohax/rb3/src"
DEFAULT_LOGS_DIR = Path("logs")


def _detect_unfixable_patterns(objdiff_output: str) -> Optional[str]:
    """
    Detect unfixable patterns in objdiff output.

    Returns:
        String describing the unfixable pattern if detected, None otherwise
    """
    # Pattern 1: Struct offset mismatches (+ or - consistent offset deltas)
    # Example: "offset +4 bytes at 0x1234"
    if re.search(r'offset\s+[+-]\d+\s+bytes', objdiff_output, re.IGNORECASE):
        return "STRUCT_OFFSET_MISMATCH"

    # Pattern 2: File path differences (__FILE__ or compilation path)
    if re.search(r'__FILE__|compilation path|source path', objdiff_output, re.IGNORECASE):
        return "FILE_PATH_MISMATCH"

    # Pattern 3: Merged/linker calls (>50% ratio)
    merged_match = re.search(r'merged.*?(\d+\.?\d*)%|linker.*?(\d+\.?\d*)%', objdiff_output, re.IGNORECASE)
    if merged_match:
        ratio_str = merged_match.group(1) or merged_match.group(2)
        if ratio_str:
            try:
                ratio = float(ratio_str)
                if ratio >= 50:
                    return "HIGH_MERGED_CALL_RATIO"
            except ValueError:
                pass

    return None


def _setup_logging(logs_dir: Path = DEFAULT_LOGS_DIR) -> logging.Logger:
    """Initialize structured logging for orchestrator.

    Sets up:
    - Console handler: INFO level, concise format
    - File handler: DEBUG level, detailed format with timestamp
    - Rotating file handler: Agent session logs for post-mortem analysis

    Args:
        logs_dir: Directory to store log files

    Returns:
        Configured logger instance
    """
    logs_dir = Path(logs_dir)
    logs_dir.mkdir(parents=True, exist_ok=True)

    logger = logging.getLogger("decomp_orchestrator")
    if logger.hasHandlers():
        return logger  # Already configured

    logger.setLevel(logging.DEBUG)

    # Console handler: INFO and above, concise
    console_handler = logging.StreamHandler()
    console_handler.setLevel(logging.INFO)
    console_fmt = logging.Formatter(
        "[%(asctime)s] %(levelname)s: %(message)s",
        datefmt="%H:%M:%S"
    )
    console_handler.setFormatter(console_fmt)
    logger.addHandler(console_handler)

    # File handler: DEBUG and above, detailed with session context
    file_handler = logging.handlers.RotatingFileHandler(
        logs_dir / "orchestrator.log",
        maxBytes=10*1024*1024,  # 10MB
        backupCount=5
    )
    file_handler.setLevel(logging.DEBUG)
    file_fmt = logging.Formatter(
        "[%(asctime)s] [%(name)s] %(levelname)s [%(funcName)s:%(lineno)d]: %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S"
    )
    file_handler.setFormatter(file_fmt)
    logger.addHandler(file_handler)

    return logger


class DecompOrchestrator:
    """
    Main orchestrator for multi-agent decompilation.

    Manages:
    - Worktree pool for agent isolation
    - Agent spawning via Claude CLI
    - Progress tracking via database
    - Result collection and patch extraction
    """

    def __init__(
        self,
        db_path: str | Path = DEFAULT_DB_PATH,
        pool_dir: Path = DEFAULT_POOL_DIR,
        pool_size: int = 3,
        main_repo: Path | None = None,
        prompt_template_path: Path | None = None,
        logs_dir: Path = DEFAULT_LOGS_DIR,
        auto_apply: bool = False,
        auto_apply_min_progress: float = 0.0,
    ):
        self.db_path = str(db_path)
        self.pool_dir = pool_dir
        self.pool_size = pool_size
        self.main_repo = main_repo or Path.cwd()
        self.prompt_template_path = prompt_template_path or (
            self.main_repo / "scripts" / "master_agent_prompt.md"
        )
        self.logs_dir = Path(logs_dir)

        # Initialize logging
        self.logger = _setup_logging(self.logs_dir)

        self.worktree_pool = WorktreePool(
            main_repo=self.main_repo,
            pool_dir=pool_dir,
            pool_size=pool_size,
            db_path=db_path,
        )

        # Patch applier for auto-applying agent progress to main repo
        self.patch_applier = PatchApplier(
            main_repo=self.main_repo,
            enabled=auto_apply,
            min_progress=auto_apply_min_progress,
            require_improvement=True,
            allow_partial=True,
        )

        # Active sessions: session_id -> asyncio.Task
        self.active_sessions: dict[str, asyncio.Task] = {}

    async def _check_quota(self, model: str = "haiku") -> None:
        """Verify we have API quota remaining before launching agents.

        Runs a minimal Claude CLI probe and checks for rate limit messages.
        Raises RuntimeError if quota is exhausted.
        """
        cli_model = get_model_id(model)

        agent_home = Path(os.environ.get("AGENT_HOME", "/home/free/code/milohax/dc3-decomp/agent-home"))
        agent_home.mkdir(parents=True, exist_ok=True)

        env = {
            **os.environ,
            "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC": "1",
            "HOME": str(agent_home),
        }

        # Add auth env (OpenRouter or Anthropic OAuth)
        env.update(self._get_auth_env(model))

        cmd = [
            "claude",
            "--print",
            "--model", cli_model,
            "--max-turns", "1",
            "--dangerously-skip-permissions",
            "--no-session-persistence",
            "Reply with exactly: ok",
        ]

        self.logger.info("Running quota preflight check...")
        process = await asyncio.create_subprocess_exec(
            *cmd,
            cwd=self.main_repo,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
            env=env,
        )

        output_lines = []
        async for line in process.stdout:
            output_lines.append(line.decode("utf-8", errors="replace"))
        await process.wait()

        output = "".join(output_lines)

        if "hit your limit" in output.lower() or "you've hit your limit" in output.lower():
            raise RuntimeError(
                f"Quota exhausted - Claude API reports rate limit reached. "
                f"Wait for quota to reset before running agents.\n"
                f"Probe output: {output.strip()[:200]}"
            )

        if process.returncode != 0:
            self.logger.warning(
                f"Quota check exited with code {process.returncode}, "
                f"but no rate limit detected. Proceeding."
            )

        self.logger.info("Quota check passed.")

    def initialize(self, force: bool = False) -> None:
        """Initialize worktree pool. Call before running agents."""
        print(f"Initializing worktree pool at {self.pool_dir}...")
        self.worktree_pool.initialize(force=force)
        status = self.worktree_pool.status()
        print(f"Pool ready: {status['available']} worktrees available")

    def _load_prompt_template(self) -> str:
        """Load the prompt template."""
        with open(self.prompt_template_path) as f:
            return f.read()

    def _build_prompt(self, func: dict[str, Any], use_incremental: bool = True, worktree_dir: Optional[str] = None, context: Optional[dict[str, Any]] = None) -> str:
        """Build prompt from template for a specific function.

        Args:
            func: Function dict from database
            use_incremental: Include build strategy hint in prompt
            worktree_dir: Path to worktree directory (for context collection)
            context: Pre-computed context dict from collect_pre_run_context

        Returns:
            Formatted prompt string
        """
        template = self._load_prompt_template()

        percent = func.get("current_percent")
        percent_str = f"{percent:.1f}" if percent is not None else "unimplemented"

        # Handle pre-computed context (default to empty dict if not provided)
        context = context or {}

        # Add build strategy hint to prompt
        build_hint = ""
        if use_incremental:
            build_hint = "\n**Build Strategy:** Incremental (fast, 2-4s per build). Use `./bin/analyze-function` for validation between changes.\n"
        else:
            build_hint = "\n**Build Strategy:** Full build (comprehensive, ~88s per build). Use for final validation after completion.\n"

        return template.format(
            symbol=func["symbol"],
            demangled=func.get("demangled") or func["symbol"],
            unit=func.get("unit") or "unknown",
            percent=percent_str,
            match_percent=f"{context.get('match_percent', '(unknown)')}",
            verdict=context.get('verdict', '(unavailable)'),
            key_patterns=", ".join(context.get('key_patterns', [])) if context.get('key_patterns') else '(unavailable)',
            previous_attempts=context.get('previous_attempts', 'No previous attempts'),
            previous_attempts_count=context.get('previous_attempts_count', 0),
            ghidra_decompilation=context.get('decompilation', '(unavailable)'),
            rb3_reference=context.get('rb3_reference', '(not available)'),
            m2c_decompilation=context.get('m2c_decompilation', '(not run)'),
            m2c_file_path=context.get('m2c_file_path', '(not written)'),
            m2c_file_path_relative=context.get('m2c_file_path_relative', '(not written)'),
            m2c_line_count=context.get('m2c_line_count', 0),
            xrefs_path_absolute=context.get('xrefs_path_absolute', '(unavailable)'),
            xrefs_path_relative=context.get('xrefs_path_relative', '(unavailable)'),
            xrefs_preview=context.get('xrefs_preview', '(unavailable)'),
            # Source file absolute path
            source_file_absolute=context.get('source_file_absolute', '(unknown)'),
            # Pre-computed objdiff output
            objdiff_file=context.get('objdiff_file', '(unavailable)'),
            objdiff_file_absolute=context.get('objdiff_file_absolute', '(unavailable)'),
            objdiff_line_count=context.get('objdiff_line_count', 0),
            objdiff_preview=context.get('objdiff_preview', '(unavailable)'),
            # Worktree location (for agents to pass to MCP tools)
            worktree_dir=worktree_dir or '(unknown)',
        ) + build_hint

    def run_single_sync(
        self,
        symbol: str,
        model: Optional[str] = None,
        verbose: bool = True,
        dry_run: bool = False,
        use_incremental: bool = True,
    ) -> dict[str, Any]:
        """
        Run single agent on one function (synchronous wrapper).

        Args:
            symbol: Function symbol to work on
            model: Force specific model (haiku, sonnet, opus)
            verbose: Print agent output
            dry_run: Don't actually run agent, just show what would happen
            use_incremental: Use incremental build if True, full build if False

        Returns:
            Result dict with status, percent, patch, etc.
        """
        return asyncio.run(self.run_single(symbol, model, verbose, dry_run, use_incremental))

    async def run_single(
        self,
        symbol: str,
        model: Optional[str] = None,
        verbose: bool = True,
        dry_run: bool = False,
        use_incremental: bool = True,
        session_id: Optional[str] = None,
        pre_locked: bool = False,
    ) -> dict[str, Any]:
        """
        Run single agent on one function.

        Args:
            symbol: Function symbol to work on
            model: Force specific model (haiku, sonnet, opus)
            verbose: Print agent output
            dry_run: Don't actually run agent, just show what would happen
            use_incremental: Use incremental build if True, full build if False
            session_id: Optional session ID (generated if not provided)
            pre_locked: If True, skip locking (already locked by caller)

        Returns:
            Result dict with status, percent, patch, etc.
        """
        # 1. Get function from database
        func = get_function_by_symbol(symbol, db_path=self.db_path)
        if not func:
            self.logger.error(f"Function not found in database: {symbol}")
            raise ValueError(f"Function not found: {symbol}")

        # 1b. Preflight quota check
        await self._check_quota(model or "haiku")

        self.logger.info(f"Starting agent session for symbol: {symbol}")
        self.logger.debug(f"Function details: demangled={func.get('demangled')}, unit={func.get('unit')}, current_percent={func.get('current_percent')}%")

        # 2. Generate session ID if not provided
        if session_id is None:
            session_id = f"single-{func['id']}-{datetime.now().strftime('%H%M%S')}"

        # 3. Lock function (skip if pre_locked)
        if not pre_locked:
            if not lock_function(func["id"], session_id, db_path=self.db_path):
                raise RuntimeError(f"Could not lock function (already locked?): {symbol}")

        # 4. Acquire worktree
        worktree = self.worktree_pool.acquire(session_id)
        if worktree is None:
            unlock_function(func["id"], db_path=self.db_path)
            raise RuntimeError("No worktrees available")

        try:
            # 5. Select model
            selected_model = select_model(func, force_model=model)
            reason = get_escalation_reason(func, selected_model)

            self.logger.info(f"Model selected: {selected_model} (reason: {reason})")
            self.logger.debug(f"Worktree: {worktree}, Build strategy: {'incremental' if use_incremental else 'full'}")

            if verbose:
                mode = "SDK" if (USE_SDK and SDK_AVAILABLE) else "subprocess"
                print(f"\n{'='*60}")
                print(f"Function: {func.get('demangled') or symbol}")
                print(f"Symbol:   {symbol}")
                print(f"Unit:     {func.get('unit') or 'unknown'}")
                print(f"Current:  {func.get('current_percent') or 'unimplemented'}%")
                print(f"Model:    {selected_model} ({reason})")
                print(f"Mode:     {mode}")
                print(f"Worktree: {worktree}")
                print(f"Session:  {session_id}")
                print(f"{'='*60}\n")

            # 6. Collect pre-run context
            context = {}
            try:
                self.logger.debug(f"Collecting pre-run context for {symbol}...")
                context = collect_pre_run_context(
                    symbol=func["symbol"],
                    unit=func.get("unit"),
                    project_dir=str(self.main_repo),
                    worktree_dir=str(worktree)
                )
                self.logger.debug(f"Context collected: {len(context)} fields")
                if context.get('verdict'):
                    self.logger.debug(f"Verdict: {context.get('verdict')}, Match: {context.get('match_percent')}%")
            except Exception as e:
                self.logger.warning(f"Failed to collect pre-run context: {e}")
                context = {}

            if dry_run:
                # Show context in dry-run output
                print("[DRY RUN] Pre-Computed Analysis Context:")
                print(f"  Match: {context.get('match_percent', '(unknown)')}%")
                print(f"  Verdict: {context.get('verdict', '(unavailable)')}")
                if context.get('key_patterns'):
                    print(f"  Patterns: {', '.join(context.get('key_patterns', []))}")
                if context.get('previous_attempts_count', 0) > 0:
                    print(f"  Previous attempts: {context.get('previous_attempts_count')}")
                # RB3 reference
                rb3_ref = context.get('rb3_reference', '(not available)')
                if rb3_ref and not rb3_ref.startswith('('):
                    rb3_lines = len(rb3_ref.split('\n'))
                    print(f"  RB3 reference: {rb3_lines} lines")
                else:
                    print(f"  RB3 reference: {rb3_ref}")
                # m2c decompilation
                m2c_path = context.get('m2c_file_path_relative', '(not written)')
                m2c_lines = context.get('m2c_line_count', 0)
                if m2c_path and not m2c_path.startswith('('):
                    print(f"  m2c output: {m2c_path} ({m2c_lines} lines)")
                else:
                    m2c_msg = context.get('m2c_decompilation', '(not run)')
                    if m2c_msg.startswith('('):
                        print(f"  m2c output: {m2c_msg}")
                # Xrefs
                if context.get('xrefs_path_relative') != '(unavailable)':
                    print(f"  Xrefs: {context.get('xrefs_path_relative')}")
                # objdiff output file
                objdiff_file = context.get('objdiff_file', '(unavailable)')
                objdiff_lines = context.get('objdiff_line_count', 0)
                if objdiff_file and not objdiff_file.startswith('('):
                    print(f"  objdiff output: {objdiff_file} ({objdiff_lines} lines)")
                print()
                return {
                    "status": "dry_run",
                    "function": func,
                    "model": selected_model,
                    "worktree": str(worktree),
                    "context": context,
                }

            # 7. Build prompt (with pre-computed context injected)
            prompt = self._build_prompt(func, use_incremental=use_incremental, worktree_dir=str(worktree), context=context)

            # Check prompt size against token budget (~40k tokens / ~120k chars)
            # SDK passes prompt as CLI argument: claude --print -- <prompt>
            prompt_size = len(prompt.encode('utf-8'))

            if prompt_size > TOTAL_CHAR_BUDGET:
                # Prompt exceeds budget - this shouldn't happen if context_collector worked correctly
                # Log error and truncate to prevent ARG_MAX failure
                self.logger.error(
                    f"Prompt exceeds budget: {prompt_size / 1024:.1f}KB > {TOTAL_CHAR_BUDGET / 1024:.1f}KB. "
                    f"Truncating to prevent ARG_MAX failure."
                )
                # Emergency truncation - keep first TOTAL_CHAR_BUDGET chars
                prompt = prompt[:TOTAL_CHAR_BUDGET]
                prompt += "\n\n[PROMPT TRUNCATED - exceeded token budget]"
            elif prompt_size > TOTAL_CHAR_BUDGET * 0.8:  # 80% threshold warning
                self.logger.warning(
                    f"Prompt approaching budget: {prompt_size / 1024:.1f}KB "
                    f"({prompt_size * 100 // TOTAL_CHAR_BUDGET}% of {TOTAL_CHAR_BUDGET / 1024:.0f}KB budget)"
                )
            else:
                self.logger.debug(f"Prompt size: {prompt_size / 1024:.1f}KB ({prompt_size * 100 // TOTAL_CHAR_BUDGET}% of budget)")

            # 8. Run agent via SDK or subprocess
            start_percent = func.get("current_percent") or 0

            if USE_SDK and SDK_AVAILABLE:
                # Use Python SDK (preferred)
                result = await self._run_agent_sdk(
                    session_id=session_id,
                    worktree=worktree,
                    prompt=prompt,
                    model=selected_model,
                    verbose=verbose,
                    use_incremental=use_incremental,
                )
                # 9. Extract patch
                patch = self.worktree_pool.extract_patch(session_id)
                # 10. Parse result from SDK messages
                parsed = self._parse_agent_result_sdk(result.get("messages", []))
            else:
                # Fallback to subprocess (legacy)
                result = await self._run_agent_process(
                    session_id=session_id,
                    worktree=worktree,
                    prompt=prompt,
                    model=selected_model,
                    verbose=verbose,
                    use_incremental=use_incremental,
                )
                # 9. Extract patch
                patch = self.worktree_pool.extract_patch(session_id)
                # 10. Parse result from agent output
                parsed = self._parse_agent_result(result.get("output", ""))

            end_percent = parsed.get("percent", start_percent)
            exit_status = parsed.get("status", "unknown")
            notes = parsed.get("notes", "")
            verdict = parsed.get("verdict")

            # Extract usage data (only available from SDK path)
            usage_data = parsed.get("usage") or {}
            actual_cost_usd = parsed.get("total_cost_usd")
            duration_ms = parsed.get("duration_ms")

            self.logger.info(f"Agent result: status={exit_status}, percent={start_percent}% → {end_percent}%, verdict={verdict}")
            self.logger.debug(f"Notes from agent: {notes[:100]}..." if len(notes) > 100 else f"Notes from agent: {notes}")
            if patch:
                self.logger.debug(f"Patch size: {len(patch)} bytes")
            if actual_cost_usd is not None:
                self.logger.debug(f"Actual cost: ${actual_cost_usd:.4f}, duration: {duration_ms}ms")
                if usage_data:
                    self.logger.debug(f"Tokens: in={usage_data.get('input_tokens')}, out={usage_data.get('output_tokens')}, cache_read={usage_data.get('cache_read_tokens')}")

            # 11. Record attempt
            self.logger.debug(f"Recording attempt to database...")
            record_attempt(
                function_id=func["id"],
                session_id=session_id,
                model=selected_model,
                start_percent=start_percent,
                end_percent=end_percent,
                exit_status=exit_status,
                verdict=verdict,
                patch=patch,
                notes=notes,
                input_tokens=usage_data.get("input_tokens"),
                output_tokens=usage_data.get("output_tokens"),
                cache_read_tokens=usage_data.get("cache_read_tokens"),
                cache_creation_tokens=usage_data.get("cache_creation_tokens"),
                actual_cost_usd=actual_cost_usd,
                duration_ms=duration_ms,
                enrichment_flags=context.get("enrichment_flags"),
                db_path=self.db_path,
            )

            # 12. Update function status
            self.logger.debug(f"Updating function status in database...")
            update_function_status(
                function_id=func["id"],
                current_percent=end_percent,
                verdict=verdict,
                source_patch=patch if exit_status == "complete" else None,
                db_path=self.db_path,
            )

            # 13. Auto-apply patch to main repo if enabled and there was progress
            apply_result = self.patch_applier.maybe_apply(
                patch=patch,
                start_percent=start_percent,
                end_percent=end_percent if end_percent is not None else start_percent,
                exit_status=exit_status,
                symbol=symbol,
            )

            if verbose:
                print(f"\n{'='*60}")
                print(f"Result: {exit_status}")
                print(f"Match:  {start_percent}% → {end_percent}%")
                if verdict:
                    print(f"Verdict: {verdict}")
                if patch:
                    print(f"Patch:  {len(patch)} bytes")
                if apply_result.get("applied"):
                    print(f"Auto-applied: {apply_result['message']}")
                # Display actual cost if available
                if actual_cost_usd is not None:
                    print(f"Cost:   ${actual_cost_usd:.4f}")
                    if duration_ms:
                        print(f"Time:   {duration_ms / 1000:.1f}s")
                    if usage_data.get("input_tokens") or usage_data.get("output_tokens"):
                        in_tok = usage_data.get("input_tokens", 0) or 0
                        out_tok = usage_data.get("output_tokens", 0) or 0
                        cache_read = usage_data.get("cache_read_tokens", 0) or 0
                        print(f"Tokens: {in_tok:,} in / {out_tok:,} out / {cache_read:,} cache")
                print(f"{'='*60}\n")

            return {
                "status": exit_status,
                "start_percent": start_percent,
                "end_percent": end_percent,
                "verdict": verdict,
                "patch": patch,
                "notes": notes,
                "model": selected_model,
                "session_id": session_id,
                "patch_applied": apply_result.get("applied", False),
                # Actual cost tracking (may be None for subprocess/MCP paths)
                "actual_cost_usd": actual_cost_usd,
                "duration_ms": duration_ms,
                "usage": usage_data if usage_data else None,
            }

        finally:
            # 14. Cleanup (only unlock if we did the locking)
            if not pre_locked:
                unlock_function(func["id"], db_path=self.db_path)
            self.worktree_pool.release(session_id)

    async def _run_agent_process(
        self,
        session_id: str,
        worktree: Path,
        prompt: str,
        model: str,
        verbose: bool = True,
        max_turns: int = 300,
        use_incremental: bool = True,
    ) -> dict[str, Any]:
        """
        Run Claude CLI agent as subprocess.

        Args:
            session_id: Unique session identifier
            worktree: Working directory for agent
            prompt: Initial prompt for agent
            model: Model to use (haiku, sonnet, opus)
            verbose: Stream output to stdout
            max_turns: Maximum conversation turns
            use_incremental: Use incremental build if True, full build if False

        Returns:
            Dict with exit_code, output, etc.
        """
        # Get model ID for current backend (Anthropic or OpenRouter)
        cli_model = get_model_id(model)

        # Build command
        # Note: Token budgets (max_thinking_tokens, etc.) are enforced at the API level
        # by OpenRouter/Anthropic, and are passed to the SDK via ClaudeCodeOptions.
        # The CLI subprocess path does not expose these token controls.
        cmd = [
            "claude",
            "--print",  # Print conversation to stdout
            "--model", cli_model,
            "--max-turns", str(max_turns),
            "--dangerously-skip-permissions",  # Auto-approve tool use
            "--no-session-persistence",  # Don't write session files (sandbox-safe)
            prompt,
        ]

        if verbose:
            build_str = "incremental" if use_incremental else "full"
            print(f"[{session_id}] Starting agent with model {cli_model} ({build_str} build)...")

        # Build environment with OAuth token if available
        # Set HOME to writable location so Claude CLI can write .claude.json etc
        agent_home = Path(os.environ.get("AGENT_HOME", "/home/free/code/milohax/dc3-decomp/agent-home"))
        agent_home.mkdir(parents=True, exist_ok=True)

        # Use Claude-specific proxy ports if available (sandbox-friendly)
        http_proxy_port = os.environ.get("CLAUDE_CODE_HOST_HTTP_PROXY_PORT")
        socks_proxy_port = os.environ.get("CLAUDE_CODE_HOST_SOCKS_PROXY_PORT")

        env = {
            **os.environ,
            "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC": "1",
            "HOME": str(agent_home),
        }

        # Override proxy settings to use Claude-specific ports
        if http_proxy_port:
            env["HTTP_PROXY"] = f"http://localhost:{http_proxy_port}"
            env["HTTPS_PROXY"] = f"http://localhost:{http_proxy_port}"
            env["http_proxy"] = f"http://localhost:{http_proxy_port}"
            env["https_proxy"] = f"http://localhost:{http_proxy_port}"
        if socks_proxy_port:
            env["ALL_PROXY"] = f"socks5h://localhost:{socks_proxy_port}"
            env["all_proxy"] = f"socks5h://localhost:{socks_proxy_port}"

        if _get_openrouter_enabled() and _get_openrouter_api_key():
            # Use OpenRouter backend
            env["ANTHROPIC_BASE_URL"] = _get_openrouter_base_url()
            env["ANTHROPIC_API_KEY"] = _get_openrouter_api_key()
            if verbose:
                print(f"[{session_id}] Using OpenRouter backend at {_get_openrouter_base_url()}")
        else:
            # Use native Anthropic backend (default)
            oauth_token = get_oauth_token()
            if oauth_token:
                env["CLAUDE_CODE_OAUTH_TOKEN"] = oauth_token

        # Run subprocess
        process = await asyncio.create_subprocess_exec(
            *cmd,
            cwd=worktree,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
            env=env,
        )

        # Collect output
        output_lines = []

        async for line in process.stdout:
            decoded = line.decode("utf-8", errors="replace")
            output_lines.append(decoded)
            if verbose:
                print(decoded, end="")

        await process.wait()

        output = "".join(output_lines)

        return {
            "exit_code": process.returncode,
            "output": output,
            "session_id": session_id,
        }

    def _parse_agent_result(self, output: str) -> dict[str, Any]:
        """
        Parse agent output to extract result.

        Looks for report_result MCP tool call or final state.
        """
        result = {
            "status": "unknown",
            "percent": None,
            "notes": "",
            "verdict": None,
        }

        # Look for report_result JSON in output
        # Pattern: {"_decomp_exit": true, "status": "...", ...}
        json_pattern = r'\{[^{}]*"_decomp_exit"[^{}]*\}'
        matches = re.findall(json_pattern, output, re.DOTALL)

        if matches:
            try:
                # Take the last match (final result)
                data = json.loads(matches[-1])
                result["status"] = data.get("status", "unknown")
                result["percent"] = data.get("percent")
                result["notes"] = data.get("notes", "")
                self.logger.debug(f"Parsed report_result MCP call: status={result['status']}, percent={result['percent']}")
            except json.JSONDecodeError as e:
                self.logger.warning(f"Failed to parse JSON from agent output: {e}")
                pass

        # Also look for verdict in objdiff output - only match valid verdict values
        # Valid verdicts (underscore or camelCase): COMPLETE, AT_LIMIT, LIKELY_FIXABLE, MAYBE_FIXABLE, UNKNOWN, NEEDS_INVESTIGATION
        verdict_pattern = r'verdict[:\s]+(COMPLETE|AT_LIMIT|LIKELY_FIXABLE|MAYBE_FIXABLE|UNKNOWN|NEEDS_INVESTIGATION|NeedsInvestigation|LikelyFixable|MaybeFixable|AtLimit)'
        verdict_matches = re.findall(verdict_pattern, output, re.IGNORECASE)
        if verdict_matches:
            # Normalize to uppercase with underscores
            verdict = verdict_matches[-1].upper()
            verdict = re.sub(r'([a-z])([A-Z])', r'\1_\2', verdict_matches[-1]).upper()
            result["verdict"] = verdict
            self.logger.debug(f"Parsed verdict from objdiff: {result['verdict']}")

        # Look for percentage in objdiff output
        percent_pattern = r'(\d+\.?\d*)\s*%\s*match'
        percent_matches = re.findall(percent_pattern, output, re.IGNORECASE)
        if percent_matches and result["percent"] is None:
            try:
                result["percent"] = float(percent_matches[-1])
                self.logger.debug(f"Parsed percentage from objdiff: {result['percent']}%")
            except ValueError:
                pass

        # Also look for new RESULT format from agent prompt
        # RESULT: complete
        # PERCENT: 100.0
        # NOTES: Fixed comparison
        result_pattern = r'RESULT:\s*(\w+)'
        result_matches = re.findall(result_pattern, output, re.IGNORECASE)
        if result_matches:
            result["status"] = result_matches[-1].lower()
            self.logger.debug(f"Parsed RESULT line format: status={result['status']}")

        percent_result_pattern = r'PERCENT:\s*([\d.]+)'
        percent_result_matches = re.findall(percent_result_pattern, output, re.IGNORECASE)
        if percent_result_matches:
            try:
                result["percent"] = float(percent_result_matches[-1])
                self.logger.debug(f"Parsed PERCENT line format: {result['percent']}%")
            except ValueError:
                pass

        notes_pattern = r'NOTES:\s*(.+?)(?:\n|$)'
        notes_matches = re.findall(notes_pattern, output, re.IGNORECASE)
        if notes_matches:
            result["notes"] = notes_matches[-1].strip()
            self.logger.debug(f"Parsed NOTES line format: {result['notes'][:50]}...")

        return result

    def _get_auth_env(self, model: str = None) -> dict[str, str]:
        """Build auth environment for SDK/subprocess.

        Returns environment variables for API authentication.
        SDK auto-detects OAuth credentials, but we still need to set
        OpenRouter environment variables when that backend is enabled
        or when the model requires OpenRouter.

        Per OpenRouter docs (https://openrouter.ai/docs/guides/guides/claude-code-integration):
        - ANTHROPIC_BASE_URL: https://openrouter.ai/api (not /api/v1)
        - ANTHROPIC_AUTH_TOKEN: OpenRouter API key
        - ANTHROPIC_API_KEY: Must be empty string
        - ANTHROPIC_DEFAULT_*_MODEL: Override for non-Anthropic models

        Args:
            model: Optional model tier. If OpenRouter-only, uses OpenRouter credentials.

        Returns:
            Dict of environment variables for auth
        """
        env: dict[str, str] = {}

        # Use OpenRouter if explicitly enabled OR if model requires it
        use_openrouter = _get_openrouter_enabled() or (model and requires_openrouter(model))

        if use_openrouter and _get_openrouter_api_key():
            # OpenRouter backend - use correct env vars per their docs
            env["ANTHROPIC_BASE_URL"] = _get_openrouter_base_url()
            env["ANTHROPIC_AUTH_TOKEN"] = _get_openrouter_api_key()
            env["ANTHROPIC_API_KEY"] = ""  # Must be explicitly empty

            # For non-Anthropic models, override the default model alias
            # Claude Code CLI uses aliases (sonnet, haiku, opus) - we override via env
            if model and requires_openrouter(model):
                openrouter_model_id = get_model_id(model)
                # Override sonnet alias (the default tier we use)
                env["ANTHROPIC_DEFAULT_SONNET_MODEL"] = openrouter_model_id
        # Anthropic: SDK auto-reads ~/.claude/.credentials.json

        return env

    async def _run_agent_sdk(
        self,
        session_id: str,
        worktree: Path,
        prompt: str,
        model: str,
        verbose: bool = True,
        max_turns: int = 300,
        use_incremental: bool = True,
    ) -> dict[str, Any]:
        """
        Run Claude agent via Python SDK.

        Uses claude-code-sdk for direct API access with structured
        message types instead of subprocess/stdout parsing.

        Args:
            session_id: Unique session identifier
            worktree: Working directory for agent
            prompt: Initial prompt for agent
            model: Model to use (haiku, sonnet, opus)
            verbose: Stream output to stdout
            max_turns: Maximum conversation turns
            use_incremental: Use incremental build if True, full build if False

        Returns:
            Dict with exit_code, messages list, output string, etc.
        """
        if not SDK_AVAILABLE:
            raise RuntimeError("claude-code-sdk not installed. Run: pip install claude-code-sdk")

        # Get model ID for CLI
        # For OpenRouter-only models, use "sonnet" alias - the actual model is set via
        # ANTHROPIC_DEFAULT_SONNET_MODEL env var in _get_auth_env()
        if requires_openrouter(model):
            cli_model = "sonnet"  # Alias gets overridden by env var
        else:
            cli_model = get_model_id(model)

        # Get thinking token budget for models that support it
        thinking_tokens = get_token_budget(model)

        if verbose:
            build_str = "incremental" if use_incremental else "full"
            # Show actual model ID (not just CLI alias)
            actual_model = get_model_id(model)
            print(f"[{session_id}] Starting agent (SDK) with model {actual_model} ({build_str} build)...")

        # Build environment with OAuth token if available
        # Set HOME to writable location so Claude CLI can write .claude.json etc
        agent_home = Path(os.environ.get("AGENT_HOME", "/home/free/code/milohax/dc3-decomp/agent-home"))
        agent_home.mkdir(parents=True, exist_ok=True)

        # Use Claude-specific proxy ports if available (sandbox-friendly)
        http_proxy_port = os.environ.get("CLAUDE_CODE_HOST_HTTP_PROXY_PORT")
        socks_proxy_port = os.environ.get("CLAUDE_CODE_HOST_SOCKS_PROXY_PORT")

        env = {
            "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC": "1",
            "HOME": str(agent_home),
        }

        # Override proxy settings to use Claude-specific ports
        if http_proxy_port:
            env["HTTP_PROXY"] = f"http://localhost:{http_proxy_port}"
            env["HTTPS_PROXY"] = f"http://localhost:{http_proxy_port}"
            env["http_proxy"] = f"http://localhost:{http_proxy_port}"
            env["https_proxy"] = f"http://localhost:{http_proxy_port}"
        if socks_proxy_port:
            env["ALL_PROXY"] = f"socks5h://localhost:{socks_proxy_port}"
            env["all_proxy"] = f"socks5h://localhost:{socks_proxy_port}"

        # Add auth environment
        env.update(self._get_auth_env(model))

        use_openrouter = _get_openrouter_enabled() or requires_openrouter(model)
        if verbose and use_openrouter and _get_openrouter_api_key():
            print(f"[{session_id}] Using OpenRouter backend at {_get_openrouter_base_url()}")

        # Configure MCP server for orchestrator tools
        mcp_config: McpStdioServerConfig = {
            "command": "python3",
            "args": ["-m", "scripts.orchestrator.mcp_server", "--db", str(self.db_path)],
            "env": {"PYTHONPATH": str(self.main_repo)},
        }

        # Build SDK options
        # Note: max_thinking_tokens is not supported in current ClaudeCodeOptions
        # Token budgets are enforced at the API level instead
        options = ClaudeCodeOptions(
            model=cli_model,
            max_turns=max_turns,
            cwd=str(worktree),
            env=env,
            permission_mode="bypassPermissions",
            mcp_servers={"orchestrator": mcp_config},
            allowed_tools=[
                "Read", "Write", "Edit", "Bash", "Glob", "Grep",
                "mcp__orchestrator__report_result",
                "mcp__orchestrator__query_functions",
                "mcp__orchestrator__get_attempts",
                "mcp__orchestrator__lookup_rb3",
                "mcp__orchestrator__run_objdiff",
            ],
            # Prevent agents from trying to spawn sub-agents (orchestrator handles this)
            disallowed_tools=["Task", "TaskOutput"],
            # Add main repo to allowed dirs so symlinks work in sandbox
            add_dirs=[str(self.main_repo)],
        )

        # Collect messages for parsing
        messages: list[Any] = []
        output_lines: list[str] = []

        try:
            async for message in sdk_query(prompt=prompt, options=options):
                messages.append(message)

                # Stream output if verbose
                if verbose:
                    if isinstance(message, AssistantMessage):
                        for block in message.content:
                            if isinstance(block, TextBlock):
                                print(block.text, end="")
                                output_lines.append(block.text)
                            elif isinstance(block, ToolUseBlock):
                                tool_str = f"\n[Tool: {block.name}]"
                                # Format input parameters
                                if hasattr(block, 'input') and block.input:
                                    input_parts = []
                                    for k, v in block.input.items():
                                        # Truncate long values
                                        v_str = str(v)
                                        if len(v_str) > 100:
                                            v_str = v_str[:100] + "..."
                                        input_parts.append(f"{k}={v_str}")
                                    if input_parts:
                                        tool_str += f" {', '.join(input_parts)}"
                                tool_str += "\n"
                                print(tool_str, end="")
                                output_lines.append(tool_str)
                    elif isinstance(message, UserMessage):
                        # Log tool results
                        for block in message.content:
                            if isinstance(block, ToolResultBlock):
                                # Format the result content
                                content = block.content
                                if content is None:
                                    content_str = "(no output)"
                                elif isinstance(content, str):
                                    content_str = content
                                else:
                                    content_str = str(content)

                                # Truncate long results
                                if len(content_str) > 200:
                                    content_str = content_str[:200] + "..."

                                error_marker = " ERROR" if block.is_error else ""
                                result_str = f"  → {content_str}{error_marker}\n"
                                print(result_str, end="")
                                output_lines.append(result_str)
                    elif isinstance(message, ResultMessage):
                        result_str = f"\n[Result: {message}]\n"
                        print(result_str, end="")
                        output_lines.append(result_str)

            return {
                "exit_code": 0,
                "messages": messages,
                "output": "".join(output_lines),
                "session_id": session_id,
            }

        except CLINotFoundError as e:
            error_msg = f"Claude CLI not found: {e}"
            if verbose:
                print(f"\n[{session_id}] Error: {error_msg}")
            return {"exit_code": 127, "error": error_msg, "messages": [], "output": ""}

        except ProcessError as e:
            error_msg = str(e)
            if verbose:
                print(f"\n[{session_id}] Process error: {error_msg}")
            return {
                "exit_code": e.exit_code if hasattr(e, 'exit_code') else 1,
                "error": error_msg,
                "messages": [],
                "output": getattr(e, 'stderr', ''),
            }

        except Exception as e:
            error_msg = str(e)
            if verbose:
                print(f"\n[{session_id}] Unexpected error: {error_msg}")
            return {"exit_code": 1, "error": error_msg, "messages": [], "output": ""}

    def _parse_agent_result_sdk(self, messages: list[Any]) -> dict[str, Any]:
        """
        Parse agent result from SDK messages.

        Primary method: Extract from ToolUseBlock with report_result call.
        Fallback: Use regex on text content (backward compat).

        Also extracts usage data from ResultMessage if present.

        Args:
            messages: List of SDK message objects

        Returns:
            Dict with status, percent, notes, verdict, and usage data
        """
        result = {
            "status": "unknown",
            "percent": None,
            "notes": "",
            "verdict": None,
            # Usage data from ResultMessage (may be None for non-SDK paths)
            "usage": None,
            "total_cost_usd": None,
            "duration_ms": None,
            "num_turns": None,
        }

        # Collect all text for fallback regex parsing
        all_text = []

        # Primary: Look for report_result tool call and ResultMessage
        for message in messages:
            if isinstance(message, AssistantMessage):
                for block in message.content:
                    if isinstance(block, ToolUseBlock):
                        if block.name == "mcp__orchestrator__report_result":
                            # Direct structured access - no regex needed!
                            input_data = block.input
                            result["status"] = input_data.get("status", "unknown")
                            result["percent"] = input_data.get("percent")
                            result["notes"] = input_data.get("notes", "")
                            # report_result found, but continue to collect text for verdict
                    elif isinstance(block, TextBlock):
                        all_text.append(block.text)
            elif isinstance(message, ResultMessage):
                # Extract usage data from ResultMessage
                result["total_cost_usd"] = message.total_cost_usd
                result["duration_ms"] = message.duration_ms
                result["num_turns"] = message.num_turns

                # Extract token usage dict
                if message.usage:
                    result["usage"] = {
                        "input_tokens": message.usage.get("inputTokens"),
                        "output_tokens": message.usage.get("outputTokens"),
                        "cache_read_tokens": message.usage.get("cacheReadInputTokens"),
                        "cache_creation_tokens": message.usage.get("cacheCreationInputTokens"),
                    }

        # Combine text for regex fallback parsing
        combined_text = "\n".join(all_text)

        # Look for verdict in objdiff output (not in report_result)
        # Valid verdicts (underscore or camelCase): COMPLETE, AT_LIMIT, LIKELY_FIXABLE, MAYBE_FIXABLE, UNKNOWN, NEEDS_INVESTIGATION
        verdict_pattern = r'verdict[:\s]+(COMPLETE|AT_LIMIT|LIKELY_FIXABLE|MAYBE_FIXABLE|UNKNOWN|NEEDS_INVESTIGATION|NeedsInvestigation|LikelyFixable|MaybeFixable|AtLimit)'
        verdict_matches = re.findall(verdict_pattern, combined_text, re.IGNORECASE)
        if verdict_matches:
            # Normalize to uppercase with underscores
            verdict = re.sub(r'([a-z])([A-Z])', r'\1_\2', verdict_matches[-1]).upper()
            result["verdict"] = verdict

        # Fallback: Look for percentage in text if not found in tool call
        if result["percent"] is None:
            percent_pattern = r'(\d+\.?\d*)\s*%\s*match'
            percent_matches = re.findall(percent_pattern, combined_text, re.IGNORECASE)
            if percent_matches:
                try:
                    result["percent"] = float(percent_matches[-1])
                except ValueError:
                    pass

        # Fallback: Look for RESULT/PERCENT/NOTES format if no tool call found
        if result["status"] == "unknown":
            result_pattern = r'RESULT:\s*(\w+)'
            result_matches = re.findall(result_pattern, combined_text, re.IGNORECASE)
            if result_matches:
                result["status"] = result_matches[-1].lower()

            percent_result_pattern = r'PERCENT:\s*([\d.]+)'
            percent_result_matches = re.findall(percent_result_pattern, combined_text, re.IGNORECASE)
            if percent_result_matches and result["percent"] is None:
                try:
                    result["percent"] = float(percent_result_matches[-1])
                except ValueError:
                    pass

            notes_pattern = r'NOTES:\s*(.+?)(?:\n|$)'
            notes_matches = re.findall(notes_pattern, combined_text, re.IGNORECASE)
            if notes_matches and not result["notes"]:
                result["notes"] = notes_matches[-1].strip()

        return result

    async def run_batch(
        self,
        pattern: str | list[str] = "*",
        min_percent: float = 0,
        max_percent: float = 100,
        max_agents: int = 3,
        model: Optional[str] = None,
        limit: int = 0,
        verbose: bool = True,
        use_incremental: bool = True,
        periodic_full_interval: int = 10,
        validate_diffs: bool = False,
    ) -> dict[str, Any]:
        """
        Run batch of functions matching pattern with N parallel agents.

        Supports incremental builds with periodic full build validation:
        - Default: incremental builds (2-4s each)
        - Every Nth batch: run full build for validation (~88s)
        - Tracks metrics separately for each strategy

        Args:
            pattern: Glob pattern(s) for unit (e.g., "src/system/char/*" or list of patterns)
            min_percent: Minimum match percentage
            max_percent: Maximum match percentage
            max_agents: Maximum concurrent agents
            model: Force specific model for all
            limit: Max functions to process (0 = unlimited)
            verbose: Print progress
            use_incremental: Use incremental builds by default (True) or full (False)
            periodic_full_interval: Run full build every Nth batch (0 = disabled)
            validate_diffs: Validate diffs between incremental and full builds

        Returns:
            Summary dict with results
        """
        # Query batch targeting stats
        batch_stats = query_batch_stats(
            pattern=pattern,
            min_percent=min_percent,
            max_percent=max_percent,
            limit=limit,
            db_path=self.db_path,
        )

        display_pattern = pattern if isinstance(pattern, str) else ", ".join(pattern)
        print(f"\n{'='*60}")
        print(f"Starting batch: {display_pattern}")
        print(f"Match range: {min_percent}% - {max_percent}%")
        print(f"Max agents: {max_agents}")
        if model:
            print(f"Forced model: {model}")
        print(f"{'='*60}")

        # Display targeting breakdown
        print(f"\nTargeting:")
        print(f"  Pattern matches: {batch_stats['total_matching_pattern']} functions in DB")
        print(f"  In range ({min_percent}%-{max_percent}%): {batch_stats['in_match_range']}")
        if batch_stats['locked'] > 0:
            print(f"  Locked: {batch_stats['locked']} (skipped)")
        if batch_stats['excluded_complete'] > 0:
            print(f"  Complete/at-limit: {batch_stats['excluded_complete']} (excluded)")
        print(f"  Available: {batch_stats['available']}")
        print(f"    - First tries: {batch_stats['first_tries']} (no prior attempts)")
        print(f"    - Retries: {batch_stats['retries']} (have prior attempts)")

        # Show selection info
        if limit > 0:
            if batch_stats['more_available']:
                print(f"\n  Selected: {batch_stats['selected']} of {batch_stats['available']} (limit: {limit})")
            else:
                print(f"\n  Selected: {batch_stats['selected']} (all available, limit was {limit})")
        else:
            print(f"\n  Selected: {batch_stats['selected']} (no limit)")

        print()

        # Ensure pool is initialized
        if self.worktree_pool.status()["total"] == 0:
            self.initialize()

        # Preflight quota check before committing to batch
        await self._check_quota(model or "haiku")

        results = []
        processed = 0
        errors = 0
        batch_count = 0
        batch_start_time = datetime.now()
        build_metrics = {
            "incremental_count": 0,
            "full_count": 0,
            "incremental_time": 0.0,
            "full_time": 0.0,
        }

        while True:
            # Get next unlocked function
            func = get_next_function(
                pattern=pattern,
                min_percent=min_percent,
                max_percent=max_percent,
                db_path=self.db_path,
            )

            if not func:
                # No more work - wait for active agents to finish
                if not self.active_sessions:
                    break
                await self._wait_for_any_completion()
                continue

            # Check limit
            if limit > 0 and processed >= limit:
                break

            # Decide if this is a periodic full build validation
            current_use_incremental = use_incremental
            if periodic_full_interval > 0 and use_incremental:
                # Every Nth batch, switch to full build for validation
                if (processed + 1) % (max_agents * periodic_full_interval) == 0:
                    current_use_incremental = False
                    if verbose:
                        print(f"\n[Batch {batch_count}] Running full build validation...")

            # Wait if at max capacity
            while len(self.active_sessions) >= max_agents:
                result = await self._wait_for_any_completion()
                if result:
                    results.append(result)
                    if result.get("status") == "error":
                        errors += 1
                    batch_count += 1

            # Lock function BEFORE spawning to prevent race conditions
            session_id = f"batch-{func['id']}-{datetime.now().strftime('%H%M%S')}"
            if not lock_function(func["id"], session_id, db_path=self.db_path):
                # Already locked by another agent, skip and get next
                continue

            # Spawn agent asynchronously
            task = asyncio.create_task(
                self._run_batch_agent(
                    session_id, func, model, verbose, use_incremental=current_use_incremental
                )
            )
            self.active_sessions[session_id] = task
            processed += 1

            if verbose:
                build_str = "inc" if current_use_incremental else "full"
                print(f"[{len(self.active_sessions)}/{max_agents}] Spawned: {func.get('demangled') or func['symbol']} ({build_str})")

        # Wait for remaining agents
        while self.active_sessions:
            result = await self._wait_for_any_completion()
            if result:
                results.append(result)
                if result.get("status") == "error":
                    errors += 1

        # Generate summary
        summary = self._generate_batch_summary(results, pattern)
        summary["build_strategy"] = "incremental" if use_incremental else "full"
        summary["periodic_validation"] = periodic_full_interval if use_incremental else 0
        summary["build_metrics"] = build_metrics
        summary["auto_apply_stats"] = self.patch_applier.stats()

        if verbose:
            elapsed = (datetime.now() - batch_start_time).total_seconds()
            print(f"\n{'='*60}")
            print("Batch complete!")
            print(f"Processed: {len(results)} functions in {elapsed:.1f}s")
            print(f"Build strategy: {summary['build_strategy']}")
            if periodic_full_interval > 0 and use_incremental:
                print(f"Periodic full builds: Every {periodic_full_interval} batches")
            print(f"Errors: {errors}")
            if summary.get("improvements"):
                print(f"Improvements: {len(summary['improvements'])}")
                total_gain = sum(
                    imp["end_percent"] - imp["start_percent"]
                    for imp in summary["improvements"]
                )
                print(f"Total gain: +{total_gain:.1f}%")
            if summary.get("modified_files"):
                print(f"Modified files: {len(summary['modified_files'])}")
            # Show auto-apply stats if enabled
            apply_stats = summary["auto_apply_stats"]
            if apply_stats["applied"] > 0 or apply_stats["failed"] > 0:
                print(f"Auto-applied: {apply_stats['applied']} patches ({apply_stats['skipped']} skipped, {apply_stats['failed']} failed)")
            print(f"{'='*60}\n")

        return summary

    async def run_batch_with_targets(
        self,
        targets: list[dict[str, Any]],
        max_agents: int = 3,
        model: Optional[str] = None,
        verbose: bool = True,
        use_incremental: bool = True,
        periodic_full_interval: int = 10,
        validate_diffs: bool = False,
    ) -> dict[str, Any]:
        """
        Run batch on a pre-selected list of target functions.

        This is used by the priority and unit-completion strategies that
        select targets using the Phase 2 scoring infrastructure.

        Args:
            targets: List of function dicts (must have 'id', 'symbol' keys)
            max_agents: Maximum concurrent agents
            model: Force specific model for all
            verbose: Print progress
            use_incremental: Use incremental builds by default
            periodic_full_interval: Run full build every Nth batch (0 = disabled)
            validate_diffs: Validate diffs between incremental and full builds

        Returns:
            Summary dict with results
        """
        print(f"\n{'='*60}")
        print(f"Starting batch with {len(targets)} pre-selected targets")
        print(f"Max agents: {max_agents}")
        if model:
            print(f"Forced model: {model}")
        print(f"{'='*60}\n")

        # Ensure pool is initialized
        if self.worktree_pool.status()["total"] == 0:
            self.initialize()

        # Preflight quota check before committing to batch
        await self._check_quota(model or "haiku")

        results = []
        processed = 0
        errors = 0
        batch_count = 0
        batch_start_time = datetime.now()
        target_idx = 0

        while target_idx < len(targets) or self.active_sessions:
            # Get next target from pre-selected list
            func = None
            while target_idx < len(targets) and func is None:
                candidate = targets[target_idx]
                target_idx += 1

                # Check if still eligible (not locked, not complete)
                current = get_function_by_symbol(candidate["symbol"], db_path=self.db_path)
                if current is None:
                    continue
                if current.get("locked_by") is not None:
                    if verbose:
                        print(f"  Skipping (locked): {candidate.get('demangled') or candidate['symbol']}")
                    continue
                if current.get("verdict") in ("COMPLETE", "AT_LIMIT"):
                    if verbose:
                        print(f"  Skipping (complete): {candidate.get('demangled') or candidate['symbol']}")
                    continue

                func = current

            if func is None and not self.active_sessions:
                # No more targets and no active agents
                break

            if func is None:
                # No more targets but have active agents - wait for completion
                result = await self._wait_for_any_completion()
                if result:
                    results.append(result)
                    if result.get("status") == "error":
                        errors += 1
                    batch_count += 1
                continue

            # Decide if this is a periodic full build validation
            current_use_incremental = use_incremental
            if periodic_full_interval > 0 and use_incremental:
                if (processed + 1) % (max_agents * periodic_full_interval) == 0:
                    current_use_incremental = False
                    if verbose:
                        print(f"\n[Batch {batch_count}] Running full build validation...")

            # Wait if at max capacity
            while len(self.active_sessions) >= max_agents:
                result = await self._wait_for_any_completion()
                if result:
                    results.append(result)
                    if result.get("status") == "error":
                        errors += 1
                    batch_count += 1

            # Lock function BEFORE spawning to prevent race conditions
            session_id = f"batch-{func['id']}-{datetime.now().strftime('%H%M%S')}"
            if not lock_function(func["id"], session_id, db_path=self.db_path):
                # Already locked by another agent, skip
                continue

            # Spawn agent asynchronously
            task = asyncio.create_task(
                self._run_batch_agent(
                    session_id, func, model, verbose, use_incremental=current_use_incremental
                )
            )
            self.active_sessions[session_id] = task
            processed += 1

            if verbose:
                build_str = "inc" if current_use_incremental else "full"
                pct = func.get("current_percent") or 0
                print(f"[{len(self.active_sessions)}/{max_agents}] Spawned: {func.get('demangled') or func['symbol']} ({pct:.1f}%, {build_str})")

        # Wait for remaining agents
        while self.active_sessions:
            result = await self._wait_for_any_completion()
            if result:
                results.append(result)
                if result.get("status") == "error":
                    errors += 1

        # Generate summary
        summary = self._generate_batch_summary(results, f"<{len(targets)} priority targets>")
        summary["build_strategy"] = "incremental" if use_incremental else "full"
        summary["periodic_validation"] = periodic_full_interval if use_incremental else 0
        summary["auto_apply_stats"] = self.patch_applier.stats()
        summary["target_count"] = len(targets)
        summary["processed_count"] = processed

        if verbose:
            elapsed = (datetime.now() - batch_start_time).total_seconds()
            print(f"\n{'='*60}")
            print("Batch complete!")
            print(f"Processed: {processed}/{len(targets)} targets in {elapsed:.1f}s")
            print(f"Build strategy: {summary['build_strategy']}")
            print(f"Errors: {errors}")
            if summary.get("improvements"):
                print(f"Improvements: {len(summary['improvements'])}")
                total_gain = sum(
                    imp["end_percent"] - imp["start_percent"]
                    for imp in summary["improvements"]
                )
                print(f"Total gain: +{total_gain:.1f}%")
            if summary.get("modified_files"):
                print(f"Modified files: {len(summary['modified_files'])}")
            apply_stats = summary["auto_apply_stats"]
            if apply_stats["applied"] > 0 or apply_stats["failed"] > 0:
                print(f"Auto-applied: {apply_stats['applied']} patches ({apply_stats['skipped']} skipped, {apply_stats['failed']} failed)")
            print(f"{'='*60}\n")

        return summary

    async def _run_batch_agent(
        self,
        session_id: str,
        func: dict[str, Any],
        model: Optional[str],
        verbose: bool,
        use_incremental: bool = True,
    ) -> dict[str, Any]:
        """Run single agent as part of batch (handles its own errors).

        Args:
            session_id: Unique session identifier
            func: Function to work on
            model: Force specific model
            verbose: Print output
            use_incremental: Use incremental build
        """
        try:
            return await self.run_single(
                symbol=func["symbol"],
                model=model,
                verbose=verbose,
                use_incremental=use_incremental,
                session_id=session_id,
                pre_locked=True,  # Batch already locked the function
            )
        except Exception as e:
            print(f"[{session_id}] Error: {e}")
            return {
                "status": "error",
                "error": str(e),
                "symbol": func["symbol"],
                "function_id": func["id"],  # Need this for unlock
            }
        finally:
            # Batch mode: unlock the function when agent completes
            unlock_function(func["id"], db_path=self.db_path)

    async def _wait_for_any_completion(self) -> Optional[dict[str, Any]]:
        """Wait for any active agent to complete. Returns its result."""
        if not self.active_sessions:
            return None

        done, _ = await asyncio.wait(
            self.active_sessions.values(),
            return_when=asyncio.FIRST_COMPLETED,
        )

        result = None
        for task in done:
            # Find and remove the completed session
            for session_id, t in list(self.active_sessions.items()):
                if t == task:
                    del self.active_sessions[session_id]
                    try:
                        result = task.result()
                    except Exception as e:
                        result = {"status": "error", "error": str(e)}
                    break

        return result

    def _generate_batch_summary(
        self, results: list[dict[str, Any]], pattern: str
    ) -> dict[str, Any]:
        """Generate summary from batch results."""
        summary = {
            "pattern": pattern,
            "total": len(results),
            "complete": 0,
            "at_limit": 0,
            "stuck": 0,
            "error": 0,
            "improvements": [],
            "modified_files": set(),
        }

        for result in results:
            status = result.get("status", "unknown")
            if status == "complete":
                summary["complete"] += 1
            elif status == "at_limit":
                summary["at_limit"] += 1
            elif status == "stuck":
                summary["stuck"] += 1
            elif status == "error":
                summary["error"] += 1

            # Track improvements (handle None values and ensure float type)
            start = float(result.get("start_percent") or 0)
            end = float(result.get("end_percent") or 0)
            if end > 0 and start >= 0 and end > start:
                summary["improvements"].append({
                    "symbol": result.get("symbol"),
                    "start_percent": start,
                    "end_percent": end,
                    "gain": end - start,
                })

            # Track modified files
            if result.get("patch"):
                # Extract file paths from patch
                patch = result["patch"]
                for line in patch.split("\n"):
                    if line.startswith("+++ b/"):
                        summary["modified_files"].add(line[6:])

        summary["modified_files"] = list(summary["modified_files"])

        return summary

    def status(self) -> dict[str, Any]:
        """Get current orchestrator status."""
        db_stats = get_stats(self.db_path)
        pool_status = self.worktree_pool.status()

        return {
            "database": db_stats,
            "worktree_pool": pool_status,
            "active_sessions": len(self.active_sessions),
        }

    def cleanup_stale_locks(self, max_age_hours: int = 2) -> int:
        """Unlock functions that have been locked for too long."""
        conn = get_connection(self.db_path)
        cursor = conn.execute(
            """
            UPDATE functions
            SET locked_by = NULL, locked_at = NULL, updated_at = CURRENT_TIMESTAMP
            WHERE locked_by IS NOT NULL
              AND locked_at < datetime('now', ? || ' hours')
            """,
            (-max_age_hours,),
        )
        conn.commit()
        return cursor.rowcount

    def _load_rb3_merge_prompt_template(self) -> str:
        """Load the RB3-merge specialized prompt template."""
        rb3_prompt_path = self.main_repo / "scripts" / "rb3_merge_agent_prompt.md"
        if rb3_prompt_path.exists():
            with open(rb3_prompt_path) as f:
                return f.read()
        # Fall back to regular prompt if rb3-specific doesn't exist
        return self._load_prompt_template()

    def _build_rb3_merge_prompt(
        self,
        func: dict[str, Any],
        rb3_source: str,
        worktree_dir: Optional[str] = None,
        context: Optional[dict[str, Any]] = None,
    ) -> str:
        """Build RB3-merge prompt with full RB3 source included.

        Args:
            func: Function dict from database
            rb3_source: Full RB3 source code
            worktree_dir: Path to worktree directory
            context: Pre-computed context dict

        Returns:
            Formatted prompt string
        """
        template = self._load_rb3_merge_prompt_template()

        percent = func.get("current_percent")
        percent_str = f"{percent:.1f}" if percent is not None else "unimplemented"

        context = context or {}

        # Truncate RB3 source if extremely large
        if len(rb3_source) > 100000:
            rb3_source = rb3_source[:100000] + "\n\n... (truncated, file too large)"

        return template.format(
            symbol=func["symbol"],
            demangled=func.get("demangled") or func["symbol"],
            unit=func.get("unit") or "unknown",
            percent=percent_str,
            match_percent=f"{context.get('match_percent', '(unknown)')}",
            verdict=context.get('verdict', '(unavailable)'),
            key_patterns=", ".join(context.get('key_patterns', [])) if context.get('key_patterns') else '(unavailable)',
            previous_attempts=context.get('previous_attempts', 'No previous attempts'),
            rb3_reference=rb3_source,
            m2c_decompilation=context.get('m2c_decompilation', '(not run)'),
            m2c_file_path_relative=context.get('m2c_file_path_relative', '(not written)'),
            m2c_line_count=context.get('m2c_line_count', 0),
            ghidra_decompilation=context.get('decompilation', '(unavailable)'),
            source_file_absolute=context.get('source_file_absolute', '(unknown)'),
            objdiff_file=context.get('objdiff_file', '(unavailable)'),
            objdiff_file_absolute=context.get('objdiff_file_absolute', '(unavailable)'),
            objdiff_line_count=context.get('objdiff_line_count', 0),
            objdiff_preview=context.get('objdiff_preview', '(unavailable)'),
            worktree_dir=worktree_dir or '(unknown)',
        )

    async def run_rb3_merge_single(
        self,
        symbol: str,
        rb3_source: str,
        model: Optional[str] = None,
        verbose: bool = True,
        dry_run: bool = False,
        session_id: Optional[str] = None,
        pre_locked: bool = False,
    ) -> dict[str, Any]:
        """
        Run single RB3-merge agent on one function.

        Uses the specialized RB3-merge prompt with full RB3 source context.

        Args:
            symbol: Function symbol to work on
            rb3_source: Full RB3 source code
            model: Force specific model
            verbose: Print agent output
            dry_run: Don't run, just show what would happen
            session_id: Optional session ID (for batch mode)
            pre_locked: If True, function was already locked by caller (batch mode)

        Returns:
            Result dict with status, percent, patch, etc.
        """
        func = get_function_by_symbol(symbol, db_path=self.db_path)
        if not func:
            raise ValueError(f"Function not found: {symbol}")

        # Preflight quota check
        await self._check_quota(model or "haiku")

        if session_id is None:
            session_id = f"rb3merge-{func['id']}-{datetime.now().strftime('%H%M%S')}"

        # Lock function if not pre-locked by batch
        if not pre_locked:
            if not lock_function(func["id"], session_id, db_path=self.db_path):
                raise RuntimeError(f"Could not lock function: {symbol}")

        worktree = self.worktree_pool.acquire(session_id)
        if worktree is None:
            if not pre_locked:
                unlock_function(func["id"], db_path=self.db_path)
            raise RuntimeError("No worktrees available")

        try:
            selected_model = select_model(func, force_model=model)

            # Collect context
            context = {}
            try:
                context = collect_pre_run_context(
                    symbol=func["symbol"],
                    unit=func.get("unit"),
                    project_dir=str(self.main_repo),
                    worktree_dir=str(worktree)
                )
            except Exception as e:
                self.logger.warning(f"Failed to collect context: {e}")

            if dry_run:
                print(f"[DRY RUN] Would process {symbol} with RB3-merge")
                print(f"  RB3 source: {len(rb3_source)} characters")
                return {"status": "dry_run", "function": func}

            # Build RB3-specific prompt
            prompt = self._build_rb3_merge_prompt(
                func, rb3_source, worktree_dir=str(worktree), context=context
            )

            start_percent = func.get("current_percent") or 0

            if USE_SDK and SDK_AVAILABLE:
                result = await self._run_agent_sdk(
                    session_id=session_id,
                    worktree=worktree,
                    prompt=prompt,
                    model=selected_model,
                    verbose=verbose,
                )
                patch = self.worktree_pool.extract_patch(session_id)
                parsed = self._parse_agent_result_sdk(result.get("messages", []))
            else:
                result = await self._run_agent_process(
                    session_id=session_id,
                    worktree=worktree,
                    prompt=prompt,
                    model=selected_model,
                    verbose=verbose,
                )
                patch = self.worktree_pool.extract_patch(session_id)
                parsed = self._parse_agent_result(result.get("output", ""))

            end_percent = parsed.get("percent", start_percent)
            exit_status = parsed.get("status", "unknown")
            notes = f"RB3-merge: {parsed.get('notes', '')}"
            verdict = parsed.get("verdict")

            usage_data = parsed.get("usage") or {}
            actual_cost_usd = parsed.get("total_cost_usd")
            duration_ms = parsed.get("duration_ms")

            record_attempt(
                function_id=func["id"],
                session_id=session_id,
                model=selected_model,
                start_percent=start_percent,
                end_percent=end_percent,
                exit_status=exit_status,
                verdict=verdict,
                patch=patch,
                notes=notes,
                input_tokens=usage_data.get("input_tokens"),
                output_tokens=usage_data.get("output_tokens"),
                cache_read_tokens=usage_data.get("cache_read_tokens"),
                cache_creation_tokens=usage_data.get("cache_creation_tokens"),
                actual_cost_usd=actual_cost_usd,
                duration_ms=duration_ms,
                enrichment_flags=context.get("enrichment_flags"),
                db_path=self.db_path,
            )

            update_function_status(
                function_id=func["id"],
                current_percent=end_percent,
                verdict=verdict,
                source_patch=patch if exit_status == "complete" else None,
                db_path=self.db_path,
            )

            apply_result = self.patch_applier.maybe_apply(
                patch=patch,
                start_percent=start_percent,
                end_percent=end_percent if end_percent is not None else start_percent,
                exit_status=exit_status,
                symbol=symbol,
            )

            return {
                "status": exit_status,
                "start_percent": start_percent,
                "end_percent": end_percent,
                "verdict": verdict,
                "patch": patch,
                "notes": notes,
                "model": selected_model,
                "session_id": session_id,
                "patch_applied": apply_result.get("applied", False),
                "actual_cost_usd": actual_cost_usd,
                "duration_ms": duration_ms,
                "usage": usage_data if usage_data else None,
                "mode": "rb3_merge",
            }

        finally:
            # Only unlock if we locked it (not in batch mode)
            if not pre_locked:
                unlock_function(func["id"], db_path=self.db_path)
            self.worktree_pool.release(session_id)

    async def _run_rb3_merge_agent(
        self,
        session_id: str,
        func: dict[str, Any],
        rb3_source: str,
        model: Optional[str],
        verbose: bool,
    ) -> dict[str, Any]:
        """Run single RB3-merge agent as part of batch (handles its own errors).

        Args:
            session_id: Unique session identifier
            func: Function to work on
            rb3_source: RB3 source code for this unit
            model: Force specific model
            verbose: Print output
        """
        try:
            return await self.run_rb3_merge_single(
                symbol=func["symbol"],
                rb3_source=rb3_source,
                model=model,
                verbose=verbose,
                session_id=session_id,
                pre_locked=True,  # Batch already locked the function
            )
        except Exception as e:
            print(f"[{session_id}] Error: {e}")
            return {
                "status": "error",
                "error": str(e),
                "symbol": func["symbol"],
                "function_id": func["id"],
            }
        finally:
            # Batch mode: unlock the function when agent completes
            unlock_function(func["id"], db_path=self.db_path)

    async def run_rb3_merge_batch(
        self,
        file_pairs: list[dict[str, Any]],
        model: Optional[str] = None,
        max_agents: int = 3,
        func_limit_per_unit: int = 20,
        min_percent: float = 0,
        max_percent: float = 100,
        verbose: bool = True,
    ) -> dict[str, Any]:
        """
        Run RB3-merge batch on multiple paired files with concurrent agents.

        For each file pair, processes incomplete functions using the
        specialized RB3-merge prompt with full RB3 source context.
        Uses async concurrency to run multiple agents in parallel.

        Args:
            file_pairs: List of file pair dicts from database
            model: Force specific model for all
            max_agents: Max concurrent agents
            func_limit_per_unit: Max functions to process per unit
            min_percent: Minimum function match percentage
            max_percent: Maximum function match percentage
            verbose: Print progress

        Returns:
            Summary dict with results
        """
        if verbose:
            print(f"\n{'='*60}")
            print("RB3-Merge Batch Mode (Concurrent)")
            print(f"{'='*60}")
            print(f"File pairs: {len(file_pairs)}")
            print(f"Max agents: {max_agents}")
            print(f"Functions per unit limit: {func_limit_per_unit}")

        # Ensure pool is initialized
        if self.worktree_pool.status()["total"] == 0:
            self.initialize()

        # Preflight quota check before committing to batch
        await self._check_quota(model or "haiku")

        # Build work queue: list of (func, rb3_source, unit) tuples
        work_queue: list[tuple[dict[str, Any], str, str]] = []

        for pair in file_pairs:
            dc3_unit = pair.get("dc3_unit", "")
            rb3_file = pair.get("rb3_file")

            if not rb3_file:
                if verbose:
                    print(f"Skipping {dc3_unit} - no RB3 pair")
                continue

            # Load RB3 source once per unit
            rb3_source = get_rb3_source_for_unit(dc3_unit, db_path=self.db_path)
            if not rb3_source:
                if verbose:
                    print(f"Skipping {dc3_unit} - could not load RB3 source")
                continue

            # Get functions in this unit
            funcs = query_functions(
                pattern=dc3_unit,
                min_percent=min_percent,
                max_percent=max_percent,
                exclude_complete=True,
                limit=func_limit_per_unit,
                db_path=self.db_path,
            )

            if funcs:
                compat = pair.get("compatibility_score", 0)
                if verbose:
                    print(f"Queued: {dc3_unit} ({compat:.1%} compat, {len(funcs)} functions)")
                for func in funcs:
                    work_queue.append((func, rb3_source, dc3_unit))

        if verbose:
            print(f"\nTotal work items: {len(work_queue)}")
            print()

        if not work_queue:
            return {
                "pattern": "rb3-merge",
                "total": 0,
                "complete": 0,
                "at_limit": 0,
                "stuck": 0,
                "error": 0,
                "mode": "rb3_merge",
                "file_pairs_processed": len(file_pairs),
            }

        results = []
        work_index = 0

        # Process work queue with concurrency
        while work_index < len(work_queue) or self.active_sessions:
            # Spawn agents up to max_agents
            while len(self.active_sessions) < max_agents and work_index < len(work_queue):
                func, rb3_source, dc3_unit = work_queue[work_index]
                work_index += 1

                session_id = f"rb3merge-{func['id']}-{datetime.now().strftime('%H%M%S')}"

                # Lock function before spawning
                if not lock_function(func["id"], session_id, db_path=self.db_path):
                    # Already locked, skip
                    if verbose:
                        print(f"Skipped (locked): {func.get('demangled') or func['symbol']}")
                    continue

                # Spawn agent task
                task = asyncio.create_task(
                    self._run_rb3_merge_agent(
                        session_id=session_id,
                        func=func,
                        rb3_source=rb3_source,
                        model=model,
                        verbose=verbose,
                    )
                )
                self.active_sessions[session_id] = task

                if verbose:
                    print(f"[{len(self.active_sessions)}/{max_agents}] Spawned: {func.get('demangled') or func['symbol']}")

            # Wait for at least one completion if at capacity or no more work
            if self.active_sessions and (len(self.active_sessions) >= max_agents or work_index >= len(work_queue)):
                result = await self._wait_for_any_completion()
                if result:
                    results.append(result)
                    if verbose:
                        status = result.get("status", "unknown")
                        end_pct = result.get("end_percent", "?")
                        symbol = result.get("symbol", result.get("session_id", "?"))
                        print(f"  Completed: {symbol} -> {status} ({end_pct}%)")

        # Generate summary
        summary = self._generate_batch_summary(results, "rb3-merge")
        summary["mode"] = "rb3_merge"
        summary["file_pairs_processed"] = len(file_pairs)
        summary["work_items_queued"] = len(work_queue)

        if verbose:
            print(f"\n{'='*60}")
            print("RB3-Merge Batch Complete")
            print(f"{'='*60}")
            print(f"Processed: {len(results)} functions")
            print(f"  Complete:  {summary['complete']}")
            print(f"  At limit:  {summary['at_limit']}")
            print(f"  Stuck:     {summary['stuck']}")
            print(f"  Errors:    {summary['error']}")

        return summary
