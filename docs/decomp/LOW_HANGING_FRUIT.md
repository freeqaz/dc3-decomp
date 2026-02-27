# Actionable Function Targets

Prioritized list of functions with known fixes. Updated after DataArray non-const
overload full rebuild.

**Last Updated:** 2026-02-27

---

## Current State

- **31,387 COMPLETE** (97.1%), **834 AT_LIMIT** (2.6%), **107 remaining** (0.3%)
- DataArray non-const overload change pushed **+1,458 functions to COMPLETE**
- Only **107 workable functions** remain across the entire project
- Struct/header layouts verified correct across all investigated classes

---

## Tier 0: Highest-Leverage Patterns

### DataArray Accessor Non-const Overloads (APPLIED — hundreds of functions, +1-17 mismatches each)

**Status: APPLIED + REBUILT. Full project-wide impact measured.**

DataArray accessor methods (`Int`, `Sym`, `Float`, `Str`, `Array`, `Command`, `Var`,
`GetObj`, `Type`, `Evaluate`, etc.) were all declared `const` only, dispatching to
`Node(int) const` (mangled QBA). The original binary calls non-const `Node(int)` (QAA)
when accessed through non-const `DataArray*` pointers (~98% of ~3,166 call sites).

**Fix applied**: Added non-const overloads of all 20 accessor methods in `Data.h`.
Also changed `BEGIN_HANDLERS` and `BEGIN_CUSTOM_HANDLERS` macros in `Object.h` to use
`CONST_ARRAY(_msg)->Sym(1)` for the initial dispatch (matching the original binary's
const path for the first Sym lookup).

**Verified results** (A/B tested against clean baseline):

| Function | Before | After | Mismatches Fixed |
|----------|--------|-------|-----------------|
| RndLine::Handle | 98.1% | 98.3% | -17 |
| Sound::Handle | 95.9% | 96.0% | -3 |
| FlowCommand::Activate | 97.7% | 97.7% | -1 |
| Flow::Save | 98.3% | 98.3% | -2 |
| ByteGrinder::op4 | 99.7% | **100.0%** | -2 (perfect match) |

Zero regressions on all tested functions. The `CONST_ARRAY`/`UNCONST_ARRAY` macros
(already at Data.h line 567-568) exist precisely for this purpose — the original codebase
had both overloads and used explicit casts to select the desired path.

**Full rebuild impact**: +1,458 functions to COMPLETE (29,929 → 31,387), 812 AT_LIMIT
functions promoted to COMPLETE (single-instruction `bl` target fixes), remaining workable
functions reduced from 753 → 107. Fuzzy match: 45.28% → 45.40%.

### FlowPtr Copy Semantics (5-7 functions, +20-40pp each)

All `Flow*::Copy` methods use `FlowPtr::operator=` which generates a compound
assignment. The target binary does field-by-field copy: save mObjName and mState
to registers, call SetObjConcrete with the object pointer, then store them back.

**Fix**: Replace `mObject = c->mObject;` with explicit field-by-field operations.

| Function | Current | Unit |
|----------|---------|------|
| `FlowCommand::Copy` | 53.3% | system/flow/FlowCommand |
| `FlowDistance::Copy` | 58.7% | system/flow/FlowDistance |
| `FlowRun::Copy` | 71.4% | system/flow/FlowRun |
| `FlowSetProperty::Copy` | 75.4% | system/flow/FlowSetProperty |
| `FlowAnimate::Copy` | 77.3% | system/flow/FlowAnimate |
| `FlowTrigger::Copy` | 80.6% | system/flow/FlowTrigger |
| `FlowSound::Copy` | 87.2% | system/flow/FlowSound |

### FlowSetProperty Rewrite (3 functions, large pp gain)

| Function | Current | Fix |
|----------|---------|-----|
| `FlowSetProperty::Load` | 55.7% | `INIT_REVS(4,0)` → `INIT_REVS(3,0)`, rewrite version branching with real `ReadEndian`/`DataNode::Load` calls |
| `FlowSetProperty::Execute` | 55.3% | Missing ~10 FLOW_LOG/debug TextStream calls, `unk_0xE8` should reference `mEventsRegistered` in some places |
| `PropertyTask::PropertyTask` | 73.3% | Constructor logic differences |

---

## Tier 1: Known Code Bugs

### HamDirector (4 fixable functions)

Class layout verified correct. Issues are source code bugs.

| Function | Current | Fix |
|----------|---------|-----|
| `ReactToCollision` | 87.6% | `ceil(beatSum / 4.0f)` → `ceil(beatSum * 0.25f) * 4.0f` (round to measure), fix const/non-const `Node()` |
| `ClosestMove` | 69.3% | Incomplete loop body — best-match tracking (numlower/i17) never updates `out` |
| `FindNextDircut` | 93.6% | Branch polarity inversion (bne vs beq) at shot-forced check |
| `UnloadMergers` | 84.2% | Loop structure around TheHamWardrobe null checks |

### FlowCommand::Load (94.5%)

Add null checks on `GetOwnerFlow()` return before calling `->Dir()`. Target has
`cmplwi cr6, r11, 0x0; beq` guards that our code lacks.

---

## Tier 2: High-Match Functions (90%+)

Functions in the 90-98% range across the Flow system. Most have register swap and
symbol relocation noise as secondary issues but may be improvable with targeted fixes.

