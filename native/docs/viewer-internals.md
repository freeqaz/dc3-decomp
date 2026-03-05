# Milo Viewer — Technical Internals

Implementation details for `native/src/viewer/milo_viewer.cpp`. For user-facing documentation, see [scene-renderer.md](scene-renderer.md).

## Architecture

The viewer is a standalone executable that initializes a minimal subset of the Milo engine (SystemPreInit, TheRnd, CharInit, WorldInit, FlowInit, HamInit), loads `.milo_xbox` files, and runs a render loop. It does NOT go through the normal `App::App` boot sequence — no game modes, no UI screens, no save system.

```
main()
  ├── Engine init (SystemPreInit → TheRnd → subsystem registration)
  ├── Load primary .milo file (ObjDirPtr::LoadFile)
  ├── Load --subdir files (loaded into separate ObjDirPtrs)
  ├── Load --clips file (CharClipSet → CharDriver setup)
  ├── Create synthetic lights (--light / --ambient)
  ├── Auto-frame camera (bounding box → orbit distance)
  └── Enter mode:
      ├── --screenshot → render N warmup frames, save PNG, exit
      ├── --video → deterministic frame loop, encode via ffmpeg pipe, exit
      └── (default) → GLFW windowed render loop with input callbacks
```

## Orbit Camera (`OrbitCamera` struct)

Spherical coordinate camera with Z-up convention (Milo world: Z = up, Y = forward).

### Parameters
- `azimuth` — radians around Z axis. 0 = looking from +Y (front), π/2 = from +X (right)
- `elevation` — radians above horizontal. Clamped to ±1.5 rad
- `distance` — distance from target point
- `targetX/Y/Z` — look-at point (tracks pelvis bone for characters)

### Update cycle (per frame)
1. Clamp elevation and distance
2. Compute eye position from spherical coords: `eye = target + distance * (cosElev*sinAz, cosElev*cosAz, sinElev)`
3. Build look-at basis vectors (fwd, right, up) with Z-up world
4. Set `RndCam` local transform (Milo: m.x=right, m.y=forward, m.z=up)
5. Build view matrix (row-major, right-multiply)
6. Build perspective projection (Y-forward depth, Z maps to [0,1] for WebGPU)
7. `cam->SetViewProj(V * P)` — bypasses stubbed `RndCam::UpdateLocal`

### Camera smoothing

The pelvis tracking target uses exponential smoothing to avoid jarring camera motion during dance animation:

```cpp
// Video mode (0.05 = very smooth)
smoothTargetX += (pelvis.x - smoothTargetX) * 0.05f;

// Interactive mode (0.08 = slightly snappier for mouse control)
gOrbitCam.targetX += (pelvis.x - gOrbitCam.targetX) * 0.08f;
```

This is a first-order IIR low-pass filter. At alpha=0.05, the camera reaches 95% of the target after ~60 frames (2 seconds at 30fps). Large dancer movements (lunges, spins) produce smooth camera drift instead of jarring snaps.

### Auto-orbit

In `auto-orbit` mode, azimuth increments by 0.005 rad/frame (video) or 0.002 rad/frame * dt * 60 (interactive, frame-rate independent). Starting azimuth is -0.5 rad (~-29 deg) to begin counter-clockwise from front-center.

## Synthetic Lighting (`--light` / `--ambient`)

Creates `RndLight` objects via `Hmx::Object::New<RndLight>()` and adds them to the scene's `RndEnviron` via `env->AddLight()`.

### Light setup
- **Directional**: Transform Z-axis set to normalized direction vector. Orthonormal basis built from direction + world up.
- **Point**: Transform position set to (x,y,z). Default range = 500 units.
- **Color**: RGB values multiplied by intensity before `SetColor()`.
- **Ambient**: Calls `env->SetAmbientColor()` directly.

### Environment fallback
If no `RndEnviron` exists in the loaded scene or subdirs, creates a synthetic one:
```cpp
env = Hmx::Object::New<RndEnviron>();
env->SetName("synth_env", scene);
scene->SetEnv(env);
```

Lights are stored in `std::vector<RndLight*> syntheticLights` to prevent premature destruction.

### Renderer integration
The WebGPU renderer (`Rnd_Wgpu.cpp`) reads lights from `RndEnviron::LightsApprox()` (directional) and `LightsReal()` (point). Up to 4 of each type are sent to the GPU via the scene uniform buffer. Light direction is taken from the Z-axis of the light's world transform.

## Character Animation Pipeline

### Clip loading
1. Load `CharClipSet` from `--clips` path
2. Find `Character` in loaded scene
3. Create `CharDriver` and wire it to character bones via `CharServoBone`
4. Select clip by name (`--clip`) or auto-select first `win_move_*` clip

### Beat-based playback
Animation is driven by beats, not seconds. Conversion: `beat = seconds * (bpm / 60)`.

In video mode, clips loop via:
```cpp
float clipBeat = clipStart + fmodf(beat, clipEnd - clipStart);
activeClip->PoseMeshes(charObj, clipBeat);
```

