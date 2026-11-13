# Live-state probing: reading engine state from a running game

How to read and compare live engine state (object properties, flows, camera)
from a running `dc3-native` using DTA, so a visual bug can be settled by
measurement instead of guesswork.

Last verified against the tree: 2026-11-13 (np-docs @ 2d678db7e).

Related:
- [docs/tools/HTTP_DEBUG_SERVER.md](../../tools/HTTP_DEBUG_SERVER.md): the
  endpoint reference (result JSON shapes, size limits, crash recovery).
- [docs/tools/STATE_DIFF.md](../../tools/STATE_DIFF.md): the probe library and
  differ under `tools/state_diff/`.
- [docs/tools/console/CONSOLE_DTA_EVAL.md](../../tools/console/CONSOLE_DTA_EVAL.md):
  the same DTA channel on a real Xbox 360 and on Xenia (`tools/console/dc3_eval.py`).

## 1. The channel

Start the port with the HTTP server enabled. `scripts/dc3-agent-test.sh` sets
`DC3_HTTP=1 DC3_FAST_BOOT=1 DC3_TEL=1`. The default port is 9090. Set
`DC3_HTTP_PORT` to choose another; `0` or `auto` picks a free port and prints
it on stdout as `DC3_HTTP_PORT=<n>`.

```bash
scripts/dc3-agent-test.sh
curl -s localhost:9090/api/health
curl -s -X POST localhost:9090/api/dta/eval -d '{+ 1 2}'      # -> value 3
```

`POST /api/dta/eval` takes raw DTA, or JSON `{"expr":"…"}`. It runs on the main
thread and returns `{"ok":true,"data":{"type":…,"value":…}}`. Several commands
can be sent at once separated by `}{`; the last one's value is returned.

Size caps live in `native/src/platform/DtaEvalSupport.h`:
- script: `DtaEval::kMaxScriptBytes` = 16384
- result: `DtaEval::kMaxResultBytes` = 32768

Both deliberately equal the RB3Enhanced console channel's `RB3E_DTA_SCRIPT_MAX`
and `RB3E_DTA_OUTPUT_MAX`. Exceeding either returns an explicit HTTP 413; the
result is never silently truncated. A probe sized for localhost therefore also
fits the console.

Probe design rules that follow:

1. **Pack scalars into one short result.** Use
   `{sprintf $s "%s|%d|%g" …}` and return `$s`. Do not dump an object tree in
   one call.
2. **Enumerate in two passes.** First `{size {object_list $dir Mat TRUE}}`,
   then fetch element `i` per request.
3. **A single request is frame-atomic; a sequence of requests is not.**
   Freeze the game first (section 4).
4. **Balance your braces.** Send only trusted input, especially to a console.

Other endpoints useful while probing (`native/src/platform/HttpServer.cpp`):
- `GET /api/screenshot`
- `GET /api/screen`, and the long-poll `/api/screen/wait/<name>`
- `GET /api/frame/wait/<n>`
- `GET /api/objects[/<name>[/children]]`, `/api/scene/tree`
- `POST /api/input/press`, `/api/input/sequence`

## 2. Read primitives

Properties are read through `Hmx::Object::SyncProperty`, reached by making the
object `this`:

```
{with <object> [<prop>]}                 ; DataWith (src/system/obj/DataFunc.cpp)
{with <object> [<prop> <subprop>]}       ; nested path, e.g. [world_xfm x]
{<object> get (<prop> <subprop>)}        ; Hmx::Object::OnGet
{<object> set (<prop> <subprop>) <v>}    ; Hmx::Object::OnSet (prop/value pairs)
```

Lookup and inspection:

```
{$dir find "name"}                        ; ObjectDir::OnFind; returns null, does not fail, when absent
{find_obj <dir> "sub" "name"}             ; multi-level descend
{object_list <dir> <ClassName> <recurse>}
{exists "name"} {type <node>} {size <arr>} {elem <arr> <i>}
{$obj showing} {$obj get_sphere} {$obj get_local_pos} {$obj get_world_forward}
{rnd aspect} {rnd screen_width} {rnd screen_height}
{ui current_screen}
{taskmgr seconds} {taskmgr beat}
```

Globals registered as probe roots (`SetName(…, ObjectDir::Main())` in `src/`):
`ui`, `rnd`, `taskmgr`, `game`, `master`, `audio`, `gamemode`, `song_mgr`,
`meta_performer`, `shell_input`, `profile_mgr`, `depth_buffer`. The data
variables `$world` (`src/system/world/Dir.cpp`) and `$hamdirector`
(`src/system/hamobj/HamDirector.cpp`) are also available.

`with`, `object_list` and `set_this` are registered in
`src/system/obj/DataFunc.cpp`, the shared Milo engine code, so the same probe
text works on RB3.

## 3. Property vocabulary for visual bugs

These names were checked against each class's `BEGIN_PROPSYNCS` block:

