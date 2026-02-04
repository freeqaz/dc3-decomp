# Orchestrate Quick Start: Incremental Builds

## TL;DR - 30 Second Guide

```bash
# Fast batch processing (incremental + validation)
./bin/orchestrate batch "src/system/char/*.cpp" --max-agents 3 --limit 30

# Super fast (incremental only)
./bin/orchestrate batch "src/system/char/*.cpp" --incremental-only --max-agents 5 --limit 100

# Super safe (full builds)
./bin/orchestrate batch "src/system/char/*.cpp" --full-build --max-agents 2 --limit 10

# Single function
./bin/orchestrate single "Character::Poll"
./bin/orchestrate single "Character::Poll" --incremental-only
```

## What's New

- `--incremental-only`: All builds fast (~15s each) - **NEW**
- `--full-build`: All builds comprehensive (~88s each)
- `--periodic-full N`: Run full build every Nth batch for validation (default: 10)
- `--validate-diffs`: Extra validation (experimental)

Default behavior: **incremental + periodic full builds** (5x faster than full, with validation)

## Performance Comparison

```
Speed (per function):
  Incremental only   ████ 15 seconds
  Incremental+full   █████ 20 seconds (includes periodic validation)
  Full build         ██████████████████ 88 seconds

Wall-clock (30 functions, 3 agents):
  Incremental only   ████ 2.5 minutes
  Incremental+full   █████ 3.3 minutes
  Full build         ███████████████ 14.7 minutes
```

## Strategy Picker

```
Do you know it works?
├─ YES (already validated elsewhere)
│   └─ Use: --incremental-only (fastest)
│
Do you want validation?
├─ YES
│   └─ Use: default (incremental + periodic full)
│
Do you need 100% certainty?
├─ YES
│   └─ Use: --full-build (safest)
```

## Common Workflows

### Workflow 1: Batch Processing (Recommended)

```bash
# Process 50 functions with validation
./bin/orchestrate batch "src/system/char/*.cpp" \
  --max-agents 3 \
  --limit 50

# Estimated time: 6-7 minutes
# Includes: periodic full build every 10 batches
```

### Workflow 2: Fast Screening

```bash
# Quick screening of 100 functions
./bin/orchestrate batch "src/system/char/*.cpp" \
  --incremental-only \
  --max-agents 5 \
  --limit 100

# Estimated time: 5 minutes
# Warning: No full build validation
```

### Workflow 3: Small Batch (Conservative)

```bash
# Critical functions only
./bin/orchestrate batch "src/system/char/*.cpp" \
  --full-build \
  --max-agents 2 \
  --limit 10

# Estimated time: 5 minutes
# Guarantees: Every build fully validated
```

### Workflow 4: Single Function Testing

```bash
# Test a single function (incremental)
./bin/orchestrate single "Character::Poll"

# Test a single function (full build)
./bin/orchestrate single "Character::Poll" --full-build

# Estimated time: 15-20s (incremental) or 90s (full)
```

## Output Examples

### Incremental Run

```
[1/3] Spawned: Character::Poll (inc)
[2/3] Spawned: Character::Update (inc)
[3/3] Spawned: Game::Poll (inc)

[Batch 1] Running full build validation...
[1/3] Spawned: PartyMode::Init (full)
[2/3] Spawned: World::Draw (inc)
[3/3] Spawned: Audio::Process (inc)

============================================================
Batch complete!
Processed: 30 functions in 182.3s
Build strategy: incremental
Periodic full builds: Every 10 batches
Errors: 0
Improvements: 18
Total gain: +87.3%
============================================================
```

### Full Build Run

```
[1/2] Spawned: Character::Poll (full)
[2/2] Spawned: Character::Update (full)

[1/2] Spawned: Game::Poll (full)
[2/2] Spawned: PartyMode::Init (full)

============================================================
Batch complete!
Processed: 10 functions in 440.5s
Build strategy: full
Errors: 0
Improvements: 8
Total gain: +42.5%
============================================================
```

## Typical Times

### Incremental Builds

- **Single function**: 15-20 seconds
- **3 parallel agents**: 5-7 seconds wall-clock per function
- **30 functions**: 2.5-3.5 minutes total

### Full Builds

- **Single function**: 85-90 seconds
- **2 parallel agents**: 45 seconds wall-clock per function
- **10 functions**: 7-10 minutes total

### Default (Incremental + Periodic Full)

