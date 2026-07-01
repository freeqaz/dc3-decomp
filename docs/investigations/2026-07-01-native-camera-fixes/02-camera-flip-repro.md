# Bug 2 — Gameplay camera flips chaotically once a song loads

Status: **REPRODUCED** (2026-07-01, orchestrator). Root cause pending (Tier-1
discovery). This doc has the repro procedure, confirmed evidence, known context, and
hypotheses.

## Reproduction — CONFIRMED

Drove `betteroffalone.txt` to gameplay (`state=playing`, `screen=game_screen`,
`venuePresent=1`, dancers animating, IK stable — `ankleRotDeterminant≈1.0`,
`*HasNaN=0`). Captured gameplay frames (evidence in `assets/`):

- `bug2_gameplay_f2200_GOOD-shot.png` / `..f2212..` — camera correctly framed on the
  dancer, right-side up. **Some cuts are fine.**
- `bug2_gameplay_f2500_upside-down-dancer.png` — dancer rendered **UPSIDE-DOWN**, a
  giant leg/foot filling the left edge. **Camera in an inverted/wrong orientation.**
- `bug2_gameplay_f2100_venue-furniture.png` — camera pointed at venue furniture (a
  bar stool), no dancer in view. **Mispositioned cut.**

**Conclusion:** the gameplay camera *cuts* between shots on the beat; some cuts land
on cameras that produce **inverted / mispositioned** views (character upside-down =
almost certainly an inverted up-vector / non-right-handed basis on those cameras, or
a bad camera-cut transform). The IK/characters are fine — this is purely the camera
transform. So the fix target is the **camera cut/selection/transform**, not the
skeleton. The next step is to identify *which* cameras/cuts invert and *why*
(narrow to specific code + likely `311e3b75`'s `CameraManager::Poll` changes).

## Symptom (user report)

Once a song is loaded and gameplay starts (the 3D venue with dancers), the camera
"chaotically flips around." Menus are the separate bug 1. This is the in-game
performance camera.

## How to reproduce

Full boot→gameplay flow, capture a spread of gameplay frames and look at the camera:

```bash
cd native/build
rm -rf ./_gp && mkdir -p ./_gp
env MILO_HEADLESS=1 DC3_FAST_BOOT=1 DC3_FAST_TIME=1 DC3_SHOW_SPLASH=0 DC3_TEL=1 \
    MILO_INPUT_SCRIPT=/abs/scripts/dc3-input-flows/betteroffalone.txt \
    MILO_SCREENSHOT_DIR=./_gp \
    MILO_SCREENSHOT_FRAMES=<pick frames after game_screen> \
    MILO_MAX_FRAMES=<past last capture> \
    ./dc3-native > ./_gp.log 2>&1   # dangerouslyDisableSandbox: true
```
Find the `game_screen` entry frame in the log (or use HTTP
`/api/screen/wait/game_screen`), then capture several consecutive frames after it
to see the camera moving frame-to-frame. `DC3_TEL=1` dumps
`GameplayTelemetry` (beat, songAnimFrame, camera, clip layers) — cross-reference.

Capturing *consecutive* frames (e.g. N, N+2, N+4, N+6) is key: "flipping" is a
per-frame instability, so a single frame won't show it — compare adjacent frames.

## Known context / leads

- The performance camera is driven by the game (camera cuts/blends per beat, venue
  `.cam` animation). Native camera selection flows through
  `RndCam::Current()` / `PanelDir::CamOverride()` and the engine's
  `WgpuRnd::EnsureSceneUniformsCurrent()` (re-uploads scene uniforms when the camera
  or its position changes).
- `NativeSettings` has camera-blend knobs (`cameraBlend`, `blendFramesSame`,
  `blendFramesCross`) and a camera debug overlay (`MILO_CAM_DEBUG` /
  `cameraDebug`) — enable the debug log (`[CAM] frame=… fov=… pos=… zRange=…` in
  `Rnd_Wgpu.cpp`) to watch the camera state per second.
- The regression that broke bug 1, **`311e3b75`**, is literally titled
  "**CameraManager::Poll 40→85%** … native UI improvements". `CameraManager::Poll`
  drives camera updates — **check whether the same commit (or nearby camera
  commits) also destabilised the gameplay camera.** The two bugs may share a root in
  the camera machinery.
- Candidate files to read: `CameraManager` (grep for `CameraManager::Poll`),
  `src/system/rndobj/Cam.cpp` (`UpdatedWorldXfm`, `GetViewProjectXfms`, blend/interp),
  any `WorldDir`/venue camera-cut code, and the engine
  `Rnd_Wgpu.cpp` camera-change detection (`mLastSceneCam`, position deltas).

## Hypotheses to test (discovery should confirm/refute each)

1. **Camera-blend/interpolation instability** — a blend between camera cuts produces
   a bad intermediate transform each frame (NaN, un-normalized basis, or wrong
   interpolation param) → visible flipping. Check `blendFramesSame/Cross` and the
   blend math.
2. **`CameraManager::Poll` regression** — the 40→85% "match" work in `311e3b75`
   changed Poll behavior; a decomp-matching change may have introduced a native
   behavioral divergence in camera selection/timing.
3. **Un-normalized / mis-multiplied camera basis** — like the `XfmOnCircleEdge`
   cross-product sign bug found earlier in Text.cpp, a camera transform build may
   produce a non-right-handed / non-orthonormal basis that "flips".
4. **Camera-cut receiver / wrong-camera-selected each frame** — the game rapidly
   switches `RndCam::Current()` between cameras (or selects a stale/uninitialized
   one), and the engine dutifully renders each → apparent flipping.
5. **Timing / __mftb / beat-driven cut jitter** — camera cuts keyed off a beat clock
   that advances erratically on native.

## Deliverable for this doc

Fill: the reproduced evidence (frame paths + what the camera does), the confirmed
root cause with the offending code + commit, and the fix design + acceptance
criteria (stable gameplay camera across a full song, no per-frame flipping;
screenshot-verified over consecutive frames).
