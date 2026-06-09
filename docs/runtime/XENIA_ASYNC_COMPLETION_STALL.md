# Xenia DC3 — Async-Completion Stall (all-black render)

**Status:** OPEN. Game boots and runs but renders nothing (zero GPU draws).

> **2026-06-01 UPDATE — the original APC hypothesis below was tested and DISPROVEN for
> this stall.** A runtime-confirmed diagnosis (see "2026-06-01 findings" section) shows the
> loader thread is NOT in any alertable wait — it is **spin-polling a critical section
> ~1.88M times** with an **unresolved import thunk** in play. The Linux alertable-wait/APC
> fix was built, unit-tested, and applied anyway (it fixes a real latent signal-safety bug
> and is worth keeping), but it does **not** change this stall: baseline and post-fix runs
> are byte-for-byte identical at the stall point. Read the 2026-06-01 section FIRST; the
> material below is the (now-falsified) original narrative, kept for history.

Date: 2026-05-31. Xenia repo: `/home/free/code/milohax/xenia`, branch
`headless-vulkan-linux`. DC3 target: `orig-assets/debug.xex`.

---

## TL;DR

1. **Environment was broken first:** NVIDIA 595 (kernel module) / 610 (userspace) mismatch
   killed Vulkan. Fixed without reboot:
   `sudo modprobe -r nvidia_drm nvidia_modeset nvidia_uvm nvidia && sudo modprobe nvidia nvidia_uvm nvidia_modeset nvidia_drm`
   (display is on the AMD iGPU, so the 3090s unload cleanly).
2. **Movie regression FIXED + COMMITTED** (xenia `6323f54ac`): the game was freezing at boot
   on a fatal `Movie.cpp:220 TheMovieSys.IsInitialized()` assert. See
   "What was fixed" below. Game now boots and runs.
3. **Remaining blocker (this doc):** game renders **all-black, zero draws**. It loads ~98 MB
   of ARK data then **stops reading and idles** to timeout. Strong candidate root cause:
   Linux `AlertableSleep`/APC delivery drops the I/O-completion callback, so the loader (or the
   thread waiting on it) never gets the "done" signal and the scene is never built/drawn.

---

## What was fixed this session (committed)

`6323f54ac Fix DC3 boot freeze on fatal Movie.cpp:220 IsInitialized() assert`

- The Apr 2 commit `8cc604e0e` stubbed `Movie::Init` (guest `0x82555678`) to a bare `blr`.
  `Movie::Init()` is just `{ TheMovieSys.Init(); }` — the boot's only entry into movie-system
  init. The stub skipped it, so `MovieSys::isInitalized` stayed false and the attract movie's
  `MILO_ASSERT(TheMovieSys.IsInitialized(), 0xdc)` (`0xdc`=220) in `Movie::BeginFromFile`
  (src/system/movie/Movie.cpp:91) fired fatally at boot.
- Fix (in `src/xenia/emulator.cc`, CompleteLaunch patch block ~line 3289): leave `Movie::Init`
  intact; change the `BinkMovieSys::Init` (`0x82E214A8`) stub from `blr` to
  `li r0,1; stb r0,4(r3); blr` (set `isInitalized`, skip the hanging `BinkStartAsyncThread`).
  `MovieSys` layout: vptr@0, `isInitalized`(bool)@4, `r3`==`this`.
- **Do NOT fully un-stub `BinkMovieSys::Init`** — its body has
  `MILO_ASSERT_FMT(BinkStartAsyncThread(...))` and the async-thread stub may return 0 → a *new*
  fatal assert.

Before this fix every captured frame was byte-identical (the red error screen, `md5 f96b78e6…`).
After it the error screen is gone and the game runs — but renders black (see below).

---

## The remaining blocker — evidence

Run: `scripts/dc3-input-flows/xenia-ymca.txt` flow, `--gpu=vulkan --dc3_ik_telemetry=true`
(IK telemetry auto-enables `--dc3_enable_gameplay_bootstrap`), 240 s timeout, capture every 200.

