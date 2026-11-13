# Web (Emscripten / WASM + WebGPU)

**State (2026-11-13, measured):** builds from a fresh worktree and plays a whole
song in Chromium on a real GPU — boot → menus → tutorial → song select →
loading → gameplay → results — at the browser's 60 fps cap with no page errors.
**Webcam pose input works** (`np-web-pose`): with a fake camera playing a dance
clip the song scores 29,859, a motionless person 7,240, the no-camera stand-in
7,581, and an empty frame pauses the game for a missing player. Details and
caveats: [POSE_INPUT.md](POSE_INPUT.md#web). Since `np-web-build` the default
page is the player page on an `-O3` build, every kXMA SFX has a sidecar, and
the heap peak of five of the six venues is measured. Work items: [PLAN.md](PLAN.md)
track W.

## Build

```bash
scripts/web/build.sh            # --both (default) | --release | --debug | --reconfigure | --opt O2 | --no-sfx
python3 native/web/server.py --port 8420
# http://localhost:8420/            -> release/, player page
# http://localhost:8420/?debug=true -> debug/, developer harness
# http://localhost:8420/?dev        -> release/, developer harness
```

| variant | build dir | flags (`flags.make` / `link.txt`) | wasm | js |
|---|---|---|---|---|
| `release/` (default page) | `native/build-web-release` | `CMAKE_BUILD_TYPE=Release`; dc3-web `-O3 -g0`, linked `-O3 -g0 --profiling-funcs` | 9.3 MB (2.0 MB brotli) | 212 KB |
| `debug/` (`?debug=true`) | `native/build-web` | `CMAKE_BUILD_TYPE=Debug` (engine and imgui unoptimised); dc3-web `-O0`, linked `-O0 -g2` | 28 MB (5.7 MB gzip) | 341 KB |

- emsdk 5.0.2. Both variants from a fresh worktree: 2 min 41 s (both configures
  included; load average ~35 on 32 cores). The release `-O` level is
  `DC3_WEB_OPT_LEVEL` (`--opt`); `-g2` keeps function names only (emcc keeps
  DWARF at `-g3`), and the release keeps them too (`--profiling-funcs`).
- `build.sh` deploys the page first (index.html, favicon, audio-worklet.js,
  `pose/`), then the SFX sidecars, then each variant in its own errexit
  subshell. A failed variant does not stop the other; the script ends with
  `BUILD FAILED: <variant>` and exit 1, and says whether that variant's deploy
  dir still holds a previous build (W-4).
- imgui is a `FetchContent` clone; `build.sh` reuses `$DC3_IMGUI_SOURCE_DIR` or
  the main checkout's `native/build/_deps/imgui-src` at the pinned commit
  (checked from a fresh worktree: no clone).
- Output: `native/web/build/{release,debug}/`, plus `sfx_sidecars/` (below).
- There is no `.data` package: assets stream from `server.py` at runtime
  (`/api/file/...`, ~132 MB and ~240 requests for boot plus one song).
- Requires crossOriginIsolated (COOP/COEP, for the audio worklet's
  SharedArrayBuffer) and JSPI (Chrome/Edge 137+, also on macOS). Safari is not
  supported: Safari 26 has WebGPU but no JSPI, and the page refuses it with an
  error card naming JSPI (`08814d5b1`) instead of failing inside the WASM
  instantiate.
- Symbols unresolved at link become runtime warnings via `missing_stubs.js`
  (163 env imports at `-O3`, 166 at `-O0`, emscripten's own JS functions
  included); `AudioDevice::Suspend`/`Resume` have no web implementation and
  are hit in play.

### Optimisation level (W-3, decided 2026-11-13)

Until `np-web-build` the two variants were swapped against every comment:
`build.sh` configured both dirs `CMAKE_BUILD_TYPE=Release`, whose `-O3` leads
every compile and link line; release appended the then-default
`DC3_WEB_OPT_LEVEL=O0` (emcc honours the last `-O`), debug appended nothing.
So the default page ran `-O0` and `?debug=true` ran `-O3`. The main checkout
never showed it: its `build-web` cache predates `6298c017c` and has an empty
build type.

The `-O0` default came from `6298c017c` (2026-06-08), which copied rb3's
caution ("-O>0 is risky for the matched-fork") with no dc3 failure behind it.
rb3's two `-O>0` faults, checked here:

- **Inline-discard** (rb3: `inline` bodies called cross-TU vanish at `-O>0`
  and become stubbed imports, so `SystemConfig` returned NULL): dc3's `-O3`
  wasm has 163 imports, the `-O0` one 166; nothing new at `-O3` (the three
  `-O0`-only imports are `GetTimeZoneInformation`, `__syscall_fstat64`,
  `emscripten_err`). The desktop build links at `-O2` without unresolved
  symbols, which is why.
- **Per-TU miscompiles** (rb3: `OutfitConfig.cpp`, `MetaPerformer.cpp`): none
  seen. Same song (The Hustle, rollerrink), Chromium 1223, RTX 3090:

| build | input | result | score | ratings | page errors |
|---|---|---|---|---|---|
| `-O3` release | stand-in (no camera) | whole song → results | **7,581** | awesome > bad > ok > bad | 0 (one run hit the splash bug fixed in W-8) |
| `-O0` debug | stand-in | whole song → results | **7,581** | awesome > bad > ok > bad | 0 |
| `-O3` release | fake camera, dance clip | whole song → results | 22,041 (load 60) | 12 changes | 0 |
| `-O0` debug | fake camera, dance clip | whole song → results | 29,354 (load 31) | 22 changes | 0 |
| `-O0` old release | fake camera, dance clip | whole song → results | 14,723 (load ~40) | 14 changes | 0 |
| `-O3` release, rerun | fake camera, dance clip | whole song → results | **28,329** (load 36) | 20 changes | 0 |

The stand-in skeleton is deterministic and scores identically. The camera runs
vary with box load, not with the build: the score follows how many pose frames
the worker gets through (inference p50 36–135 ms across these runs), and the
two `-O0` runs alone differ by 2x. At similar load the two builds agree: `-O3`
28,329 at load 36 against `-O0` debug's 29,354 at load 31.

## Measured runtime (headless-shell, RTX 3090, localhost)

| | release (`-O3`) | `?debug=true` (`-O0`) |
|---|---|---|
| whole song | yes, 5 of 6 venues (throneroom is locked in quickplay; W-6) | yes |
| wasm heap peak, one song | 399–472 MB of a 1024 MB cap (table below) | same code, same data |

fps is pinned by `requestAnimationFrame`, so it says nothing about headroom on a
slower machine. Video plays from `.webm` transcodes (all 142 `.bik` have one;
see [`native/web/VIDEO_ASSETS.md`](../../native/web/VIDEO_ASSETS.md)).

### Heap per venue (W-6)

Decided 2026-11-13: dc3-web links `-sMAXIMUM_MEMORY=1024MB`
(`DC3_WEB_MAX_MEMORY` in `native/CMakeLists.txt`, overriding the engine's
512MB for dc3-web only; last `-s` wins). No engine change needed.
Optional engine patch (not applied) turning the engine's value into a
`MILO_WEB_MAXIMUM_MEMORY` knob:
`/home/free/tmp/np-web-build-scratch/engine-web-maximum-memory.patch`.

How it was measured: emcc's default growth is geometric (+20%, at most +96 MB
a step), so `buffer.byteLength` overstates demand by up to ~96 MB. The
measurement build is release `-O3` with `-DDC3_WEB_MEMORY_GROWTH_LINEAR_STEP=8MB
-DDC3_WEB_MAX_MEMORY=1024MB`, so byteLength tracks the high-water mark to
within 8 MB. One whole song each, stand-in player (no camera), boot to results,
`pose-gameplay.mjs --song-index N` (`wasmMax` = highest byteLength sampled):

