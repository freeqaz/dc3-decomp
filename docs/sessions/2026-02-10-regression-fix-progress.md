# Regression Fix Progress — 2026-02-10

Continuation of `2026-02-09-hidden-regression-triage.md`. Applied partial fixes, agents were interrupted mid-run.

**Build delta:** 4,035,776 → 4,044,900 matched bytes (+9,124 bytes, +36 functions)

---

## What's Done (verified with objdiff)

### StackString base class order — 6 functions FIXED
**File:** `src/system/utl/Str.h`
Reverted `ca723ac0`: base class order back to `FixedString, TextStream`, restored explicit `TextStream()` init and `(char*)` cast.

| Function | Before | After |
|----------|--------|-------|
| `StackString<100>::StackString(const char*)` | 78.4% | 99.8% |
| `StackString<256>::StackString(const char*)` | 78.4% | 99.3% |
| `StackString<1024>::StackString(const char*)` | 78.4% | 99.3% |
| `StackString<256>::StackString()` | 53.0% | 67.6% |
| `StackString<2048>::StackString()` | 53.0% | 67.6% |
| `StackString<4096>::StackString()` | 53.0% | 67.6% |

Remaining diff on all is LINKER_MERGED (unfixable).

### RndParticleSys::CreateParticles — FIXED (agent completed)
**File:** `src/system/rndobj/Part.cpp`, `src/system/rndobj/Part.h`
Restored full implementation: emit rate randomization, `CheckBursts`, `Burst::Set`, `Burst::Emit`, and `while (mEmitCount >= 1.0f)` loop. Moved `Burst()` ctor back to inline.

| Before | After |
|--------|-------|
| 12.4% | 98.7% |

### TexMovie::Load — IMPROVED (not fully recovered)
**File:** `src/system/movie/TexMovie.cpp`
Restored `ASSERT_REVS(8, 0)` with full rev-conditional load logic, `bs >> sRoot`, and `DoBeginMovieFromFile` call.

| Before | After | Original |
|--------|-------|----------|
| 56.2% | 67.7% | 68.1% |

Still 0.4% short of original. Remaining diff has REGISTER_SWAP and CONTROL_FLOW patterns. Likely needs minor tweaks (comparison style, variable ordering).

### FlowTrigger::ActivateWithParams — IMPROVED
**File:** `src/system/flow/FlowTrigger.cpp`
Reverted gotos back to if/else structure, restored `!= 0` comparison.

| Before | After | Original |
|--------|-------|----------|
| 94.7% | 95.0% | 95.6% |

0.6% short. Has LINKER_MERGED (unfixable) + OFFSET_SWAP patterns.

### MoveFrame::Load — PARTIALLY FIXED (needs verification)
**File:** `src/system/hamobj/HamMove.cpp`
Reverted 5 changes from merge commit `325e8fcf`:
- Restored braces on `else { unk4 = -1; }`
- Swapped loop limits back: `2` for mFrameWeights, `kNumMoveMirrored` for node iteration
- Reverted comparison: `fabsf(cur) >= 0.0000099999997f` (not reversed)
- Restored `for i < 2` (was changed to `i < 4`)
- Restored removed code block (oldNodeWeights copy, ham2 frame weights, default scale fill)

No objdiff run yet on this specific function.

### SkeletonClip::Load — IMPROVED (agent wrote changes before kill)
**File:** `src/system/gesture/SkeletonClip.cpp`
Restructured weighted/mWeighted conditional logic.

| Before | After | Original |
|--------|-------|----------|
| 96.5% | 97.1% | 98.8% |

---

## Unverified Agent Changes (from killed Tier 2/3 agents)

These files were modified by agents that were stopped mid-run. Changes may be incomplete or harmful. **Review before committing.**

| File | Functions targeted |
|------|-------------------|
| `src/system/char/CharSignalApplier.cpp` | SyncProperty, Save, Copy |
| `src/system/char/CharSignalApplier.h` | BoneOp struct, member names |
| `src/system/hamobj/MoveDir.cpp` | SongSeconds |
| `src/system/os/ContentMgr.h` | unknown |
| `src/system/os/UsbMidiGuitar.cpp` | unknown |
| `src/system/rndobj/HiResScreen.cpp` | GetBorderForTitle |
| `src/system/rndobj/Wind.cpp` | unknown |
| `src/system/rndobj/Wind.h` | unknown |
| `src/system/synth_xbox/Mic.cpp` | ~MicXbox dtor |
| `src/system/synth_xbox/Mic.h` | member layout |
| `src/system/utl/AllocInfo.cpp` | AllocInfo ctor |

**WARNING:** `CharSignalApplier::SyncProperty` is now at **82.3%**, down from 90.7% pre-session. The agent's header changes (BoneOp struct gutted, members renamed) likely caused collateral damage. Consider reverting `CharSignalApplier.cpp/.h`.

---

## Still TODO (not touched)

### Tier 1 remaining

