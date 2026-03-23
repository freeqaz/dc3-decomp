# Character Animation Investigation — 2026-03-23

Investigation into why character animations weren't working during songs in the native port. Characters' bones didn't visually change — they showed a basic shuffle instead of full song choreography.

## Root Causes Found (5 issues)

### 1. HamDriver::Poll() weight bootstrap deadlock
**File**: `src/system/hamobj/HamDriver.cpp`

`Layer::mWeight` is never initialized in the constructor (`Layer() : mBeat(-kHugeFloat) {}`). On Xbox, uninitialized pool memory provides a non-zero bootstrap value that lets the first `Eval()` call compute the real weight. On native, zero-initialized heap memory keeps `mWeight` at 0 forever — the guard `mWeight > 0.0f` prevents `Eval()` from ever running.

**Fix**: Added `#ifdef HX_NATIVE` bootstrap that forces `Eval(1.0f)` when layers exist but `mWeight <= 0`. After the first bootstrap, the normal feedback loop takes over.

**Validated**: Bootstrap log fires in headless tests. No PPC regression (97.4% unchanged).

### 2. SortDraws null pointer crash (frame 4)
**File**: `src/system/rndobj/Utl.cpp`

`NullifyAllRefs()` (added for cascade destruction safety) nullifies `ObjPtrList` entries during destruction. When `RndDir::SyncDrawables()` later sorts the drawables list via `std::sort`, `SortDraws()` dereferences null pointers (SIGSEGV at address 0x20 = `GetOrder()` vtable slot).

**Fix**: Added null guard in `SortDraws()` — sorts nulls to the end. Guarded with `#ifdef HX_NATIVE`.

**Validated**: Headless tests run 15000+ frames without crash (previously crashed at frame 4).

### 3. MetaPanel exit stall (multiuser_screen blocking)
**File**: `src/lazer/meta_ham/MetaPanel.cpp`

`MetaPanel::Exiting()` checks `TheMetaMusic->IsActive()` during panel exit. On native, audio fadeout timing is unreliable — `IsActive()` stays true indefinitely, blocking screen transitions. Same pattern as `HamPanel::Exiting()` (which already returns false on native).

**Fix**: Skipped `TheMetaMusic->IsActive()` check on native with `#ifndef HX_NATIVE`.

**Validated**: multiuser_screen exit completes within 5 frames. Full DTA flow reaches game_screen.

### 4. FaderGroup heap-use-after-free (VenueEnter crash)
**File**: `src/system/synth/Faders.cpp`

During cascade destruction (`ObjectDir::DeleteObjects`), `FaderGroup::~FaderGroup()` early-returned during `InDeleteObjects()` without calling `RemoveClient(this)` on surviving faders (e.g., `TheSynth->MasterFader()`). The master fader's `mClients` set retained a dangling pointer. Later `Fader::UpdateValue()` → `SetDirty()` on the freed FaderGroup caused heap corruption, manifesting as a misleading SIGSEGV in `RndTransformable::SetTransParent` during `HamCamTransform::Setup`.

**Fix**: In the `InDeleteObjects()` early-return path, iterate `mFaders` and call `RemoveClient(this)` on each surviving fader (checked via `IsRefAlive()`). Guarded with `#ifdef HX_NATIVE`.

**Validated**: HamDirector::VenueEnter completes without crash. Game reaches gameplay.

### 5. Game::PostWaitStart beat-freeze bug
**File**: `src/lazer/game/Game.cpp`

When audio fails on native (mogg file not found), the `PostWaitStart` else branch set `mRealTime = false`. This caused `CurrentMs(false)` to call `mAudio.GetTime()` which returns 0 from a dead stream — freezing the beat timeline at 0 forever. The comment said "beat advances from real time" but the code did the opposite.

**Fix**: Changed `mRealTime = false` to `mRealTime = true` and added `mGameInput->SetTimeOffset()` call. Now `CurrentMs(true)` uses the wall-clock timer so beats advance naturally even without audio.

**Validated**: Beat timeline advances from -9 to positive values. `OnSelectCamera` fires with advancing beat values.

## Choreography Pipeline — Full Trace

