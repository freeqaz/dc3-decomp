# Z.AI Integration Guide

Z.AI provides access to GLM models (glm-4.7, glm-5) optimized for code reasoning tasks. This guide covers using Z.AI with both the Claude CLI and the orchestrator.

## Setup

The Z.AI credentials are stored in `.env.zai` (auto-loaded by the orchestrator):

```bash
# Z.AI credentials for Claude CLI (used by claude-with-provider.sh)
ANTHROPIC_AUTH_TOKEN=<your-token>
ANTHROPIC_BASE_URL=https://api.z.ai/api/anthropic
API_TIMEOUT_MS=3000000

# Z.AI credentials for orchestrator (auto-loaded, auto-routes glm-* models)
ZAI_API_KEY=<your-token>
ZAI_BASE_URL=https://api.z.ai/api/anthropic
ZAI_API_TIMEOUT_MS=3000000
```

## Using Z.AI with Orchestrator

Just pass `--model glm-5` or `--model glm-4.7`. No sourcing or env setup needed — the orchestrator auto-loads `.env.zai` and routes GLM models through Z.AI:

```bash
# Batch processing with GLM-5
./bin/orchestrate batch "*" --model glm-5 --limit 10

# Single function
./bin/orchestrate single "?Poll@CharMirror@@UAEXXZ" --model glm-4.7

# See all available models including Z.AI
./bin/orchestrate info
```

## Using Z.AI with Claude CLI

Use the `claude-with-provider.sh` wrapper script:

```bash
# Interactive session
./claude-with-provider.sh zai

# One-shot prompt
./claude-with-provider.sh zai "Explain this function"
```

## Available Models

| Model | Token Budget | Cost ($/M tokens) | Notes |
|-------|--------------|-------------------|-------|
| glm-4.7 | 30,000 | $0.40 / $1.50 | Optimized for code/reasoning |
| glm-5 | 35,000 | $0.50 / $2.00 | More capable, higher budget |

## Backend Selection

The orchestrator automatically selects the Z.AI backend when you specify a GLM model. No env vars needed — just `--model glm-5`.

Default backend for haiku/sonnet/opus remains Anthropic (or OpenRouter if configured).

Priority order: Z.AI (for glm-*) > OpenRouter (if enabled) > Anthropic (default)

## Integration Details

When using GLM models, the orchestrator:
1. Auto-loads `.env.zai` from project root
2. Sets `ANTHROPIC_BASE_URL` to Z.AI endpoint
3. Sets `ANTHROPIC_AUTH_TOKEN` to your Z.AI API key
4. Routes the model through `ANTHROPIC_DEFAULT_SONNET_MODEL`
5. Applies the extended timeout (`API_TIMEOUT_MS=3000000`)

The Claude CLI sees the model as "sonnet" but Z.AI receives the actual GLM model ID.