| Class | File (under `src/system/`) | Discriminating properties |
|---|---|---|
| `RndDrawable` | `rndobj/Draw.cpp` | `showing`, `draw_order`, `sphere`, `clip_planes` |
| `RndTransformable` | `rndobj/Trans.cpp` | `trans_parent`, `trans_constraint`, `trans_target`, `local_xfm`, `world_xfm` |
| `RndMat` | `rndobj/Mat.cpp` | `blend`, `color`, `alpha`, `z_mode`, `stencil_mode`, `cull`, `intensify`, `use_environ`, `prelit`, `alpha_cut`, `alpha_threshold`, `alpha_write`, `force_alpha_write`, `next_pass`, `diffuse_tex`, `diffuse_tex2`, `normal_map`, `emissive_map`, `environ_map`, `emissive_multiplier`, `specular_rgb`, `rim_rgb`, `refract_enabled`, `refract_strength`, `tex_gen`, `tex_wrap`, `tex_xfm`, `per_pixel_lit`, `metamaterial` |
| `RndTex` | `rndobj/Tex.cpp` | `width`, `height`, `bpp`, `mip_map_k`, `file_path` |
| `RndCam` | `rndobj/Cam.cpp` | `near_plane`, `far_plane`, `y_fov`, `z_range`, `screen_rect`, plus the `RndTransformable` set |
| `RndLight` | `rndobj/Lit.cpp` | `light_type`, `color`, `intensity`, `range`, `falloff_start`, `topradius`, `botradius`, `texture`, `cube_texture`, `texture_xfm`, `projected_blend`, `shadow_objects` |
| `RndEnviron` | `rndobj/Env.cpp` | `lights_real`, `lights_approx`, `ambient_color`, `ambient_alpha`, `fog_enable`, `fog_start`, `fog_end`, `fog_color`, `ambient_fog_owner`, `ao_strength`, `exposure`, `white_point`, `tone_map`, `use_color_adjust`, `hue`, `saturation`, `brightness`, `contrast`, `in_lo`, `in_hi`, `out_lo`, `out_hi` |
| `RndPostProc` | `rndobj/PostProc.cpp` | `priority` and the bloom / colour-transform / noise / motion-blur families |
| `RndText` | `rndobj/Text.cpp` | `text`, `align`, `caps_mode`, `width`, `height`, `circle`, `fit_type`, `leading`, `indentation`, `markup`, `basic_markup`, `styles` |
| `RndMesh` | `rndobj/Mesh.cpp` | `mat`, `geom_owner`, `mutable`, `num_verts`, `num_faces`, `volume`, `has_valid_bones`, `bones`, `keep_mesh_data` |
| `RndGroup` | `rndobj/Group.cpp` | `objects`, `draw_only`, `sort_in_world` |
| `PanelDir` | `ui/PanelDir.cpp` | `cam`, `use_specified_cam`, `postprocs_before_draw`, `focus_component`, `owner_panel`, `front_view_only_panels`, `back_view_only_panels` |
| `WorldDir` | `world/Dir.cpp` | `hud`, `hud_filename`, `show_hud`, `hide_overrides`, `mat_overrides`, `bitmap_overrides`, `preset_overrides`, `camshot_overrides`, `test_light_preset_1`, `test_light_preset_2` |
| any `RndAnimatable` | `rndobj/Anim.cpp` | `rate`, `frame`, `start_frame`, `end_frame`. `[frame]` is the cheapest test of whether an animation is advancing. |

Value encodings (`src/system/obj/PropSync.cpp`):

- `Hmx::Color`: a **packed int** (`Color::Pack()`), one integer per colour,
  which makes it ideal for diffing.
- `Vector3`: sub-paths `x`/`y`/`z`.
- `Transform`: `x`/`y`/`z` are the translation. Other names fall through to
  `Hmx::Matrix3`, which accepts `pitch`/`roll`/`yaw` (converted to degrees) and
  `x_scale`/`y_scale`/`z_scale`. So `[world_xfm x]` and `[world_xfm yaw]` both
  work.
- `Sphere`: `x`/`y`/`z`/`radius`. `Hmx::Rect`: `x`/`y`/`w`/`h`.
- Enum properties are ints. `RndMat` blend (`rndobj/BaseMaterial.h`) is
  `0 kBlendDest, 1 kBlendSrc, 2 kBlendAdd, 3 kBlendSrcAlpha,
  4 kBlendSrcAlphaAdd, 5 kBlendSubtract, 6 kBlendMultiply, 7 kPreMultAlpha`.
  Z mode is `0 kZModeDisable, 1 kZModeNormal, 2 kZModeTransparent, 3 kZModeForce`.
- Object-valued properties (`diffuse_tex`, `mat`, `trans_parent`, `next_pass`)
  come back as type `object`: the object **name** plus its `class`, or `null`.
  "Texture is null" versus "texture is `foo.tex`" is therefore directly visible.

