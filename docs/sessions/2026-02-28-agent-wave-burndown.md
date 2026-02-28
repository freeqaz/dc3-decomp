# Agent Wave Burndown Session — 2026-02-28

## Overview

A wave of 18 parallel decomp agents ran across multiple units, implementing stubs and improving match percentages. This doc captures what each worktree produced and the porting plan.

Worktree base: `279089d9` → main repo HEAD: `e9b59e38`

---

## Worktree Inventory

### Single-file / Clean Ports (no conflicts)

| Worktree | Files | Description |
|---|---|---|
| `agent-ae9f3098` | `src/system/midi/MidiReader.cpp` | Replaced forward-declared `pow(float, int)` with full binary-exponentiation implementation (handles negative exponents) |
| `agent-ae9eb9e5` | `src/system/hamobj/HamMove.cpp` | Implemented `PSNRToDetectFrac(float)` — walks MoveRating PSNR thresholds and linearly interpolates detect fraction within rating bands |
| `agent-a91c92da` | `src/system/hamobj/RhythmDetector.cpp` | Fixed `initCheat()`: replaced dead commented-out guard with `static bool sInitialized` + inlined Symbol constructor calls |
| `agent-a6889968` | `src/system/rndobj/Tex.cpp` | `PlatformBppOrder()`: removed stale `plat` local, fixed Wii alpha branch, removed dead PS2 case, added 3DS case, restored `MILO_FAIL` default |
| `agent-a32a789d` | `src/system/char/CharClipSet.cpp` | Added `if (IsProxy()) return;` guard in `BEGIN_SAVES` block before serializing mCharFilePath/mPreviewClip/mFilterFlags |
| `agent-a6ff9745` | `src/system/char/ClipCollide.cpp`, `Waypoint.h` | Implemented `SyncWaypoint()` body: Enter, Teleport, adjust xfm.v by radius along correct axis for front/back/left/right; added `friend class ClipCollide` to Waypoint |

### Multi-file / Clean Ports

| Worktree | Files | Description |
|---|---|---|
| `agent-aaf2209c` | `flow/Flow.cpp`, `FlowPickOne.cpp`, `FlowQueueable.cpp`, `FlowSequence.cpp`, `FlowSwitchCase.cpp`, `FlowTimer.cpp`, `FlowTimer.h`, `FlowValueCase.h` | Flow system: `Flow::Enter()/Exit()`, `FlowPickOne::Activate()` (all 5 choice modes), `FlowQueueable::Deactivate/ChildFinished/Activate` (5 interrupt modes), `FlowSequence::Activate()`, `FlowSwitchCase::IsValidCase()`, `EventTask` class for FlowTimer |
| `agent-ad10d142` | `rndobj/Env.h`, `Lit.h`, `MultiMesh.h`, `ui/UI.h`, `world/CameraShot.h`, `world/Dir.cpp`, `world/FreeCamera.cpp`, `world/LightPreset.cpp/.h`, `world/Spotlight.h`, `world/SpotlightDrawer.h` | RndEnviron/RndLight accessors, `WorldDir::DrawShowing()` full impl, `FreeCamera::UpdateFromCamera()`, `LightPreset::Animate*()` family, Spotlight/SpotlightDrawer accessors |
| `agent-aba5b278` | `char/Character.cpp`, `rndobj/Text.h`, `ui/LocalePanel.cpp/.h`, `ui/UI.cpp/.h`, `ui/UIList.cpp/.h`, `ui/UIListLabel.cpp`, `ui/UIPanel.h`, `utl/Loader.cpp/.h` | `Character::UnhookShadow/SyncShadow/FindInterestObjects/DrawShowing/DrawLod`, Text.h + UIList/UIPanel/LocalePanel accessors, `LoadMgr::PollFrontLoader()` with glitch data |

### Conflicting Ports (files modified in both main WC and worktree)

| Worktree | Files | Conflict With | Resolution |
|---|---|---|---|
| `agent-a24115e8` | `ui/UILabel.cpp`, `ui/UILabel.h` | main WC | Merge: `LabelStyle::~LabelStyle(){}` + `ObjDirPtr` rename |
| `agent-a576df99` | `char/CharBonesSamples.cpp` | main WC | Merge: agent adds `LoadHeader()` impl, main WC has other changes |
| `agent-a558dc64` | `char/CharLipSyncDriver.cpp/.h`, `CharLipSync.h` | main WC | Merge: agent adds `UpdatePlayback()` + `Poll()` impls |
| `agent-a2aa2a0d` | 494 files — mass unk→named renames across all headers + LightPreset.cpp, PhysicsManager.cpp, HamNavList.cpp, BaseMaterial.cpp, FontBase.cpp | main WC (HamNavList.cpp, BaseMaterial.cpp, FontBase.cpp) | Merge headers carefully; skip LightPreset.cpp (covered by ad10d142) |

### No-op / Skipped

| Worktree | Reason |
|---|---|
| `agent-a132a84a` | No src changes (parent of sub-worktrees, sub-worktrees all at same base commit with no further changes) |
| `agent-a351cc89` | Only symbols.txt changed, no src changes |
| `agent-a8229b53` | Only config/objects.json + symbols.txt, no src changes |
| `agent-aba6321d` | No changes at all |
| `agent-a68435df` | Only `tools/project.py` — not porting build tool experiments |

---

## Porting Order

1. Single-file clean ports (MidiReader, HamMove, RhythmDetector, Tex, CharClipSet, ClipCollide/Waypoint)
2. Multi-file clean ports (Flow system, World/Light, Character/UI/Loader)
3. Conflicting files (UILabel, CharBonesSamples, CharLipSyncDriver, CharLipSync.h)
4. Mass header renames from a2aa2a0d (additive to headers not in main WC modified list)

---

## Notes

- `agent-a2aa2a0d` is the largest change (494 files, ~6,755 insertions) — a readability sweep renaming `unkXX` → semantic names. Headers not modified in main WC can be taken directly. Conflicting ones need manual merge.
- LightPreset.cpp appears in both `a2aa2a0d` and `ad10d142` — using `ad10d142`'s version which is more focused/correct.
- FlowSequence.cpp mentioned in both `aaf2209c` and `aba5b278` — `aaf2209c` is the authoritative flow agent.

---

## Full Diffs

### agent-ae9f3098 — MidiReader.cpp
```diff
diff --git a/src/system/midi/MidiReader.cpp b/src/system/midi/MidiReader.cpp
index 1e69a45d..c48b006f 100644
--- a/src/system/midi/MidiReader.cpp
+++ b/src/system/midi/MidiReader.cpp
@@ -174,7 +174,22 @@ void MidiReader::ReadMidiEvent(
         QueueChannelMsg(tick, status, data1, data2);
 }
 
-float pow(float, int);
+float pow(float base, int exponent) {
+    int exp = exponent;
+    if (exponent < 0)
+        exp = -exponent;
+    float result = 1.0f;
+    for (;;) {
+        if (exp & 1)
+            result *= base;
+        exp = (unsigned)exp >> 1;
+        if (!exp) break;
+        base *= base;
+    }
+    if (exponent < 0)
+        result = 1.0f / result;
+    return result;
+}
 
 void MidiReader::ReadMetaEvent(int tick, unsigned char type, BinStream &bs) {
     MidiVarLenNumber num(bs);
```

### agent-ae9eb9e5 — HamMove.cpp
```diff
diff --git a/src/system/hamobj/HamMove.cpp b/src/system/hamobj/HamMove.cpp
index e17d6ff6..cd7fda77 100644
--- a/src/system/hamobj/HamMove.cpp
+++ b/src/system/hamobj/HamMove.cpp
@@ -741,6 +741,51 @@ float HamMove::PSNRThreshold(MoveRating r) const {
     return thresh;
 }
 
+extern std::vector<float> sDefaultRatingThresholds;
+
+float HamMove::PSNRToDetectFrac(float psnr) const {
+    MoveRating rating = (MoveRating)0;
+    do {
+        float thresh = PSNRThreshold(rating);
+        if (psnr > thresh)
+            break;
+        rating = (MoveRating)(rating + 1);
+    } while ((int)rating < kNumMoveRatings);
+
+    if (rating == (MoveRating)0) {
+        return 1.0f;
+    }
+
+    MoveRating prevRating = (MoveRating)(rating - 1);
+    float upperPsnr = PSNRThreshold(prevRating);
+    float lowerPsnr;
+    if (rating == kNumMoveRatings) {
+        lowerPsnr = 0.0f;
+    } else {
+        lowerPsnr = PSNRThreshold(rating);
+    }
+    MILO_ASSERT_FMT(
+        upperPsnr > lowerPsnr,
+        "upper psnr threshold (%f) not greater than lower (%f)",
+        upperPsnr,
+        lowerPsnr
+    );
+
+    float frac = Clamp<float>(0.0f, 1.0f, (psnr - lowerPsnr) / (upperPsnr - lowerPsnr));
+
+    float upperDetect = 1.0f;
+    if (prevRating != (MoveRating)0) {
+        upperDetect = sDefaultRatingThresholds[prevRating - 1];
+    }
+    float lowerDetect;
+    if (rating == kNumMoveRatings) {
+        lowerDetect = 0.0f;
+    } else {
+        lowerDetect = sDefaultRatingThresholds[rating - 1];
+    }
+    return (upperDetect - lowerDetect) * frac + lowerDetect;
+}
+
 float HamMove::Confusability(const HamMove *move) const {
     if (move == this)
         return 4.0f;
```

### agent-a91c92da — RhythmDetector.cpp
```diff
diff --git a/src/system/hamobj/RhythmDetector.cpp b/src/system/hamobj/RhythmDetector.cpp
index b6c97d6c..50172bf8 100644
--- a/src/system/hamobj/RhythmDetector.cpp
+++ b/src/system/hamobj/RhythmDetector.cpp
@@ -69,17 +69,13 @@ namespace {
     }
 
     void initCheat() {
-        // if(SomeGlobalOrSymbol == 0) {
-        // SomeGlobalOrSymbol = 1;
-        Symbol cycle_movement_bone("cycle_movement_bone");
-        DataRegisterFunc(cycle_movement_bone, CycleDebugBone);
-        Symbol tighten_current_bone("tighten_current_bone");
-        DataRegisterFunc(tighten_current_bone, TightenDebugBone);
-        Symbol loosen_current_bone("loosen_current_bone");
-        DataRegisterFunc(loosen_current_bone, LoosenDebugBone);
-        Symbol ktb_debug_cheat("ktb_debug_cheat");
-        DataRegisterFunc(ktb_debug_cheat, DataSpaceCheat);
-        //}
+        static bool sInitialized = false;
+        if (sInitialized) return;
+        sInitialized = true;
+        DataRegisterFunc(Symbol("cycle_movement_bone"), CycleDebugBone);
+        DataRegisterFunc(Symbol("tighten_current_bone"), TightenDebugBone);
+        DataRegisterFunc(Symbol("loosen_current_bone"), LoosenDebugBone);
+        DataRegisterFunc(Symbol("ktb_debug_cheat"), DataSpaceCheat);
     }
 
     float Mean(const std::vector<float> &vec, int start, int end) {
```

### agent-a6889968 — Tex.cpp
```diff
diff --git a/src/system/rndobj/Tex.cpp b/src/system/rndobj/Tex.cpp
index feb84933..02105c6f 100644
--- a/src/system/rndobj/Tex.cpp
+++ b/src/system/rndobj/Tex.cpp
@@ -171,32 +171,28 @@ void RndTex::SaveBitmap(const char *bmp) {
 }
 
 void RndTex::PlatformBppOrder(const char *path, int &bpp, int &order, bool hasAlpha) {
-    Platform plat = TheLoadMgr.GetPlatform();
     bool bbb;
 
     switch (TheLoadMgr.GetPlatform()) {
     case kPlatformWii:
         order = 8;
         if (hasAlpha) {
-            order |= 0x100;
+            order = 0x148;
             bpp = 8;
         } else
             bpp = 4;
         order |= 0x40;
         break;
 
-    case kPlatformPS2:
-        break;
-
     case kPlatformXBox:
     case kPlatformPC:
     case kPlatformPS3:
         bbb = path && strstr(path, "_norm");
 
         if (bbb) {
-            if (plat == kPlatformXBox)
+            if (TheLoadMgr.GetPlatform() == kPlatformXBox)
                 order = 0x20;
-            else if (plat == kPlatformPS3)
+            else if (TheLoadMgr.GetPlatform() == kPlatformPS3)
                 order = 8;
             else
                 order = 0;
@@ -213,12 +209,18 @@ void RndTex::PlatformBppOrder(const char *path, int &bpp, int &order, bool hasAl
             bpp = 0x10;
         break;
 
+    case kPlatform3DS:
+        order = 0x600;
+        bpp = hasAlpha ? 8 : 4;
+        break;
+
     case kPlatformNone:
         order = 0;
         break;
-        // default:
-        //     MILO_FAIL("bad input platform value!");
-        //     break;
+
+    default:
+        MILO_FAIL("bad input platform value!");
+        break;
     }
 }
 
```

