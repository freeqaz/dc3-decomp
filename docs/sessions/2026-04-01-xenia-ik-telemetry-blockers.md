# Xenia IK Telemetry — Blockers & Status (2026-04-01)

## Goal
Extract IK bone position telemetry from the original DC3 Xbox 360 binary running in Xenia to understand how the original game keeps feet above the floor. The native port has feet clipping through the floor due to a dirty cascade bug. We need Xbox ground truth to design the correct fix.

## What Works
- **Full menu navigation**: attract → title → main → choose_mode → song_select → multiuser → game_screen (~18s)
- **Movie::Poll stubbed** at `0x82555CB8` (`li r3, 0; blr`) — attract_screen bypasses instantly
- **Controller mode enforced** — GestureMgr.mInControllerMode forced true every NUI frame
- **Song selection works** — DOWN×3 + A at song_select_screen, screen-aware script navigates properly
- **Host-side timer advancement** — writes tick deltas to TaskMgr and LiveInput Timer::mCycles from NUI handler
- **Beat advancing** — goes from -4.86 to 276+ over the run
- **Game state**: kGamePlaying (gpState=2), mPaused=false, waitState=0, pollEnabled=1
- **Bone dirty probe** — reads mDirty from guest memory via RTTI-based object traversal
- **IK telemetry infrastructure** — PPC code caves capture totalWeight, groundHeight, effectorType, thisPtr, bone WorldXfm + mDirty
- **ExitControllerMode stubbed** at `0x82902748` (blr)
- **CDReadDone stubbed** at `0x826026E0` (`li r3, 1; blr`)
- **ContentMgr::RefreshDone stubbed** at `0x825FEB48` (`li r3, 1; blr`)

## The Blocker: Animation Pipeline Not Running

### Symptom
Characters are loaded (bone objects found in guest memory), but they stay in rest pose. Only one transient hand-type IK hit captured at frame 1080. No ankle IK data at all. Dance animation doesn't play.

### Root Cause: PostWaitStart Never Fires
The animation pipeline initialization flows through `Game::HandleWait()`:
- **State 3** (PostWaitRestart): checks `mHasIntro` and `Seconds(kRealTime) < 0`, then falls through to `PostWaitStart()`
- **PostWaitStart()**: checks `mMaster->GetAudio()->IsReady()`, then calls `mAudio->Play()`, sets `mPaused=false`, initializes the song sequence

`PostWaitStart()` never runs because `HamAudio::IsReady()` returns false:
- The Xbox binary uses `XboxSynth` which creates `StandardStream` (not `StreamNull`)
- `StandardStream` stays in `kInit` state because Vorbis decode needs XAudio2 hardware
- Despite patching `HamAudio::IsReady+0x70` (replacing `bctrl` with `li r3, 1`) and patching `HandleWait+0x090` (bne→b to bypass IsReady check), `audioReady=0` persists in diagnostics
- HandleWait stays stuck in state 3 for ~20s until the safety-net forces `mWaitState=0` at frame 3000

Without PostWaitStart:
- `mAudio->Play()` never called → song audio never starts
- `TheSongSequence.OnSongLoaded()` never called → song sequence not initialized
- `TheHamDirector->SetPollEnabled(true)` from case 5 never called (though the safety-net forces this)
- `SetupAnims()` from case 5 never called → mSongAnims may be empty

### Why Animation Needs PostWaitStart
The dance animation chain is:
1. `Game::Poll()` → advances beat/seconds via TaskMgr
2. `WorldDir::Poll()` fires `select_camera` → `HamDirector::OnSelectCamera()`
3. `OnSelectCamera()` → `SongAnim(0)->SetFrame(seconds * 30)` — drives the song animation
4. SongAnim frame changes → CharClipDriver evaluates → CharServoBone::Poll() → PoseMeshes writes bone transforms
5. HamIKEffector::Poll() runs IK on animated bone positions

Without PostWaitStart, step 3 fails because `SongAnim(0)` returns null (mSongAnims not populated) or HamDirector.mPollEnabled is false.

### Alternative Initialization Path
For the initial song load (not mid-game LoadNewSong), SetupAnims is also called from:
`HamDirector::Enter()` → `Initialize()` → `SetupAnims()`

This fires when world_panel enters via UIScreen::Enter. It should have run when game_screen loaded. But diagnostics suggest it either didn't fire or the song.anim PropAnim wasn't available at that point.

