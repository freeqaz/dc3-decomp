# milo-viewer drops geometry that the asset asks for — root cause and fix

**Date:** 2026-08-02
**Oracle:** `rb3-xenon`'s `rb3-render` target, documented in
`rb3-xenon/docs/plans/x3-first-render-2026-08-01.md` §4
**Commits:** dc3-decomp `13b583df`, `fc40baec`, `3f66008e` · milo-native-engine `9898a63`, `138e160`
**Engine SHA for the rb3 coordinator: `138e160`. No pin was bumped.**

---

## Verdict

Three independent defects, all in DC3-owned code. Two were the viewer overriding
what the asset says; one was a framing bug. Nothing in the shared engine's
*rendering* was wrong — but the engine was carrying two DC3 content filters
hardcoded into `RndMesh::DrawShowing`, which is why every consumer inherited
DC3's assumptions. Those moved to the seam the engine already has for them.

| cell | before | after |
|---|---|---|
| RB3 `char/crowd/gen/crowd_female01` | 0.399% coverage, 1628 colours — two disembodied hands | **7.202%, 11148** — full clothed figure, head included |
| RB3 `ui/track/gen/tracksystem_meshes` | 0.000%, 1 colour — **blank** | **1.050%, 70** — legible highway/rail geometry |
| DC3 `char/main/dancer/gen/aubrey01` | 8.313%, 28308 | unchanged, **PNG byte-identical** |
| DC3 `char/crowd/gen/crowd_f_01` | 7.861%, 4687 | unchanged, **PNG byte-identical** |
| DC3 `world/shared/props/gen/discoballsml` | 11.395%, 16038 | unchanged, **PNG byte-identical** |
| DC3 — 35-asset sweep (see §6) | — | **all 35 byte-identical** |

`coverage` = fraction of pixels differing from the modal colour; `distinct` =
distinct RGB values. Same definition rb3-xenon's gate uses, so the numbers are
directly comparable to its oracle (11.07% / 17960 and 4.54% / 104).

Before/after PNGs: [`assets/`](assets/).

---

## 1. Root cause, with the probe evidence

The viewer was instrumented with `--verbose`, which prints every mesh with its
`Showing()` flag and material. That located all three drop sites on the first
run; no new instrumentation was needed beyond a LOD-group dump added as part of
the fix.

### 1a. `crowd_female01` — 5 of 6 meshes drew; the 6th was the whole character

```
  hide LOD/wrinkle mesh 'female_crowd_body01_lod02.mesh'
Milo Viewer: hid 1 meshes (LOD/wrinkle/combined)
  mesh 'horns.mesh':                        showing=1 faces=504  mat=female_crowd_body01_lod02.mat
  mesh 'fist.mesh':                         showing=1 faces=504  mat=female_crowd_body01_lod02.mat
  mesh 'female_crowd_body01_lod02.mesh':    showing=0 faces=1378 mat=female_crowd_body01_lod02.mat
  mesh 'clap.mesh':                         showing=1 faces=501  mat=female_crowd_body01_lod02.mat
  mesh 'lighter.mesh':                      showing=1 faces=504  mat=female_crowd_body01_lod02.mat
  mesh 'lighter.1.mesh':                    showing=1 faces=96   mat=crowd_lighter.mat
```

**Drop site: `native/src/viewer/ViewerScene.cpp:333` (pre-fix)** —
`ResolveMeshVisibility` did `if (strstr(name, "_lod") || strstr(name, "_wrinkle"))
meshIt->SetShowing(false);`. The mesh ships with `Showing()` set and a valid
material; the viewer cleared the flag on the strength of a substring. What
remained is four hand/prop meshes — the "two disembodied hands".

**Second drop site, same cause: `milo-native-engine/src/platform/Mesh_Wgpu.cpp:136`
(pre-fix)** — `RndMesh::DrawShowing` carried an identical blanket
`strstr(Name(), "_lod")` skip. Fixing only the viewer would not have helped;
the engine would have dropped the mesh a layer down. rb3-xenon's `rb3-render`
never saw this because it re-issues lod-named meshes through
`DrawMeshImmediate`, bypassing `DrawShowing` entirely
(`rb3-xenon/native/src/main_render.cpp:947`) — the same workaround, written
independently, which is the signal that the filter was in the wrong place.

