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
    get_token_budget,
    requires_openrouter,
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

    def build_env(self, model: str = None) -> dict[str, str]:
        """Build environment dict for agent process. Public for testing."""
        agent_home = Path(os.environ.get(
            "AGENT_HOME",
            "/home/free/code/milohax/dc3-decomp/agent-home",
        ))
        agent_home.mkdir(parents=True, exist_ok=True)

        http_proxy_port = os.environ.get("CLAUDE_CODE_HOST_HTTP_PROXY_PORT")
        socks_proxy_port = os.environ.get("CLAUDE_CODE_HOST_SOCKS_PROXY_PORT")

        env: dict[str, str] = {
            "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC": "1",
            "HOME": str(agent_home),
        }

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
        SDK auto-detects OAuth credentials, but we still need to set
        OpenRouter environment variables when that backend is enabled
        or when the model requires OpenRouter.
        """
        env: dict[str, str] = {}

        use_openrouter = _get_openrouter_enabled() or (model and requires_openrouter(model))

        if use_openrouter and _get_openrouter_api_key():
            env["ANTHROPIC_BASE_URL"] = _get_openrouter_base_url()
            env["ANTHROPIC_AUTH_TOKEN"] = _get_openrouter_api_key()
            env["ANTHROPIC_API_KEY"] = ""  # Must be explicitly empty

            if model and requires_openrouter(model):
                openrouter_model_id = get_model_id(model)
                env["ANTHROPIC_DEFAULT_SONNET_MODEL"] = openrouter_model_id

        return env

    def build_sdk_options(self, config: AgentRunConfig) -> Any:
        """Build SDK options from config. Public for testing.

        Returns ClaudeAgentOptions (or raises if SDK not available).
        """
        if not SDK_AVAILABLE:
            raise RuntimeError("claude-agent-sdk not installed")

        if requires_openrouter(config.model):
            cli_model = "sonnet"
        else:
            cli_model = get_model_id(config.model)

        env = self.build_env(config.model)
        env.update(self.build_auth_env(config.model))
        env["REPO_ROOT"] = str(config.worktree)

        mcp_config: McpStdioServerConfig = {
            "command": "python3",
            "args": ["-m", "scripts.orchestrator.mcp_server", "--db", str(self.db_path)],
            "env": {"PYTHONPATH": str(self.main_repo)},
        }

        tools = config.effective_tools
        if config.disallowed_tools:
            disallowed = config.disallowed_tools
        elif config.model == "haiku":
            disallowed = ["Task", "TaskOutput", "TodoWrite", "Skill"]
        else:
            disallowed = []

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
        )

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
                            # Show match% from objdiff results
                            match = re.search(r'"match_percent":\s*([\d.]+)', content_str)
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

        use_openrouter = _get_openrouter_enabled() or requires_openrouter(config.model)
        if config.verbose >= 2 and use_openrouter and _get_openrouter_api_key():
            actual_model = get_model_id(config.model)
            pfx = _clr.colored_prefix(config.session_id)
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
            if config.verbose >= 1:
                pfx = _clr.colored_prefix(config.session_id)
                print(f"\n{pfx}{_clr.BOLD_RED}Process error:{_clr.RESET} {error_msg}")
            return {
                "exit_code": e.exit_code if hasattr(e, 'exit_code') else 1,
                "error": error_msg,
                "messages": [],
                "output": getattr(e, 'stderr', ''),
            }

        except Exception as e:
            error_msg = str(e)
            if config.verbose >= 1:
                pfx = _clr.colored_prefix(config.session_id)
                print(f"\n{pfx}{_clr.BOLD_RED}Unexpected error:{_clr.RESET} {error_msg}")
            return {"exit_code": 1, "error": error_msg, "messages": [], "output": ""}

    async def _run_process(self, config: AgentRunConfig) -> dict[str, Any]:
        """Run Claude CLI agent as subprocess."""
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

        env = {**os.environ, **self.build_env(config.model)}

        if _get_openrouter_enabled() and _get_openrouter_api_key():
            env["ANTHROPIC_BASE_URL"] = _get_openrouter_base_url()
            env["ANTHROPIC_API_KEY"] = _get_openrouter_api_key()
            if config.verbose >= 2:
                pfx = _clr.colored_prefix(config.session_id)
                print(f"{pfx}Using OpenRouter backend at {_get_openrouter_base_url()}")
        else:
            oauth_token = get_oauth_token()
            if oauth_token:
                env["CLAUDE_CODE_OAUTH_TOKEN"] = oauth_token

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
