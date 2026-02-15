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

# Always load .env.zai if it exists (provides ZAI_* credentials).
# This lets `--model glm-5` work without manually sourcing anything.
_zai_env_file = Path(__file__).resolve().parent.parent.parent / ".env.zai"
if _zai_env_file.exists():
    from dotenv import load_dotenv as _load_dotenv_zai
    _load_dotenv_zai(_zai_env_file, override=False)  # Don't override existing env vars

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

def _get_zai_enabled() -> bool:
    """Check if Z.AI backend is enabled via environment variable."""
    return os.getenv("USE_ZAI", "false").lower() == "true"

def _get_zai_api_key() -> str:
    """Get Z.AI API key from environment."""
    return os.getenv("ZAI_API_KEY", "")

def _get_zai_base_url() -> str:
    """Get Z.AI base URL from environment."""
    return os.getenv("ZAI_BASE_URL", "https://api.z.ai/api/anthropic")

def _get_zai_timeout() -> str:
    """Get Z.AI API timeout from environment."""
    return os.getenv("ZAI_API_TIMEOUT_MS", "3000000")

# For backward compatibility, also expose as properties
# These allow code to do: from config import OPENROUTER_API_KEY
OPENROUTER_API_KEY = _get_openrouter_api_key()
OPENROUTER_BASE_URL = _get_openrouter_base_url()
ZAI_API_KEY = _get_zai_api_key()
ZAI_BASE_URL = _get_zai_base_url()
ZAI_TIMEOUT = _get_zai_timeout()

BackendType = Literal["anthropic", "openrouter", "zai"]

# ============================================================================
# CENTRAL MODEL REGISTRY - Single source of truth for all models
# ============================================================================
# Format: {backend: {model_name: {model_id, token_budget, prompt_rate, completion_rate}}}
# Rates are $/M tokens (dollars per million tokens).
# Add new models here only!

# Default token assumptions for per-function cost estimates
_DEFAULT_INPUT_TOKENS = 5000
_DEFAULT_OUTPUT_TOKENS = 20000


def estimate_per_function_cost(prompt_rate: float, completion_rate: float) -> float:
    """Estimate per-function cost from token rates.

    Uses default token assumptions (~5K input + ~20K output per function).

    Args:
        prompt_rate: Cost per million input tokens ($/M)
        completion_rate: Cost per million output tokens ($/M)

    Returns:
        Estimated cost in USD for one function attempt
    """
    return (prompt_rate * _DEFAULT_INPUT_TOKENS + completion_rate * _DEFAULT_OUTPUT_TOKENS) / 1_000_000


MODEL_REGISTRY = {
    "anthropic": {
        # Anthropic direct API pricing (Feb 2026):
        # Rates in $/M tokens
        "haiku": {
            "model_id": "haiku",
            "token_budget": 10000,
            "prompt_rate": 1.00,       # $/M input tokens
            "completion_rate": 5.00,   # $/M output tokens
        },
        "sonnet": {
            "model_id": "sonnet",
            "token_budget": 20000,
            "prompt_rate": 3.00,
            "completion_rate": 15.00,
        },
        "opus": {
            "model_id": "opus",
            "token_budget": 30000,
            "prompt_rate": 5.00,
            "completion_rate": 25.00,
        },
    },
    "openrouter": {
        # OpenRouter pricing (Feb 2026) - rates in $/M tokens
        # Claude models via OpenRouter (same pricing as Anthropic direct)
        "haiku": {
            "model_id": "anthropic/claude-haiku-4.5",
            "token_budget": 10000,
            "prompt_rate": 1.00,
            "completion_rate": 5.00,
        },
        "sonnet": {
            "model_id": "anthropic/claude-sonnet-4.5",
            "token_budget": 20000,
            "prompt_rate": 3.00,
            "completion_rate": 15.00,
        },
        "opus": {
            "model_id": "anthropic/claude-opus-4.6",
            "token_budget": 30000,
            "prompt_rate": 5.00,
            "completion_rate": 25.00,
        },
        # Alternative models - optimized for code/reasoning
        "glm-4.7": {
            "model_id": "z-ai/glm-4.7",
            "token_budget": 30000,
            "prompt_rate": 0.40,
            "completion_rate": 1.50,
        },
        "grok-code-fast-1": {
            "model_id": "x-ai/grok-code-fast-1",
            "token_budget": 25000,
            "prompt_rate": 0.20,
            "completion_rate": 1.50,
        },
        "minimax-m2.1": {
            "model_id": "minimax/minimax-m2.1",
            "token_budget": 20000,
            "prompt_rate": 0.27,
            "completion_rate": 0.95,
        },
        "deepseek-v3.2": {
            "model_id": "deepseek/deepseek-chat-v3-0324",
            "token_budget": 25000,
            "prompt_rate": 0.19,
            "completion_rate": 0.87,
        },
        "gemini-3-flash": {
            "model_id": "google/gemini-3-flash-preview",
            "token_budget": 15000,
            "prompt_rate": 0.50,
            "completion_rate": 3.00,
        },
        "gemini-3-pro": {
            "model_id": "google/gemini-3-pro-preview",
            "token_budget": 25000,
            "prompt_rate": 2.00,
            "completion_rate": 12.00,
        },
        "kimi-k2.5": {
            "model_id": "moonshotai/kimi-k2.5",
            "token_budget": 20000,
            "prompt_rate": 0.45,
            "completion_rate": 2.50,
        },
        "gpt-oss-120b": {
            "model_id": "openai/gpt-oss-120b",
            "token_budget": 20000,
            "prompt_rate": 0.04,
            "completion_rate": 0.19,
        },
        "qwen/qwen3-coder": {
            "model_id": "qwen/qwen3-235b-a22b",
            "token_budget": 20000,
            "prompt_rate": 0.20,
            "completion_rate": 0.60,
        },
        "qwen3-coder-next": {
            "model_id": "qwen/qwen3-coder-next",
            "token_budget": 20000,
            "prompt_rate": 0.07,
            "completion_rate": 0.30,
        },
        "trinity-large": {
            "model_id": "arcee-ai/trinity-large-preview:free",
            "token_budget": 15000,
            "prompt_rate": 0.00,
            "completion_rate": 0.00,
        },
        "gpt-5.2": {
            "model_id": "openai/gpt-5.2",
            "token_budget": 25000,
            "prompt_rate": 1.75,
            "completion_rate": 14.00,
        },
        "step-3.5-flash": {
            "model_id": "stepfun/step-3.5-flash:free",
            "token_budget": 15000,
            "prompt_rate": 0.00,
            "completion_rate": 0.00,
        },
        "palmyra-x5": {
            "model_id": "writer/palmyra-x5",
            "token_budget": 20000,
            "prompt_rate": 0.60,
            "completion_rate": 6.00,
        },
        "gpt-5.2-codex": {
            "model_id": "openai/gpt-5.2-codex",
            "token_budget": 25000,
            "prompt_rate": 1.75,
            "completion_rate": 14.00,
        },
        "olmo-3.1-32b": {
            "model_id": "allenai/olmo-3.1-32b-think",
            "token_budget": 15000,
            "prompt_rate": 0.15,
            "completion_rate": 0.50,
        },
    },
    "zai": {
        # Z.AI backend - GLM models optimized for code/reasoning
        "glm-4.7": {
            "model_id": "glm-4.7",
            "token_budget": 30000,
            "prompt_rate": 0.40,
            "completion_rate": 1.50,
        },
        "glm-5": {
            "model_id": "glm-5",
            "token_budget": 35000,
            "prompt_rate": 0.50,
            "completion_rate": 2.00,
        },
    },
}


