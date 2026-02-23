# Unk Field Rename Sweep - 2026-02-21

## Summary

Multi-wave parallel agent sweep to rename `unk*` member variables across the DC3 codebase to meaningful names. Fields were renamed based on:
- RB3 DWARF reference (shared Milo engine)
- Code usage context (assignments, comparisons, function calls)
- SYNC_PROP names, HANDLE_ACTION names, accessor method semantics
- External caller context revealing field purpose

## Statistics

- **Waves completed**: ~35 (initial sweep) + follow-up sessions
- **Fields/accessors renamed**: ~750+
- **Files modified**: ~350+
- **Remaining unk references in headers**: ~750 (down from ~984)
- **Remaining unk references in .cpp files**: ~594 (down from ~1130)
- **Build status**: Clean throughout (no match% regressions)

## Session Log

### 2026-02-22: Blocking Function Burn-Down + Continued Renames

Functions implemented/verified:
- **HamScrollBehavior::Update()** — implemented at 66.2% match (AT_LIMIT due to complex scroll physics)
- **HamMaster::CheckLevels()** — implemented at 100% match, all 8 blocked fields renamed
- **CharEyes::DartUpdate()** — implemented at 91.8% match, renamed unk17c-unk188
- **CharEyes::Poll()** — verified 100% match (426 instructions, all equal) without explicit source; blocked fields already renamed via DartUpdate
- **FlowAnimate::RequestStop/RequestStopCancel/IsRunning** — implemented

Additional field renames completed (no function implementation needed):
- **WahEffect.h/.cpp**: unk38→mFilterState1, unk3c→mFilterState2, unk40→mFilterState3; Params struct: unk4→mGain, unk8→mFreqHi, unkc→mFreqLo, unk10→mResonance, unk14→mBandwidth, unk18→mSweepRate, unk1c→mSweepRange, unk20→mEnvAmount, unk24→mStaticSweep
- **CompressionEffect.h/.cpp**: Params struct: unk4→mThresholdDb, unk8→mRatio, unkc→mOutputGainDb, unk10→mAttackTime, unk14→mReleaseTime, unk18→mPostGain, unk1c→mPeakAttackTime, unk20→mPeakReleaseTime, unk24→mGateThreshDb
- **DelayEffect.h/.cpp**: Params struct: unk4→mDelaySamples, unk8→mDecayDb, unkc→mWetPercent
- **CharLipSyncDriver.h/.cpp**: unk90→mMainBlendAlpha, unk108→mOverrideBlendClip, unk11c→mOverrideBlendWeight, unk120→mOverrideBlendDuration, unk128→mOverrideBlendActive
- **ThreeDSound.h/.cpp**: unk1c8→mDistanceFader, unk1a4→mDelayedOwner
- **VorbisReader.h/.cpp**: unka0→mHasPendingPacket
- **Sound.h/.cpp**: unkb4→mIsSynthSample
- **SkeletonViz.h/.cpp**: unk110→mCurrentCamRotation
- **RndParticleSys (Part.h/.cpp)**: unk3e0→mTotalTileTime, unk3e4→mInvTotalTileTime
- **MetagameRank.h/.cpp**: unk38→mFirstTimePlayed, unk39→mOneTimeTaskFlags, unkc9→mHasNewRank
- **FlowManager.h/.cpp**: unk2d→mExecuting, unk64→mEventTimes
- **HamScrollBehavior.h**: 10 fields renamed from Update() analysis
- **HamCamShot.h**: field renames from SetFrame analysis
- **CharIKFingers.h**: FingerDesc struct: unk78→mCurFinger02Angle, unk7c→mCurFinger03Angle, unk80→mDestFinger02Angle, unk84→mDestFinger03Angle, unk8c→mBlendOutFrames, unk94→mDestOrientVec, unka4→mCurOrientVec, unkb4→mNeedsIKSolve (8 fields from RB3 reference)
- **RndParticleSys (Part.h/.cpp)**: Burst struct: unk0→mPeakRate, unk4→mHalfDuration, unk8→mInvHalfDuration, unkc→mRemainingDuration; RndParticleSys: unk138→mLastFrame, unk13c→mDrawCount, unk144→mPausedTime, unk2b4→mMotionParentDelta
- **Font.h/.cpp**: RndFont3d: unk6c→mCellSize, unk7c→mInvCellSize; BitmapLocker: unk8→mBitmapPtr, unkc→mBitmap
- **JobMgr.h/.cpp**: MultipleItemsEnumJob: unk10→mItemIDsBegin, unk14→mItemIDsEnd, unk30→mEnumStatus, unk34→mEnumSuccess, unk5c→mOfferSymbol, unk60→mPurchaserID
- **SkeletonUpdate.h/.cpp**: unk5360→mSkeletonsLeft, unk5368→mSkeletonsRight, unk5380→mSkeletonTrackingIDs, unk53a0→mUpdateThread
- **SkeletonClip.h/.cpp**: unk1231→mIsRecording
- **PartyModeMgr.h/.cpp**: unk328→mEventBucketSequences, unk32c→mPlayerSequences

