# Actionable Function Targets

Prioritized list of functions with known fixes. Updated after struct/header investigation.

**Last Updated:** 2026-02-27

---

## Current State

- **29,927 COMPLETE** (92.6%), **1,674 AT_LIMIT** (5.2%), **727 remaining** (2.2%)
- Struct/header layouts verified correct across all investigated classes
- Remaining work is primarily **code logic bugs**, register allocation, and build artifacts
- ~180 functions have fixable code logic bugs; ~400 are effectively at-limit

---

## Tier 0: Highest-Leverage Patterns

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

## Tier 3: Units with Remaining Work

These units have the most remaining workable functions. See `docs/plans/NEXT_STEPS.md`
Phase 3 for detailed analysis of each.

| Unit | Remaining | Range | Notes |
|------|-----------|-------|-------|
| system/meta/StorePanel | 9 | untriaged | Needs investigation |
| system/rndobj/HiResScreen | 8 | untriaged | Needs investigation |
| system/rnddx9/Rnd_Xbox | 7 | untriaged | Likely platform-specific |
| system/char/CharClip | 7 | 64-97% | `__FILE__` stack sizing + regswaps, struct verified correct |
| system/utl/MemTracker | 6 | varies | Memory subsystem |
| system/utl/Cheats | 6 | varies | Needs investigation |
| system/ui/UILabel | 6 | 67-94% | Register allocation, code structure |
| system/ui/UIFontImporter | 6 | 56-95% | Code logic |
| system/ui/UI | 6 | varies | Needs investigation |
| system/rndobj/Text | 6 | 57-94% | Code logic (FontMap::Page struct verified correct) |
| system/hamobj/HamDirector | 6 | 69-97% | Code bugs (see Tier 1), struct verified correct |
| system/hamobj/HamCamShot | 6 | varies | Needs investigation |
| system/char/CharLipSync | 6 | 87-97% | Inner class logic (Generator, PlayBack) |
| lazer/meta_ham/MetaPerformer | 6 | varies | Needs investigation |

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

## Root Cause Distribution (727 remaining functions)

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

1. **FlowPtr copy pattern** — one fix pattern, 5-7 functions, highest leverage
2. **FlowSetProperty rewrite** — 3 functions with specific known fixes
3. **HamDirector code bugs** — 4 functions with identified fixes
4. **FlowCommand::Load null guards** — small targeted fix
5. **Unit-by-unit sweeps** — push remaining units toward completion
6. **Bulk AT_LIMIT triage** — report ~400 effectively-unfixable functions

---

## See Also

- [TECHNICAL_NOTES.md](TECHNICAL_NOTES.md) - Compiler patterns and matching techniques
- [RB3_REFERENCE.md](RB3_REFERENCE.md) - Using RB3 as reference
- [SUBAGENT_STRATEGY.md](SUBAGENT_STRATEGY.md) - Parallel agent workflow
- [../plans/NEXT_STEPS.md](../plans/NEXT_STEPS.md) - Full project roadmap
