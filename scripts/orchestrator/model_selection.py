"""Model selection and escalation logic.

Determines which Claude model to use based on function characteristics
and attempt history.
"""

from typing import Any, Optional

from .config import get_backend, get_token_budget, estimate_per_function_cost


def select_model(func: dict[str, Any], force_model: Optional[str] = None) -> str:
    """
    Select model based on function characteristics and attempt history.

    Model escalation strategy:
    - First attempt: Haiku (cheap exploration)
    - Second attempt: Sonnet (more capable)
    - Third+ attempt: Opus (only if near-match, worth the cost)

    Args:
        func: Function dict from database
        force_model: Override model selection

    Returns:
        Model name: "haiku", "sonnet", or "opus"
    """
    if force_model:
        return force_model

    attempt_count = func.get("attempt_count", 0)
    percent = func.get("current_percent") or 0

    # First attempt: always Haiku (cheap exploration)
    if attempt_count == 0:
        return "haiku"

    # Second attempt: Sonnet (more capable)
    if attempt_count == 1:
        return "sonnet"

    # Third+ attempt: Opus only if close to matching (worth the cost)
    if attempt_count >= 2:
        if percent >= 90:
            return "opus"  # Near-match, Opus might crack it
        else:
            return "sonnet"  # Not close enough for Opus cost

    return "haiku"  # Fallback


def should_retry(func: dict[str, Any], last_attempt: Optional[dict[str, Any]] = None) -> bool:
    """
    Decide if function should be retried.

    Args:
        func: Function dict from database
        last_attempt: Most recent attempt dict

    Returns:
        True if should retry, False if should give up
    """
    verdict = func.get("verdict", "")
    attempt_count = func.get("attempt_count", 0)

    # Already complete or at limit - don't retry
    if verdict in ("COMPLETE", "AT_LIMIT"):
        return False

    # Too many attempts - give up
    if attempt_count >= 5:
        return False

    # No progress after Opus? Give up
    if last_attempt:
        if (
            last_attempt.get("model") == "opus"
            and last_attempt.get("exit_status") != "complete"
        ):
            # Opus tried and failed/stuck - mark as at limit
            return False

        # Stuck twice in a row? Give up
        if last_attempt.get("exit_status") == "stuck" and attempt_count >= 2:
            return False

    return True


def get_escalation_reason(func: dict[str, Any], new_model: str) -> str:
    """Get human-readable reason for model escalation."""
    attempt_count = func.get("attempt_count", 0)
    percent = func.get("current_percent") or 0
    last_model = func.get("last_model", "none")

    if new_model == "haiku":
        return "First attempt - using Haiku for cheap exploration"

    if new_model == "sonnet":
        if last_model == "haiku":
            return f"Haiku unsuccessful after {attempt_count} attempt(s) - escalating to Sonnet"
        return "Using Sonnet for better reasoning"

    if new_model == "opus":
        return f"Near-match at {percent:.1f}% - escalating to Opus for final push"

    return f"Using {new_model}"


# Backward compatibility: derive MODEL_MAPS from registry
# This allows existing code that uses MODEL_MAPS to still work
from .config import MODEL_REGISTRY

MODEL_MAPS = {
    "anthropic": {
        name: info["model_id"]
        for name, info in MODEL_REGISTRY.get("anthropic", {}).items()
    },
    "openrouter": {
        name: info["model_id"]
        for name, info in MODEL_REGISTRY.get("openrouter", {}).items()
    },
}

# Backward compatibility: derive COST_TABLES from registry token rates
COST_TABLES = {
    backend: {
        name: estimate_per_function_cost(info["prompt_rate"], info["completion_rate"])
        for name, info in models.items()
    }
    for backend, models in MODEL_REGISTRY.items()
}

# Anthropic Claude model IDs (used for cache pricing detection)
_ANTHROPIC_MODEL_PREFIXES = ("anthropic/", "claude")


def compute_cost_from_tokens(
    model: str,
    input_tokens: int,
    output_tokens: int,
    cache_read_tokens: int = 0,
    cache_creation_tokens: int = 0,
    backend: str | None = None,
) -> float | None:
    """Compute actual cost from token counts using registry rates.

    For Anthropic/Claude models, applies cache pricing:
    - cache_read: 10% of prompt_rate
    - cache_creation: 125% of prompt_rate

    For non-Anthropic models, cache tokens are treated as regular input.

    Args:
        model: Model name (e.g., "haiku", "sonnet", "deepseek-v3.2")
        input_tokens: Number of input tokens
        output_tokens: Number of output tokens
        cache_read_tokens: Number of cache read tokens (Anthropic only)
        cache_creation_tokens: Number of cache creation tokens (Anthropic only)
        backend: Backend name. If None, auto-detects from model.

    Returns:
        Cost in USD, or None if model not found in registry
    """
    if backend is None:
        from .config import get_backend
        backend = get_backend(model)

    info = MODEL_REGISTRY.get(backend, {}).get(model, {})
    if not info:
        return None

    prompt_rate = info["prompt_rate"]
    completion_rate = info["completion_rate"]

    # Check if this is a Claude/Anthropic model (gets cache pricing)
    model_id = info.get("model_id", "")
    is_anthropic = (
        backend == "anthropic"
        or any(model_id.startswith(p) for p in _ANTHROPIC_MODEL_PREFIXES)
    )

    if is_anthropic:
        # Anthropic cache pricing: read at 10%, creation at 125% of prompt rate
        input_cost = prompt_rate * input_tokens / 1_000_000
        cache_read_cost = prompt_rate * 0.10 * cache_read_tokens / 1_000_000
        cache_creation_cost = prompt_rate * 1.25 * cache_creation_tokens / 1_000_000
        output_cost = completion_rate * output_tokens / 1_000_000
        return input_cost + cache_read_cost + cache_creation_cost + output_cost
    else:
        # Non-Anthropic: cache tokens count as regular input
        total_input = input_tokens + cache_read_tokens + cache_creation_tokens
        input_cost = prompt_rate * total_input / 1_000_000
        output_cost = completion_rate * output_tokens / 1_000_000
        return input_cost + output_cost


def estimate_batch_cost(functions: list[dict], model: Optional[str] = None) -> dict:
    """
    Estimate cost for a batch of functions.

    Args:
        functions: List of function dicts
        model: Force specific model, otherwise auto-select per function

    Returns:
        Dict with cost breakdown
    """
    backend = get_backend()
    cost_tables = COST_TABLES[backend]
    model_list = list(cost_tables.keys())

    costs = {m: 0 for m in model_list}
    costs["total"] = 0.0
    costs["count"] = len(functions)

    for func in functions:
        m = model if model else select_model(func)
        costs[m] += 1
        costs["total"] += cost_tables.get(m, 1.0)

    return costs


def get_model_id(model_tier: str) -> str:
    """Get API model ID for current backend.

    Args:
        model_tier: Model tier ("haiku", "sonnet", "opus", or OpenRouter-only models)

    Returns:
        Model ID string for the current backend
    """
    backend = get_backend(model_tier)  # Pass model for auto-detection
    return MODEL_MAPS[backend][model_tier]


def get_model_cost(model_tier: str) -> float:
    """Get estimated cost per function for current backend.

    Args:
        model_tier: Model tier ("haiku", "sonnet", "opus")

    Returns:
        Estimated cost in USD
    """
    backend = get_backend()
    return COST_TABLES[backend][model_tier]