### agent-a32a789d — CharClipSet.cpp
```diff
diff --git a/src/system/char/CharClipSet.cpp b/src/system/char/CharClipSet.cpp
index ed647231..ffdbae1d 100644
--- a/src/system/char/CharClipSet.cpp
+++ b/src/system/char/CharClipSet.cpp
@@ -29,6 +29,8 @@ END_PROPSYNCS
 BEGIN_SAVES(CharClipSet)
     SAVE_REVS(24, 0)
     SAVE_SUPERCLASS(ObjectDir)
+    if (IsProxy())
+        return;
     bs << mCharFilePath;
     bs << mPreviewClip;
     bs << mFilterFlags;
```

### agent-a6ff9745 — ClipCollide.cpp + Waypoint.h
```diff
diff --git a/src/system/char/ClipCollide.cpp b/src/system/char/ClipCollide.cpp
index 0bd43652..f6b440a7 100644
--- a/src/system/char/ClipCollide.cpp
+++ b/src/system/char/ClipCollide.cpp
@@ -84,6 +84,34 @@ void ClipCollide::SyncWaypoint() {
     static Symbol back("back");
     static Symbol left("left");
     static Symbol right("right");
+    mChar->Enter();
+    Waypoint *wp = mWaypoint;
+    mChar->Teleport(wp);
+    Transform xfm = wp->WorldXfm();
+    float radius = wp->mYRadius;
+    if (radius <= 0)
+        radius = wp->mRadius;
+    if (mPosition == front) {
+        xfm.v.x += xfm.m.y.x * radius;
+        xfm.v.y += xfm.m.y.y * radius;
+        xfm.v.z += xfm.m.y.z * radius;
+    } else if (mPosition == back) {
+        radius = -radius;
+        xfm.v.x += xfm.m.y.x * radius;
+        xfm.v.y += xfm.m.y.y * radius;
+        xfm.v.z += xfm.m.y.z * radius;
+    } else if (mPosition == left) {
+        radius = wp->mRadius;
+        xfm.v.x += xfm.m.x.x * radius;
+        xfm.v.y += xfm.m.x.y * radius;
+        xfm.v.z += xfm.m.x.z * radius;
+    } else {
+        radius = -wp->mRadius;
+        xfm.v.x += xfm.m.x.x * radius;
+        xfm.v.y += xfm.m.x.y * radius;
+        xfm.v.z += xfm.m.x.z * radius;
+    }
+    mChar->SetLocalXfm(xfm);
 }
 
 void ClipCollide::ClearReport() {
diff --git a/src/system/char/Waypoint.h b/src/system/char/Waypoint.h
index dfda934f..f22d0157 100644
--- a/src/system/char/Waypoint.h
+++ b/src/system/char/Waypoint.h
@@ -8,6 +8,7 @@
 /** "A waypoint for character movement. Characters walk to
  *  these, start themselves out from these, etc." */
 class Waypoint : public RndTransformable {
+    friend class ClipCollide;
 public:
     virtual ~Waypoint();
     OBJ_CLASSNAME(Waypoint)
```

### agent-a24115e8 — UILabel.cpp + UILabel.h
```diff
diff --git a/src/system/ui/UILabel.cpp b/src/system/ui/UILabel.cpp
index b130277e..0505fd62 100644
--- a/src/system/ui/UILabel.cpp
+++ b/src/system/ui/UILabel.cpp
@@ -18,6 +18,8 @@
 
 bool UILabel::sDeferUpdate;
 
+UILabel::LabelStyle::~LabelStyle() {}
+
 void UILabel::Load(BinStream &bs) {
     PreLoad(bs);
     PostLoad(bs);
diff --git a/src/system/ui/UILabel.h b/src/system/ui/UILabel.h
index ef604065..6a48477b 100644
--- a/src/system/ui/UILabel.h
+++ b/src/system/ui/UILabel.h
@@ -18,7 +18,7 @@ public:
         ~LabelStyle();
 
         ObjPtr<UIColor> mColorOverride; // 0x0
-        ObjPtr<UILabelDir> unk14; // 0x14
+        ObjDirPtr<UILabelDir> unk14; // 0x14
         int unk28;
     };
     // Hmx::Object
```

### agent-a576df99 — CharBonesSamples.cpp
```diff
diff --git a/src/system/char/CharBonesSamples.cpp b/src/system/char/CharBonesSamples.cpp
index f2efc69e..ffeb4110 100644
--- a/src/system/char/CharBonesSamples.cpp
+++ b/src/system/char/CharBonesSamples.cpp
@@ -1,10 +1,15 @@
 #include "char/CharBonesSamples.h"
 
 #include "CharClip.h"
+#include "math/Mtx.h"
+#include "math/Vec.h"
 #include "obj/Object.h"
 #include "os/Debug.h"
+#include "os/Timer.h"
 #include "utl/MemMgr.h"
 
+BinStream &ReadChunks(BinStream &bs, void *data, int total_len, int max_chunk_size);
+
 CharBonesSamples::CharBonesSamples()
     : mNumSamples(0), mPreviewSample(0), mRawData(nullptr) {}
 
@@ -58,6 +63,70 @@ void CharBonesSamples::ScaleAddSample(CharBones &bones, float f1, int i, float f
     }
 }
 
+void CharBonesSamples::LoadHeader(BinStreamRev &d) {
+    MemFree(mRawData);
+    int numBones;
+    d >> numBones;
+    mBones.resize(numBones);
+    if (d.rev > 0xA) {
+        for (int i = 0; i < numBones; i++) {
+            d >> mBones[i];
+        }
+    } else {
+        for (int i = 0; i < numBones; i++) {
+            d >> mBones[i].name;
+        }
+    }
+
+    if (d.rev > 9) {
+        ReadCounts(d.stream, d.rev > 0xF ? 7 : 10);
+        d >> (int &)mCompression;
+        d >> mNumSamples;
+    } else {
+        int i;
+        if (d.rev > 5) {
+            int count;
+            if (d.rev > 7) {
+                count = 9;
+            } else {
+                count = 6;
+                if (d.rev <= 6)
+                    count = 10;
+            }
+            for (i = 0; i < count; i++) {
+                int tmp;
+                d >> tmp;
+            }
+            d >> (int &)mCompression;
+            d >> mNumSamples;
+        } else {
+            d >> mNumSamples;
+            if (d.rev > 3) {
+                d >> (int &)mCompression;
+            }
+        }
+        for (i = 0; i < 7; i++) {
+            mCounts[i] = 0;
+        }
+        for (i = 0; i < mBones.size(); i++) {
+            mCounts[CharBones::TypeOf(mBones[i].name) + 1]++;
+        }
+        for (i = 1; i < 7; i++) {
+            mCounts[i] += mCounts[i - 1];
+        }
+    }
+
+    if (d.rev > 0xB) {
+        d >> mFrames;
+    } else {
+        mFrames.clear();
+    }
+    RecomputeSizes();
+    mRawData = (char *)MemAlloc(
+        AllocateSize(), "CharBonesSamples.cpp", 0x301, "CharBonesSamples", 0
+    );
+}
+
 void CharBonesSamples::ReadCounts(BinStream &bs, int i2) {
     int i = 0;
     int numTypesToRead = Min(7, i2);
@@ -74,6 +143,74 @@ void CharBonesSamples::ReadCounts(BinStream &bs, int i2) {
     }
 }
 
+void CharBonesSamples::LoadData(BinStreamRev &d) {
+    if (d.rev == 0xE) {
+        bool x;
+        d >> x;
+    }
+    bool cached = d.stream.Cached();
+    if (cached && d.rev > 0xE) {
+        mStart = mRawData;
+        ReadChunks(d.stream, mRawData, AllocateSize(), mTotalSize << 7);
+        return;
+    }
+    for (int i = 0; i < mNumSamples; i++) {
+        mStart = &mRawData[mTotalSize * Min(i, mNumSamples - 1)];
+        if (cached) {
+            d.stream.Read(mStart, mOffsets[TYPE_END] - mOffsets[TYPE_POS]);
+        } else {
+            if (mCompression >= kCompressVects) {
+                short *offset = (short *)(mStart + mOffsets[TYPE_QUAT]);
+                for (short *p = (short *)mStart; p < offset; p += 3) {
+                    d >> p[0] >> p[1] >> p[2];
+                }
+            } else {
+                Vector3 *offset = (Vector3 *)(mStart + mOffsets[TYPE_QUAT]);
+                for (Vector3 *p = (Vector3 *)mStart; p < offset; p++) {
+                    d >> *p;
+                }
+            }
+
+            if (mCompression >= kCompressQuats) {
+                char *offset = mStart + mOffsets[TYPE_ROTX];
+                for (char *p = mStart + mOffsets[TYPE_QUAT]; p < offset; p += 4) {
+                    d >> p[0] >> p[1] >> p[2] >> p[3];
+                }
+            } else if (mCompression != kCompressNone) {
+                short *offset = (short *)(mStart + mOffsets[TYPE_ROTX]);
+                for (short *p = (short *)(mStart + mOffsets[TYPE_QUAT]); p < offset;
+                     p += 4) {
+                    d >> p[0] >> p[1] >> p[2] >> p[3];
+                }
+            } else {
+                Hmx::Quat *offset = (Hmx::Quat *)(mStart + mOffsets[TYPE_ROTX]);
+                for (Hmx::Quat *p = (Hmx::Quat *)(mStart + mOffsets[TYPE_QUAT]);
+                     p < offset; p++) {
+                    d >> *p;
+                }
+            }
+
+            if (mCompression != kCompressNone) {
+                short *offset = (short *)(mStart + mOffsets[TYPE_END]);
+                for (short *p = (short *)(mStart + mOffsets[TYPE_ROTX]); p < offset; p++) {
+                    d >> *p;
+                }
+            } else {
+                float *offset = (float *)(mStart + mOffsets[TYPE_END]);
+                for (float *p = (float *)(mStart + mOffsets[TYPE_ROTX]); p < offset; p++) {
+                    d >> *p;
+                }
+            }
+        }
+
+        if ((i & 0x7F) == 0x7F) {
+            while (d.stream.Eof() == TempEof) {
+                Timer::Sleep(0);
+            }
+        }
+    }
+}
+
 void CharBonesSamples::Set(
     const std::vector<CharBones::Bone> &bones, int i, CharBones::CompressionType ty
 ) {
```

