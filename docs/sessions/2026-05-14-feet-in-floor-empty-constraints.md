# Feet-in-Floor — Root Cause: Empty `mConstraints`

**Date**: 2026-05-14
**Status**: Root cause identified, fix not yet implemented
**Failing test**: `GameplayTelemetryTest.FeetNotBelowFloorDuringGameplay`

## TL;DR

Real Xbox does **not** exhibit feet-in-floor (user verified visually). Native does
(toe Z=-3.12, ankle Z=0.84 vs floor at Z=0).

The bug is **not** in `HamIKEffector::Poll` math (which matches Xbox at 99.9%).
The bug is that **`HamIKEffector::mConstraints` is empty for every poll on the
player character.** The IK solver has no constraint targets to anchor the feet to
the floor, so feet follow the animation-driven pelvis through crouching poses.

## Evidence

Captured via `DC3_IK_DIAG` `fprintf`s in `HamIKEffector::Poll` and
`GameplayTelemetry::CaptureSnapshot`. Telemetry log:
`/tmp/claude-1000/gameplay_tel_FeetNotBelowFloorDuringGameplay.log`

### Bone positions

| metric | rest pose (loading) | gameplay (crouch) |
|---|---|---|
| `player0.WorldXfm.v.z` (char root) | 0.00 | 0.11 |
| `bone_pelvis.WorldXfm.v.z` | 42.51 | 33.62 |
| `lAnkle.WorldXfm.v.z` | 4.39 | 0.84 |
| `lToe.WorldXfm.v.z` | 0.01 (on floor) | -3.12 (below floor) |
| `lAnkle.WorldXfm.m.x` (X axis direction) | (0,0,-1) | (0,0,-1) |

The character root is identical in both states. The pelvis dropped 8.9 units
(crouch dance pose). With proper IK, the foot would stay planted on the floor;
without constraints, the foot follows the pelvis down by 3.5 units.

The ankle's local X axis points **straight down** in both rest and gameplay —
that's the bind pose orientation (foot dangling like a T-pose). In a flat-foot
standing/dancing pose, the IK should rotate the ankle so X axis is horizontal.
Without constraints, no such rotation happens.

### IK solver state during gameplay

```
DC3_IK_DIAG IkSnap[1]: effPath=bone_L-ankle.ikf (char/main/main.milo)
  fingerW.v=(32.44,-28.22,-3.12)
  effW.v=(30.91,-24.05,0.84)
  neutral.v=(32.44,-28.22,-3.12)
  totalWeight=0.000  constraintCount=0
```

