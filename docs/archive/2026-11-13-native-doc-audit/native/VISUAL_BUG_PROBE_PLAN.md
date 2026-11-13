# Visual Bug Probe Plan — targets for live DTA state-diffing

**Purpose.** A sibling effort is bringing up the ability to evaluate arbitrary DTA on a
*running* game — on the native port today (`/api/dta/eval`, see
[HTTP_DEBUG_SERVER.md](../tools/HTTP_DEBUG_SERVER.md)) and imminently on a **real Xbox 360**
(RB3 via the RB3Enhanced command channel; DC3 via the debug XEX's remote surface). A third
effort owns the probe library, canonical dump format and ranked differ under `tools/state_diff/`.

This document is the **bug side** of that equation: which visual defects are actually open,
what live engine state discriminates between their competing causes, which ones a DTA-level
state diff will *nail* versus which will look **identical on both sides** and need a different
technique, and the console runbook to collect the captures.

This file does **not** define the dump format or the differ — that's `tools/state_diff/`.

> **Scope note.** "Visual" here means *what the frame looks like*. Perf, audio, input and load
> bugs are excluded unless they produce a visible artifact (e.g. T-posed crowd from late clips).

---

## 0. What the channel can actually do (read this before writing a probe)

Everything below is verified against source in this repo, `../rb3`, `../milo-native-engine` and
`../RB3Enhanced`. A probe that ignores these constraints will not run on hardware.

### 0.1 The two transports are asymmetric

| | Native port (dc3-native / rb3-native) | Real console (RB3Enhanced) |
|---|---|---|
| Endpoint | `POST /api/dta/eval` (body = raw DTA or `{"expr":"…"}`) | `GET /execute?script=<urlencoded>` on port **21070** (`0x524E`) |
| Source | `native/src/platform/HttpServer.cpp:883` | `../RB3Enhanced/source/net_http_server.c:360` |
| Gate | `DC3_HTTP=1` | `AllowScripts=1` in `rb3.ini` (`source/config.c:104`) |
| Result | JSON `{"ok":true,"data":{"type":…,"value":…}}` | plain text, `SerializeDataNode` of the result node |
| Thread | queued to main thread (`QueueAndWait`) | queued to main thread (`ExecuteDTAWithResult`, `net_http_server.c:165`) |
| **Script size limit** | body, effectively unbounded | **`decoded_path[0x400]`** → script must decode to **< ~1000 chars** |
| **Result size limit** | unbounded | **`PENDING_SCRIPT_RESULT_MAX 1024`** — output is truncated past 1023 bytes |
| Multi-command | `}{`-separated sequence supported | one top-level `{command}`, else the whole array runs |
| Robustness | DTA failures return an error | **`DataReadString` faults on unbalanced braces** — treat input as trusted |

**Consequences for probe design (non-negotiable):**

1. **One request ≈ one short packed line.** Pack up to ~10 scalars with
   `{sprintf $s "%s|%d|%g|%g" …}` and return `$s`. Do *not* try to dump an object tree in one
   call — you will silently lose the tail at byte 1024.
2. **Enumerate in two passes.** `{size {object_list $dir Mat TRUE}}` first, then fetch element
   `i` per request. Never return the whole `object_list`.
3. **Each request is frame-atomic but a probe *sequence* is not.** Freeze before probing
   (§5.2), or the diff is full of animation-phase noise.
4. **Balance your braces.** A malformed script crashes the console-side reader.

### 0.2 The read primitives

Property reads go through `Hmx::Object::SyncProperty`, which is reached from DTA by making the
object `this`:

```
{with <object> [<prop>]}                     ; DataWith → ExecuteScript(2, obj, …)  DataFunc.cpp:1312
{with <object> [<prop> <subprop>]}           ; nested path, e.g. [world_xfm x]
```

Message sends and object lookup:

```
{$dir find "name"}                 ; ObjectDir::OnFind — Dir.cpp:1078, does NOT fail when absent
{find_obj <dir> "sub" "name"}      ; multi-level descend  DataFunc.cpp:1167
{object_list <dir> <ClassName> <recurse>}   ; DataFunc.cpp:1434
{exists "name"} {type <node>} {size <arr>} {elem <arr> <i>}
{$obj showing} {$obj get_sphere} {$obj get_local_pos} {$obj get_world_forward}
{rnd aspect} {rnd screen_width} {rnd screen_height}    ; rndobj/Rnd.cpp:195-197
{ui current_screen}                                    ; ui/UI.cpp:938
{taskmgr seconds} {taskmgr beat}                       ; obj/Task.cpp:358,361
```

Verified sub-property names (`src/system/obj/PropSync.cpp`): `Sphere` → `x`/`y`/`z`/`radius`
(:212); `Hmx::Rect` → `x`/`y`/`w`/`h` (:245). `RndAnimatable` (`rndobj/Anim.cpp:37`) adds
`rate`, `frame`, `start_frame`, `end_frame` to every `RndPropAnim`/`CharClip`/`Flow`-driven
animatable — `[frame]` is the cheapest "is this anim actually advancing?" reading there is.

Registered globals usable as probe roots (`SetName(…, ObjectDir::Main())`):
`ui`, `rnd`, `taskmgr`, `game`, `master`, `audio`, `gamemode`, `song_mgr`, `meta_performer`,
`shell_input`, `profile_mgr`, `depth_buffer`, … plus the DataVariables `$world`
(`world/Dir.cpp:51`) and, on DC3, `$hamdirector` (`hamobj/HamDirector.cpp:127`).

Both `with` / `object_list` / `set_this` are registered identically in DC3 and RB3
(`src/system/obj/DataFunc.cpp:1745-1755` and `../rb3/src/system/obj/DataFunc.cpp:1595-1607`),
so **one probe library works on both games**.

### 0.3 The property vocabulary that matters for visuals

Verified `BEGIN_PROPSYNCS` blocks — these are the names a probe may use:

| Class | File | Discriminating properties |
|---|---|---|
| `RndDrawable` | `rndobj/Draw.cpp:36` | `showing`, `draw_order`, `sphere`, `clip_planes` |
| `RndTransformable` | `rndobj/Trans.cpp:98` | `trans_parent`, `trans_constraint`, `trans_target`, `local_xfm`, `world_xfm` |
| `RndMat` | `rndobj/Mat.cpp:117` | `blend`, `color`, `alpha`, `z_mode`, `stencil_mode`, `cull`, `intensify`, `use_environ`, `prelit`, `alpha_cut`, `alpha_threshold`, `alpha_write`, `force_alpha_write`, `next_pass`, `diffuse_tex`, `diffuse_tex2`, `normal_map`, `emissive_map`, `environ_map`, `emissive_multiplier`, `specular_rgb`, `rim_rgb`, `refract_enabled`, `refract_strength`, `tex_gen`, `tex_wrap`, `tex_xfm`, `per_pixel_lit`, `metamaterial` |
| `RndTex` | `rndobj/Tex.cpp:55` | `width`, `height`, `bpp` (get-only), `mip_map_k`, `file_path` |
| `RndCam` | `rndobj/Cam.cpp:61` | `near_plane`, `far_plane`, `y_fov`, `z_range`, `screen_rect` + all of `RndTransformable` |
| `RndLight` | `rndobj/Lit.cpp:167` | `light_type`, `color` (packed), `intensity`, `range`, `falloff_start`, `topradius`, `botradius`, `texture`, `cube_texture`, `texture_xfm`, `projected_blend`, `shadow_objects`, `animate_*_from_preset` |
| `RndEnviron` | `rndobj/Env.cpp` | `lights_real`, `lights_approx`, `ambient_color`, `ambient_alpha`, `fog_enable`, `fog_start`, `fog_end`, `fog_color`, `ambient_fog_owner`, `fade_*`, `ao_strength`, `exposure`, `white_point`, `tone_map`, `use_color_adjust`, `hue`/`saturation`/`brightness`/`contrast`/`in_lo`/`in_hi`/`out_lo`/`out_hi` |
| `RndPostProc` | `rndobj/PostProc.cpp` | `priority`, `bloom_*`, colour-xfm set, `noise_*`, `motion_blur_*`, `gradient_map*`, `kaleidoscope_*`, `hall_of_time_*` |
| `RndText` | `rndobj/Text.cpp` | `text`, `align`, `caps_mode`, `width`, `height`, `circle`, `fit_type`, `leading`, `indentation`, `markup`, `basic_markup`, `styles`, `scroll_*` |
| `RndMesh` | `rndobj/Mesh.cpp` | `mat`, `geom_owner`, `mutable`, `num_verts`, `num_faces`, `volume`, `has_valid_bones`, `bones`, `keep_mesh_data` |
| `RndGroup` | `rndobj/Group.cpp` | `objects`, `draw_only`, `sort_in_world` |
| `PanelDir` | `ui/PanelDir.cpp:226` | `cam`, `use_specified_cam`, `postprocs_before_draw`, `focus_component`, `owner_panel`, `front_view_only_panels`, `back_view_only_panels` |
| `WorldDir` | `world/Dir.cpp:166` | `hud`, `hud_filename`, `show_hud`, `hide_overrides`, `mat_overrides`, `bitmap_overrides`, `preset_overrides`, `camshot_overrides`, `test_light_preset_1/2` |

Value encodings you must decode in the differ (`src/system/obj/PropSync.cpp`):

- `Hmx::Color` → **packed int** (`color.Pack()`), line 32. Ideal for diffing — one integer.
- `Vector3` → sub-paths `x`/`y`/`z`, line 62.
- `Transform` → `x`/`y`/`z` for translation, then falls through to `Hmx::Matrix3` for
  `pitch`/`roll`/`yaw` (degrees) and scale, lines 89 & 185. So `[world_xfm x]`,
  `[world_xfm yaw]` etc. all work.
- Enum props are **ints**. `RndMat::Blend` (`rndobj/BaseMaterial.h:108`):
  `0 kBlendDest, 1 kBlendSrc, 2 kBlendAdd, 3 kBlendSrcAlpha, 4 kBlendSrcAlphaAdd,
  5 kBlendSubtract, 6 kBlendMultiply, 7 kPreMultAlpha`. `ZMode` (`:64`):
  `0 Disable, 1 Normal, 2 Transparent, 3 Force`.
- Object-valued props (`diffuse_tex`, `mat`, `trans_parent`, `next_pass`) serialize as the
  object **name** — so "texture is null" vs "texture is `foo.tex`" is directly visible. This is
  the single most valuable signal in the whole toolkit.

### 0.4 Known dependency risks

- **DC3 console channel is unproven.** RB3's channel is real and shipping today
  (`/execute?script=`, result returned in the body). DC3's debug XEX remote surface has **not**
  been demonstrated in this tree. Mitigations, in order: (a) many DC3 bugs live in the *shared*
  Milo engine and can be settled from an RB3 console capture; (b) Xenia runs the DC3 debug XEX
  and renders all 9 screens (`project_xenia_async_stall` memory; commit chain up to `5084c6acd`)
  — a Xenia-hosted DTA channel is a legitimate DC3 stand-in for menu-screen state; (c) worst
  case, DC3 items become native-vs-Xenia diffs rather than native-vs-console.
- **DC3's renderer is a *fork*, not a symlink of the shared engine.** `native/src/platform/`
  and `native/src/gfx/` in this repo are a divergent older copy of
  `../milo-native-engine/src/`; `Rnd_Wgpu.cpp` is offset by 41 lines, `Rnd_Wgpu_RB3.cpp`
  (~5700 lines, where most RB3 render fixes live) exists **only** in the engine repo. A fix in
  one tree does not reach the other. Treat "RB3 fixed it" as *not* implying "DC3 has it".

---

## 1. Ranked bug list — one line of status each

Ranking is by **tractability under state-diffing** (T1 = the tool settles it; T3 = the tool is
blind), then by visual severity. Status is graded against the *newest* doc touching each item —
several older docs are stale and are called out in §6.

### Tier 1 — state-diff will settle these (probe specs in §2)

| # | Bug | Game | Status | Why T1 |
|---|---|---|---|---|
| 1 | Venue lighting dead: PropAnim-driven `RndLight`/`RndEnviron` changes never reach the frame | DC3 | **OPEN** | Every input (light colour/intensity/range, environ ambient/fog/exposure, PropAnim target resolution) is a DTA property |
| 2 | HUD merge target divergence — merger's `mDir` resolves to the wrong `PanelDir`, papered over by `SetHUD` | DC3 | **OPEN** (workaround live) | `WorldDir` `hud`/`hud_filename`/`show_hud` + child enumeration is pure state |
| 3 | Contradictory sibling flows co-activate on `main_screen`/`choose_mode` (`select`+`enter1`+`exit`) | DC3 | **OPEN** | The *result* is object `showing`/`alpha`/`world_xfm`; the console shows the correct set |
| 4 | `F7-SIDEBAR` — song-select sidebar backing panel never draws (`draws=0` on every candidate mesh) | RB3 | **OPEN** | "Does the object exist / is it `showing` / which `PanelDir` owns it" is exactly a state question |
| 5 | Letterboxing artifact — black bars at right edge at some camera angles | DC3 | **OPEN** | `RndCam::screen_rect` + `y_fov` + `z_range` are properties; that *is* the letterbox mechanism |
| 6 | `V4` — closeup harness venue lighting collapses to blackout + single yellow key | RB3 | **OPEN / faithfulness undecided** | Resolves "regression vs. authored mood" in one capture |
| 7 | Kinect `MeshFilter` blocklist is load-bearing (white opaque overlays without it) | DC3 | **OPEN** (permanent workaround) | Ask the console how *it* hides them: `showing=0`? `blend=kBlendDest`? null diffuse? |
| 8 | 29 zero-alpha background meshes never become visible (alpha floor `#if 0`'d) | DC3 | **OPEN** (accepted) | `RndMat::alpha` on console at the same moment answers it outright |
| 9 | `A7` walk-on frozen remnant — member parked at the shell-vignette spot during gameplay | RB3 | **OPEN** | `[world_xfm y]` on the member + its clip state |
| 10 | `turbo_shell` menu background camera orientation shifted/rotated vs Xbox | DC3 | **UNCERTAIN** (open as of 2026-03-23, never closed) | Camera `world_xfm` euler + `y_fov` diff is decisive |
| 11 | `F2-PILL` — HUD score pill translucent, no dark backing | RB3 | **OPEN** (chartered, never executed) | Discriminates authored-material vs shader: `blend`/`alpha`/`color`/`next_pass`/`refract_enabled` |
| 12 | Projected light textures (gobo cookies) + `kFakeSpot` unimplemented | DC3 | **OPEN** | `RndLight::texture` / `light_type` tells you whether content even uses them |
| 13 | `A11` `<alt>` markup renders as a raw letter on Previous-Best lines | RB3 | **OPEN** | `RndText::styles` + the alt font material are readable |
| 14 | DC3 `<alt>` markup unstyled (claimed "missing asset entries, not code") | DC3 | **OPEN / not-code-fixable** | Same probe as #13 — confirms or refutes the asset claim in one shot |

### Tier 2 — state-diff sees *part* of it (usually "is it configured?" but not "does it draw?")

| # | Bug | Game | Status | What the diff gets / misses |
|---|---|---|---|---|
| 15 | UI text positions offset ~15% vertically from Xbox | DC3 | **CLOSED as stated / REPLACED — measured 2026-08-02, see §6.9** | The *positioning* claim is confirmed stale: `motd.lbl`'s `world_xfm` matches the shipped `main.milo_xbox` byte-for-byte, `{rnd aspect}` = `kWidescreen` (`YRatio` 0.5625 = exact 16:9), and `turbo_shell.cam` reprojects the label to the observed pixel row within 1 px. **But the screen is not clean**: `main_screen`'s MOTD label was rendering a permanently-truncated string. Root cause was *not* placement — see §6.9 |
| 15a | Marquee/ticker `RndText` labels never scroll (`kFitScroll*`) | DC3 | **FIXED 2026-08-02** — two causes, see §6.9 | Invisible to a state diff: `mScrollPos`/`mScrollTimer`/`mWrapEnabled` are not `SyncProperty`-visible. Caught by sweeping the label's `local_xfm` and watching which screen columns stayed pinned |
| 15b | `RndDrawable::mClipPlanes` is a **no-op on native** — `WgpuRnd` never overrides `PushClipPlanesInternal` (only `DxRnd` implements it, `rnddx9/Rnd.cpp:279`) | DC3 | **OPEN** | `[clip_planes]` reads identically on both sides (`motd.lbl` reports 2) — a pure T3 "state agrees, frame differs". Authored gradient-mask quads (`mod_frame2_lt_gradient*.mesh`) currently hide the un-clipped overspill on `main_screen`, so it is cosmetically masked there |
| 15c | `RndText::mScrollCopies` is computed but **never read** — the marquee draws one copy, so a ticker has a dead gap between repetitions instead of chaining at `mTotalWidth` spacing | DC3 | **OPEN** | Not state-visible. Follow-on to §6.9 |
| 16 | `main_screen` overshell rows project off-bottom (y≈1.03 / 1.53) | DC3 | **UNCERTAIN** — flagged "likely intentionally offscreen" | Gets authored `world_xfm`; console settles intent immediately |
| 17 | Mixed-camera composition on `choose_mode_panel` (`turbo_shell.cam` chrome + `ui.cam` payload) | DC3 | **UNCERTAIN** (last status 2026-03-11) | `PanelDir::cam` / `use_specified_cam` per panel is readable; the compositing order is not |
| 18 | Bone garbage — leg/foot bones with ~1e16 translations, masked by identity fallback | DC3 | **OPEN** | Bone objects are `RndTransformable` → `[world_xfm x/y/z]` readable per-bone, but you can only sample a handful per request |
| 19 | Knee `.rotz` under-accumulation — dancers under-crouch | DC3 | **OPEN** | Same: sample knee `[local_xfm roll]` on both. Float print precision is the limiting factor |
| 20 | Missing post-proc effects (motion blur, gradient map, kaleidoscope, flicker, noise) | DC3 | **OPEN** in RENDERING_SYSTEM / **WONTFIX** in TODO | `RndPostProc` props tell you whether shipped content ever *sets* them — settles the WONTFIX argument |
| 21 | Shadow mapping "partial" vs "implemented"; `SpotlightDrawer` + `Reflection` untested | DC3 | **UNCERTAIN** (docs conflict) | `RndLight::shadow_objects`, `RndTex` type/size readable; the pass itself is not |
| 22 | RTT limitations: compressed RT silently skipped, nested RTT skipped, GPU tex leak | DC3 | **OPEN** | `RndTex` `width`/`height`/`file_path` and the consuming `RndMat::diffuse_tex` are readable; contents are not |
| 23 | `F6` hub night grade / neon-plate relative gap 2.33 vs 0.98 | RB3 | **OPEN**, blocked on UIGRADE | `RndEnviron` grade + `RndMat::emissive_multiplier` readable; the composite is not |
| 24 | `V6` endgame vignette — giant deformed character + white shard planes | RB3 | **OPEN, never re-captured** since the 2026-08-01 alias fix | Root transform is readable; deformation is not. **Re-shoot before probing** |
| 25 | Green/olive faces, magenta skin casts, fully-unlit characters | RB3 | **OPEN**, deferred pending a reference | The char-`RndEnviron` vs venue-`RndEnviron` split and light routing *is* state — this may promote to T1 |
| 26 | Crowd T-pose for first frames on web; camera-person clips never resolve | DC3 | **OPEN** (accepted / open) | Clip resolution is queryable; the pose is not |
| 27 | `F8` overshell card overlap at settle frame | RB3 | **OPEN**, unadjudicated 3 waves | Card `world_xfm` + `draw_order` readable — cheap to settle |

### Tier 3 — a DTA state diff will look **identical on both sides**; use something else

| # | Bug | Game | Status | Why blind — and what *would* catch it |
|---|---|---|---|---|
| 28 | `A5` magenta/pink full-frame wash (intermittent, boot-nondeterministic) | RB3 | **OPEN, unrooted** | **Proven**: `RB3_WASH_PROBE` showed pink boots have *byte-identical lighting inputs* to clean boots. The divergence is a downstream screen-space composite grade. → **framebuffer capture + post-proc shader input dump**, or an engine hook in `RB3PostProc` |
| 29 | `A8` / `F5` BandPatchMesh patch shards (tattoos/facepaint spikes) — twice bisect-reverted | RB3 | **OPEN, no hypothesis** | Lives in `RndMesh::Vert` data under an LP64 ABI mismatch. `[num_verts]`/`[num_faces]` will match; per-vertex data can't fit the 1 KB channel. Also **the existing bone-ratio gate is blind to it** (34/34 PASS on visibly exploded frames). → **vertex-buffer dump via an engine-side hook**, or GPU capture (`gpu-capture` skill) |
| 30 | Skinning / bone-palette divergences generally (hands "mitten", forearm float, crowd slivers) | RB3 (+DC3 fork) | mostly **FIXED 2026-08-01**, re-measure | The palette is composed in `Rnd_Wgpu_RB3.cpp` after all DTA-visible state. → **`BAND_ANIM_ANAT` invariant probe** (see §4.2) — a *self-evident invariant* beats a ground-truth diff here |
| 31 | Texture *content* differences: font atlas placeholder pixels, DXT decode fallback, hi-res sidecar offset bug | both | **OPEN** | `width`/`height`/`file_path` will match perfectly while the pixels differ. → **texture-hash dump via engine hook**, or GPU capture |
| 32 | Blend-state / depth-load / pass-boundary bugs (`BeginFramePass(false)` uses `LoadOp::Load` for depth "which is wrong") | both | **OPEN** (worked around) | Purely backend. The `RndMat` state will agree. → **RenderDoc / GFXReconstruct** (`gpu-debug`, `gpu-capture` skills) |
| 33 | Shader math divergences: lit-sum hotter than the console, point falloff `(1-d/r)²` vs inverse-linear, emissive multiplier defaults | both | **OPEN** (env-flag mitigations, several default-**OFF**) | The *inputs* (light colour/range/falloff, environ exposure) will match — that's the point. → **shader-input dump at the uniform-buffer level**, then reimplement the console's falloff curve |
| 34 | Occlusion queries missing — flares force-visible | DC3 | **OPEN** | Occlusion result is a GPU query, not engine state. → GPU capture, or an engine-side visibility log |
| 35 | Web-only canvas/viewport issues (1280×720 buffer pin, ImGui mouse scale) | DC3 | **FIXED** (`a53ac33b`) / residual engine-side | No console analogue at all |
| 36 | Dead `GetDrawMode() == 8` compare in `Mesh_Wgpu.cpp:206,:299` (enum tops out at `kDrawVelocity = 6`) → two two-sided-cull overrides dead on every consumer | **shared engine, affects DC3** | **OPEN**, found 2026-08-01 | A source bug, not a state divergence. → just fix it; it needs no capture |

### Already FIXED — do **not** spend console time (full list in §6)

DC3: text invisible / zero-height glyph quads (`df487ac8`); chaotic camera roll from
`Transform::LookAt` (`a9fc8528`); IK feet-in-floor (**shipped 2026-07-02**, `3fb97a37` — TODO.md
is stale); `Transform::Multiply` y/z swap; `LayerArray::Eval` LP64 bug; white rectangles across
all venues; "black venue" BC3 render-attachment bug; web metamaterials (1,636 errors → 0);
AppLabel WASM `call_indirect`; CharHair/cloth; move-card textures; intro-movie/autosave UI scale
(`f8392d46`).

RB3: `V1` branch-hands / vignette pose explosion (**root cause = alias-unsafe `Multiply` in
`math/Rot.cpp:736`, fixed + ratified 2026-08-01, 3650 detonations → 0**); results-screen
`NameToDrumVenue` SIGSEGV; assert-formatter SIGSEGV; web hub floating yellow quad; prop /
drumstick spike-fans; dark composite characters; walk-on knot (acute half); grey skin on web
(workaround); faceless characters; floating eyes/teeth; crowd "at origin" (**not a bug, twice
re-confirmed**); the whole 8-issue `render-polish-2026-06-11` campaign; RB3 feet-in-floor
(**does not reproduce on RB3** — DC3's is separate and was open until 2026-07-02).

---

## 2. Probe specs — the top 5

Each spec names the object path, the exact DTA, and **which reading decides which hypothesis**.

---

### PROBE-1 — DC3 venue lighting: are the PropAnims driving anything?

**On screen.** Venue stage lights, spotlights and environment do not animate. Light-catcher
overlay meshes were white blocks until forced prelit; TV/screen meshes are black slabs.
Lighting is static where the console pulses with the performance state.

**Background.** DC3 does *not* use `world_event` PropKeys or `LightPreset` objects (confirmed:
zero `LightPreset` instances in `rollerrink.milo_xbox`). Lighting is driven by venue Flows +
PropAnims (`env_char_lighting.anim`, `env_lights_environs.anim`, `environments_master.anim`)
writing directly onto `RndLight` / `RndEnviron`. Enter/Poll chain is confirmed intact; the
failure is downstream. (`docs/sessions/2026-03-27-venue-lighting-investigation.md`.)

**Competing hypotheses.**

- **H1 — PropAnim targets don't resolve.** The `PropKey` `ObjPtr`s fail to bind inside the venue
  dir at load, so the anims run against null. Everything downstream is correct but inert.
- **H2 — Targets resolve, properties never change.** The anims are not being ticked (AnimTask
  not registered / wrong timeline), so light properties sit at their authored load values.
- **H3 — Properties change on both sides identically; the renderer ignores them.**
  `WgpuRnd::WriteSceneUniforms` doesn't re-read `RndEnviron::mLights` / light colour after the
  first frame. **This is a Tier-3 outcome** — see the fallback.
- **H4 — Wrong environ is selected.** The character environ vs venue environ split picks a
  different `RndEnviron` on native, so the right values are written to the wrong object.

**Discriminating state.** Capture at **three** song moments (§5.3) so you see change over time.

```dta
; A. resolve the venue world and count its lights (2 requests)
{set $v {$hamdirector get_venue_world}}
{size {object_list $v Light TRUE}}
{size {object_list $v Environ TRUE}}

; B. per-light identity + drive state — ONE light per request, i = 0..n-1
{set $l {elem {object_list $v Light TRUE} 0}}
{sprintf $s "%s|%d|%d|%g|%g|%g|%s|%d|%d|%d"
   $l  {with $l [light_type]}  {with $l [color]}  {with $l [intensity]}
   {with $l [range]} {with $l [falloff_start]}  {with $l [texture]}
   {with $l [animate_color_from_preset]} {with $l [animate_position_from_preset]}
   {with $l [animate_range_from_preset]}}

; C. the active environ
{set $e {elem {object_list $v Environ TRUE} 0}}
{sprintf $s "%s|%d|%d|%d|%g|%g|%g|%g|%g|%d|%d"
   $e {with $e [ambient_color]} {with $e [fog_enable]} {with $e [fog_color]}
   {with $e [fog_start]} {with $e [fog_end]} {with $e [exposure]}
   {with $e [white_point]} {with $e [ao_strength]}
   {with $e [tone_map]} {with $e [use_color_adjust]}}

; D. PropAnim target resolution — the H1 discriminator
{set $a {$v find "env_lights_environs.anim"}}
{sprintf $s "%s|%d|%g" $a {with $a [showing]} {with $a [frame]}}
```

**Decision table.**

| Reading | Verdict |
|---|---|
| Light/environ **counts differ** between console and port | Load/merge bug — the venue dir isn't what the console has. Fix first; everything else is downstream noise |
| Counts match, but a given light's `[color]`/`[intensity]` is **constant across all three moments on native and varies on console** | **H1 or H2.** Distinguish with probe D: if the anim object doesn't resolve or its `frame` doesn't advance → H2; if it advances but light values don't move → H1 (targets null) |
| Values **vary identically on both sides** | **H3** — the state is right, the renderer discards it. Escalate to the Tier-3 technique below |
| The *named* environ differs (probe C returns a different object name) | **H4** — environ selection bug; check the char-vs-venue environ gate |
| `[texture]` on console names a `.tex` and native returns null | Gobo/cookie assets aren't being bound — this is bug #12, not a lighting-drive bug |

**Tier-3 fallback for H3.** GPU-capture one frame (`gpu-capture` skill, works headless) and
inspect the light uniform buffer against the probe's values (`gpu-inspect`). If the buffer holds
the load-time values while the probe reports animated ones, the bug is in
`Rnd_Wgpu.cpp::WriteSceneUniforms`.

---

### PROBE-2 — DC3 HUD merge target: which `PanelDir` did `game_hud` actually merge into?

**On screen.** Flashcards / `hud_left` / `hud_right` were absent or crashing until a workaround
(`HamDirector::OnFileMerged` calls `world->SetHUD(hudDir)`) forced the swap. Flashcards were
still invisible after the swap (suspected zero-colour additive material).

**Competing hypotheses.**

- **H1 — `FileMerger::mDir` resolves to a different `PanelDir` on native.** The merge lands in
  a dir that isn't `WorldDir::mHUD`, so DTA handlers fire on the wrong object.
- **H2 — The merge lands correctly but the objects are killed afterwards.** The native
  `~ObjectDir` `NullifyAllRefs` cascade (`Dir.cpp:66-122`, no Xbox equivalent) reaches objects
  reparented during `MergeDirs`. Two failing tests define this:
  `SubdirsSurviveSourceDirDeletion`, `MergedObjectsSurviveParentDirReload`.
- **H3 — Objects exist and are parented right, but are invisible.** `showing=0`, or the
  flashcard material is additive with a zero colour.
- **H4 — `WorldDir::mHUD` is right on both, and the divergence is in draw order** — `mHUD` is
  removed from `mDraws` and drawn after `EndWorld()` on Xbox; native may draw it inline.

**Discriminating state.**

```dta
; A. the WorldDir's HUD identity and gate
{set $w $world}
{sprintf $s "%s|%s|%d|%s" $w {with $w [hud]} {with $w [show_hud]} {with $w [hud_filename]}}

; B. is the HUD dir the merge target? — enumerate its children
{set $h {with $world [hud]}}
{sprintf $s "%d|%d|%s|%s"
   {size {object_list $h Mesh TRUE}}
   {size {object_list $h Group TRUE}}
   {$h find "hud_left"} {$h find "hud_right"}}

; C. flashcard visibility — the H3 discriminator
{set $f {$h find "flash_cards"}}
{sprintf $s "%s|%d|%g" $f {with $f [showing]} {with $f [draw_order]}}
{set $m {$h find "flash_card_mat"}}   ; substitute the real mat name from the capture
{sprintf $s "%d|%d|%g|%d|%s"
   {with $m [blend]} {with $m [color]} {with $m [alpha]}
   {with $m [z_mode]} {with $m [diffuse_tex]}}

; D. which cam draws it
{sprintf $s "%s|%d" {with $h [cam]} {with $h [use_specified_cam]}}
```

**Decision table.**

| Reading | Verdict |
|---|---|
| `[hud]` names **different objects** between console and port (after removing the `SetHUD` workaround) | **H1 confirmed** — chase `FileMerger::mDir` resolution. This is the whole point of the capture |
| `[hud]` matches but B's child counts are **lower on native** | **H2** — the cascade is eating merged objects. Go fix `CollectCascadeDirs` / `NullifyAllRefs`, not the call sites |
| Children present on both, but native `[showing]` is 0 or `[alpha]` is 0 while console's isn't | **H3** — a flow/anim isn't driving visibility; cross-check with PROBE-3 |
| `[blend]` is `2` (`kBlendAdd`) with `[color]` packing to 0x00000000 on **both** sides | The material is *authored* additive-with-zero-colour: it is meant to be driven. Then it's a drive bug, not a material bug |
| Everything matches | **H4** — draw-order/compositing. Tier-3: compare `WorldDir::DrawShowing` ordering with a drawlog |

> **Capture hygiene:** to test H1 you must run the native side with the `SetHUD` workaround
> disabled, otherwise you are diffing the workaround's output, not the bug.

---

### PROBE-3 — DC3 contradictory sibling flows on `main_screen` / `choose_mode`

**On screen.** Mutually exclusive UI flows fire together on panel enter (`select.flow` +
`enter1.flow` + `exit.flow`; `show_game_mode_icon.flow` + `hide_game_mode_icon.flow`), leaving
panel elements at the wrong visibility/alpha/position. Recorded open at `DECOMP_GAPS.md:51-53`.

**Competing hypotheses.**

- **H1 — `ShouldActivateNativeFlow()` is too permissive.** Native activates `mStartMode==0` flows
  that the console leaves dormant; the console only runs the one flow the *message* selects.
- **H2 — Both sides activate the same flows; the console's later flow wins and native's
  ordering is reversed.** Same set, different final state.
- **H3 — The flows are equivalent and the visible difference comes from the panel camera**
  (`PanelDir::cam` / `use_specified_cam`), not from flow state — i.e. this is bug #17 in disguise.

**Discriminating state.** Do this at a *settled* moment (≥30 frames after the screen becomes
current), not during the transition.

```dta
; A. what screen and panel are we on
{sprintf $s "%s" {ui current_screen}}

; B. enumerate the flows in the panel dir and their run state
{set $p <panel dir object>}                 ; from /api/objects or {ui current_screen} walk
{size {object_list $p Flow TRUE}}
{set $f {elem {object_list $p Flow TRUE} 0}}
{sprintf $s "%s|%d" $f {with $f [showing]}}   ; repeat per index

; C. the OUTCOME — this is the reading that actually matters
{set $o {$p find "game_mode_icon"}}
{sprintf $s "%s|%d|%g|%g|%g|%g"
   $o {with $o [showing]} {with $o [draw_order]}
   {with $o [world_xfm x]} {with $o [world_xfm y]} {with $o [world_xfm z]}}
{set $m {with $o [mat]}}
{sprintf $s "%d|%d|%g|%d" {with $m [blend]} {with $m [color]} {with $m [alpha]} {with $m [z_mode]}}

; D. camera control — the H3 discriminator
{sprintf $s "%s|%d|%s" {with $p [cam]} {with $p [use_specified_cam]} {with $p [focus_component]}}
```

**Decision table.**

| Reading | Verdict |
|---|---|
| The set of Flow objects present is the same but **native's outcome object has both a show-anim and a hide-anim end state** — e.g. `showing=1` with `alpha=0`, or a position that is neither the authored ON nor OFF rest | **H1 confirmed.** Two contradictory flows both ran. Tighten `ShouldActivateNativeFlow` |
| Outcome differs but flow membership and per-flow `showing` are identical | **H2** — activation ordering. Instrument `FlowAnimate::Activate` order |
| Outcome object's `world_xfm` **matches the console exactly** yet it looks wrong on screen | **H3** — it's the camera (probe D), not the flows. Reroute to bug #17 |
| Native has *more* Flow objects in the dir than the console | A load/merge divergence, not a flow-activation one |

**Why this is high-value:** flow over-activation is the root class behind several DC3 UI
oddities (#8 zero-alpha meshes, #16 off-bottom overshell rows, and part of #17). One good
capture on `main_screen` and one on `choose_mode` likely settles three entries at once.

---

### PROBE-4 — RB3 `F7-SIDEBAR`: is the backing panel absent, hidden, or drawn off-screen?

**On screen.** On `song_select`, a hard vertical seam at the sidebar; the backdrop character
appears clipped. Retail has a near-opaque panel behind the difficulty grid. Existing evidence:
*nothing* clips the character (world.cam draws everything on-screen); every candidate backing
mesh reports `draws=0`. `song_select_details` never shows in quick-view.

**Competing hypotheses.**

- **H1 — The backing mesh object does not exist on native** (asset/merge gap).
- **H2 — It exists but `showing=0`** — a panel/flow never enables it.
- **H3 — It exists and shows, but its owning `PanelDir` isn't entered / draws under a camera
  that puts it off-screen.**
- **H4 — It exists, shows, and draws — but with a transparent material** (`blend=kBlendDest`
  or `alpha≈0`), so it contributes nothing.

**Discriminating state.** Console first — the console tells you the *name* of the object you're
looking for, which native currently can't.

```dta
; A. what is the current screen + its panels
{sprintf $s "%s" {ui current_screen}}

; B. on the console: enumerate meshes in the song-select panel dir and find the big one
{set $p <song_select panel dir>}
{size {object_list $p Mesh TRUE}}
{set $x {elem {object_list $p Mesh TRUE} <i>}}
{sprintf $s "%s|%d|%g|%g|%g|%g|%g"
   $x {with $x [showing]} {with $x [draw_order]}
   {with $x [world_xfm x]} {with $x [world_xfm y]} {with $x [world_xfm z]}
   {with $x [sphere radius]}}

; C. the material of the candidate backing mesh
{set $m {with $x [mat]}}
{sprintf $s "%d|%d|%g|%d|%s|%s"
   {with $m [blend]} {with $m [color]} {with $m [alpha]} {with $m [z_mode]}
   {with $m [diffuse_tex]} {with $m [next_pass]}}

; D. the details sub-panel
{set $d {$p find "song_select_details"}}
{sprintf $s "%s|%d|%s|%d" $d {with $d [showing]} {with $d [cam]} {with $d [use_specified_cam]}}
```

**Decision table.**

| Reading | Verdict |
|---|---|
| Console's mesh list contains a large quad the native list **lacks entirely** | **H1** — asset/merge gap. Name it, then find where it should load |
| Present on both, native `[showing]=0` | **H2** — flow/panel enable path. Cheapest fix |
| Present + showing on both, but native `[world_xfm]` differs, or `[cam]`/`use_specified_cam` on its `PanelDir` differ | **H3** — camera/placement |
| Identical object, showing, transform, **and** material — yet `draws=0` in the drawlog | **H4/Tier-3.** The state agrees; the divergence is in the draw-submission path. Use `/api/drawlog?prov=1` (§4.1) and compare `rectKind`/`pass`, not DTA state |

---

### PROBE-5 — DC3 letterboxing / camera framing: `screen_rect` vs FOV vs aspect

**On screen.** Black bars appear on the **right** side at some gameplay camera angles.
(`TODO.md:273`, still open.)

**Why this is a near-perfect state-diff target:** `RndCam::mScreenRect` is *literally* the
viewport rectangle, and it is a synced property (`rndobj/Cam.cpp:71`,
`SYNC_PROP_MODIFY(screen_rect, mScreenRect, UpdateLocal())`). A right-edge black bar is either a
`screen_rect` whose width < 1, a `y_fov`/aspect mismatch, or a backend viewport program.

**Competing hypotheses.**

- **H1 — Some camshot's `RndCam` carries a non-full `screen_rect`** and native honours it
  differently (or the console's is different).
- **H2 — Aspect/FOV mismatch.** `y_fov` matches but the effective aspect differs, so the
  horizontal extent is short. Note the known native mechanism: `RndCam::Select`
  (`Cam.cpp` `HX_NATIVE` block) programs a *pixel* viewport from `TheRnd.Width()/Height()`.
- **H3 — It's a letterbox *object*, not the camera** — a `LetterboxPanel`/black quad drawn at
  the wrong extent.
- **H4 — Backend viewport rounding** in `Rnd_Wgpu.cpp`. Tier-3.

**Discriminating state.** Capture at the *specific shot* that shows the bars — pin it first
(§5.4), then:

```dta
; A0. the renderer's own framing numbers — the fastest H2 test (Rnd.cpp:195-197)
{sprintf $s "%g|%d|%d" {rnd aspect} {rnd screen_width} {rnd screen_height}}

; A. the camera for the pinned shot, and its full frustum
;    (there is NO {rnd current_cam}; resolve the cam by name from the venue or the PanelDir)
{set $c {$v find "<shot>.cam"}}             ; or: {with <paneldir> [cam]}
{sprintf $s "%s|%g|%g|%g" $c
   {with $c [near_plane]} {with $c [far_plane]} {with $c [y_fov]}}
{sprintf $s "%g|%g|%g|%g"
   {with $c [screen_rect x]} {with $c [screen_rect y]}
   {with $c [screen_rect w]} {with $c [screen_rect h]}}
{sprintf $s "%g|%g" {with $c [z_range x]} {with $c [z_range y]}}

; B. camera pose
{sprintf $s "%g|%g|%g|%g|%g|%g"
   {with $c [world_xfm x]} {with $c [world_xfm y]} {with $c [world_xfm z]}
   {with $c [world_xfm pitch]} {with $c [world_xfm roll]} {with $c [world_xfm yaw]}}

; C. is there a letterbox drawable in the frame?
{set $lb {$world find "letterbox"}}
{sprintf $s "%s|%d|%g|%g" $lb {with $lb [showing]}
   {with $lb [world_xfm x]} {with $lb [sphere radius]}}
```

**Decision table.**

| Reading | Verdict |
|---|---|
| `screen_rect` differs between console and port on the offending shot | **H1** — the bug is in how the shot's rect is authored/restored. Highest-probability answer, and it is one number |
| `screen_rect` identical and full-frame, `y_fov` identical, pose identical, but **A0 differs** (`{rnd aspect}` / `screen_width` / `screen_height`) | **H2 confirmed, and it is one number.** Native drives the viewport from `TheRnd.Width()/Height()` in the `RndCam::Select` `HX_NATIVE` block; a non-16:9 target explains a right-edge bar directly |
| The `letterbox` object exists on native and is `showing=1` with a wrong extent | **H3** — it's an object, not the camera |
| Everything matches | **H4/Tier-3.** GPU-capture and read the actual `VkViewport`/`setViewport` args |

---

### Runners-up (specs deliberately kept short)

- **#7 Kinect `MeshFilter`.** For each blocklisted name (`silhouette_guy*`, `buffer_glass`,
  `*_crown.mesh`, `mic_*`, `pose_flash*`, `preview.mesh`) ask the console:
  `{sprintf $s "%s|%d|%d|%d|%s" $x {with $x [showing]} {with $m [blend]} {with $m [z_mode]}
  {with $m [diffuse_tex]}}`. If the console reports `showing=1` with a live render-target
  texture, native's problem is the *unfilled RT*, not the mesh — delete the blocklist entry and
  fill the target. If the console reports `showing=0`, native is missing the hide path — find
  what hides it. Either answer retires a permanent hack.
- **#8 zero-alpha background meshes.** Read `[alpha]` and `[blend]` on the 29 materials at a
  settled menu moment. Console non-zero → a DTA property-set path isn't running on native
  (likely the same root as PROBE-3). Console zero too → they are genuinely invisible; close the
  item as faithful.
- **#11 `F2-PILL`.** `{with $m [blend]} {with $m [alpha]} {with $m [color]}
  {with $m [refract_enabled]} {with $m [refract_strength]} {with $m [next_pass]}
  {with $m [alpha_write]} {with $m [force_alpha_write]}` on all three pill layers. If the
  authored state matches (expected), the hypothesis *"Wii refraction opacity does not derive
  from `diffuse.a`, native's generic unlit path multiplies by it"* is confirmed as a **shader**
  bug — a T3 outcome, but the probe *proves* it in five minutes instead of a wave.
- **#9 walk-on frozen remnant.** `{with $member [world_xfm y]}` per band member at gameplay +
  the same at the shell vignette. A member above world Y=100 during gameplay is the remnant.
  Console gives the correct Y; the delta is the whole finding.
- **#13/#14 `<alt>` markup.** `{with $t [markup]} {with $t [basic_markup]} {with $t [styles]}`
  plus the alt font material name. If the console's `styles` array is longer than native's, the
  "missing alt font style entries in the .milo assets" claim is **refuted** and it is a load bug.

---

## 3. Honest split: what this tool solves and what it cannot

**It solves (14 Tier-1 + parts of 13 Tier-2 entries).** Every bug whose cause is *wrong
property, missing object, wrong parent, wrong transform, wrong camera, wrong material
assignment, wrong visibility* — because all of those are `SyncProperty`-visible and the console
is the ground truth for them. The object-valued properties (`diffuse_tex`, `mat`,
`trans_parent`, `next_pass`, `hud`) are the highest-signal fields in the entire vocabulary: a
null-vs-named difference is unambiguous and needs no interpretation.

**It cannot see (9 Tier-3 entries).** The state on both sides will agree and the frames will
still differ, because the divergence is *downstream of DTA*:

| Class | Example | Use instead |
|---|---|---|
| Screen-space composite / grade | RB3 magenta wash (**already proven** to have byte-identical lighting inputs) | Framebuffer capture comparison + a `RB3PostProc` engine hook dumping the grade inputs |
| Vertex / index data | BandPatchMesh shards (LP64 `MeshVert` ABI) | Engine-side vertex-buffer dump; GPU capture (`gpu-capture` → `gpu-inspect`) |
| Bone palette composition | hands, forearm, crowd slivers | The **invariant probe** (§4.2) — no ground truth needed |
| Texture pixels | placeholder font atlas, DXT fallback, hi-res sidecar offset | Texture-hash dump via engine hook; GPU capture |
| Pipeline / pass / blend state | depth `LoadOp::Load`, per-draw pipeline rebuild | RenderDoc (`gpu-debug`) or GFXReconstruct (`gpu-capture`) |
| Shader math | lit-sum brightness, point falloff curve, emissive default | Uniform-buffer dump, then reimplement the console curve |
| GPU queries | flare occlusion | Engine-side visibility log |
| Source-level dead code | `GetDrawMode() == 8` vs enum max 6 | Nothing — just fix it |

**Rule of thumb for triage** (this is the existing DC3 heuristic and it holds):
*misplacement / wrong element / wrong visibility → shared-engine UI logic → state diff sees it.
Wrong blend / transparency / brightness / camera uniform → native backend → state diff is
blind.*

---

## 4. Existing instrumentation to reuse (do not rebuild)

### 4.1 RB3 `/api/uidump` + `/api/drawlog?prov=1` — the closest prior art
`../rb3/docs/native/uidump-forensics.md`. `drawlog` gives per-draw provenance
(`mesh, mat, cam, trans, panel, owner, matColor[4], boundColor[4], rect[4], rectKind, pass,
passDepthLoad`); `uidump` walks `TheUI.CurrentScreen()` → panels → `PanelDir`s recursively with
per-object `name/class/showing/drawOrder/world[12]/sphere`. Crucially it distinguishes
**authored** `matColor` from **post-binder effective** `boundColor` — which is exactly the
native-vs-console distinction our probes cannot make from DTA alone. There is a standing
byte-identical golden gate at **792 draws** (`--fixed-clock --canonical-order`).

Known limits worth inheriting: `BandRnd::DrawMesh` only (2D overlay quads never enter the log);
instanced list content and persistent HUD milos are not under a screen panel dir so the authored
walk misses them; `rectKind=1` (sphere fallback) projects to near-full-viewport.

### 4.2 The invariant oracle — steal this pattern
`BAND_ANIM_ANAT` (RB3 `BandCharacter.cpp:711-918`): `childWorld − parentWorld ≡
childLocal.v × parentWorld.m` in Milo's row-vector convention, so for a pure-rotation parent
basis the bone-length ratio is **identically 1.000**. A stretch is therefore an invariant
violation detectable with **no ground truth at all**. This found the 2026-08-01 `Rot.cpp:736`
alias bug (3650 detonations → 0). Where an invariant exists, prefer it over a diff.

### 4.3 Other harnesses
`scripts/analysis/visual_diff.py` (strict + perceptual pixel diff, machine-readable
`VISUAL_DIFF …` verdict line, calibrated: identical=100, black=27.6, noise=30.0,
different-screens=32.0); `scripts/native/band-closeup-capture.py` (matched-`(shot,songMs)` A/B
with a determinism gate — **but blind to patch-mesh shards**); `{rb3_pos_dump}` object-tree
position dump (GPU-independent); the Dolphin Wii oracle recipe
(`c8-ground-truth-2026-07-01/t2-dolphin-oracle.md`); the DC3 Xenia path (`xenia-gameplay` skill,
`dc3_inline_render` cvar → clean full-scene frames every frame).

---

## 5. Hardware capture runbook

Target: **one 30-minute console session yields captures that settle 4–6 bugs.** Everything here
is sequential and scripted; the only manual work is menu navigation.

### 5.1 Setup (5 min, once)

1. On the console, `rb3.ini`: `AllowScripts=1` (and CORS if you want a browser). Boot RB3 with
   RB3Enhanced. Note the console IP.
2. Sanity-check the channel — the arithmetic probe must return `3`:
   ```bash
   CONSOLE=${RB3_XBOX:?export RB3_XBOX=<console ip> first}
   curl -s "http://$CONSOLE:21070/execute?script=%7B%2B%201%202%7D"
   ```
   If this returns nothing, `AllowScripts` is off or the DLL didn't load. Stop and fix.
3. Record the build identity in the dump header: RB3E build tag (in the `Server:` response
   header), disc region, and DLC state.
4. On the workstation, start the port at the **matching** build:
   ```bash
   scripts/dc3-agent-test.sh            # DC3: DC3_HTTP=1 DC3_FAST_BOOT=1 DC3_TEL=1
   curl -s localhost:9090/api/health
   ```
5. **Neutralise the port's compat flags before capturing.** `../milo-native-engine/src/platform/
   NativeCompatFlags.h` + `NativeCompatFlags.gen.inc` list 101 `Workaround`-class flags, many
   default-**ON**. A capture taken with workarounds live diffs the *workaround*, not the bug.
   For each bug, disable only the flags in its own subsystem (e.g. `RB3_HANDS_MITTEN_OFF=1` for
   skinning; the DC3 `SetHUD` workaround for PROBE-2) and record which ones you flipped in the
   dump header.

### 5.2 Establishing "the same logical point" (this is what makes the diff readable)

An apples-to-oranges capture produces a diff that is 90 % animation phase. Enforce all four:

1. **Same screen.** `{ui current_screen}` must return the same symbol on both sides. Use the
   port's long-poll (`curl 'localhost:9090/api/screen/wait/song_select'`) so the port is
   *waiting*, not racing.
2. **Same content.** Same song, same venue, same difficulty, same character/outfit, same player
   count. On DC3, force it: `{game load_new_song <sym>}` / `{game load_new_venue <sym>}`.
   Record all of it in the header.
3. **Same song position.** Seek both sides to an identical millisecond, then verify:
   ```dta
   {game jump 45000}        ; DC3 Game.cpp:136  /  RB3 has {game jump …} too
   {game get_song_ms}       ; DC3 Game.cpp:124 ; RB3 Game.cpp:1303
   ```
   Record the *actual* returned ms on each side in the header — they will differ by a frame or
   two and the differ must tolerate that.
4. **Freeze before probing.** A probe *sequence* spans frames; a moving scene guarantees noise.
   ```dta
   {game set_paused 1}        ; DC3 Game.cpp:126
   {game set_time_paused 1}   ; DC3 Game.cpp:132 — freezes the song clock specifically
   ```
   Also capture `{taskmgr seconds}` and `{taskmgr beat}` (`obj/Task.cpp:358,361`) into the
   header so the differ can prove both sides were frozen at a comparable tick.

For **menu** screens there is no song clock: instead wait ≥30 frames after the screen becomes
current (settled state, all enter-flows done) and record `{taskmgr seconds}`. Menu screens are
where the highest-value DC3 bugs live (#2, #3, #8, #10, #16, #17) and they are the *easiest*
to synchronise — do these first.

### 5.3 The session script (25 min)

Run each block on the console, then re-run the *identical* probe list against the port. Save
raw responses; let the differ normalise.

| Block | Time | Screen / moment | Probes | Settles |
|---|---|---|---|---|
| **B1** | 4 min | `main_screen`, settled 30 frames | PROBE-3 (flows) + #8 (zero-alpha mats) + #16 (overshell row `world_xfm`) | DC3 #3, #8, #16 |
| **B2** | 4 min | `choose_mode`, settled | PROBE-3 again + #17 (`PanelDir::cam` / `use_specified_cam` per panel) + #10 (`turbo_shell.cam` pose + `y_fov`) | DC3 #3, #10, #17 |
| **B3** | 4 min | `song_select`, settled, cursor on a fixed song | PROBE-4 (sidebar) + #13 (`<alt>` on a Previous-Best line) | RB3 #4, #13 |
| **B4** | 6 min | Gameplay, **frozen** at three song positions (e.g. 20 s / 45 s / 75 s) | PROBE-1 (venue lighting, all three moments) + PROBE-5 (camera at the offending shot) | DC3 #1, #5, #12 |
| **B5** | 4 min | Gameplay, frozen, same moment as B4 | PROBE-2 (HUD) + #7 (Kinect mesh filter names) + #11 (`F2-PILL` materials) | DC3 #2, #7; RB3 #11 |
| **B6** | 3 min | Gameplay + endgame | #9 (walk-on remnant `world_xfm y` per member) + #24 (endgame vignette — **screenshot first**, it may be fixed) | RB3 #9, #24 |

**Take a screenshot at every capture point on both sides** (`/api/screenshot` on the port;
console-side capture by whatever means is available). The pixel pair is what tells you a
"matching" state diff is nonetheless wrong — i.e. the T1/T3 boundary. Without it you cannot
distinguish "no divergence" from "divergence the tool is blind to".

### 5.4 Pinning a specific camera shot (needed for PROBE-5 and B6)

RB3 has three native DTA accessors for exactly this — `{rb3_force_shot}`,
`{rb3_director_disable}`, `{rb3_cur_shot}` — which set `mDisabled=1` on the director then force
a named `BandCamShot`. DC3's equivalent is `{$hamdirector force_shot <name>}` /
`{$hamdirector cycle_shot}` / `{$hamdirector select_camera …}` (`HamDirector.cpp:156-158`), with
`{$hamdirector camera_source}` returning the venue. Pin the shot on **both** sides before
probing; otherwise the camera-cut phase alone will differ.

### 5.5 If a block returns nothing

- Empty body → the script hit the 1024-byte path limit, or a brace is unbalanced. Shorten it.
- Truncated at ~1023 chars → the result buffer. Split into more requests.
- `<parse error>` → `DataReadString` rejected it; check quoting after URL-decode.
- Correct-looking values that never change between the three B4 moments → you did not actually
  freeze/advance; re-check `{game get_song_ms}` per moment.

---

## 6. Stale-doc warnings (reporting a fixed bug as open wastes hardware time)

1. **`docs/native/TODO.md` is from 2026-05-26.** Its **P1 "IK feet-in-floor"** and the related
   `CharIKFoot::Poll` / empty-`mConstraints` framing are **superseded** — the faithful IK stack
   shipped 2026-07-02 (`06f1569d` → `7264136b` → `00c9b165` → `3fb97a37`), toes Xbox-exact,
   0 below floor. Its **"Missing metamaterials on web"** bullet is contradicted eight lines
   below by the 2026-03-18 fix. Its line 32 "Characters render but don't dance" is long fixed.
2. **`docs/native/PLAN.md` is from 2026-03-24.** Its "UI text offset ~15 %" and "turbo_shell
   camera orientation" entries predate the 2026-07-01/02 `[ui.cam]` + `Transform::LookAt` work
   and must be re-observed before being probed. The `[ui.cam]` FOV-widen that caused the ~63 %
   shrink was reverted in `f8392d46`; stock cam (fov 34.516, `(0,-768,0)`) is Xenia-exact.
3. **`docs/native/UI_ANIMATION_STATUS.md` headline (2026-03-12)** — "the performer is
   overlapped/misplaced or posed incorrectly" — is contradicted by Sessions 63/75. Not an open
   character-corruption bug.
4. **RB3: anything before 2026-08-01 blaming "the SKEL / C8 rotation-basis family" is suspect.**
   Wave 34 found the real mechanism was the alias-unsafe `Multiply` in `math/Rot.cpp:736`
   (3650 anatomy detonations → 0, maxRatio 1.945 → 1.000). Fifteen waves of "terminal" framing
   — the `RB3_HANDS_MITTEN` workaround, eight dead bind-side cells, forearm float, face shards —
   need **re-measurement, not re-litigation**. Try `RB3_HANDS_MITTEN_OFF=1` first: the
   workaround may now be the thing distorting the hands.
   *(Note this is the same bug class as DC3's own `Multiply` alias fix in `00c9b165` — a native
   rewrite dropping the PPC original's deliberate aliasing semantics. Third strike after
   `Transform::LookAt`.)*
5. **RB3 pixel evidence from `triage-2026-07-13` and `W33-V1-POSE` is void** — the host GPU had
   fallen back to Dawn's Null backend, so every screenshot was blank white / `max=0`. Verify a
   real Vulkan adapter before trusting any capture from that window.
6. **RB3 crowd "at origin" is NOT a bug** — twice re-confirmed (0/268 at origin). Do not
   re-investigate.
7. **RB3 feet-in-floor does not reproduce on RB3.** Do not port DC3's foot-plant to RB3; it
   would assert an already-satisfied pose.
8. **DC3 in-song native screenshots are black and get skipped** — the Bink intro movie never
   ends and covers the world. Do in-song visual checks on the **web** build, or fix the movie
   end condition first. This directly affects B4/B5 above: budget for it.
9. **"DC3 UI text ~15% offset" (item #15) was re-measured live on 2026-08-02 and the
   *offset* framing is dead — but the screen it was reported on was still wrong, for an
   unrelated reason.** Do not spend console time on item #15 as written.

   *What was actually wrong.* `main_screen`'s profile-hint bar (`motd.lbl`, a
   `kFitScrollMarqueeWrapAlways` MOTD ticker in `ui/main/gen/main.milo_xbox`, armed by
   `{$this motd_setup motd.lbl}` → `MainMenuPanel::MotdSetup`) rendered a **frozen,
   head-truncated** string — "in to a Gamer Profile to save progress and stats." — forever.
   Two independent causes, both now fixed:

   1. **`RndText::SizeCheck()` re-laid out the text every frame.** On Xbox this hook only
      emits an oversized-font warning; the native port replaced its whole body with
      `UpdateText()` (`7415c525f`, 2026-03-12). For a scrolling label `UpdateText()` reaches
      `FitTextScroll()`, which resets `mScrollTimer = 0` and `mScrollPos` — so the marquee
      was re-armed every frame and `UpdateScrollOffsets()` never got past its
      `mScrollTimer < mScrollDelay` early-out. Now gated on `!mWrapEnabled`.
   2. **`RndText::FitTextScroll()` was mis-decompiled (82.5%).** It (a) clobbered `mWidth`
      with the *measured text width* instead of restoring the authored box width, and
      (b) seeded `mLineWidths`/`mLineOffsets` with four `push_back`s where the target does
      three inserts. The target's seeding puts `mTotalWidth` in **both** lists — and
      `mTotalWidth` is precisely the sentinel that the (100 %-matching)
      `UpdateScrollOffsets()` tests in both of its wrap-reset branches
      (`if (firstWidth == mTotalWidth)` / `if (firstOffset == mTotalWidth)`). With our
      seeding neither branch could ever fire, so even an unfrozen marquee would have
      scrolled off once and never come back. Corrected → **82.5 % → 92.7 %**, and
      `motd.lbl`'s `[width]` now reads the authored **400.0** instead of a computed 300.41.

   *Method note worth reusing.* The discriminator was **not** a state diff — none of
   `mScrollPos`, `mScrollTimer`, `mWrapEnabled` or `mScrollCopies` is `SyncProperty`-visible.
   It was a **position sweep against `/api/screenshot`**: drive the label's `local_xfm.x`
   through 6 values and measure which screen columns the glyphs occupy. Columns that stay
   pinned while the transform moves prove screen-anchored occlusion; columns that track the
   transform prove the geometry is fine. `/api/screenshot` renders under `MILO_HEADLESS=1`
   on a machine with no display, so this loop is available to sandboxed agents.

---

## 7. Suggested first cut

If only one capture session is possible, do **B1 + B2 + B4** (menu flows, menu cameras, venue
lighting). Between them they settle DC3 #1, #3, #5, #8, #10, #12, #16, #17 — the largest cluster
of open, Tier-1, genuinely-unknown-cause visual bugs in either port, and all of them are on
screens that are trivially reproducible on both sides.