### agent-a558dc64 — CharLipSync.h + CharLipSyncDriver.cpp + CharLipSyncDriver.h
```diff
diff --git a/src/system/char/CharLipSync.h b/src/system/char/CharLipSync.h
index 87dafbdb..9d211a26 100644
--- a/src/system/char/CharLipSync.h
+++ b/src/system/char/CharLipSync.h
@@ -11,6 +11,8 @@
 /** "A full lipsync animation, basically a changing set of weights
     for a set of named visemes.  Sampled at 30hz" */
 class CharLipSync : public Hmx::Object {
+    friend class CharLipSyncDriver;
+
 public:
     class Generator {
     public:
diff --git a/src/system/char/CharLipSyncDriver.cpp b/src/system/char/CharLipSyncDriver.cpp
index 6012d44e..9deb4aa8 100644
--- a/src/system/char/CharLipSyncDriver.cpp
+++ b/src/system/char/CharLipSyncDriver.cpp
@@ -1,13 +1,19 @@
 #include "char/CharLipSyncDriver.h"
 #include "char/Char.h"
+#include "char/CharDriver.h"
 #include "char/CharFaceServo.h"
 #include "char/CharLipSync.h"
 #include "char/CharWeightable.h"
+#include "math/Utl.h"
 #include "obj/Dir.h"
 #include "obj/Object.h"
 #include "obj/Utl.h"
+#include "os/Timer.h"
 #include "rndobj/Poll.h"
 #include "rndobj/Rnd.h"
+#include "utl/Loader.h"
+#include "world/CameraManager.h"
+#include "world/Dir.h"
 
 CharLipSyncDriver::CharLipSyncDriver()
     : mLipSync(this), mClips(this), mBlinkClip(this), mSongOwner(this), mSongOffset(0),
@@ -259,6 +265,301 @@ void CharLipSyncDriver::Highlight() {
     }
 }
 
+void CharLipSyncDriver::UpdatePlayback(
+    CharLipSync::PlayBack *playback, float weight, float songOffset
+) {
+    if (!playback)
+        return;
+
+    float time = TheTaskMgr.Seconds(TaskMgr::kRealTime) + songOffset;
+
+    if (mLoop) {
+        CharLipSync *lipSync = playback->mLipSync;
+        float duration = (float)(lipSync->mFrames - 1) * (1.0f / 30.0f);
+        float epsilon = 0.001f;
+        time = Mod(time, duration - epsilon);
+    }
+
+    if (mAlternateDriver) {
+        time = mAlternateDriver->TopClipFrame();
+    }
+
+    playback->Poll(time);
+
+    int count = (int)playback->mWeights.size();
+    if (count == 0)
+        return;
+
+    for (int i = 0; i < (unsigned int)count; i++) {
+        CharLipSync::PlayBack::Weight &w = playback->mWeights[i];
+        float wt = w.unk1c;
+        if (wt == 0.0f)
+            continue;
+
+        CharClip *clip = w.unk0;
+        if (clip != mBlinkClip) {
+            if (mSongOwner) {
+                wt = 0.0f;
+            } else {
+                wt *= weight;
+            }
+        }
+
+        if (!clip)
+            continue;
+        if (wt == 0.0f)
+            continue;
+
+        MILO_ASSERT(wt >= 0.0f, "weight = %f");
+        if (wt < 0.0f)
+            wt = 0.0f;
+        ScaleAddViseme(clip, wt);
+    }
+}
+
+void CharLipSyncDriver::Poll() {
+    START_AUTO_TIMER("lipsyncdriver");
+
+    if (!mClips)
+        return;
+    if (!mBones)
+        return;
+
+    // Test clip: if there's a test clip in edit mode and it has a relative, use it
+    if (mTestClip) {
+        if (TheLoadMgr.EditMode()) {
+            if (mTestClip->Relative()) {
+                if (mTestWeight >= 0.0f) {
+                    float startBeat = mTestClip->StartBeat();
+                    mBones->ScaleAdd(mTestClip, mTestWeight, startBeat, 0.0f);
+                }
+                return;
+            }
+            return;
+        }
+    }
+
+    float currentTime = TheTaskMgr.Seconds(TaskMgr::kRealTime) * 1000.0f;
+
+    if (unkd0) {
+        unkd4 = currentTime;
+        unkd0 = false;
+    }
+
+    // Blend in/out overrides
+    if (unkc8) {
+        float endTime = unkd4 + unkcc;
+        if (currentTime > endTime) {
+            unkc4 = 1.0f;
+            unkc8 = false;
+        } else {
+            float pct = (currentTime - unkd4) / unkcc;
+            pct = Clamp(0.0f, 1.0f, pct);
+            if (pct > unkc4) {
+                unkc4 = pct;
+            }
+        }
+    } else if (unkc9) {
+        float endTime = unkd4 + unkcc;
+        if (currentTime > endTime) {
+            unkc4 = 0.0f;
+            unkc9 = false;
+        } else {
+            float pct = (currentTime - unkd4) / unkcc;
+            pct = Clamp(0.0f, 1.0f, pct);
+            pct = 1.0f - pct;
+            if (pct < unkc4) {
+                unkc4 = pct;
+            }
+        }
+    }
+
+    // Handle override clip blend transition
+    if (unk11c > 0.0f) {
+        if (unk128) {
+            unk124 = currentTime;
+            unk128 = false;
+        }
+        float endTime = unk124 + unk120;
+        if (currentTime > endTime) {
+            mOverrideClip.CopyRef(unk108);
+            mOverrideWeight = unk11c;
+            unk11c = 0.0f;
+        }
+
+        if (unk11c > 0.0f) {
+            // Blend still in progress - skip to pct section below
+        } else if (mOverrideClip) {
+            float weight = mOverrideWeight * unkc4;
+            if (weight > 0.0f) {
+                ScaleAddViseme(mOverrideClip, weight);
+                if (unkc4 >= 1.0f) {
+                    ApplyBlinks();
+                    return;
+                }
+            }
+        }
+
+        if (unk88) {
+            float pct = (currentTime - unkd4) / unkcc;
+            pct = Clamp(0.0f, 1.0f, pct);
+
+            if (mOverrideClip && mOverrideWeight > 0.0f) {
+                float weight = (1.0f - pct) * mOverrideWeight * unkc4;
+                if (weight < 0.0f) {
+                    if (unkc4 < 0.0f)
+                        TheDebug.Fail(MakeString("mOverallOverrideWeight = %f", unkc4), 0);
+                    if (mOverrideWeight < 0.0f)
+                        TheDebug.Fail(MakeString("mOverrideWeight = %f", mOverrideWeight), 0);
+                    if (pct > 1.0f)
+                        TheDebug.Fail(MakeString("pct = %f", pct), 0);
+                    weight = 0.0f;
+                }
+                ScaleAddViseme(mOverrideClip, weight);
+            }
+
+            if (unk108 && unk11c > 0.0f) {
+                float weight = unk11c * unkc4 * pct;
+                if (weight < 0.0f) {
+                    if (unkc4 < 0.0f)
+                        TheDebug.Fail(MakeString("mOverallOverrideWeight = %f", unkc4), 0);
+                    if (unk11c < 0.0f)
+                        TheDebug.Fail(MakeString("mOverrideWeight = %f", unk11c), 0);
+                    if (pct < 0.0f)
+                        TheDebug.Fail(MakeString("pct = %f", pct), 0);
+                    weight = 0.0f;
+                }
+                ScaleAddViseme(unk108, weight);
+            }
+
+            if (unkc4 >= 1.0f) {
+                ApplyBlinks();
+                return;
+            }
+        }
+    }
+
+    // Main weight factor
+    float mainWeight = 1.0f - unkc4;
+    if (mainWeight <= 0.0f)
+        return;
+
+    // Update main playback with unk88
+    UpdatePlayback(unk88, unk90 * mainWeight, mSongOffset);
+
+    float oneOverThirty = 1.0f / 30.0f;
+
+    // Check for VO lipsync end trigger
+    if (!unk8c) {
+        if (unk88) {
+            CharLipSync *lipSync = unk88->mLipSync;
+            if (lipSync) {
+                int frames = lipSync->mFrames;
+                float duration = (float)(frames - 1) * oneOverThirty;
+                float time = TheTaskMgr.Seconds(TaskMgr::kRealTime);
+                if (time + mSongOffset >= duration) {
+                    const char *name = "";
+                    if (unk88->mLipSync) {
+                        name = unk88->mLipSync->Name();
+                    }
+                    TheDebug << MakeString(
+                        "CharLipSyncDriver::Poll() - Triggering end of VO lipsync fade for: %s\n",
+                        name
+                    );
+                    unk8c = true;
+                }
+            }
+        }
+    }
+
+    // Fade out VO lipsync
+    float fadeThreshold = 0.001f;
+    if (unk8c) {
+        if (unk90 < fadeThreshold) {
+            // Delete the faded playback
+            const char *name = "";
+            if (unk88->mLipSync) {
+                name = unk88->mLipSync->Name();
+            }
+            TheDebug << MakeString(
+                "CharLipSyncDriver::Poll() - Deleting VO lipsync fade after duration exceeded.  Name: %s\n",
+                name
+            );
+            delete unk88;
+            unk88 = nullptr;
+            mLipSync = nullptr;
+            unk90 = 1.0f;
+            unk8c = false;
+        } else {
+            unk90 *= 0.99f;
+        }
+    }
+
+    // Check if in battle mode
+    bool isBattle = false;
+    if (TheWorld) {
+        CameraManager *camMgr = TheWorld->GetCameraManager();
+        if (camMgr) {
+            CamShot *shot = camMgr->CurrentShot();
+            if (!shot) {
+                shot = camMgr->MiloCamera();
+            }
+            if (unk88 && unk88->mLipSync && shot) {
+                const char *shotName = shot->Name();
+                if (shotName) {
+                    if (strncmp(shotName, "battle_", 7) == 0) {
+                        isBattle = true;
+                    }
+                }
+            }
+        }
+    }
+
+    // Update unk94 (VO playback) if not in battle
+    if (!isBattle) {
+        UpdatePlayback(unk94, mainWeight, 0.0f);
+    }
+
+    // Process song owner's playback
+    CharLipSyncDriver *songOwner = mSongOwner;
+    if (songOwner) {
+        CharLipSync::PlayBack *songPlayback = songOwner->unk88;
+        if (songPlayback) {
+            float songTime = TheTaskMgr.Seconds(TaskMgr::kRealTime);
+            float songOffset = songOwner->mSongOffset;
+            float time = songTime + songOffset;
+
+            if (mLoop) {
+                CharLipSync *lipSync = mSongOwner->unk88->mLipSync;
+                int frames = lipSync->mFrames;
+                float duration = (float)(frames - 1) * oneOverThirty;
+                float modTime = duration - fadeThreshold;
+                time = Mod(time, modTime);
+            }
+
+            mSongOwner->unk88->Poll(time);
+
+            // Iterate weights from song owner's playback
+            CharLipSync::PlayBack *ownerPlayback = mSongOwner->unk88;
+            for (int i = 0; i < (int)ownerPlayback->mWeights.size(); i++) {
+                CharLipSync::PlayBack::Weight &w = ownerPlayback->mWeights[i];
+                float wt = w.unk1c * mainWeight;
+                CharClip *clip = w.unk0;
+                if (wt != 0.0f && clip) {
+                    if (clip != mSongOwner->mBlinkClip) {
+                        CharClip *localClip =
+                            mClips->Find<CharClip>(clip->Name(), true);
+                        ScaleAddViseme(localClip, wt);
+                    }
+                }
+            }
+        }
+    }
+
+    ApplyBlinks();
+    return;
+}
+
 void CharLipSyncDriver::ScaleAddViseme(CharClip *clip, float f1) {
     float length;
     float dVar2;
diff --git a/src/system/char/CharLipSyncDriver.h b/src/system/char/CharLipSyncDriver.h
index 5881e89d..23777b8b 100644
--- a/src/system/char/CharLipSyncDriver.h
+++ b/src/system/char/CharLipSyncDriver.h
@@ -53,6 +53,7 @@ protected:
     CharLipSyncDriver();
 
     void ApplyBlinks();
+    void UpdatePlayback(CharLipSync::PlayBack *, float, float);
 
     /** "The lipsync file to use" */
     ObjPtr<CharLipSync> mLipSync; // 0x30
```

