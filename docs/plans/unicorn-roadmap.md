# Unicorn Runner: Strategic Assessment & Improvement Roadmap

## Context

After building and demonstrating the unicorn runner, we've learned where it works and where it doesn't. This document is an honest assessment based on real data from the value demonstration session.

---

## Honest Assessment: Where We Are

### What actually works (proven)

**False positive filtering is the killer feature.** Real numbers from the demo:
- UITransitionHandler: objdiff flagged 9 functions → all 9 were EQUIVALENT. Zero agent work needed.
- Profile: 7 flagged → 6 equivalent. Only 1 needs attention.
- ContentMgr: 16 flagged → 10 equivalent. Cut the work list by 63%.
- keygen_xbox: 16 flagged → 9 equivalent.

At ~70% equivalence rate across tested units, this saves real time. A batch triage of a unit takes ~2 seconds.

### What doesn't work (proven)

**Bug localization is mostly a miss.** The plan hypothesized unicorn would say "call #3 diverges, r4 differs" and you'd fix the bug. In practice:
- **Call count mismatches** dominate (looping over zeroed linked lists) — not actionable
- **Arg mismatches** are usually register allocation artifacts — not fixable code bugs
- We found zero functions where unicorn output led directly to a code fix

**DIVERGENT ≠ "broken code."** Most DIVERGENT functions diverge because:
1. Zeroed memory causes different loop counts (~60% of divergences)
2. Symbol name differences cause different pointer values (~25%)
3. Structural code differences (decomp isn't done yet) (~10%)
4. Actual semantic bugs (~5%)

---

## The Core Question: Niche Tool or Significant Value?

**Current state: Niche but useful.** Saves agent time on false positives. That's real value but it's a screening tool, not a primary decomp instrument.

**Can it become significantly more valuable?** Yes, but it requires solving the zeroed-memory problem. Everything else is incremental.

Here's the honest tier list:

### Tier 1: Already works, just needs workflow integration
- Batch triage before agent assignment
- "Unit done" confirmation (all functions EQUIVALENT)
- AT_LIMIT confidence boost

### Tier 2: Feasible improvements (moderate effort, clear payoff)
- Intra-TU call co-loading (biggest single improvement)
- Permuter guard rail
- bctr switch table support

### Tier 3: Hard problems (high effort, uncertain payoff)
- Intelligent fixtures from DWARF struct info
- Cross-TU call resolution
- Full symbolic execution

---

## Improvement Roadmap

### 1. Intra-TU Call Co-Loading (HIGH PRIORITY — biggest value unlock)

**The problem**: 50.4% of functions (12,937) call other functions in the same .obj file. Currently those calls hit trampolines and return 0. This is the #1 source of false DIVERGENT results.

**Example**: `Profile::Profile()` calls `FixedSizeSaveable::FixedSizeSaveable()` which is in the same .obj. Both sides call it, but since the trampoline returns 0 instead of executing the real constructor, any downstream state that depends on the constructor's side effects diverges.

**The fix**: When loading a function, also load its intra-TU callees into the code region. Patch caller's REL24 relocs to jump to the real callee instead of a trampoline.

**Complexity**: Moderate.
- Need recursive callee extraction with cycle detection (300 circular pairs exist)
- Code region is 64KB — need to track total loaded size
- Each callee needs its own relocation patching
- Debugging gets harder (which callee caused divergence?)

**Impact**: Would convert many false DIVERGENT → true EQUIVALENT. Estimated 15-25% of currently-divergent functions would flip to equivalent. This would push the overall equivalence rate from ~70% to ~80-85%.

### 2. Permuter Guard Rail (HIGH PRIORITY — prevents regressions)

**The problem**: The C++ permuter generates source variants to improve match %. Some variants subtly change semantics (e.g., reordering assignments that have side effects). There's no safety net to catch this.

**The fix**: After each permuter variant builds, run unicorn comparison. If a variant that was EQUIVALENT becomes DIVERGENT, reject it automatically.

**Impact**: Prevents semantic regressions in automated code generation. Small in scope but high in trust — lets you run the permuter more aggressively.

**Integration point**: `permuter/scorer.py`. The .obj path is already available after ninja build.

### 3. Smart Fixture Generation (MEDIUM PRIORITY — attacks root cause)

**The problem**: Zeroed memory is why ~25-30% of functions diverge falsely. If we could initialize memory with realistic-ish values, more functions would follow the same code paths on both sides.

**Approaches, ranked by feasibility**:

**3a. Pattern-byte initialization — IMPLEMENTED**
Fill object/globals/stack/vtable regions with `0xCD` (MSVC debug fill) instead of zeros. Tests equivalence under non-null inputs. CLI: `--fill-pattern 0xCD`. Also fills on-demand mapped pages.

**3b. DWARF-derived struct layouts (moderate)**
Parse DWARF debug info from the original .obj to understand struct layouts. Initialize `this` pointer's memory with valid-looking data: vtable pointer at offset 0, reasonable small integers for size fields, NULL for pointer fields (already zeroed), valid enum values. This would let constructors and simple methods follow realistic paths.

**3c. Dual-run differential — IMPLEMENTED**
Run each function twice (zeros + 0xCD fill), combine verdicts. If both agree → `confidence=high`. If they disagree → `confidence=fixture_sensitive`. CLI: `--dual-fixture`. Works in single, batch, and batch-all modes. JSON output includes `confidence` and `fixture_mode` fields.

**3d. Snapshot from Xenia (hard)**
Capture real memory state from a Xenia emulation run and replay it into unicorn. This gives realistic inputs but requires Xenia integration, function entry detection, and state serialization. The QEMU research doc confirms this path is blocked by missing Xenon CPU support.

**Recommendation**: ~~Do 3a immediately (free), then 3c (cheap confidence boost), then 3b if needed.~~ 3a and 3c done. 3b is next if needed.

### 4. bctr Switch Table Handling (MEDIUM PRIORITY — unblocks 182 functions)

**Already partially implemented** in the current tool (prepare_switch_tables in patcher.py, rdata_bytes in engine.py). The Phase 3 research says 182 functions are blocked.

**Remaining work**: Improve reliability of switch table detection, handle vtable tail calls (detect the `lwz→lwz→mtctr→bctr` pattern and treat as trampoline).

### 5. Batch-All CI Integration (LOW PRIORITY — enables automation)

**The idea**: Run `ninja test-unicorn` as part of CI. Any function that was EQUIVALENT becoming DIVERGENT after a code change = regression alert.

**Prerequisites**: Batch-all needs to complete in <5 minutes (currently ~30-60 min). Needs multiprocessing optimization and result caching.

**Impact**: Catches regressions automatically. But only valuable after the equivalence rate is high enough that regressions are rare (>80%).

### 6. Richer Comparison Dimensions (LOW PRIORITY — diminishing returns)

- Capture r7-r10 args in call log (only r3-r6 currently)
- Compare callee-saved registers (r14-r31) at function exit
- Compare condition register (CR) state
- Compare all FPRs (f0-f31), not just f1

Each of these catches edge cases but the current comparison already catches the major divergence modes. These are polish, not breakthroughs.

---

## What WON'T Help (Anti-Roadmap)

### Full bctrl virtual dispatch (deprioritize)

3,704 functions are blocked by virtual calls. The current vtable mock (256 slots, each returning 0) handles the dispatch mechanism, but the callee side effects are lost — same problem as intra-TU calls but worse (callees are in different .obj files). Solving this properly requires cross-TU call resolution, which is a much bigger problem.

**Better strategy**: Fix intra-TU co-loading first. Most bctrl functions also have intra-TU calls, so co-loading helps them too.

### Symbolic execution / formal verification

Way too complex for this codebase. The tool's value is in fast, pragmatic screening — not proofs.

### QEMU as replacement backend

The research doc confirms QEMU has no Xenon CPU model and no VMX128 support. Building one would be a multi-month effort for marginal benefit over Unicorn (which already handles PPC32 well enough).

---

## The Trajectory

| Milestone | Equivalence Rate | Functions Testable | Value Level |
|-----------|------------------|--------------------|-------------|
| **Today** | ~70% | 21,804 | Niche screening tool |
| **+ Intra-TU co-loading** | ~80-85% | 21,804 | Reliable triage tool |
| **+ Smart fixtures (3c)** | ~85-90% | 21,804 | Strong confidence signal |
| **+ bctr handling** | ~85-90% | 21,986 (+182) | Broader coverage |
| **+ Permuter guard rail** | ~85-90% | 21,986 | Regression prevention |
| **Theoretical ceiling** | ~92-95% | ~25,000 | Limited by zeroed inputs + cross-TU calls |

The tool will never reach 100% equivalence for all functions — that would require real runtime data from Xenia. But at 85-90% with intra-TU co-loading, it becomes a **reliable** screening tool where EQUIVALENT genuinely means "this function is done" and DIVERGENT is worth investigating.

---

## Bottom Line

**The tool is not destined to be niche.** The #1 improvement (intra-TU co-loading) would push it from "useful screening" to "reliable triage" by eliminating the biggest source of false divergences. The permuter guard rail adds a new use case entirely. Smart fixtures add confidence.

The realistic ceiling is ~90% equivalence across all eligible functions. That means:
- For any unit, run batch triage in 2 seconds
- 90% of functions get a confident SKIP/DONE verdict
- The remaining 10% are genuinely worth investigating
- No agent time wasted on false positives

That's not niche — that's a core workflow tool.