The bbox was poisoned too: `AutoFrameCamera` skips `!Showing()` meshes, so the
before-run framed `(-17.73,-1.32,34.19)-(17.73,3.75,42.62)` — the hands only.

### 1b. `tracksystem_meshes` — 130 of 130 meshes had no material

```
Milo Viewer: 130 meshes, 0 materials, 0 textures, 2 other objects
  mesh '_gem_style_00.mesh':  showing=1 faces=334 bones=0 mat=(none) pos=-38.0,32.0,0.0
  mesh '_track_rails.mesh':   showing=1 faces=352 bones=0 mat=(none) pos=0.0,0.0,0.0
  ... 130 of 130 with mat=(none)
```

**Drop site: `milo-native-engine/src/platform/Mesh_Wgpu.cpp:150`** —
`RndMesh::DrawShowing` returns early when `Mat()` is null. **This skip is
correct** (§2) and was left alone.

### 1c. `tracksystem_meshes` — the camera was 243180 units away

```
Milo Viewer: auto-frame bbox (-114.02,-132.07,-2.53)-(84.84,121458.38,3.81)
             center=(-14.59,60663.16,0.64) dist=243180.88
```

**Drop site: `native/src/viewer/ViewerScene.cpp:487-510` (pre-fix)** —
`AutoFrameCamera` took a raw min/max over every world-space vertex. One vertex
decodes to `Y = 121458.38` and it alone sets the frame. Even with every mesh
drawn, the result is a blank image.

★ rb3-xenon's independently written compressed-vertex reader produces
**121458.38 to the decimal** (x3 doc §4). Two unrelated implementations, one
number — the outlier is in the asset or in a vertex-format branch both engines
get wrong identically, **not** in the framing code. That is what licenses fixing
this at the framing layer instead of going hunting in the decode.

---

## 2. Correctness adjudication — what *should* a viewer draw?

Three separate questions, three different answers. The rule applied throughout:
**a viewer shows what the asset says; where the asset says nothing, it
substitutes something and announces it.**

**The LOD skip was the viewer overriding the asset, and it had no authority to
do so.** `female_crowd_body01_lod02.mesh` ships with `Showing()` set. The
justification for hiding it — "`Character::DrawLod` picks one LOD, so a
lod-named mesh is a redundant copy" — is checkable, and it is false here.
`Character.h` states the real rule: *"drawables not in any lod group will be
drawn at every LOD"*, and a drawable in group `i` is drawn only at LOD `i`. So
`Character::mLods` is the authority, not the file name. Dumping the groups
settles every case:

| asset | LOD groups | verdict |
|---|---|---|
| RB3 `crowd_female01` | **none at all** | nothing is demoted → draw the body |
| DC3 `aubrey01` | 0: 21 opaque, 1: 14 opaque | group 1 demoted → hide, as before |
| DC3 `emilia01` | 0: 21 opaque, 1: 14 opaque | ditto |
| DC3 `crowd_f_01` | 0: 2 opaque, 1: 1 opaque | ditto |

So xenon's liberal behaviour is the *correct* one for this asset, and DC3's
existing behaviour is correct for DC3's. The fix is not "be more liberal", it is
"ask the asset". `--show-all-lods` is still provided for a genuine show-me-
everything view.

**A name test cannot do this job, and DC3's own content proves it.** The first
attempt (dc3 `13b583df`, engine `9898a63`) derived a "higher-detail sibling"
name and hid the LOD mesh only when that sibling existed. It works on
`crowd_f_body01_lod.mesh → crowd_f_body01.mesh` and on `aubrey01_lod.1.mesh →
aubrey01.1.mesh`, and it **fails on `emilia01`**, which names its LOD-1 meshes
`emilia01_lod1*` while the full-detail ones are `emilia01_outfit*`. All five
LOD-1 meshes were kept and double-drew over the body — 25611 → 21375 distinct
colours. That approach was reverted, not tuned. It is recorded here so it is not
retried.

