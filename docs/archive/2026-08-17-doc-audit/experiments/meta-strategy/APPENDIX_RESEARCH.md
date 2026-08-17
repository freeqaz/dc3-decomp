# Appendix: Research Findings from Other Decomp Projects

Insights gathered from other decompilation projects to inform DC3 strategy.

---

## Key Finding: Prioritization Is Informal

Most decomp projects **do not use algorithmic prioritization**. They rely on:
- Contributor interest
- Discord coordination
- Manual assessment of difficulty

This suggests our scoring model adds value that other projects lack.

---

## How Major Projects Prioritize

### SM64 Decomp
- No formal prioritization strategy
- Contributors work on functions based on interest
- Uses `NON_MATCHING` flag for functional-but-not-identical code
- Progress driven by community coordination

### Zelda: Ocarina of Time (ZeldaRET)
- Discord-based coordination
- Contributors pick functions they understand
- No algorithmic scoring
- Focus on "what can I comprehend" not "what's easiest"

### Super Smash Bros. Melee
- ~50% completion as of analysis
- Recommends searching repo before starting
- Uses decomp.me to avoid conflicts
- Suggests starting with "functions that aren't that long"

### Breath of the Wild Decomp
- Uses **Trello board with labels**:
  - "Easy" tasks for newcomers
  - "Blocked" for dependency issues
  - "Requires library integration" for external deps
- Most structured of the community projects

---

## Universal Advice

From multiple projects:

> "Try to understand what the function does using Hex-Rays or Ghidra. Understanding the function is very important."
> — BotW Decompilation Guide

> "Register allocation is an NP-hard problem which means there are all types of heuristics you can use to select registers, some of which can be confused by things as silly as variable names."
> — Melee Decomp Guide

**Key insight**: Pick functions you can *understand*, not just "easy by metrics."

---

## The Real Bottleneck

Research shows **matching is the bottleneck, not finding targets**.

From [Chris Lewis's blog on AI-assisted decompilation](https://blog.chrislewis.au/the-unexpected-effectiveness-of-one-shot-decompilation-with-claude/):

> "The limiting factors shifted from human availability to computational resources and frontier model access."

This means:
- Finding targets is easy (SQL queries work fine)
- Writing byte-identical C code is hard
- Good prioritization helps spend expensive model time wisely

---

## How Projects Handle Stuck Functions

### Stubbing
Declare functions without implementing them to unblock dependents.

### NON_MATCHING Macros
Allow functionally-equivalent but non-matching code to proceed.

### Permutation Tools
[decomp-permuter](https://github.com/simonlindholm/decomp-permuter) automatically tries:
- Temporary variables
- Type changes
- PERM macros for user-specified variations

Best for "towards the end, when mostly regalloc changes remain."

### Attempt Thresholds
From AI-assisted decompilation work:
> "Claude was configured with a ten-attempt threshold - if no progress occurred after ten tries, the function was logged as too difficult."

### Blocking Labels
BotW uses "Blocked" labels for tasks depending on other work.

---

## Call Graph Usage

### Academic Recognition
Papers recognize call graph value for:
- Context awareness
- Function name recovery
- Binary diffing

### Community Practice
**Rarely used in practice.** Most projects don't document call-graph-based prioritization.

### DC3 Recommendation
Validate before investing:
1. Extract sample call graph
2. Check if >= 10 functions have 20+ callers
3. If yes, build infrastructure
4. If no, focus on pattern-based scoring

---

## AI-Assisted Decompilation

Recent work shows LLMs can automate significant portions:

> "62.7% of calls flowed from higher to lower complexity functions."
> — One-shot decompilation analysis

This suggests:
- Simpler functions (that complex ones call) should be done first
- Call graph might help identify these
- AI shifts bottleneck from human time to compute cost

### Model Escalation
From empirical work:
- Start cheap (Haiku equivalent)
- Escalate to smarter models for stuck functions
- Give up after threshold (10 attempts)

---

## Tools Used Across Projects

| Tool | Purpose | DC3 Equivalent |
|------|---------|----------------|
| decomp.me | Collaborative scratch workspace | N/A (could add) |
| decomp-permuter | Automatic variation testing | N/A (could add) |
| m2c | MIPS/PPC decompiler | `tools/decompile.sh` |
| objdiff | Local assembly diffing | MCP orchestrator tools (`run_objdiff`, `run_diff_inspect`) |
| decomp-toolkit | GameCube/Wii automation | `scripts/decomp_orchestrate.py` |

---

## What DC3 Does Better

Based on research, DC3 tooling is more advanced than typical community projects:

| Capability | Other Projects | DC3 |
|------------|---------------|-----|
| Batch orchestration | Manual | `decomp_orchestrate.py` |
| Pattern detection | Rare | MCP orchestrator `run_analyze_function` |
| Verdict classification | BotW Trello labels | Automated in CLI |
| Scoring model | None documented | This strategy |
| Ghidra integration | Varies | MCP server with caching |
| Progress tracking | decomp.dev | Database + reports |

---

## Recommendations from Research

1. **Keep informal element** - Let humans pick what they understand
2. **Add scoring as supplement** - Help find targets, don't mandate
3. **Validate call graph value** - Don't assume it helps; test first
4. **Support stubbing** - Let blocked functions proceed
5. **Track attempt counts** - Give up on persistently stuck functions
6. **Use pattern detection** - Our key differentiator

---

## Sources

- [SM64 Decomp](https://github.com/n64decomp/sm64)
- [Zelda OoT Decomp](https://github.com/zeldaret/oot)
- [Melee Decomp Guide](https://doldecomp.github.io/melee/getting_started.html)
- [BotW Decompilation Guide](https://botw.link/contribute/how-to-decompile)
- [decomp.me FAQ](https://www.decomp.me/faq)
- [decomp-permuter](https://github.com/simonlindholm/decomp-permuter)
- [decomp-toolkit](https://github.com/encounter/decomp-toolkit)
- [One-Shot Decompilation with Claude](https://blog.chrislewis.au/the-unexpected-effectiveness-of-one-shot-decompilation-with-claude/)
- [decomp.dev Progress Tracker](https://decomp.dev/projects)
