# DC3 Native Port — Viewer Status & Roadmap

**Last updated**: 2026-03-02

The milo-viewer (`native/src/viewer/milo_viewer.cpp`) is the standalone asset
viewer for DC3's `.milo_xbox` scene files. It's the primary testbed for rendering
work (Track B), separate from the full engine boot (Track A, see `STATUS.md`).

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

## Current Limitations

### Single-File Loading Only

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

### Missing Drawable Types

Particles (`RndParticleSys`), lines (`RndLine`), flares (`RndFlare`) are not
implemented. Text and 2D quads work in the engine but haven't been wired into
the standalone viewer.

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
| `native/src/viewer/milo_viewer.cpp` | Viewer main — loading, camera, animation, render loop |
| `native/src/platform/Mesh_Wgpu.cpp` | `RndMesh::DrawShowing` (static + skinned paths) |
| `native/src/platform/Rnd_Wgpu.cpp` | Frame lifecycle, scene uniforms, bind groups |
| `native/src/gfx/standard_wgsl.inc` | WGSL shaders (static + skinned + skin/hair) |
| `native/src/gfx/PipelineManager.cpp` | 4 bind group layouts, pipeline cache |
| `native/src/gfx/VertexFormats.cpp` | Vertex layouts, compressed vertex unpacking |
| `native/scripts/render_screenshots.sh` | Batch screenshot script |

## How to Run

```bash
# Build
cd native/build && cmake --build . --target milo-viewer

# View a prop (windowed)
./native/build/milo-viewer path/to/file.milo_xbox

# Screenshot mode
./native/build/milo-viewer path/to/file.milo_xbox --screenshot output.png

# With animation
./native/build/milo-viewer path/to/file.milo_xbox --frame 50 --screenshot output.png

# Batch screenshots
bash native/scripts/render_screenshots.sh
bash native/scripts/render_screenshots.sh --only disco
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