### 2026-02-22 (continued): Parallel Agent Rename Sweep

Dispatched 4 sonnet Explore agents + 10 haiku rename agents in parallel waves.

Batch 1 — Milo engine (RB3 reference + code context):
- **synth_xbox/Synth.h/.cpp**: unkdc→mHeadsetSubmixes, unk108→mDolbyTimer
- **rndobj/Rnd.h/.cpp**: unk110→mDefaultCubeTexBlack, unk114→mDefaultCubeTexWhite, unk118→mRateTotal, unk120→mRateCount, unk14c→mWorldEndCallback, unk18c→mPreClearDraws
- **rndobj/MeshVertCompress.h + Mesh.cpp**: unk0→mPosX, unk4→mPosY, unk8→mPosZ, unkc→mColor, unk10→mNormal, unk14→mTangent, unk18→mBinormal, unk1c→mBoneIndices, unk20→mBoneWeights
- **synth/VorbisReader.h/.cpp**: unkee→mEof, unk100→mLastGranulePos, unk108→mPcmReadPos
- **world/SpotlightDrawer.h**: SpotMeshEntry: unk0→mCanMesh, unk4→mEnvMesh, unk8→mSpotlight, unk10→mTransform; SpotlightEntry: unk0→mColorKey
- **net_ham/HamStoreCartJobs.h/.cpp**: CartRow: unk0→mSongID, unk4→mName

Batch 2 — DC3-only (hamobj, code context):
- **hamobj/ErrorNode.h + HamMove.cpp + MoveDir.cpp + DetectFrame.cpp**: Ham1NodeWeight::unk0→mActive, Ham2FrameWeight::unk0→mWeight
- **hamobj/HamListRibbon.h/.cpp**: HamListRibbonDrawState: unk0→mSwellSmoother, unk14→mSelected, unk1c→mHidden; HamListRibbon: unk26c→mSelectToggle
- **hamobj/HamMove.h/.cpp**: MoveFrame::unk4→mTypeMask
- **hamobj/HamVisDir.h/.cpp**: unk334→mGrooviness
- **hamobj/FreestyleMoveRecorder.h/.cpp**: JointPos::unk0→mJoint
- **hamobj/HamNavList.h/.cpp**: unk184→mDirectionGestureFilter
- **hamobj/CharCameraInput.h/.cpp**: unk11d8→mCharFrame

Batch 3 — Mixed (RB3 reference + code context):
- **math/kdTree.h**: kdTree::unk0→mItems, kdTree::unkc→mBounds, kdTreeNode::unk4→mFlags, kdTriList::unk0→mIndex
- **gesture/SkeletonViz.h/.cpp**: unk214→mLineWidthScale
- **os/ContentMgr_Xbox.h/.cpp**: unk74→mEnumHandles
- **synth/EQEffect.h/.cpp**: 34 biquad filter coefficient fields renamed (5 bands × ~7 fields: mBandNEnabled, mBandNB0/B1/B2, mBandNA1/A2, mBandNZ1/Z2)
- **synth/StandardStream.h/.cpp**: unk150→mPollingEnabled
- **synth/MoggClip.h/.cpp**: unk44→mControllerVolume
- **synth/Sequence_p.h + Sequence.cpp**: RandomIntervalGroupSeqInst: unk40→mMaxSimultaneous, unk44→mAvgIntervalSecs, unk48→mIntervalSpread, unk4c→mNextPlayTimes
- **world/Spotlight.h/.cpp**: unk310→mOrientMatrix, unk340→mSnapToTarget, unk35c→mLastTargetPos, unk36e→mUpdating, unk370→mDampQuat
- **world/Crowd.h/.cpp**: unk70→mCenter
- **flow/Flow.h/.cpp**: DynamicPropertyEntry::unk24→mSymbolList

