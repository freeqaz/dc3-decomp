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
  dir still holds a previous build (W-4). Pose input files are part of the
  page: if tasks-vision is not in `scripts/web/node_modules` the script runs
  `npm ci --prefix scripts/web`, and if it or a `.task` model is still missing
  it ends `FAILED: pose input files` with exit 1 (W-13; it used to warn and
  the page used the jsdelivr CDN). `--page-only` deploys just the page.
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

fps is pinned by `requestAnimationFrame` (and capped at 60, below), so it says
nothing about headroom on a slower machine.

### Frame cap on high-refresh displays (H-34, np-web4)

`main_web.cpp` ran one game frame per `requestAnimationFrame`, and rAF fires
at the display's rate, so a 120/144 Hz browser ran the game at 120/144 fps
where the desktop caps at 60 (H-29). The rAF loop now asks `dc3RafGate(ts)`
first (`Dc3RafGate`, `native/src/platform/FramePacer_Native.cpp`):

- it estimates the display rate from the **median** of the last 64 rAF gaps
  (first decision after 16), so one long frame does not move it;
- it passes that estimate to the same `Dc3FrameCapDecide` the desktop uses, so
  the cap snaps the same way: 144 Hz → every 2nd rAF (72 fps), 120 → 60,
  240 → 60, 165 → 82.5, and 60 Hz or slower runs every rAF;
- a rAF runs the frame when at least one period minus half a display interval
  has passed since the last frame that ran, so jitter does not drop frames;
- `?env=DC3_FPS_CAP=N` caps at N (snapped), `DC3_FPS_CAP=0` turns it off, as on
  desktop.

It logs `DC3 Web: frame cap … (display ~N Hz from rAF)` when the decision
changes, and `window.dc3FrameCap` holds the estimate, the cap, and rAFs run and
skipped. Tests: `RafGate.*` in `test_frame_pacer.cpp` drive the gate with a
simulated rAF grid (144/120/240/165/500 Hz, 60 Hz, a game slower than the
display, jitter, env overrides). Sabotages (MEASURED, in a standalone copy):
running on every rAF fails every capped case; dropping the half-interval
tolerance fails 120 Hz and the jitter case. In the browser,
`scripts/web/frame-cap.mjs` runs Chromium with `--disable-frame-rate-limit
--disable-gpu-vsync` (an unthrottled rAF stands in for a fast display) and
fails above 66 game frames a second; `--env DC3_FPS_CAP=0 --expect uncapped` is
its control. It needs rAF ≥ 90/s to say anything, and exits 3 (SKIPPED) below
that. Video plays from `.webm` transcodes (all 142 `.bik` have one;
see [`native/web/VIDEO_ASSETS.md`](../../native/web/VIDEO_ASSETS.md)).

### Intro movie (W-11)

Until np-web3 the attract screen's intro movie was never on the web canvas: the
`<video>` played (`currentTime` 2.5 → 25 s) and the canvas stayed black at
8/14/20/30 s, so a gesture player looked at a black screen for ~83 s.
`MoviePanel::Draw` draws the movie after the post-processing flush
(`WgpuRnd::FlushPostProcessingForOverlay`, which opens a 1× depthless overlay
pass on the frame target). Something later in the frame called
`BeginFramePass(false)` (SavePreBuffer, ComposeTexBlend, a point test or
`MakeDrawTarget` resumes the frame pass); that reopened the **MSAA** main pass,
whose resolve at the end of the frame overwrote the frame target, overlay
draws included. Engine fix (np-web3 branch `e030a36`): a resume after the
flush reopens the overlay pass instead. With it the movie and its help bar /
SKIP overlay show, `attract-movie.mjs` passes (mean luma 22 and 31, 29% of
sampled pixels change in 2 s), and `gesture-menus.mjs --skip-attract` leaves
attract 11.7 s after the performer starts (gesture 5, button 0). Song-select
preview movies open and load (256×256); whether they look right is the same
question as on desktop (np-media).

### Song clock (W-12)

