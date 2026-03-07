# AI-Guided Permuter — Cost Model

## Per-Call Costs

Based on prompt size estimates from [PROMPT_DESIGN.md](PROMPT_DESIGN.md).

### Tier 1: Pattern Applicator

| | Tokens | Cost/1K tokens | Cost/call |
|-|--------|----------------|-----------|
| Input | ~3,500 | $0.80 (Haiku) | $0.0028 |
| Output | ~400 | $4.00 (Haiku) | $0.0016 |
| **Total** | | | **~$0.005** |

With Sonnet (if Haiku quality proves insufficient):

| | Tokens | Cost/1K tokens | Cost/call |
|-|--------|----------------|-----------|
| Input | ~3,500 | $3.00 (Sonnet) | $0.0105 |
| Output | ~400 | $15.00 (Sonnet) | $0.0060 |
| **Total** | | | **~$0.017** |

### Tier 2: Novel Fix Advisor

| | Tokens | Cost/1K tokens | Cost/call |
|-|--------|----------------|-----------|
| Input | ~8,000 | $3.00 (Sonnet) | $0.024 |
| Output | ~600 | $15.00 (Sonnet) | $0.009 |
| **Total** | | | **~$0.033** |

With Opus (for hardest cases):

| | Tokens | Cost/1K tokens | Cost/call |
|-|--------|----------------|-----------|
| Input | ~8,000 | $15.00 (Opus) | $0.120 |
| Output | ~600 | $75.00 (Opus) | $0.045 |
| **Total** | | | **~$0.165** |

## Batch Economics

### Scenario: Full AT_LIMIT sweep

Assuming ~500 AT_LIMIT functions worth attempting (excluding ICF, guards, unfixable):

| Approach | Model | Cost | Expected hits | Cost/hit |
|----------|-------|------|---------------|----------|
| Tier 1 only | Haiku | ~$2.50 | 75-125 (15-25%) | $0.02-0.03 |
| Tier 1 only | Sonnet | ~$8.50 | 75-125 (15-25%) | $0.07-0.11 |
| Tier 1 + Tier 2 fallback | Haiku + Sonnet | ~$15 | 95-155 | $0.10-0.16 |
| Tier 1 + Tier 2 + Opus escalation | Mixed | ~$40 | 100-170 | $0.24-0.40 |

### Comparison: compilation cost

Each suggested edit requires one compile + objdiff run:
- Compile time: ~1.0s (with PCH + fs cache)
- Objdiff: ~0.5s
- Per edit: ~1.5s wall clock
- 5 suggestions per function, 500 functions: ~2,500 builds = ~62 minutes

Compilation cost is negligible (electricity, disk I/O). The real cost is API calls.

### Comparison: manual effort

A human working on decomp functions in conversation typically:
- Spends 5-30 minutes per function
- Uses 1-5 Claude API calls (via Claude Code) per function at ~$0.10-0.50 each
- Gets ~70-80% success rate on attempted functions

The batch advisor spends ~$0.005-0.03 per function with 15-25% hit rate. Even accounting for the lower hit rate, it's 10-100x more cost-efficient per improvement because it operates on hundreds of functions without human supervision.

### Break-even analysis

The advisor pays for itself if it saves even a few hours of manual decomp work:

- 1 hour of manual work ≈ fixes 3-5 functions
- $2.50 of Haiku API calls ≈ fixes 75-125 functions (at 15-25% hit rate on 500 attempted)
- Break-even: immediate

## Model Selection Decision Tree

```
Is the pattern library sufficient to diagnose this mismatch?
├── Yes → Tier 1 (Haiku: $0.005/call)
│         Does Haiku produce correct edits?
│         ├── Yes → Use Haiku
│         └── No → Upgrade to Sonnet ($0.017/call)
│
└── No → Tier 2 (Sonnet: $0.033/call)
          Does Sonnet diagnose the issue?
          ├── Yes → Use Sonnet
          └── No → Consider Opus ($0.165/call) or flag for manual review
```

The decision can be automated based on Tier 1 results: if Tier 1 returns `skip_reason: "no_known_pattern"` or all suggestions fail to improve, escalate to Tier 2.

## Cost Controls

- **Budget cap**: Set a per-run budget (e.g., $5.00). Stop after budget is exhausted.
- **Skip on no-op**: If diagnosis shows only noise (offset/symbol relocations), skip the API call entirely.
- **Cache**: Don't re-advise functions that haven't changed since last advisory. Key on (source_hash, diagnosis_hash).
- **Batch API**: Use Claude batch API for non-interactive runs (50% cost reduction, 24hr turnaround). Perfect for overnight sweeps.

## Batch API Consideration

Anthropic's Batch API offers 50% cost reduction for non-interactive workloads with 24-hour turnaround. This is ideal for the batch sweep use case:

- Submit 500 function contexts as a batch
- Get results within 24 hours
- Process results, apply improvements, commit

This halves all cost estimates above. A full AT_LIMIT sweep with Haiku via batch API: ~$1.25.
