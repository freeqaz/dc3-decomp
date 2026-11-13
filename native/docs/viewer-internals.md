# Milo Viewer: Technical Internals

How `milo-viewer` works. The code is in `native/src/viewer/`. For usage see
[scene-renderer.md](scene-renderer.md) and
[docs/native/reference/VIEWER.md](../../docs/native/reference/VIEWER.md).

Last verified against the tree: 2026-11-13 (np-docs @ 2d678db7e).

## Architecture

The viewer is a standalone executable. It does **not** go through `App::App`,
so there are no game modes, UI screens or save system. `main()` in
`milo_viewer.cpp` does the following, in order:

```
main()                                   milo_viewer.cpp
  ├── ViewerConfig::Parse                ViewerArgs.cpp
  ├── set MILO_RENDER (0 for export-only, else 1), MILO_HEADLESS=1 for --screenshot/--video
  ├── SystemPreInit("config/ham_preinit_keep.dta") → TheRnd.PreInit/Init
  │     (exits 2 if the GPU fell back to the Null backend, e.g. sandboxed Vulkan)
  ├── FlowInit, CharInit, WorldInit, HamInit, Movie::Init
  ├── ViewerScene::Load (primary .milo, --subdir, lights, LOD/visibility)   ViewerScene.cpp
  ├── export-only modes (--export-textures/--export-materials/--export-gltf) → exit
  ├── --clips: create CharDriver "main.drv" + CharServoBone "bone.servo" if absent,
  │     SetClips, pick --clip or the first clip in the dir, StuffBones
  ├── --visemes (skipped when --char-setup's FileMerger already loaded them)
  └── SelectMode(cfg) → std::visit                                         ViewerCapture.cpp
        ├── ScreenshotMode → RunScreenshot (warmup frames, PNG, optional pose dump)
        ├── VideoMode      → RunVideo (deterministic dt = 1/fps, VideoEncoder → ffmpeg)
        └── InteractiveMode→ RunInteractive (GLFW window, ImGui debug UI)
```

`--char-setup <path>` replaces the primary path. `ViewerScene::LoadFileMerger`
then loads outfit and viseme content through `FileMerger`, the way the game does.

## Module map

| File | Contents |
|------|----------|
| `milo_viewer.cpp` | `main()`, engine init, clip/viseme wiring, keyboard callback |
| `ViewerArgs.{h,cpp}` | `ViewerConfig` (all CLI state), `Parse`, `PrintHelp` (the `--help` text is the authoritative flag list) |
| `ViewerCamera.{h,cpp}` | `OrbitCamera`, global `gOrbitCam`, mouse/scroll callbacks, `Mat4Multiply` |
| `ViewerScene.{h,cpp}` | `ViewerScene`: owned `ObjDirPtr`s (base, subdirs, clips, visemes), synthetic lights and environment, LOD choice, fallback material, `--movie` TexMovie |
| `ViewerAnimation.{h,cpp}` | `AnimState` (frame-based TransAnim/PropAnim), `CharAnimState` (beat-based clips, face), `BlinkState`, `PoseMeshesWithFacing` |
| `ViewerCapture.{h,cpp}` | `ViewerMode = std::variant<ScreenshotMode, VideoMode, InteractiveMode>`, `SelectMode`, the three `Run*` loops |
| `ViewerPoseDump.{h,cpp}` | `WritePoseDumpJson` for `--pose-dump` |
| `ViewerDebugUI.{h,cpp}` | `ViewerDebugUI`: ImGui light/environment controls and light gizmos |

The renderer, `ImGuiBackend` and `VideoEncoder` come from the shared engine
(`../milo-native-engine/src/platform/Rnd_Wgpu.cpp`, `../milo-native-engine/src/gfx/`),
linked as `libmilo-engine.a`.

## Orbit camera (`OrbitCamera`, `ViewerCamera.cpp`)

