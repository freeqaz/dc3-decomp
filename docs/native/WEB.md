# Web (Emscripten / WASM + WebGPU)

**State (2026-11-13, measured):** builds from a fresh worktree and plays a whole
song in Chromium on a real GPU — boot → menus → tutorial → song select →
loading → gameplay → results — at the browser's 60 fps cap with no page errors.
**There is no player input that affects the score**: the web build has no pose
path, so the game scores a stand-in standing skeleton (one measured run: 7,581,
0 stars). Work items: [PLAN.md](PLAN.md) track W.

## Build

```bash
scripts/web/build.sh            # --release (default) | --debug | --both | --reconfigure
python3 native/web/server.py --port 8420
# http://localhost:8420/  -> release/      http://localhost:8420/?debug=true -> debug/
```

- emsdk 5.0.2. Fresh release build ≈ 3 min (configure ≈ 2 min, 866 objects + link ≈ 70 s).
- Output: `native/web/build/{release,debug}/`. Release `dc3-web.wasm` 16.8 MB
  (2.56 MB brotli), `dc3-web.js` 346 KB.
- There is no `.data` package: assets stream from `server.py` at runtime
  (`/api/file/...`, ~132 MB and ~240 requests for boot plus one song).
- Requires crossOriginIsolated (COOP/COEP, for the audio worklet's
  SharedArrayBuffer) and JSPI (Chrome 137+). Safari is not supported.
- Known build defects (W-3, W-4 in PLAN):
  - Every build dir clones imgui from GitHub with full history at configure
    time (`FetchContent_Declare(imgui)`); concurrent configures can hang on it.
  - If the debug half of `--both` fails, `index.html` is never copied.
  - **The variants are swapped relative to their comments**: "release" compiles
    and links unoptimised (`-O0 -g0`), "debug" is `CMAKE_BUILD_TYPE=Release`
    (`-O3`). Check `build-web*/CMakeFiles/dc3-web.dir/flags.make`.
  - 41 symbols are unresolved at link and become runtime warnings via
    `missing_stubs.js`; `AudioDevice::Suspend`/`Resume` have no web
    implementation and are hit in play.

## Measured runtime (headless-shell, RTX 3090, localhost)

| | release (-O0) | `?debug=true` (-O3) |
|---|---|---|
| whole song | yes (and a 2-song run) | yes |
| attract screen reached | 6.7 s | 4.6 s |
| fps menus / gameplay | 59.1 / 59.9 | 59.4 / 59.5 |
| loading → gameplay | 3.5 s | 6.5 s |
| wasm heap in gameplay | 459.9 MB of 512 MB cap | 435.5 MB |

fps is pinned by `requestAnimationFrame`, so it says nothing about headroom on a
slower machine. The heap is ~50 MB under the hard cap (`-sMAXIMUM_MEMORY=512MB`
in the engine CMake); a second play of the same song did not grow it; other
venues are unmeasured. ~20 SFX sidecars under `sfx/gen/xma_pcm/` 404. Video
plays from `.webm` transcodes (all 142 `.bik` have one; see
[`native/web/VIDEO_ASSETS.md`](../../native/web/VIDEO_ASSETS.md)).

## Automated testing

Scripts live in `scripts/web/` and share `scripts/web/lib/core.mjs`:
`screenshot.mjs`, `scroll.mjs`, `gameplay.mjs`, `smoke-test.mjs`,
`audio-probe.mjs`, `cdp-break.mjs`, `netperf-lite.mjs`. Menus advance with
Enter (not Space).

**They do not run on this box as installed** (W-2): `node_modules/playwright`
1.58.2 wants `chromium_headless_shell-1208`, which is absent, and full Chrome in
headless mode gets a null WebGPU adapter. What works: headless-shell 1223 with
the `core.mjs` flags plus `--ozone-platform=x11`, or full Chrome under Xvfb.
rb3's `scripts/web` pins Playwright 1.60, which matches.

## Input

Keyboard and gamepad reach the game through the engine's `Joypad_Native.cpp`.
Pose: `native/src/platform/Skeleton_Native.cpp` stubs every provider method
under `__EMSCRIPTEN__`, so `GestureMgr_Native.cpp` always feeds
`FillDummySkeletonData`. The seam a browser pose pipeline must feed, and the plan
for it (MediaPipe tasks-vision + getUserMedia → an exported `dc3_pose_push`), is
in [POSE_INPUT.md](POSE_INPUT.md) gap P-9. ncnn is not an option on web
(disabled under Emscripten; no model files).

## Deployment

`dc3-web.service` serves the **main checkout's** `native/web/build` behind
nginx at `/dc3/` on the home server. As of 2026-11-13 that build dates from
2026-07-27 and predates the 2026-10-08 relink. The asset API in that nginx
block has no authentication. Redeploying or changing exposure is the owner's
call (PLAN W-5).

## Debugging

[../debugging/web.md](../debugging/web.md) — CDP break, WASM stacks, console
capture.