## Attempted Fixes (in emulator.cc)

| Fix | Approach | Result |
|-----|----------|--------|
| HamAudio::IsReady bytepatch | `li r3, 1` replacing `bctrl` at IsReady+0x70 | audioReady still 0 — possibly JIT cached the old code before patch applied |
| HandleWait bne→b bypass | Unconditional branch past IsReady check | HandleWait still stuck in state 3 |
| Remove mWaitState=0 force | Let HandleWait proceed naturally | Stuck in state 3 forever (audio never ready) |
| mHasIntro clearing | Force mHasIntro=false every NUI frame | Prevents intro check blocking, but PostWaitStart still fails on IsReady |
| Safety-net at frame 3000 | Force mPaused=false, mWaitState=0, mPollEnabled=true | Gets to kGamePlaying but skips PostWaitStart initialization |
| Earlier beat drive | Activate timer writes at frame 1200 | Beat advances but animation doesn't play without PostWaitStart |
| Guest startup calls | Call `HamDirector::SetupAnims`, `SongSequence::OnSongLoaded`, `GamePanel::StartGame` from NUI handler at `nuiFrame=2400` | `gpState` reaches 2, but bones remain frozen and IK stays inactive |
| Host-driven `song.anim` SetFrame | Resolve `SongAnimByDifficulty(kDifficultyExpert)` and call `RndPropAnim::SetFrame(frame, 1.0f)` every NUI frame after `nuiFrame=2400` | No visible effect on guest bone transforms; ankle world Z remains 0.0 |
| Scrub stale `CharDriver` clips | Clear both players' normal-driver `mFirst` clip pointers once gameplay is active | `HamDirector::SongAnimation()` flips from `0` to `1`, proving the old idle clip gate was real, but `song.hdrv` still never gets a clip and bones stay frozen |
| Alias routine-builder anim pointers to expert `song.anim` | Write the expert anim pointer into the routine-builder anim slots on `HamDirector` | No effect; `SongAnim(0/1)` still returned the original routine-builder anim pointers |
| Patch `HamDirector::SongAnim(int)` to expert anim | PPC patch at `0x82475578`: `li r4,2; b SongAnimByDifficulty` | `SongAnim(0/1)` now resolve to the expert anim, but `song.hdrv` still stays empty and bones remain frozen |

## Follow-up Findings (later 2026-04-01 run)

### What the new probes proved
- **The original XEX already reaches `game_screen` and `gpState=2`** under the current Xenia patch set. This is no longer a front-end boot problem.
- **`HamDirector::SongAnim(0)` is non-null before any new host intervention**. On the follow-up run it returned `0x406DE7C0` at `nuiFrame=2400`.
- **`SongSequence::OnSongLoaded()` can be invoked from the host, but it does not unfreeze the rig**. The follow-up log shows:
  - `SongAnim(before)=406DE7C0`
  - `SongAnim(after_setup)=406DE7C0`
  - `SongAnim(after_songload)=406DE7C0`
  - `ExpertAnim=412F6B88`
  - `gpState=2`
- **Directly driving the expert `song.anim` frame also does not move the bones**. The host started calling `RndPropAnim::SetFrame` on `0x412F6B88` at `nuiFrame=2400`, but the bone probe still reported:
  - `bone_L-ankle.mesh worldZ=0.0000`
  - `bone_R-ankle.mesh worldZ=0.0000`
  - unchanged knee Z (`~5.016`)
  - `mDirty=0`
  through `nuiFrame=2640`
- **IK telemetry remained dead**. At `nuiFrame=2400`, telemetry still logged `NO DATA — all slots zero`.

### Narrowed blocker
The remaining failure is now **downstream of song.anim discovery and guest StartGame state**. Even with:
- `mPaused=false`
- `mWaitState=0`
- `mPollEnabled=true`
- `gpState=2`
- a valid `SongAnim(0)` pointer
- a valid expert song.anim pointer
- explicit guest `SetFrame` calls

the character skeleton still does not update. That points to one of these deeper blockers:
- `RndPropAnim::SetFrame` is running but the **clip / prop-key handler chain is not affecting characters**
- the selected anim exists but **does not contain the clip keys needed for gameplay choreography in this original-XEX state**
- **HamDirector / HamCharacter / ClipPlayer poll chain is not active** even after `gpState=2`
- the anim targets or merge products are present but **disconnected from the live character instances**

