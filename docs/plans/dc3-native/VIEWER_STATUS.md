# DC3 Native Port — Viewer Status & Roadmap

**Last updated**: 2026-03-05

The milo-viewer (`native/src/viewer/milo_viewer.cpp`) is the standalone asset
viewer for DC3's `.milo_xbox` scene files. It's the primary testbed for rendering
work (Track B), separate from the full engine boot (Track A, see `STATUS.md`).

## Status Refresh (2026-03-05, Graphics Polish Checkpoint)

- Focused pose/clip validation is healthy:
  - `./native/build/milo-tests '--gtest_filter=MiloViewerPosePipeline.*:ClipPoseFixture.*' --gtest_color=no`
  - **10/10 passed** when run with unrestricted GPU/process access.
  - In sandboxed runs, `MiloViewerPosePipeline.ViewerPoseDumpMatchesInProcessPoseMeshes` can fail when the viewer subprocess cannot produce the pose dump file.
- Demo YAML inventory and output coverage:
  - `native/scenes/demo.yaml` currently defines **20 scenes** and **44 shots**.
  - Current `archive/screenshots` coverage for those named outputs is **14/44** (30 missing).
- Demo YAML currently has real data integrity issues:
  - **8 broken asset paths** (missing files on disk), including:
    - `emilia_dclive` clips
    - `bodie_houseparty` venue + clips
    - `angel_rollerrink` venue + clips
    - `taye_flash4wrd` venue + clips
    - `emilia_solo` clips
  - Duplicate shot key: `dare_front` is declared twice; the second declaration silently overrides the first.
  - `dare_streets` is referenced by the first `dare_front` declaration but has no corresponding scene block.
- Visual quality spot-checks after path correction:
  - Corrected Emilia/Bodie/Taye path combinations render successfully.
  - Several venue shots still read as sparse/dark framing; this is now mostly a shot composition + scene assembly issue, not a renderer crash issue.

## Completed

### Static Mesh Rendering (Tier 1.5)

Static props render with full Blinn-Phong materials: specular, emissive, rim
lighting, intensify, multi-directional lighting from RndEnviron, half-Lambert
diffuse, fog, alpha test. Ring buffer auto-grow, GPU resource cleanup, pipeline
cache.

**Tested**: 15 static props via `render_screenshots.sh` (discoball, chandelier,
microphone, arcade, couch, etc.)

### Skinned Mesh Rendering

4 bind groups (scene/material/object/bone), `vs_skinned` shader with 4-bone
blending, bone palette from `mBones[i].mOffset * BoneTransAt(i)->WorldXfm()`,
skin shader variant (half-Lambert + warm shadows + dual specular), compressed
vertex unpacking.

**Tested**: 12+ dancer characters + 2 crowd models. Combined/split mesh overlap
resolver heuristic for LOD/wrinkle meshes.

### Animation Playback (Phase 1)

Real-time `RndTransAnim` keyframe playback with delta-time advancement at 30fps.

- **Tick loop**: Per-animatable frame clamping within `[StartFrame, EndFrame]`
- **Interpolation**: Quaternion slerp/fast for rotation, spline for translation,
  vector interp for scale — all via `RndTransAnim::MakeTransform()`
- **Dirty propagation**: `SetLocalXfm()` → `SetDirty_Force()` → children
  invalidated → `WorldXfm()` recalculates on next draw
- **Controls**: Space (pause), `.`/`,` (step frame), Up/Down (speed), Home (reset)
- **CLI**: `--frame N`, `--speed X`, `--paused`

**Tested**: Palmtrees (24 TransAnims), banners (30+), newspaper (3). Video proof
at `archive/screenshots/palmtree_animation.mp4`.

### Multi-File Scene Loading (Phase 1.5) — COMPLETE

`--subdir` flag loads additional `.milo` files as subdirectories. Venue meshes render
alongside character meshes. Supports offset (`--subdir-offset X Y Z`) and rotation
(`--subdir-rotate deg`). Multiple subdirs supported.

**Tested**: Character + venue combos (Aubrey on Glitterati, Emilia on DC Live, etc.)

