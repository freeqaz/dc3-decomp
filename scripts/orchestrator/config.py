"""Orchestrator configuration for API backends.

Central model registry: Define all models once, use everywhere.
Adding a new model requires updates in only 3 files:
  1. scripts/orchestrator/config.py (MODEL_REGISTRY)
  2. scripts/orchestrator/model_selection.py (if needed for cost estimates)
  3. That's it! CLI choices are generated dynamically from registry.
"""
import os
from pathlib import Path
from typing import Literal, Optional

# Load .env file from project root if not already loaded
if not os.getenv("GHIDRA_INSTALL_DIR"):
    env_file = Path(__file__).resolve().parent.parent.parent / ".env"
    if env_file.exists():
        from dotenv import load_dotenv
        load_dotenv(env_file)

# Backend selection (read from environment, support both .env and command-line)
# Note: These are evaluated each time they're accessed to support runtime env var changes
def _get_openrouter_enabled() -> bool:
    """Check if OpenRouter backend is enabled via environment variable."""
    return os.getenv("USE_OPENROUTER", "false").lower() == "true"

def _get_openrouter_api_key() -> str:
    """Get OpenRouter API key from environment."""
    return os.getenv("OPENROUTER_API_KEY", "")

def _get_openrouter_base_url() -> str:
    """Get OpenRouter base URL from environment.

    Note: OpenRouter's Anthropic-compatible API uses /api (not /api/v1).
    The Claude Code CLI appends the appropriate path suffix.
    """
    return os.getenv("OPENROUTER_BASE_URL", "https://openrouter.ai/api")

# For backward compatibility, also expose as properties
# These allow code to do: from config import OPENROUTER_API_KEY
OPENROUTER_API_KEY = _get_openrouter_api_key()
OPENROUTER_BASE_URL = _get_openrouter_base_url()

BackendType = Literal["anthropic", "openrouter"]

# ============================================================================
# CENTRAL MODEL REGISTRY - Single source of truth for all models
# ============================================================================
# Format: {backend: {model_name: {model_id, token_budget, cost (OR)}}
# Add new models here only!
MODEL_REGISTRY = {
    "anthropic": {
        # Anthropic direct API pricing (Feb 2026):
        # Haiku: $1/M input, $5/M output
        # Sonnet: $3/M input, $15/M output
        # Opus: $5/M input, $25/M output
        # Per-function estimate assumes ~5K input + ~20K output tokens
        "haiku": {
            "model_id": "haiku",
            "token_budget": 10000,
            "cost": 0.105,  # $1*5K + $5*20K = $0.105 per function
        },
        "sonnet": {
            "model_id": "sonnet",
            "token_budget": 20000,
            "cost": 0.315,  # $3*5K + $15*20K = $0.315 per function
        },
        "opus": {
            "model_id": "opus",
            "token_budget": 30000,
            "cost": 0.525,  # $5*5K + $25*20K = $0.525 per function
        },
    },
    "openrouter": {
        # OpenRouter pricing (Feb 2026) - per-function estimates
        # Assumes ~5K input + ~20K output tokens per function
        # Claude models via OpenRouter (same pricing as Anthropic direct)
        "haiku": {
            "model_id": "anthropic/claude-haiku-4.5",
            "token_budget": 10000,
            "cost": 0.105,  # $1/M in + $5/M out
        },
        "sonnet": {
            "model_id": "anthropic/claude-sonnet-4.5",
            "token_budget": 20000,
            "cost": 0.315,  # $3/M in + $15/M out
        },
        "opus": {
            "model_id": "anthropic/claude-opus-4.6",
            "token_budget": 30000,
            "cost": 0.525,  # $5/M in + $25/M out
        },
        # Alternative models - optimized for code/reasoning
        "glm-4.7": {
            "model_id": "z-ai/glm-4.7",
            "token_budget": 30000,
            "cost": 0.032,  # $0.40/M in + $1.50/M out
        },
        "grok-code-fast-1": {
            "model_id": "x-ai/grok-code-fast-1",
            "token_budget": 25000,
            "cost": 0.031,  # $0.20/M in + $1.50/M out
        },
        "minimax-m2.1": {
            "model_id": "minimax/minimax-m2.1",
            "token_budget": 20000,
            "cost": 0.007,  # ~$0.10/M in + $0.30/M out (estimate)
        },
        "deepseek-v3.2": {
            "model_id": "deepseek/deepseek-chat-v3-0324",
            "token_budget": 25000,
            "cost": 0.018,  # $0.19/M in + $0.87/M out
        },
        "gemini-3-flash": {
            "model_id": "google/gemini-2.5-flash-preview",
            "token_budget": 15000,
            "cost": 0.009,  # ~$0.10/M in + $0.40/M out (estimate)
        },
        "gpt-oss-120b": {
            "model_id": "openai/gpt-oss-120b",
            "token_budget": 20000,
            "cost": 0.021,  # ~$0.20/M in + $1.00/M out (estimate)
        },
        "qwen/qwen3-coder": {
            "model_id": "qwen/qwen3-235b-a22b",
            "token_budget": 20000,
            "cost": 0.013,  # $0.20/M in + $0.60/M out
        },
    },
}


