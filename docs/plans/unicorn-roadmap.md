# Unicorn Runner: Strategic Assessment & Improvement Roadmap

## Context

After building and demonstrating the unicorn runner, we've learned where it works and where it doesn't. This document is an honest assessment based on real data from the value demonstration session.

---

## Honest Assessment: Where We Are

### What actually works (proven)

**False positive filtering is the killer feature.** Real numbers:
- 93.1% equivalence rate across 25,682 functions in 949 units (stress test Feb 13, 2026)
- DirLoader: objdiff flagged 42 as needing work → 37 are actually equivalent
- CharPollGroup: 25 flagged → all 25 equivalent
- ~61% of objdiff-flagged functions are false positives (time saved by unicorn)

**Divergence classification** (Phase 0, Feb 13, 2026) now auto-tags unfixable FIX results:
- `FIX(build_env)` — `__FILE__` path differences, merged symbols (ICF) — don't waste time
- `FIX(regalloc)` — register allocation artifacts — usually unfixable
- `FIX` — real logic differences — these are the ones worth investigating

**Multi-input probing** (Phase 1, Feb 13, 2026) runs N times with varied fill patterns for higher confidence than dual-fixture (2 runs).

### What doesn't work (proven)

**Bug localization is mostly a miss.** The plan hypothesized unicorn would say "call #3 diverges, r4 differs" and you'd fix the bug. In practice:
- **Call count mismatches** dominate (looping over zeroed linked lists) — not actionable
- **Arg mismatches** are usually register allocation artifacts — now auto-classified as `FIX(regalloc)`
- We found zero functions where unicorn output led directly to a code fix

**DIVERGENT ≠ "broken code."** Most DIVERGENT functions diverge because:
1. Zeroed memory causes different loop counts (~60% of divergences)
2. Symbol name differences cause different pointer values (~25%) — now auto-classified as `FIX(build_env)`
3. Structural code differences (decomp isn't done yet) (~10%)
4. Actual semantic bugs (~5%)

---

## The Core Question: Niche Tool or Significant Value?

**Current state: Core workflow tool.** At 93% equivalence with classification, it's no longer a niche screening tool — it's the primary triage mechanism for decomp work prioritization.

Here's the current tier list:

### Tier 1: Done and working
- Batch triage before agent assignment (93% equiv rate)
- "Unit done" confirmation (all functions EQUIVALENT)
- AT_LIMIT confidence boost
- Intra-TU call co-loading (done)
- Dual-fixture and multi-input probing (done)
- Divergence classification — auto-filters build_env and regalloc noise (done)

### Tier 2: Next improvements (moderate effort, clear payoff)
- Struct field access probing (Phase 2 of structural probing plan)
- Permuter guard rail
- bctr switch table improvements

### Tier 3: Hard problems (high effort, uncertain payoff)
- Intelligent fixtures from DWARF struct info
- Cross-TU call resolution
- Mock return variation (Phase 3 of structural probing plan)

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
| ~~Today~~ | ~~70%~~ | ~~21,804~~ | ~~Niche screening tool~~ |
| **DONE: + Intra-TU co-loading** | ~80-85% | 21,804 | Reliable triage tool |
| **DONE: + Smart fixtures (3a, 3c)** | ~85-90% | 21,804 | Strong confidence signal |
| **DONE: + Divergence classification** | ~93% | 25,682 | **Noise-free triage** — build_env/regalloc auto-filtered |
| **DONE: + Multi-input probing** | ~93% | 25,682 | Higher confidence via N-run probing |
| + bctr handling | ~93% | 25,864 (+182) | Broader coverage |
| + Permuter guard rail | ~93% | 25,864 | Regression prevention |
| + Struct field probing | ~93% | 25,864 | Decomp reconnaissance |
| **Theoretical ceiling** | ~95% | ~25,000 | Limited by zeroed inputs + cross-TU calls |

**Current state** (Feb 13, 2026): 93.1% equivalence rate (23,899/25,682 functions), 0 crashes, 0 hangs. Divergence classification cuts remaining FIX items by identifying unfixable build-env and regalloc artifacts. Multi-input probing with 4-8 runs provides higher confidence than dual-fixture.

The tool will never reach 100% equivalence for all functions — that would require real runtime data from Xenia. But at 93% with classification, it's already a **core workflow tool** where EQUIVALENT genuinely means "this function is done" and `FIX(build_env)` means "don't waste time on this."

---

## Bottom Line

**The tool is a core workflow tool.** With intra-TU co-loading, dual-fixture, divergence classification, and multi-input probing all implemented, it's at 93% equivalence — exceeding the original 90% target.

Current capabilities:
- For any unit, run batch triage in ~2 seconds with `diagnose --batch`
- 93% of functions get a confident SKIP/DONE verdict
- FIX results auto-classified: `FIX(build_env)` and `FIX(regalloc)` are auto-filtered as unfixable
- Multi-input probing via `probe --batch --runs 8` for higher confidence
- No agent time wasted on false positives

Next frontier: structural probing (Phase 2-3) turns the tool from a validator into a **decomp reconnaissance tool** — discovering struct field access patterns and call dependencies before you start writing code.
