"""ANSI color support for orchestrator output.

Provides color codes, tool-name coloring, and per-session color cycling
so parallel agent output is visually distinguishable.
"""

import logging
import os
import re
import sys
import threading

# --- Detection ---

def _colors_enabled() -> bool:
    """Check if stdout supports color."""
    if os.getenv("NO_COLOR"):
        return False
    if os.getenv("FORCE_COLOR"):
        return True
    return hasattr(sys.stdout, "isatty") and sys.stdout.isatty()

USE_COLOR = _colors_enabled()


# --- ANSI codes ---

def _c(code: str) -> str:
    return code if USE_COLOR else ""

RESET = _c("\033[0m")
BOLD = _c("\033[1m")
DIM = _c("\033[2m")
RED = _c("\033[31m")
GREEN = _c("\033[32m")
YELLOW = _c("\033[33m")
BLUE = _c("\033[94m")  # bright blue (readable on dark bg)
MAGENTA = _c("\033[35m")
CYAN = _c("\033[36m")
WHITE = _c("\033[37m")
BOLD_RED = _c("\033[1;31m")
BOLD_GREEN = _c("\033[1;32m")
BOLD_YELLOW = _c("\033[1;33m")
BOLD_BLUE = _c("\033[1;94m")  # bright blue
BOLD_MAGENTA = _c("\033[1;35m")
BOLD_CYAN = _c("\033[1;36m")

# --- Tool colors ---

TOOL_COLORS: dict[str, str] = {
    "Read": BLUE,
    "Edit": YELLOW,
    "Write": YELLOW,
    "Glob": GREEN,
    "Grep": GREEN,
    "Bash": MAGENTA,
    "TodoWrite": DIM,
    "Task": CYAN,
}

DEFAULT_TOOL_COLOR = CYAN  # for MCP tools


# --- Per-session color cycling ---
# Each agent session gets a distinct color so interleaved output is readable.

_SESSION_COLORS = [
    BOLD_CYAN,
    BOLD_GREEN,
    BOLD_YELLOW,
    BOLD_MAGENTA,
    BOLD_BLUE,
    CYAN,
    GREEN,
    YELLOW,
    MAGENTA,
    BLUE,
]

_session_color_map: dict[str, str] = {}
_session_color_lock = threading.Lock()
_session_color_idx = 0


def session_color(session_id: str) -> str:
    """Get a stable color for a session ID, assigning one on first use."""
    global _session_color_idx
    with _session_color_lock:
        if session_id not in _session_color_map:
            _session_color_map[session_id] = _SESSION_COLORS[_session_color_idx % len(_SESSION_COLORS)]
            _session_color_idx += 1
        return _session_color_map[session_id]


def colored_prefix(session_id: str) -> str:
    """Return a colored [session_id] prefix string."""
    c = session_color(session_id)
    return f"{c}[{session_id}]{RESET} "


# --- Colored logging formatter ---

# Level -> (color, short label)
_LEVEL_STYLES: dict[int, tuple[str, str]] = {
    logging.DEBUG: (DIM, "DEBUG"),
    logging.INFO: (DIM, "INFO"),
    logging.WARNING: (YELLOW, "WARN"),
    logging.ERROR: (BOLD_RED, "ERROR"),
    logging.CRITICAL: (BOLD_RED, "CRIT"),
}

# Patterns in log messages to highlight
_HIGHLIGHT_PATTERNS: list[tuple[str, str]] = [
    (r"Pattern classification: (\S+)", None),  # handled specially
    (r"Verdict: (\S+)", None),  # handled specially
    (r"(\d+\.?\d*% match)", GREEN),
    (r"(MAYBE_FIXABLE|LIKELY_FIXABLE)", GREEN),
    (r"(UNFIXABLE|NOT_FIXABLE)", RED),
]


class ColoredFormatter(logging.Formatter):
    """Logging formatter that adds ANSI colors to console output.

    Colors:
    - Timestamp: dim
    - Level: colored by severity
    - Session tags [batch-xxx]: per-session cycling color
    - Key terms: highlighted (verdicts, match%, errors)
    """

    def __init__(self, fmt: str = None, datefmt: str = None):
        super().__init__(fmt or "[%(asctime)s] %(levelname)s: %(message)s", datefmt or "%H:%M:%S")

    def format(self, record: logging.LogRecord) -> str:
        if not USE_COLOR:
            return super().format(record)

        # Save originals
        orig_levelname = record.levelname
        orig_msg = record.msg

        # Color the level name
        level_color, level_label = _LEVEL_STYLES.get(record.levelno, (RESET, record.levelname))
        record.levelname = f"{level_color}{level_label}{RESET}"

        # Format first, then post-process the full string
        result = super().format(record)

        # Restore originals
        record.levelname = orig_levelname
        record.msg = orig_msg

        # Color timestamp (the [HH:MM:SS] part)
        result = re.sub(
            r"^\[(\d{2}:\d{2}:\d{2})\]",
            f"{DIM}[\\1]{RESET}",
            result,
        )

        # Color session tags [batch-xxx] or [session-xxx]
        def _color_session_tag(m: re.Match) -> str:
            tag = m.group(1)
            c = session_color(tag)
            return f"{c}[{tag}]{RESET}"

        result = re.sub(r"\[(batch-[^\]]+|session-[^\]]+)\]", _color_session_tag, result)

        # Highlight verdicts and key terms
        for pattern, color in _HIGHLIGHT_PATTERNS:
            if color:
                result = re.sub(pattern, f"{color}\\1{RESET}", result)

        # Special: color "Prompt section sizes" total
        result = re.sub(
            r"\(total ([\d.]+KB)\)",
            f"(total {BOLD}\\1{RESET})",
            result,
        )

        return result
