# Actionable Function Targets

Prioritized list of remaining decomp work and patterns. Updated after regression
fixes and DataArray non-const overload cleanup.

**Last Updated:** 2026-02-27

---

## Current State

- **31,385 COMPLETE** (97.1%), **961 AT_LIMIT** (3.0%), **2 remaining workable**
- **25,546 functions at 100% match** out of 47,463 total
- **45.44% fuzzy match** (normalized), 368 / 2,224 units fully linked
- Only **2 workable functions** left in the DB — effectively all triaged
- Struct/header layouts verified correct across all investigated classes

### Match Distribution (1,899 non-zero non-100% functions)

| Range     | Count | % of total | Notes |
|-----------|------:|------------|-------|
| 99-99.9%  |   383 |    20.2%   | 222 KB — 1-2 instruction diffs, mostly unfixable |
| 98-99%    |   138 |     7.3%   | Regswap + symbol noise dominated |
| 97-98%    |   112 |     5.9%   | Mixed |
| 95-97%    |   230 |    12.1%   | Mix of regswap and minor code issues |
| 90-95%    |   284 |    15.0%   | Some structural issues fixable |
| 80-90%    |   388 |    20.4%   | More code logic differences |
| <80%      |   364 |    19.2%   | Significant structural differences |

---

## Completed Since Last Update

### DataArray Non-const Overloads — APPLIED + CLEANED

Non-const overloads added for `Int`, `Float`, `Str`, `Array`, `Command`, `Var`,
`GetObj`, `Type`, `Evaluate`, etc. (+1,458 functions to COMPLETE).

**Regression cleanup**: Non-const overloads for `Obj<T>`, `Sym`, `ForceSym`, and
`LiteralSym` were **removed** — they caused const template instantiations to vanish
(inlined away) in 42 compilation units. The remaining non-const overloads (Int, Str,
Float, etc.) are safe because they're not templates and the compiler doesn't eliminate
the const out-of-line body.

### FlowPtr Copy Semantics — ALL FIXED (7/7 → 100%)

All `Flow*::Copy` methods now match at 100%:

| Function | Before | After |
|----------|--------|-------|
| `FlowCommand::Copy` | 53.3% | **100%** |
| `FlowDistance::Copy` | 58.7% | **100%** |
| `FlowRun::Copy` | 71.4% | **100%** |
| `FlowSetProperty::Copy` | 75.4% | **100%** |
| `FlowAnimate::Copy` | 77.3% | **100%** |
| `FlowTrigger::Copy` | 80.6% | **100%** |
| `FlowSound::Copy` | 87.2% | **100%** |

### HamDirector::ClosestMove — 69.3% → 89.0%

Loop body rewritten with proper best-match tracking, `do-while` structure, and
manual strlen via empty `do {} while` loops.

---

## Tier 0: Largest Byte-Gap Opportunities

Functions with the most recoverable bytes (biggest absolute gap from 100%). These
represent the highest-leverage individual function fixes.

| Function | Match | Size | Gap | Unit |
|----------|------:|-----:|----:|------|
| `MetagameRank::UpdateScore` | 49.9% | 8,596B | 4,307B | lazer/meta_ham/MetagameRank |
| `CSHA1::Transform` | 55.0% | 5,856B | 2,637B | system/math/SHA1 |
| `GameEndedDataPointJob` ctor | 35.3% | 3,700B | 2,394B | lazer/net_ham/DataMinerJobs |
| `NgEnviron::Select` | 11.2% | 1,956B | 1,736B | system/rndobj/Env_NG |
| `ShellInput::Poll` | 35.7% | 2,256B | 1,451B | lazer/meta_ham/ShellInput |
| `UIListDir::BuildDrawState` | 7.6% | 1,476B | 1,364B | system/ui/UIListDir |
| `UIManager::Handle` | 59.5% | 3,300B | 1,338B | system/ui/UI |
| `FlowManager::Poll` | 27.7% | 1,820B | 1,316B | system/flow/FlowManager |
| `SaveLoadManager::SetState` | 74.8% | 5,152B | 1,298B | lazer/meta_ham/SaveLoadManager |
| `MemAlloc` | 1.4% | 1,172B | 1,155B | system/utl/MemMgr |

### Top Units by Total Recoverable Bytes