### Twist bone solver
Outfit `.milo` files lack `CharUpperTwist`/`CharForeTwist` pollables (they live in the shared character setup dir). The viewer replicates their `Poll()` math directly:
- `SolveUpperTwistPoll()` — interpolates twist1/twist2 rotation from parent→upperArm
- `SolveForeTwistPoll()` — interpolates forearm twist from hand→elbow

## Video Recording

Headless deterministic rendering via ffmpeg pipe:
1. Set `MILO_HEADLESS=1` → GPU device creates offscreen surface
2. Pre-advance character to a reasonable pose for initial camera setup
3. Frame loop: advance animation → smooth-track pelvis → update orbit → render → readback pixels → encode
4. `VideoEncoder` pipes raw RGBA frames to `ffmpeg -f rawvideo ... -c:v libx264`

Frame timing is deterministic (`dt = 1.0 / fps`), not wall-clock. This guarantees identical output regardless of machine speed.

## File Layout

| File | Purpose |
|------|---------|
| `native/src/viewer/milo_viewer.cpp` | Main viewer: loading, camera, animation, lights, render loop |
| `native/scripts/render_scenes.py` | YAML scene runner (parallel batch rendering) |
| `native/scripts/render_screenshots.sh` | Legacy bash batch renderer (props only) |
| `native/scenes/demo.yaml` | Demo scene definitions (dancers, venues, props, edits) |
| `native/docs/scene-renderer.md` | User-facing scene renderer documentation |
| `native/docs/viewer-internals.md` | This file (technical internals) |

## Key Structs

| Struct | Location | Purpose |
|--------|----------|---------|
| `OrbitCamera` | milo_viewer.cpp:352 | Spherical orbit camera with mouse input |
| `AnimState` | milo_viewer.cpp:460 | Animation playback state (frame, speed, pause) |
| `SubdirEntry` | milo_viewer.cpp:665 | Subdir path + offset/rotation |
| `LightDef` | milo_viewer.cpp:696 | Synthetic light definition from CLI |

## CLI Flags (Complete)

### Scene loading
| Flag | Args | Description |
|------|------|-------------|
| `--subdir` | `<path>` | Load additional .milo as subdirectory (repeatable) |
| `--subdir-offset` | `<X> <Y> <Z>` | Offset subdir transform (follows --subdir) |
| `--subdir-rotate` | `<deg>` | Rotate subdir around Z (follows --subdir) |
| `--clips` | `<path>` | Load CharClip animation directory |
| `--clip` | `<name>` | Play specific clip by name |
| `--bpm` | `<number>` | Beats per minute (default: 120) |

### Camera
| Flag | Args | Description |
|------|------|-------------|
| `--camera` | `<mode>` | `orbit` or `auto-orbit` |
| `--azimuth` | `<degrees>` | Orbit azimuth (0=front) |
| `--elevation` | `<degrees>` | Orbit elevation |
| `--distance` | `<units>` | Orbit distance |
| `--eye` | `<X> <Y> <Z>` | Explicit camera position |
| `--lookat` | `<X> <Y> <Z>` | Explicit look-at target |

### Output
| Flag | Args | Description |
|------|------|-------------|
| `--screenshot` | `<path.png>` | Headless screenshot mode |
| `--video` | `<path.mp4>` | Headless video recording |
| `--duration` | `<seconds>` | Video duration (default: 10) |
| `--fps` | `<number>` | Video framerate (default: 30) |
| `--width` | `<pixels>` | Render width (default: 1280) |
| `--height` | `<pixels>` | Render height (default: 720) |

### Lighting
| Flag | Args | Description |
|------|------|-------------|
| `--light` | `<type> <X> <Y> <Z> <R> <G> <B> [intensity]` | Add synthetic light (repeatable) |
| `--ambient` | `<R> <G> <B>` | Override ambient color |

### Animation
| Flag | Args | Description |
|------|------|-------------|
| `--frame` | `<number>` | Start at specific animation frame |
| `--speed` | `<multiplier>` | Animation speed (default: 1.0) |
| `--paused` | — | Start with animation paused |

### Mesh control
| Flag | Args | Description |
|------|------|-------------|
| `--hide` | `<pattern>` | Hide meshes matching substring (repeatable) |

### Export
| Flag | Args | Description |
|------|------|-------------|
| `--export-textures` | `<dir>` | Export all textures as PNG |
| `--export-materials` | `<dir>` | Export all materials as JSON |
| `--export-gltf` | `<path>` | Export scene as glTF 2.0 |

### Debug
| Flag | Args | Description |
|------|------|-------------|
| `--verbose` / `-v` | — | Print detailed object info |
| `--dump-bones` | — | Dump raw bone buffer after clip eval |
| `--direct-pose` | — | Use CharClip::PoseMeshes directly |
| `--test-bone` | `<name> <angle> [axis]` | Manually rotate a bone |
| `--pose-dump` | `<file.json>` | Dump final pose transforms |
| `--pose-dump-bones` | `<csv>` | Restrict pose dump to named bones |
| `--pose-dump-beat` | `<value>` | Beat for pose dump (number/START/MID) |