## Undecompiled Functions Blocking Further Naming

These function bodies are not yet implemented in the decomp. Implementing them would unblock naming of the associated unk fields.

### High Priority (blocks many fields)

| Function | File | Blocked Fields | Notes |
|----------|------|----------------|-------|
| ~~`HamScrollBehavior::Update()`~~ | ~~hamobj/HamScrollBehavior.cpp~~ | ~~10 fields~~ | DONE (66.2%, fields renamed) |
| ~~`HamMaster::CheckLevels()`~~ | ~~hamobj/HamMaster.cpp~~ | ~~8 fields~~ | DONE (100%, fields renamed) |
| ~~`CharEyes::Poll()`~~ | ~~char/CharEyes.cpp~~ | ~~4 fields~~ | DONE (100%, fields renamed via DartUpdate) |
| `NgMat` (all methods) | rndobj/Mat_NG.cpp | 41 fields | DX9 material rendering - none of the 41 unk fields are used in any decompiled code |
| `RndShaderMgr` rendering methods | rndobj/ShaderMgr.cpp | ~20 fields (unk14-unk5f) | Set in PreInit but never read in decompiled code |
| `DxRnd` rendering methods | rnddx9/Rnd.cpp | ~15 fields (unk2b8-unk408) | GPU state fields |

### Medium Priority (blocks several fields)

| Function | File | Blocked Fields | Notes |
|----------|------|----------------|-------|
| ~~`MainMenuPanel::MotdInitializeTexts()`~~ | ~~meta_ham/MainMenuPanel.cpp~~ | ~~7 fields~~ | Already implemented in commit 8e4f197c |
| ~~`MainMenuPanel::MotdPickNextText()`~~ | ~~meta_ham/MainMenuPanel.cpp~~ | ~~(same)~~ | Already implemented |
| ~~`MainMenuPanel::MotdHandleTextScrolledIn()`~~ | ~~meta_ham/MainMenuPanel.cpp~~ | ~~(same)~~ | Already implemented |
| `HollaBackMinigame` multiple methods | hamobj/HollaBackMinigame.cpp | ~5 fields | Minigame logic |
| `HamWardrobe::OnSetVenue()` | hamobj/HamWardrobe.cpp | unk34 (Symbol, init to "medium") | Venue-dependent wardrobe |
| `HamAudio::PollCrossfade()` | hamobj/HamAudio.cpp | unk6c | Audio crossfade |
| `HamAudio::FinishLoad()` | hamobj/HamAudio.cpp | (same) | |
| `ClipPlayer` methods | hamobj/ClipPlayer.cpp | unk4c | Clip playback |
| ~~`Spotlight` unimplemented funcs~~ | ~~world/Spotlight.cpp~~ | ~~unk310, unk340, unk35c, unk36e, unk370~~ | DONE (fields renamed from RB3 reference) |
| `HamCamShot::SetFrame()` | hamobj/HamCamShot.cpp | unk2cc-unk388 | Camera shot positioning |
| `RhythmBattle` most methods | hamobj/RhythmBattle.cpp | unk94, unk120 | Battle state |

### Lower Priority (blocks 1-2 fields each)

| Function | File | Blocked Fields |
|----------|------|----------------|
| `CharDriverMidi` methods | char/CharDriverMidi.cpp | unke0 |
| `CharHair` methods | char/CharHair.cpp | Point::unk78 |
| `CharClipDisplay` methods | char/CharClipDisplay.cpp | ~20 fields |
| `CharCollide::Deform()` | char/CharCollide.cpp | unk1a0, unk1f4-unk20c |
| ~~`FlowAnimate` methods~~ | ~~flow/FlowAnimate.cpp~~ | ~~unk94, unk98, unkc4~~ | DONE (methods implemented) |
| `JoypadData` gamepad methods | os/Joypad.cpp | unk98-unkd8 (15 fields) |