| Unit | Gap | Funcs | Notes |
|------|----:|------:|-------|
| lazer/meta_ham/MetagameRank | 4,506B | 7 | Large `UpdateScore` dominates |
| system/meta/StorePanel | 3,049B | 20 | Many small diffs |
| system/math/SHA1 | 2,690B | 3 | `Transform` is crypto inner loop |
| xdk/nuispeech/mmio | 2,488B | 9 | XDK code, likely unfixable |
| lazer/net_ham/DataMinerJobs | 2,394B | 1 | **Workable** — constructor |
| system/rndobj/AmbientOcclusion | 2,334B | 17 | Tree-building code |
| system/utl/MemMgr | 1,939B | 19 | Low-level allocator |
| system/ui/UI | 1,842B | 13 | UIManager::Handle dominates |
| system/rndobj/Env_NG | 1,736B | 2 | Platform-specific renderer |
| system/flow/FlowSetProperty | 1,592B | 7 | Execute + operator<< |

---

## Tier 1: Known Code Bugs (fixable with source changes)

### HamDirector (still 4 fixable functions)

All `unicorn_equivalent_high` — behaviorally correct but assembly differs.

| Function | Current | Fix |
|----------|---------|-----|
| `ReactToCollision` | 86.0% | `ceil(beatSum / 4.0f)` → `ceil(beatSum * 0.25f) * 4.0f` |
| `ClosestMove` | 89.0% | Improved from 69.3% — remaining diff is regswap/structural |
| `UnloadMergers` | 83.7% | Loop structure around TheHamWardrobe null checks |
| `FindNextDircut` | 93.1% | Branch polarity inversion at shot-forced check |
| `OnLoadSong` | 97.4% | Regswap-dominated remaining diff |

### FlowSetProperty (structural issues)

| Function | Current | Fix |
|----------|---------|-----|
| `FlowSetProperty::Execute` | 55.3% | Missing FLOW_LOG/debug TextStream calls |
| `FlowSwitchCase::Execute` | 96.5% | `has_fixable_structural` |
| `FlowMultiSetProperty::Activate` | 90.1% | `has_fixable_mixed` |

---

## Tier 2: AT_LIMIT Functions Worth Revisiting

These are AT_LIMIT but have `has_fixable_*` verdicts at high match%. Many were
triaged before the DataArray non-const overload change and may now be closer to
100% than recorded. Consider batch-checking these units.

### 95%+ with fixable verdicts

| Function | Match | Verdict | Unit |
|----------|------:|---------|------|
| `SkeletonChooser::IsSinglePlayerMode` | 99.0% | has_fixable_mixed | lazer/meta_ham/SkeletonChooser |
| `FlowSwitchCase::Load` | 98.8% | has_fixable_regswap_plus | system/flow/FlowSwitchCase |
| `PhysicsManager::HarvestCollidables` | 98.7% | has_fixable_mixed | system/world/PhysicsManager |
| `RhythmBattlePlayer::Load` | 98.3% | has_fixable_regswap_plus | system/hamobj/RhythmBattlePlayer |
| `FireFlowLabel` | 98.1% | has_fixable_regswap_plus | system/rndobj/Anim |
| `RndShockwave::Load` | 97.7% | has_fixable_regswap_plus | system/rndobj/Shockwave |
| `DxMovie::SetFile` | 97.4% | has_fixable_mixed | system/rnddx9/Movie |
| `DateTime::Format` | 96.9% | has_fixable_structural | system/os/DateTime |
| `RndPostProc::Load` | 96.6% | has_fixable_mixed | system/rndobj/PostProc |
| `UISlider::Copy` | 96.6% | has_fixable_mixed | system/ui/UISlider |
| `HamCamShot::IterateNextShot` | 95.4% | has_fixable_mixed | system/hamobj/HamCamShot |
| `StreamRenderer::Load` | 95.2% | has_fixable_structural | system/gesture/StreamRenderer |
| `UIFontImporter::Save` | 94.8% | has_fixable_structural | system/ui/UIFontImporter |

### Structural fixes at 80-95%