## Follow-up Findings (later 2026-04-01 run, deeper gating probes)

### What the new probes proved
- **The first choreography gate was real**. At `nuiFrame=2400`, both players had:
  - non-null normal `CharDriver::FirstClip()`
  - null `song.hdrv->FirstClip()`
  - `HamCharacter::SongAnimation() == -1`
  - `HamDirector::SongAnimation() == 0`
- **Clearing the stale normal-driver clips changes the game-state logic exactly as expected**. After scrubbing those clip pointers:
  - `HamCharacter::SongAnimation() == 0` for both players
  - `HamDirector::SongAnimation() == 1`
  - `drvClip=00000000` for both players
  - but `songDrvClip` still stayed `00000000`
- **The routine-builder anim selection was also a real blocker**. A direct guest PPC patch to `HamDirector::SongAnim(int)` made:
  - `SongAnim(0) == SongAnim(1) == ExpertAnim`
  - `SongAnim(before)=ExpertAnim` during the guest startup call path
- **Even with both gates forced open, the live character drivers still never populate**. Across `nuiFrame=2400..2640`, the logs still showed:
  - `songDrvClip=00000000`
  - frozen ankle world Z at `0.0000`
  - unchanged knee Z around `5.016`
  - `IK [nuiFrame 2400] NO DATA`

### Updated narrowed blocker
This is now squarely inside the **`ClipPlayer::Init(...)` prerequisite path**. With:
- `doSongAnim=1`
- `SongAnim(0/1)` forced to the expert anim
- `drvClip=0` on both normal drivers
- valid `song.hdrv` objects present

the guest still never inserts a clip into `song.hdrv`. That means the remaining failure is one of:
- `HamDirector::mClipDir` is null or invalid under Xenia
- `HamDirector::GetMasterKeys("clip")` is returning null / failing to initialize `mMasterClipAnim`
- `RndPropAnim::GetKeys(TheHamDirector, "clip")` on the expert anim is returning null in this original-XEX runtime state
- `ClipPlayer::Init(0/1)` is failing for some other internal reason before `PlayAnims(...)` can run

### Best next probes
- Log `HamDirector::mClipDir` and `mMasterClipAnim` state at the same `Song gate` probe point.
- Instrument the return value of `ClipPlayer::Init(int)` or patch in logging around its `mClipKeys && mMasterClipKeys && mClipDir` gate.
- Probe `RndPropAnim::GetKeys(TheHamDirector, "clip")` directly on the expert anim from Xenia to see whether the clip-key table exists in guest memory.

### Best next probes
- Instrument **`HamCharacter::SongAnimation()`** and/or **`HamDirector::SongAnimation()`** to see whether the game thinks choreography is active.
- Probe **`ClipPlayer` / `CharClipDriver` / `CharServoBone::Poll()`** to see whether any clip evaluation happens after the host `SetFrame`.
- Instrument **`RndPropAnim::SetFrame`** or the relevant prop-key handler dispatch to verify whether clip/move keys fire at all on the original XEX.
- Inspect whether the resolved `SongAnim(0)` / expert anim actually has gameplay clip keys in guest memory, not just camera/shot keys.

## Xenia Infrastructure Details

### Key Addresses (from symbols.txt)
| Symbol | Address |
|--------|---------|
| HamIKEffector::Poll | 0x824C21E8 |
| RndTransformable::SetWorldXfm | 0x82647A70 |
| Character::Poll | 0x82351090 |
| CharBonesMeshes::PoseMeshes | 0x823486E0 |
| Movie::Poll | 0x82555CB8 |
| HolmesClientPoll (telemetry hook) | 0x82631C58 |
| TheUI (UIManager*) | 0x82F1A8E0 |
| TheTaskMgr | 0x82F18670 |
| TheHamWardrobe | 0x82F60110 |

### RndTransformable Memory Layout
- mWorldXfm.v (position xyz): `bone_ptr + 0x6C` (3 big-endian floats)
- mDirty: `bone_ptr + 0xBD` (1 byte bool)

### IK Telemetry Slot Layout (dc3_hack_pack.cc)
```
+0:  float totalWeight
+4:  float groundHeight
+8:  u32   effectorType
+12: float posWeight
+16: u32   thisPtr (Poll r3)
+20: float ikElbowZ
+24: float fancyWeight
```