def get_available_models(backend: Optional[str] = None) -> list[str]:
    """Get list of available model names for a backend.

    Args:
        backend: Backend name ("anthropic", "openrouter", or "zai").
                 If None, uses current backend. Use "all" for all backends.

    Returns:
        Sorted list of model names available for the backend
    """
    if backend == "all":
        all_models: set[str] = set()
        for models in MODEL_REGISTRY.values():
            all_models.update(models.keys())
        return sorted(all_models)
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


def _derive_zai_only_models() -> set[str]:
    """Derive Z.AI-only models from registry (don't maintain separately)."""
    # All models in zai backend are exclusive to Z.AI
    return set(MODEL_REGISTRY.get("zai", {}).keys())


# Backward compatibility: derive these from registry
OPENROUTER_ONLY_MODELS = _derive_openrouter_only_models()
ZAI_ONLY_MODELS = _derive_zai_only_models()

# Backward compatibility: derive from registry
TOKEN_BUDGETS = {
    backend: {
        name: info["token_budget"]
        for name, info in models.items()
    }
    for backend, models in MODEL_REGISTRY.items()
}


def requires_openrouter(model: str) -> bool:
    """Check if model requires OpenRouter backend.

    Args:
        model: Model name (e.g., "deepseek-v3.2")

    Returns:
        True if model is only available via OpenRouter
    """
    return model in OPENROUTER_ONLY_MODELS


def requires_zai(model: str) -> bool:
    """Check if model requires Z.AI backend.

    Args:
        model: Model name (e.g., "glm-4.7", "glm-5")

    Returns:
        True if model is only available via Z.AI
    """
    return model in ZAI_ONLY_MODELS


def get_backend(model: str = None) -> BackendType:
    """Get current backend type.

    Returns "zai" if:
    - A Z.AI-only model is specified (glm-4.7, glm-5), OR
    - Z.AI is enabled via USE_ZAI=true and API key is configured

    Returns "openrouter" if:
    - An OpenRouter-only model is specified, OR
    - OpenRouter is enabled via USE_OPENROUTER=true and API key is configured

    Otherwise returns "anthropic" (default backend).

    This function checks environment variables at call time to support runtime
    configuration changes (e.g., USE_OPENROUTER=true on command line).

    Args:
        model: Optional model name. If a backend-specific model, auto-selects that backend.

    Returns:
        BackendType: Either "zai", "openrouter", or "anthropic"
    """
    # Auto-select Z.AI for Z.AI-only models (highest priority)
    if model and requires_zai(model):
        return "zai"
    # Auto-select OpenRouter for OpenRouter-only models
    if model and requires_openrouter(model):
        return "openrouter"
    # Check explicit backend enablement (Z.AI takes priority over OpenRouter)
    if _get_zai_enabled() and _get_zai_api_key():
        return "zai"
    if _get_openrouter_enabled() and _get_openrouter_api_key():
        return "openrouter"
    return "anthropic"


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