## 4. Comparing two runs: put both sides at the same logical moment

A capture taken at mismatched moments mostly diffs animation phase. Enforce all
of the following, and record each value alongside the capture.

1. **Same screen.** `{ui current_screen}` must match. Long-poll so the port
   waits instead of racing: `curl 'localhost:9090/api/screen/wait/song_select'`.
2. **Same content.** Same song, venue, difficulty, character/outfit and player
   count. Force them with `{game load_new_song <sym>}` and
   `{game load_new_venue <sym>}` (`src/lazer/game/Game.cpp`).
3. **Same song position.** Seek with `{game jump <ms>}`, then read the actual
   position back with `{game get_song_ms}`. The two sides will differ by a frame
   or so; the comparison must tolerate that.
4. **Freeze before probing.** Send `{game set_paused 1}` and
   `{game set_time_paused 1}` (the latter freezes the song clock). Record
   `{taskmgr seconds}` and `{taskmgr beat}` so you can show both sides were
   frozen at comparable ticks.
5. **Pin the camera shot.** Use `{$hamdirector force_shot <name>}`,
   `{$hamdirector cycle_shot}` or `{$hamdirector select_camera …}`.
   `{$hamdirector camera_source}` returns the venue. Without this, camera-cut
   phase alone will differ.

Menu screens have no song clock. Wait a fixed number of frames after the screen
becomes current (enter flows done; `/api/frame/wait/<n>` helps), then record
`{taskmgr seconds}`.

**Take a screenshot at every capture point on both sides**
(`/api/screenshot` on the port). A matching state diff on a frame that still
looks wrong tells you the bug is invisible at the DTA level. Without the
pixels you cannot tell "no divergence" from "divergence this tool cannot see".

**Record which workarounds were active.** A capture with a native workaround
enabled diffs the workaround, not the bug. Record every non-default env var in
the capture header. (The engine's flag catalogue,
`../milo-native-engine/src/platform/NativeCompatFlags.gen.inc`, is mostly RB3
flags.)

## 5. When a state diff cannot see it: the position sweep

Some state is not `SyncProperty`-visible at all, for example `RndText`'s
`mScrollPos`, `mScrollTimer` and `mScrollCopies`. A DTA diff will then read
identical on both sides. The method that works is a **sweep against the
framebuffer**:

1. Pick one transform property of the suspect object, e.g. `local_xfm x`.
2. Step it through a handful of values with `{$obj set (local_xfm x) <v>}`.
3. After each step, grab `/api/screenshot` and measure which screen columns or
   rows the object's pixels occupy.
4. Pixels that **track** the transform mean the geometry is fine. Pixels that
   **stay pinned** while the transform moves mean something screen-anchored is
   occluding or clipping it.

`/api/screenshot` works under `MILO_HEADLESS=1` on a machine with no display, so
a sandboxed agent can run this loop, given GPU access
(`dangerouslyDisableSandbox: true`).

**Prefer an invariant over a diff where one exists.** Example: for a pure
rotation parent, `child world - parent world` must equal
`child local.v × parent world.m`, so bone-length ratios are identically 1. A
violation is a bug with no ground truth needed. That idea comes from RB3's
`BAND_ANIM_ANAT` check (RB3 only) and applies to any DC3 skeleton.

## 6. RB3-side prior art (RB3 only, not in this repo)

- `/api/uidump` and `/api/drawlog?prov=1`, documented in
  `../rb3/docs/native/uidump-forensics.md`. These give per-draw provenance and
  distinguish authored material colour from the effective bound colour. DC3's
  HTTP server has no equivalent endpoint.
- `../rb3/scripts/analysis/visual_diff.py`: strict plus perceptual pixel diff
  with a machine-readable verdict line.
- `../rb3/scripts/native/band-closeup-capture.py`: matched shot/song-ms A/B
  capture with a determinism gate.

For DC3 against the original game, use the Xenia path instead (the
`xenia-gameplay` skill and [docs/tools/console/CONSOLE_DTA_EVAL.md](../../tools/console/CONSOLE_DTA_EVAL.md)).

## Sources

- `native/src/platform/HttpServer.cpp`, `native/src/platform/DtaEvalSupport.h`,
  `src/system/obj/{DataFunc,PropSync,Object}.cpp`, the `BEGIN_PROPSYNCS` blocks
  listed above, `src/lazer/game/Game.cpp`, `src/system/hamobj/HamDirector.cpp`.
- Archived: [native/VISUAL_BUG_PROBE_PLAN.md](../../archive/2026-11-13-native-doc-audit/native/VISUAL_BUG_PROBE_PLAN.md)
  §0.1–0.3, §4, §5.2, §6.9. Its console limits (`0x400` path, 1024-byte result)
  describe an older RB3Enhanced channel. RB3Enhanced's `source/DTAEval.c` now
  uses `RB3E_DTA_SCRIPT_MAX` / `RB3E_DTA_OUTPUT_MAX`.
