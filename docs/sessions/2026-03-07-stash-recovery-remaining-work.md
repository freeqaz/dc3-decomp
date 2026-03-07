# Stash Recovery: Remaining Work

**Date**: 2026-03-07
**Context**: An agent ran `git stash` in the main repo (against project rules), then a subsequent stash restore mixed valid new decomp work with regression-causing refactoring. This session triaged 104 regressions down to 29 remaining.

## Completed

### Category 1: Full reverts to HEAD (done)
Restored original matching code for 70+ regressions caused by header changes and source rewrites. Key files: ObjPtr_p.h, Object.h, Dir.h, Text.h, Geo.h, ShaderMgr.h, ByteGrinder.cpp, CharNeckTwist.cpp, CharCollide.h, FlowNode.cpp, HamStoreProvider, Locale, StandardStream, Mic, SynthSample, and more.

### Category 3: HX_NATIVE guards (done)
Re-added `#ifdef HX_NATIVE` guards to ObjPtr_p.h, Object.h, and Dir.h so decomp matches Xbox original while native port retains safety guards for use-after-free during MergeObjectsRecurse.

### Category 4: Investigation items (done)
- Mtx.h: Restored inline `Multiply(Transform, Matrix3, Transform)` (non-inline had no .cpp implementation = linker error)
- Sequence_p.h: Kept `unsigned int idx` (matches implementation), restored access modifiers
- MoggClip.h: Already reverted in prior session

### Category A: Reverted matching code from stash (done)
- HamStoreOffer.cpp: Restored full Cmp() with AlphaKeyStrCmp/DateTimeCmp (88.5% → 39.2% fixed)
- OptionsPanel.cpp: Restored Poll() with correct nesting/method calls (98.9% → 65.2% fixed)
- HamRibbon.cpp: Restored 171-line UpdateChase() (64.5% → 20.6% fixed)
- Rnd.cpp: Surgically restored DrawTimers with externs lbl_830A4100/lbl_830A4104 (57.4% → 0.1% fixed)
- Timer.h: Restored `AutoTimer::Timers()` accessor and `friend class Rnd` (required by DrawTimers)

## Remaining: 29 Regressions

### Category B: New decomp work (KEEP - not regressions)
These were stubs/empty functions that now have real implementations. The "regression" is from a trivially-matching stub being replaced with real code that doesn't match perfectly yet.

| Function | Unit | Before → After | Notes |
|---|---|---|---|
| RndParticleSys::Load | system/rndobj/Part | 100% → 78.2% | 300-line deserialization replacing empty stub |
| TexProc::DrawToTexture | system/rndobj/TexProc | 100% → 92.3% | New implementation (was stub) |
| MeterDisplay::DrawShowing | system/hamobj/MeterDisplay | 100% → 73.5% | New implementation added |
| MeterDisplay::Update | system/hamobj/MeterDisplay | 100% → 89.9% | New implementation added |

### Category C: Refactoring regressions (REVERT)
These are previously-matching or closer-to-matching functions that were made worse by refactoring during the stash restore. Each file needs examination — some have mixed valid+regression changes requiring surgical edits rather than full `git checkout HEAD~1`.

#### High priority (large drops, previously 100% or near-100%)

| Function | Unit | Before → After | Change description |
|---|---|---|---|
| RandomIntervalGroupSeqInst::Poll | system/synth/Sequence | 99.4% → 70.9% | `int`→`unsigned int`, `RandomVal`→`RandomFloat`, `push_back`→`resize` |
| InlineHelp::DrawShowing | system/ui/InlineHelp | 96.2% → 81.6% | Implementation removed |
| MoveDir::DrawShowing | system/hamobj/MoveDir | 97.2% → 85.1% | Implementation removed and replaced with different algorithm |
| UIScreen::ReloadStrings | system/ui/UIScreen | 100% → 88.5% | `DataDir()`→`LoadedDir()`, type change, added null check |
| UIFontImporter::GetGenericFont | system/ui/UIFontImporter | 100% → 88.9% | Early return replacing assignment pattern |
| UIFontImporter::FindTextForFont | system/ui/UIFontImporter | 100% → 94.4% | Removed font comparison check (logic break) |
| UIListDir::DrawShowing | system/ui/UIListDir | 96.1% → 86.7% | Variable reordering, temp variable extractions |
| Flow::Enter | system/flow/Flow | 94.6% → 86.1% | Removed ProxyFile() check, added PollEnabled() guard |
| FlowPtrBase::LoadObj | system/flow/FlowPtr | 100% → 95.8% | Goto removal → nested ifs |
| UIScreen::Draw | system/ui/UIScreen | 96.6% → 91.6% | if-else → ternary |
| UIManager::FakeKeyboardAction | system/ui/UI | 100% → 97.4% | Constructor arg reordering |
| FlowTimer::Deactivate | system/flow/FlowTimer | 100% → 99.1% | Removed `(Task*)` cast before delete |

