# DC3 Scene Renderer

YAML-based scene definition system for batch-rendering screenshots and videos from DC3 .milo files.

## Quick Start

```bash
# Render all shots from the demo scene file
python native/scripts/render_scenes.py native/scenes/demo.yaml

# Preview commands without running
python native/scripts/render_scenes.py native/scenes/demo.yaml --dry-run

# List all defined shots
python native/scripts/render_scenes.py native/scenes/demo.yaml --list

# Render a single shot
python native/scripts/render_scenes.py native/scenes/demo.yaml --shot aubrey_front

# Render all shots for one scene
python native/scripts/render_scenes.py native/scenes/demo.yaml --scene "aubrey_*"

# Override parallelism
python native/scripts/render_scenes.py native/scenes/demo.yaml --jobs 8
```

## Prerequisites

- **milo-viewer** built: `cd native/build && cmake --build . --target milo-viewer`
- **PyYAML**: `pip install pyyaml`
- **Milo asset library** at the path specified in `settings.milo_lib`
- **Vulkan ICD** for GPU rendering

## YAML Format

A scene file has three sections: `settings`, `scenes`, and `shots`.

### Settings

Global defaults. All are optional.

```yaml
settings:
  milo_lib: ~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3
  output_dir: archive/renders    # where PNGs/MP4s go
  width: 1920
  height: 1080
  jobs: 4                        # parallel render workers
  viewer: native/build/milo-viewer  # auto-detected if omitted
```

### Scenes

Reusable definitions of what to load. A scene combines a character, venue, clips, lights, and hide patterns.

```yaml
scenes:
  aubrey_glitterati:
    character: char/main/dancer/gen/aubrey01.milo_xbox   # primary .milo (has meshes)
    venue: world/glitterati/gen/glitterati_set.milo_xbox  # loaded as --subdir
    clips: char/main/backup/glitterati01_bd01/gen/clips.milo_xbox
    clip: idle_01            # optional: play a specific clip
    bpm: 120                 # optional: clip BPM
    hide: [shadow, Shadow]   # hide meshes matching substrings

    # Optional venue placement
    venue_offset: { x: 0, y: 0, z: -10 }
    venue_rotate: 90

    # Optional extra subdirs
    subdirs:
      - world/glitterati/gen/glitterati_chairs.milo_xbox
      - path: world/shared/props/gen/discoballsml.milo_xbox
        offset: { x: 0, y: 0, z: 200 }
        rotate: 45

    # Optional synthetic lights (added to RndEnviron)
    lights:
      - type: dir                          # dir or point
        dir: [-0.5, -0.8, -0.3]           # direction vector (normalized internally)
        color: [1.0, 0.95, 0.9]           # RGB 0-1
        intensity: 1.2                     # multiplier
      - type: point
        pos: [0, 50, 100]
        color: [0.8, 0.4, 0.2]

    ambient: [0.15, 0.15, 0.2]            # override ambient color
```

**Key design**: `character` is always the primary file (loaded first, has the skeletal meshes). `venue` is loaded as a `--subdir`. For venue-only or prop-only renders, use `primary` instead:

```yaml
scenes:
  glitterati_venue:
    primary: world/glitterati/gen/glitterati_set.milo_xbox
```

### Shots

What to render from each scene. Each shot produces one PNG or MP4.

```yaml
shots:
  # Screenshot with specific camera
  hero_shot:
    scene: aubrey_glitterati
    camera:
      azimuth: 0           # degrees, 0=front, 90=right, 180=back
      elevation: 15         # degrees above horizontal
      distance: 120         # camera distance from origin
    frame: 0                # optional: specific animation frame
    speed: 1.0              # optional: animation speed

  # Video with auto-orbit camera
  orbit_video:
    scene: aubrey_glitterati
    type: video
    duration: 8             # seconds
    fps: 30
    camera:
      mode: auto-orbit
      elevation: 20
      distance: 150

  # Fixed-position camera
  custom_cam:
    scene: aubrey_glitterati
    camera:
      eye: [100, 50, 80]     # camera position
      lookat: [0, 50, 0]     # look-at target

  # Per-shot light override (replaces scene lights)
  dramatic:
    scene: aubrey_glitterati
    lights:
      - type: dir
        dir: [-1, -0.5, -0.2]
        color: [1.0, 0.3, 0.1]
        intensity: 1.5
    ambient: [0.05, 0.05, 0.08]
```

Shots inherit lights/hide/clip from their scene unless overridden.

## Camera Reference