### agent-aaf2209c — Flow system
```diff
diff --git a/src/system/flow/Flow.cpp b/src/system/flow/Flow.cpp
index 47c7a969..d9b7eaa6 100644
--- a/src/system/flow/Flow.cpp
+++ b/src/system/flow/Flow.cpp
@@ -384,6 +384,26 @@ FlowLabel *Flow::GetLabelForSym(Symbol sym) {
     return nullptr;
 }
 
+void Flow::Enter() {
+    if (ProxyFile().empty() && unk170 != 0) {
+        if (unk170 == 1) {
+            Execute(kQueue);
+        } else {
+            TheFlowMgr->QueueCommand(this, kQueue);
+        }
+    }
+}
+
+void Flow::Exit() {
+    if (IsRunning() && ProxyFile().empty()) {
+        if (mHardStop) {
+            Deactivate(false);
+        } else {
+            RequestStop();
+        }
+    }
+}
+
 void ScanForOutPorts(ObjPtrVec<FlowOutPort> &, FlowNode *, Flow *);
 
 void Flow::RefreshPortLabelLists() {
diff --git a/src/system/flow/FlowPickOne.cpp b/src/system/flow/FlowPickOne.cpp
index 5d813620..8662a3fb 100644
--- a/src/system/flow/FlowPickOne.cpp
+++ b/src/system/flow/FlowPickOne.cpp
@@ -1,6 +1,13 @@
 #include "flow/FlowPickOne.h"
+#include "flow/DrivenPropertyEntry.h"
 #include "flow/FlowNode.h"
+#include "math/Rand.h"
 #include "obj/Object.h"
+#include "os/Debug.h"
+#include "utl/MakeString.h"
+#include <algorithm>
+#include <stdlib.h>
+#include <vector>
 
 FlowPickOne::FlowPickOne()
     : unk5c(this), mChoiceType(kChoiceRandom), mIndex(0), mChance(1) {}
@@ -44,3 +51,110 @@ BEGIN_LOADS(FlowPickOne)
         d >> mChance;
     }
 END_LOADS
+
+bool FlowPickOne::Activate() {
+    FLOW_LOG("Activate\n");
+    unk58 = false;
+    PushDrivenProperties();
+
+    if (mChance != 1.0f) {
+        int roll = rand() % 100;
+        if (mChance * 100.0f < (float)roll) {
+            return false;
+        }
+    }
+
+    if (mChildNodes.empty()) {
+        return false;
+    }
+
+    switch (mChoiceType) {
+    case kChoiceOrdered: {
+        if (mIndex < 0 || mChildNodes.size() <= mIndex) {
+            mIndex = 0;
+        }
+        ActivateChild(mChildNodes[mIndex]);
+        mIndex++;
+        break;
+    }
+    case kChoiceRandom: {
+        mIndex = RandomInt(0, mChildNodes.size());
+        ActivateChild(mChildNodes[mIndex]);
+        break;
+    }
+    case kChoiceRandomNoRepeat: {
+        if (mChildNodes.size() < 2) {
+            mIndex = 0;
+        } else {
+            int pick;
+            do {
+                pick = RandomInt(0, mChildNodes.size());
+            } while (pick == mIndex);
+            mIndex = pick;
+        }
+        ActivateChild(mChildNodes[mIndex]);
+        break;
+    }
+    case kChoiceRandomJukeBox: {
+        int numChildren = mChildNodes.size();
+        if (numChildren > 1) {
+            if (mIndex < 0 || unk5c.size() <= mIndex) {
+                FlowNode *lastPlayed = 0;
+                if (!unk5c.empty()) {
+                    lastPlayed = unk5c[unk5c.size() - 1];
+                }
+                unk5c.clear();
+                std::vector<FlowNode *> temp;
+                for (ObjPtrVec<FlowNode>::iterator it = mChildNodes.begin();
+                     it != mChildNodes.end();
+                     ++it) {
+                    temp.push_back((*it).Obj());
+                }
+                std::random_shuffle(temp.begin(), temp.end());
+                int count = temp.size();
+                while (count > 0) {
+                    count--;
+                    unk5c.push_back(temp[count]);
+                }
+                mIndex = 0;
+                if (lastPlayed == unk5c[0]) {
+                    mIndex = 1;
+                }
+            }
+            ActivateChild(unk5c[mIndex]);
+            mIndex++;
+        } else if (numChildren == 1) {
+            ActivateChild(mChildNodes[0]);
+        }
+        break;
+    }
+    case kChoiceUseIndex: {
+        int idx = mIndex % mChildNodes.size();
+        mIndex = idx;
+        ObjPtrVec<FlowNode>::iterator it = mChildNodes.begin();
+        for (int i = 0; i < mIndex; i++) {
+            ++it;
+        }
+        ActivateChild((*it).Obj());
+        break;
+    }
+    default:
+        MILO_NOTIFY_ONCE("FlowPickOne: bad picking type");
+        break;
+    }
+
+    return !mRunningNodes.empty();
+}
+
+void FlowPickOne::OnChoiceTypeChanged() {
+    if (mChoiceType != kChoiceUseIndex) {
+        for (ObjVector<DrivenPropertyEntry>::iterator it = mDrivenPropEntries.begin();
+             it != mDrivenPropEntries.end();
+             ++it) {
+            if (it->Node().Array()->Sym(0) == "index") {
+                mDrivenPropEntries.erase(it);
+                return;
+            }
+        }
+    }
+}
diff --git a/src/system/flow/FlowQueueable.cpp b/src/system/flow/FlowQueueable.cpp
index f2f6467c..0a3ed24b 100644
--- a/src/system/flow/FlowQueueable.cpp
+++ b/src/system/flow/FlowQueueable.cpp
@@ -2,6 +2,8 @@
 #include "flow/FlowNode.h"
 #include "obj/Msg.h"
 #include "obj/Object.h"
+#include "os/Debug.h"
+#include "utl/MakeString.h"
 #include <list>
 
 FlowQueueable::FlowQueueable() : mInterrupt(kImmediate) {}
@@ -45,6 +47,131 @@ void FlowQueueable::ReleaseListener(Hmx::Object *obj) {
     }
 }
 
+void FlowQueueable::Deactivate(bool b) {
+    std::list<Hmx::Object *> local(unk60);
+    unk60.clear();
+    while (local.size() != 0) {
+        ReleaseListener(local.front());
+        local.erase(local.begin());
+    }
+    FlowNode::Deactivate(b);
+}
+
+void FlowQueueable::ChildFinished(FlowNode *node) {
+    FLOW_LOG("Child Finished of class:%s\n", node->ClassName());
+    if ((int)mInterrupt == 5) {
+        FlowNode::ChildFinished(node);
+        return;
+    }
+    mRunningNodes.remove(node);
+    if (mRunningNodes.empty()) {
+        if (unk58) {
+            std::list<Hmx::Object *> local(unk60);
+            unk60.clear();
+            while (local.size() != 0) {
+                ReleaseListener(local.front());
+                local.erase(local.begin());
+            }
+            if (mFlowParent && mRunningNodes.empty()
+                && mFlowParent->HasRunningNode(this)) {
+                FLOW_LOG("Releasing\n");
+                mFlowParent->ChildFinished(this);
+            }
+        } else {
+            if (unk60.size() > 1) {
+                Hmx::Object *front = unk60.front();
+                bool duplicate = false;
+                std::list<Hmx::Object *>::iterator it = unk60.begin();
+                ++it;
+                for (; it != unk60.end(); ++it) {
+                    if (*it == front) {
+                        duplicate = true;
+                        break;
+                    }
+                }
+                if (!duplicate) {
+                    ReleaseListener(front);
+                }
+                unk60.erase(unk60.begin());
+                ActivateTrigger();
+            } else if (!unk60.empty()) {
+                ReleaseListener(unk60.front());
+                unk60.erase(unk60.begin());
+            }
+        }
+    }
+}
+
+bool FlowQueueable::Activate(Hmx::Object *obj) {
+    FLOW_LOG("Activate\n");
+    unk58 = false;
+    if (mRunningNodes.empty()) {
+        if (obj == nullptr) {
+            unk60.push_back(nullptr);
+        } else {
+            unk60.push_back(obj);
+        }
+        if (ActivateTrigger()) {
+            return true;
+        }
+        unk60.clear();
+        return false;
+    }
+    switch (mInterrupt) {
+    case kIgnore:
+        FLOW_LOG("Ignoring trigger\n");
+        ReleaseListener(obj);
+        return false;
+    case kQueue:
+        FLOW_LOG("Queueing trigger\n");
+        if (obj == nullptr) {
+            unk60.push_back(nullptr);
+        } else {
+            unk60.push_back(obj);
+        }
+        break;
+    case kQueueOne:
+        FLOW_LOG("Queue One\n");
+        while (unk60.size() > 1) {
+            ReleaseListener(unk60.front());
+            unk60.erase(unk60.begin());
+        }
+        if (obj == nullptr) {
+            unk60.push_back(nullptr);
+        } else {
+            unk60.push_back(obj);
+        }
+        break;
+    case kImmediate:
+        FLOW_LOG("Immediate Interrupt\n");
+        Deactivate(false);
+        if (obj == nullptr) {
+            unk60.push_back(nullptr);
+        } else {
+            unk60.push_back(obj);
+        }
+        ActivateTrigger();
+        return mRunningNodes.empty() ? false : true;
+    case kWhenAble:
+        FLOW_LOG("When Able Interruption\n");
+        while (unk60.size() > 1) {
+            ReleaseListener(unk60.front());
+            unk60.erase(unk60.begin());
+        }
+        if (obj == nullptr) {
+            unk60.push_back(nullptr);
+        } else {
+            unk60.push_back(obj);
+        }
+        RequestStop();
+        return true;
+    default:
+        MILO_NOTIFY_ONCE("FlowQueueable: bad interupt value");
+        return false;
+    }
+    return true;
+}
+
 void FlowQueueable::RequestStopCancel() {
     if (!unk58)
         return;
diff --git a/src/system/flow/FlowSequence.cpp b/src/system/flow/FlowSequence.cpp
index 0500cd74..0a59ea40 100644
--- a/src/system/flow/FlowSequence.cpp
+++ b/src/system/flow/FlowSequence.cpp
@@ -2,6 +2,7 @@
 #include "flow/FlowNode.h"
 #include "obj/Object.h"
 #include "os/Debug.h"
+#include "utl/MakeString.h"
 
 FlowSequence::FlowSequence()
     : mItr(nullptr), mLooping(0), mRepeats(0), unk68(0), mStopMode(kStopImmediate),
@@ -50,6 +51,48 @@ BEGIN_LOADS(FlowSequence)
         bs >> (int &)mStopMode;
 END_LOADS
 
+bool FlowSequence::Activate() {
+    FLOW_LOG("Activate\n");
+    unk58 = false;
+    if (IsRunning()) {
+        MILO_NOTIFY(
+            MakeString(
+                "FlowSequence re-entrance error, deactivating %s", FindPathName()
+            )
+        );
+        Deactivate(false);
+        return false;
+    }
+    if (unk68 == 0) {
+        PushDrivenProperties();
+    }
+    unk68 = 0;
+    mItr = mChildNodes.begin();
+    unk70 = true;
+    while (mItr != mChildNodes.end()) {
+        ActivateChild(mItr->Obj());
+        if (unk58 || !mRunningNodes.empty())
+            break;
+        ++mItr;
+    }
+    unk70 = false;
+    if (unk58 || !mRunningNodes.empty()) {
+        if (mItr != mChildNodes.end())
+            return true;
+    }
+    MILO_ASSERT(mRunningNodes.size() < 2, 0x50);
+    if (mItr != mChildNodes.end())
+        return true;
+    if (mRunningNodes.size() != 0)
+        return true;
+    if (!mLooping) {
+        if (mRepeats == 0)
+            return false;
+    }
+    MILO_NOTIFY_ONCE("Instant looping sequence in %s! ", FindPathName());
+    return mRunningNodes.size() > 0;
+}
+
 void FlowSequence::ChildFinished(FlowNode *node) {
     FLOW_LOG(
         "Child Finished of class:%s ; potential advance of iterator\n", node->ClassName()
diff --git a/src/system/flow/FlowSwitchCase.cpp b/src/system/flow/FlowSwitchCase.cpp
index 792d0c26..98c8dc39 100644
--- a/src/system/flow/FlowSwitchCase.cpp
+++ b/src/system/flow/FlowSwitchCase.cpp
@@ -139,6 +139,107 @@ bool FlowSwitchCase::IsRunning() {
         return FlowNode::IsRunning();
 }
 
+bool FlowSwitchCase::IsValidCase(
+    FlowNode *node, DataNode *curValue, const DataNode *lastValue, bool hasLast
+) {
+    PushDrivenProperties();
+    bool useLastVal = mUseLastValue;
+
+    if (mOperator == kTransition) {
+        if (useLastVal) {
+            mFromValue = *lastValue;
+        }
+        // Check that curValue matches toValue type and lastValue matches fromValue type
+        DataNode toNode = mToValue.Node();
+        bool matchesTo = (curValue->Type() == toNode.Type());
+        bool typesDontMatch = false;
+        if (matchesTo) {
+            DataNode fromNode = mFromValue.Node();
+            if (curValue->Type() != fromNode.Type()) {
+                typesDontMatch = true;
+            }
+        } else {
+            typesDontMatch = true;
+        }
+        if (typesDontMatch) {
+            return false;
+        }
+        // Check toValue equals curValue and fromValue equals lastValue
+        DataNode toNode2 = mToValue.Node();
+        bool toEquals = curValue->Equal(toNode2, nullptr, true);
+        bool result = false;
+        if (toEquals) {
+            DataNode fromNode2 = mFromValue.Node();
+            result = true;
+            if (!lastValue->Equal(fromNode2, nullptr, true)) {
+                result = false;
+            }
+        }
+        return result;
+    }
+
+    // Non-transition operators
+    if (useLastVal) {
+        mToValue = *lastValue;
+    }
+    bool result;
+    switch (mOperator) {
+    case kEqual: {
+        DataNode toNode = mToValue.Node();
+        result = curValue->Equal(toNode, nullptr, true);
+        break;
+    }
+    case kNotEqual: {
+        DataNode toNode = mToValue.Node();
+        result = *curValue != toNode;
+        break;
+    }
+    case kGreaterThan: {
+        DataNode toNode = mToValue.Node();
+        if ((curValue->Type() == kDataInt || curValue->Type() == kDataFloat)
+            && (toNode.Type() == kDataInt || toNode.Type() == kDataFloat)) {
+            result = curValue->LiteralFloat(nullptr) > toNode.LiteralFloat(nullptr);
+        } else {
+            result = false;
+        }
+        break;
+    }
+    case kGreaterThanOrEqual: {
+        DataNode toNode = mToValue.Node();
+        if ((curValue->Type() == kDataInt || curValue->Type() == kDataFloat)
+            && (toNode.Type() == kDataInt || toNode.Type() == kDataFloat)) {
+            result = curValue->LiteralFloat(nullptr) >= toNode.LiteralFloat(nullptr);
+        } else {
+            result = false;
+        }
+        break;
+    }
+    case kLessThan: {
+        DataNode toNode = mToValue.Node();
+        if ((curValue->Type() == kDataInt || curValue->Type() == kDataFloat)
+            && (toNode.Type() == kDataInt || toNode.Type() == kDataFloat)) {
+            result = curValue->LiteralFloat(nullptr) < toNode.LiteralFloat(nullptr);
+        } else {
+            result = false;
+        }
+        break;
+    }
+    case kLessThanOrEqual: {
+        DataNode toNode = mToValue.Node();
+        if ((curValue->Type() == kDataInt || curValue->Type() == kDataFloat)
+            && (toNode.Type() == kDataInt || toNode.Type() == kDataFloat)) {
+            result = curValue->LiteralFloat(nullptr) <= toNode.LiteralFloat(nullptr);
+        } else {
+            result = false;
+        }
+        break;
+    }
+    default:
+        return false;
+    }
+    return result;
+}
+
 void FlowSwitchCase::UseLastValueChanged() {
     if (mUseLastValue) {
         DrivenPropertyEntry *entry = GetDrivenEntry("to_value");
diff --git a/src/system/flow/FlowTimer.cpp b/src/system/flow/FlowTimer.cpp
index c0710f02..2a0d01a5 100644
--- a/src/system/flow/FlowTimer.cpp
+++ b/src/system/flow/FlowTimer.cpp
@@ -2,8 +2,11 @@
 #include "flow/Flow.h"
 #include "flow/FlowManager.h"
 #include "flow/FlowNode.h"
+#include "flow/FlowValueCase.h"
 #include "obj/Object.h"
+#include "obj/Task.h"
 #include "os/Debug.h"
+#include "utl/MakeString.h"
 
 FlowTimer::FlowTimer() : unk5c(0), unk60(this), mRate(0), mTotalTime(0.0f) {}
 
@@ -108,3 +111,33 @@ void FlowTimer::OnTimerEnd() {
 BEGIN_HANDLERS(FlowTimer)
     HANDLE_SUPERCLASS(FlowNode)
 END_HANDLERS
+
+EventTask::EventTask(
+    FlowTimer *timer, ObjPtrVec<FlowNode> *nodes, TaskUnits units, float endTime
+)
+    : mTimer(this, nullptr), mNodes(nodes), mItr(nullptr), mEndTime(endTime) {
+    mTimer = timer;
+    mItr = mNodes->begin();
+    TheTaskMgr.Start(this, units, 0.0f);
+}
+
+EventTask::~EventTask() {}
+
+void EventTask::Poll(float time) {
+    if (!mTimer) {
+        MILO_NOTIFY("EventTask::Poll NULL mOwner");
+        return;
+    }
+    while (mItr != mNodes->end()) {
+        FlowValueCase *node = static_cast<FlowValueCase *>(mItr->Obj());
+        if (time < node->mValue) {
+            break;
+        }
+        mTimer->OnKeyframe(node);
+        ++mItr;
+    }
+    if (time >= mEndTime) {
+        mTimer->OnTimerEnd();
+        delete this;
+    }
+}
diff --git a/src/system/flow/FlowTimer.h b/src/system/flow/FlowTimer.h
index 9934e49d..cd40f7fb 100644
--- a/src/system/flow/FlowTimer.h
+++ b/src/system/flow/FlowTimer.h
@@ -6,6 +6,21 @@
 #include "obj/Task.h"
 #include "utl/BinStream.h"
 
+class FlowTimer;
+
+class EventTask : public Task {
+public:
+    EventTask(FlowTimer *, ObjPtrVec<FlowNode> *, TaskUnits, float);
+    virtual ~EventTask();
+    OBJ_CLASSNAME(EventTask)
+    virtual void Poll(float);
+
+    ObjPtr<FlowTimer> mTimer; // 0x2c
+    ObjPtrVec<FlowNode> *mNodes; // 0x40
+    ObjPtrVec<FlowNode>::iterator mItr; // 0x44
+    float mEndTime; // 0x48
+};
+
 class FlowTimer : public FlowNode {
 public:
     // Hmx::Object
diff --git a/src/system/flow/FlowValueCase.h b/src/system/flow/FlowValueCase.h
index 16ab4181..acafc04a 100644
--- a/src/system/flow/FlowValueCase.h
+++ b/src/system/flow/FlowValueCase.h
@@ -20,6 +20,8 @@ public:
 protected:
     FlowValueCase();
 
+    friend class EventTask;
+
     /** "Key frame value" */
     float mValue; // 0x5c
 };
```