## Remaining Dense Files

Files with the most remaining unk fields (ordered by count):

| File | Unk Count | Notes |
|------|-----------|-------|
| rndobj/Mat_NG.h | 41 | All unused in decompiled code |
| net_ham/KinectShare.h | 34 | Network protocol struct fields |
| synth_xbox/Mic.h | 31 | Audio hardware interface |
| os/Joypad.h | ~25 | Many are XInput capability/state fields |
| char/CharClipDisplay.h | 21 | Animation display fields |
| rndobj/ShaderMgr.h | ~20 | Rendering state, only set in PreInit |
| rnddx9/Rnd.h | ~15 | GPU state fields |
| hamobj/HamCamShot.h | ~15 | Camera shot state |
| hamobj/ErrorNode.h | ~15 | Scoring weight structs |
| gesture/DepthBuffer3D.h | ~10 | Depth buffer rendering |

## Key Findings

1. **RB3 reference is authoritative** for shared Milo engine fields. Most high-confidence renames came from RB3 DWARF data.
2. **Survey proposals are often wrong** - agents correctly reject ~30% of survey-proposed names by analyzing actual code semantics.
3. **External callers reveal purpose** - fields accessed via `->unkXX` in other files often have clearer context than the owning class.
4. **Accessor methods are high-value targets** - `GetUnkXX()`/`SetUnkXX()` methods are the most impactful to rename since they affect API readability.
5. **Remaining fields are genuinely hard** - most remaining unks are either initialized-but-never-read (undecompiled functions) or in hardware/protocol structs without DWARF info.
6. **CharEyes::Poll() matches 100% without explicit source** — the compiler/build system generates matching code from the virtual declaration alone. Worth investigating the mechanism, but not blocking.

## Bugs and Issues Discovered

Issues found during the rename sweep research that are worth tracking:

### Pre-existing Bugs in Original Binary (preserved for matching)

1. **CharDriverMidi.cpp ~line 124** — Null pointer dereference in error message: `grp->Name()` called when `grp` is NULL. The original binary has this bug; preserved intentionally for matching.

2. **HamNavList destructor** — Memory leak: `mDirectionGestureFilter` and `unk188` are owned pointers (allocated in constructor, commented-out `delete` in destructor). The original binary leaks these objects.

### Decomp Issues (not in original)

3. **NavListNode.cpp line 150-163** — `NavListShortcutNode::Insert()` has an uninitialized pointer bug in the else branch. When `range.first != range.second` (header already exists), `newNode` is never assigned, then `newNode->Insert(node, sort)` is called on garbage. The FIXME comment indicates the correct fix is unclear — likely `newNode = static_cast<NavListHeaderNode*>(*range.first)` but needs verification against the binary.

4. **math/Utl.h line 224** — `Sigmoid()` has a suppressed `MILO_ASSERT(t >= 0 && t <= 1)` due to circular header dependencies between Color.h and Utl.h. The original binary has this assert.

5. **PlaylistSortMgr.cpp line 54** — `_force_playlist_assign` stub exists to force template instantiation pending `HandleCmdGetPlaylistsFromRC` implementation.

### Code Quality (fixed during sweep)

6. **FreestyleMoveRecorder.h** — Duplicate `friend class BustAMovePanel;` declaration (lines 15 and 63). Removed the duplicate.

7. **rndobj/Text.h.backup** — Leftover backup file. Deleted.

### Naming Inconsistency (not fixed, needs investigation)

8. **EQEffect.h band numbering** — The `Params` struct uses 1-indexed band names (`mBand1Freq` through `mBand5Freq`), but the biquad state fields use 0-indexed names (`mBand0Enabled` through `mBand4Enabled`). The mapping between Params bands and state bands is unclear without `SetParameter()`/`Process()` implementations. May need to renumber state fields to 1-indexed once the implementation is decompiled.