The camera uses spherical coordinates in Milo's Z-up world:

- `eye = target + distance * (cos(elev)*sin(az), cos(elev)*cos(az), sin(elev))`
- `elevation` is clamped to ±1.5 rad and `distance` to at least 0.1.
- The local transform uses the Milo axes `m.x = right`, `m.y = forward`, `m.z = up`.
- The view and projection are built by hand (row-major, Y-forward depth, depth
  mapped to [0,1] for WebGPU, fixed 16:9 aspect) and set with
  `RndCam::SetToolViewProj`. This is the tool view-projection API, kept separate
  from the camera's own matrices.

Pelvis tracking is an exponential low-pass on the target point:
`target += (pelvis - target) * alpha`, with alpha 0.05 in video mode and 0.08 in
interactive mode.

In `auto-orbit` mode, azimuth advances 0.005 rad/frame in video mode and
`0.002 * dt * 60` rad/frame in interactive mode. Video mode starts at
azimuth -0.5 rad.

## Synthetic lighting (`--light`, `--ambient`)

`ViewerScene` creates each light with `Hmx::Object::New<RndLight>()` and adds it
with `RndEnviron::AddLight`. The lights are kept alive in
`ViewerScene::syntheticLights`.

- **Directional** (`dir`): the transform's Z axis is set to the normalized
  direction vector.
- **Point** (`point`): the transform position is set to (x,y,z), with range 500.
- **Ambient**: set with `RndEnviron::SetAmbientColor`.
- If the scene has no `RndEnviron`, one named `synth_env` is created.

The engine renderer reads directional lights from the environment's "approx"
list and point lights from its "real" list
(`../milo-native-engine/src/platform/Rnd_Wgpu.cpp`). The scene uniform carries
up to 4 of each (`lightDirs[4]`, `pointLightPos[4]` in
`../milo-native-engine/src/gfx/UniformStructs.h`).

`ViewerDebugUI` can also add a default key/fill/rim rig to a scene that lacks
lights.

## Character animation

Animation is beat-driven: `beat = startBeat + seconds * bpm / 60`
(`CharAnimState::BeatForSeconds`).

- **`AdvanceBeat`** steps `TheTaskMgr.SetSecondsAndBeat` in 0.1-beat increments
  and calls `Character::Poll()`. That runs the engine's dependency-sorted
  pollables (CharDriver, CharServoBone, IK, CharUpperTwist and CharForeTwist),
  so twist bones come from the real pollables, not a viewer re-implementation.
- **`--direct-pose`** applies the clip at an exact beat with
  `PoseMeshesWithFacing` (ScaleDown/ScaleAdd with facing correction), then polls.
- **Video mode** loops a clip as `clipStart + fmodf(beat, clipLen)`.
- **Face**: `PollFace` drives `CharLipSyncDriver`, then `CharEyes::ForceBlink`
  (or a procedural blink weight on `CharFaceServo`) from `BlinkState`, then
  polls the face servo.

## Video recording

`RunVideo` is headless and deterministic: `dt = 1/fps`, not wall-clock time.
Each frame it advances animation, tracks the pelvis, updates the orbit, renders,
reads back the pixels and hands them to the engine's `VideoEncoder`, which pipes
them to an external `ffmpeg`.

## CLI flags (complete, from `ViewerArgs.cpp`)

### Scene loading
| Flag | Args | Description |
|------|------|-------------|
| `<path>` | positional | Primary `.milo_xbox` (first non-flag argument) |
| `--subdir` | `<path>` | Load an additional .milo as a subdirectory (repeatable) |
| `--subdir-offset` | `<X> <Y> <Z>` | Offset the preceding `--subdir` |
| `--subdir-rotate` | `<deg>` | Rotate the preceding `--subdir` around Z |
| `--char-setup` | `<path>` | Load a base HamCharacter; outfit/visemes through FileMerger |
| `--clips` | `<path>` | Load a CharClip directory |
| `--clip` | `<name>` | Play a specific clip (default: first clip in the dir) |
| `--visemes` | `<path>` | Load a facial viseme clip directory |
| `--bpm` | `<number>` | Beats per minute (default 120) |
| `--movie` | `<file>` | Play a video through the TexMovie pipeline |