**The material-less skip is correct and was left in the engine.** A mesh with no
`RndMat` has no shader inputs to bind; there is nothing for the renderer to do
with it. `tracksystem_meshes` is a geometry *library* — the venue that
instantiates it supplies the materials — so "blank" is a faithful report of what
the file ships. But a viewer whose job is *show me this file* should show the
geometry, so `ApplyFallbackMaterial` attaches a neutral prelit grey and prints,
every run:

```
Milo Viewer: 130 of 130 meshes ship NO material and were given a neutral prelit
grey — their colour below is the VIEWER's, not the asset's
(--no-fallback-material to disable)
```

The shape is the asset's; the grey is ours, and the reader is told. This is
skipped entirely for `--export-*` runs so exported glTF/materials only ever
contain what the file actually ships.

**The framing outlier is an unambiguous bug** — no adjudication needed. Fixed
conservatively: the percentile bound is used only on an axis whose raw span
exceeds 4× its robust span, and it warns when it fires, so well-formed assets
keep their exact historical framing (verified: all 35 DC3 renders byte-identical).

---

## 3. The fix

### DC3 — `native/src/viewer/ViewerScene.cpp`

* `ChooseLod()` — picks the most detailed LOD group that actually *has*
  geometry, not blindly group 0 (an asset may ship an empty group 0).
* `CollectRedundantLodMeshes()` — hides only drawables that appear in some other
  group and not in the chosen one. **When the asset defines no LOD groups,
  nothing is hidden by name.**
* `ApplyFallbackMaterial()` — neutral prelit grey for material-less meshes,
  announced; off for export runs and under `--no-fallback-material`.
* `AutoFrameCamera()` — outlier-guarded percentile bounds, with a warning.

### DC3 — `native/src/platform/MeshFilter.cpp`

Takes ownership of the two name filters the engine used to hardcode.
`grid_80by60` (Kinect depth-sensor grid) applies everywhere; the `_lod` rule is
`#ifndef MILO_VIEWER`, so **`dc3-native` keeps its exact previous behaviour**
while the viewer uses the LOD groups instead.

### DC3 — `src/system/char/Character.h`

`NumLods()` / `GetLod()`, `#ifdef HX_NATIVE`-gated. `mLods` stays protected. The
match build passes no `/D`, so these do not exist there and its token stream is
unchanged.

### Engine — `milo-native-engine/src/platform/Mesh_Wgpu.cpp` (`138e160`)

`RndMesh::DrawShowing`'s two hardcoded name tests are replaced by one call to
the consumer seam the engine already declares, `ShouldSkipMesh`
(`platform/MeshFilter.h`). The call sits exactly where the old tests sat —
before `IncrementMeshDrawCalls()` — so even the draw-call counter is unchanged
for consumers that keep the rules. `ShouldSkipMesh` is now called with a
possibly-null `RndMat`; both existing implementations only read the name.

The material-less skip and everything else in the draw path are untouched.

---

## 4. Engine-change record — scoping across the three consumers

The engine now has three consumers. `Mesh_Wgpu.cpp` is compiled **only in the
`dc3` GPU-backend flavor** (`milo-native-engine/CMakeLists.txt:298-307`), which
bounds the blast radius before anything else.

| consumer | effect | evidence |
|---|---|---|
| `dc3-native` | **none.** `MeshFilter.cpp` keeps both rules verbatim, and the skip still precedes the draw-call counter. | `milo-tests` 362 passed / 0 failed, identical to baseline; 35 DC3 renders byte-identical; `dc3-native` + `render-test` relink clean |
| `milo-viewer` | `_lod` rule off, LOD groups used instead | §1, §6 |
| `rb3-xenon` | the two DC3 filters no longer apply; its `ShouldSkipMesh` returns `false` by design (`native/src/rb3_render_glue.cpp:45`) | `rb3-render` rebuilt **at its X3 commit `625b14f9`** against this engine: `ALL GATES PASSED`, both PNGs byte-identical — `30692a8d02c1ada0` (crowd_female01), `cbdb29fa95a5b574` (tracksystem_meshes), matching the shas recorded in the X3 doc |
| `rb3-Wii` | **cannot be affected** — builds the `rb3` flavor, does not compile `Mesh_Wgpu.cpp`, defines no `ShouldSkipMesh` | grep of `rb3/native/`: no `ShouldSkipMesh` anywhere |

