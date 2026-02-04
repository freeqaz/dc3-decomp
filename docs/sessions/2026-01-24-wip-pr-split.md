# WIP PR Split Plan - 2026-01-24

This document outlines the plan to split the large WIP commit into 6 smaller, focused PRs.

## Overview

The original WIP commit (`8d57a78`) contains 86 files with various match percentages.
We're splitting this into 6 PRs based on logical code groupings.

**Base branch:** `upstream/main` (after first high-confidence PR was merged)

---

## PR 1: lazer/game

**Branch name:** `freeqaz/wip-lazer-game`

| File | Match % | Delta | Size |
|------|---------|-------|------|
| BustAMovePanel.cpp | 39.50% | +6.43% | 30,992 |
| BustAMovePanel.h | - | - | - |
| Game.cpp | 82.43% | -0.17% | 20,388 |
| SongSequence.cpp | 91.94% | +16.88% | 11,548 |
| SongSequence.h | - | - | - |

**Files:**
```
src/lazer/game/BustAMovePanel.cpp
src/lazer/game/BustAMovePanel.h
src/lazer/game/Game.cpp
src/lazer/game/SongSequence.cpp
src/lazer/game/SongSequence.h
```

---

## PR 2: lazer/meta_ham

**Branch name:** `freeqaz/wip-lazer-meta-ham`

| File | Match % | Delta | Size |
|------|---------|-------|------|
| AccomplishmentManager.cpp | 89.57% | +0.05% | 34,320 |
| CampaignProgress.cpp | 97.53% | -0.15% | 13,268 |
| ChallengeResultPanel.cpp | 79.78% | +17.74% | 6,444 |
| Challenges.h | - | - | - |
| HamMemcardAction.cpp | 88.67% | 0.00% | 600 |
| HamMemcardAction.h | - | - | - |
| HamStorePanel.cpp | 45.59% | +3.14% | 21,168 |
| HamStoreProvider.h | - | - | - |
| KinectSharePanel.cpp | 89.25% | -0.00% | 8,160 |
| MetagameRank.cpp | 34.49% | +14.54% | 28,940 |
| MetagameRank.h | - | - | - |
| MetaPanel.cpp | 90.55% | +0.05% | 24,192 |
| MoveRatingHistory.cpp | 94.78% | +0.26% | 2,192 |
| NavListSortMgr.cpp | 95.55% | +7.28% | 11,220 |
| NavListSortMgr.h | - | - | - |
| ProfileMgr.cpp | 86.41% | 0.00% | 22,952 |
| ProfileMgr.h | - | - | - |
| SaveLoadManager.cpp | 72.56% | +14.49% | 25,640 |
| SaveLoadManager.h | - | - | - |

**Files:**
```
src/lazer/meta_ham/AccomplishmentManager.cpp
src/lazer/meta_ham/CampaignProgress.cpp
src/lazer/meta_ham/ChallengeResultPanel.cpp
src/lazer/meta_ham/Challenges.h
src/lazer/meta_ham/HamMemcardAction.cpp
src/lazer/meta_ham/HamMemcardAction.h
src/lazer/meta_ham/HamStorePanel.cpp
src/lazer/meta_ham/HamStoreProvider.h
src/lazer/meta_ham/KinectSharePanel.cpp
src/lazer/meta_ham/MetagameRank.cpp
src/lazer/meta_ham/MetagameRank.h
src/lazer/meta_ham/MetaPanel.cpp
src/lazer/meta_ham/MoveRatingHistory.cpp
src/lazer/meta_ham/NavListSortMgr.cpp
src/lazer/meta_ham/NavListSortMgr.h
src/lazer/meta_ham/ProfileMgr.cpp
src/lazer/meta_ham/ProfileMgr.h
src/lazer/meta_ham/SaveLoadManager.cpp
src/lazer/meta_ham/SaveLoadManager.h
```

---

## PR 3: system/char + system/flow + system/gesture

**Branch name:** `freeqaz/wip-system-char-flow-gesture`