- **Single function**: 15-20 seconds (or 90s for periodic full)
- **3 parallel agents**: 6-7 seconds wall-clock per function
- **30 functions**: 3-4 minutes total (includes 1-2 full builds)

## When to Use What

| Scenario | Strategy | Time | Why |
|----------|----------|------|-----|
| Large batch (>50) | Incremental-only | ~5 min/50 | Speed matters most |
| Normal batch (20-50) | Default | ~3-4 min/30 | Balanced |
| Small batch (<20) | Full-build | ~5-10 min | Safety matters most |
| Pre-screening | Incremental-only | Fast | Just exploring |
| Final validation | Full-build | Slow | Guarantee correctness |
| Production run | Default | Medium | Best of both |

## Advanced Options

```bash
# Custom periodic interval (full build every 5 batches instead of 10)
./bin/orchestrate batch "src/system/char/*.cpp" \
  --periodic-full 5 \
  --limit 50

# Disable periodic validation entirely
./bin/orchestrate batch "src/system/char/*.cpp" \
  --periodic-full 0 \
  --limit 50

# With validation debugging
./bin/orchestrate batch "src/system/char/*.cpp" \
  --validate-diffs \
  --periodic-full 10 \
  --limit 30

# Run with less output
./bin/orchestrate batch "src/system/char/*.cpp" \
  --incremental-only \
  --quiet \
  --limit 100
```

## Tips & Tricks

1. **Start with default**: Unless you have a specific reason, use default strategy
2. **Monitor output**: Watch for "LINKER_MERGED" verdict → switch to full build
3. **Use JSON output**: `--json` for scripting and analysis
4. **Query first**: Use `query` to understand what you're processing before `batch`
5. **Watch for errors**: Errors usually mean full build needed

```bash
# Query first
./bin/orchestrate query --pattern "src/system/char/*" --max-percent 50 --limit 20

# Then run batch
./bin/orchestrate batch "src/system/char/*.cpp" --max-percent 50 --limit 20
```

## Troubleshooting

### Build is too slow
- Use `--incremental-only` (but no validation)
- Increase `--max-agents`
- Use `--periodic-full 0` to skip periodic full builds

### Getting wrong results
- Use `--full-build` for comprehensive validation
- Check `--validate-diffs` for diagnostic output
- Reduce `--periodic-full` interval

### Not sure which strategy to use
- Default (no flags) is safe choice
- If unsure, use `--full-build` for critical functions
- Use `--incremental-only` for exploration

## Using OpenRouter

OpenRouter provides access to Claude models at lower costs and with additional providers.

### Setup

1. Get an API key from [OpenRouter](https://openrouter.ai/)

2. Configure environment variables:
   ```bash
   export USE_OPENROUTER=true
   export OPENROUTER_API_KEY="sk-or-v1-..."
   ```

3. Verify configuration:
   ```bash
   ./bin/orchestrate info
   ```

4. Run normally - orchestrator will use OpenRouter automatically:
   ```bash
   ./bin/orchestrate batch "src/system/char/*.cpp"
   ```

### Cost Comparison

| Model | Anthropic | OpenRouter | Savings |
|-------|-----------|------------|---------|
| Haiku | $0.25     | $0.10      | 60%     |
| Sonnet| $3.00     | $1.50      | 50%     |
| Opus  | $15.00    | $7.50      | 50%     |

*Estimates per function based on typical decomp work (Jan 2026)*

### Switching Backends

To switch back to native Anthropic:
```bash
unset USE_OPENROUTER
# or
export USE_OPENROUTER=false
```

### Troubleshooting

**Error: "Invalid model ID"**
- Check that OpenRouter API key is set correctly
- Verify model IDs in `scripts/orchestrator/model_selection.py`

**Error: "Unauthorized"**
- Check API key has sufficient credits
- Verify key starts with `sk-or-v1-`

**Slow response times**
- OpenRouter may have higher latency than direct Anthropic
- Consider using native backend for time-critical work

**Error: "Unable to connect"**
- Verify internet connection
- Check that firewall allows access to `openrouter.ai`
- Try setting OPENROUTER_BASE_URL explicitly: `export OPENROUTER_BASE_URL="https://openrouter.ai/api/v1"`

## See Also

- [ORCHESTRATE_INCREMENTAL_BUILDS.md](./ORCHESTRATE_INCREMENTAL_BUILDS.md) - Full documentation
- [IMPLEMENTATION_PLAN_2026-01-25.md](./IMPLEMENTATION_PLAN_2026-01-25.md) - Implementation details