### Character Dance Animation (CharClip) — COMPLETE

Full `CharClip::PoseMeshes()` pipeline: load `CharClipSet` from `--clips`, create
`CharDriver` + `CharServoBone`, play beat-based animation. Twist bone solver
replicates `CharUpperTwist::Poll()` / `CharForeTwist::Poll()`.

**Tested**: 4 dance routines (Glitterati, Riptide, Ninja, Hi-Def) across multiple characters.

### Video Recording — COMPLETE

Headless deterministic frame capture via ffmpeg pipe. `--video out.mp4 --duration 10 --fps 30`.
Frame timing is deterministic (not wall-clock), so output is machine-speed-independent.

### Synthetic Lighting — COMPLETE

`--light <type> <X> <Y> <Z> <R> <G> <B> [intensity]` creates `RndLight` objects and
adds them to the scene's `RndEnviron`. Supports directional and point lights. `--ambient R G B`
overrides ambient color. Falls back to creating a synthetic `RndEnviron` if none exists.

### Smooth Camera Tracking — COMPLETE

Orbit camera tracks dancer's pelvis bone with exponential smoothing (alpha=0.05 for video,
0.08 for interactive). Eliminates "bouncing" from hip movement during dance animation.
Auto-orbit starts at -29 deg (counter-clockwise from front) to sweep across the dancer's front.

### YAML Scene Renderer — COMPLETE

Python-based batch renderer (`native/scripts/render_scenes.py`) reads YAML scene definitions
and runs `milo-viewer` in parallel. Supports screenshots, videos, multi-camera edits,
custom lighting, camera orbits.

**Docs**: [`native/docs/scene-renderer.md`](../../../native/docs/scene-renderer.md) (user guide),
[`native/docs/viewer-internals.md`](../../../native/docs/viewer-internals.md) (technical)

### Export Pipelines — COMPLETE

- `--export-textures <dir>` — all textures as PNG
- `--export-materials <dir>` — all materials as JSON
- `--export-gltf <path>` — scene as glTF 2.0

## Current Limitations

### ~~Single-File Loading Only~~ (RESOLVED — see Multi-File Scene Loading above)

The viewer loads one `.milo_xbox` in isolation. DC3 splits assets across a
parent-child hierarchy:

```
dclive.milo_xbox                        ← parent: textures, materials, lights
  ├── dclive_palmtrees_anim.milo_xbox   ← child: meshes, bones, animation
  ├── dclive_palmtrees_anim01.milo_xbox
  └── ...
```

Sub-scenes reference textures from their parent via `ObjectDir::FindObject(name,
parentDirs=true)`, which walks up `Dir()` pointers. Without the parent loaded,
textures resolve to null → flat gray shading.

| Asset Type | Example | Has Textures | Has Animation | Renders Textured |
|------------|---------|:---:|:---:|:---:|
| Self-contained prop | `discoballsml.milo_xbox` | Yes | No | Yes |
| Character model | `aubrey01.milo_xbox` | Yes | No | Yes |
| Venue sub-scene | `dclive_palmtrees_anim.milo_xbox` | No | Yes | No (gray) |
| Animation clip | `crowd/anim/female_base.milo_xbox` | No | Yes (no geo) | N/A |

### Drawable Parity Gaps

- **Particles**: native billboard draw path exists (`DrawParticlesBillboard`), but
  particle deserialization is still stubbed in native (`RndParticleSys::Load(BinStream&) {}`),
  so many authored particle systems are not faithfully reproduced.
- **Lines / flares**: object-side draw paths exist, but we do not yet have dedicated
  viewer regression coverage proving end-to-end parity across representative scenes.
- Text/2D draw paths exist in engine code, but viewer parity coverage is still limited.

### Demo Scene Data Hygiene / Coverage

- `native/scenes/demo.yaml` includes stale or incorrect asset paths, plus a duplicate
  shot key (`dare_front`) and one undefined scene reference (`dare_streets`).