### agent-ad10d142 — World/Env/Rndobj
```diff
diff --git a/src/system/rndobj/Env.h b/src/system/rndobj/Env.h
index 6bd356da..9535163f 100644
--- a/src/system/rndobj/Env.h
+++ b/src/system/rndobj/Env.h
@@ -61,6 +61,18 @@ public:
     void SetAmbientColor(const Hmx::Color &col) {
         mAmbientFogOwner->mAmbientColor.Set(col.red, col.green, col.blue);
     }
+    const Hmx::Color &FogColor() const { return mAmbientFogOwner->mFogColor; }
+    void SetFogColor(const Hmx::Color &col) {
+        mAmbientFogOwner->mFogColor.Set(col.red, col.green, col.blue);
+    }
+    float GetFogStart() const { return mAmbientFogOwner->mFogStart; }
+    float GetFogEnd() const { return mAmbientFogOwner->mFogEnd; }
+    void SetFogRange(float start, float end) {
+        mAmbientFogOwner->mFogStart = start;
+        mAmbientFogOwner->mFogEnd = end;
+    }
+    void SetFogEnable(bool b) { mAmbientFogOwner->mFogEnable = b; }
+    bool GetAnimateFromPreset() const { return mAnimateFromPreset; }
     bool FadeOut() const { return mFadeOut; }
     bool UseColorAdjust() const { return mUseColorAdjust; }
     float FadeStart() const { return mFadeStart; }
diff --git a/src/system/rndobj/Lit.h b/src/system/rndobj/Lit.h
index 5a64ce36..84519af8 100644
--- a/src/system/rndobj/Lit.h
+++ b/src/system/rndobj/Lit.h
@@ -41,13 +41,14 @@ public:
     void SetShowing(bool b) { mShowing = b; }
     float Intensity() const;
     void SetProjectedBlend(int i) { mProjectedBlend = i; }
-    // bool GetAnimateFromPreset() const {
-    //     return mAnimateColorFromPreset || mAnimatePositionFromPreset
-    //         || mAnimateRangeFromPreset;
-    // }
+    bool GetAnimateFromPreset() const {
+        return mAnimateColorFromPreset || mAnimatePositionFromPreset
+            || mAnimateRangeFromPreset;
+    }
     bool Showing() const { return mShowing; }
-    // bool AnimatePosFromPreset() const { return mAnimatePositionFromPreset; }
-    // bool AnimateRangeFromPreset() const { return mAnimateRangeFromPreset; }
+    bool AnimateColorFromPreset() const { return mAnimateColorFromPreset; }
+    bool AnimatePosFromPreset() const { return mAnimatePositionFromPreset; }
+    bool AnimateRangeFromPreset() const { return mAnimateRangeFromPreset; }
 
     Transform Projection();
 
diff --git a/src/system/rndobj/MultiMesh.h b/src/system/rndobj/MultiMesh.h
index 0872957f..e2cb6dfb 100644
--- a/src/system/rndobj/MultiMesh.h
+++ b/src/system/rndobj/MultiMesh.h
@@ -109,6 +109,11 @@ public:
     virtual float GetDistanceToPlane(const Plane &, Vector3 &);
     virtual bool MakeWorldSphere(Sphere &, bool);
     virtual void Mats(std::list<class RndMat *> &, bool);
+    virtual void UpdateSphere() {
+        Sphere s;
+        MakeWorldSphere(s, true);
+        SetSphere(s);
+    }
     virtual void DrawShowing();
     virtual void ListDrawChildren(std::list<RndDrawable *> &);
     virtual void CollideList(const Segment &, std::list<Collision> &);
diff --git a/src/system/ui/UI.h b/src/system/ui/UI.h
index b4c03c00..2b5c5d8e 100644
--- a/src/system/ui/UI.h
+++ b/src/system/ui/UI.h
@@ -56,6 +56,7 @@ public:
     bool OverloadHorizontalNav(JoypadAction, JoypadButton, bool) const;
     bool IsGameScreenActive();
     bool DefaultAllowEditText() const { return mDefaultAllowEditText; }
+    RndEnviron *GetEnv() { return mEnv; }
 
 private:
     void ToggleLoadTimes();
diff --git a/src/system/world/CameraShot.h b/src/system/world/CameraShot.h
index 2229e93a..19a9586b 100644
--- a/src/system/world/CameraShot.h
+++ b/src/system/world/CameraShot.h
@@ -186,6 +186,9 @@ public:
     RndCam *GetCam();
     void SetParent(RndDir *d) { unk1a4 = d; }
     bool ShotOver() const { return mShotOver; }
+    ObjPtrList<RndDrawable> &DrawOverrides() { return mDrawOverrides; }
+    ObjPtrList<RndDrawable> &PostProcOverrides() { return mPostProcOverrides; }
+    Spotlight *GlowSpot() const { return mGlowSpot; }
     class WorldDir *GetCrowdDir() const;
     void AddAnim(RndAnimatable *);
     void ClearCrowds();
diff --git a/src/system/world/Dir.cpp b/src/system/world/Dir.cpp
index ba1d58e5..4e64f866 100644
--- a/src/system/world/Dir.cpp
+++ b/src/system/world/Dir.cpp
@@ -11,6 +11,7 @@
 #include "rndobj/BaseMaterial.h"
 #include "rndobj/Cam.h"
 #include "rndobj/Dir.h"
+#include "rndobj/Graph.h"
 #include "rndobj/Mat.h"
 #include "rndobj/PostProc.h"
 #include "rndobj/Rnd.h"
@@ -18,6 +19,7 @@
 #include "rndobj/Utl.h"
 #include "synth/FxSend.h"
 #include "ui/PanelDir.h"
+#include "ui/UI.h"
 #include "utl/BinStream.h"
 #include "world/CameraManager.h"
 
@@ -567,4 +569,71 @@ void WorldDir::SyncCamShots(bool b) {
     }
 }
 
+void WorldDir::DrawShowing() {
+    START_AUTO_TIMER("world_draw");
+    if (TheWorld) {
+        MILO_ASSERT(TheWorld != this, 0x25C);
+        if (Showing())
+            RndDir::DrawShowing();
+    } else {
+        SetTheWorld(this);
+        CamShot *shot = 0;
+        CameraManager *cameraMgr = mCameraMgr;
+        if (cameraMgr) {
+            shot = cameraMgr->MiloCamera();
+            if (!shot)
+                shot = cameraMgr->CurrentShot();
+        }
+        if (shot)
+            shot = shot->CurrentShot();
+        RndCam *cam = CamOverride();
+        RndCam *saveCam = RndCam::Current();
+        if (cam) {
+            cam->Select();
+            saveCam = cam;
+        }
+        RndEnviron *env = GetEnv();
+        if (!env)
+            env = TheUI->GetEnv();
+        env->Select(0);
+        if (TheRnd.ProcCmds() & kProcessWorld && shot && !shot->DrawOverrides().empty()) {
+            FOREACH (it, shot->DrawOverrides()) {
+                (*it)->DrawShowing();
+            }
+        } else {
+            RndDir::DrawShowing();
+        }
+        if (shot) {
+            Spotlight *spot = shot->GlowSpot();
+            if (spot && sGlowMat && spot->Showing() && spot->Intensity() > 0) {
+                Hmx::Rect rect(0, 0, TheRnd.Width(), TheRnd.Height());
+                Hmx::Color color(spot->Color());
+                color.alpha = 0.25f;
+                TheRnd.DrawRect(rect, color, sGlowMat, 0, 0);
+            }
+        }
+        TheRnd.CopyWorldCam(TheWorld->Cam());
+        if (mExplicitPostProc)
+            TheRnd.EndWorld();
+        if (shot) {
+            saveCam->Select();
+            env->Select(0);
+            FOREACH (it, shot->PostProcOverrides()) {
+                (*it)->DrawShowing();
+            }
+        }
+        RndGraph::SetCamera(RndCam::Current());
+        if (mHUDDir)
+            mHUDDir->DrawShowing();
+        if (mHUD && mHUD->Showing()) {
+            START_AUTO_TIMER("hud_draw");
+            mHUD->DrawShowing();
+        }
+        if (TheRnd.ProcCmds() & kProcessPost && SpotlightDrawer::Current()) {
+            SpotlightDrawer::Current()->DeSelect();
+        }
+        SetTheWorld(0);
+    }
+}
+
 DataNode WorldDir::OnGetPhysicsManager(const DataArray *) { return mPhysicsMgr; }
diff --git a/src/system/world/FreeCamera.cpp b/src/system/world/FreeCamera.cpp
index ae5fa900..25253bdb 100644
--- a/src/system/world/FreeCamera.cpp
+++ b/src/system/world/FreeCamera.cpp
@@ -1,8 +1,9 @@
 #include "world/FreeCamera.h"
-#include "obj/Object.h"
 #include "math/Rot.h"
+#include "obj/Object.h"
 #include "rndobj/Cam.h"
 #include "rndobj/DOFProc.h"
+#include "world/Dir.h"
 
 float gUnitsPerMeter = 39.370079;
 
@@ -31,11 +32,15 @@ void FreeCamera::SetParentDof(bool b1, bool b2, bool b3) {
     mUseParentRotateZ = b3;
 }
 
-// void FreeCamera::UpdateFromCamera() {
-//     RndCam *cam = mWorld->GetCam();
-//     mFov = cam->YFov();
-//     mXfm = cam->WorldXfm();
-//     MakeEuler(mXfm.m, mRot);
-//     mParent = 0;
-//     mFocalPlane = TheDOFProc->FocalPlane();
-// }
+void FreeCamera::UpdateFromCamera() {
+    RndCam *cam = mWorld->Cam();
+    mFov = cam->YFov();
+    mXfm = cam->WorldXfm();
+    MakeEuler(mXfm.m, mRot);
+    mParent = 0;
+    mFocalPlane = TheDOFProc->FocalPlane();
+}
+
+// FreeCamera::Poll - large DC3-specific gamepad camera controller
+// Needs: JoypadGetPadData, DeltaUISeconds, MakeRotMatrix, Multiply, MakeEuler
+//        WorldXfm_Force, LimitAng, pow, memcpy, SetFrustum, SetDirty_Force
diff --git a/src/system/world/LightPreset.cpp b/src/system/world/LightPreset.cpp
index e62e2ab4..feb641a3 100644
--- a/src/system/world/LightPreset.cpp
+++ b/src/system/world/LightPreset.cpp
@@ -2,14 +2,17 @@
 #include "LightPreset.h"
 #include "SpotlightDrawer.h"
 #include "math/Mtx.h"
+#include "math/Rot.h"
 #include "obj/Msg.h"
 #include "obj/Object.h"
 #include "os/Debug.h"
 #include "rndobj/Anim.h"
+#include "rndobj/Cam.h"
 #include "rndobj/Env.h"
 #include "rndobj/PostProc.h"
 #include "utl/BinStream.h"
 #include "utl/Loader.h"
+#include <float.h>
 
 LightPreset *gEditPreset;
 std::deque<std::pair<LightPreset::KeyframeCmd, float> > LightPreset::sManualEvents;
@@ -59,6 +62,23 @@ bool LightPreset::EnvironmentEntry::operator!=(const LightPreset::EnvironmentEnt
         return mFogColor != e.mFogColor;
 }
 
+void LightPreset::EnvironmentEntry::Animate(
+    const LightPreset::EnvironmentEntry &entry, float f2
+) {
+    Interp(mAmbientColor, entry.mAmbientColor, f2, mAmbientColor);
+    if (entry.mFogEnable) {
+        Interp(mFogColor, entry.mFogColor, f2, mFogColor);
+        Interp(mFogStart, entry.mFogStart, f2, mFogStart);
+        Interp(mFogEnd, entry.mFogEnd, f2, mFogEnd);
+    } else {
+        float far = RndCam::Current() ? RndCam::Current()->FarPlane() : FLT_MAX;
+        Interp(mFogStart, far, f2, mFogStart);
+        Interp(mFogEnd, far, f2, mFogEnd);
+    }
+    if (f2 == 1)
+        mFogEnable = entry.mFogEnable;
+}
+
 BinStream &operator<<(BinStream &bs, const LightPreset::EnvironmentEntry &e) {
     e.Save(bs);
     return bs;
@@ -109,6 +129,15 @@ bool LightPreset::EnvLightEntry::operator!=(const LightPreset::EnvLightEntry &e)
         return mColor != e.mColor;
 }
 
+void LightPreset::EnvLightEntry::Animate(
+    const LightPreset::EnvLightEntry &entry, float f2
+) {
+    Interp(mColor, entry.mColor, f2, mColor);
+    Interp(mRange, entry.mRange, f2, mRange);
+    Interp(unk0, entry.unk0, f2, unk0);
+    Interp(mPosition, entry.mPosition, f2, mPosition);
+}
+
 BinStream &operator<<(BinStream &bs, const LightPreset::EnvLightEntry &e) {
     e.Save(bs);
     return bs;
@@ -210,6 +239,15 @@ void LightPreset::SpotlightDrawerEntry::Load(BinStreamRev &d) {
     }
 }
 
+void LightPreset::SpotlightDrawerEntry::Animate(
+    const LightPreset::SpotlightDrawerEntry &e, float f
+) {
+    Interp(mBaseIntensity, e.mBaseIntensity, f, mBaseIntensity);
+    Interp(mSmokeIntensity, e.mSmokeIntensity, f, mSmokeIntensity);
+    Interp(mLightInfluence, e.mLightInfluence, f, mLightInfluence);
+    Interp(mTotalIntensity, e.mTotalIntensity, f, mTotalIntensity);
+}
+
 bool LightPreset::SpotlightDrawerEntry::operator!=(
     const LightPreset::SpotlightDrawerEntry &e
 ) const {
@@ -689,6 +727,205 @@ void LightPreset::SetSpotlight(Spotlight *s, int data) {
     }
 }
 
+void LightPreset::AnimateLightFromPreset(
+    RndLight *light, const LightPreset::EnvLightEntry &entry, float f3
+) {
+    if (light->Showing()) {
+        Hmx::Color c;
+        Interp(light->GetColor(), entry.mColor, f3, c);
+        light->SetColor(c);
+    }
+    if (light->AnimateRangeFromPreset()) {
+        float range;
+        Interp(light->Range(), entry.mRange, f3, range);
+        light->SetRange(range);
+    }
+    if (light->AnimatePosFromPreset()) {
+        Hmx::Matrix3 m;
+        MakeRotMatrix(entry.unk0, m);
+        Transform t;
+        Interp(light->WorldXfm().v, entry.mPosition, f3, t.v);
+        Interp(light->WorldXfm().m, m, f3, t.m);
+        light->SetWorldXfm(t);
+    }
+}
+
+void LightPreset::FillEnvPresetData(RndEnviron *env, LightPreset::EnvironmentEntry &e) {
+    e.mAmbientColor = env->AmbientColor();
+    e.mFogColor = env->FogColor();
+    e.mFogEnable = env->FogEnable();
+    e.mFogStart = env->GetFogStart();
+    e.mFogEnd = env->GetFogEnd();
+}
+
+void LightPreset::AnimateEnvFromPreset(
+    RndEnviron *env, const LightPreset::EnvironmentEntry &entry, float f3
+) {
+    Hmx::Color c;
+    Interp(env->AmbientColor(), entry.mAmbientColor, f3, c);
+    env->SetAmbientColor(c);
+    float fogStart, fogEnd;
+    if (entry.mFogEnable) {
+        Hmx::Color fc;
+        Interp(env->FogColor(), entry.mFogColor, f3, fc);
+        env->SetFogColor(fc);
+        Interp(env->GetFogStart(), entry.mFogStart, f3, fogStart);
+        Interp(env->GetFogEnd(), entry.mFogEnd, f3, fogEnd);
+    } else {
+        float far = RndCam::Current() ? RndCam::Current()->FarPlane() : FLT_MAX;
+        Interp(env->GetFogStart(), far, f3, fogStart);
+        Interp(env->GetFogEnd(), far, f3, fogEnd);
+    }
+    env->SetFogRange(fogStart, fogEnd);
+    if (f3 == 1) {
+        env->SetFogEnable(entry.mFogEnable);
+    }
+}
+
+void LightPreset::AnimateSpotFromPreset(
+    Spotlight *spot, const LightPreset::SpotlightEntry &entry, float f3
+) {
+    if (spot->AnimateColorFromPreset()) {
+        Hmx::Color spotColor = spot->Color();
+        float intensity = spot->Intensity();
+        Hmx::Color col;
+        col.Unpack(entry.mColor);
+        Interp(spotColor, col, f3, spotColor);
+        Interp(intensity, entry.mIntensity, f3, intensity);
+        spot->SetColorIntensity(spotColor, intensity);
+        if (spot->GetFlare() && f3 == 1.0f) {
+            spot->SetFlareEnabled(entry.unk8 & SpotlightEntry::kEnabled);
+        }
+    }
+    if (spot->AnimateOrientationFromPreset()) {
+        Hmx::Quat q0(0, 0, 0, 0);
+        Hmx::Quat q;
+        if (!(entry.unk8 & 2)) {
+            spot->unk36e = false;
+            q.Reset();
+        } else {
+            if (entry.unk20 != q0) {
+                q = entry.unk20;
+            } else {
+                entry.CalculateDirection(spot, q);
+            }
+        }
+        Interp(spot->unk370, q, f3, q);
+        spot->unk370 = q;
+        if (f3 == 1) {
+            spot->mTarget = entry.mTarget;
+        }
+        spot->unk340 = true;
+    }
+}
+
+void LightPreset::AnimateSpotlightDrawerFromPreset(
+    SpotlightDrawer *sd, const LightPreset::SpotlightDrawerEntry &e, float f
+) {
+    float val;
+    Interp(sd->Params().mBaseIntensity, e.mBaseIntensity, f, val);
+    sd->Params().mBaseIntensity = val;
+    Interp(sd->Params().mSmokeIntensity, e.mSmokeIntensity, f, val);
+    sd->Params().mSmokeIntensity = val;
+    Interp(sd->Params().mLightingInfluence, e.mLightInfluence, f, val);
+    sd->Params().mLightingInfluence = val;
+    Interp(sd->Params().mIntensity, e.mTotalIntensity, f, val);
+    sd->Params().mIntensity = val;
+}
+
+void LightPreset::SpotlightEntry::CalculateDirection(
+    Spotlight *s, Hmx::Quat &q
+) const {
+    q = unk20;
+    RndTransformable *target = mTarget;
+    if ((unk8 & 2) && target) {
+        Hmx::Matrix3 m;
+        s->CalculateDirection(target, m);
+        q = Hmx::Quat(m);
+    }
+}
+
+void LightPreset::SpotlightEntry::Animate(
+    Spotlight *spot, const LightPreset::SpotlightEntry &entry, float f3
+) {
+    float fout;
+    Interp(mIntensity, entry.mIntensity, f3, fout);
+    Hmx::Color c1;
+    Hmx::Color c2;
+    c1.Unpack(mColor);
+    c2.Unpack(entry.mColor);
+    Interp(c1, c2, f3, c1);
+    mColor = c1.Pack();
+    Hmx::Quat q1;
+    CalculateDirection(spot, q1);
+    Hmx::Quat q2;
+    entry.CalculateDirection(spot, q2);
+    Interp(q1, q2, f3, unk20);
+    if (f3 == 1) {
+        unk8 = entry.unk8;
+        mTarget = entry.mTarget;
+    }
+}
+
+void LightPreset::AnimateState(
+    const LightPreset::Keyframe &k1, const LightPreset::Keyframe &k2, float f3
+) {
+    if (f3 < 1.1920929E-7f)
+        return;
+    for (uint i = 0; i != mSpotlightState.size(); i++) {
+        if (k2.mSpotlightChanges[i]) {
+            mSpotlightState[i].Animate(mSpotlights[i], k1.mSpotlightEntries[i], f3);
+        }
+    }
+    for (uint i = 0; i != mEnvironmentState.size(); i++) {
+        if (k2.mEnvironmentChanges[i]) {
+            mEnvironmentState[i].Animate(k1.mEnvironmentEntries[i], f3);
+        }
+    }
+    for (uint i = 0; i != mLightState.size(); i++) {
+        if (k2.mLightChanges[i]) {
+            mLightState[i].Animate(k1.mLightEntries[i], f3);
+        }
+    }
+    for (uint i = 0; i != mSpotlightDrawerState.size(); i++) {
+        if (k2.mSpotlightDrawerChanges[i]) {
+            mSpotlightDrawerState[i].Animate(k1.mSpotlightDrawerEntries[i], f3);
+        }
+    }
+}
+
+void LightPreset::Animate(float f) {
+    if (f < 1.1920929E-7f)
+        return;
+    MILO_ASSERT(mSpotlights.size() == mSpotlightState.size(), 0x3CD);
+    for (uint i = 0; i != mSpotlights.size(); i++) {
+        if (mSpotlights[i]->GetAnimateFromPreset()) {
+            float blend = f;
+            if (blend >= 1.1920929E-7f) {
+                AnimateSpotFromPreset(mSpotlights[i], mSpotlightState[i], blend);
+            }
+        }
+    }
+    MILO_ASSERT(mEnvironments.size() == mEnvironmentState.size(), 0x3DF);
+    for (uint i = 0; i != mEnvironments.size(); i++) {
+        if (mEnvironments[i]->GetAnimateFromPreset()) {
+            AnimateEnvFromPreset(mEnvironments[i], mEnvironmentState[i], f);
+        }
+    }
+    MILO_ASSERT(mLights.size() == mLightState.size(), 1000);
+    for (uint i = 0; i != mLights.size(); i++) {
+        if (mLights[i]->GetAnimateFromPreset()) {
+            AnimateLightFromPreset(mLights[i], mLightState[i], f);
+        }
+    }
+    MILO_ASSERT(mSpotlightDrawers.size() == mSpotlightDrawerState.size(), 0x3F1);
+    for (uint i = 0; i != mSpotlightDrawers.size(); i++) {
+        AnimateSpotlightDrawerFromPreset(
+            mSpotlightDrawers[i], mSpotlightDrawerState[i], f
+        );
+    }
+}
+
 DataNode LightPreset::OnViewKeyframe(DataArray *da) {
     ApplyState(mKeyframes[da->Int(2)]);
     Animate(1.0f);
diff --git a/src/system/world/LightPreset.h b/src/system/world/LightPreset.h
index a91ce91d..d8a30d1f 100644
--- a/src/system/world/LightPreset.h
+++ b/src/system/world/LightPreset.h
@@ -93,6 +93,7 @@ public:
         SpotlightDrawerEntry();
         void Save(BinStream &) const;
         void Load(BinStreamRev &);
+        void Animate(const SpotlightDrawerEntry &, float);
         bool operator!=(const SpotlightDrawerEntry &) const;
 
         /** "Global intensity scale" */
@@ -192,6 +193,10 @@ protected:
     int NextManualFrame(LightPreset::KeyframeCmd) const;
     void FillLightPresetData(RndLight *, LightPreset::EnvLightEntry &);
     void AnimateLightFromPreset(RndLight *, const LightPreset::EnvLightEntry &, float);
+    void AnimateEnvFromPreset(RndEnviron *, const EnvironmentEntry &, float);
+    void AnimateSpotFromPreset(Spotlight *, const SpotlightEntry &, float);
+    void AnimateSpotlightDrawerFromPreset(SpotlightDrawer *, const SpotlightDrawerEntry &, float);
+    void AnimateState(const Keyframe &, const Keyframe &, float);
     void ApplyState(LightPreset::Keyframe const &);
     void SetKeyframe(Keyframe &);
     void FillEnvPresetData(RndEnviron *, EnvironmentEntry &);
diff --git a/src/system/world/Spotlight.h b/src/system/world/Spotlight.h
index fb7a8923..f0255403 100644
--- a/src/system/world/Spotlight.h
+++ b/src/system/world/Spotlight.h
@@ -17,6 +17,8 @@
 
 /** "Represents a beam and floorspot for venue modeling" */
 class Spotlight : public RndDrawable, public RndTransformable, public RndPollable {
+    friend class LightPreset;
+
 public:
     struct BeamDef {
         enum Shape {
@@ -112,6 +114,13 @@ public:
     BeamDef GetBeam() const { return mBeam; }
     RndFlare *GetFlare() const { return mFlare; }
     ObjPtrList<RndDrawable> GetAdditionalObjects() const { return mAdditionalObjects; }
+    bool AnimateColorFromPreset() const { return mAnimateColorFromPreset; }
+    bool AnimateOrientationFromPreset() const { return mAnimateOrientationFromPreset; }
+    bool GetAnimateFromPreset() const {
+        return mAnimateColorFromPreset || mAnimateOrientationFromPreset;
+    }
+    RndTransformable *GetTarget() const { return mTarget; }
+    bool IsFlareEnabled() const { return mFlareEnabled; }
     void SetFlareIsBillboard(bool);
     void SetIntensity(float);
     void SetColorIntensity(const Hmx::Color &c, float f);
diff --git a/src/system/world/SpotlightDrawer.h b/src/system/world/SpotlightDrawer.h
index d7837936..4048ec50 100644
--- a/src/system/world/SpotlightDrawer.h
+++ b/src/system/world/SpotlightDrawer.h
@@ -81,6 +81,7 @@ public:
     void UpdateBoxMap();
     void ApplyLightingApprox(BoxMapLighting &, float) const;
     const SpotDrawParams &Params() const { return mParams; }
+    SpotDrawParams &Params() { return mParams; }
 
     static SpotlightDrawer *Current() { return sCurrent; }
     static bool DrawNGSpotlights();
```

