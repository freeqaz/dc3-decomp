# milo-viewer: how to run it

How to build and run `milo-viewer`, the standalone `.milo_xbox` viewer and renderer.

Last verified against the tree: 2026-11-13 (np-docs @ 2d678db7e).

`milo-viewer` loads one or more `.milo_xbox` files into a minimal engine
(no `App`, no UI screens), then does one of three things:

- renders them in a GLFW window,
- renders headlessly to a PNG or MP4,
- exports textures, materials or glTF.

How it works inside: [native/docs/viewer-internals.md](../../../native/docs/viewer-internals.md),
which also carries the complete flag table. Batch rendering from YAML:
[native/docs/scene-renderer.md](../../../native/docs/scene-renderer.md).
The authoritative flag list is always `milo-viewer --help`
(`native/src/viewer/ViewerArgs.cpp`).

## Build

```bash
scripts/native_configure.sh                     # once per build dir (finds Dawn etc.)
cmake --build native/build --target milo-viewer
```

The binary is `native/build/milo-viewer`. Rendering needs a real GPU. Under the
agent sandbox, run with `dangerouslyDisableSandbox: true`. If Vulkan falls back
to the Null backend, the viewer refuses to render and exits 2.

## Examples

Asset paths below are under `orig-assets/extracted/`.

```bash
V=native/build/milo-viewer
A=orig-assets/extracted

# A prop, windowed (left-drag orbit, scroll zoom, middle-drag pan, R reset,
# Space pause, "." / "," step a frame, Up/Down speed, Home restart, Esc quit)
$V $A/world/shared/props/gen/discoballsml.milo_xbox

# Headless screenshot
$V $A/world/shared/props/gen/discoballsml.milo_xbox --screenshot out.png

# A dancer in a venue, playing a clip
$V $A/char/main/dancer/gen/aubrey01.milo_xbox \
   --subdir $A/world/glitterati/gen/glitterati_set.milo_xbox \
   --clips  $A/char/main/backup/glitterati01_bd01/gen/clips.milo_xbox \
   --bpm 90

# Same, recorded to video with an auto-orbiting camera (needs ffmpeg on PATH)
$V $A/char/main/dancer/gen/aubrey01.milo_xbox \
   --subdir $A/world/glitterati/gen/glitterati_set.milo_xbox \
   --clips  $A/char/main/backup/glitterati01_bd01/gen/clips.milo_xbox \
   --bpm 90 --video dance.mp4 --duration 10 --camera auto-orbit

# Custom lighting on a character with no venue
$V $A/char/main/dancer/gen/aubrey01.milo_xbox \
   --clips $A/char/main/backup/glitterati01_bd01/gen/clips.milo_xbox \
   --light dir -0.5 -0.8 -0.3 1.0 0.95 0.9 1.2 \
   --ambient 0.15 0.15 0.2 --screenshot lit.png

# Pose JSON at the middle of the clip, for numeric comparison
$V $A/char/main/dancer/gen/aubrey01.milo_xbox \
   --clips $A/char/main/backup/glitterati01_bd01/gen/clips.milo_xbox \
   --screenshot pose.png --pose-dump pose.json --pose-dump-beat MID

# Export, then exit
$V file.milo_xbox --export-textures out/tex
$V file.milo_xbox --export-materials out/mat
$V file.milo_xbox --export-gltf out/scene.gltf
```

Useful extras: `--direct-pose`, `--dump-bones`, `--test-bone <name> <angle> [axis]`,
`--hide <substring>`, `--show-all-lods`, `--no-fallback-material`, `--visemes`,
`--char-setup`, `--movie`, `--width/--height`. See the flag table in
[viewer-internals.md](../../../native/docs/viewer-internals.md#cli-flags-complete-from-viewerargscpp).

## Batch rendering from YAML

```bash
python native/scripts/render_scenes.py native/scenes/demo.yaml            # all shots
python native/scripts/render_scenes.py native/scenes/demo.yaml --dry-run  # print commands
python native/scripts/render_scenes.py native/scenes/demo.yaml --list     # list shots
python native/scripts/render_scenes.py native/scenes/demo.yaml --shot "aubrey_*"
python native/scripts/render_scenes.py native/scenes/demo.yaml --scene "aubrey_*" --jobs 8
```

Other options: `--viewer <path>` (default `native/build/milo-viewer`) and
`--timeout <secs>` (default 120). The YAML `settings.milo_lib` key gives the
asset root that scene paths resolve against. `native/scenes/demo.yaml` points it
at a `milo-engine-libs` checkout, so point it at `orig-assets/extracted` if you
do not have that checkout. Needs PyYAML. Format reference:
[scene-renderer.md](../../../native/docs/scene-renderer.md).

`native/scripts/render_screenshots.sh` is an older bash batch renderer for props.

## Pose regression goldens

`native/scripts/pose_regression.sh` captures viewer screenshots and pose-JSON
sidecars for a fixed set of poses and compares them against goldens. Modes:
`--capture-with-dump`, `--update-goldens`, `--compare`, `--compare-pose-json`.
Its asset root is `MILO_LIB`. This covers character animation, not webcam pose
input. See [TESTING.md](../TESTING.md).

## Reference screenshots

Xbox 360 reference images for visual comparison live in
`archive/screenshots/references/`: `dc3_main_menu.jpg`, `dc3_song_select.jpg`
and `dc3_gameplay_ui.jpg`. `archive/` is gitignored; `scripts/setup_worktree.sh`
symlinks it into worktrees.

## Sources

- `native/src/viewer/*.cpp`, `native/scripts/render_scenes.py`,
  `native/scripts/pose_regression.sh`, `native/CMakeLists.txt` (`milo-viewer` target)
- Archived: [plans/dc3-native/VIEWER_STATUS.md](../../archive/2026-11-13-native-doc-audit/plans/dc3-native/VIEWER_STATUS.md) § How to Run