| venue | song (`--song-index`) | true peak | release, 512MB, geometric |
|---|---|---|---|
| dci | omg (51) | **471.8 MB** | never reached (preload bug, below); the 1024 MB release grows to **522.6 MB**, past the old cap |
| dci | betteroffalone (2) | 443.9 MB | **512 MB: at the cap** on the results screen |
| dci | takeovercontrol (35) | 441.8 MB | never reached (preload bug) |
| streetside | iceicebaby (17) | 426.6 MB | never reached (preload bug) |
| rollerrink | thehustle (3) | 419.9 MB | 435.5 MB |
| dclive | 12step (9) | 403.9 MB | 457.1 MB |
| houseparty | macarena (6) | 398.8 MB | — |
| throneroom | — | not measured | locked venue (rank unlock / campaign) |

- The peak follows song + venue: the largest song (omg, 25 MiB of milo) in
  the largest venue (dci, 33 MiB) is the worst pair, 471.8 MB. throneroom's
  world is the smallest of the six (20 MiB vs dci's 33), so it should not set
  the peak; it was not reached because quickplay needs it unlocked.
- Songs do not accumulate: two songs in one session (rollerrink then dci)
  peaked at 420.2 then 444.2 MB, the same as each alone.
- Why 512 MB was not enough: under geometric growth the heap already sat at
  the 512 MB cap after one dci song, and omg needs ~472 MB of real demand,
  which geometric steps round past 512: the shipped 1024 MB release reaches
  522.6 MB on omg (whole song, 0 page errors). A failed growth aborts the page. The
  memory is not shared, so 1024 MB only reserves address space. (The 459.9 MB
  figures elsewhere in this doc are geometric reservations, not demand.)
- **Preload bug found on the way** (fixed in `185b6f50e`; the fix is shared
  with desktop, the bug was web-only): 34 of 57 song dirs went back to the
  title screen right after loading. `loading.dta` gives the song preload cache
  16 MiB in on-disc bytes; the 360 reads compressed milos (largest ~10 MiB),
  the web build reads the inflated extraction (iceicebaby 19.4 MiB), so
  `FileCache` evicted the song milo, the preload failed and `data_error` sent
  the game to the title. Native now scales the budget by 4. Desktop reads the
  ark's compressed bytes (largest set 13.2 MiB) and was never affected: with
  the ×4 removed it still plays omg. Guarded by `PreloadBudgetTest`
  (`native/tests/test_preload_budget.cpp`: every song's set, in the bytes
  each platform reads, against the budget `SetTypeDef` reads from the real
  `loading.dta`) and `LargestSongFlowTest`; every song to gameplay in
  Chromium below.

### SFX sidecars (W-7)

The web build has no FFmpeg, so `SampleData::Load` swaps every kXMA sample for
`sfx/gen/xma_pcm/<key>.ogg` (`native/src/platform/XmaPcmSidecar.h`; key = FNV-1a
of the XMA payload, its size and rate). The set used to be whatever desktop runs
had decoded (1035 keys), so a full song 404'd on 20 keys (tutorial VO,
`ui/hud/gen/xptoaster`, `camp_scene_01`) and played them silent.