- `mConstraints.size() == 0` for ankle, pelvis, hand — every effector type.
- `weight=1.0` (the effector's Weight() returns 1, meaning IK is "on").
- `totalWeight=0.000` because there are no constraints to accumulate weight from.
- `mMore = null` (no chain of effectors with additional constraints).
- `finger = spot_L-toe.trans` (IK end target — a separate transform from the toe bone).

With `totalWeight = 0`, in `HamIKEffector::Poll`:
```cpp
float remaining = 1.0f - totalWeight;        // = 1.0
q.v += remaining * effQ.v;                   // q.v = effQ.v (the current finger world position)
ScaleAddEq(q.q, effQ.q, remaining);          // q.q = effQ.q (current rotation)
```

So the IK target ends up being whatever the toe spot already is. The effector
(ankle) is then placed such that the toe stays where it is. No anchoring to floor.

## What we ruled out

- **Decomp bugs in IK math.** `Poll` is 99.9% match (only stack offset diffs).
  `ApplyConstraints` is 100% match.
- **Wrong character root.** `player0` is at Z≈0 in both rest and gameplay.
- **Foot inversion.** `m.z.z > 0` consistently; foot Z-axis points upward.
- **Garbage bones / NaN.** No NaN in any WorldXfm or LocalXfm.
- **Render-time Z offset in `BoneSetup.cpp`.** No such code exists (claimed by a
  prior agent, fabricated).
- **The IK "ground clamp" at line ~387.** Only runs when `totalWeight < 1.0`. Even
  if it ran, `clampFactor = (neutralQ.v.z - groundHeight - 5.0) * 0.09090909f`
  evaluates to a negative number for normal characters, clamped to 0 → no effect.

## The `Load`/`Save` asymmetry (note, not the bug)

```cpp
BEGIN_SAVES(HamIKEffector)  SAVE_SUPERCLASS(Hmx::Object)    // !!
BEGIN_LOADS(HamIKEffector)  LOAD_SUPERCLASS(CharPollable)   // !!
```

Both Load and Save match Xbox at 100%, so this is the same in the original game.
Either both are byte-level no-ops, or DC3's .ikf binary data doesn't include
constraints and they're populated at runtime by some other system.

## Next concrete steps

1. **Dump the binary contents of a HamIKEffector from a .milo file** to check
   whether the `mConstraints` array is empty in the original data or whether we
   silently drop them on load. (Tool: extract a player character's `bone_L-ankle.ikf`
   from `main.milo` and inspect the raw bytes.)

2. **Search for runtime population of `mConstraints`.** Candidates:
   - `BustAMoveData` / `BustAMove` (move/animation processing)
   - `HamRegulate` (limb regulation)
   - `ClipPlayer` (animation clip playback)
   - `MoveTrans` / `MoveGraph`
   - Any DTA scripts that set `(constraints ...)` on IK effectors (greps so far:
     no DTA files set this property — only schema definitions in `ham_objects.dta`).

3. **If constraints are populated dynamically:** find the path. Add telemetry at
   the population site to verify it runs for player characters in our build. If
   it's stubbed, implement it.

4. **If constraints are static in .ikf data:** binary-diff our load output against
   Xbox to find where bytes diverge. Likely a serialization fix in
   `ObjVector<Constraint>::operator>>` or a missing field in `Constraint`.

5. **Parallel work:** Xenia loading hang investigation continues to be valuable
   for ground truth, but the first-pass fix bypasses gameplay init. See
   `docs/sessions/2026-03-27-xenia-ik-debugging-plan.md`.

## Files in this investigation

### Modified for diagnostics (not yet committed)
- `src/system/hamobj/HamIKEffector.cpp` — `DC3_IK_DIAG IkSnap` + `PollOrder` logs
  in `Poll()`, gated by `HX_NATIVE`. Add `totalWeight`, constraint count, finger/
  effector names and world positions.
- `native/src/telemetry/GameplayTelemetry.cpp` — `DC3_IK_DIAG FootGeom` +
  `RestGeom` + `CharPath` logs in `CaptureSnapshot`. Capture ankle/toe/parent
  positions in both rest and gameplay state.

### Modified intentionally (commit-worthy)
- `native/tests/test_gameplay_telemetry.cpp` — `FeetNotBelowFloorDuringGameplay`
  test threshold restored to -2.0 with anti-relaxation warning.

### Background context
- `docs/sessions/2026-03-25-feet-in-ground-fix.md` — Phase 7 appended with same
  findings. Earlier phases describe the mLocalXfm back-computation fix (still
  valid) and the prior agents' fabricated conclusions.
- `docs/sessions/2026-03-27-xenia-ik-debugging-plan.md` — Xenia path for
  capturing Xbox ground truth.
- `docs/sessions/2026-03-28-ik-feet-continuation.md` — earlier continuation work.

## Running the failing test

```bash
cd /home/free/code/milohax/dc3-decomp/native/build && ninja milo-tests dc3-native
DC3_GAMEPLAY_TESTS=1 timeout 200 \
  /home/free/code/milohax/dc3-decomp/native/build/milo-tests \
  --gtest_filter='GameplayTelemetryTest.FeetNotBelowFloorDuringGameplay'
# Full log: /tmp/claude-1000/gameplay_tel_FeetNotBelowFloorDuringGameplay.log
# Grep:    grep "DC3_IK_DIAG" /tmp/claude-1000/gameplay_tel_*.log
```

## 2026-05-14: Re-validation attempt blocked by native build regressions

Re-ran the test after recent decomp progress to check if the bug was fixed by any of:
- `f1b65b4b fix feet-in-floor: IK save/restore survives dirty cascade + fix footDepth`
- `31fe13b7 strip all HX_NATIVE IK hacks — restore original Xbox ground truth`
- `5b67520c fix decomp bugs across 5 functions`
- `7d9c61e1 remove mLocalXfm back-computation hacks — fixes flying feet`

### Native build broken by upstream-port commits

The following pre-existing native build breaks were fixed in this session just to get a
binary out:

| File | Issue | Fix |
|---|---|---|
| `src/system/obj/ObjPtr_p.h` | Duplicate `Node::RefOwner` / `operator<<` definitions in `#ifdef HX_NATIVE` block conflicting with unconditional definitions at lines 389/636 and `Object.h:318` | Removed duplicates; kept comment noting where the canonical versions live |
| `src/lazer/meta_ham/ChallengeSortNode.h` | `Renumber(stlpmtx_std::vector<...>)` — namespace undefined for native; mismatches base class `std::vector` | Changed to `std::vector` |
| `src/lazer/meta_ham/OptionsPanel.cpp` | Switch case values `0x800A0003`–`0x800A0009` exceed `int` range with narrowing-conversion warning | Cast switch expr to `unsigned int`, append `U` to case literals |
| `src/system/char/CharEyes.cpp` | `NormalizeScale` redefined here (HX_NATIVE) vs inline in `src/system/math/Vec.h:284` | Removed local definition |
| `src/lazer/meta_ham/Playlist.cpp` | `static_cast<bool>(unk24) = false` lvalue cast in `const` method | `const_cast<CustomPlaylist *>(this)->unk24 = false;` |
| `src/system/obj/TypeProps.cpp` | Used undeclared `key` (likely a renamed param); param is `prop` | Substituted `prop` |
| `src/system/movie/Splash.cpp` | `Splash::ThreadStart` return type drifted `DWORD` → `unsigned long` | Restored `DWORD` to match decl in `Splash.h` |
| `src/system/utl/GlitchFinder.cpp` | `GlitchFindScriptImpl` called before definition (friend forward decl not enough in this build path) | Added explicit forward declaration at top of file |
| `native/src/platform/NetworkSocket_Stub.cpp` | `InqBoundPort` returns `int` vs base `bool` | Changed to `bool` |
| `src/system/char/CharClipSet.cpp` | `CharClipSet::DrawShowing` declared but never defined → link error | Stub forwards to `mPreviewChar->DrawShowing()` |
| `src/system/os/HolmesKeyboard.cpp` | `HolmesInput::Handle` declared virtual but never defined → link error | Stub returns `DataNode(0)` |
| `src/lazer/meta_ham/ShellInput.cpp` | (1) `HandsUp()`/`RaisedMs()` methods renamed to `GetHandsUp()`/`GetRaisedMs()` on `HandsUpGestureFilter`. (2) `sHasSkeleton` used before declaration in HX_NATIVE block. (3) Null-pointer dereferences in headless mode on `mCursorPanel`, `mDepthBuffer`, `mSkelIdentifier`, `mSkelChooser`, `mSkelExtTracker`, `mHandsUpGestureFilter`. | Renamed calls, declared `sHasSkeleton`, added null guards on all gesture/cursor subsystem polls. |

### After all those fixes: still can't reach gameplay

Engine boot now reaches `attract_screen` but crashes in `SkeletonChooser::Poll` →
`IsSinglePlayerMode` → `DataNode::Evaluate` at offset 0x8 (null DataNode access). The
Kinect/skeleton subsystem isn't initialized in headless native builds, but the recent
upstream `ShellInput::Poll` port unconditionally polls it.

```
DC3 Native: Caught SIGSEGV (signal 11) at address 0x8
  DataNode::Evaluate()
  DataNode::Sym(DataArray*)
  SkeletonChooser::IsSinglePlayerMode()
  SkeletonChooser::UpdateTrackedSkeletonsElective()
  SkeletonChooser::Poll()
  ShellInput::Poll()
  HamUI::Poll()
  App::RunWithoutDebugging()
```

### Status of the IK bug

**Re-validation complete (2026-05-15): bug confirmed, root cause still empty `mConstraints`.**

After fixing the build (see below), the failing test runs to completion:

```
Foot floor penetration check (556 samples):
  L-toe worst Z: -4.20 (ankle Z: 0.20)
  R-toe worst Z: -4.10 (ankle Z: -0.20)
  L below floor: 522/556 samples
  R below floor: 493/556 samples
```

Compared with the prior session:

| metric | 2026-05-14 | 2026-05-15 (post-decomp-progress) |
|---|---|---|
| L-toe worst Z | -3.30 | **-4.20** (slightly worse) |
| L-ankle (worst-toe sample) | 0.70 | **0.20** (lower) |
| samples below floor | 608/609 | **522/556** |
| `mConstraints.size()` on main.milo ankle | 0 | **0** (unchanged) |
| `totalWeight` returned by ApplyConstraints | 0.000 | **0.000** (unchanged) |

The recent commits — `f1b65b4b fix feet-in-floor: IK save/restore survives dirty
cascade + fix footDepth`, `31fe13b7 strip all HX_NATIVE IK hacks`, `7d9c61e1 remove
mLocalXfm back-computation hacks` — addressed *different* facets of the IK code
(dirty cascade, hacks cleanup), but the **constraint inputs to the IK solver are
still empty**, so the solver has nothing to anchor the feet against. The bug
manifests slightly differently (pelvis crouches deeper, ankle goes through floor
instead of sitting just above it) but the underlying mechanism is unchanged.

### Build unblock chain (2026-05-15)

Got the test running by patching the following on top of the build-fix list above:

| File | Issue | Fix |
|---|---|---|
| `native/src/native_link_glue.cpp` | `OggMalloc/OggCalloc/OggRealloc` undefined symbols at runtime (VorbisMem.cpp is empty in this tree; Xbox link_glue.cpp isn't compiled for native) | Added libc-backed allocator stubs |
| `src/lazer/meta_ham/SongSortMgr.cpp` | `SongSortMgr::GetSetlistMode()` declared but never defined → undefined symbol at runtime | Stub returns `false` (no setlist tracking yet) |
| `src/lazer/meta_ham/VoiceInputPanel.cpp` | `CreateSongSelectGrammar` deref'd `TheSpeechMgr` without nullcheck → crash on entering song_select_screen in headless (no SpeechMgr instance) | Added null guard before `->Enabled()` |
| `src/lazer/meta_ham/ShellInput.cpp` | (See above for the in-block renames.) Also wrapped the Kinect/cursor/skeleton subsystem polls under `#ifndef HX_NATIVE` because their `Poll()` bodies dereference DataNode state that isn't initialized in headless. | `#ifndef HX_NATIVE` skip of `mCursorPanel/mDepthBuffer/mSkelIdentifier/mSkelChooser/mSkelExtTracker->Poll()` |

After all of the above: engine boots, navigates attract → title → main → choose_mode →
song_select → loading → game_screen, plays gameplay for ~9000 frames, captures
gameplay telemetry on player 0, and the test fails on real data.

### After the Kinect-block bypass: new init-time crash (resolved)

Skipping the Kinect/Cursor/Depth/Skel polls under `HX_NATIVE` got past the `SkeletonChooser`
crash, but the engine now hits a SIGSEGV during early init *before* the main loop runs.
Crash address looks libc-internal (`0x7f352a773e7d`), suggesting a corrupted callback or
uninitialized state from somewhere else recent.

```
DC3 Native: Archive loaded, 10 ark files
...
AudioDevice: initialized — 44100 Hz, 2 channels, period 512 frames, gain 1.1

DC3 Native: Caught SIGSEGV (signal 11) at address 0x7f352a773e7d
```

This is the same pre-init crash a prior session also hit (seen in the conversation
summary). It's reproducible across runs, so it's a real regression — not flaky.

### 2026-05-15 deeper dive: `CharIKFoot` is the real foot-IK and it never runs

After confirming `HamIKEffector` polls have `constraintCount=0`, I checked whether a
separate IK system handles foot planting. There IS one:

- `src/system/char/CharIKFoot.cpp` — extends `CharIKHand` → `CharServoBone` → `CharPollable`.
- `CharIKFoot::Poll()` runs a foot FSM with states "in air" / "planting" / "blending",
  and explicitly tests `tf.v.z < f10` against a ground threshold (line 110).
- Tracked by `HamRegulate::mLeftFoot` / `mRightFoot` (`HamRegulate.h:49-50`).
- Found via `sDir->Find<CharIKFoot>("left.ikfoot", false)` in `CharClipDisplay.cpp:197`.

Added a one-shot `DC3_IK_DIAG CharIKFootPoll` log at the top of `CharIKFoot::Poll`.
**It NEVER fires during the entire gameplay run.**

So the actual foot-planting IK isn't being polled at all. Either:

- (a) `main.milo` doesn't contain `*.ikfoot` instances (extracted assets don't have
  any `*.ikfoot` files, but they could be inside the binary .milo bundle).
- (b) The objects exist but `PollEnabled()` returns false → they get sorted into
  `mEnters` rather than `mPolls`.
- (c) They live in a subdir whose `Poll()` isn't wired up.

This is now the strongest lead. The empty `mConstraints` story is a *symptom* — the
actual foot-planting IK (`CharIKFoot`) is the system DC3 uses, and it's silent.

### Suggested next steps

1. **Decide on native build strategy.** The recent upstream-port commits assume Kinect
   is fully initialized. Either:
   - (a) Stub out / guard the Kinect/gesture pipeline more aggressively for headless
     (the path I started — null guards on the per-frame polls), then surface a real
     stub `DataNode` for `SkeletonChooser`'s lookups; or
   - (b) Add a `#ifdef HX_NATIVE` early-return in `ShellInput::Poll` for headless mode,
     skipping the Kinect block entirely; or
   - (c) Revert / re-port `ShellInput::Poll` so it doesn't depend on Kinect being live.

2. **Once the build runs again**, re-run the failing test and re-check the
   `DC3_IK_DIAG IkSnap` log for `constraintCount`. If still 0, the empty-mConstraints
   theory is confirmed and we proceed with finding the runtime populator. If now > 0,
   the recent IK fixes addressed it and we just need to confirm the test passes.