def get_available_models(backend: Optional[str] = None) -> list[str]:
    """Get list of available model names for a backend.

    Args:
        backend: Backend name ("anthropic" or "openrouter"). If None, uses current backend.

    Returns:
        Sorted list of model names available for the backend
    """
    if backend is None:
        backend = get_backend()
    return sorted(MODEL_REGISTRY.get(backend, {}).keys())


def get_model_info(model: str, backend: Optional[str] = None) -> dict:
    """Get model configuration info from registry.

    Args:
        model: Model name (e.g., "haiku", "glm-4.7")
        backend: Backend name. If None, uses current backend.

    Returns:
        Dict with model_id, token_budget, cost (if available)
    """
    if backend is None:
        backend = get_backend(model)
    return MODEL_REGISTRY.get(backend, {}).get(model, {})


def _derive_openrouter_only_models() -> set[str]:
    """Derive OpenRouter-only models from registry (don't maintain separately)."""
    anthropic_models = set(MODEL_REGISTRY.get("anthropic", {}).keys())
    all_openrouter_models = set(MODEL_REGISTRY.get("openrouter", {}).keys())
    return all_openrouter_models - anthropic_models


# Backward compatibility: derive these from registry
OPENROUTER_ONLY_MODELS = _derive_openrouter_only_models()

# Backward compatibility: derive from registry
TOKEN_BUDGETS = {
    "anthropic": {
        name: info["token_budget"]
        for name, info in MODEL_REGISTRY.get("anthropic", {}).items()
    },
    "openrouter": {
        name: info["token_budget"]
        for name, info in MODEL_REGISTRY.get("openrouter", {}).items()
    },
}


def requires_openrouter(model: str) -> bool:
    """Check if model requires OpenRouter backend.

    Args:
        model: Model name (e.g., "deepseek-v3.2")

    Returns:
        True if model is only available via OpenRouter
    """
    return model in OPENROUTER_ONLY_MODELS


def get_backend(model: str = None) -> BackendType:
    """Get current backend type.

    Returns "openrouter" if:
    - An OpenRouter-only model is specified, OR
    - OpenRouter is enabled via USE_OPENROUTER=true and API key is configured

    Otherwise returns "anthropic" (default backend).

    This function checks environment variables at call time to support runtime
    configuration changes (e.g., USE_OPENROUTER=true on command line).

    Args:
        model: Optional model name. If an OpenRouter-only model, auto-selects OpenRouter.

    Returns:
        BackendType: Either "openrouter" or "anthropic"
    """
    # Auto-select OpenRouter for OpenRouter-only models
    if model and requires_openrouter(model):
        return "openrouter"
    return "openrouter" if _get_openrouter_enabled() and _get_openrouter_api_key() else "anthropic"


def get_token_budget(model_tier: str) -> int:
    """Get token budget for model (used by SDK for extended thinking).

    These values are passed to the Claude Agent SDK via the max_thinking_tokens
    option. For models with thinking capability (Claude 4.x and alternatives),
    this enables extended reasoning chains during decomposition work.

    Token limits are ultimately enforced at the API level by Anthropic/OpenRouter.

    Args:
        model_tier: Model name (e.g., "haiku", "sonnet", "glm-4.7")

    Returns:
        Token budget for the model (used for max_thinking_tokens in SDK)
    """
    backend = get_backend(model_tier)
    info = get_model_info(model_tier, backend)
    budget = info.get("token_budget", 30000)
    # Allow override via environment (for testing/estimation only)
    env_budget = os.getenv("MAX_TOKEN_BUDGET")
    return int(env_budget) if env_budget else budget
