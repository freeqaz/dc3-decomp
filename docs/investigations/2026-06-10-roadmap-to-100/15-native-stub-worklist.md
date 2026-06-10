# 15 — Native Stub Worklist (live boot, Wave 2 Lane A)

**Date:** 2026-06-10. **Lane:** Wave 2 Lane A (native boot unblock + live stub worklist).
**Branch:** `wave2/a-native-boot`. **Source:** doc 93 §Lane A item 3 (roadmap N.2 payoff).

This is the live ranked stub worklist produced after unblocking the headless boot.
Before this lane, `dc3-native` crashed during `App` construction
(`CameraManager::RandomizeCategory`) before the HTTP server bound, so `/api/stubs`
was unreachable. With the boot crashes fixed (see §Crash fixes below) the engine
now boots through the full attract flow to `main_screen` and the HTTP debug server
serves `/api/health` and `/api/stubs` from a real run.

## How it was captured

```bash
# from orig-assets/ (engine resolves assets relative to CWD)
DC3_HTTP=1 DC3_HTTP_PORT=9099 DC3_STUB_TRACE=1 \
  DC3_STUB_TRACE_DUMP=/tmp/stubs.json \
  MILO_HEADLESS=1 DC3_FAST_BOOT=1 MILO_FATAL_FAILS=0 \
  native/build/dc3-native
# then, while it runs:  curl localhost:9099/api/stubs
```

`/api/health` returned `{"ok":true,"data":{"status":"ok","frame":1,...}}` and
`/api/stubs` returned live ranked counts. A new `DC3_STUB_TRACE_DUMP=<path>` env var
also writes the ranked worklist to a file from the crash signal handler, so the
boot-path hits are captured even though a downstream audio bug (`Sound::SynthPoll`,
see below) currently terminates the process shortly after `main_screen` is reached.

## Boot reach

The engine advances through the real boot flow with `DC3_FAST_BOOT=1`:

```
attract_screen -> autosave_warning_screen -> title_screen
              -> wait_main_after_saveload_screen -> main_screen (panels: meta,
                 background_panel, main_panel, main_menu_wait_for_content_panel)
```

It also instantiates `store_main_screen` panels. So the worklist below reflects the
boot/attract + main-menu enter path. A full gameplay-session capture (load a song)
is not yet reachable — it is gated on the `Sound::SynthPoll` audio bug below.

## Ranked stub worklist (live boot to `main_screen`)

| Rank | Stub symbol | Hits | Class | Notes |
|-----:|-------------|-----:|-------|-------|
| 1 | `OutputDebugStringA` | 94 | XDK debug | Xbox debug-print shim. Pure no-op; every `OutputDebugString` call routes here. Highest count but ZERO functional impact — safe to leave stubbed (or wire to stderr for parity). **Not worth implementing.** |
| 2 | `vorbis_synthesis_poll` | 69 | Audio (Vorbis) | Hit every audio poll. This is the audio/Vorbis synthesis poll path — the same subsystem that currently aborts in `Sound::SynthPoll` (`free(): invalid pointer`). **Highest-value real stub:** implementing/finishing this unblocks audio and likely the gameplay path. Fix order #1. |
| 3 | `DmGetSystemInfo` | 1 | XDK devkit | Devkit system-info query, called once at boot. Returns 0. Low impact. |
| 4 | `DmMapDevkitDrive` | 1 | XDK devkit | Maps the devkit drive, called once at boot. Returns 0. Low impact. |

Total: **165 hits across 4 distinct stubs** at the crash point (`main_screen`).

The list is short because the boot terminates early (at `main_screen` enter) on a
downstream audio bug; it does not yet exercise gameplay. As that bug and the ones
after it are fixed, re-capture to extend the worklist toward the gameplay path
(which is where the audit expected the high-value stubs).

## Recommended fix order

1. **`vorbis_synthesis_poll` + `Sound::SynthPoll`** — the audio subsystem is the
   live blocker. `Sound::SynthPoll` (`src/system/synth/Sound.cpp:174`) has an
   iterator-misuse double-free: it captures `cur = *it`, does `it++`, then
   `mSamples.erase(it)` — erasing the element AFTER `cur` (and `erase(end())` when
   `it` reaches the end), which is the `free(): invalid pointer` that aborts the
   boot at `main_screen`. Fixing this (and wiring `vorbis_synthesis_poll`) is the
   gate to a gameplay-session stub capture. **Out of this lane's scope** (audio
   subsystem, not one of the two named boot crashes) — filed here as the next blocker.