`scripts/web/gen_sfx_sidecars.sh` (run by `build.sh`) derives the set from the
assets: `native/tools/xma_sidecars` walks every `.milo_xbox`, inflates it like
`ChunkStream`, finds each SampleData and decodes the kXMA ones with the desktop
build's own `DecodeXMAToPCM`; `gen_sfx_ogg.py` encodes the `.ogg`.

- English only: 2595 containers, 21,317 kXMA blobs, **4760 keys**, 114.5 MB of
  XMA. (All 15 locales: 47,313 keys, ~1 GB; the game loads one.)
- Committed: the key list, `native/web/sfx_sidecars.manifest` (370 KB). Not
  committed: 738 MB of `.pcm` (content-addressed cache,
  `~/.cache/dc3-xma-sidecars`, shared by every checkout) and 109 MB of `.ogg`
  in `native/web/build/sfx_sidecars/`. Cold run 41 s; afterwards a file check.
- Checks: all 1035 desktop-written keys are in the set and their `.pcm` are
  byte-identical to the tool's; the 20 keys the song 404'd on are in the set;
  a full song afterwards has 0 sidecar 404s.
  `scripts/web/gen_sfx_sidecars.sh --check` exits 1 on any missing key.
- `server.py` serves `sfx/gen/xma_pcm/*` from the deploy dir first, then
  `<assets>/sfx/gen/xma_pcm`; nothing is written into `orig-assets` (shared
  with every checkout and the public server, whose boot bundle walks that dir).
- The boot bundle `/api/bundle/sfx_pcm` packs only `native/web/sfx_bundle.keys`
  — the 1035 keys it always served (27.9 MB) — so it did not grow; every other
  key is one on-demand fetch (33 in a song).