### agent-aba5b278 — Character/UI/Loader
```diff
diff --git a/src/system/char/Character.cpp b/src/system/char/Character.cpp
index a49c0719..889b429b 100644
--- a/src/system/char/Character.cpp
+++ b/src/system/char/Character.cpp
@@ -591,6 +591,83 @@ void Character::ClearInterestFilterFlags() {
     }
 }
 
+void Character::UnhookShadow() {
+    for (int i = 0; i < mShadowBones.size(); i++) {
+        ShadowBone *cur = mShadowBones[i];
+        ObjRef &refs = const_cast<ObjRef &>(cur->Refs());
+        while (!refs.empty()) {
+            ObjRef::iterator it = refs.begin();
+            it->Replace(cur->Parent());
+        }
+    }
+    DeleteAll(mShadowBones);
+}
+
+void Character::SyncShadow() {
+    UnhookShadow();
+    if (!mShadow.empty()) {
+        FOREACH (it, mShadow) {
+            RndMesh *mesh = dynamic_cast<RndMesh *>((RndDrawable *)*it);
+            if (mesh) {
+                if (mesh->NumBones() != 0) {
+                    for (int i = 0; i < mesh->NumBones(); i++) {
+                        mesh->SetBone(i, AddShadowBone(mesh->BoneTransAt(i)), false);
+                    }
+                } else {
+                    mesh->SetTransParent(AddShadowBone(mesh->TransParent()), false);
+                }
+            }
+        }
+    }
+}
+
+void Character::FindInterestObjects(ObjectDir *dir) {
+    if (dir) {
+        CharEyes *eyes = GetEyes();
+        if (eyes) {
+            eyes->ClearAllInterestObjects();
+            for (ObjDirItr<CharInterest> it(dir, true); it != nullptr; ++it) {
+                if (ValidateInterest(it, dir)) {
+                    eyes->AddInterestObject(it);
+                }
+            }
+            for (ObjDirItr<Character> it(dir, true); it != nullptr; ++it) {
+                if (!streq(it->Name(), Name())) {
+                    for (ObjDirItr<CharInterest> it2(it, true); it2 != nullptr; ++it2) {
+                        if (ValidateInterest(it2, it)) {
+                            eyes->AddInterestObject(it2);
+                        }
+                    }
+                }
+            }
+        }
+    }
+}
+
+void Character::DrawShowing() {
+    START_AUTO_TIMER("char_draw");
+    float screenSize = ComputeScreenSize(RndCam::Current());
+    int lod;
+    if (mForceLod < 0) {
+        for (lod = 0; lod < (int)mLods.size() - 1; lod++) {
+            float hysteresis;
+            if (lod < mLastLod)
+                hysteresis = 0.09f;
+            else
+                hysteresis = -0.09f;
+            if (screenSize >= (hysteresis + 1.0f) * mLods[lod].mScreenSize)
+                break;
+        }
+    } else {
+        lod = Clamp<int>(0, mLods.size() - 1, mForceLod);
+    }
+    DrawLod(lod);
+}
+
+void Character::DrawLod(int lod) {
+    DrawLodOrShadow(lod, mDrawMode);
+}
+
 #pragma endregion
 #pragma region Character Methods
 
diff --git a/src/system/rndobj/Text.h b/src/system/rndobj/Text.h
index 4c4d129b..2d5a90cf 100644
--- a/src/system/rndobj/Text.h
+++ b/src/system/rndobj/Text.h
@@ -281,6 +281,7 @@ public:
     static bool IsBlacklightModeEnabled() { return sBlacklightModeEnabled; }
 
     int GetTextSize() const { return Max<int>(mFixedLength, mText.length()); }
+    const String &GetText() const { return mText; }
     void SetCapsMode(CapsMode c) { mCapsMode = c; }
     void UpdateText();
     void SetText(const char *);
diff --git a/src/system/ui/LocalePanel.cpp b/src/system/ui/LocalePanel.cpp
index 51221efa..05d3c0ed 100644
--- a/src/system/ui/LocalePanel.cpp
+++ b/src/system/ui/LocalePanel.cpp
@@ -1,10 +1,26 @@
 #include "ui/LocalePanel.h"
+#include "obj/Dir.h"
+#include "ui/PanelDir.h"
 #include "ui/UI.h"
+#include "ui/UILabel.h"
+#include "ui/UIList.h"
 #include "ui/UIListLabel.h"
+#include "ui/UIListWidget.h"
 #include "utl/Locale.h"
+#include "utl/MakeString.h"
 #include "utl/Std.h"
+#include <algorithm>
 
-LocalePanel::LocalePanel() {}
+namespace {
+    struct LabelSort {
+        bool operator()(const UILabel *u1, const UILabel *u2) const {
+            return stricmp(
+                const_cast<UILabel *>(u1)->TextToken().Str(),
+                const_cast<UILabel *>(u2)->TextToken().Str()
+            ) < 0;
+        }
+    };
+}
 
 int LocalePanel::NumData() const { return mEntries.size(); }
 
@@ -63,7 +79,55 @@ void LocalePanel::Text(int i, int j, UIListLabel *listlabel, UILabel *label) con
     }
 }
 
-LocalePanel::Entry::Entry() {}
+void LocalePanel::AddDirEntries(ObjectDir *dir, const char *cc) {
+    std::vector<UILabel *> labels;
+    for (ObjDirItr<UILabel> it(dir, true); it != nullptr; ++it) {
+        if (it->Showing())
+            labels.push_back(it);
+    }
+    std::sort(labels.begin(), labels.end(), LabelSort());
+    if (!labels.empty()) {
+        AddHeading(MakeString("%s: %s", cc ? cc : "proxy", PathName(dir)));
+    }
+    for (std::vector<UILabel *>::iterator it = labels.begin(); it != labels.end(); ++it) {
+        UILabel *cur = *it;
+        Entry entry;
+        entry.mLabel = cur->Name();
+        entry.mToken = TokenForLabel(cur);
+        entry.mString = cur->GetText().c_str();
+        mEntries.push_back(entry);
+    }
+    for (ObjDirItr<UIList> it(dir, true); it != nullptr; ++it) {
+        if (it->Showing()) {
+            AddHeading(MakeString("%s: %s", it->ClassName(), it->Name()));
+            const std::vector<UIListWidget *> &widgets = it->GetWidgets();
+            std::vector<UIListWidget *>::const_iterator wIt;
+            for (int i = 0; i < it->NumDisplay(); i++) {
+                for (wIt = widgets.begin(); wIt != widgets.end(); ++wIt) {
+                    UIListLabel *listLabel = dynamic_cast<UIListLabel *>(*wIt);
+                    if (listLabel) {
+                        UILabel *elementLabel = listLabel->ElementLabel(i);
+                        if (elementLabel) {
+                            if (elementLabel->GetText().length() != 0) {
+                                Entry entry;
+                                entry.mLabel =
+                                    MakeString("%i:%s", i, listLabel->MatchName());
+                                entry.mToken = TokenForLabel(elementLabel);
+                                entry.mString = elementLabel->GetText().c_str();
+                                mEntries.push_back(entry);
+                            }
+                        }
+                    }
+                }
+            }
+        }
+    }
+    for (ObjDirItr<PanelDir> it(dir, true); it != nullptr; ++it) {
+        if (it != dir) {
+            AddDirEntries(it, 0);
+        }
+    }
+}
 
 BEGIN_HANDLERS(LocalePanel)
     HANDLE_EXPR(token, mEntries[_msg->Int(2)].mToken)
diff --git a/src/system/ui/LocalePanel.h b/src/system/ui/LocalePanel.h
index 5d12f337..963f4478 100644
--- a/src/system/ui/LocalePanel.h
+++ b/src/system/ui/LocalePanel.h
@@ -9,20 +9,23 @@
 class LocalePanel : public UIPanel, public UIListProvider {
 public:
     struct Entry {
-        Entry();
+        Entry() {}
+        ~Entry() {}
         String mHeading; // 0x0
         String mLabel; // 0x8
         Symbol mToken; // 0x10
         String mString; // 0x14
     };
-    LocalePanel();
+    LocalePanel() {}
     // Hmx::Object
-    virtual ~LocalePanel();
+    virtual ~LocalePanel() {}
     OBJ_CLASSNAME(LocalePanel)
     OBJ_SET_TYPE(LocalePanel)
     virtual DataNode Handle(DataArray *, bool);
     // UIPanel
     virtual void Enter();
+
+    NEW_OBJ(LocalePanel)
     // UIListProvider
     virtual void Text(int, int, UIListLabel *, UILabel *) const;
     virtual int NumData() const;
diff --git a/src/system/ui/UI.cpp b/src/system/ui/UI.cpp
index c2374c7f..55a4ee67 100644
--- a/src/system/ui/UI.cpp
+++ b/src/system/ui/UI.cpp
@@ -496,6 +496,11 @@ DataNode Automator::OnCustomMsg(const Message &msg) {
     return DATA_UNHANDLED;
 }
 
+DataNode Automator::OnMsg(const UIComponentScrollMsg &msg) {
+    HandleMessage(msg.Message::Type());
+    return DATA_UNHANDLED;
+}
+
 DataNode Automator::OnMsg(const UITransitionCompleteMsg &msg) {
     if (mScreenScripts && !mRecord)
         StartAuto(msg.GetNewScreen());
diff --git a/src/system/ui/UI.h b/src/system/ui/UI.h
index b4c03c00..304225ee 100644
--- a/src/system/ui/UI.h
+++ b/src/system/ui/UI.h
@@ -122,6 +122,7 @@ private:
 
     DataNode OnCustomMsg(const Message &);
     DataNode OnMsg(UITransitionCompleteMsg const &);
+    DataNode OnMsg(UIComponentScrollMsg const &);
     DataNode OnMsg(ButtonDownMsg const &);
     void FillButtonMsg(ButtonDownMsg &, int);
     DataNode OnCheatInvoked(DataArray const *);
diff --git a/src/system/ui/UIList.cpp b/src/system/ui/UIList.cpp
index aeabe44e..0524706a 100644
--- a/src/system/ui/UIList.cpp
+++ b/src/system/ui/UIList.cpp
@@ -156,6 +156,10 @@ END_COPYS
 
 UIListDir *UIList::GetUIListDir() const { return mListDir; }
 
+int UIList::Selected() const { return mListState.Selected(); }
+
+UIListState &UIList::GetListState() { return mListState; }
+
 int UIList::SelectedPos() const { return mListState.Selected(); }
 
 bool UIList::IsScrolling() const { return mListState.IsScrolling(); }
diff --git a/src/system/ui/UIList.h b/src/system/ui/UIList.h
index 0baf4ec5..e18f194b 100644
--- a/src/system/ui/UIList.h
+++ b/src/system/ui/UIList.h
@@ -85,6 +85,9 @@ public:
     bool SetSelectedSimulateScroll(Symbol, bool);
     UIListDir *GetUIListDir() const;
 
+    int Selected() const;
+    UIListState &GetListState();
+    const std::vector<UIListWidget *> &GetWidgets() const { return mWidgets; }
     int NumDisplay() const { return mListState.NumDisplay(); }
     int GridSpan() const { return mListState.GridSpan(); }
     bool Circular() const { return mListState.Circular(); }
diff --git a/src/system/ui/UIListLabel.cpp b/src/system/ui/UIListLabel.cpp
index f5668f91..6cd33282 100644
--- a/src/system/ui/UIListLabel.cpp
+++ b/src/system/ui/UIListLabel.cpp
@@ -66,6 +66,8 @@ UIListSlotElement *UIListLabel::CreateElement(UIList *uilist) {
     return nullptr;
 }
 
+RndTransformable *UIListLabel::RootTrans() { return mLabel; }
+
 #pragma endregion UIListLabel
 #pragma region UIListLabelElement
 
diff --git a/src/system/ui/UIPanel.h b/src/system/ui/UIPanel.h
index 94cd7952..e0fd7359 100644
--- a/src/system/ui/UIPanel.h
+++ b/src/system/ui/UIPanel.h
@@ -39,7 +39,7 @@ public:
     virtual bool Exiting() const;
     virtual bool Unloading() const;
     virtual void Poll();
-    virtual void SetPaused(bool);
+    virtual void SetPaused(bool paused) { mPaused = paused; }
     virtual UIComponent *FocusComponent();
     virtual void FocusIn() {}
     virtual void FocusOut() {}
diff --git a/src/system/utl/Loader.cpp b/src/system/utl/Loader.cpp
index 7042f798..124d32ef 100644
--- a/src/system/utl/Loader.cpp
+++ b/src/system/utl/Loader.cpp
@@ -8,6 +8,7 @@
 #include "os/File.h"
 #include "os/Platform.h"
 #include "os/System.h"
+#include "os/Timer.h"
 #include "utl/ChunkStream.h"
 #include "utl/FilePath.h"
 #include "utl/MemMgr.h"
@@ -23,13 +24,7 @@ void FrontLoaderGlitchCB(float f1, void *v) {
     MILO_LOG("Loader %s %s took %f (%s to %s)\n");
 }
 
-const char *WhiteSpace(int count) {
-    int len = 0x80;
-    MILO_ASSERT(count < len, 0x179);
-    MILO_ASSERT(count >= 0, 0x17A);
-    return &"                                                                                                                                "
-        [0x80 - count];
-}
+const char *WhiteSpace(int count);
 
 #pragma region Loader
 
@@ -275,6 +270,84 @@ Loader *LoadMgr::ForceGetLoader(const FilePath &fp) {
     }
 }
 
+struct FrontLoaderGlitchData {
+    String filename;
+    const char *startState;
+    const char *endState;
+    LoaderPos pos;
+};
+
+void LoadMgr::PollFrontLoader() {
+    Loader *loader = mLoading.front();
+    LoaderPos savedPos = mLoaderPos;
+    mLoaderPos = loader->mPos;
+    FrontLoaderGlitchData glitchData;
+    glitchData.filename = loader->LoaderFile().c_str();
+    glitchData.pos = loader->mPos;
+    glitchData.startState = loader->StateName();
+    if (TheArchive && Archive::DebugArkOrder()) {
+        if (loader->unk14 == -1) {
+            loader->unk14 = SystemMs();
+            if (gLoadCount == 0) {
+                TheDebug << MakeString(
+                    "Loading%s Start '%s'\n",
+                    WhiteSpace(0),
+                    glitchData.filename
+                );
+            }
+            gLoadCount++;
+        }
+    }
+    int startTime = loader->unk14;
+    bool isLoaded = false;
+    bool deleted = false;
+    MemPushHeap(loader->mHeap);
+    if (UsingCD()) {
+        AutoGlitchReport glitch(
+            mPeriod * 3.0f, FrontLoaderGlitchCB, &glitchData
+        );
+        loader->PollLoading();
+        if (!ListFind(mLoading, loader)) {
+            isLoaded = true;
+            deleted = true;
+            glitchData.endState = "deleted";
+        } else {
+            glitchData.endState = loader->StateName();
+            isLoaded = loader->IsLoaded();
+        }
+    } else {
+        loader->PollLoading();
+    }
+    MemPopHeap();
+    if (TheArchive && Archive::DebugArkOrder() && isLoaded) {
+        int endTime = SystemMs();
+        if (!deleted) {
+            gLoadCount--;
+            loader->unk14 = -1;
+        }
+        int elapsed = endTime - startTime;
+        if (elapsed > 20 || gLoadCount == 0) {
+            TheDebug << MakeString(
+                "Loading%s End   %4d [%5d,%5d]  '%s'\n",
+                WhiteSpace(gLoadCount),
+                elapsed,
+                startTime,
+                endTime,
+                glitchData.filename
+            );
+        }
+    }
+    mLoaderPos = savedPos;
+}
+
+__declspec(noinline) const char *WhiteSpace(int count) {
+    int len = 0x80;
+    MILO_ASSERT(count < len, 0x179);
+    MILO_ASSERT(count >= 0, 0x17A);
+    return &"                                                                                                                                "
+        [0x80 - count];
+}
+
 void LoadMgr::Poll() {
     if (mPeriod > 0) {
         mTimer.Restart();
diff --git a/src/system/utl/Loader.h b/src/system/utl/Loader.h
index 9f183a62..bbc7192d 100644
--- a/src/system/utl/Loader.h
+++ b/src/system/utl/Loader.h
@@ -32,6 +32,8 @@ public:
 
     MEM_OVERLOAD(Loader, 0xA8);
 
+    friend class LoadMgr;
+
 protected:
     virtual void PollLoading() = 0;
 
```
