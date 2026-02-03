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
from typing import Any, Callable, Optional

from .agent_runner import AgentRunner, USE_SDK, SDK_AVAILABLE
from .types import AgentRunConfig, AgentRunResult, DEFAULT_DECOMP_TOOLS, REFACTOR_TOOLS
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


class TaggedLogger(logging.LoggerAdapter):
    """Logger adapter that prepends a [tag] prefix to all messages.

    Used to disambiguate interleaved log output from parallel agents.
    """

    def process(self, msg, kwargs):
        return f"[{self.extra['tag']}] {msg}", kwargs


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

        # Agent runner for executing Claude agents
        self.runner = AgentRunner(
            main_repo=self.main_repo,
            db_path=self.db_path,
            logger=self.logger,
        )

        # Patch applier for auto-applying agent progress to main repo
        self.patch_applier = PatchApplier(
            main_repo=self.main_repo,
            enabled=auto_apply,
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
        env.update(self.runner.build_auth_env(model))

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

    def _worktree_has_changes(self, worktree: Path) -> bool:
        """Check if worktree has uncommitted changes."""
        result = subprocess.run(
            ["git", "diff", "--quiet", "HEAD"],
            cwd=worktree,
            capture_output=True,
        )
        return result.returncode != 0

    def _build_refactor_prompt(self, func: dict, worktree: Path, first_pass_percent: float) -> str:
        """Build prompt for refactor-staff second pass."""
        # Read the skill file
        skill_path = self.main_repo / ".claude" / "skills" / "refactor-staff" / "SKILL.md"
        if not skill_path.exists():
            raise RuntimeError(f"refactor-staff skill not found at {skill_path}")

        with open(skill_path) as f:
            skill_content = f.read()

        # Get modified files list
        result = subprocess.run(
            ["git", "diff", "--name-only", "HEAD"],
            cwd=worktree,
            capture_output=True,
            text=True,
        )
        modified_files = result.stdout.strip().split("\n") if result.stdout.strip() else []

        # Build the prompt
        prompt = f"""# Refactor-Staff Cleanup Pass

{skill_content}

---

## Context

**Function:** {func['symbol']}
**Demangled:** {func.get('demangled') or func['symbol']}
**Current match:** {first_pass_percent:.1f}%
**Worktree:** {worktree}

**Modified files from first pass:**
{chr(10).join(f"- {f}" for f in modified_files) if modified_files else "(none)"}

---

## Critical Constraints

You MUST preserve or improve the match percentage. Current: {first_pass_percent:.1f}%.

If your changes reduce the match, revert them immediately.

After making any changes, verify with:
```bash
./bin/objdiff-cli diff {func['symbol']} --project-dir {worktree}
```

When using MCP tools (run_objdiff, analyze_function, etc.), pass:
```
project_dir={worktree}
```

---

## Your Task

Apply the refactor-staff methodology to clean up the code from the first pass.
Focus on readability and maintainability while preserving exact behavior and match percentage.
"""
        return prompt

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

        # Log per-section sizes for budget debugging
        sections = {
            "template": len(template),
            "rb3_reference": len(context.get('rb3_reference', '')),
            "m2c_decompilation": len(context.get('m2c_decompilation', '')),
            "ghidra_decompilation": len(context.get('decompilation', '')),
            "objdiff_preview": len(context.get('objdiff_preview', '')),
            "previous_attempts": len(context.get('previous_attempts', '')),
            "xrefs_preview": len(context.get('xrefs_preview', '')),
            "header_contents": len(context.get('header_contents', '')),
            "source_contents": len(context.get('source_contents', '')),
        }
        total_sections = sum(sections.values())
        self.logger.info(
            f"Prompt section sizes (total {total_sections / 1024:.1f}KB): "
            + ", ".join(f"{k}={v / 1024:.1f}KB" for k, v in sorted(sections.items(), key=lambda x: -x[1]))
        )

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
            ghidra_file_path_relative=context.get('ghidra_file_path_relative', '(not written)'),
            rb3_reference=context.get('rb3_reference', '(not available)'),
            rb3_file_path_relative=context.get('rb3_file_path_relative', '(not found)'),
            m2c_decompilation=context.get('m2c_decompilation', '(not run)'),
            m2c_file_path=context.get('m2c_file_path', '(not written)'),
            m2c_file_path_relative=context.get('m2c_file_path_relative', '(not written)'),
            m2c_line_count=context.get('m2c_line_count', 0),
            xrefs_path_absolute=context.get('xrefs_path_absolute', '(unavailable)'),
            xrefs_path_relative=context.get('xrefs_path_relative', '(unavailable)'),
            xrefs_preview=context.get('xrefs_preview', '(unavailable)'),
            # Source file absolute path
            source_file_absolute=context.get('source_file_absolute', '(unknown)'),
            # Header file
            header_file_absolute=context.get('header_file_absolute', '(no header)'),
            header_contents=context.get('header_contents', '(no header found)'),
            header_line_count=context.get('header_line_count', 0),
            # Source window
            source_contents=context.get('source_contents', '(not read)'),
            source_window_start_line=context.get('source_window_start_line', 0),
            source_window_end_line=context.get('source_window_end_line', 0),
            source_total_lines=context.get('source_total_lines', 0),
            # Pre-computed objdiff output
            objdiff_file=context.get('objdiff_file', '(unavailable)'),
            objdiff_file_absolute=context.get('objdiff_file_absolute', '(unavailable)'),
            objdiff_line_count=context.get('objdiff_line_count', 0),
            objdiff_preview=context.get('objdiff_preview', '(unavailable)'),
            # Worktree location (for agents to pass to MCP tools)
            worktree_dir=worktree_dir or '(unknown)',
        ) + build_hint

    async def _execute_session(
        self,
        func: dict,
        session_id: str | None,
        pre_locked: bool,
        model: str | None,
        verbose: int,
        dry_run: bool,
        use_incremental: bool,
        prompt_builder: Callable[[dict, str, dict], str],
        notes_prefix: str = "",
        session_prefix: str = "single",
        dry_run_handler: Callable[[dict, str, dict], dict] | None = None,
        refactor: bool = False,
    ) -> dict[str, Any]:
        """Execute a session flow shared by run_single and run_rb3_merge_single.

        Args:
            func: Function dict from database
            session_id: Optional session ID (generated if not provided)
            pre_locked: If True, skip locking (already locked by caller)
            model: Force specific model (haiku, sonnet, opus)
            verbose: Print agent output
            dry_run: Don't actually run agent, just show what would happen
            use_incremental: Use incremental build if True, full build if False
            prompt_builder: Callable(func, worktree_dir, context) -> str for building prompt
            notes_prefix: Prefix for notes field (e.g., "RB3-merge: ")
            session_prefix: Prefix for session ID generation (e.g., "single", "rb3merge")
            dry_run_handler: Optional callable for custom dry-run output. If None, uses default.
            refactor: Reserved for future use (Phase 4)

        Returns:
            Result dict with status, percent, patch, etc.
        """
        # 0. Generate session ID early so all log messages carry it
        if session_id is None:
            session_id = f"{session_prefix}-{func['id']}-{datetime.now().strftime('%H%M%S')}"
        log = TaggedLogger(self.logger, {"tag": func['id']})

        # 0b. Reject merged symbols (ICF artifacts, not real decomp targets)
        if func.get("symbol", "").startswith("merged_"):
            log.warning(f"Skipping merged symbol {func['symbol']} (ICF artifact, not actionable)")
            return {
                "status": "at_limit",
                "start_percent": 0.0,
                "end_percent": 0.0,
                "notes": f"Merged symbol (ICF artifact). Not a real decomp target.",
                "symbol": func["symbol"],
            }

        # 1. Preflight quota check
        await self._check_quota(model or "haiku")

        log.info(f"Starting agent session for symbol: {func['symbol']}")
        log.debug(f"Function details: demangled={func.get('demangled')}, unit={func.get('unit')}, current_percent={func.get('current_percent')}%")

        # 3. Lock function (skip if pre_locked)
        if not pre_locked:
            if not lock_function(func["id"], session_id, db_path=self.db_path):
                raise RuntimeError(f"Could not lock function (already locked?): {func['symbol']}")

        # 4. Acquire worktree
        worktree = self.worktree_pool.acquire(session_id)
        if worktree is None:
            if not pre_locked:
                unlock_function(func["id"], db_path=self.db_path)
            raise RuntimeError("No worktrees available")

        try:
            # 5. Select model
            selected_model = select_model(func, force_model=model)
            reason = get_escalation_reason(func, selected_model)

            log.info(f"Model selected: {selected_model} (reason: {reason})")
            log.debug(f"Worktree: {worktree}, Build strategy: {'incremental' if use_incremental else 'full'}")

            # 6. Collect pre-run context
            context = {}
            try:
                log.debug(f"Collecting pre-run context for {func['symbol']}...")
                context = collect_pre_run_context(
                    symbol=func["symbol"],
                    unit=func.get("unit"),
                    project_dir=str(self.main_repo),
                    worktree_dir=str(worktree)
                )
                log.debug(f"Context collected: {len(context)} fields")
                if context.get('verdict'):
                    log.debug(f"Verdict: {context.get('verdict')}, Match: {context.get('match_percent')}%")
            except Exception as e:
                log.warning(f"Failed to collect pre-run context: {e}")
                context = {}

            # 6b. Correct stale DB percent using measured match
            # The DB percent can drift if a prior session reported wrong data or
            # if source files were edited outside the orchestrator.  Use the
            # freshly-measured objdiff percent as the authoritative start value
            # so that improvement/regression detection is accurate.
            measured_percent = context.get("match_percent")
            db_percent = func.get("current_percent") or 0
            if measured_percent is not None and abs(measured_percent - db_percent) > 0.05:
                log.warning(
                    f"DB percent stale for {func['symbol']}: "
                    f"DB={db_percent:.2f}%, measured={measured_percent:.2f}%. "
                    f"Correcting DB to measured value."
                )
                update_function_status(
                    function_id=func["id"],
                    current_percent=measured_percent,
                    db_path=self.db_path,
                )
                func["current_percent"] = measured_percent

            # 7. Handle dry-run
            if dry_run:
                if dry_run_handler:
                    return dry_run_handler(func, str(worktree), context)
                else:
                    # Default dry-run handler
                    print(f"[DRY RUN] Would process {func['symbol']}")
                    print(f"  Model: {selected_model}")
                    print(f"  Worktree: {worktree}")
                    return {
                        "status": "dry_run",
                        "function": func,
                        "model": selected_model,
                        "worktree": str(worktree),
                        "context": context,
                    }

            # 8. Build prompt (using provided builder)
            prompt = prompt_builder(func, str(worktree), context)

            # Check prompt size against token budget
            prompt_size = len(prompt.encode('utf-8'))

            if prompt_size > TOTAL_CHAR_BUDGET:
                log.error(
                    f"Prompt exceeds budget: {prompt_size / 1024:.1f}KB > {TOTAL_CHAR_BUDGET / 1024:.1f}KB. "
                    f"Truncating to reduce context noise."
                )
                prompt = prompt[:TOTAL_CHAR_BUDGET]
                prompt += "\n\n[PROMPT TRUNCATED - exceeded token budget]"
            elif prompt_size > TOTAL_CHAR_BUDGET * 0.8:
                log.warning(
                    f"Prompt approaching budget: {prompt_size / 1024:.1f}KB "
                    f"({prompt_size * 100 // TOTAL_CHAR_BUDGET}% of {TOTAL_CHAR_BUDGET / 1024:.0f}KB budget)"
                )
            else:
                log.debug(f"Prompt size: {prompt_size / 1024:.1f}KB ({prompt_size * 100 // TOTAL_CHAR_BUDGET}% of budget)")

            # 9. Run agent via AgentRunner
            start_percent = func.get("current_percent") or 0

            log.info(f"Launching agent (model={selected_model}, worktree={worktree})...")
            agent_config = AgentRunConfig(
                session_id=session_id,
                worktree=worktree,
                prompt=prompt,
                model=selected_model,
                verbose=verbose,
            )
            agent_result = await self.runner.run(agent_config)
            log.info(
                f"Agent finished: status={agent_result.status}, percent={agent_result.percent}, "
                f"exit_code={agent_result.exit_code}"
            )

            # 9b. Refactor pass (if enabled and first pass made changes)
            if refactor and self._worktree_has_changes(worktree):
                try:
                    log.info(f"Running refactor-staff cleanup pass (Haiku)...")
                    refactor_prompt = self._build_refactor_prompt(func, worktree, agent_result.percent or start_percent)
                    refactor_config = AgentRunConfig(
                        session_id=f"{session_id}-refactor",
                        worktree=worktree,
                        prompt=refactor_prompt,
                        model="haiku",
                        verbose=verbose,
                        max_turns=30,
                        allowed_tools=list(REFACTOR_TOOLS),
                    )
                    refactor_result = await self.runner.run(refactor_config)
                    log.info(f"Refactor pass completed: percent={refactor_result.percent}, status={refactor_result.status}")

                    # Safety: check if match% regressed
                    if refactor_result.percent is not None and agent_result.percent is not None:
                        if refactor_result.percent < agent_result.percent - 0.1:
                            # Regressed — revert refactor changes
                            log.warning(
                                f"Refactor pass regressed: {agent_result.percent}% -> {refactor_result.percent}%. Reverting."
                            )
                            subprocess.run(["git", "checkout", "--", "."], cwd=worktree, capture_output=True)
                        else:
                            # Success — merge cost
                            agent_result.merge_cost(refactor_result)
                            if refactor_result.percent is not None:
                                agent_result.percent = refactor_result.percent
                    else:
                        # Can't verify — merge cost anyway (assume ok)
                        agent_result.merge_cost(refactor_result)
                except Exception as e:
                    log.error(f"Refactor pass failed with exception: {e}", exc_info=True)
                    log.info("Continuing with first-pass results (refactor skipped due to error)")
            elif refactor:
                log.debug("Refactor pass skipped — no changes in worktree")

            # 10. Extract patch
            log.info("Extracting patch from worktree...")
            patch = self.worktree_pool.extract_patch(session_id)
            if patch:
                log.info(f"Patch extracted: {len(patch)} bytes")
            else:
                log.info("No patch (no changes in worktree)")

            # 10b. Detect "agent claims progress but no patch" inconsistency
            if not patch and agent_result.percent is not None and agent_result.percent > (func.get("current_percent") or 0):
                log.warning(
                    f"Agent reported improvement ({func.get('current_percent') or 0:.2f}% -> "
                    f"{agent_result.percent:.2f}%) but worktree has no changes. "
                    f"Possible causes: agent worked on wrong file, reverted changes, "
                    f"or edited outside src/include/. Discarding agent-reported percent."
                )
                # Don't trust the agent's reported percent — use the measured start
                agent_result.percent = func.get("current_percent") or 0

            # 11. Unpack result
            end_percent = agent_result.percent if agent_result.percent is not None else start_percent
            exit_status = agent_result.status
            notes = notes_prefix + agent_result.notes
            verdict = agent_result.verdict

            usage_data = agent_result.usage or {}
            actual_cost_usd = agent_result.total_cost_usd
            duration_ms = agent_result.duration_ms

            log.info(f"Agent result: status={exit_status}, percent={start_percent}% → {end_percent}%, verdict={verdict}")
            log.debug(f"Notes from agent: {notes[:100]}..." if len(notes) > 100 else f"Notes from agent: {notes}")
            if patch:
                log.debug(f"Patch size: {len(patch)} bytes")
            if actual_cost_usd is not None:
                log.debug(f"Actual cost: ${actual_cost_usd:.4f}, duration: {duration_ms}ms")
                if usage_data:
                    log.debug(f"Tokens: in={usage_data.get('input_tokens')}, out={usage_data.get('output_tokens')}, cache_read={usage_data.get('cache_read_tokens')}")

            # 12. Record attempt
            log.info(f"Recording attempt to database (status={exit_status}, {start_percent}% -> {end_percent}%)...")
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

            # 13. Update function status
            log.info(f"Updating function status in database...")
            update_function_status(
                function_id=func["id"],
                current_percent=end_percent,
                verdict=verdict,
                source_patch=patch if exit_status == "complete" else None,
                db_path=self.db_path,
            )

            # 14. Auto-apply patch to main repo if enabled and there was progress
            apply_result = self.patch_applier.maybe_apply(
                patch=patch,
                start_percent=start_percent,
                end_percent=end_percent if end_percent is not None else start_percent,
                exit_status=exit_status,
                symbol=func["symbol"],
            )

            log.info(
                f"Session complete for {func['symbol']}: "
                f"{start_percent}% -> {end_percent}%, status={exit_status}, "
                f"patch_applied={apply_result.get('applied', False)}"
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
            }

        finally:
            # 15. Cleanup (only unlock if we did the locking)
            log.debug(f"Cleaning up session {session_id}...")
            if not pre_locked:
                unlock_function(func["id"], db_path=self.db_path)
                log.debug(f"Unlocked function {func['id']}")
            self.worktree_pool.release(session_id)
            log.debug(f"Released worktree for session {session_id}")

    def run_single_sync(
        self,
        symbol: str,
        model: Optional[str] = None,
        verbose: int = 1,
        dry_run: bool = False,
        use_incremental: bool = True,
        refactor: bool = True,
        custom_prompt: Optional[str] = None,
    ) -> dict[str, Any]:
        """
        Run single agent on one function (synchronous wrapper).

        Args:
            symbol: Function symbol to work on
            model: Force specific model (haiku, sonnet, opus)
            verbose: Print agent output
            dry_run: Don't actually run agent, just show what would happen
            use_incremental: Use incremental build if True, full build if False
            refactor: Run a Haiku cleanup pass after the first agent (default: True)
            custom_prompt: Custom instructions to append to agent prompt

        Returns:
            Result dict with status, percent, patch, etc.
        """
        return asyncio.run(self.run_single(symbol, model, verbose, dry_run, use_incremental, refactor=refactor, custom_prompt=custom_prompt))

    async def run_single(
        self,
        symbol: str,
        model: Optional[str] = None,
        verbose: int = 1,
        dry_run: bool = False,
        use_incremental: bool = True,
        session_id: Optional[str] = None,
        pre_locked: bool = False,
        refactor: bool = False,
        custom_prompt: Optional[str] = None,
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
            refactor: Reserved for future use (Phase 4)
            custom_prompt: Custom instructions to append to agent prompt

        Returns:
            Result dict with status, percent, patch, etc.
        """
        # Get function from database
        func = get_function_by_symbol(symbol, db_path=self.db_path)
        if not func:
            self.logger.error(f"Function not found in database: {symbol}")
            raise ValueError(f"Function not found: {symbol}")

        # Print verbose header before delegating to _execute_session
        if verbose and not dry_run:
            # Need to select model early for verbose header
            selected_model = select_model(func, force_model=model)
            reason = get_escalation_reason(func, selected_model)
            mode = "SDK" if (USE_SDK and SDK_AVAILABLE) else "subprocess"

            # Generate session ID for display (will be regenerated in _execute_session if None)
            display_session_id = session_id or f"single-{func['id']}-{datetime.now().strftime('%H%M%S')}"

            print(f"\n{'='*60}")
            print(f"Function: {func.get('demangled') or symbol}")
            print(f"Symbol:   {symbol}")
            print(f"Unit:     {func.get('unit') or 'unknown'}")
            print(f"Current:  {func.get('current_percent') or 'unimplemented'}%")
            print(f"Model:    {selected_model} ({reason})")
            print(f"Mode:     {mode}")
            print(f"Session:  {display_session_id}")
            if custom_prompt:
                print(f"Prompt:   {custom_prompt[:80]}{'...' if len(custom_prompt) > 80 else ''}")
            print(f"{'='*60}\n")

        # Custom dry-run handler for run_single's elaborate output
        def run_single_dry_run_handler(func: dict, worktree_dir: str, context: dict) -> dict:
            selected_model = select_model(func, force_model=model)

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

            # Header
            header_file = context.get('header_file_absolute', '(no header)')
            if header_file and not header_file.startswith('('):
                print(f"  Header: {header_file} ({context.get('header_line_count', 0)} lines)")

            # Source window
            src_start = context.get('source_window_start_line', 0)
            src_end = context.get('source_window_end_line', 0)
            src_total = context.get('source_total_lines', 0)
            if src_total > 0:
                print(f"  Source window: lines {src_start}-{src_end} of {src_total}")

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
                "worktree": worktree_dir,
                "context": context,
            }

        # Prompt builder for run_single
        def build_single_prompt(func: dict, worktree_dir: str, context: dict) -> str:
            prompt = self._build_prompt(func, use_incremental=use_incremental, worktree_dir=worktree_dir, context=context)
            if custom_prompt:
                prompt += f"\n\n## Custom Instructions\n\n{custom_prompt}\n"
            return prompt

        # Delegate to _execute_session
        result = await self._execute_session(
            func=func,
            session_id=session_id,
            pre_locked=pre_locked,
            model=model,
            verbose=verbose,
            dry_run=dry_run,
            use_incremental=use_incremental,
            prompt_builder=build_single_prompt,
            notes_prefix="",
            session_prefix="single",
            dry_run_handler=run_single_dry_run_handler if dry_run else None,
            refactor=refactor,
        )

        # Print verbose footer
        if verbose and not dry_run:
            print(f"\n{'='*60}")
            print(f"Result: {result['status']}")
            print(f"Match:  {result['start_percent']}% → {result['end_percent']}%")
            if result.get('verdict'):
                print(f"Verdict: {result['verdict']}")
            if result.get('patch'):
                print(f"Patch:  {len(result['patch'])} bytes")
            if result.get('patch_applied'):
                print(f"Auto-applied: patch applied successfully")

            # Display actual cost if available
            if result.get('actual_cost_usd') is not None:
                print(f"Cost:   ${result['actual_cost_usd']:.4f}")
                if result.get('duration_ms'):
                    print(f"Time:   {result['duration_ms'] / 1000:.1f}s")
                usage = result.get('usage', {})
                if usage.get('input_tokens') or usage.get('output_tokens'):
                    in_tok = usage.get('input_tokens', 0) or 0
                    out_tok = usage.get('output_tokens', 0) or 0
                    cache_read = usage.get('cache_read_tokens', 0) or 0
                    print(f"Tokens: {in_tok:,} in / {out_tok:,} out / {cache_read:,} cache")
            print(f"{'='*60}\n")

        return result

    async def run_batch(
        self,
        pattern: str | list[str] = "*",
        min_percent: float = 0,
        max_percent: float = 100,
        max_agents: int = 3,
        model: Optional[str] = None,
        limit: int = 0,
        verbose: int = 1,
        use_incremental: bool = True,
        periodic_full_interval: int = 10,
        validate_diffs: bool = False,
        refactor: bool = True,
        exclude_at_limit: bool = False,
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
            refactor: Run Haiku refactor-staff cleanup pass after main agent (default: True)

        Returns:
            Summary dict with results
        """
        # Query batch targeting stats
        batch_stats = query_batch_stats(
            pattern=pattern,
            min_percent=min_percent,
            max_percent=max_percent,
            limit=limit,
            exclude_at_limit=exclude_at_limit,
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
            label = "Complete/at-limit" if exclude_at_limit else "Complete"
            print(f"  {label}: {batch_stats['excluded_complete']} (excluded)")
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
                exclude_at_limit=exclude_at_limit,
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
                    session_id, func, model, verbose, use_incremental=current_use_incremental, refactor=refactor
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
            if apply_stats["applied"] > 0 or apply_stats["failed"] > 0 or apply_stats["skipped"] > 0:
                parts = [f"Auto-applied: {apply_stats['applied']} patches"]
                extra = []
                if apply_stats["skipped"] > 0:
                    extra.append(f"{apply_stats['skipped']} skipped (errors)")
                if apply_stats["failed"] > 0:
                    extra.append(f"{apply_stats['failed']} failed")
                if extra:
                    parts.append(f"({', '.join(extra)})")
                print(" ".join(parts))
            print(f"{'='*60}\n")

        return summary

    async def run_batch_with_targets(
        self,
        targets: list[dict[str, Any]],
        max_agents: int = 3,
        model: Optional[str] = None,
        verbose: int = 1,
        use_incremental: bool = True,
        periodic_full_interval: int = 10,
        validate_diffs: bool = False,
        refactor: bool = True,
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
            refactor: Run Haiku refactor-staff cleanup pass after main agent (default: True)

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
                    session_id, func, model, verbose, use_incremental=current_use_incremental, refactor=refactor
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
            if apply_stats["applied"] > 0 or apply_stats["failed"] > 0 or apply_stats["skipped"] > 0:
                parts = [f"Auto-applied: {apply_stats['applied']} patches"]
                extra = []
                if apply_stats["skipped"] > 0:
                    extra.append(f"{apply_stats['skipped']} skipped (errors)")
                if apply_stats["failed"] > 0:
                    extra.append(f"{apply_stats['failed']} failed")
                if extra:
                    parts.append(f"({', '.join(extra)})")
                print(" ".join(parts))
            print(f"{'='*60}\n")

        return summary

    async def _run_batch_agent(
        self,
        session_id: str,
        func: dict[str, Any],
        model: Optional[str],
        verbose: int,
        use_incremental: bool = True,
        refactor: bool = True,
    ) -> dict[str, Any]:
        """Run single agent as part of batch (handles its own errors).

        Args:
            session_id: Unique session identifier
            func: Function to work on
            model: Force specific model
            verbose: Print output
            use_incremental: Use incremental build
            refactor: Run Haiku refactor-staff cleanup pass after main agent
        """
        try:
            return await self.run_single(
                symbol=func["symbol"],
                model=model,
                verbose=verbose,
                use_incremental=use_incremental,
                session_id=session_id,
                pre_locked=True,  # Batch already locked the function
                refactor=refactor,
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
            # Header file
            header_file_absolute=context.get('header_file_absolute', '(no header)'),
            header_contents=context.get('header_contents', '(no header found)'),
            header_line_count=context.get('header_line_count', 0),
            # Source window
            source_contents=context.get('source_contents', '(not read)'),
            source_window_start_line=context.get('source_window_start_line', 0),
            source_window_end_line=context.get('source_window_end_line', 0),
            source_total_lines=context.get('source_total_lines', 0),
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
        verbose: int = 1,
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

        # Custom dry-run handler for RB3-merge
        def rb3_merge_dry_run_handler(func: dict, worktree_dir: str, context: dict) -> dict:
            print(f"[DRY RUN] Would process {symbol} with RB3-merge")
            print(f"  RB3 source: {len(rb3_source)} characters")
            return {"status": "dry_run", "function": func}

        # Prompt builder for RB3-merge
        def build_rb3_merge_prompt(func: dict, worktree_dir: str, context: dict) -> str:
            return self._build_rb3_merge_prompt(func, rb3_source, worktree_dir=worktree_dir, context=context)

        # Delegate to _execute_session
        result = await self._execute_session(
            func=func,
            session_id=session_id,
            pre_locked=pre_locked,
            model=model,
            verbose=verbose,
            dry_run=dry_run,
            use_incremental=True,  # RB3-merge always uses incremental
            prompt_builder=build_rb3_merge_prompt,
            notes_prefix="RB3-merge: ",
            session_prefix="rb3merge",
            dry_run_handler=rb3_merge_dry_run_handler if dry_run else None,
            refactor=False,
        )

        # Add mode field to distinguish RB3-merge results
        result["mode"] = "rb3_merge"

        return result

    async def _run_rb3_merge_agent(
        self,
        session_id: str,
        func: dict[str, Any],
        rb3_source: str,
        model: Optional[str],
        verbose: int,
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
        verbose: int = 1,
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