### Camera
| Flag | Args | Description |
|------|------|-------------|
| `--camera` | `<mode>` | `orbit` (default) or `auto-orbit` |
| `--azimuth` | `<degrees>` | Orbit azimuth |
| `--elevation` | `<degrees>` | Orbit elevation |
| `--distance` | `<units>` | Orbit distance (default: auto-framed) |
| `--eye` | `<X> <Y> <Z>` | Explicit camera position |
| `--lookat` | `<X> <Y> <Z>` | Explicit look-at point (with `--eye`) |

### Output
| Flag | Args | Description |
|------|------|-------------|
| `--screenshot` / `--output` | `<path.png>` | Headless screenshot |
| `--frames` | `<count>` | Max frames (video/interactive); screenshot warmup count |
| `--video` | `<path.mp4>` | Headless video (needs ffmpeg on PATH) |
| `--duration` | `<seconds>` | Video duration (default 10) |
| `--fps` | `<number>` | Video frame rate (default 30) |
| `--width` / `--height` | `<pixels>` | Render size (default 1280x720; sets `MILO_WIDTH`/`MILO_HEIGHT`) |

### Lighting
| Flag | Args | Description |
|------|------|-------------|
| `--light` | `<dir\|point> <X> <Y> <Z> <R> <G> <B> [intensity]` | Add a synthetic light (repeatable) |
| `--ambient` | `<R> <G> <B>` | Override the ambient color (0.0-1.0) |

### Animation
| Flag | Args | Description |
|------|------|-------------|
| `--frame` | `<number>` | Start frame for prop animation; in screenshot mode with a character, the beat to pose at (default: clip start) |
| `--speed` | `<multiplier>` | Animation speed (default 1.0) |
| `--paused` | none | Start paused |

### Mesh control
| Flag | Args | Description |
|------|------|-------------|
| `--hide` | `<pattern>` | Hide meshes whose name contains the substring (repeatable) |
| `--show-all-lods` | none | Draw every LOD variant |
| `--no-fallback-material` | none | Leave material-less meshes undrawn instead of neutral grey |

### Export (export, then exit)
| Flag | Args | Description |
|------|------|-------------|
| `--export-textures` | `<dir>` | All textures as PNG |
| `--export-materials` | `<dir>` | All materials as JSON |
| `--export-gltf` | `<path>` | Scene as glTF 2.0 |

### Debug
| Flag | Args | Description |
|------|------|-------------|
| `--help` / `-h` | none | Print usage and exit |
| `--verbose` / `-v` | none | Detailed object/drawable info |
| `--dump-bones` | none | Dump the raw CharServoBone buffer after clip evaluation |
| `--direct-pose` | none | Pose with `PoseMeshesWithFacing` at the exact beat |
| `--test-bone` | `<name> <angle> [x\|y\|z]` | Rotate one bone (default axis x) |
| `--pose-dump` | `<file.json>` | Dump final pose transforms |
| `--pose-dump-bones` | `<csv>` | Restrict the pose dump to the named bones |
| `--pose-dump-beat` | `<number\|START\|MID>` | Beat for the pose dump |

## Related files

| File | Purpose |
|------|---------|
| `native/scripts/render_scenes.py` | YAML scene runner (parallel batch rendering) |
| `native/scripts/render_screenshots.sh` | Older bash batch renderer (props) |
| `native/scripts/pose_regression.sh` | Pose screenshot/JSON golden capture and compare |
| `native/scenes/demo.yaml` | Demo scene definitions |
| [scene-renderer.md](scene-renderer.md) | YAML scene renderer documentation |
