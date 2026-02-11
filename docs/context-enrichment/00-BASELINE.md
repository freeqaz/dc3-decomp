# Context Enrichment Baseline Metrics

**Generated**: 2026-01-28
**Database**: `decomp.db`
**Total attempts**: 11,016

This document establishes baseline metrics before implementing context enrichments. All experiments will be measured against these numbers.

---

## Summary

| Metric | Value |
|--------|-------|
| Total attempts | 11,016 |
| Attempts with token data | 0 (MCP-based orchestrator doesn't track) |
| Primary model | haiku (83% of attempts) |
| Overall improvement rate | 17.3% of attempts improve the match |

---

## Success Rate by Match Band

| Band | Attempts | Avg Gain | Success Rate |
|------|----------|----------|--------------|
| `<30%` | 9,247 | +62.79% | 15.0% |
| `30-80%` | 319 | +5.99% | 41.1% |
| `80-95%` | 573 | +1.20% | 33.7% |
| `95%+` | 877 | +0.11% | 26.5% |

### Interpretation

1. **`<30%` band dominates** - 84% of all attempts are on low-match functions
2. **When <30% improves, the gain is massive** - Average +62.79% gain reflects initial implementation success
3. **Diminishing returns at high %** - 95%+ band shows only 26.5% can improve, with tiny gains (+0.11%)
4. **Mid-range (30-80%) is highest success** - 41.1% success suggests this band benefits most from agent work

---

## AT_LIMIT Verdicts by Band

| Band | Total | AT_LIMIT | AT_LIMIT % |
|------|-------|----------|------------|
| `<30%` | 9,247 | 611 | 6.6% |
| `30-80%` | 319 | 214 | 67.1% |
| `80-95%` | 573 | 455 | 79.4% |
| `95%+` | 877 | 682 | 77.8% |

### Interpretation

1. **Low-match functions rarely hit AT_LIMIT** - Only 6.6% of `<30%` attempts conclude as unfixable
2. **High-match functions are frequently AT_LIMIT** - 77-80% of `80%+` attempts hit compiler/linker limits
3. **This is the key opportunity** - Reducing false AT_LIMIT verdicts in 80%+ band could save significant effort

---

## Model Effectiveness Comparison

| Model | Attempts | Avg Gain | Success Rate | Notes |
|-------|----------|----------|--------------|-------|
| deepseek-chat-v3-0324 | 4 | +52.48% | 100% | Very small sample |
| gpt-oss-120b | 27 | +51.70% | 7.4% | High gain but low reliability |
| unknown | 1,505 | +35.02% | 58.1% | Early attempts before model tracking |
| haiku | 9,135 | +32.42% | 10.1% | Primary model, very large sample |
| sonnet | 233 | +15.24% | 51.5% | Higher success rate than haiku |
| opus | 19 | +25.13% | 10.5% | Small sample |
| deepseek-v3.2 | 53 | +14.60% | 13.2% | Moderate performance |

### Key Insight: Haiku vs Sonnet

| Metric | Haiku | Sonnet |
|--------|-------|--------|
| Attempts | 9,135 | 233 |
| Avg gain when improved | +32.42% | +15.24% |
| Success rate | 10.1% | 51.5% |

**Sonnet has 5x higher success rate** but attempts fewer functions. This suggests:
- Haiku is better for initial exploration (cheaper, faster)
- Sonnet should be used for retry/escalation (more reliable)

---

## Haiku Performance by Band

| Band | Attempts | Avg Gain | Improved % | Reached 100% |
|------|----------|----------|------------|--------------|
| `<30%` | 8,294 | +63.25% | 8.0% | 4.1% |
| `30-80%` | 155 | +5.77% | 39.4% | 2.6% |
| `80-95%` | 283 | +1.26% | 34.3% | 7.4% |
| `95%+` | 403 | +0.14% | 25.3% | 7.9% |

### Interpretation

1. **Haiku succeeds more often in 30-95% bands** - 34-39% vs only 8% for `<30%`
2. **Reaching 100% is hardest in mid-range** - Only 2.6% of 30-80% reach completion
3. **95%+ band has highest 100% rate** - 7.9% reach full match (small final fixes)

---

## Token Efficiency

**Note**: Token tracking is not currently implemented in the MCP orchestrator. Token counts are NULL for all attempts.

### Recommendation
Add token tracking to enable cost-per-percentage-point analysis:
```python
# In mcp_server.py report_result handler
"input_tokens": request.usage.input_tokens,
"output_tokens": request.usage.output_tokens,
```

---

## Key Metrics for A/B Testing

Based on this baseline, the following metrics should be tracked for enrichment experiments:

### Primary Metrics
1. **Success rate** - % of attempts that improve match%
2. **False AT_LIMIT rate** - % of AT_LIMIT verdicts where function was later improved
3. **Average gain** - Mean improvement when successful

### Secondary Metrics
4. **Time to verdict** - How quickly agent reaches AT_LIMIT or completion
5. **Iteration count** - Tool calls before final verdict (not currently tracked)
6. **Cost** - Tokens used per percentage point gained (requires token tracking)

---

## Baseline Targets

For each enrichment experiment, aim to beat these numbers:

| Band | Current Success Rate | Target (10% improvement) |
|------|---------------------|--------------------------|
| `<30%` | 15.0% | 16.5% |
| `30-80%` | 41.1% | 45.2% |
| `80-95%` | 33.7% | 37.1% |
| `95%+` | 26.5% | 29.2% |

| Metric | Current | Target |
|--------|---------|--------|
| False AT_LIMIT (80%+) | ~78% | -30% → ~55% |
| Avg iterations to AT_LIMIT | N/A | Baseline after tracking |

---

## Next Steps

1. **Add enrichment_flags column** to track which enrichments were active
2. **Implement A/B assignment** for deterministic experiment groups
3. **Start Experiment 1** (Diff Pattern Classification) with low token cost and high expected impact