| Function | Current | Unit | Primary Issue |
|----------|---------|------|---------------|
| `FlowCommand::Activate` | 98.3% | flow/FlowCommand | 1 insert/delete cluster |
| `Flow::Save` | 98.2% | flow/Flow | Symbol noise |
| `FlowPtrBase::LoadObject` | 95.8% | flow/FlowPtr | Mixed |
| `FlowSound::RequestStop` | 95.3% | flow/FlowSound | Symbol noise only |
| `FlowWhile::ChildFinished` | 94.6% | flow/FlowWhile | Symbol noise only |
| `FlowSound::Load` | 94.6% | flow/FlowSound | Mixed |
| `FlowAnimate::RequestStop` | 94.4% | flow/FlowAnimate | Symbol noise + control flow |
| `FlowSwitchCase::Execute` | 93.2% | flow/FlowSwitchCase | Code logic |
| `FlowIf::Load` | 93.0% | flow/FlowIf | Code logic |
| `Flow::SyncObjects` | 92.4% | flow/Flow | Structural |
| `FlowSlider::Load` | 91.6% | flow/FlowSlider | Mixed |
| `FlowMultiSetProperty::Activate` | 90.7% | flow/FlowMultiSetProperty | Mixed |

---

## Tier 3: Remaining 107 Workable Functions

After the DataArray overload rebuild, only 107 functions remain workable. These are
scattered across many units (no single unit has more than 3). Top remaining units:

| Unit | Remaining | Range | Notes |
|------|-----------|-------|-------|
| lazer/meta_ham/HamSongMgr | 3 | 97-97% | Regswap-dominated |
| system/utl/NetCacheMgr | 2 | 94-95% | Regswap |
| system/rndobj/Shockwave | 2 | 96-97% | Mixed |
| system/rndobj/LitAnim | 2 | 94% | Regswap |
| system/os/ContentMgr_Xbox | 2 | 75-79% | Platform-specific |
| system/moviebink/BinkMovieSys | 2 | 83-90% | Structural |
| system/moviebink/BinkMovieImpl | 2 | 59-87% | Mixed |
| system/hamobj/HamCamShot | 2 | 94-95% | Mixed |
| system/gesture/StubCameraInput | 2 | 74-88% | Structural |
| lazer/meta_ham/ProfileMgr | 2 | 85-89% | Regswap/structural |
| lazer/meta_ham/OptionsPanel | 2 | 76-86% | Mixed |
| lazer/meta_ham/MultiUserGesturePanel | 2 | 79-92% | Structural |

Most remaining functions are dominated by register allocation differences (regswaps)
and structural issues. The median match is ~87%, with a long tail down to ~52%.

---

## Verified Non-Issues (Don't Investigate Further)

These were suspected struct/header problems but confirmed correct by deep analysis:

| Class | Suspected Issue | Actual Finding |
|-------|----------------|----------------|
| **FlowNode** | Layout causing 20+ function mismatches | Layout correct. Issues are code logic in subclasses. |
| **CharClip** | -8 offset delta = struct field error | Stack frame size difference from `__FILE__` string length. All fields match. |
| **CharBonesSamples** | Internal layout wrong | RB2 DWARF confirms 0x6c total, every field matches. |
| **HamDirector** | -4 offset delta = missing field | Stack frame offsets, not `this`-relative. Code bugs instead. |
| **FontMap::Page** | mVertStart/mSyncFlags swapped | Compiler instruction scheduling. CleanupSyncMeshes at 99.8% confirms layout. |
| **VorbisReader** | mReadBuffer/mHdrBuf should be inline arrays | Both correctly declared as pointers (accessed via `lwz` in target). Inline arrays (mNonce, mKeyMask) already correct. |

---

## Root Cause Distribution (107 remaining functions)

From a 25-function stratified sample:

| Root Cause | Prevalence | Fixable? |
|------------|-----------|----------|
| **Register swaps** (GPR/FPR) | ~80% | Rarely (declaration reordering, 3-4 attempts max) |
| **Symbol relocation noise** | ~60% | No (build artifact, different symbol addresses) |
| **Control flow differences** | ~50% | Sometimes (branch polarity, if/else structure) |
| **Code logic bugs** | ~25% | **Yes** (wrong revisions, missing code, incomplete loops) |
| **`__FILE__` stack frame sizing** | ~25% | No (systemic, MakeString template) |
| **Anon namespace hash** | ~8% | No (build environment, mitigated by patcher) |

**Key insight**: The LINKER_MERGED estimate from the saturation doc ("~80% blocked") was
incorrect. 0/25 sampled workable functions had LINKER_MERGED patterns. Those functions
were already moved to AT_LIMIT status.

---

## Recommended Execution Order

1. **Individual function fixes** — 107 remaining functions, work through by match% descending
2. **Regswap attempts** — functions tagged `has_fixable_regswap_plus`, try 3-4 declaration reorderings
3. **Structural fixes** — functions tagged `has_fixable_structural`, investigate control flow
4. **Bulk AT_LIMIT triage** — report remaining ~834 effectively-unfixable functions

---

## See Also

- [TECHNICAL_NOTES.md](TECHNICAL_NOTES.md) - Compiler patterns and matching techniques
- [RB3_REFERENCE.md](RB3_REFERENCE.md) - Using RB3 as reference
- [SUBAGENT_STRATEGY.md](SUBAGENT_STRATEGY.md) - Parallel agent workflow
- [../plans/NEXT_STEPS.md](../plans/NEXT_STEPS.md) - Full project roadmap