2. **XDK devkit stubs** (`DmGetSystemInfo`, `DmMapDevkitDrive`, `OutputDebugStringA`)
   — leave stubbed; they are debug/devkit shims with no functional payoff.

## Crash fixes that unblocked this (Wave 2 Lane A)

Two pre-existing crashes (doc 92 follow-up #3) blocked the boot; both are fixed at
root with the PPC match preserved (all touched functions verified via
`run_objdiff`). A third crash, exposed only after the first fix, shares the same
root cause and was fixed by the same root change.

1. **`CameraManager::RandomizeCategory`** (the App-construction crash) and
   **`FlowPickOne::Activate`** (the first-UI-poll crash exposed after #1) — both
   indexed a `std::vector` / `ObjPtrVec` with the result of `Rand::Int(low, high)`.
   `Rand::Int` lowers `Int() % (high - low)` with a **signed** divide on PPC
   (`divw`; confirmed in the target asm), and `Int()` reinterprets its unsigned
   table draw as a signed int, so a top-bit-set draw yields a **negative**
   remainder — an out-of-range index. Benign scratch-heap indexing on the Xbox;
   heap corruption + an ObjRef-ring `SIGSEGV` on the host. **Root fix** in
   `src/system/math/Rand.h` `Rand::Int(int, int)` under `HX_NATIVE`: fold the same
   single draw into `[low, high)` with **unsigned** modulo. PPC path byte-unchanged
   (`RandomizeCategory`, `RandomInt`, `FlowPickOne::Activate` all unchanged at their
   prior `run_objdiff` percent). Fixes every index-from-random caller, not just
   these two — chosen over per-callsite clamps per project policy.

2. **`CharBones::ScaleDown`** — forms one-past-the-end iterator/bound pointers
   (`&mBones[mCounts[TYPE_END]]`, index == `mBones.size()`) used purely as loop
   bounds, never dereferenced. Hardened libstdc++ aborts on the out-of-range
   `operator[]`. **Fix:** address with `mBones.data() + index` (bounds-check-free,
   PPC-identical — the matched `ScaleAddIdentity` already uses this idiom).
   `ScaleDown` stays 100% normalized; neighbors (`ScaleAdd` 98.2%, `RotateBy`
   88.4%, `Blend` 100%) unchanged.

Regression tests pinning all of the above: `native/tests/test_native_boot_crashes.cpp`
(7 tests, all green).

## Next downstream blocker (for the record)

After both targeted crashes (+ the FlowPickOne root-share) are fixed, the boot
reaches `main_screen` and then aborts in `Sound::SynthPoll` (`free(): invalid
pointer`, the iterator-erase bug above). That is a fourth, audio-subsystem bug
outside this lane's two-crash scope; it is the gate to a gameplay-path stub capture
and is the top item in the fix order.

---

# Wave 3 Lane A — `Sound::SynthPoll` fixed; boot now reaches `game_screen` (gameplay)

**Date:** 2026-06-10. **Lane:** Wave 3 Lane A. **Branch:** `wave3/a-gameplay-feet`.
**Worktree:** `/home/free/code/milohax/wt-wave3-a-gameplay-feet`.

The `Sound::SynthPoll` blocker named above is **FIXED** (see §SynthPoll fix). With it
gone, `dc3-native` boots cleanly through the **entire** UI chain to **`game_screen`**
and runs `state=playing` with `worldLoaded=1 venuePresent=1 doSongAnim=1` and live foot
telemetry — strictly further than the Wave-2 `main_screen` frontier.

## New boot reach (Wave 3)

Driven by `scripts/dc3-input-flows/ymca.txt` (headless, `DC3_FAST_BOOT=1 DC3_FAST_TIME=1`):

```
attract_screen -> autosave_warning_screen -> title_screen
  -> wait_main_after_saveload_screen -> main_screen -> choose_mode_screen
  -> song_select_screen -> multiuser_screen -> loading_screen
  -> preloading_screen -> real_loading_screen -> game_screen   (state=playing)
```

Clean `EXIT=0` over 5,000 frames (no SIGSEGV/SIGABRT). Verified twice (one-shot run +
a live background run polled over HTTP).

## How the gameplay table was captured (live, at `game_screen`)

```bash
cd native/build
DC3_DATA=/home/free/code/milohax/dc3-decomp/orig-assets \
  DC3_HTTP=1 DC3_HTTP_PORT=9093 DC3_FAST_BOOT=1 DC3_TEL=1 DC3_STUB_TRACE=1 \
  MILO_HEADLESS=1 DC3_FAST_TIME=1 MILO_FATAL_FAILS=0 MILO_MAX_FRAMES=100000 \
  MILO_INPUT_SCRIPT=.../scripts/dc3-input-flows/ymca.txt ./dc3-native &
# once /api/telemetry shows screen=game_screen state=playing:
curl localhost:9093/api/stubs
curl localhost:9093/api/health    # {"ok":true,"data":{"status":"ok","frame":2567,...}}
```

(Worktrees have no reflinked `orig-assets`, so `DC3_DATA` must point at the main repo's
copy. The binary's own cwd-relative fallback only works from the main repo.)

## Ranked stub worklist (live, boot -> `game_screen` gameplay)

| Rank | Stub symbol | Hits | Class | Notes |
|-----:|-------------|-----:|-------|-------|
| 1 | `OutputDebugStringA` | 2011 | XDK debug | Debug-print shim. No-op, zero functional impact. Not worth implementing. |
| 2 | `vorbis_synthesis_poll` | 290 | Audio (Vorbis) | Hit every audio poll. Now that `Sound::SynthPoll` no longer crashes, this is the remaining audio stub — still the highest-value REAL stub if/when audio output is wanted. Boot is no longer gated on it. |
| 3 | `NuiIdentityAbort` | 1 | Kinect (NUI) | **NEW on the gameplay path** (absent from the boot-only table). Kinect identity-tracking abort; called once when entering gameplay. Fake-Kinect/headless skeleton path. Low impact (returns once). |
| 4 | `DmGetSystemInfo` | 1 | XDK devkit | Devkit system-info, once at boot. Low impact. |
| 5 | `DmMapDevkitDrive` | 1 | XDK devkit | Maps devkit drive, once at boot. Low impact. |

Total: **2,304 hits across 5 distinct stubs** through a full boot->gameplay session.
The only NEW stub the gameplay path exercises beyond the boot table is `NuiIdentityAbort`
(Kinect). No new high-value functional stub appeared — the audit's expectation that
gameplay would surface many new stubs is **not borne out**; the gameplay path is served
by the same handful, dominated by no-op debug/devkit shims. The one real candidate
(`vorbis_synthesis_poll`) is unchanged from boot.

## `Sound::SynthPoll` fix (the named Wave-2 blocker)

Two real decomp bugs, both found by reverse-engineering the Xbox asm for
`?SynthPoll@Sound@@UAAXXZ` (`run_objdiff`, full instruction listing), pinned with a new
gtest (`native/tests/test_sound_synthpoll.cpp`) that crashes/fails on the old code:

1. **`mSamples` wrong-erase / `erase(end())` (the double-free).** Source did
   `cur = *it; it++; if (cur->DonePlaying()) mSamples.erase(it);` — erasing the element
   AFTER `cur`, and `erase(end())` when `cur` was the last node. The Xbox asm saves the
   pre-increment iterator (idx 57 `mr r29,r31`, idx 59 advance) and erases THAT node
   (idx 78 `stw r29,0x50,r1` -> erase). **Fix:** save `auto curIt = it;` before `it++`
   and `mSamples.erase(curIt)`.
2. **`mDelayArgs` delayed-play forwarded `this` as the event receiver.** Source passed
   `this` as `Play(...)`'s `obj`; the target loads `cur->mEventReceiver` (idx 26
   `lwz r7, 0xc, r31` — `obj` is the 4th arg in r7 with 3 leading float args).
   **Fix:** `Play(cur->mVolume, cur->mPan, cur->mTranspose, cur->mEventReceiver, 0)`.
   Also cached `DelayArgs *cur = *it;` (target reads all fields from one register) and
   used the `mDelayArgs.erase(it++)` idiom (target advances before erase) — both close
   the remaining lowering diffs.

**PPC match (measured in this worktree via `run_objdiff`): 79.3% -> 91.7% normalized**
(+12.4). Behavioral bugs eliminated; residual is FPR/scheduling noise (commutative
`fadds`, fader GetVal stack-slot order, SetPan/SetSpeed scheduling). Final certification
is on main post-merge. The PPC `#else` path is unaffected — these are pure source-logic
corrections that improve BOTH host behavior and the PPC match (a real decomp bug, not an
HX_NATIVE guard).
