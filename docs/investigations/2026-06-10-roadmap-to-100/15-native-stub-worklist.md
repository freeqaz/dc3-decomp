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