| Function | Match | Verdict | Unit |
|----------|------:|---------|------|
| `CharEyes::SetFocusInterest` | 87.9% | has_fixable_mixed | system/char/CharEyes |
| `Splash::Splash` ctor | 87.8% | has_fixable_structural | system/movie/Splash |
| `RndPartLauncher` ctor | 87.5% | has_fixable_structural | system/rndobj/PartLauncher |
| `MidiParser::FixGap` | 86.1% | has_fixable_structural | system/midi/MidiParser |
| `CampaignPerformer::GetLastEra` | 88.1% | has_fixable_structural | lazer/meta_ham/CampaignPerformer |
| `ProfileMgr::GetActiveProfile` | 84.8% | has_fixable_structural | lazer/meta_ham/ProfileMgr |
| `RndShader::Init` | 64.0% | has_fixable_structural | system/rndobj/Shader |
| `WorldCrowd::SetMatAndCameraLod` | 61.3% | has_fixable_structural | system/world/Crowd |
| `Watcher::Update` | 59.4% | has_fixable_structural | system/rndobj/Watcher |
| `RndBitmap::PixelOffset` | 57.8% | has_fixable_structural | system/rndobj/Bitmap |
| `RndText::FontMap::UpdateScrolling` | 57.1% | has_fixable_structural | system/rndobj/Text |
| `DataProvider::Text` | 55.9% | has_fixable_structural | system/ui/UIListProvider |

---

## Tier 3: Unfixable Patterns (don't investigate)

### Linker-merged / ICF (`unreachable_linker_merged`)

ByteGrinder ops (op2, op15-20, op30-31), EventTrigger::Anim::operator=,
FileTerminate, SetBSPParams, DataReplaceTags, CheckContextNumPlayers, etc.
These call merged symbols that our linker resolves to different addresses.

### Boolean mask (`bool_mask`)

ByteGrinder op21-23/25-26, curl_global_init. The compiler generates different
boolean narrowing sequences that we can't reproduce.

### Build environment artifacts

- **Symbol relocation noise**: Different symbol addresses produce different `lis`/`addi`
  pairs — purely cosmetic, no behavioral impact
- **`__FILE__` stack frame sizing**: MakeString template instantiation N depends on
  filename length — systemic, unfixable
- **Anonymous namespace hash**: `?A0x<hash>@@` differs between builds — mitigated by
  post-build patcher but not perfect for header-sourced symbols

---

## Root Cause Distribution (AT_LIMIT functions)

| Root Cause | Fixable? | Notes |
|------------|----------|-------|
| **Register swaps** (GPR/FPR) | Rarely | Declaration reordering, 3-4 attempts max |
| **Symbol relocation noise** | No | Build artifact, different symbol addresses |
| **Linker-merged symbols** | No | ICF, different merged addresses |
| **Control flow differences** | Sometimes | Branch polarity, if/else structure |
| **Code logic bugs** | **Yes** | Wrong revisions, missing code, incomplete loops |
| **`__FILE__` stack frame** | No | Systemic, MakeString template |
| **Boolean mask patterns** | No | Compiler boolean narrowing |

---

## Verified Non-Issues (Don't Investigate Further)

| Class | Suspected Issue | Actual Finding |
|-------|----------------|----------------|
| **FlowNode** | Layout causing 20+ function mismatches | Layout correct. Issues are code logic in subclasses. |
| **CharClip** | -8 offset delta = struct field error | Stack frame size difference from `__FILE__` string length. |
| **CharBonesSamples** | Internal layout wrong | RB2 DWARF confirms 0x6c total, every field matches. |
| **HamDirector** | -4 offset delta = missing field | Stack frame offsets, not `this`-relative. Code bugs. |
| **FontMap::Page** | mVertStart/mSyncFlags swapped | Compiler instruction scheduling. |
| **VorbisReader** | mReadBuffer/mHdrBuf should be inline arrays | Both correctly declared as pointers. |

---

## Recommended Execution Order

1. **Batch-check stale AT_LIMIT units** — many were triaged before DataArray overload
   change and may now be at higher match%. Run `batch_check` on units with many
   `has_fixable_*` functions to find newly-100% matches.
2. **Large byte-gap functions** — MetagameRank::UpdateScore, CSHA1::Transform,
   SaveLoadManager::SetState offer the most matched-bytes per fix.
3. **Structural fixes** — AT_LIMIT functions with `has_fixable_structural` at 80%+
   are most likely to yield real improvements.
4. **Regswap attempts** — functions with `has_fixable_regswap_plus` at 95%+, try
   3-4 declaration reorderings max before moving on.
5. **Report remaining** — bulk-report truly unfixable functions as AT_LIMIT.

---

## See Also

- [TECHNICAL_NOTES.md](TECHNICAL_NOTES.md) - Compiler patterns and matching techniques
- [RB3_REFERENCE.md](RB3_REFERENCE.md) - Rock Band 3 decomp reference (shared engine)
- [SUBAGENT_STRATEGY.md](SUBAGENT_STRATEGY.md) - Parallel agent strategy for batch decomp work
- [../plans/NEXT_STEPS.md](../plans/NEXT_STEPS.md) - Full project roadmap