| File | Match % | Delta | Size |
|------|---------|-------|------|
| CharBones.cpp | 29.82% | +0.17% | 13,900 |
| CharBonesBlender.cpp | 97.33% | 0.00% | 3,184 |
| CharBonesMeshes.cpp | 61.89% | +16.07% | 9,856 |
| CharClip.cpp | 82.99% | +5.96% | 33,632 |
| CharLipSync.cpp | 51.22% | 0.00% | 25,100 |
| CharTaskMgr.cpp | 84.21% | +57.89% | 228 |
| CharTaskMgr.h | - | - | - |
| CharUpperTwist.h | - | - | - |
| FlowEventListener.cpp | 83.36% | +0.00% | 4,520 |
| BaseSkeleton.cpp | 39.75% | +0.00% | 2,596 |
| GestureMgr.cpp | 69.13% | +0.04% | 13,388 |
| SpeechMgr.cpp | 97.82% | 0.00% | 14,448 |

**Files:**
```
src/system/char/CharBones.cpp
src/system/char/CharBonesBlender.cpp
src/system/char/CharBonesMeshes.cpp
src/system/char/CharClip.cpp
src/system/char/CharLipSync.cpp
src/system/char/CharTaskMgr.cpp
src/system/char/CharTaskMgr.h
src/system/char/CharUpperTwist.h
src/system/flow/FlowEventListener.cpp
src/system/gesture/BaseSkeleton.cpp
src/system/gesture/GestureMgr.cpp
src/system/gesture/SpeechMgr.cpp
```

---

## PR 4: system/hamobj + system/math + system/midi + system/oggvorbis

**Branch name:** `freeqaz/wip-system-hamobj-math-midi`

| File | Match % | Delta | Size |
|------|---------|-------|------|
| DancerSkeleton.h | - | - | - |
| DetectFrame.h | - | - | - |
| FreestyleMoveRecorder.h | - | - | - |
| HamDirector.cpp | 80.75% | 0.00% | 81,124 |
| HamScrollBehavior.cpp | 60.66% | +0.01% | 4,332 |
| RhythmBattle.cpp | 42.38% | +18.18% | 35,144 |
| RhythmDetector.cpp | 41.43% | 0.00% | 16,728 |
| Geo.cpp | 23.04% | +0.00% | 17,784 |
| Interp.cpp | 82.16% | +35.09% | 852 |
| Key.cpp | 90.12% | +0.36% | 2,320 |
| Key.h | - | - | - |
| SHA1.cpp | 60.99% | +0.66% | 7,092 |
| Trig.cpp | 97.13% | 0.00% | 1,444 |
| MidiReader.cpp | 93.74% | 0.00% | 9,420 |
| psy.c | 95.95% | +0.00% | 10,572 |

**Files:**
```
src/system/hamobj/DancerSkeleton.h
src/system/hamobj/DetectFrame.h
src/system/hamobj/FreestyleMoveRecorder.h
src/system/hamobj/HamDirector.cpp
src/system/hamobj/HamScrollBehavior.cpp
src/system/hamobj/RhythmBattle.cpp
src/system/hamobj/RhythmDetector.cpp
src/system/math/Geo.cpp
src/system/math/Interp.cpp
src/system/math/Key.cpp
src/system/math/Key.h
src/system/math/SHA1.cpp
src/system/math/Trig.cpp
src/system/midi/MidiReader.cpp
src/system/oggvorbis/psy.c
```

---

## PR 5: system/rndobj

**Branch name:** `freeqaz/wip-system-rndobj`

| File | Match % | Delta | Size |
|------|---------|-------|------|
| AmbientOcclusion.cpp | 42.71% | 0.00% | 30,008 |
| Anim.cpp | 86.84% | +0.07% | 12,480 |
| Bitmap.h | - | - | - |
| BoxMap.cpp | 34.42% | +21.84% | 2,396 |
| BoxMap.h | - | - | - |
| Cam.cpp | 81.87% | 0.00% | 10,176 |
| Cam.h | - | - | - |
| Dir.cpp | 93.05% | +0.02% | 15,836 |
| Draw.cpp | 97.28% | +0.12% | 7,424 |
| Env_NG.cpp | 15.84% | +3.53% | 6,272 |
| Font.cpp | 70.46% | +16.32% | 24,504 |
| Group.cpp | 72.72% | +0.18% | 10,760 |
| HiResScreen.cpp | 77.51% | +64.57% | 6,672 |
| HiResScreen.h | - | - | - |
| Line.cpp | 67.33% | +0.19% | 16,176 |
| Lit.cpp | 72.27% | +0.13% | 9,628 |
| Lit.h | - | - | - |
| Mat.cpp | 95.39% | +6.18% | 23,068 |
| Mesh.cpp | 67.32% | +0.05% | 46,708 |
| Part.cpp | 65.95% | 0.00% | 41,072 |
| PostProc.cpp | 73.42% | +0.00% | 17,276 |
| Rnd.cpp | 84.70% | +0.08% | 43,992 |
| Rnd.h | - | - | - |
| Shader.cpp | 20.54% | +0.10% | 19,444 |
| TexProc.h | - | - | - |
| Trans.cpp | 89.34% | +0.01% | 20,336 |