### Bone Dirty Probe
The NUI handler traverses: `TheHamWardrobe → mMainCharacters[0] → ObjectDir subdir hash table → find bones by name → read mDirty at complete_obj + 0xBD`. Uses MSVC RTTI Complete Object Locator (COL at vftable[-1]) to compute complete object pointer from Hmx::Object vbase pointer (COL offset = 0xC4 for RndMesh).

### Screen-Aware Input Script
`scripts/dc3-input-flows/xenia-ymca.txt` — uses `wait_screen` directives for reliable navigation.

### Build & Run
```bash
cd /home/free/code/milohax/xenia && make -C build -f Makefile xenia-headless -j$(nproc)

cd /home/free/code/milohax/dc3-decomp && /home/free/code/milohax/xenia/build/bin/Linux/Checked/xenia-headless \
  --target=orig-assets/debug.xex --gpu=vulkan \
  --dc3_nui_patch_layout=original --dc3_crt_skip_nui=true \
  --fake_kinect_data=true --dc3_ik_telemetry=true \
  --scripted_input_file=scripts/dc3-input-flows/xenia-ymca.txt \
  --headless_timeout_ms=240000 \
  2>&1 | tee /tmp/xenia-ik.log
```

## 2026-04-01 Follow-Up

### Build Fix
- `xenia-headless` was not linkable in the current tree because `dc3_hack_pack.cc` declared `cvars::dc3_ik_telemetry` but nothing defined it.
- Added `DEFINE_bool(dc3_ik_telemetry, false, ...)` in `/home/free/code/milohax/xenia/src/xenia/emulator.cc`.
- After that change, `make -C build -f Makefile xenia-headless -j$(nproc)` completed successfully again.

### Runtime Regression While Reproducing Song-Gate Probes
- Fresh runs from the rebuilt binary did **not** reproduce the earlier `Song gate` / `nuiFrame` telemetry path.
- With `--dc3_ik_telemetry=true`, boot entered an early exception-heavy path before any NUI-frame logs appeared:
  - `/tmp/xenia_playanims_probe.log`
  - repeated `RtlRaiseException(...)` on worker threads 15-17
  - long-running `VdSwap` loop afterward
- Re-running **without** `--dc3_ik_telemetry=true` still did not reach `Song gate` or any `nuiFrame=` logs during the sampled window:
  - `/tmp/xenia_playanims_noik.log`
  - no `DC3: Host-driven beat activated`
  - no `DC3: Song gate`
  - no screen-aware `wait_screen satisfied` lines

### Current Interpretation
- The immediate blocker is now earlier than the previous clip-driver investigation: the rebuilt Xenia path is not re-entering the NUI/beat-drive instrumentation loop that previously reached gameplay.
- The quickest next diagnostic is to add a one-shot log at first NUI callback entry so we can distinguish:
  1. NUI callback never fires in the current boot path
  2. NUI callback fires, but the later gameplay/beat gates never open
- The existing `ClipPlayer::PlayAnims` probe work is still conceptually the right next downstream test once the run is back to the earlier `Song gate` state.

### Restoration Retest
- Re-read `/home/free/code/milohax/xenia/src/xenia/emulator.cc` and restored the specific missing pieces we had high confidence in:
  - the boot-time `HamDirector::SongAnim(int)` PPC patch (`li r4,2; b SongAnimByDifficulty`)
  - a one-shot `DC3: NUI callback alive` log in the fake skeleton override
- Also repaired a bad merge state in `emulator.cc` so `xenia-headless` linked again:
  - removed a duplicated fake-NUI helper block that had been pasted in below the real one
  - restored the missing `CompleteLaunch(...)` tail (`LaunchModule`, `on_launch`, `return X_STATUS_SUCCESS`)
  - restored the small `Dc3NuiReturn1Extern` helper that the guest-override table still referenced
- After those repairs, `make -C /home/free/code/milohax/xenia/build -f Makefile xenia-headless -j$(nproc)` succeeded again.
- Fresh retest log: `/tmp/xenia_restore_retest.log`

### What the new retest proved
- The fake-Kinect guest override path is alive again:
  - `DC3: NUI callback alive (original layout) frameBuf=40455D80 fakeFrame=1 nuiFrame=1`
- The run still does **not** reach the earlier gameplay path:
  - no `DC3: Host-driven beat activated`
  - no `DC3: Song gate`
  - no `wait_screen satisfied`
