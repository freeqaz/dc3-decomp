# Regression Analysis: HEAD~15 (2026-03-07)

42 regressions detected across 15 commits. Classified into actionable batches.

## Batch 1: Quick Reverts (single agent, ~15 min)

Small source-level changes that regressed previously-matching functions. Revert or adjust each.

| Function | Unit | Drop | Diagnosis |
|----------|------|------|-----------|
| `HDCache::Init` | system/os/HDCache | 96.7→94.5% | `auto _tmp0 = memcmp(...)` temp variable changed codegen. Revert to direct `memcmp()` in conditional. |
| `ObjectDir::RemoveSubDir` | system/obj/Dir | 100→98.1% | Cast changed from `*(u32*)` pointer compare to `(ObjectDir*)*it == (ObjectDir*)dPtr`. Revert to raw compare. |
| `HamDirector::Poll` | system/hamobj/HamDirector | 92.8→91.6% | `(bool)` cast added to float comparison (`(bool)(forceBlend < blend)`). Triggers branchless materialization. Remove the cast. |
| `Locale::Init` | system/utl/Locale | 87.2→86.4% | `mSize = 0` moved before `MILO_ASSERT`. Revert ordering back to assert-first. |
| `VirtualKeyboard::Terminate` | system/os/Memcard_Xbox | 100→40% | Empty `{}` stub added in Memcard_Xbox.cpp but only matches at 40% — likely wrong TU. Check if it belongs in a VirtualKeyboard-specific file or if the stub was already in link_glue. |

## Batch 2: Header Cascades — Mtx.h Inline (single agent, ~20 min)

`Multiply(Transform, Matrix3, Transform)` was changed from extern declaration to inline definition in Mtx.h. This cascaded into multiple TUs. Investigate whether the inline is correct (does the target inline it?) or if it should be reverted.

| Function | Unit | Drop | Notes |
|----------|------|------|-------|
| `Skeleton::operator=` | system/gesture/GestureMgr | 100→97.8% | GestureMgr.h includes Mtx.h. Transform ops now inlined. |
| `DepthBuffer3D::UpdateAttachment` | system/gesture/DepthBuffer3D | 88.5→86.1% | New includes + Mtx.h inline change. |
| `HDCache::Poll` | system/os/HDCache | 100→97.1% | Not directly modified; Mtx.h or Memory.h cascade. |
| `LocalePanel::LocalePanel` | system/ui/UI | 100→97.9% | Not directly modified; header cascade. |
| `BinkMovieImpl::BinkMovieImpl` | system/moviebink/BinkMovieImpl | 59.3→57% | Not directly modified; Timer.h or Mtx.h. |

**Action**: Run objdiff on `Skeleton::operator=` before/after reverting Mtx.h inline. If it restores 100%, revert the inline and fix all 5 at once.

## Batch 3: Header Cascades — Voice.h / BINK struct (single agent, ~20 min)

Two distinct header changes caused cascading regressions in their TUs.

### Voice.h: PoolVoice struct + deque globals

| Function | Unit | Drop | Notes |
|----------|------|------|-------|
| `_Deque_base<PoolVoice>` | system/synth_xbox/Voice | 100→75.6% | New PoolVoice struct changed template instantiation. |
| `Voice::~Voice` | system/synth_xbox/Voice | 91.3→80.4% | Deque globals + dispose() stub changed TU layout. |
| `PoolVoice* StlNodeAlloc` | system/synth_xbox/Voice | 100→97.9% | Template codegen affected. |
| `StlNodeAlloc deallocate` | system/synth_xbox/Voice | 100→98.1% | Same cause. |

**Action**: These are likely correct additions for the Voice pool system. Run objdiff on all 4 and try to match the new codegen rather than reverting.

### BinkMovieImpl: BINK struct forward-declaration

| Function | Unit | Drop | Notes |
|----------|------|------|-------|
| `MovieInternalBuffers::~MovieInternalBuffers` | system/moviebink/BinkMovieImpl | 91.4→39.3% | `struct BINK { virtual ~BINK(); };` changed to `struct BINK;`. Removes virtual dtor dispatch from `delete mBinks[N]`. |

**Action**: The virtual dtor was likely correct (the target uses virtual dispatch delete). Revert to `struct BINK { virtual ~BINK(); };`.

## Batch 4: Header Cascades — Text.h / Timer.h / Includes (single agent, ~20 min)

Various header and include changes that cascaded into nearby functions.

### Text.h struct layout changes