- The demo output pack is incomplete (14/44 outputs currently present under
  `archive/screenshots` for defined shot names), so polished showcase generation is
  currently gated by YAML/data cleanup before renderer-only polish.

## Roadmap

### Phase 1.5: Multi-File Loading

**Goal**: Load parent + child `.milo` files together so sub-scenes can resolve
textures from their parent.

**Why now**: Blocks testing animation on textured content. Every animated venue
sub-scene lacks textures because they live in the parent.

#### Option A: Manual `--subdir` flag (recommended first step)

```bash
./milo-viewer parent.milo_xbox --subdir child_anim.milo_xbox
```

1. Load parent via `ObjDirPtr::LoadFile()` (existing code)
2. For each `--subdir`, load into separate `ObjDirPtr<ObjectDir>`
3. Call `parentDir->AppendSubDir(childDir)` — this calls `AddedSubDir()` →
   `SetSubDir(true)` → registers child objects with parent
4. `SyncObjects()` on parent (re-scans with children visible)

Now `FindObject("texture_name", parentDirs=true)` in the child walks up to
the parent and finds the texture.

**Effort**: ~30 lines of viewer code. The engine's subdir machinery already works.

**Limitation**: User must know which files go together.

#### Option B: Automatic subdir discovery

Parse `baseScene->SubDirs()` after loading the parent to find referenced subdir
file paths, auto-load them. Needs path remapping (`.milo` data stores game-relative
paths resolved via the archive system; the viewer uses raw filesystem paths).

#### Option C: Partial parent loading

Load only textures/materials from the parent, skip complex objects that crash.
Needs `DirLoader` modifications for selective type loading — the stream is
sequential and each object's `Load()` consumes variable bytes.

#### Blocker: Parent scene crashes

Full venue scenes crash during loading. Example — `dclive.milo_xbox`:

```
FAIL: PoolAlloc.cpp:392  bytes <= mAllocSizeWords * 4
```

`WorldCrowd::Load()` overflows `ReclaimableAlloc`. Other complex types
(`RndMultiMesh`, `WorldInstance`) may have similar issues. Fixing these is
required for Option A/B with venue parents.

#### Character animation assembly

To see a textured dancing character, load the character model (has textures) as
parent and an animation clip (has TransAnim keyframes) as subdir. The animation's
`mTrans` ObjPtrs reference bones by name — `SyncObjects()` resolves them across
the subdir boundary. This is Option A plus verifying cross-dir `ObjPtr` resolution.

### Phase 2: Secondary Texture Maps — COMPLETE

Normal maps, specular maps, emissive maps, rim maps, environment cube maps,
detail-normal maps. All bound in material bind group with WGSL shader support.

**Files**: `standard_wgsl.inc`, `Mesh_Wgpu.cpp`, `Rnd_Wgpu.h`

### Phase 3: Additional Drawable Types

`RndParticleSys` (billboard quads), `RndLine` (line strips), `RndFlare`
(screen-space glow), `RndGroup` (hierarchical draw order). New shader variants.

### Phase 4: UI / Text Rendering — COMPLETE (engine only)

`DrawRect()` (2D textured quads with gradient colors), text glyph mesh generation
via `DrawShowing()` → `FontMapBase` → `RndMesh::DrawShowing()`. Implemented in the
engine (`Rnd_Wgpu.cpp`, `Text.cpp`), not yet wired into standalone viewer.

### Phase 5: Post-Processing — COMPLETE (basic effects)

Contrast, chromatic aberration, posterization, vignette, color levels. Implemented
in `Rnd_Wgpu.cpp:1015-1264`. Advanced effects (bloom, shadow pass) remain future work.

### Phase 6: Advanced Effects

Fur (shell-based), refraction, motion blur, occlusion queries, movie textures.

## Key Files