- By `nuiFrame=1800`, the game-facing pointers are still effectively uninitialized:
  - `screen='unknown'`
  - `curScreen=00000000`
  - `gpState=-1`
  - `gpPollLoad=-1`
  - `audioFL/audioBuf/audioStream=0`
- The original-XEX fallback patch branch only partly logged:
  - `DC3: Stubbed SaveLoadManager::Activate ...`
  - but **none** of the later expected logs appeared for
    - calibration bypass patches
    - `Movie::Poll` stub
    - audio fixes
    - `Anim fix 8f`

### Updated Interpretation
- We are no longer blocked by “NUI callback never fires.” It does fire immediately on the restored binary.
- The current blocker is now earlier than `game_screen` *and* earlier than the original-XEX gameplay bootstrap path: UI state never becomes valid enough for the screen-aware flow to advance.
- The missing patch logs inside the original-XEX branch are now important evidence. Either:
  1. execution is not reaching that later portion of the branch as expected, or
  2. those specific guest addresses are no longer mapping / matching in this boot state.
- The next high-value check is to log the success/failure of each original-XEX patch address lookup in the `fake_kinect_data && !dc3_is_decomp_layout` block so we can see exactly where execution stops or starts returning null.

## Next Steps to Unblock

### Option A: Fix HamAudio::IsReady at the PPC level
The boot-time bytepatch at `IsReady+0x70` may not be hitting the right return path. Disassemble `HamAudio::IsReady` fully (there may be multiple return paths) and patch ALL of them. Or stub the entire function to `li r3, 1; blr` (2 instructions at function entry).

Find the full IsReady address:
```bash
grep "IsReady.*HamAudio" config/373307D9/symbols.txt
```

### Option B: Call PostWaitStart from the host
Instead of letting HandleWait call PostWaitStart, invoke it directly from the NUI handler by:
1. Finding PostWaitStart's address in symbols.txt
2. Using `RegisterGuestFunctionOverride` to create a callable wrapper
3. Or writing the equivalent state changes to guest memory (set what PostWaitStart would set)

PostWaitStart sets: `mPaused=false`, calls `mAudio->Play()`, calls `SetTimeOffset()`, resets `mWaitState`. The critical missing piece is `mAudio->Play()` which starts the audio timeline. Without audio, the song timing chain might not work. BUT the host is already driving beat/seconds externally, so audio Play() may not be needed — the missing piece might just be the SongAnim setup.

### Option C: Force SongAnim setup from the host
The critical missing initialization is `SetupAnims()` populating `mSongAnims`. From the host:
1. Find the HamDirector object in the WorldDir
2. Find song.anim PropAnims in the merged song data
3. Write the PropAnim pointers directly to `mSongAnims[]`

### Option D: Capture bone positions via GPU frame capture
Instead of IK telemetry, capture a Xenia Vulkan frame and extract the bone matrices from the vertex shader constants. The `--dump_frames_path` flag captures rendered frames. GPU shader constants contain the skin matrices that were uploaded for each mesh draw call. If we can extract these, we get the actual bone positions used for rendering.

### Option E: PPC breakpoint on HamIKEffector::Poll
Use Xenia's GDB RSP stub (`--dc3_gdb_rsp_stub=true`) to set a breakpoint on HamIKEffector::Poll. When it fires, dump register state to see the effector type and bone addresses. This only works if Poll actually fires — which requires animation.

## Related Files
- `/home/free/code/milohax/xenia/src/xenia/emulator.cc` — All DC3-specific patches (~1500 lines)
- `/home/free/code/milohax/xenia/src/xenia/dc3_hack_pack.cc` — IK telemetry PPC code caves
- `/home/free/code/milohax/dc3-decomp/docs/debugging/xenia.md` — Xenia usage reference
- `/home/free/code/milohax/dc3-decomp/docs/sessions/2026-03-25-feet-in-ground-fix.md` — IK dirty cascade investigation
- `/home/free/code/milohax/dc3-decomp/docs/sessions/2026-03-30-host-driven-beat-design.md` — TaskMgr memory layout for beat advancement

## Key Finding from This Session
The user confirmed from retail Xbox screenshots that **feet ARE above the ground** in the original game. This means the dirty cascade either doesn't happen on Xbox (different poll order or different engine behavior) or is compensated by something we haven't found. Getting Xenia IK telemetry remains critical to understanding the difference.
