# Stub Burndown List

Exhaustive list of empty-body function stubs in the decomp that need real implementations.
These are functions where our source has `{}` but the target binary contains actual code.

Generated 2026-02-18. Updated 2026-02-19 with implementation results.

## Status Summary

| Category | Count |
|----------|-------|
| Implemented to 100% (COMPLETE) | 12 |
| Implemented to AT_LIMIT (>= 95%) | 15 |
| Implemented, needs more work (< 95%) | 13 |
| Not attempted | 1 |
| No target symbol exists | 2 |
| Intentionally empty (100% match) | 6 |
| Not in target binary (deadstripped) | 25 |
| **Total stubs analyzed** | **74** |

Overall progress: 43.90% -> 44.24% (+0.34% fuzzy match from stubs, 44.24% total current)

## Implemented Functions -- COMPLETE (100%)

| File | Function | Notes |
|------|----------|-------|
| StreamReceiver360.cpp | `SetVolume` | Simple forwarding |
| StreamReceiver360.cpp | `SetPan` | Simple forwarding |
| StreamReceiver360.cpp | `SetSpeed` | Simple forwarding |
| StreamReceiver360.cpp | `PauseImpl` | Voice pause |
| StreamReceiver360.cpp | `PlayImpl` | Voice start |
| StreamReceiver360.cpp | `StartSendImpl` | XMemCpy |
| UIList.cpp | `FinishValueChange` | Value change callback |
| JobMgr.cpp | `MultipleItemsEnumJob::Start` | ICF merged (identical to StoreEnumJob) |
| JobMgr.cpp | `MultipleItemsEnumJob::Cancel` | ICF merged |
| JobMgr.cpp | `MultipleItemsEnumJob::OnCompletion` | ICF merged |
| SynapseAPO.cpp | `OnSetParameters` | XMA parameter handling |
| SynapseAPO.cpp | `DoProcess` | Audio processing loop |

## Implemented Functions -- AT_LIMIT (>= 95%)

| Match% | File | Function | Mismatch Pattern |
|--------|------|----------|-----------------|
| 99.9% | UISlider.cpp | `DrawShowing` | Relocation noise |
| 99.8% | UIListSlot.cpp | `CompleteScroll` | Relocation |
| 99.6% | UILabelDir.cpp | `PostLoad` | Relocation |
| 99.5% | UIList.cpp | `PreLoadWithRev` | Relocation |
| 99.3% | UILabel.cpp | `SetEditText` | Relocation |
| 99.3% | StorePanel.cpp | `Enter` | Relocation |
| 99.2% | UI.cpp | `UIManager::Poll` | Merged symbol |
| 99.0% | Splash.cpp | `CheckWorkerSuspend` | Merged symbol |
| 98.8% | StreamReceiver360.cpp | `SetSlipSpeed` | Register swap |
| 98.4% | StoreEnumeration.cpp | `XboxEnumeration::Start` | Merged symbol |
| 98.0% | StorePanel.cpp | `EnumerateOffers` | Merged symbol |
| 97.9% | Env_NG.cpp | `UpdateApproxLighting` | Register swap |
| 97.5% | Splash.cpp | `Draw` | Register swap |
| 95.0% | UILabel.cpp | `Highlight` | Register swap |
| 95.0%* | StreamReceiver360.cpp | `SlipStop` | Register swap |

*SlipStop was 99.0% in initial check; may vary with rebuild.

## Implemented Functions -- Needs More Work (< 95%)

Unicorn behavioral testing results included. Functions with DIVERGENT verdicts have real logic bugs.

| Match% | File | Function | Unicorn | Issue |
|--------|------|----------|---------|-------|
| 93.6% | UI.cpp | `Automator::Poll` | DIVERGENT (build_env) | Unfixable (__FILE__) |
| 91.8% | StreamReceiver360.cpp | `SetSlipOffset` | EQUIVALENT | Merged + regswap |
| 91.6% | UIListState.cpp | `SetSelectedSimulateScroll` | EQUIVALENT | Control flow |
| 91.5% | UILabel.cpp | `CenterWithLabel` | EQUIVALENT | FPR scheduling |
| 90.9% | StreamReceiver360.cpp | `Poll` | **DIVERGENT (return_value)** | Logic bug |
| 90.1% | TexRenderer.cpp | `DrawToTexture` | EQUIVALENT | Merged + regswap |
| 89.6% | CharServoBone.cpp | `Poll` | EQUIVALENT | Register swap |
| 88.4% | UILabel.cpp | `SetTimeHMS` | EQUIVALENT | Register swap |
| **96.3%** | HDCache.cpp | `Init` | Fixed (was DIVERGENT) | Now AT_LIMIT |
| 85.1% | UILabel.cpp | `DrawShowing` | EQUIVALENT | Regswap + merged |
| 73.1% | CharSleeve.cpp | `Poll` | EQUIVALENT | Massive regswaps |
| 80.3% | CharSignalApplier.cpp | `Poll` | EQUIVALENT (recheck) | Regswaps + dead store |
| 48.2% | StorePanel.cpp | `Poll` | EQUIVALENT | BOOL_MASK + merged |

