# Pattern Investigation Session — 2026-03-05

Investigation of undetected mismatch patterns in the DC3 decomp. Seven parallel subagents explored each pattern with real objdiff data, source analysis, and online research.

## Context

The `sync_objdiff.py` script and objdiff fork were not auto-classifying 99%+ register-swap functions as AT_LIMIT. Fixes were applied to both the Rust verdict engine and Python sync script. During that work, the patterns wiki revealed several documented-but-undetected patterns. This session investigates each to confirm fixability.

## Changes Applied Before Investigation

### sync_objdiff.py — Python-side auto AT_LIMIT fallback
- Added `PRACTICALLY_UNFIXABLE` set extending `UNFIXABLE_PATTERNS` with `REGISTER_SWAP`, `PROLOGUE_MISMATCH`, `STATIC_GUARD_COUNTER`
- Functions at >=95% where ALL detected patterns are in this set now auto-promote to AT_LIMIT

### objdiff analysis.rs — Rust verdict engine
- Register swap verdict returns `AtLimit` (not `MaybeFixable`) when >=95% match and all patterns are practically unfixable
- `SCOPE_COUNTER_MISMATCH`: `Unfixable` -> `LikelyFixable` (fixable via brace changes)
- `STATIC_GUARD_COUNTER`: `LikelyFixable` -> `UsuallyUnfixable` (source reordering doesn't work; needs COFF patcher)

### Impact
- 104 functions now auto-classify as AT_LIMIT that previously required manual reporting

---

## Pattern Fixability Confirmation (from earlier subagents)

| Pattern | Verdict | In PRACTICALLY_UNFIXABLE? |
|---------|---------|---------------------------|
| REGISTER_SWAP | USUALLY_UNFIXABLE (~30% fixable via decl reorder) | Yes |
| PROLOGUE_MISMATCH | USUALLY_UNFIXABLE (~40-50% fixable subset) | Yes |
| SCOPE_COUNTER_MISMATCH | FIXABLE (brace add/remove, 100% success) | No (removed) |
| STATIC_GUARD_COUNTER | USUALLY_UNFIXABLE from source (needs COFF patcher) | Yes |

---

## Undetected Pattern Investigation Results

### 1. ASSERT_REVS Scheduling — NOT A DISTINCT PATTERN

The "ASSERT_REVS scheduling mismatch" documented in meta-strategy docs is a myth. Analysis of 15 Load functions found:

- **67% are standard register swaps** (r26<->r27 in ASSERT_REVS code) — already detected as REGISTER_SWAP
- **27% are gRev/gAltRev addressing mode** differences (`subi r29, 0x4` vs `mr r28, gRev`) — already detected as ADDRESS_RELOCATION_NOISE
- The `subi r11, r11, 0x4` vs `addi r11, r11, 0x4` signature from the docs was **never observed** in any of the 15 functions sampled
- ASSERT_REVS usage does NOT correlate with mismatches (85.1% of 100% Load functions use it too)
- Only 37 Load/PreLoad functions are at 99-99.9%, not ~1,000 as claimed in meta-strategy docs

**Verdict: No new detector needed.** Existing REGISTER_SWAP and ADDRESS_RELOCATION detectors already cover this. The -25 scoring penalty for `has_assert_revs` in SCORING_MODEL.md is misleading.

### 2. Store-then-Reload Scheduling — FIXABLE (source mismatch, not compiler bug)

Investigation of SpotlightDrawer::Init (94.1%) revealed:

- The "store-then-reload" is caused by **accessing through a local variable instead of the global directly**
- RB3 reference and NgSpotlightDrawer::Init (100%) both use `sDefault->member` instead of `ptr->member`
- When code accesses through a global (`sDefault->member`), the compiler MUST reload (can't prove aliasing safety). With a local (`ptr->member`), it keeps the value in register.
- Only **1-2 genuine instances** found out of ~22,000 functions
- The documented claim "source reordering doesn't fix this" tested the wrong approach (reordering while keeping the local variable, rather than eliminating it)

**Fix:** Replace local pointer pattern with direct global access:
```cpp
// Before (our code — local variable, no reload)
SpotlightDrawer* ptr = Hmx::Object::New<SpotlightDrawer>();
ptr->mParams.mLightingInfluence = 0.0f;
sDefault = ptr;
ptr->Select();

// After (matches target — global access, forces reload)
sDefault = Hmx::Object::New<SpotlightDrawer>();
sDefault->mParams.mLightingInfluence = 0.0f;
sDefault->Select();
```

**Verdict: Fixable. Not worth a detector** — too rare (1-2 instances).

### 3. Stack Spill Scheduling — CONFIRMED UNFIXABLE, ~17% prevalence

The most thoroughly investigated pattern. Confirmed across 7 functions with real objdiff data.

**What it is:** Target spills locals to stack that our compiler keeps in registers (or vice versa — goes both ways). Always 1-2 `stw rN, offset(r31)` instructions.

**Canonical example — PhysicsManager::HarvestCollidables (98.7%):**
```
77 equal    lwz r3, 0x148(r29)        // GetGeomOwner() return
78 equal    cmplw cr6, r29, r3        // mesh != owner
79 DELETE   stw r3, 0x54(r31)         // TARGET spills owner to stack
80 equal    beq cr6, 0x150
81 DELETE   stw r3, 0x54(r31)         // TARGET spills owner again
82 equal    bl HasKeepMeshData
```

**Confirmed instances:**

| Function | Match% | Direction |
|----------|--------|-----------|
| PhysicsManager::HarvestCollidables | 98.7% | Target spills, we don't |
| Game::LoadNewSong | 99.3% | Target spills, we don't |
| MoveDir::Enter | 98.9% | Target spills, we don't |
| FlowNode::Load | 98.8% | Target spills, we don't |
| HiResScreen::Finish | 99.4% | We spill, target doesn't |
| ChunkStream::~ChunkStream | 99.4% | We spill, target doesn't |
| MicXbox::~MicXbox | 97.7% | We spill, target doesn't |

**Detection signature:**
- Lone `stw` insert/delete (1 instruction, not part of larger structural mismatch)
- Base register is r1 (stack pointer) or r31 (frame pointer)
- Stored register contains a value already live (function return, callee-saved, freshly loaded)
- Surrounding instructions match perfectly

**How to distinguish from other stw differences:**
- Member store: `stw rN, offset(rOBJ)` where rOBJ is NOT r1/r31 — field write, not spill
- Argument setup: `stw rN, 0x8(r1)` with small offset before `bl` — stack arg passing
- Frame save: `stw rN, -offset(r1)` in prologue — callee-saved save

**Fixability:** Unfixable. Source restructuring, adding variables, declaration reordering — none work. The spill decisions are made by the register allocator's spill cost model.

**Prevalence:** ~17% of near-match AT_LIMIT functions. Accounts for 0.5-1.3% gap per function.

**Verdict: UNFIXABLE. Worth adding as a Rust detector** — clear signature, affects many functions.

### 4. fsel Register Pressure — CONFIRMED UNFIXABLE, but very rare

Only **3 confirmed cases** out of ~40 fsel-using files:

| Function | Match% | Extra FPRs |
|----------|--------|------------|
| DebugGraph::Draw | 70.2% | +2 (f28-f31 vs f30-f31) |
| CharLookAt::Poll | 94.0% | +2 (f21-f27 vs f23-f27) |
| CharEyes::Poll | 94.7% | +1 |

Pattern only triggers in large functions with many live floats. Smaller functions handle fsel fine (5+ tested at 100%). FPRs are NOT addressable by the BSF coloring mechanism (base=0 is GPR only), making this even less controllable than GPR register allocation.

**Verdict: UNFIXABLE but too rare for a dedicated detector.** Already subsumed by PROLOGUE_MISMATCH.

### 5. FMA Direction Mismatch — MOSTLY FIXABLE

Comprehensive scan of 999 functions found 51 affected:

| Category | Count | Fixability |
|----------|-------|------------|
| Pure NEED_OFF | 13 | HIGH — `#pragma fp_contract(off)` |
| Pure NEED_ON | 10 | MEDIUM — expression restructuring |
| Variant swaps (fnmsubs/fmsubs) | 3 | HIGH — expression order fix |
| Displaced (scheduling, not FMA) | 18 | N/A — false positives |
| Truly mixed direction | 7 | LOW — needs function splitting |

Only 3 functions have FMA as primary blocker:
- NgFur::Shell (96.8%) — pure NEED_OFF
- InterpTangent (98.1%) — variant swap
- CalcSpline (96.0%) — variant swap

`#pragma fp_contract` confirmed working (BustAMovePanel::PlayIntroVO at 100%).

**Verdict: MOSTLY FIXABLE. Low priority for auto-AT_LIMIT.** Existing FSEL_TERNARY + FLOAT_PRECISION partially cover it.

### 6. cmplwi vs cmpwi (unsigned vs signed compare) — PARTIALLY FIXABLE, low impact

Two distinct sub-patterns:
- **COMPARISON_STYLE** (different immediate + branch): `>= 5` vs `> 4` — fixable by adjusting operator
- **Opcode mismatch** (cmplwi vs cmpwi, same immediate 0x0): type-sensitivity on pointer null checks

**20 functions fixed to 100%** via casting strategies:
- `(int)ptr != 0` forces `cmpwi` (signed)
- `(unsigned long)ptr > 0` forces `cmplwi` (unsigned)
- Extract smart pointer to local `T* ptr = smartPtr; if(ptr)` changes register usage

**2 confirmed unfixable:** SfxInst::IsRunning, TexMovie::Enter — smart pointer implicit conversion (`operator T*()`) generates signed compare with deferred CR field that can't be controlled.

Only 34 functions flagged in DB, almost never the sole blocker (91% also have REGISTER_SWAP).

**Verdict: PARTIALLY FIXABLE. Not worth auto-AT_LIMIT** — too rare as sole blocker.

### 7. External Community Research — DC3 is alone

- **No other Xbox 360 matching decomp exists.** All Xbox 360 projects (XenonRecomp, rexdex/recompiler, Minecraft 360) are recompilations or functional decomps.
- RB3 decomp targets Wii (CodeWarrior, not MSVC) — useful for source reference only.
- DC3's c2.dll register allocator characterization (BSF coloring at RVA 0x026780) is **original research documented nowhere else**.
- GC/Wii decomps (Twilight Princess, etc.) share some general concepts (declaration order affects regalloc) but the compiler backends are completely unrelated.
- decomp.me does not support Xbox 360 / MSVC PPC.
- No new fixability techniques found externally that DC3 hasn't already tried.

---

## Recommended Next Steps

| Pattern | Action | Priority |
|---------|--------|----------|
| **Stack Spill Scheduling** | Add Rust detector (lone stw insert/delete to stack frame) | High — affects ~17% of AT_LIMIT |
| **Store-then-Reload** | Fix the 1-2 instances by using global directly | Low — manual fix |
| **FMA Direction** | Fix 3 primary-blocker functions (NgFur::Shell, InterpTangent, CalcSpline) | Medium — easy wins |
| **ASSERT_REVS** | Remove inflated claims from meta-strategy docs, remove -25 scoring penalty | Cleanup |
| **fsel FPR Pressure** | No action — too rare, already covered by PROLOGUE_MISMATCH | None |
| **cmplwi/cmpwi** | No action — casting fixes work case-by-case, not systemic | None |

The only pattern worth adding as a new Rust detector is **Stack Spill Scheduling**.

---

## Follow-up: Permuter Execution Results (2026-03-05)

Ran the permuter on all 6 recommended targets. Results:

| Function | Before | After | Fix | Notes |
|----------|--------|-------|-----|-------|
| **CalcSpline** | 96.0% | **100%** | `p3 - (p2*3 - p1x3m0)` → `p1x3m0 - p2*3 + p3` | Paren subtraction expansion |
| **InterpTangent** | 98.1% | **99.6%** | `1.0f - (f4 - fsq3)` → `fsq3 - f4 + 1.0f` | Same pattern; remaining 0.4% = volatile FPR regswaps |
| **NgFur::Shell** | 96.8% | 96.8% | None | `fp_contract(off)` had NO effect; compiler fuses at /O1 regardless. Also has struct offset mismatch (+28 bytes). **Investigation was wrong**: this is NOT a "pure NEED_OFF" case. |
| **SpotlightDrawer::Init** | 94.1% | 94.1% | None | Using global directly made it WORSE (91.4%) — compiler adds null-check (`clrrwi`). **Investigation was wrong**: store-then-reload fix doesn't apply here. |
| **Vector3Keys::SetFrame** | 98.4% | 98.4% | None | Volatile regswaps (r11↔r4, r1↔r30) |
| **SymbolKeys::SetFrame** | 96.0% | 96.0% | None | Callee-saved regswaps (r23↔r24, r27↔r28) |

### Corrections to Investigation Conclusions

1. **NgFur::Shell** was incorrectly classified as "pure NEED_OFF". The MSVC PPC compiler at `/O1` performs FMA contraction regardless of `#pragma fp_contract`. This function is **unfixable** — dominated by FMA contraction + struct offset mismatch.

2. **SpotlightDrawer::Init store-then-reload** — the recommended fix of using the global directly actually made match% worse because accessing through `sDefault->` triggers a null-check (`clrrwi`) that the original local-variable approach avoids. The remaining gap is instruction scheduling noise, not store-then-reload.

3. **CalcSpline and InterpTangent** were both fixed by the same algebraic transform: **parenthesized subtraction expansion** (`a - (b - c)` → `c - b + a`). This changes FMA selection from `fmsubs`/`fsubs` to `fnmsubs`/`fadds`.

### Permuter Improvement

The `fma_reorder` pattern was missing the parenthesized subtraction expansion transform. Added:
- `_find_paren_sub_candidates()` — finds `a - (b ± c)` patterns
- `_collect_terms()` — flattens nested +/- chains with sign tracking
- `_generate_paren_expansions()` — emits reversed and flat variants

The new pattern correctly generates the exact source changes that fixed both CalcSpline and InterpTangent. This will catch similar cases automatically in future permuter runs.

Also added **guard-to-conjunction** transform to `early_return_merge.py`:
- `if (!cond) return false; return expr;` → `return cond && expr;`
- Reverse direction: `return A && B;` → `if (!(A)) return false; return B;`
- Also handles `||` with `return true;` guards
- Inspired by MetaPanel::IsLoaded fix (beq↔bne inversions from control flow restructuring)