- `?env=DC3_SFX_OGG_OFF=1` (raw `.pcm` A/B arm) still only has the 1035 old
  `.pcm` files; the rest 404 on that arm.

## Page (W-8)

The default page is for players: the game canvas fills the window at 16:9 on
black, the pose panel sits bottom-left on it, and a loading card ("Loading… /
Downloading game data… / Starting…") covers it until the first screen. Errors
(no WebGPU, a device/adapter failure, no build on the server) replace the
card's text and stay up. Double-click the game for fullscreen.

`?dev` (any value but `0`/`false`/`off`), or `?debug=true` unless `?dev=0`,
gives the developer harness: status bar, key help, Pause/Step (also `P` and
`.`), and the engine console, now capped at 500 lines. Engine output goes to
the browser console in both layouts, so every `scripts/web/*.mjs` works on
either (they use only `#dc3-canvas`, the console, `window.dc3FrameCount` and
the pose globals). `scripts/web/page-states.mjs` checks both layouts and the
error card (exit 1 if the player page shows any harness element).

`MILO_NOTIFY` / `MILO_WARN` go to the browser devtools console only, as
`console.warn` lines `MILO_<KIND>: <msg>` (W-9; on by default,
`?env=DC3_TAG_MODALS=0` turns it off). They bypass `Module.print`, so neither
the player page nor the `?dev` console shows them. Each distinct message prints
its first 5 repeats, then at every power of ten with the count. On the boot
route to the title a web run prints the same notifies as a desktop run with
`DC3_TAG_MODALS=1`, plus one the desktop does not raise (`DataNode::Equal:
DataNodes 0.00 and dance_battle ... are not compatible`, not yet traced).
`scripts/web/notify-console.mjs` checks all of this (exit 1 on a build without
the tap).

## Automated testing

Scripts live in `scripts/web/` and share `scripts/web/lib/core.mjs`:
`screenshot.mjs`, `scroll.mjs`, `gameplay.mjs`, `smoke-test.mjs`,
`audio-probe.mjs`, `cdp-break.mjs`, `netperf-lite.mjs`, `pose-gameplay.mjs`
(one song with a fake camera or none, per-second heap/pose/score samples),
`page-states.mjs` (W-8), `song-order.mjs` (prints which song each
`--song-index` selects). Menus advance with Enter (not Space).

Song-select index → song (sorted by skill level; a category header repeats the
song above it): 1 ymca, 2 betteroffalone, 3 thehustle, 5 starships,
6 macarena, 7 indaclub, 8 giveituptome, 9 12step, 11 discoinferno, 17
iceicebaby, 35 takeovercontrol, …, 51 omg (last). One per venue: dci 2,
rollerrink 3, houseparty 6, dclive 9, streetside 17, throneroom 35.

**Install once per checkout or worktree:** `npm ci --prefix scripts/web`
(W-2, fixed in `np-web-pose`). `scripts/web/package.json` pins Playwright
1.60.0, whose headless shell (Chromium 1223) gets a WebGPU adapter here with
the `core.mjs` flags (incl. `--ozone-platform=x11`); Node resolves
`scripts/web/node_modules` before the repo root's stale 1.58.2. The same
package pins `@mediapipe/tasks-vision`, which `build.sh` vendors for pose input.

## Input

Keyboard and gamepad reach the game through the engine's `Joypad_Native.cpp`.

**Pose (webcam):** the page asks for the camera on load, runs MediaPipe
BlazePose in a worker, and pushes DC3's 20 joints into the same provider the
desktop socket feeds — see [POSE_INPUT.md](POSE_INPUT.md#web). No camera,
permission refused, or `?pose=off`: the game scores the stand-in skeleton, as
before.

## Deployment

`dc3-web.service` serves the **main checkout's** `native/web/build` behind
nginx at `/dc3/` on the home server. As of 2026-11-13 that build dates from
2026-07-27 and predates the 2026-10-08 relink. The asset API in that nginx
block has no authentication. Redeploying or changing exposure is the owner's
call (PLAN W-5).

## Debugging

[../debugging/web.md](../debugging/web.md) — CDP break, WASM stacks, console
capture.