**No pin was bumped.** `dc3-decomp/native/CMakeLists.txt`'s `MILO_ENGINE_PIN`
and `rb3/native/CMakeLists.txt`'s are both left alone. Engine SHA for the rb3
coordinator: **`138e160`**.

⚠ `rb3-xenon`'s `main` currently **cannot link `rb3-render`** — its own commit
`81d23046` (110 commits after X3) left `RndEnvAnim::Save` referencing an
undefined `operator<< <RndEnvAnim>`. That break predates and is unrelated to
this work; verification was therefore done in a worktree at `625b14f9`. The
failed link in their `native/build/` removed the stale `rb3-render` binary
there; it was already unbuildable at their HEAD.

---

## 5. Reproduce

```bash
cd dc3-decomp/native
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DDawn_DIR=$HOME/code/milohax/dc3-decomp-deps/dawn/lib/cmake/Dawn
cmake --build build --target milo-viewer -j 32

# run from native/ so the game archive resolves (or set DC3_DATA)
./build/milo-viewer ~/code/milohax/rb3/orig-assets/extracted/char/crowd/gen/crowd_female01.milo_xbox \
    --screenshot /tmp/crowd_female01.png --verbose
./build/milo-viewer ~/code/milohax/rb3/orig-assets/extracted/ui/track/gen/tracksystem_meshes.milo_xbox \
    --screenshot /tmp/tracksystem_meshes.png
```

New flags: `--show-all-lods`, `--no-fallback-material`.

Coverage / distinct-colour metric:

```python
from PIL import Image; from collections import Counter
px = list(Image.open(p).convert("RGB").getdata()); c = Counter(px)
print((len(px) - c.most_common(1)[0][1]) / len(px), len(c))
```

---

## 6. Gate baselines and results

Baselines were captured **before** any edit, from a build of `13b583df^` +
engine `2ea8e34` in throwaway worktrees, so "unchanged" means measured, not
assumed.

* **DC3 render sweep — 35 assets, all byte-identical.** Every entry in
  `native/scripts/render_screenshots.sh` (15 props, 12 dancers, 2 crowd
  characters, 2 glitterati sets) plus `world/rollerrink/gen/rollerrink`,
  `world/dclive/gen/dclive`, `world/glitterati/gen/glitterati_set`.
* **`milo-tests`** — 440 ran, **362 passed, 0 failed**, before and after.
  (A first run showed 360; the 2-test delta was `HeadlessBootTest.*` skipping
  because `dc3-native` was not in the fresh build dir. With it present: both
  `OK`.)
* **`dc3-native` and `render-test`** relink clean.
* **Determinism** — both RB3 cells rendered twice, `sha256`-identical:
  `8a89838aba267422` (crowd_female01), `578fca2d013d1697` (tracksystem_meshes).

---

## 7. Not fixed, and why

* **`tracksystem_meshes` reaches 1.05% coverage where rb3-xenon reaches 4.54%.**
  Both render the same geometry; xenon frames tighter because it applies the
  percentile bound unconditionally on every axis, while this fix only applies it
  to axes with a demonstrated outlier. The remaining Y span (≈420 units) is real
  track geometry, not a second outlier. Framing preference, not a defect.
* **The garbage vertex itself.** `_inactive_crash_gem_top.mesh` has 0 vertices
  and one tracksystem mesh decodes `Y=121458` in both engines. That is
  asset/decode triage and is owed to whoever owns the compressed-vertex reader,
  not to the framing layer (§1c).
* **Flat lighting on `crowd_female01`.** The milo ships no `RndEnviron` and the
  viewer does not synthesise one (rb3-xenon does, which is most of why its
  coverage/colour numbers are higher). Deliberate: the viewer shows the asset.
  `--light` / `--ambient` are available.
* **T-pose.** No `CharClip` is driven. Out of scope.