- **Zero draws the entire run.** `FlushDeferredDraws()` only runs
  `if (!deferred_draws_.empty())` (`src/xenia/gpu/vulkan/vulkan_command_processor.cc:1341`) and
  that log line (`FlushDeferredDraws: executing N deferred draws`) **never appears**. No
  `headless_draw_count_ > 0` timing lines either. The game issues no draw calls → black.
- **Not a file/IO failure.** ARKs open (`main_xbox_0..9.ark`); `tid=6` streams real data via
  `NtReadFile` (#1→#500, sequential 64 KB chunks) up to offset ~98 MB of the 938 MB ARK.
- **Loads then stalls.** The last `NtReadFile` is at log line **3055 of 37399**. After that the
  game runs idle — VdSwaps streaming, 19 threads, SIGSEGV=0, **no further reads, no draws** —
  until the 240 s timeout. So it loads initial data, then **waits for something that never comes.**
- `LoadSong` probe shows `char=''`, `venue=''`, `default_outfit=''`, `audio=0` — gameplay
  resources never resolve (consistent with the scene-load never completing).

This matches the long-standing "menu panels show loaded=0 / async loading stalls" symptom from
`docs/runtime/XENIA_HEADLESS_STATUS.md` and memory `project_xenia_timer_blocker`.

---

## Root-cause investigation — the APC / AlertableSleep mechanism

DC3 uses Xbox async file I/O with **APC completion routines** (confirmed: `NtReadFile_entry`
reads `apc_routine_ptr`/`apc_context` and, on completion,
`thread->EnqueueApc(apc_routine, apc_context, io_status_block, 0)` —
`src/xenia/kernel/xboxkrnl/xboxkrnl_io.cc:191`, ~line 237). Note **NtReadFile is synchronous**
(`if (true || file->is_synchronous())`): it reads immediately and never returns STATUS_PENDING,
which is why raw reads progress regardless of APC delivery.

APC delivery chain (host side):

1. `XThread::EnqueueApc` (`src/xenia/kernel/xthread.cc:595`) inserts into `apc_list_`, then
   `UnlockApc(true)` → if pending, `thread_->QueueUserCallback([this]{ DeliverAPCs(); })`
   (`xthread.cc:591`).
2. Linux `PosixThread::QueueUserCallback` (`src/xenia/base/threading_posix.cc:675`) stores the
   callback in a **single** `user_callback_` field and sends a real-time signal
   (`pthread_sigqueue`, `SignalType::kThreadUserCallback`) to the target host thread.
3. `signal_handler` (`threading_posix.cc:1176`), `kThreadUserCallback` case, runs the callback
   **only if gated**:
   ```cpp
   case SignalType::kThreadUserCallback:
     if (alertable_state_) {            // threading_posix.cc:1193  <-- THE GATE
       p_thread->CallUserCallback();    // -> DeliverAPCs() -> runs guest completion APC
     }
   ```
4. `alertable_state_` (thread_local, `threading_posix.cc:171`) is set true **only** for the
   duration of `AlertableSleep` (172) or an alertable `Wait` (857).

### The bugs

- **Dropped-APC race (primary suspect).** If the completion signal arrives while the target
  thread is **not** inside an alertable wait (`alertable_state_ == false`) — e.g. it's running
  guest code, or hasn't entered its alertable wait yet — the handler **silently drops** the
  callback (the `if (alertable_state_)` is false, nothing is queued for later). The thread then
  enters its alertable wait expecting the APC to wake it; the signal already came and went, so
  it **sleeps the full (often very long / effectively infinite) timeout → stall.**
- **`AlertableSleep` never reports the alert.** `AlertableSleep` (172) is
  `alertable_state_=true; Sleep(d); alertable_state_=false; return kSuccess;`. It (a) does not
  check for already-pending APCs on entry, (b) `Sleep` (156) loops while `errno==EINTR`, so even
  when the signal *does* interrupt the sleep and runs the callback, it **restarts the sleep for
  the remaining time instead of returning**, and (c) always returns `kSuccess`, never `kAlerted`.
  So `XThread::Delay(alertable)` (`xthread.cc:821`) returns `X_STATUS_SUCCESS` instead of
  `X_STATUS_USER_APC` — the guest never learns its wait was interrupted by an APC.
- **Alertable `Wait` (857)** has the same shape: sets `alertable_state_`, waits on the
  condition, never returns a `kUserCallback`/alerted result, never drains pending APCs on entry.

Why reads still progress but the *final* completion stalls: the per-read loop is synchronous
(no APC needed to advance), but the **post-load completion** (loader signalling "all loaded",
or the main thread's alertable wait on the loader) lands in the dropped-APC race and never
fires. Exact dropped signal needs runtime confirmation (below).

---

## Proposed fix (design)

Make Linux alertable waits behave like NT alertable waits: deliver pending user callbacks
(APCs) reliably and return an alerted status.

1. **Make pending callbacks durable, not edge-gated.** Replace the single `user_callback_` +
   `if (alertable_state_)` gate with a per-thread **pending flag/queue** that survives outside
   alertable waits. Options:
   - In `signal_handler`'s `kThreadUserCallback` case, if `!alertable_state_`, set an atomic
     `user_callback_pending_` instead of dropping it.
   - In `AlertableSleep` and alertable `Wait`, **on entry** check `user_callback_pending_` (and
     run `CallUserCallback()`); if anything ran, return `kAlerted`/`kUserCallback` **without
     sleeping/waiting**.
2. **Return early + alerted on delivery.** When the signal interrupts the sleep/wait and a
   callback runs, **return `kAlerted`** (do not restart the sleep). `Sleep`'s `EINTR` restart
   loop must not swallow the alertable interruption — give `AlertableSleep` its own
   `nanosleep`-with-EINTR-returns-alerted implementation rather than calling shared `Sleep`.
3. **Wire `X_STATUS_USER_APC` through.** `XThread::Delay` already maps `kAlerted` →
   `X_STATUS_USER_APC` (`xthread.cc:844`). Ensure the alertable `Wait` path
   (`xobject.cc` Wait → `threading_posix.cc:857`) also returns the alerted result so
   `NtWaitForSingleObjectEx`/`KeWaitForSingleObject` report user-APC to the guest.

Risk: this is core emulator threading — touches every title and every thread. Validate that
non-DC3 paths still behave (suspend/resume, normal waits). Keep the change minimal and guarded
to the alertable path.

---

## How to reproduce / verify

```bash
# 0. Ensure Vulkan works (see TL;DR driver-reload if vulkaninfo fails).
# 1. Build:
cd /home/free/code/milohax/xenia/build && make xenia-headless config=checked_linux -j$(nproc)
# 2. Run the YMCA flow (dangerouslyDisableSandbox / GPU access required):
XENIA=/home/free/code/milohax/xenia/build/bin/Linux/Checked/xenia-headless
DC3=/home/free/code/milohax/dc3-decomp
$XENIA --target=$DC3/orig-assets/debug.xex --gpu=vulkan \
  --dc3_nui_patch_layout=original --dc3_crt_skip_nui=true --fake_kinect_data=true \
  --dc3_ik_telemetry=true \
  --scripted_input_file=$DC3/scripts/dc3-input-flows/xenia-ymca.txt \
  --dump_frames_path=/tmp/xenia-run/frames --headless_capture_interval=200 \
  --headless_timeout_ms=240000 2>&1 | tee /tmp/xenia-run/run.log
```

Signals to watch in `run.log`:
- **Fixed:** no more `Movie ... Line 220` / red error screen; frames are no longer the
  `f96b78e6…` hash.
- **Still broken (current):** no `FlushDeferredDraws: executing N deferred draws` lines; last
  `NtReadFile #` is early in the log; all PNG frames identical black (`md5sum` them).
- **Fix working (target):** `FlushDeferredDraws: executing N` with N>0; frames vary; menus
  render.

### Runtime confirmation of the exact dropped signal (do this first next session)
Use the in-tree GDB RSP server to inspect the stalled loader thread's guest PC and what it's
waiting on. Cvar `--dc3_gdb_rsp_host` (default `127.0.0.1`,
`src/xenia/app/emulator_headless.cc:55`). Attach gdb/lldb, break once reads stop, dump the
thread doing the alertable wait, and confirm it's in `KeDelayExecutionThread`/
`NtWaitForSingleObjectEx` (alertable) on a loader-completion object. That pins the exact APC
that's being dropped and validates the fix target before touching threading code.

---

## Key files & lines

| What | Location |
|---|---|
| Movie fix (committed) | `xenia src/xenia/emulator.cc` CompleteLaunch ~3289; commit `6323f54ac` |
| Zero-draw gate | `xenia src/xenia/gpu/vulkan/vulkan_command_processor.cc:1341` |
| NtReadFile (sync + EnqueueApc) | `xenia src/xenia/kernel/xboxkrnl/xboxkrnl_io.cc:191` |
| EnqueueApc → QueueUserCallback | `xenia src/xenia/kernel/xthread.cc:595,591` |
| DeliverAPCs | `xenia src/xenia/kernel/xthread.cc:619` |
| XThread::Delay (alertable→USER_APC) | `xenia src/xenia/kernel/xthread.cc:821` |
| **AlertableSleep (no-op, kSuccess)** | `xenia src/xenia/base/threading_posix.cc:172` |
| Sleep (EINTR restart loop) | `xenia src/xenia/base/threading_posix.cc:156` |
| QueueUserCallback (single field + RT signal) | `xenia src/xenia/base/threading_posix.cc:675` |
| **signal_handler gate `if (alertable_state_)`** | `xenia src/xenia/base/threading_posix.cc:1193` |
| Alertable Wait (no alerted return) | `xenia src/xenia/base/threading_posix.cc:857` |
| GDB RSP cvar | `xenia src/xenia/app/emulator_headless.cc:55` |
| Guest movie assert | `dc3 src/system/movie/Movie.cpp:91` (`MILO_ASSERT(...,0xdc)`) |

## Related
- `docs/runtime/XENIA_HEADLESS_STATUS.md` — broader headless status, deferred-draw capture arch.
- `.claude/skills/xenia-gameplay/SKILL.md` — run flow & flags.
- memory `project_xenia_movie_init_regression`, `project_xenia_timer_blocker`,
  `xenia-vulkan-rendering`.
- Existing Xbox-pipeline character ground truth (pre-regression):
  `archive/screenshots/xenia-gameplay/09-loading-cutscene-3d-characters.png`.

---

## 2026-06-01 findings — APC hypothesis DISPROVEN; real blocker is a spin-poll on an unresolved thunk

Ran the YMCA flow twice (xenia-gameplay skill), captured a **baseline** (pre-fix) and a
**post-fix** run, and diffed the loader-thread diagnostics. Result: **identical stall in both.**

| Signal | Baseline | Post-APC-fix |
|---|---|---|
| `FlushDeferredDraws: executing N` | 0 | 0 |
| Distinct frame hashes | 1 (black) | 1 (black, same md5 `5ae1b906…`) |
| Last `NtReadFile` line | ~3049 | ~3085 |
| **Invoked** `NtWaitForSingleObjectEx`/`KeWaitForSingleObject`/`KeDelayExecutionThread` | **0** | **0** |
| `RtlEnterCS` / `RtlLeaveCS` counts | ~1.50M / 1.50M | ~1.47M–1.88M |
| Thread 6 frozen PC | `0x825E4794` | `0x825E4794` |
| Thunk `0x83A00964` | all-zero, NOT FOUND, `Indirection=0` | identical |

### What the loader is actually doing (runtime-confirmed)
- **Thread 6 (loader) is NOT blocked in a wait.** The log shows it invoking only
  `NtCreateFile`/`NtReadFile`/`NtSetEvent`/`VdSwap` — **zero** alertable wait calls the entire
  run. The APC/alertable-wait path the original hypothesis blamed is never exercised by this
  stall.
- **It is spin-polling a critical section** ~1.88 million times (`RtlEnterCS=1883723`,
  `RtlLeaveCS=1883734`). Classic busy-wait on a condition that never flips — same family as the
  existing `CDReadDone`/`ChunkStream::Eof` "spin forever" hack (`dc3_hack_pack.cc:2664`) and
  the `UIManager::Init` "Data is not Symbol" spin (`dc3_hack_pack.cc:4236`).
- **Unresolved import thunk:** `Thunk[0x83A00964]` reads all-zeros (expected a
  `44000042 4E800020`= `sc; blr` syscall thunk), `Thunk fn: NOT FOUND by QueryFunction`,
  `Indirection[0x83A00964] = 0x00000000`, `CTR=0x00000000`. The decomp PE has 379 xboxkrnl
  imports but the XEX resolves only ~196 (see `dc3_hack_pack.cc:3845`); some unresolved IAT
  entries are bypassed by host overrides (e.g. the `CriticalSection::CriticalSection/Enter`
  overrides at `dc3_hack_pack.cc:3843+`), but evidently not the one this loader path needs.
- The stall loop disassembly around `0x825E4778`–`0x825E47B0` is a tiny
  `RtlEnterCriticalSection(this+4); (*counter)++; RtlLeaveCriticalSection` body on the object at
  `0x82F5F888` (its memory begins `…58454E00` = "XEN…"). The *caller* loops on it forever.

### Why this was missed originally
The original doc's central claim ("loader blocks in an alertable wait on a dropped APC") was a
**hypothesis that was never runtime-confirmed** — the doc itself flagged the GDB-RSP
confirmation as "do this first," and it was skipped. The dropped-APC mechanism is real and the
fix for it is correct (see below), but it is **orthogonal** to this stall.

### Status of the APC/alertable fix (KEEP, but it is not this fix)
Applied to `threading_posix.cc` (+ 2 caller fixes). It is a genuine latent-bug fix:
- Signal handler now async-signal-safe (atomic flag + `eventfd` write only); APCs are durable
  (FIFO deque); alertable `Sleep`/`Wait`/`WaitMultiple`/`SignalAndWait` return
  `kAlerted`/`kUserCallback` → `X_STATUS_USER_APC`, matching NT semantics on Linux for the
  first time. Non-alertable paths unchanged.
- **Unit tests:** `xenia-base-tests "[thread]"` → all pass (76 assertions, 6 cases), including a
  new `Test Alertable Wait Returns kUserCallback` (infinite-wait-breaks-on-APC, finite early
  return, pre-entry durability) and the unchanged suspend test.
- **Caller regressions caught by adversarial review and fixed:**
  `xam_net.cc:NetDll_WSAWaitForMultipleEvents` (retry loop now re-issues on `X_STATUS_USER_APC`,
  was only on the never-produced `X_STATUS_ALERTED` → silent fake `WSA_WAIT_EVENT_0`);
  `xam_msg.cc:XMsgCancelIORequest` (made the cancel-completion wait non-alertable so it can't
  return early on a pending APC).
- **Decision pending:** keep on-branch as an independent improvement, or revert to keep the
  branch minimal. It does broaden cross-title alertable behavior (intended per NT, but
  unvalidated against other titles).

### Real next step (supersedes the old "Proposed fix")
Pin and resolve the spin: identify the guest function that owns the `0x825E4794` loop and what
condition it polls, and which unresolved import (`0x83A00964` indirection) it needs. Likely a
new entry in the `dc3_hack_pack.cc` import-stopgap / host-override table (the established
pattern for the ~183 unresolved xboxkrnl imports), NOT a core-threading change. Use the GDB-RSP
server (`--dc3_gdb_rsp_host`) to read the caller's stack and the polled flag, or trace which
`RtlEnterCriticalSection` callsite at `0x825E47xx` is hot.

---

## 2026-06-02 (cont.) — All-black fixed; render reaches game_screen; gameplay crash root-caused

The all-black / async-stall blockers above are RESOLVED for the render path. DC3 now boots
**deterministically** to `game_screen` under headless Xenia and renders all 9 screens (986 deferred
draws). Two distinct problems now gate a *playing song* (the telemetry goal):

### #2 Boot determinism — SOLVED (commit `5084c6acd`, headless-vulkan-linux)
`MoviePanel::IsLoaded` (0x82E0EFE8) → true + a gated `UIManager::GotoFirstScreen` (0x8277B140)
re-nav to `attract_screen`. Empirically: **4/4 GPU boots reach `wait_screen 'game_screen' SATISFIED`,
0 attract stalls** (was ~4/5 stuck). The old stuck signature was repeated `DC3 Script: observed stuck
UI transition cur=00000000 ... trans='attract_screen'`.

### Gameplay crash — DETERMINISTIC, in the song-anim map, INDEPENDENT of the host beat-drive
At the instant `game_screen` is reached (`load=3 wait=3 paused=1`), tid=6 SIGSEGVs (rc=139). A
beat-drive `mPaused==0` gate was added to xenia `emulator.cc` + `hid/nop/nop_input_driver.cc`
(offsets binary-verified, below); with it the beat-drive correctly does NOT activate while paused —
**yet the crash still happens at the same point** → the crash is NOT the beat/audio-resync path that
was previously hypothesized.

Core-dump forensics (both gated and ungated boots identical: host `rip=0xa0000c44`, target
`rax=0xa0000c40` = uncached-mirror of phys ~0 = garbage fn ptr):
- Xenia x64 ctx ptr = `rsi`, membase = `rdi` (0x100000000). PPCContext: `lr@+0x10, r3@+0x38, r12@+0x80`.
- `r12` = `HamDirector::SongAnimByDifficulty(Difficulty)` (0x82473e58); `r3` = the map.
- Crash is inside `SongAnimByDifficulty` = `return mSongAnims[diff];` →
  `std::map<Difficulty,AnimPtr>::operator[]` (0x82471b28) doing a non-linking indirect branch to
  garbage. (`lr=0x82471b30` is the `__savegprlr_28` prologue-helper return — a red herring.)
- `mSongAnims` is `HamDirector+0x5c`, populated by `HamDirector::SetupAnims()` (HamDirector.cpp:571,
  `mSongAnims[d] = GetPropAnim(d, "song.anim", true)`). Map header node_count=3 but tree nodes at
  0x4b02xxxx held code-like garbage → **song.anim content did not load cleanly headless.**

### The game cannot self-unpause headless (4-agent workflow, binary-verified)
`mPaused=0` is set ONLY by `Game::PostWaitStart` (Game.cpp:342), reached only via `Game::HandleWait`,
gated on `HamAudio::IsReady()`. Audio never goes ready headless (the `.mogg` async-read completions
never land). Every HX_NATIVE escape (audio-fail wall-clock unpause, 120-poll `IsLoaded` timeout,
DC3_FAST_TIME, sync PollStream pump) is `#ifdef HX_NATIVE` → compiled OUT of debug.xex. Time is also
dead (TaskMgr.Seconds ← LiveInput::CurrentMs → dead stream or frozen `__mftb()`).

**Verified offsets** (RB2-DWARF Game layout is WRONG for DC3 — do not use it): TheGamePanel
`0x83117410`, TheGame `0x83116ec8`; GamePanel.mGame +0x38, .mState +0x80, .unkf8 +0xF8;
Game.mMaster +0x50, .mPaused +0x5E, .mTimePaused +0x5F, .mRealTime +0x60, .mHasIntro +0x62,
.mLoadState +0x90, .mWaitState +0xA4; TaskMgr `0x82F64A58` .mTimelines +0x2C, TaskTimeline mTime
+0x10 / mLastTime +0x14, stride 0x1C.

### Convergence + real next step
Both the crash (corrupt `mSongAnims`) and the unpause deadlock (audio never ready) trace to the SAME
durable root cause: **headless async file-load completion doesn't land**, so song content (anim/move
data + audio stream) loads incompletely. For REAL telemetry the `song.anim` MUST load (it *is* the
skeleton animation we want to capture) → there is no host-poke shortcut to valid ground truth. The
real next step remains: resolve the async-completion stall (the `0x825E4794` spin / unresolved import
thunk `0x83A00964`) so `.mogg`/`.milo` reads complete — then audio reaches `IsReady`, the game
unpauses itself, `SetupAnims` populates a valid map, and the dancer animates. A binary-verified
5-step host-nudge (wait=0, unkf8=0, mRealTime=1, mPaused=0, host-drive TaskMgr clock) exists as a
fallback to force the unpause, but it is moot until the song-anim crash (content load) is fixed.

---

## 2026-06-09 — REFRAME: the async stall is SOLVED; the dance ANIMATES; live Xbox bone telemetry achieved

> **This supersedes the entire doc above.** The "import thunk `0x83A00964` / song-load CS spin"
> framing was a **misdiagnosis** (see the CORRECTION block at the end of this section). The async
> file-load completion stall is already solved in the working tree; DC3 boots, renders, plays the
> song, and the dancer animates under headless Xenia. We now read live Xbox IK/foot telemetry.

Re-ran the YMCA flow on the current `xenia-headless` (built Jun 5, includes ~2,211 lines of
uncommitted working-tree work beyond commit `5084c6acd`). Log: `/tmp/xenia-stall-baseline/run.log`
(88,925 lines; clean exit rc=0 at the 200 s timeout). **The picture has moved a lot vs the
2026-06-02 section above — that section is now partially STALE.**

What is now WORKING (was broken before):
- **NtReadFile reaches #2000** (vs reads stopping at ~log line 3055 before).
- **`FlushDeferredDraws: executing N` fires 37×** (was 0) → real GPU draws.
- **Full screen flow reached**: `title → main → choose_mode → song_select → game_screen`
  (all `wait_screen '…' SATISFIED`).
- **gpState=2, paused=0, load=3, wait=0 (PLAYING)** for ~130 beats; host beat-drive advances
  `sec≈133→139, beat≈266→287`; then flips to `gpState=3 paused=1 realTime=1` (song-end/restart).
- **No SIGSEGV** the entire run (the old `SongAnimByDifficulty` crash is GONE).
- **74 captured frames, all-distinct md5** (was 1 identical black frame). IK telemetry is LIVE
  (`DC3:IK CLAMP/EFF/BONE/OUT` fire every gameplay frame).

**THE DANCE ANIMATES — live Xbox foot ground truth captured.** The `DC3:IK CLAMP` telemetry
advances smoothly **frame 990 → 5280** (every 30 frames) with the ankle position moving in
venue-world every sample. The Xbox raw-animated ("neutral") **ankle Z trajectory** over the dance:
median **0.049** (= floor; `groundHeight=0`), range −0.35 → 10.6, with **23/144 samples slightly
below floor**, 85 near floor [0,0.6], 30 lifted (dance steps). Verdict tally: 288 `PLANTED(z~floor)`,
143 `PLANTED(neutral!=eff)`. The foot-plant **IK is near-inert on Xbox** — `clampF=0.0000` on every
sample (the clamp only engages for feet lifted >5u). **This matches native** (native: ankle raw-pose
at floor, IK inert/discarded) → confirms the native pose pipeline is faithful at the ANKLE.

CORRECTION to my first read of this run (do not repeat the error): the
`bone_*.mesh world=(0,0,5.00xx)` "BONE" telemetry is a **broken read** (wrong field offset / it
returns a near-constant value while the SAME object's effector read animates), NOT a frozen
skeleton. The skeleton is fully posed and dancing. Likewise the `RtlEnterCS` 32M count + thunk
`0x83A00964` "all-zeros" are NOT a song-load blocker:

### CORRECTION — the import-thunk/song-load framing was WRONG (5-agent recon + symbol confirmation)
- **`0x83A00964` is guest BSS/data, not a thunk** (it's above the whole XEX image
  0x82000000–0x83250000; in the `0x83A00000–0x83B20000` BSS scan range). The "all-zeros / NOT
  FOUND / Indirection=0" came from a **buggy diagnostic** (`xenia emulator_headless.cc:1444-1470`)
  that deref's a host pointer for a guest VA and `QueryFunction`s a data address. `RtlEnterCriticalSection`
  (ord 0x125) is fully resolved (IAT `0x82000900`, thunk `0x82EE5884`) and implemented
  (`xboxkrnl_rtl.cc:471`). **Do NOT write an import override or touch threading/APC code.**
- **The `0x825E4794` spin = the Kinect `SkeletonUpdate` gesture poll, not the file loader.**
  `0x825E4794` is inside `CriticalSection::Enter` (0x825E4778, confirmed in symbols.txt); the CS
  object `0x82F5F888` is `SkeletonUpdateHandle::sCritSec` (confirmed). It's the `App` main-loop
  per-frame `SkeletonUpdateHandle` RAII waiting on `SkeletonUpdate::sSkeletonUpdatedEvent`
  (`0x82F5F884`), set only after `NuiSkeletonGetNextFrame` (real Kinect) — never fires headless.
  The 32M Enter/Exit count is genuine guest-loop churn (gesture subsystem), **and it does NOT stop
  the dance from animating** (the dance plainly advances 990→5280). It's CPU waste / cosmetic, not
  the gate. Same family as already-landed fix `07b11d791`.
- **The async file-load stall is SOLVED in the uncommitted working tree** via the `merge_busy`
  HOLD gate (`emulator.cc`: latch `TheFileMergerOrganizer` `*0x82f5ef44`→`mActiveOrg +0x38`, hold
  the loading→game_screen transition until merge done, then real `GotoScreen`, then unpause nudge
  in `nop_input_driver.cc`) + supporting patches (`Game::PauseForSkeletonLoss 0x82866D50→blr`,
  `HamDirector::SongAnim 0x82475578→b 0x82473E58`, `SaveLoadManager::Activate 0x82894A10→blr`).
  My run confirms it: `merge_busy=0 seenBusy=1` throughout, no crash, song plays.

**Real remaining gap (small):** the only missing datum is the **Xbox TOE Z** (and knee) — the toe
is not an IK effector so it's not in the CLAMP data, and the `.mesh` bone read is broken. Fixing
that read (correct offset/instance for `bone_*-toe.mesh` / `bone_*-knee` world Z) gives the decisive
Xbox-vs-native toe comparison: if Xbox toe ≈ 0 (flat foot) while native toe ≈ −4 with matching
ankles, the native bug is a **foot/ankle-rotation or knee** divergence, not ankle height. See the
feet investigation doc (`docs/sessions/2026-06-09-xenia-xbox-foot-truth.md`).

**Optional polish (not blocking):** tame the gesture spin by stubbing `SkeletonUpdate::PostUpdate`
(live `0x8242E2B0`) / `InstanceHandle` (`0x8242CDB0`) — the in-tree gesture stubs target WRONG
addresses for `debug.xex` (manifest is decomp-layout; fallback literals like `0x827D0EC0` don't
exist in `debug.xex`). Live addrs: `GestureMgr::Poll=0x82428F40`, `GetSkeleton=0x824293F8`,
`UpdateTrackedSkeletons=0x82429810`. Re-pin to these only if the CPU churn matters.