| Function | Unit | Drop | Notes |
|----------|------|------|-------|
| `RndText::RndText` | system/rndobj/Text | 98.5→84.4% | Char struct `bool brk` added + int→float member changes. |
| `RndText::OnComputeCharWidths` | system/rndobj/Text | 64.1→60.2% | Char struct layout shift. |
| `RndText::ReFitTextScroll` | system/rndobj/Text | 90.4→78.1% | int→float scroll field type changes. |

**Action**: Verify Text.h struct changes against DWARF/Ghidra. If brk field and float types are correct, these need re-matching. If wrong, revert.

### Other TU include cascades

| Function | Unit | Drop | Notes |
|----------|------|------|-------|
| `Achievements::Init` | system/meta/Achievements | 100→96.6% | Timer.h layout changes cascaded. |
| `Spotlight::~Spotlight` | system/world/Spotlight | 100→95.3% | New includes (Timer.h, Cam.h, Rnd.h) in Spotlight.cpp. |
| `EventTrigger::CloneFilteredAnim` | system/rndobj/EventTrigger | 100→98.1% | New includes (AnimFilter.h, Dir.h, Task.h). |
| `FitnessGoalMgr::Handle` | lazer/meta_ham/FitnessGoalMgr | 100→98.7% | New stub added nearby changed TU layout. |
| `RandomIntervalGroupSeqInst::Stop` | system/synth/Sequence | 100→99.4% | Likely header cascade. |

**Action**: For each, run objdiff to confirm the new includes are needed. If an include was added for a new stub, it's expected collateral — try to match the new codegen.

## Batch 5: Real Regressions — Code Rewrites (single agent, ~30 min)

Functions where the logic was intentionally rewritten but the new code doesn't match well.

| Function | Unit | Drop | Diagnosis |
|----------|------|------|-----------|
| `MergeObjectsRecurse` | system/obj/Utl | 100→70% | Rewritten from simple `if(sd)` to `switch(filt.FilterSubdir(...))` with kMergeKeep/kMergeReplace. New real logic needs matching. |
| `FlowPtr<Hmx::Object>::operator=` | system/flow/FlowNode | 100→68% | FlowNode MoveIntoDir/DuplicateChild rewrite changed inline template context. |
| `SongSortByLocation::NavListShortcutNode` | lazer/meta_ham/SongSortByLocation | 76→60.8% | New LocationCmp ctor/dtor + NewHeaderNode overload changed codegen context. |
| `NetworkSocket::ResolveIP` | system/os/NetworkSocket_Win | 100→94% | ResolveHostName moved between TUs with rewrite. ResolveIP affected by extern decl removal. |

**Action**: These are intentional rewrites. Use objdiff + permuter on each to improve match%.

## Batch 6: Stub-to-Real — High Priority (close to matching, single agent each)

New implementations that are close to 100% and worth finishing.

### Sub-batch 6a: 95%+ (quick wins)

| Function | Unit | Current | Size |
|----------|------|---------|------|
| `Flow::Enter` | system/flow/Flow | 94.6% | 156B |
| `Flow::Exit` | system/flow/Flow | 99.1% | 168B |
| `BinkMovieImpl::Poll` | system/moviebink/BinkMovieImpl | 95.6% | 908B |
| `Spotlight::DrawShowing` | system/world/Spotlight | 96.1% | 1104B |
| `InlineHelp::DrawShowing` | system/ui/InlineHelp | 96.2% | 588B |
| `MoveDir::DrawShowing` | system/hamobj/MoveDir | 97.2% | 1164B |
| `Game::Poll` | lazer/game/Game | 98.0% | 992B |
| `OptionsPanel::Poll` | lazer/meta_ham/OptionsPanel | 98.9% | 364B |
| `HamCharacter::Poll` | system/hamobj/HamCharacter | 94.4% | 1016B |

### Sub-batch 6b: 80-95% (more work needed)

| Function | Unit | Current | Size |
|----------|------|---------|------|
| `RndTexBlender::DrawShowing` | system/rndobj/TexBlender | 88.6% | 2636B |
| `HamRibbon::UpdateMesh` | system/hamobj/HamRibbon | 80.9% | 404B |
| `JointToVertexData` | system/gesture/DepthBuffer3D | 80.5% | 184B |
| `RndText::FitTextJust` | system/rndobj/Text | 80.1% | 556B |

### Sub-batch 6c: Large / complex (dedicated agent)

| Function | Unit | Current | Size |
|----------|------|---------|------|
| `SaveLoadManager::Poll` | lazer/meta_ham/SaveLoadManager | 59.4% | 3108B |
| `PostPurchaseEnumJob::OnComplete` | system/os/PlatformMgr_Xbox | 37.5% | 276B |