`StandardStream::UpdateTime` logs `timer error is large: <ms>` when the song
clock and its own timer disagree by more than 50 ms. On web the clock
(`GetRawTime`) read `StreamReceiverNative::GetBytesPlayed()`, the bytes
handed to the mixer. The mixer fills the AudioWorklet's queue (adaptive,
140–500 ms) once a frame, so the clock ran a queue depth ahead of the
speaker in pump-sized steps. Across a main-thread stall it read a whole stall
behind before the refill and as far ahead after: errors came in −S/+S pairs
within 10 ms, each logged once per stream (3–4×), and song time moved both
ways. On web it now reads `StreamReceiverNative::HeardBytes()`: every
`RenderAudio` call records which mix frames it filled and the cursor before
and after, and `HeardBytes` interpolates at the frames the worklet has
consumed (`AudioDevice::MixFramesConsumed`; engine np-web3 `7404fb3`,
`0a283f5`). `GetRawTime` keeps its match (100%, 26/26, `name_check`); the
block is `HX_WEB` only.

thehustle, Easy, no camera, release, `pose-gameplay.mjs`:

| | timer errors in game | per game second | median | max |
|---|---|---|---|---|
| unmuted (real output, 44.1 kHz, outputLatency 32 ms), before | 154 | 0.93 | 72.5 ms | 182 ms |
| unmuted, after | 4 | 0.02 | 81 ms | 121 ms |
| `--mute-audio`, before | 43 | 0.26 | 70.3 ms | 260 ms |
| `--mute-audio`, after | 7 | 0.04 | 61 ms | 103 ms |

The remaining few are at song start and section changes. The dogfood's ~9/s
was not reproduced here (that box was also running other lanes and the camera
pipeline). The score is 52,805 either way: the stand-in skeleton scores the
same, so a score says nothing about the clock. If the engine lacks
`HeardBytes` it links as a missing stub that returns 0, the clock stands still
and every song scores 0 (measured); dc3 needs the engine branch first.
`pose-gameplay.mjs --unmute` fails (exit 1) above 0.25 errors a game second
(`--max-timer-errors-per-s`).

### Frame pacing (W-14/W-15, np-web4)

`pose-gameplay.mjs --pacing` records every game frame and rAF interval, long
tasks (and long-animation-frame script attribution), every request, audio
underruns and the engine's per-frame counters (`DC3_FRAME_TRACE`: fetch, texture,
mesh, unpack, pipeline, DTA, stream ms), and gives each stall over 50 ms one
cause. It writes `pacing.json` and prints the per-phase table. Phases: menu,
song_select, loading, game, results.

