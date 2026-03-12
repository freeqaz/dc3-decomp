"""Agent execution and result parsing for orchestrator.

Encapsulates running Claude agents (via SDK or subprocess) and parsing
their output into structured AgentRunResult objects.
"""

import asyncio
import json
import logging
import os
import re
from pathlib import Path
from typing import Any, Optional

from .types import AgentRunConfig, AgentRunResult, DEFAULT_DECOMP_TOOLS
from . import colors as _clr

# SDK integration toggle (default: use SDK)
USE_SDK = os.getenv("ORCHESTRATOR_USE_SDK", "true").lower() == "true"

# Import SDK types conditionally
try:
    from claude_agent_sdk import (
        query as sdk_query,
        ClaudeAgentOptions,
        AssistantMessage,
        UserMessage,
        ResultMessage,
        ToolUseBlock,
        ToolResultBlock,
        TextBlock,
        CLINotFoundError,
        ProcessError,
    )
    from claude_agent_sdk.types import McpStdioServerConfig
    SDK_AVAILABLE = True
except ImportError:
    SDK_AVAILABLE = False

from .config import (
    _get_openrouter_enabled,
    _get_openrouter_api_key,
    _get_openrouter_base_url,
    _get_zai_enabled,
    _get_zai_api_key,
    _get_zai_base_url,
    _get_zai_timeout,
    get_token_budget,
    requires_openrouter,
    requires_zai,
)
from .model_selection import get_model_id


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