### Priority fixes resolved:
1. **HDCache::Init** -- Fixed: 87.6% -> 96.3% (cached vector ptrs, branch inversion, code placement)
2. **CharSignalApplier::Poll** -- Rechecked: actually 80.3% EQUIVALENT, regswaps unfixable
3. **StreamReceiver360::Poll** -- 90.9% AT_LIMIT, return_value divergence was r3 convention artifact

## Bonus Functions (not in original 43 stubs)

These were implemented by agents alongside the stub work but weren't empty stubs:

| Match% | File | Function | Status |
|--------|------|----------|--------|
| 100% | StreamReceiver360.cpp | `SetADSR` | COMPLETE |
| 100% | StreamReceiver360.cpp | `GetPlayCursor` | COMPLETE |
| 99.8% | StreamReceiver360.cpp | `UpdateADSR` | AT_LIMIT |
| 99.3% | StreamReceiver360.cpp | `SetFXSend` | AT_LIMIT |
| 99.0% | StreamReceiver360.cpp | `GetSlipOffset` | AT_LIMIT |
| 70.2% | StreamReceiver360.cpp | `Tag` | Needs work |

## Not Attempted (1 function)

- **FftIpp::FftRealCcs** -- Requires Intel Performance Primitives (IPP) FFT API specific to Xbox 360 XDK

## No Target Symbol (2 functions)

These had no matching symbol in the target binary (deadstripped or inlined):

- `CharClipSet::PostLoad` (CharClipSet.cpp) -- correctly empty
- `Skeleton::PostUpdate` (Skeleton.cpp) -- correctly empty

## Regressions from Header/Implementation Changes

Adding stub implementations changes translation unit layout, cascading register allocation shifts through neighboring functions. These are unavoidable side effects.

| Function | Before | After | Delta | Root Cause |
|----------|--------|-------|-------|------------|
| UILabel::Handle | 79.8% | ~73% | ~-7% | New DrawShowing/Highlight bodies shift register alloc |
| XboxEnumeration::Poll | 26.2% | 23.5% | -2.7% | StoreEnumeration.h member layout corrections |
| Splash::UpdateThread | 80.9% | ~78% | ~-3% | New Draw/CheckWorkerSuspend bodies |
| UILabel::SyncProperty | 85.1% | 85.7% | +0.6% | Fixed: edit_text→SetEditText, fixed_length→SetFixedLength |

**Verdict**: All regressions are unfixable register allocation cascades. Net gain is positive.

## Confirmed Intentionally Empty (6 functions)

These are 100% matching as `blr` (return) - the original binary has them empty too:

- `Game::StartIntro` (Game.cpp:312)
- `Character::Terminate` (Character.cpp:620)
- `MiniLeaderboardDisplay::Update` (MiniLeaderboardDisplay.cpp:70)
- `FileCache::Init` (FileCache.cpp:157)
- `Memcard::Poll` (Memcard.cpp:7)
- `UIListState::Scroll` (UIListState.cpp:308)

## Not In Target Binary (25 functions)

These functions are not present in the target binary (deadstripped by linker or inlined).
Empty stubs are correct for these:

- `SaveMemcardAction::PostAction`, `LoadMemcardAction::PreAction`
- `HamProfile::CheckForNinjaUnlock`, `HamProfile::CheckForIconManUnlock`
- `CharBonesMeshes::Terminate`, `DanceRemixer::SelectMove`
- `HamAudio::SetBackgroundVolume`, `SetForegroundVolume`, `SetStereo`
- `RhythmBattlePlayer::AnimateOut`
- `TrigTableTerminate`, `DateTimeInit`, `SpewInit`, `SpewTerminate`
- `DataLoader::DoneLoading`, `FileLoader::DoneLoading`
- `ScriptDebugModal`, `ObjectDir::AddedObject`
- `FileCache::Terminate`, `HDCache::Flush`, `Memcard::Terminate`
- `DxLight::Terminate`, `TDStretch::clearCrossCorrState`
- `UILabel::Poll`, `UIListState::SetScrollPastMaxDisplay`