#### Medium priority (smaller drops)

| Function | Unit | Before → After | Change description |
|---|---|---|---|
| UIList::DrawShowing | system/ui/UIList | 82.5% → 77.9% | Header virtual method changes |
| UIList::CalcBoundingBox | system/ui/UIList | 66.6% → 62.2% | UIListWidget.h/UIListSlot.h virtual method signature moves |
| UIListState::SetMaxDisplay | system/ui/UIListState | 97.7% → 95.4% | Logic error — merged conditions changed semantics |
| StartLog | system/utl/MemTrack | 89.4% → 85.8% | Variable declaration reordering |
| DanceRemixer::PostMoveFinish | system/hamobj/DanceRemixer | 98% → 94.7% | Expression inlining |
| HamUI::DisplayNextCameraOut | lazer/meta_ham/HamUI | 84.6% → 81.4% | Replaced enums with magic numbers |
| SuperEasyRemixer::LoadAllVariations | system/hamobj/SuperEasyRemixer | 96.6% → 94.5% | Removed casts, introduced local var |
| UITrigger::Trigger | system/ui/UITrigger | 96.7% → 95.4% | `fabsf`→`std::fabs`, possible stream var typo |
| FlowTrigger::Load | system/flow/FlowTrigger | 98.1% → 97.5% | Control flow simplification, operator>> removal |
| MetagameRank::UpdateScore | lazer/meta_ham/MetagameRank | 89% → 88.5% | Variable elimination, decl reordering |
| ReadSingleXinputJoypad | system/os/Joypad_Xinput | 80.6% → 78.8% | Variable declaration moved |
| FlowAnimate::Replace | system/flow/FlowAnimate | 43.4% → 40.7% | Unknown change |
| FlowAnimate::RequestStop | system/flow/FlowAnimate | 96.4% → 95.8% | Unknown change |

## Approach for Category C

For each file:
1. Run `git diff HEAD~1 -- <file>` to see all changes
2. Determine if the file has ONLY regression changes → `git checkout HEAD~1 -- <file>`
3. If mixed valid + regression changes → surgical edit to revert only the regression parts
4. Rebuild and verify with `scripts/measure_progress.sh`

### Files likely safe for full checkout (only regression changes)
- FlowPtr.cpp, FlowTimer.cpp, UIListState.cpp, MemTrack.cpp, Joypad_Xinput.cpp

### Files needing surgical edits (mixed valid + regression)
- Sequence.cpp (Poll rewrite is regression, but ComputeNextTime `unsigned int` may be correct)
- Flow.cpp (Enter rewrite is regression, but SyncObjects bug fix may be valid)
- MeterDisplay.cpp (DrawShowing is new work to keep, but other changes may be regressions)
- UIScreen.cpp (ReloadStrings/Draw are regressions, but other changes may be valid)
- UIFontImporter.cpp (GetGenericFont/FindTextForFont are regressions)
- UIListWidget.h/UIListSlot.h (virtual method signature moves affect CalcBoundingBox)

## Also Modified (130 files total)

Beyond the 29 regressing functions, there are ~100 additional source files with diffs vs HEAD~1. Most of these appear to be formatting-only changes, variable renaming, or minor refactoring that didn't cause measurable regressions. These should be reviewed but are low priority — they may cause small codegen differences not captured by the function-level regression report.
