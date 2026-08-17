# Codex Coordination Workflow

## Overview

This document describes how to coordinate with GPT-5.3-Codex (via OpenRouter) for decompilation analysis assistance.

**Model**: `openai/gpt-5-codex` (GPT-5.3-Codex)
**Reasoning Effort**: `high` (configured in `~/.codex/config.toml`)
**Context Window**: 400K tokens
**Purpose**: Provide deep technical analysis for PowerPC decompilation matching

## Architecture

```
┌─────────────────┐
│   Coordinator   │  (This agent - team-lead-2)
│  (Claude Code)  │
└────────┬────────┘
         │
         ├─── Manages team communication
         ├─── Routes analysis requests to Codex
         ├─── Synthesizes findings
         └─── Coordinates with @editor for edits
              │
         ┌────▼────────────────┐
         │  Codex via OpenRouter│
         │  (gpt-5.3-codex)     │
         └─────────────────────┘
              │
              └─── Deep reasoning on:
                   • PowerPC instruction sequences
                   • Register allocation patterns
                   • Compiler optimization strategies
                   • Control flow matching
```

## Tools Available

### 1. Python Script: `scripts/codex_helper.py`

Direct API interaction with OpenRouter.

**Usage:**
```bash
# Simple query
python scripts/codex_helper.py "Analyze this instruction sequence..."

# With system prompt for context
python scripts/codex_helper.py --system "You are a PowerPC decompilation expert" "What causes register r10<->r11 swaps?"

# From file
python scripts/codex_helper.py --file /tmp/analysis_request.txt

# JSON output
python scripts/codex_helper.py --json "..." > result.json

# Custom reasoning effort
python scripts/codex_helper.py --reasoning medium "..."
```

**Note:** Requires `dangerouslyDisableSandbox: true` due to network restrictions.

### 2. API Direct (Python)

```python
from scripts.codex_helper import call_codex

result = call_codex(
    prompt="Analyze this diff...",
    system_prompt="Context about DC3 decomp...",
    reasoning_effort="high"
)

print(result['content'])
print(f"Model used: {result['model']}")
print(f"Tokens: {result['usage']}")
```

## Coordination Protocol

### When to Use Codex

Use Codex for:
1. **Deep instruction sequence analysis** - When objdiff shows complex register swap or control flow patterns
2. **Compiler optimization reasoning** - Understanding why specific code patterns generate different assembly
3. **Multi-variable refactoring suggestions** - When structural changes need careful coordination
4. **Alternative approach generation** - Exploring different code structures that might match better
5. **Unfixable pattern validation** - Second opinion on whether issues are truly compiler-determined

**DO NOT use Codex for:**
- Simple string/constant lookups (use grep/m2c directly)
- Quick verification of single instructions
- Tasks better suited for specialized team agents (m2c-expert, ppc-expert, ghidra-expert)

### Request Format

When sending analysis requests to Codex, include:

```
## Context
- Function: BustAMovePanel::OnBeat
- Current match: X%
- Base size: Y bytes, Target: Z bytes
- Specific issue: [description]

## Objective
[What you need analyzed]

## Data
[Relevant objdiff output, code snippets, m2c decompilation]

## Constraints
- MWCPPC compiler for Xbox 360 PowerPC
- No LTCG (debug build)
- C++ with STL (list, vector)

## Question
[Specific question for Codex]
```

### Response Handling

1. **Capture full output** to `/tmp/codex-analysis-[topic].md`
2. **Extract actionable items** and communicate to team-lead
3. **Validate recommendations** against m2c/objdiff before suggesting edits
4. **Document findings** in project notes for future reference

## Current Focus: BustAMovePanel::OnBeat

### Status Snapshot (as of last session)
- Match: **52.0%**
- Base: **11976 bytes**, Target: **12056 bytes** (80-byte gap)
- Key issues:
  - 9 condition inversions (beq vs bne, bgt vs ble)
  - 168 register swaps (r10↔r11 dominant)
  - 8 offset swaps
  - 1 comparison_style diff
  - Large insert/delete clusters at regions 1621-1632 and 1766-1778

### Resolved Issues (do not re-analyze)
- ✅ Static variable name mangling (8 variables renamed)
- ✅ Deallocate count (3v3 match via declaration order)
- ✅ Case 7 mode switch structure

### High-Priority Analysis Areas

1. **80-byte gap investigation**
   - Region 1621-1632: 11 inserts (extra code we have)
   - Region 1766-1778: 12 deletes (missing code)
   - Hypothesis: beat 4 VO handling or case 7/8 initialization

2. **Condition inversions**
   - idx 375: beq vs bne in beat 4 VO
   - idx 386: bgt vs ble in beat 4 VO
   - Root cause: comparison operators or if/else order

3. **Register allocation patterns**
   - 29 r10↔r11 swaps suggest local variable ordering issues
   - Likely fixable via careful declaration order

## Example Queries

### 1. Gap Analysis
```bash
cat > /tmp/codex_query.txt <<'EOF'
## Context
Function: BustAMovePanel::OnBeat (PowerPC decompilation, MWCPPC)
Current: 52.0% match, 11976 base vs 12056 target (80-byte gap)

## Issue
objdiff shows two major clusters:
- Region 1621-1632: 11 inserts (we have extra code)
- Region 1766-1778: 12 deletes (target has code we're missing)

These are in beat 4 VO handling (switch case on unk44 value) and case 7/8 boundary.

## Data
[paste relevant objdiff output, m2c snippets]

## Question
What code patterns could account for 20 missing instructions (80 bytes)?
Consider: static local initialization, inline function decisions, STL container operations.
EOF

python scripts/codex_helper.py --file /tmp/codex_query.txt --system "You are a PowerPC decompilation expert specializing in MWCPPC compiler behavior." > /tmp/codex-gap-analysis.md
```

### 2. Condition Inversion Analysis
```bash
python scripts/codex_helper.py --system "PowerPC MWCPPC expert" "
In C++, what comparison patterns cause MWCPPC to emit 'bgt' vs 'ble' for the same semantic check?

Context: Switch case on integer, comparing against constant 2.
Current code: if (unk44 == 2) { ... }
Generates: ble (should be bgt)

What alternative forms might fix this?
" > /tmp/codex-condition-inversions.md
```

## API Reference

**OpenRouter API:**
- Base URL: `https://openrouter.ai/api/v1`
- Endpoint: `/chat/completions`
- Auth: `Authorization: Bearer $OPENROUTER_API_KEY`
- Model: `openai/gpt-5-codex`

**Pricing:**
- Input: $1.25/M tokens
- Output: $10/M tokens

**Sources:**
- [OpenRouter API Authentication](https://openrouter.ai/docs/api/reference/authentication)
- [GPT-5 Codex API Quickstart](https://openrouter.ai/openai/gpt-5-codex/api)
- [OpenAI GPT-5.3-Codex Announcement](https://openai.com/index/introducing-gpt-5-3-codex/)

## Notes

- Codex responses include reasoning chains visible in output
- High reasoning effort increases latency but improves analysis quality
- Always validate Codex suggestions against actual objdiff/build results
- Document all queries and responses for team continuity