### What Xbox does (perform mode)
1. `GameMode::SetGameplayMode("perform", true)` → `merge_moves=1`
2. `perform.dta` reset handler → `{[remixer] init}` → `OriginalChoreoRemixer::Init()`
3. `{[remixer] start_reset}` → DTA TypeDef handler → calls `{$this reset}` → `OriginalChoreoRemixer::Reset()`
4. `Reset()` loops over all measures → `SelectMove()` → `InsertMoveInSong()` → populates routine builder anim with clip/move PropKeys
5. `HamDirector::SongAnim(0)` returns the populated routine builder anim
6. Camera shot system plays intro clip on CharDriver during pre-song
7. `SongAnimation()=-1` gates off HamDriver during intro (intentional)
8. `realTime < 0` gates off `SetFrame` during intro (intentional)
9. Intro clip naturally starves (finite duration, non-looping)
10. `SongAnimation()=0`, `realTime >= 0` → SetFrame runs → ClipPlayer finds clips → HamDriver plays choreography

### What was broken on native
- Step 5 failed: routine builder was empty → SongAnim fallback hack needed
- Step 6-8 timing: beat-freeze kept everything at 0
- Step 9: intro clip never advanced (frozen beat)

### What works now
- Remixer Init + Reset fire via DTA (`start_reset` TypeDef handler works — `OBJ_CLASSNAME(SuperEasyRemixer)` fix was already applied)
- Routine builder is populated with 71 measures of clip keys
- Beat timeline advances via wall-clock timer
- Intro clip plays and eventually starves
- `SongAnimation()` flips from -1 to 0
- `songAnim->GetFrame()` advances (0.54 → 5.11 → 9.66 → ...)
- ClipPlayer::Init succeeds, PushClip/PushExpertClip find clips
- Full Xbox pipeline operates without hacks

## SongAnim Fallback (still present, safety net)
**File**: `src/system/hamobj/HamDirector.cpp` in `SongAnim()`

When `merge_moves=1` but the routine builder has no clip keys (remixer didn't run), falls back to the expert/pre-authored `song.anim`. This is a safety net — shouldn't trigger now that the remixer works, but protects against edge cases.

## Remaining Open Questions

1. **Visual verification needed**: The headless pipeline is validated (all diagnostic values correct), but visual confirmation in windowed/web mode is needed to confirm characters actually dance the full choreography.

2. **Mogg file availability**: Audio streams fail because mogg files aren't found (large files, not in extracted assets). Characters animate via wall-clock timing, but there's no music. Need to either ensure mogg files are available or accept silent gameplay.

3. **Diagnostic logging cleanup**: Multiple `ANIM-DIAG`, `SHOT-DIAG`, `CLIP-DIAG`, `REMIXER-DIAG` log statements were added during investigation. These should be removed or converted to `MILO_LOG` with rate limiting before merging.

4. **PushClip vs PushExpertClip path**: With `merge_moves=1` and routine builder populated, `PlayNormal` should use `PushRoutineBuilderClip` (path 1). If `TheMoveMgr->HasRoutine()` returns false for some reason, it falls to PushExpertClip/PushClip which may have different behavior. Needs visual verification.

5. **HamDriver bootstrap vs Xbox behavior**: The bootstrap hack is still needed because `Layer::mWeight` is uninitialized. On Xbox, garbage memory bootstraps it. This is a latent bug in the original code that happens to work due to memory allocator behavior. The hack is correct but worth noting.

6. **Camera shot `play_group` with empty animGroup**: Gameplay camera shots (e.g., `venue01.shot`) send `play_group` with `animGroup=''` (empty string). `CharDriver::PlayGroup("")` fails to find the group and returns without clearing the intro clip. The intro clip clears via natural starvation instead. This matches Xbox behavior but is worth documenting.

## Files Modified

| File | Change | Type |
|------|--------|------|
| `src/system/hamobj/HamDriver.cpp` | mWeight bootstrap | Native fix |
| `src/system/rndobj/Utl.cpp` | SortDraws null guard | Native fix |
| `src/lazer/meta_ham/MetaPanel.cpp` | Skip IsActive check | Native fix |
| `src/system/synth/Faders.cpp` | RemoveClient in dtor | Native fix |
| `src/lazer/game/Game.cpp` | mRealTime=true on audio fail | Native fix |
| `src/system/hamobj/HamDirector.cpp` | SongAnim fallback + diagnostics | Native safety net |
| `src/system/hamobj/HamCamShot.cpp` | SHOT-DIAG logging | Diagnostic |
| `src/system/hamobj/ClipPlayer.cpp` | CLIP-DIAG logging | Diagnostic |
| `src/system/hamobj/OriginalChoreoRemixer.cpp` | REMIXER-DIAG logging | Diagnostic |
| `scripts/dc3-input-flows/ymca.txt` | Updated comments + timing | Test script |
