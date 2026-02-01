# MoveDir Function Implementations - 2026-02-01

## Summary

Implemented and matched four MoveDir functions in `src/system/hamobj/MoveDir.cpp`.

| Function | Match % | Instructions | Status |
|----------|---------|-------------|--------|
| **SongSpeed** | **100%** | 24 | Perfect match |
| **ClosestMoveFrame** | **99.9%** | 36 | 1 unfixable ICF merged call |
| **SongSeconds** | **93.5%** | 48 | 3 dead-code instructions (compiler artifact) |
| **InGracePeriod** | **87.9%** | 46 | Branch polarity + comparison pattern diffs |

## Key Learnings

### Early Return Pattern (ClosestMoveFrame)
Placing `if (!move) return nullptr;` before the local struct definition eliminated extra `b`/`li r3,0` instructions and fixed scope mangling (`?3` vs `?4` in the FilterFrameDist symbol name).

### Pre-computing Integer Expressions (ClosestMoveFrame)
Extracting `int measureBeats = measure * 4;` into a separate variable forced the compiler to schedule `slwi` before the `bl GetMoveFrames` call, matching the target instruction order.

### ICF Merged Calls (ClosestMoveFrame)
The remaining 0.1% diff is `merged_824B0BC8` — the linker merged `GetMoveFrames()` const and non-const overloads to the same address. Unfixable at compile time.

### Direct Accessor vs Wrapper (SongSpeed)
Using `TheMaster->GetAudio()->GetSongStream()` directly instead of a `GetHxAudio()` wrapper generated the correct call sequence.

### Double-Call Pattern (SongSeconds)
The target code reloads the stream pointer after a null check:
```cpp
Stream *stream = audio->GetSongStream();
if (stream) {
    stream = TheMaster->GetAudio()->GetSongStream();  // Reload
    seconds += stream->GetJumpBackTotalTime() * 0.001f;
}
```

### Signed Null Check Cast (SongSeconds)
Using `(int)audio` generates `cmpwi` (signed comparison) vs `cmplwi` (unsigned) for pointer null checks.

### Dead Code Scheduling (SongSeconds, unfixable)
The compiler schedules `seconds * 1000.0f` before the `GetJumpBackTotalTime()` call even though the result is overwritten by the return value in f1. This is a compiler artifact we cannot replicate — accounts for 3 deleted instructions.

### Virtual Base Adjustment (InGracePeriod)
`PropertyEventProvider` uses virtual inheritance from `Hmx::Object`. Using `Hmx::Object *provider` instead of `PropertyEventProvider *provider` generates the correct virtual base table lookup.

### Property Name Discovery (InGracePeriod)
The property is `"start_score_move_index"`, not `"grace_period_measure"` — determined by cross-referencing Ghidra decompilation.

### Branch Polarity (InGracePeriod, unfixable)
Target uses `bne` with fallthrough; our compiler generates `beq` to exit. The equality comparison also differs: target uses `subfc`/`eqv`/`srwi`/`addze`/`clrlwi` vs our `subf`/`cntlzw`/`extrwi`.

## Git Diff

```diff
diff --git a/src/system/hamobj/MoveDir.cpp b/src/system/hamobj/MoveDir.cpp
index 14ddf3f1..4445c919 100644
--- a/src/system/hamobj/MoveDir.cpp
+++ b/src/system/hamobj/MoveDir.cpp
@@ -1072,9 +1072,12 @@ float MoveDir::SongSeconds() {
     float seconds = TheTaskMgr.Seconds(TaskMgr::kRealTime);
     if (TheMaster) {
         HamAudio *audio = TheMaster->GetAudio();
-        if (audio && audio->GetSongStream()) {
+        if ((int)audio) {
             Stream *stream = audio->GetSongStream();
-            seconds += stream->GetJumpBackTotalTime() * 0.001f;
+            if (stream) {
+                stream = TheMaster->GetAudio()->GetSongStream();
+                seconds += stream->GetJumpBackTotalTime() * 0.001f;
+            }
         }
     }
     return seconds;
@@ -1090,7 +1093,7 @@ float MoveDir::SongSpeed() const {

 bool MoveDir::InGracePeriod(int player) {
     HamPlayerData *playerData = TheGameData->Player(player);
-    PropertyEventProvider *provider = playerData->Provider();
+    Hmx::Object *provider = playerData->Provider();
     if (provider) {
         static Symbol prop("start_score_move_index");
         const DataNode *node = provider->Property(prop, false);
```

Note: ClosestMoveFrame and SongSpeed implementations were already in the working tree from the previous session and are not shown in this diff (they were part of the existing unstaged changes).