**Box load decides the numbers on the rig.** Every run below was taken at load
average 50–190 on 32 cores (other lanes compiling and running flow tests, plus
this lane's own native gate for part of it), with the macarena loop as a fake
camera and the pose worker taking 80–175 ms a frame (41–47 ms on a quiet box).
Game-phase fps was 18.8–23.7 and whole-run p95 frame time 108–130 ms in every run,
before or after any change; the run-to-run spread is larger than any fix here.
So the before/after below is measured on the thing that changed, not on the
whole-run percentiles. A quiet-box or GPD measurement is still owed (the GPD's
W-14 row on np-gpd-test: p50 53.8 fps, p5 20.6, stalls to 1.37 s).

What the stalls were, two-song runs (MEASURED, menus → Macarena → results →
Give It Up To Me):

| cause | before1 | after1 | after2 | before2 |
|---|---|---|---|---|
| game frame compute (main thread) | 2279 / 191 s | 1584 / 129 s | 2728 / 226 s | 991 / 82 s |
| browser frame gap (main thread busy elsewhere, compositor) | 971 / 102 s | 1183 / 143 s | 1135 / 123 s | 1985 / 206 s |
| blocking asset fetch | 110 / 39 s | 47 / 7 s | 157 / 46 s | 60 / 9 s |
| main-thread task, not a game frame | 183 / 23 s | 146 / 17 s | 254 / 29 s | 128 / 15 s |

(stalls over 50 ms / their total ms). "Before" is `MILO_WEB_BUNDLE_SLICE_MS=0`,
the old one-task unpack; the devkit fix is in all four.

Fixed:

1. **Screen-bundle unpack in one task** (engine `WebAssets.cpp`). The bundle's
   fetch callback wrote every file into MEMFS in one go: the boot set (493
   files), the SFX set (1035), title_flow (25 files, 77.6 MB) and
   song_select each held the main thread for one task. It now unpacks in
   ~6 ms slices (`MILO_WEB_BUNDLE_SLICE_MS`, default 6, 0 = the old way),
   re-posting itself with `emscripten_async_call(…, 0)`, and skips a file
   already in MEMFS at the same size (a sync fetch that beat the bundle).
   MEASURED, longest single unpack task:

   | bundle | before (one task) | after (slices, mean per slice) |
   |---|---|---|
   | boot (493 files) | 158, 184 ms | 21 slices, 7.4 ms; 19 slices, 7.3 ms |
   | SFX (1035) | 152, 403 ms | 33 slices, 6.5 ms; 45 slices, 8.0 ms |
   | title_flow (25, 77.6 MB) | 81, 179 ms | 9 slices, 9.9 ms; 8 slices, 9.5 ms |

   A slice can run over budget by one file (the largest title_flow milo).
   Total busy time is unchanged; wall time grows because other frames run in
   between, which is the point.
2. **`devkit:` reads went to the server** (engine `AsyncFile_Native.cpp`).
   `devkit:/dancers.dta` and `devkit:/locale/eng/locale_keep.dta` were each a
   blocking round trip and a 404. They now open as missing without a request
   (see "Known console lines").

Not fixed (MEASURED, INFERRED where marked):

- **Song art at song select is fetched one file at a time**, blocking:
  `songs/<s>/gen/<s>_keep.png_xbox`, 174 KB each, as the cursor reaches the
  song; 11–35 ms each on localhost. INFERRED: over a remote link each is a
  round trip; a `song_art` screen bundle (66 files, 11.5 MB raw) fired from
  main_screen would remove them at the cost of the download.
- **Loading-screen fetches** (song milo, venue, characters, SFX sidecars;
  up to 620 ms each under this load) are blocking by design of the loader;
  they land on the loading screen.
- The two big causes are the game's own frame and main-thread contention,
  which is what a loaded box does to a single-threaded wasm game plus a pose
  worker; they need the quiet-box measurement before anything is attributed.

### Pose frame rate (W-13, L-3)

The page tracked at ~16.6 fps in the dogfood. The cause is serial work in the
one pose worker: BlazePose `full` on the CPU (XNNPACK) delegate takes 41–47 ms
a frame, and the P-4 camera picture (segmentation masks, RGB scale) adds
5–10 ms. 30 fps needs ≤33 ms. Measured on the attract screen with the game
running, macarena fake camera at 30 fps, RTX 3090 box, 20 s windows:

| config | frames/s | inference p50 / p95 | picture p50 |
|---|---|---|---|
| default (`full`, CPU, picture 320×240) | 17.4 | 46.7 / 69.8 ms | 5.1 ms |
| `pose_frames=off` | 23.6 | 40.7 / 51.0 ms | — |
| `pose_model=lite` | 24.6 | 34.8 / 46.1 ms | 3.6 ms |
| `pose_delegate=GPU` | 25.4 | 31.1 / 87.1 ms | 9.7 ms |
| `pose_delegate=GPU&pose_frames=off` | 28.4 | 33.9 / 46.2 ms | — |

None reaches 30 fps on its own. INFERRED: 30 needs the GPU delegate and the
picture work moved out of the inference worker, or two frames in flight. The
GPU delegate also shares the GPU with the game, and its p95 is worse. The
defaults are unchanged: CPU `full` is what the desktop `pose_server.py` runs,
and the tracker is np-tracking's. This is an owner decision (PLAN L-3).

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
black, the pose panel sits in the bottom band between the two help bars
(below), and a loading card ("Loading… /
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

The page layout and the engine's defaults are separate switches. The engine
runs the **dev profile** in either layout (developer Options rows, the
original's dev menu and overlays, input/move-rating/frame-time console lines;
[debugging/native.md "Profiles"](../debugging/native.md#profiles-dc3_profile)).
`?profile=release` gives the player defaults (`dc3_pre.js` sets
`DC3_PROFILE`; `?env=DC3_PROFILE=release` works too). The player page stays the
default layout (W-8): the `?dev` harness is one parameter away, and proposed as
such rather than made the default.

Pose panel (W-13): it used to sit bottom-left, over the left help bar, and
its status text ran out of its box at small sizes. It is now centred in the
bottom 14% of the canvas, no wider than 48% of it (the help bars hold the
outer quarters), and sized in container units so it scales with the canvas;
the status wraps inside its box. `page-states.mjs` checks the band and the
containment at 1280×720 and 800×450 with the real status, "Tracking you ✓"
with the preview on, and a long unbreakable status (the old panel failed 6
position and 2 containment checks, overflowing 141 px).

Engine stderr (W-13): every `printErr` line used to reach devtools as
`console.error` (986 on one boot to the title). The page now picks the level
from the text (`window.dc3StderrLevel`): `FAIL`/`MILO_FAIL`/assert/abort/
`error`/`failed`/`exception`/`crash` lines stay errors, `WARN`/`warning` and
`timer error is large` are warnings, everything else is `console.log` (12
errors on the same boot, all real). `page-states.mjs` checks the table and the
boot count (< 20).

The loading-card check in `page-states.mjs` waited a fixed time after the
first screen and could read the card mid-fade; it now waits for the `hidden`
class and then for the fade to finish (10 s deadline). Not reproduced locally,
even at a 6× CDP CPU throttle (`--cpu-throttle 6`); the fix is in the test.

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

### Phones and touch (W-16, np-web4)

Checked with Playwright's Pixel 7 emulation (viewport, touch, mobile UA) on the
rig's Chromium, so the GPU and memory are the rig's; no real phone was used.

- **Secure context.** WebGPU, the camera and the audio worklet's
  SharedArrayBuffer all need https (or localhost). A phone opening the page
  over plain http from the LAN or a Tailscale IP used to get the "no WebGPU,
  try these browser flags" card. It now gets "This page needs a secure
  connection" and the engine does not boot.
- **No WebGPU on a phone**: Android gets "Chrome 137+, Android 12+, a supported
  GPU"; iPhone/iPad gets "Safari has WebGPU from iOS 26 but not the JSPI this
  build needs".
- **Buttons.** `touch_controls.js` shows a d-pad and A (Enter) / B (Esc) /
  Start (Space) when the device has no fine pointer (`?touch=1` forces,
  `?touch=0` hides). They dispatch the keys the engine's document listener
  maps, so any screen a keyboard drives works. Each press is held at least
  120 ms: the engine samples the key bitmask once per frame, and the first
  version's quick tap (keydown and keyup between two frames) did nothing on
  the title screen. Portrait puts the buttons under the picture and asks to
  turn the phone; landscape puts them over the lower corners, clear of the
  pose panel.
- **Camera.** The page asks for the front camera (`facingMode: user`). A
  blocked prompt shows "Camera blocked — the game is using a stand-in player"
  and the retry button.

`scripts/web/phone.mjs --port P` checks all of it (21 checks: layout in both
orientations, a touchscreen tap on A leaving the title, blocked camera,
desktop shows no buttons, the LAN address shows the secure card).
`--sabotage` serves a `touch_controls.js` that dispatches on `window` instead
of `document`; the tap check then fails, as it must.

Not known without a real phone: the GPU and frame rate with the pose worker,
and memory (the release peaks at 523 MB of wasm heap). The page must be served
over https to reach a phone at all.

### Known console lines (np-web4)

A boot to gameplay used to print four lines that look like faults. One is
fixed, three are the dev build doing what it does:

| line | what it is | verdict |
|---|---|---|
| `/api/file/devkit:/dancers.dta` and `devkit:/locale/eng/locale_keep.dta` 404 | `HamPlayerData` and the locale loader read optional files on the Xbox **devkit drive**, a dev kit's local disk. The web `AsyncFile` sent them to the server like any other path. | **Fixed** (engine `AsyncFile_Native.cpp`, branch np-web4): a `devkit:` path is not fetched; it opens as missing, as it does on desktop, where no such file exists either. The other optional reads (`watches.dta`) still go to the server, because nothing marks them as optional and the server is the only one that knows. |
| `watches.dta` 404 | `Watcher` (`src/system/rndobj/Watcher.cpp`) reads a developer watch list from the game root when it starts. No retail or dev data has one. | Harmless; one request at boot. |
| `/api/file/ui/image/dc_logo_keep.png` 404 | `config/persistent_filecache.dta` lists the `.png` name. No native renderer runs `Rnd::Init`/`RndUtlInit`, so `ResourceFileCacheHelper` is not registered and the name is not rewritten to `gen/…png_xbox` (PLAN H-11 *Left*). The read falls through to the real `_xbox` file, which the song-select bundle carries. | Harmless, one request. The fix is a renderer init on native, which is shared with desktop; `test_persistent_filecache.cpp` checks both outcomes so it goes stale loudly. Not changed here. |
| `FAIL: Couldn't resolve holmes_host` | `UI::Init` → `OSCMessenger::Connect` (`src/system/utl/OSCMessenger.cpp`). The dev build connects an OSC (Open Sound Control) socket to the developer's PC whenever it is **not** reading from disc (`!UsingCD()`). Web reads loose files, so `UsingCD()` is false, and `HolmesClient::PlatformResolveIP` `MILO_FAIL`s without a Holmes host. Desktop reads the ark (`UsingCD()` true) and never gets here. | Faithful to the image. `MILO_FAIL` is non-fatal on native; the messenger stays unconnected and `Poll` does nothing. Changing it means changing `UsingCD()` on web, which also picks the file-loading paths, so it is left alone. |
| `[cheats] XTitleServerCreateEnumerator failed with error 1317` (on screen) | `XLSPConnection::StartEnumeration` calls the Xbox LSP enumerator. Our `xbox_link_stubs` return `ERROR_NO_SUCH_USER` (1317) on purpose (no Xbox LIVE), and the image raises a `MILO_NOTIFY`, which the **dev profile** shows in the `cheat_display` overlay. | Expected in the dev profile, which the web page runs by default (above). `?profile=release` hides it, as it hides every dev overlay. |

## Automated testing

Scripts live in `scripts/web/` and share `scripts/web/lib/core.mjs`:
`screenshot.mjs`, `scroll.mjs`, `gameplay.mjs`, `smoke-test.mjs`,
`audio-probe.mjs`, `cdp-break.mjs`, `netperf-lite.mjs`, `pose-gameplay.mjs`
(one song with a fake camera or none, per-second heap/pose/score samples),
`page-states.mjs` (W-8, W-13), `song-order.mjs` (prints which song each
`--song-index` selects), `attract-movie.mjs` (W-11: the intro movie's pixels
are on the canvas), `gesture-menus.mjs --skip-attract` (a gesture skips the
intro movie). `pose-gameplay.mjs --unmute` runs the audio on the real output
device and checks the song clock (W-12, below). Menus advance with Enter (not Space).

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

`dc3-web.service` (a user unit) serves the **main checkout's**
`native/web/build` behind nginx at `/dc3/` on the home server. The asset API in
that nginx block has no authentication; changing exposure is the owner's call
(PLAN W-5).

**Deploy with `scripts/web/deploy.sh`, from the checkout the unit serves (W-13):**

```bash
scripts/web/deploy.sh                    # build both variants, restart if needed, smoke test
scripts/web/deploy.sh --browser          # + boot the page in Chromium (smoke-test.mjs; GPU, no sandbox)
scripts/web/deploy.sh --no-build         # restart check + smoke test only
scripts/web/deploy.sh -- --release       # anything after -- goes to build.sh
scripts/web/deploy.sh --unit U --port P  # another user unit / port (default dc3-web.service, 8420)
```

`build.sh` writes `native/web/build/`, which `server.py` reads per request, so
a rebuild is live without a restart. `server.py`'s own code is not: on
2026-11-13 the unit was still running a `server.py` from before W-7's sidecar
routes, and 106 SFX sidecars 404'd. `/api/health` now reports
`server_sha256`, the hash of the `server.py` the process started from;
`deploy.sh` restarts the unit (`systemctl --user restart`) when that differs
from the tree's file, when the field is missing (a server older than this
change) or when the server does not answer, then waits up to 30 s for the new
hash. The smoke test fetches the page, the worklet, each built variant's
wasm/js, the vendored tasks-vision bundle and the `full` model (no CDN), the
intro movie through the asset API, and one SFX sidecar named in
`native/web/sfx_sidecars.manifest`.

| exit | meaning |
|---|---|
| 0 | deployed, smoke test passed |
| 1 | build failed; nothing restarted (the server keeps serving what is on disk) |
| 2 | restart failed, or the server did not come back with this tree's `server.py` in 30 s |
| 3 | refused: the unit does not exist, or its `WorkingDirectory` is not this checkout (deploying from a worktree cannot restart another tree's server) |
| 4 | smoke test failed |

Tested against a throwaway transient unit (`systemd-run --user`, port 8449,
`WorkingDirectory` = the np-web3 worktree): matching hash → no restart, PASS;
`server.py` edited → restart, PASS; a server without the field → restart,
PASS; model removed from the deploy dir → exit 4; `--unit dc3-web.service` from
the worktree → exit 3. It has not been run against `dc3-web.service`.

## Debugging

[../debugging/web.md](../debugging/web.md) — CDP break, WASM stacks, console
capture.