| Parameter   | Description                                  | Default   |
|-------------|----------------------------------------------|-----------|
| `azimuth`   | Horizontal angle in degrees (0=front)        | ~23       |
| `elevation` | Vertical angle in degrees (0=level)          | ~17       |
| `distance`  | Distance from origin                         | auto      |
| `mode`      | `orbit` (default) or `auto-orbit` (rotating) | `orbit`   |
| `eye`       | Explicit camera position `[x, y, z]`         | —         |
| `lookat`    | Explicit look-at target `[x, y, z]`          | —         |

Azimuth convention (Z-up coordinate system):
- 0° = front (+Y toward camera)
- 90° = right side (+X toward camera)
- 180° = back (-Y toward camera)
- 270° = left side (-X toward camera)

## Multi-Camera Video Edits

To create a multi-camera edit, render separate video clips at different frame offsets, then concatenate with ffmpeg:

```yaml
shots:
  # Wide establishing shot (frames 0-89)
  edit_wide:
    scene: aubrey_glitterati
    type: video
    duration: 3
    camera: { azimuth: -20, elevation: 30, distance: 250 }
    frame: 0

  # Medium shot (frames 90-149)
  edit_medium:
    scene: aubrey_glitterati
    type: video
    duration: 2
    camera: { azimuth: 15, elevation: 15, distance: 120 }
    frame: 90

  # Close-up (frames 150-209)
  edit_closeup:
    scene: aubrey_glitterati
    type: video
    duration: 2
    camera: { azimuth: -10, elevation: 20, distance: 60 }
    frame: 150

  # Orbit finish (frames 210+)
  edit_orbit:
    scene: aubrey_glitterati
    type: video
    duration: 3
    camera: { mode: auto-orbit, elevation: 20 }
    frame: 210
```

Concatenate the clips:
```bash
# Create a file list
cat > shots.txt << 'EOF'
file 'archive/renders/edit_wide.mp4'
file 'archive/renders/edit_medium.mp4'
file 'archive/renders/edit_closeup.mp4'
file 'archive/renders/edit_orbit.mp4'
EOF

# Concatenate
ffmpeg -f concat -safe 0 -i shots.txt -c copy final_edit.mp4
```

## Camera Smoothing

The orbit camera tracks the dancer's pelvis bone to keep the character centered. Raw pelvis positions jitter frame-to-frame as the dancer moves, which causes visible camera "bouncing."

The camera uses **exponential smoothing** to filter out this jitter:

```
smoothTarget += (pelvisPos - smoothTarget) * alpha
```

- Video mode: `alpha = 0.05` — very smooth, the camera glides even through large dance moves
- Interactive mode: `alpha = 0.08` — slightly snappier so mouse drag still feels responsive

### Auto-Orbit Starting Angle

Auto-orbit starts at **-29 degrees** (counter-clockwise from front-center), so the camera sweeps across the front of the dancer before reaching the side. Without this offset, the orbit would go behind the dancer early in the video.

The starting angle is only applied when `--azimuth` is not explicitly set, so you can still override it.

## Lighting

The `--light` and `--ambient` CLI flags (and their YAML equivalents) create synthetic `RndLight` objects and add them to the scene's `RndEnviron`. This is useful for:

- Characters without a venue (no environment lights)
- Overriding dim venue lighting for screenshots
- Dramatic/stylized lighting setups

Light types:
- **`dir`** (directional): Infinite light from a direction. The `dir` vector is the light direction (where light comes from). `[-0.5, -1, -0.3]` means light coming from upper-right-front.
- **`point`**: Positional light at `pos`. Has a default range of 500 units.

Color values are RGB 0-1, multiplied by `intensity`. So `color: [1, 1, 1]` with `intensity: 2.0` gives a very bright white light.

## CLI Reference

```
python render_scenes.py <yaml> [options]

Options:
  --shot <pattern>     Render only shots matching glob pattern
  --scene <pattern>    Render only shots from matching scenes
  --dry-run            Print milo-viewer commands without executing
  --list               List all shots and exit
  --jobs <N>           Override parallel worker count
  --viewer <path>      Override milo-viewer binary path
  --timeout <secs>     Per-shot timeout (default: 120)
```

## Output

Results are printed as they complete:

```
=== DC3 Scene Renderer ===
YAML:   native/scenes/demo.yaml
Shots:  15
Jobs:   4

  aubrey_front                   OK        12.3s  (2,847,291 bytes)
  aubrey_side                    OK        11.8s  (2,612,445 bytes)
  emilia_front                   DARK       9.2s  (847 bytes)
  prop_discoball                 OK         4.1s  (1,923,102 bytes)

=== Results: 13 OK, 1 dark, 1 failed / 15 total ===
```

Status meanings:
- **OK**: Output file exists and is >1KB
- **DARK**: Output file exists but <=1KB (likely all-black — missing lights or environment)
- **FAIL**: No output file produced
- **TIMEOUT**: Render exceeded timeout
- **ERROR**: Exception during execution
