# Session: MoveDir::ClosestMoveFrame Decomp

**Date**: 2026-02-01
**Function**: `MoveDir::ClosestMoveFrame()`
**Symbol**: `?ClosestMoveFrame@MoveDir@@AAAPAVMoveFrame@@XZ`
**Result**: 84.1% -> **99.9%** (1 unfixable linker-merged call remaining)

## Summary

Implemented `MoveDir::ClosestMoveFrame()` from scratch and iterated through multiple code variations to achieve a near-perfect match. The function finds the closest `MoveFrame` to the current beat position within the active move.

## What Worked

Three key changes were needed to go from 84.1% to 99.9%:

### 1. Early return instead of if-block (84.1% -> 84.6%)
Changed from `if (move) { ... } return nullptr;` to `if (!move) return nullptr;`. This eliminated 2 extra instructions (`b`/`li r3,0`) for the nullptr fallthrough path, matching the target's 144-byte size.

### 2. Struct at function scope, not inside if-block (fixed scope `?3`)
With the struct inside the if-block, the mangled `min_element` template instantiation used scope `?4`. Moving the struct definition after the early return (at function scope but below the guard) produced scope `?3`, matching the target symbol name exactly.

### 3. Pre-computing `measure * 4` as a separate variable (84.7% -> 99.9%)
The critical fix. The target compiles `slwi` (shift left = multiply by 4) BEFORE calling `GetMoveFrames`, but the compiler kept scheduling the call first. Extracting `int measureBeats = measure * 4;` as a separate variable forced the compiler to compute the shift before the function call, matching the target's instruction scheduling.

## Remaining Diff (unfixable)

The sole remaining mismatch is at index 13:
- **Target**: `bl merged_824B0BC8` (ICF-merged `GetMoveFrames`)
- **Base**: `bl GetMoveFrames` (non-merged)

This is Identical COMDAT Folding (ICF) where the linker merged the const and non-const `GetMoveFrames` overloads to a single address. This only happens at link time and cannot be replicated at the compilation unit level.

## Lessons Learned

- **MSVC scope numbering in mangled names**: The `?N` in template instantiation names corresponds to the lexical scope depth where the struct is defined. Moving a struct between scopes changes the template instantiation's mangled name.
- **Variable extraction controls instruction scheduling**: When the compiler schedules a function call before an arithmetic operation, extracting the operation into a named variable can force it to be computed earlier.
- **Incremental builds can be stale**: Always force rebuild with `ninja` before running objdiff to avoid comparing against cached object files.
- **Early return vs if-block**: Can eliminate dead code paths that the compiler doesn't optimize away.

## Files Modified

- `src/system/hamobj/MoveDir.cpp` - Added implementation
- `src/system/hamobj/MoveDir.h` - Added declaration
- `src/system/hamobj/HamMove.h` - Added `GetBeat()` accessor

## Git Diff

```diff
diff --git a/src/system/hamobj/HamMove.h b/src/system/hamobj/HamMove.h
index e4f85288..db420d73 100644
--- a/src/system/hamobj/HamMove.h
+++ b/src/system/hamobj/HamMove.h
@@ -31,6 +31,7 @@ public:
     const Vector3 &NodeInverseScale(int, MoveMirrored) const;
     void SetNodeScale(int, MoveMirrored, const Vector3 &);
     float QuantizedSeconds(float) const;
+    float GetBeat() const { return mBeat; }
     FilterVersionType Version() const {
         int filterMask = (unk4 & 0x300000) >> 5;
         return filterMask ? kFilterVersionHam1 : kFilterVersionHam2;
diff --git a/src/system/hamobj/MoveDir.cpp b/src/system/hamobj/MoveDir.cpp
index 584782d2..9951f68b 100644
--- a/src/system/hamobj/MoveDir.cpp
+++ b/src/system/hamobj/MoveDir.cpp
@@ -1047,6 +1047,27 @@ float MoveDir::DetectRangePSNR(
     return result;
 }

+MoveFrame *MoveDir::ClosestMoveFrame() {
+    HamMove *move = mMovePlayerData[0].mCurMove;
+    if (!move)
+        return nullptr;
+    struct FilterFrameDist {
+        float mDist;
+        FilterFrameDist(float dist) : mDist(dist) {}
+        bool operator()(const MoveFrame &a, const MoveFrame &b) {
+            return fabsf(a.GetBeat() - mDist) < fabsf(b.GetBeat() - mDist);
+        }
+    };
+    int measure = TheTaskMgr.CurrentMeasure();
+    float beat = TheTaskMgr.TotalBeat();
+    int measureBeats = measure * 4;
+    std::vector<MoveFrame> &frames = move->GetMoveFrames();
+    MoveFrame *result = std::min_element(
+        frames.begin(), frames.end(), FilterFrameDist(beat - (float)measureBeats)
+    );
+    return result != frames.end() ? result : nullptr;
+}
+
 DancerSequence *MoveDir::SkillsSequence(Difficulty d, Symbol s1, Symbol s2) {
     PracticeSection *section = GetPracticeSection(d);
     if (section) {
diff --git a/src/system/hamobj/MoveDir.h b/src/system/hamobj/MoveDir.h
index 235d358a..cb37d82c 100644
--- a/src/system/hamobj/MoveDir.h
+++ b/src/system/hamobj/MoveDir.h
@@ -125,6 +125,7 @@ private:
         const std::pair<DetectFrame *, DetectFrame *> &, const FilterVersion *
     ) const;
     void PostUpdateFilters();
+    MoveFrame *ClosestMoveFrame();

     DataNode OnStreamJump(const DataArray *);
```