**Files:**
```
src/system/rndobj/AmbientOcclusion.cpp
src/system/rndobj/Anim.cpp
src/system/rndobj/Bitmap.h
src/system/rndobj/BoxMap.cpp
src/system/rndobj/BoxMap.h
src/system/rndobj/Cam.cpp
src/system/rndobj/Cam.h
src/system/rndobj/Dir.cpp
src/system/rndobj/Draw.cpp
src/system/rndobj/Env_NG.cpp
src/system/rndobj/Font.cpp
src/system/rndobj/Group.cpp
src/system/rndobj/HiResScreen.cpp
src/system/rndobj/HiResScreen.h
src/system/rndobj/Line.cpp
src/system/rndobj/Lit.cpp
src/system/rndobj/Lit.h
src/system/rndobj/Mat.cpp
src/system/rndobj/Mesh.cpp
src/system/rndobj/Part.cpp
src/system/rndobj/PostProc.cpp
src/system/rndobj/Rnd.cpp
src/system/rndobj/Rnd.h
src/system/rndobj/Shader.cpp
src/system/rndobj/TexProc.h
src/system/rndobj/Trans.cpp
```

---

## PR 6: system/synth + system/synth_xbox + system/ui + system/utl + system/world

**Branch name:** `freeqaz/wip-system-synth-ui-utl-world`

| File | Match % | Delta | Size |
|------|---------|-------|------|
| FxSendMeterEffect.cpp | 96.38% | +0.04% | 2,132 |
| ExternalMic.cpp | 3.76% | +0.16% | 7,344 |
| UIList.cpp | 55.36% | +0.09% | 29,964 |
| BinStream.h | - | - | - |
| KeylessHash.h | - | - | - |
| MultiTempoTempoMap.cpp | 91.04% | +0.55% | 3,808 |
| PoolAlloc.cpp | 88.37% | +1.01% | 3,160 |
| Str.cpp | 81.38% | +0.01% | 5,928 |
| CameraShot.cpp | 78.54% | +0.01% | 46,512 |

**Files:**
```
src/system/synth/FxSendMeterEffect.cpp
src/system/synth_xbox/ExternalMic.cpp
src/system/ui/UIList.cpp
src/system/utl/BinStream.h
src/system/utl/KeylessHash.h
src/system/utl/MultiTempoTempoMap.cpp
src/system/utl/PoolAlloc.cpp
src/system/utl/Str.cpp
src/system/world/CameraShot.cpp
```

---

## Execution Plan

1. Start from `upstream/main`
2. For each PR:
   - Create branch: `git checkout -b <branch-name> upstream/main`
   - Cherry-pick only the specific files from the WIP commit
   - Commit with descriptive message
   - Push and create PR

**Commands to create each PR:**

```bash
# Template for each PR
git checkout upstream/main
git checkout -b freeqaz/wip-<name>
git checkout 8d57a78 -- <file1> <file2> ...
git commit -m "WIP: <description>"
git push -u origin freeqaz/wip-<name>
```

---

## Summary

| PR | Area | Files | Notable Improvements |
|----|------|-------|---------------------|
| 1 | lazer/game | 5 | SongSequence +16.88% |
| 2 | lazer/meta_ham | 19 | ChallengeResultPanel +17.74%, SaveLoadManager +14.49% |
| 3 | system/char+flow+gesture | 12 | CharTaskMgr +57.89%, CharBonesMeshes +16.07% |
| 4 | system/hamobj+math+midi+ogg | 15 | Interp +35.09%, RhythmBattle +18.18% |
| 5 | system/rndobj | 26 | HiResScreen +64.57%, BoxMap +21.84%, Font +16.32% |
| 6 | system/synth+ui+utl+world | 9 | PoolAlloc +1.01% |

**Total: 86 files across 6 PRs**