| File | Role |
|------|------|
| `native/src/viewer/milo_viewer.cpp` | Viewer main — loading, camera, animation, lights, render loop |
| `native/src/platform/Mesh_Wgpu.cpp` | `RndMesh::DrawShowing` (static + skinned paths) |
| `native/src/platform/Rnd_Wgpu.cpp` | Frame lifecycle, scene uniforms, bind groups, lighting |
| `native/src/platform/Part_Wgpu.cpp` | Native particle billboard draw path |
| `native/src/gfx/standard_wgsl.inc` | WGSL shaders (static + skinned + skin/hair) |
| `native/src/gfx/PipelineManager.cpp` | 4 bind group layouts, pipeline cache |
| `native/src/gfx/VertexFormats.cpp` | Vertex layouts, compressed vertex unpacking |
| `native/scripts/render_scenes.py` | YAML scene batch renderer (parallel, screenshots + video) |
| `native/scripts/render_screenshots.sh` | Legacy bash batch renderer (props only) |
| `native/scenes/demo.yaml` | Demo scene definitions (44 shots across 20 scenes; currently needs path cleanup) |
| `native/docs/scene-renderer.md` | User guide: YAML format, camera, lighting, CLI |
| `native/docs/viewer-internals.md` | Technical: orbit camera, smoothing, light injection, pipeline |

## Immediate Priorities (Polish Track)

1. Fix demo YAML pathing and key collisions (`dare_front` duplicate, undefined `dare_streets`, 8 missing asset paths).
2. Regenerate full 44-shot demo pack and classify results (OK / dark / fail) from a clean run.
3. Tune per-shot camera + clip/frame selections for venue readability and pose quality.
4. Implement/port `RndParticleSys::Load` for authored FX parity in showcase scenes.
5. Add a renderer-side scene/YAML validation pass to catch missing assets and duplicate keys before long batch runs.

## How to Run

```bash
# Build
cd native/build && cmake --build . --target milo-viewer

# View a prop (windowed)
./native/build/milo-viewer path/to/file.milo_xbox

# Character on venue with dance animation
./native/build/milo-viewer char/aubrey01.milo_xbox \
  --subdir world/glitterati_set.milo_xbox \
  --clips char/backup/glitterati01_bd01/gen/clips.milo_xbox \
  --bpm 90

# Screenshot
./native/build/milo-viewer file.milo_xbox --screenshot output.png

# Video with auto-orbit camera
./native/build/milo-viewer file.milo_xbox --subdir venue.milo_xbox \
  --clips clips.milo_xbox --bpm 90 \
  --video dance.mp4 --duration 10 --camera auto-orbit

# Custom lighting (character without venue)
./native/build/milo-viewer char.milo_xbox --clips clips.milo_xbox \
  --light dir -0.5 -0.8 -0.3 1.0 0.95 0.9 1.2 \
  --ambient 0.15 0.15 0.2 \
  --screenshot dramatic.png

# YAML batch rendering (recommended for multi-shot workflows)
python native/scripts/render_scenes.py native/scenes/demo.yaml
python native/scripts/render_scenes.py native/scenes/demo.yaml --dry-run
python native/scripts/render_scenes.py native/scenes/demo.yaml --shot "aubrey_*"
python native/scripts/render_scenes.py native/scenes/demo.yaml --list

# Legacy prop batch screenshots
bash native/scripts/render_screenshots.sh
```

## Reference Screenshots

Xbox 360 reference images for visual comparison are in `archive/screenshots/references/`:

- `dc3_main_menu.jpg` — Main menu (DC3 logo, player silhouettes, swirl background)
- `dc3_song_select.jpg` — Song select (list with gradient bars, character preview)
- `dc3_gameplay_ui.jpg` — Gameplay (3D venue, move cards, score, characters)

Native port progress screenshots are in `archive/screenshots/native_alpha_fix_f*.png`.

## Related Docs

- [STATUS.md](STATUS.md) — Track A: full engine boot status
- [../custom-graphics-engine/PLAN.md](../custom-graphics-engine/PLAN.md) — master native port plan
- [../custom-graphics-engine/GRAPHICS_SYSTEM_DESIGN.md](../custom-graphics-engine/GRAPHICS_SYSTEM_DESIGN.md) — rendering pipeline design
- [../custom-graphics-engine/STREAM_DESYNC.md](../custom-graphics-engine/STREAM_DESYNC.md) — stream issues with stub objects