class AgentRunner:
    """Runs Claude agents and parses their results.

    Accepts dependencies through constructor for testability. Parsing
    methods are public so they can be tested without running agents.
    """

    def __init__(self, main_repo: Path, db_path: str, logger: logging.Logger):
        self.main_repo = main_repo
        self.db_path = db_path
        self.logger = logger

    async def run(self, config: AgentRunConfig) -> AgentRunResult:
        """Execute an agent and return structured result."""
        if USE_SDK and SDK_AVAILABLE:
            raw = await self._run_sdk(config)
            parsed = self.parse_sdk_messages(raw.get("messages", []))
            parsed.output = raw.get("output", "")
            parsed.exit_code = raw.get("exit_code", 1)
            parsed.messages = raw.get("messages", [])
            return parsed
        else:
            raw = await self._run_process(config)
            parsed = self.parse_process_output(raw.get("output", ""))
            parsed.exit_code = raw.get("exit_code", 1)
            parsed.output = raw.get("output", "")
            return parsed

    # Environment variables to strip from subprocess (inherited from parent session).
    # Note: Proxy vars (HTTP_PROXY etc.) are NOT stripped here because build_env()
    # sets them correctly from CLAUDE_CODE_HOST_*_PROXY_PORT. Since SDK merges
    # provided env over os.environ, build_env()'s values override stale system ones.
    _STRIP_ENV_VARS = frozenset({
        # CLAUDECODE triggers nested session check in CLI
        "CLAUDECODE",
    })

    def build_env(self, model: str = None) -> dict[str, str]:
        """Build environment dict for agent process. Public for testing.

        Optionally translates parent session's proxy ports into standard
        HTTP_PROXY/HTTPS_PROXY vars. Only enabled when ORCHESTRATOR_USE_PROXY=true
        (e.g. when direct internet access is unavailable). By default, child
        processes connect directly — the parent's sandbox proxy doesn't work
        for child claude processes making their own API calls.
        """
        _project_root = Path(__file__).resolve().parent.parent.parent
        agent_home = Path(os.environ.get(
            "AGENT_HOME",
            str(_project_root / "agent-home"),
        ))
        agent_home.mkdir(parents=True, exist_ok=True)

        env: dict[str, str] = {
            "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC": "1",
            "HOME": str(agent_home),
        }

        # Only forward parent proxy when explicitly requested.
        # The parent Claude Code session's proxy is a sandbox-internal proxy
        # that doesn't work for child claude processes making API calls.
        if os.environ.get("ORCHESTRATOR_USE_PROXY", "").lower() == "true":
            http_proxy_port = os.environ.get("CLAUDE_CODE_HOST_HTTP_PROXY_PORT")
            socks_proxy_port = os.environ.get("CLAUDE_CODE_HOST_SOCKS_PROXY_PORT")
            if http_proxy_port:
                env["HTTP_PROXY"] = f"http://localhost:{http_proxy_port}"
                env["HTTPS_PROXY"] = f"http://localhost:{http_proxy_port}"
                env["http_proxy"] = f"http://localhost:{http_proxy_port}"
                env["https_proxy"] = f"http://localhost:{http_proxy_port}"
            if socks_proxy_port:
                env["ALL_PROXY"] = f"socks5h://localhost:{socks_proxy_port}"
                env["all_proxy"] = f"socks5h://localhost:{socks_proxy_port}"

        return env

    def build_auth_env(self, model: str = None) -> dict[str, str]:
        """Build auth environment for SDK/subprocess. Public for testing.

        Returns environment variables for API authentication.
        Uses get_backend() which respects --provider override and defaults
        Anthropic-native models to the anthropic backend (subscription).
        """
        from .config import get_backend

        env: dict[str, str] = {}
        backend = get_backend(model)

        if backend == "zai" and _get_zai_api_key():
            env["ANTHROPIC_BASE_URL"] = _get_zai_base_url()
            env["ANTHROPIC_AUTH_TOKEN"] = _get_zai_api_key()
            env["ANTHROPIC_API_KEY"] = ""  # Must be explicitly empty
            env["API_TIMEOUT_MS"] = _get_zai_timeout()

            if model and requires_zai(model):
                zai_model_id = get_model_id(model)
                env["ANTHROPIC_DEFAULT_SONNET_MODEL"] = zai_model_id

        elif backend == "openrouter" and _get_openrouter_api_key():
            env["ANTHROPIC_BASE_URL"] = _get_openrouter_base_url()
            env["ANTHROPIC_AUTH_TOKEN"] = _get_openrouter_api_key()
            env["ANTHROPIC_API_KEY"] = ""  # Must be explicitly empty

            # Always set ANTHROPIC_DEFAULT_SONNET_MODEL for OpenRouter so the CLI
            # sends the OpenRouter model ID (e.g. "anthropic/claude-sonnet-4.6")
            # instead of Anthropic's internal ID.
            if model:
                from .config import MODEL_REGISTRY
                or_models = MODEL_REGISTRY.get("openrouter", {})
                if model in or_models:
                    env["ANTHROPIC_DEFAULT_SONNET_MODEL"] = or_models[model]["model_id"]

        return env

    def build_sdk_options(self, config: AgentRunConfig) -> Any:
        """Build SDK options from config. Public for testing.

        Returns ClaudeAgentOptions (or raises if SDK not available).
        """
        if not SDK_AVAILABLE:
            raise RuntimeError("claude-agent-sdk not installed")

        # When using alt backends (OpenRouter/Z.AI), CLI model must be a standard
        # name ("sonnet") because the CLI doesn't understand full model IDs like
        # "anthropic/claude-sonnet-4.6". Backend-specific models route through
        # ANTHROPIC_DEFAULT_SONNET_MODEL.
        use_alt = (requires_zai(config.model) or requires_openrouter(config.model)
                   or _get_openrouter_enabled() or _get_zai_enabled())
        if use_alt:
            cli_model = "sonnet"
        else:
            cli_model = get_model_id(config.model)

        env = self.build_env(config.model)
        env.update(self.build_auth_env(config.model))
        env["REPO_ROOT"] = str(config.worktree)

        # Explicitly clear vars that shouldn't be inherited from parent session
        # SDK merges provided env with os.environ, so we must set to empty string
        for var in self._STRIP_ENV_VARS:
            env[var] = ""

        mcp_config: McpStdioServerConfig = {
            "command": "python3",
            "args": ["-m", "scripts.orchestrator.mcp_server", "--db", str(self.db_path), "--no-record-attempts"],
            "env": {
                "PYTHONPATH": str(self.main_repo),
                "REPO_ROOT": str(config.worktree),  # So MCP tools default to worktree builds
            },
        }

        tools = config.effective_tools
        if config.disallowed_tools:
            disallowed = config.disallowed_tools
        elif config.model == "haiku":
            disallowed = ["Task", "TaskOutput", "TodoWrite", "Skill"]
        else:
            disallowed = []

        # Capture stderr lines for debugging agent crashes
        stderr_lines: list[str] = []
        def _capture_stderr(line: str) -> None:
            stderr_lines.append(line)

        options = ClaudeAgentOptions(
            model=cli_model,
            max_turns=config.max_turns,
            cwd=str(config.worktree),
            env=env,
            permission_mode="bypassPermissions",
            system_prompt={"type": "preset", "preset": "claude_code"},
            setting_sources=["user", "project", "local"],
            mcp_servers={"orchestrator": mcp_config},
            allowed_tools=tools,
            disallowed_tools=disallowed,
            add_dirs=[str(self.main_repo)],
            stderr=_capture_stderr,
        )

        # Attach stderr collector to options so _run_sdk can access it
        options._stderr_lines = stderr_lines  # type: ignore[attr-defined]
        return options

    def parse_process_output(self, output: str) -> AgentRunResult:
        """Parse subprocess output into AgentRunResult. Public for testing."""
        result = {
            "status": "unknown",
            "percent": None,
            "notes": "",
            "verdict": None,
        }

        # Look for report_result JSON in output
        json_pattern = r'\{[^{}]*"_decomp_exit"[^{}]*\}'
        matches = re.findall(json_pattern, output, re.DOTALL)

        if matches:
            try:
                data = json.loads(matches[-1])
                result["status"] = data.get("status", "unknown")
                result["percent"] = data.get("percent")
                result["notes"] = data.get("notes", "")
                self.logger.debug(f"Parsed report_result MCP call: status={result['status']}, percent={result['percent']}")
            except json.JSONDecodeError as e:
                self.logger.warning(f"Failed to parse JSON from agent output: {e}")

        # Look for verdict in objdiff output
        verdict_pattern = r'verdict[:\s]+(COMPLETE|AT_LIMIT|LIKELY_FIXABLE|MAYBE_FIXABLE|UNKNOWN|NEEDS_INVESTIGATION|NeedsInvestigation|LikelyFixable|MaybeFixable|AtLimit)'
        verdict_matches = re.findall(verdict_pattern, output, re.IGNORECASE)
        if verdict_matches:
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

        # Also look for RESULT/PERCENT/NOTES format
        result_pattern = r'RESULT:\s*(\w+)'
        result_matches = re.findall(result_pattern, output, re.IGNORECASE)
        if result_matches:
            result["status"] = result_matches[-1].lower()

        percent_result_pattern = r'PERCENT:\s*([\d.]+)'
        percent_result_matches = re.findall(percent_result_pattern, output, re.IGNORECASE)
        if percent_result_matches:
            try:
                result["percent"] = float(percent_result_matches[-1])
            except ValueError:
                pass

        notes_pattern = r'NOTES:\s*(.+?)(?:\n|$)'
        notes_matches = re.findall(notes_pattern, output, re.IGNORECASE)
        if notes_matches:
            result["notes"] = notes_matches[-1].strip()

        return AgentRunResult(
            exit_code=0,
            status=result["status"],
            percent=result["percent"],
            notes=result["notes"],
            verdict=result["verdict"],
        )

    def parse_sdk_messages(self, messages: list[Any]) -> AgentRunResult:
        """Parse SDK message stream into AgentRunResult. Public for testing."""
        result = {
            "status": "unknown",
            "percent": None,
            "notes": "",
            "verdict": None,
            "usage": None,
            "total_cost_usd": None,
            "duration_ms": None,
            "num_turns": None,
        }

        all_text = []

        for message in messages:
            if SDK_AVAILABLE and isinstance(message, AssistantMessage):
                for block in message.content:
                    if isinstance(block, ToolUseBlock):
                        if block.name == "mcp__orchestrator__report_result":
                            input_data = block.input
                            result["status"] = input_data.get("status", "unknown")
                            result["percent"] = input_data.get("percent")
                            result["notes"] = input_data.get("notes", "")
                    elif isinstance(block, TextBlock):
                        all_text.append(block.text)
            elif SDK_AVAILABLE and isinstance(message, ResultMessage):
                result["total_cost_usd"] = message.total_cost_usd
                result["duration_ms"] = message.duration_ms
                result["num_turns"] = message.num_turns

                if message.usage:
                    result["usage"] = {
                        "input_tokens": message.usage.get("inputTokens"),
                        "output_tokens": message.usage.get("outputTokens"),
                        "cache_read_tokens": message.usage.get("cacheReadInputTokens"),
                        "cache_creation_tokens": message.usage.get("cacheCreationInputTokens"),
                    }

        combined_text = "\n".join(all_text)

        # Look for verdict in text
        verdict_pattern = r'verdict[:\s]+(COMPLETE|AT_LIMIT|LIKELY_FIXABLE|MAYBE_FIXABLE|UNKNOWN|NEEDS_INVESTIGATION|NeedsInvestigation|LikelyFixable|MaybeFixable|AtLimit)'
        verdict_matches = re.findall(verdict_pattern, combined_text, re.IGNORECASE)
        if verdict_matches:
            verdict = re.sub(r'([a-z])([A-Z])', r'\1_\2', verdict_matches[-1]).upper()
            result["verdict"] = verdict

        # Fallback percentage from text
        if result["percent"] is None:
            percent_pattern = r'(\d+\.?\d*)\s*%\s*match'
            percent_matches = re.findall(percent_pattern, combined_text, re.IGNORECASE)
            if percent_matches:
                try:
                    result["percent"] = float(percent_matches[-1])
                except ValueError:
                    pass

        # Fallback RESULT/PERCENT/NOTES format
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

        return AgentRunResult(
            exit_code=0,
            status=result["status"],
            percent=result["percent"],
            notes=result["notes"],
            verdict=result["verdict"],
            total_cost_usd=result["total_cost_usd"],
            duration_ms=result["duration_ms"],
            usage=result["usage"],
        )

    # --- Private execution methods ---

    # Keys worth showing inline for each tool at normal verbosity
    _TOOL_SUMMARY_KEYS: dict[str, list[str]] = {
        "Read": ["file_path"],
        "Edit": ["file_path"],
        "Write": ["file_path"],
        "Glob": ["pattern"],
        "Grep": ["pattern"],
        "Bash": ["description", "command"],
        "mcp__orchestrator__run_objdiff": ["symbol"],
        "mcp__orchestrator__run_diff_inspect": ["symbol", "mode"],
        "mcp__orchestrator__run_analyze_function": ["symbol"],
        "mcp__orchestrator__report_result": ["status", "percent", "notes"],
        "mcp__orchestrator__lookup_struct_offset": ["class_name", "offset"],
        "mcp__orchestrator__lookup_merged_symbol": ["address"],
        "mcp__orchestrator__lookup_rb3": ["symbol"],
        "mcp__orchestrator__query_functions": ["unit_pattern"],
    }

    def _format_tool_summary(self, block: Any, prefix: str = "") -> str:
        """Format a tool call for normal verbosity: name + key args."""
        name = block.name
        inp = block.input or {}
        keys = self._TOOL_SUMMARY_KEYS.get(name, list(inp.keys())[:2])
        parts = []
        for k in keys:
            if k in inp:
                v = str(inp[k])
                # For Bash, prefer description over command
                if name == "Bash" and k == "command" and "description" in inp:
                    continue
                if len(v) > 80:
                    v = v[:77] + "..."
                parts.append(v)
        suffix = f" {', '.join(parts)}" if parts else ""
        color = _clr.TOOL_COLORS.get(name, _clr.DEFAULT_TOOL_COLOR)
        return f"{prefix}  {color}[{name}]{_clr.RESET}{suffix}"

    def _print_message(self, message: Any, config: AgentRunConfig, output_lines: list[str]) -> None:
        """Print SDK message at the appropriate verbosity level.

        verbose=1 (normal): tool names with key args, match%, errors, result summary
        verbose=2 (--verbose): full text, all tool args, tool result snippets

        All output is prefixed with [session_id] to disambiguate interleaved
        output from parallel agents.
        """
        if not SDK_AVAILABLE:
            return

        prefix = _clr.colored_prefix(config.session_id)

        if isinstance(message, AssistantMessage):
            for block in message.content:
                if isinstance(block, TextBlock):
                    output_lines.append(block.text)
                    if config.verbose >= 2:
                        print(f"{prefix}{block.text}", end="")
                elif isinstance(block, ToolUseBlock):
                    if config.verbose >= 2:
                        tool_str = f"\n{prefix}[Tool: {block.name}]"
                        if hasattr(block, 'input') and block.input:
                            input_parts = []
                            for k, v in block.input.items():
                                v_str = str(v)
                                if len(v_str) > 100:
                                    v_str = v_str[:100] + "..."
                                input_parts.append(f"{k}={v_str}")
                            if input_parts:
                                tool_str += f" {', '.join(input_parts)}"
                        tool_str += "\n"
                        print(tool_str, end="")
                        output_lines.append(tool_str)
                    else:
                        print(self._format_tool_summary(block, prefix=prefix))

        elif isinstance(message, UserMessage):
            for block in message.content:
                if isinstance(block, ToolResultBlock):
                    content = block.content
                    if content is None:
                        content_str = "(no output)"
                    elif isinstance(content, str):
                        content_str = content
                    else:
                        content_str = str(content)

                    if config.verbose >= 2:
                        if len(content_str) > 200:
                            content_str = content_str[:200] + "..."
                        error_marker = " ERROR" if block.is_error else ""
                        result_str = f"{prefix}  → {content_str}{error_marker}\n"
                        print(result_str, end="")
                        output_lines.append(result_str)
                    else:
                        if block.is_error:
                            short = content_str[:150] + "..." if len(content_str) > 150 else content_str
                            print(f"{prefix}    {_clr.BOLD_RED}ERROR:{_clr.RESET} {_clr.RED}{short}{_clr.RESET}")
                        else:
                            # Show match% from objdiff results (JSON or markdown format)
                            match = re.search(r'"match_percent":\s*([\d.]+)', content_str)
                            if not match:
                                match = re.search(r'Match:\s*([\d.]+)%', content_str)
                            if match:
                                pct = float(match.group(1))
                                if pct >= 100:
                                    pct_color = _clr.BOLD_GREEN
                                elif pct >= 80:
                                    pct_color = _clr.GREEN
                                elif pct >= 50:
                                    pct_color = _clr.YELLOW
                                else:
                                    pct_color = _clr.RED
                                print(f"{prefix}    {pct_color}→ {match.group(1)}% match{_clr.RESET}")

        elif isinstance(message, ResultMessage):
            if config.verbose >= 2:
                result_str = f"\n{prefix}[Result: {message}]\n"
                print(result_str, end="")
                output_lines.append(result_str)
            else:
                cost = f"${message.total_cost_usd:.3f}" if message.total_cost_usd else "n/a"
                turns = message.num_turns or "?"
                print(f"{prefix}  {_clr.DIM}Done: {turns} turns, cost {cost}{_clr.RESET}")

    async def _run_sdk(self, config: AgentRunConfig) -> dict[str, Any]:
        """Run agent via Python SDK."""
        if not SDK_AVAILABLE:
            raise RuntimeError("claude-agent-sdk not installed. Run: pip install claude-agent-sdk")

        options = self.build_sdk_options(config)

        use_zai = _get_zai_enabled() or requires_zai(config.model)
        use_openrouter = _get_openrouter_enabled() or requires_openrouter(config.model)

        if config.verbose >= 2:
            pfx = _clr.colored_prefix(config.session_id)
            if use_zai and _get_zai_api_key():
                print(f"{pfx}Using Z.AI backend at {_get_zai_base_url()}")
            elif use_openrouter and _get_openrouter_api_key():
                print(f"{pfx}Using OpenRouter backend at {_get_openrouter_base_url()}")

        if config.verbose >= 1:
            actual_model = get_model_id(config.model)
            pfx = _clr.colored_prefix(config.session_id)
            print(f"{pfx}{_clr.DIM}Starting agent (SDK) with model {actual_model}...{_clr.RESET}")

        messages: list[Any] = []
        output_lines: list[str] = []

        try:
            async for message in sdk_query(prompt=config.prompt, options=options):
                messages.append(message)

                if config.verbose >= 1:
                    self._print_message(message, config, output_lines)

            return {
                "exit_code": 0,
                "messages": messages,
                "output": "".join(output_lines),
                "session_id": config.session_id,
            }

        except CLINotFoundError as e:
            error_msg = f"Claude CLI not found: {e}"
            if config.verbose >= 1:
                pfx = _clr.colored_prefix(config.session_id)
                print(f"\n{pfx}{_clr.BOLD_RED}Error:{_clr.RESET} {error_msg}")
            return {"exit_code": 127, "error": error_msg, "messages": [], "output": ""}

        except ProcessError as e:
            error_msg = str(e)
            # Get captured stderr from our callback (more useful than SDK's placeholder)
            captured_stderr = getattr(options, '_stderr_lines', [])
            stderr = "\n".join(captured_stderr) if captured_stderr else (getattr(e, 'stderr', '') or '')
            if config.verbose >= 1:
                pfx = _clr.colored_prefix(config.session_id)
                print(f"\n{pfx}{_clr.BOLD_RED}Process error:{_clr.RESET} {error_msg}")
                if captured_stderr:
                    print(f"{pfx}{_clr.DIM}  Captured stderr ({len(captured_stderr)} lines):{_clr.RESET}")
                    for line in captured_stderr[-20:]:
                        print(f"{pfx}{_clr.DIM}  {line.rstrip()}{_clr.RESET}")
            return {
                "exit_code": e.exit_code if hasattr(e, 'exit_code') else 1,
                "error": error_msg,
                "messages": messages,  # preserve messages collected before error
                "output": stderr,
            }

        except Exception as e:
            error_msg = str(e)
            captured_stderr = getattr(options, '_stderr_lines', [])
            stderr = "\n".join(captured_stderr) if captured_stderr else ''
            if config.verbose >= 1:
                pfx = _clr.colored_prefix(config.session_id)
                print(f"\n{pfx}{_clr.BOLD_RED}Unexpected error:{_clr.RESET} {error_msg}")
                if captured_stderr:
                    print(f"{pfx}{_clr.DIM}  Captured stderr ({len(captured_stderr)} lines):{_clr.RESET}")
                    for line in captured_stderr[-20:]:
                        print(f"{pfx}{_clr.DIM}  {line.rstrip()}{_clr.RESET}")
            return {"exit_code": 1, "error": error_msg, "messages": messages, "output": stderr}

    async def _run_process(self, config: AgentRunConfig) -> dict[str, Any]:
        """Run Claude CLI agent as subprocess."""
        # When using alt backends, CLI model must be a standard name ("sonnet")
        use_alt = (requires_zai(config.model) or requires_openrouter(config.model)
                   or _get_openrouter_enabled() or _get_zai_enabled())
        if use_alt:
            cli_model = "sonnet"
        else:
            cli_model = get_model_id(config.model)

        cmd = [
            "claude",
            "--print",
            "--model", cli_model,
            "--max-turns", str(config.max_turns),
            "--dangerously-skip-permissions",
            "--no-session-persistence",
            config.prompt,
        ]

        if config.verbose >= 1:
            pfx = _clr.colored_prefix(config.session_id)
            print(f"{pfx}{_clr.DIM}Starting agent with model {cli_model}...{_clr.RESET}")

        # Strip stale system proxy vars but keep CLAUDE_CODE_HOST_*_PROXY_PORT
        # so child claude sessions route through the parent's proxy (needed when
        # direct internet access is unavailable).
        strip_vars = {"HTTP_PROXY", "HTTPS_PROXY", "http_proxy", "https_proxy",
                      "ALL_PROXY", "all_proxy", "CLAUDECODE"}
        env = {k: v for k, v in os.environ.items() if k not in strip_vars}
        env.update(self.build_env(config.model))
        env.update(self.build_auth_env(config.model))

        # Fall back to OAuth if no backend auth configured
        if not env.get("ANTHROPIC_API_KEY") and not env.get("ANTHROPIC_AUTH_TOKEN"):
            oauth_token = get_oauth_token()
            if oauth_token:
                env["CLAUDE_CODE_OAUTH_TOKEN"] = oauth_token

        # Verbose logging of backend selection
        if config.verbose >= 2:
            pfx = _clr.colored_prefix(config.session_id)
            use_zai = _get_zai_enabled() or requires_zai(config.model)
            use_openrouter = _get_openrouter_enabled() or requires_openrouter(config.model)
            if use_zai and _get_zai_api_key():
                print(f"{pfx}Using Z.AI backend at {_get_zai_base_url()}")
            elif use_openrouter and _get_openrouter_api_key():
                print(f"{pfx}Using OpenRouter backend at {_get_openrouter_base_url()}")

        process = await asyncio.create_subprocess_exec(
            *cmd,
            cwd=config.worktree,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
            env=env,
        )

        prefix = _clr.colored_prefix(config.session_id)
        output_lines = []
        async for line in process.stdout:
            decoded = line.decode("utf-8", errors="replace")
            output_lines.append(decoded)
            if config.verbose >= 2:
                print(f"{prefix}{decoded}", end="")

        await process.wait()

        output = "".join(output_lines)

        return {
            "exit_code": process.returncode,
            "output": output,
            "session_id": config.session_id,
        }