| Function | Current | Original | Notes |
|----------|---------|----------|-------|
| `RndTransformable::Load` | 90.1% | 92.5% | No direct file changes in `24106e15`; regression from header change. Has REGISTER_SWAP + CONTROL_FLOW patterns. |
| `RndFont::Load` | 77.0% | 78.2% | Font.cpp changes in `24106e15` (KerningTable refactor, BitmapLocker, CharDefined restructure). Needs partial revert. |
| `Rnd::PreInit` | 98.6% | 100% | Agent killed before writing. Currently at 98.6% (up from 97.9% — some other change helped). Only diff_arg remaining. |
| `Trie::remove` | 69.9% | 79.2% | Massive rewrite in `325e8fcf` (merge). Entire trie.cpp/h replaced with macro-based approach. Would need reverting to pre-merge inline accessor style. Complex. |

### Tier 2 (not yet attempted or agent-incomplete)

| # | Function | Current | Original | Size |
|---|----------|---------|----------|------|
| 10 | `Sound::Stop` | 96.5% | 97.3% | 700 |
| 11 | `RndShockwave::SyncProperty` | 95.9% | 97.4% | 768 |
| 12 | `FlowAnimate::Save` | 85.6% | 100% | 424 |
| 13 | `HamDirector::Reteleport` | 95.0% | 97.2% | 568 |
| 14 | `HamCharacter::SyncProperty` | 96.7% | 98.2% | 388 |
| 15 | `FlowAnimate::Copy` | 77.3% | 81.9% | 220 |
| 16 | `CharSignalApplier::SyncProperty` | 82.3%* | 93.2% | 464 |
| 17 | `Skeleton::RequestIdentity` | 94.8% | 100% | 284 |
| 18 | `CharClipGroup::HasClip` | 55.2% | 100% | 128 |

*CharSignalApplier worse due to agent changes — revert header first.

### Tier 3 (22 functions)

| # | Function | Current | Original | Size | Commit |
|---|----------|---------|----------|------|--------|
| 19 | `Skeleton::Skeleton` | 88.9% | 100% | 152 | `24106e15` |
| 20 | `__linear_insert<SpotlightDrawer>` | 69.9% | 99.5% | 276 | `61efeb6b` |
| 21 | `CharSignalApplier::Save` | 89.9% | 100% | 164 | `24106e15` |
| 22 | `CharSignalApplier::Copy` | 0.7% | 41.1% | 216 | `24106e15` |
| 23 | `MemTracker::DiffDump` | 87.4% | 88.0% | 536 | `a5c48113` |
| 24 | `StorePanel::HandleNetCacheMgrFailure` | 49.2% | 62.3% | 200 | `3462e146` |
| 25 | `MetaPerformer::OnRecallMovePassed` | 82.0% | 91.8% | 192 | `3e8cbb3a` |
| 26 | `MicXbox::~MicXbox` | 75.1% | 89.5% | 176 | `7f5d5eea` |
| 27 | `HiResScreen::GetBorderForTitle` | 95.3% | 96.6% | 180 | `7f5d5eea` |
| 28 | `MoveDir::SongSeconds` | 84.8% | 93.5% | 192 | `325e8fcf` |
| 29 | `AllocInfo::AllocInfo` | 77.9% | 87.7% | 164 | `325e8fcf` |
| 30 | `Trie::get_free_node` | 94.3% | 98.1% | 168 | `325e8fcf` |
| 31 | `ClipDistMap scalar dtor` | 61.7% | 100% | 96 | `24106e15` |
| 32 | `MoveVariant::IsRest` | 88.6% | 89.2% | 268 | `325e8fcf` |
| 33 | `FftIpp::SetMode` | 58.8% | 60.1% | 448 | `f9630286` |
| 34 | `curlx_nonblock` | 81.4% | 100% | 56 | `24106e15` |
| 35 | `Curl_HMAC_init` | 97.0% | 100% | 420 | `24106e15` |
| 36 | `Curl_updateconninfo` | 96.1% | 100% | 416 | `24106e15` |
| 37 | `Curl_he2ai` | 94.9% | 100% | 364 | `24106e15` |
| 38 | `UILabel::OldResize` | 70.2% | 100% | 96 | `9f92e1e5` |
| 39 | `MQSongSortNode virtual` | 78.8% | 81.4% | 124 | `9d4b44e0` |
| 40 | `stlpmtx_std::vector<CharSignalApplier>` | 23.8% | 30.7% | 120 | `24106e15` |

Note: items 21, 22, 40 are all CharSignalApplier TU — will shift if header is reverted.
Items 34-37 are curl functions from `24106e15` — likely share a common header root cause.

---

## Recommended Next Steps

1. **Review & commit** the verified fixes (Str.h, Part.cpp/h, TexMovie.cpp, FlowTrigger.cpp, HamMove.cpp, SkeletonClip.cpp)
2. **Revert** `CharSignalApplier.cpp/.h` — agent changes made it worse
3. **Individually verify** remaining agent changes (MoveDir, HiResScreen, Mic, AllocInfo, etc.) with objdiff before keeping
4. **Trie revert** is the biggest remaining win (~9% on 924 bytes) but requires reverting the entire macro-based rewrite back to inline accessors
5. **Rnd::PreInit** at 98.6% may be close enough to skip (only diff_arg remaining)
6. **RndFont::Load** needs careful partial revert of the `24106e15` changes to Font.cpp
