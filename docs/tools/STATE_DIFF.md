# State Diff — cross-target engine state capture and divergence ranking

Turns *"the native port looks wrong"* into *"`world_panel`'s PanelDir has a null
`cam`, and 47 meshes lost the same material"*.

The tool captures live engine state through **DTA evaluation**, normalizes it
into a canonical JSON snapshot, and structurally diffs two snapshots into a
**ranked** divergence report. Both sides — the native port and a real console —
speak the same probe library, so a finding means the same thing on both.

```
probes/*.dta ──compile──► paged DTA ──transport──► raw records
                                                       │
                                                  normalize
                                                       │
                                              snapshot JSON ──diff──► ranked findings
```

| File | Role |
|------|------|
| `tools/state_diff/probes/*.dta` | Probe specs (declarative, DTA syntax) + `manifest.json` |
| `tools/state_diff/probes/dta_classes.json` | DTA class vocabulary — the names a class filter actually accepts |
| `tools/state_diff/probe.py` | Spec parser, DTA compiler, pager, runner |
| `tools/state_diff/budget.py` | Transport caps, brace validation, enumeration safety |
| `tools/state_diff/transport.py` | `Target` interface, dir scopes, rosters, native HTTP, console adapter, replay |
| `tools/state_diff/normalize.py` | Canonical snapshot format |
| `tools/state_diff/diff.py` | Ranked, collapsed differ |
| `tools/state_diff/noise.py` | Run-to-run noise floor measurement |
| `tools/state_diff/sweep.py` | Property sweep + pixel measurement (`--sweep` mode) |
| `tools/state_diff/tests/` | 66 unit tests, no engine required |

---

## Quick start

```bash
# 1. Boot the native port headless with the HTTP debug server
MILO_HEADLESS=1 DC3_FAST_TIME=1 MILO_MAX_FRAMES=100000000 \
  DC3_HTTP=1 DC3_FAST_BOOT=1 ./native/build/dc3-native &
until curl -sf 127.0.0.1:9090/api/health >/dev/null; do sleep 1; done

cd tools

# 2. What can I probe?
python3 -m state_diff.capture --list

# 3. Measure the noise floor FIRST (see below — this is not optional)
python3 -m state_diff.noise --runs 5 -o /tmp/noise_native.json

# 4. Capture
python3 -m state_diff.capture --probe draw_state -o /tmp/native.json

# 4b. Capture INSIDE a loaded panel dir — this is where the UI bugs are
python3 -m state_diff.capture --probe transforms --dir panel:main_panel \
    -o /tmp/native_panel.json

# 5. Diff two snapshots
python3 -m state_diff.diff /tmp/console.json /tmp/native.json \
    --noise-profile /tmp/noise_native.json

# 6. When every state field is CORRECT and the frame is still wrong: sweep
python3 -m state_diff.sweep --dir panel:main_panel --object motd.lbl \
    --prop 'local_xfm x' --values=-400,-200,0,200,400,600 \
    --region 40,578,1160,606 --bg-threshold 190 --repeat 3
```

> Use `127.0.0.1`, not `localhost`. The engine's cpp-httplib server binds IPv4
> only; on hosts where `localhost` resolves to `::1` first, every Python request
> fails with ECONNREFUSED while `curl` (which falls back) works. The default
> base URL already accounts for this.

---

## Scoping: `main` is not where the UI lives

`main` (`ObjectDir::Main()`) holds ~687 globals and **zero** of the objects
inside a loaded `.milo` panel dir. `ObjDirItr`'s recursion does not cross into a
`PanelDir` hung off a `Panel`, so `/api/objects?dir=main&recurse=true` cannot
see `motd.lbl`, `parent_motd.trans`, `motd_clip_left.trans` or
`motd_debloom.mesh` — the entire cast of the first UI bug this tool was pointed
at. Measured, dc3-native headless at `main_screen`:

```
{main iterate Cam ...}                    -> 5   [tex proc cam] … [ui.cam]
{{main_panel loaded_dir} iterate Cam ...} -> 2   camera1.cam, turbo_shell.cam
```

Disjoint sets. So every scope-bearing probe takes a **dir spec**, in the spec
(`(scope (dir …))`) or on the CLI (`--dir`):

| Form | Compiles to | Use |
|------|-------------|-----|
| `main` *(default)* | `main` | the globals |
| `panel:main_panel` | `{main_panel loaded_dir}` | **a loaded panel dir** |
| `{<any DTA expr>}` | verbatim | anything else (`{$world_panel loaded_dir}`, …) |

The compiled page binds the dir **once** —
`{do ($s "") ($o 0) ($p 0) ($d {main_panel loaded_dir}) … {find_obj $d "motd.lbl"} …}`
— so a panel scope costs the same script bytes per object as `main`, and the
paging budgets are unchanged. The dir expression's own length is charged
against the script cap (`Probe.wrapper_len()`), so it can never silently push a
page over.

### Scope CLI

Shared by `capture` and `noise`, so a panel scope can have its own measured
noise floor:

| Flag | Effect |
|------|--------|
| `--dir SPEC` | scope to a dir (above) |
| `--names A,B` | read only these; still two-pass, a name enumeration did not return is **refused** |
| `--classes A,B` | **exact** class post-filter |
| `--limit N` | cap the read set |
| `--no-recurse` | this dir only (routes to `iterate_self`) |
| `--enumerate auto\|object_list\|iterate` | enumeration primitive |

`transforms.dta`'s header used to promise `--names` / `--classes`; they exist
now.

### Enumeration primitives — measured, not assumed

Against `{main_panel loaded_dir}` on dc3-native headless, 2026-08-02:

| Primitive | Result | Used for |
|-----------|--------|----------|
| `{object_list $d <Class> FALSE}` | **works** — a *sorted* `DataArray` of name strings (`Utl.cpp:289`), indexable with `{elem $a $i}` | **default** when recursing |
| `{$d iterate <Class> $o {…}}` | **works** — 57 `Trans`, 45 `Draw`, 2 `Cam` | fallback (`--enumerate iterate`) |
| `{$d iterate_self <Class> $o {…}}` | **works**, non-recursive | `--no-recurse` |
| `{find_obj $d "<name>"}` | works, null when absent | every property read |
| `/api/objects?dir=…` | native-only, and cannot name a `PanelDir` reached via a `Panel` | `main` fast path |

Both enumeration back ends were run against all shipped probes on the same
build: **identical snapshots**, byte-for-byte after eliding volatiles, for
`transforms` (57), `hierarchy` (620) and `materials` (49). That cross-check is
what licenses trusting either.

`object_list` is the default because it returns a **sorted** array with a real
cursor, so paging is `{foreach_int $i lo {min hi {size $a}} …}` and the
traversal order is deterministic for free. `iterate` has no cursor, so its
paging re-walks the dir per window and emits only the ordinals in range.

> `object_list` used to SIGSEGV and three of them killed the engine. The bug
> was never in `object_list`: `DataArray::SortNodes` hardcoded **8** as the
> `qsort` element size — `sizeof(DataNode)` on PPC32 but **16** on LP64 native
> — so it strided over half-nodes, and `ObjectList` ends with `SortNodes(0)`.
> Fixed in `8c73183d`; PPC codegen verified byte-identical, so no `HX_NATIVE`
> gate. Any DTA in the game that sorted an array was affected, not just this.

### Class filters take **DTA** class names, not C++ class names

This one fails **silently, as an empty result**, which is indistinguishable
from "there are no such objects" — the exact failure that makes a state diff
look clean while it is blind. Filtering resolves through the shipped `objects`
superclass graph (`IsASubclass` → `SystemConfig("objects", child)`), which is
keyed by DTA names. Measured on `main_panel`:

| You might write | Result | What works | Result |
|---|---|---|---|
| `RndDrawable` | **0** | `Draw` | 45 |
| `RndGroup` | **0** | `Group` | 7 |
| — | — | `UILabel` / `HamLabel` | 6 |
| — | — | `UIComponent` | 7 |

Three defences, because a silent zero is unacceptable:

1. **`probes/dta_classes.json`** ships the vocabulary (269 names + the 199-entry
   superclass graph), extracted from `orig-assets/extracted/**/*objects.dta` —
   the same data `SystemConfig("objects")` is built from — plus the C++-alias
   table and the live counts above.
2. **`probe.validate_classes()`** runs *before* the capture and names the
   replacement: *"class `'RndGroup'` is a C++ class name and will enumerate ZERO
   (measured). Use the DTA name `'Group'`."*
3. **Any class filter that enumerates zero is recorded as a capture error**, as
   is an exact `--classes` filter that keeps nothing out of a non-empty roster
   (that error lists the classes actually seen). Capture errors are a `BLOCKER`
   in the differ, so a blind run cannot masquerade as a clean one.

Not fatal, only loud: `IsASubclass` short-circuits on `child == parent`
(`Utl.cpp:67`), so a literal `ClassName` absent from the graph still works.
`UIScreen` enumerates the 3 objects whose class is literally `UIScreen` — and
`HamScreen` appears in no `objects.dta`, so `{main iterate UIScreen}` yields 3,
not 296. **The real console behaves identically**; this is a cross-target
invariant, not a native divergence. Likewise `FAIL: Couldn't find 'HamScreen'
in array` in the log comes from a discarded lookup the target also performs —
not an error signal.

---

## Probe inventory

Each probe is scoped and parameterized so output stays diffable. Request counts
are for 60 objects under the default `portable` transport profile; see
`probes/manifest.json` for the generated, always-current table.

| Probe | What it reads | What it discriminates | Req |
|-------|---------------|-----------------------|-----|
| `screen_state` | active/transition/bottom screen, focus panel, `in_transition`, render surface size | **Run this first.** The two sides not being on the same screen at all; resolution/aspect differences that rescale the whole frame | 2 |
| `panels` | per-panel `is_up` / `showing` / `is_loaded` / `paused` / `loaded_dir` | A panel that failed to load (`loaded_dir` null) so its screen region is blank; panel stack differing between sides | 4 |
| `panel_dirs` | PanelDir `cam`, `environ`, `focus_component`, `owner_panel`; WorldDir `hud`, `show_hud` | An entire panel rendering with the wrong camera or none; missing environ so a subtree is unlit; HUD overlay absent | 20 |
| `hierarchy` | every object's class, DTA type, owning dir, transform parent | Objects missing entirely on one side; proxy/inline dirs that failed to load; reparenting bugs | 4 |
| `draw_state` | `showing`, `draw_order`, bounding sphere | Invisible/wrongly-visible objects; draw-order and overlay stacking; frustum culling from a zero or stale sphere | 5 |
| `transforms` | world + local translation, euler rotation, scale, constraint, target | Objects drawn in the wrong place/orientation/scale; broken parent-chain propagation; local-vs-world dirty-flag desync | 15 |
| `cameras` | near/far, `y_fov`, `z_range`, `screen_rect`, world placement | Whole-screen framing wrong; geometry clipped by planes; viewport covering the wrong region | 10 |
| `materials` | `blend`, `z_mode`, `cull`, `stencil_mode`, colour/alpha, all texture refs | Wrong blending, z-write/z-test sorting artifacts, culling flips, missing or swapped textures | 15 |
| `textures` | `width`, `height`, `bpp`, `size_kb`, `tex_type`, `file_path` | Textures that never loaded (`size_kb` 0), wrong dimensions, resolved to a different file | 5 |
| `meshes` | `mat`, `geom_owner`, vert/face counts, bone state | Meshes bound to a wrong or null material; geometry decoded to a different count; instancing divergence | 8 |
| `lights` | type, colour, intensity, range, falloff, cone radii, placement | Scene too dark/bright, wrong light colour, point-vs-spot mismatch, lights positioned wrongly | 10 |
| `environ` | ambient, fog, fade, exposure, tone mapping, full colour-grading block | Whole-scene ambient/fog wrong; colour grading / tone mapping divergence | 15 |

`lights` deliberately separates `color` from `intensity`: `RndLight::PackedColor()`
divides the stored colour by `Intensity()` (`Lit.cpp:156`), so a uniformly
too-dark scene shows as an intensity delta with colour unchanged.

### Writing a probe

```lisp
(probe my_probe
    (doc "one line")
    (discriminates "what class of visual bug this catches")
    (scope (dir main) (isa Draw) (guard 0) (limit 500))
    (fields
        (showing    prop (showing)      bool)
        (class      msg  class_name     sym)
        (mat        prop (mat)          obj)))
```

* `prop` fields compile to `{$o get (path) <default>}`. **The default is
  mandatory** — `Hmx::Object::OnGet` (`src/system/obj/Object.cpp:870-899`) uses
  `Property(sym, a->Size() < 4)`, so a 3-node get hard-fails on console.
* `guard 1` (default) additionally wraps reads in `{$o has (path)}` so a class
  lacking the property yields `<absent>` instead of the default — which is
  indistinguishable from a real 0. The guard roughly **doubles** the emitted
  script, so probes whose `isa` already guarantees the property set set `guard 0`.
* `kind` is `int | float | bool | color | sym | obj`.
* Transform sub-paths (`PropSync.cpp:185`): translation is `x`/`y`/`z`; anything
  else falls through to `Hmx::Matrix3` giving `pitch`/`roll`/`yaw` (**degrees**,
  derived) and `x_scale`/`y_scale`/`z_scale`.
* `Hmx::Color` is a leaf packed int; the normalizer unpacks it to r/g/b/a.

For global state that has no roster, use a `scalars` probe whose payload is a
DTA program returning `key=value;` pairs (`screen_state.dta` + its `.prog.dta`
pages).

---

## Safety rules (learned the hard way — all observed live)

**1. Never message a bare `Object`.** `main` holds ~170 DTA *script* objects
whose behaviour lives in `mTypeDef`; `Hmx::Object` forwards unmatched messages
there (`Object.cpp:159`). Sending one even a harmless-looking `is_a`
**executes game script** — probing `campaign_commence_mindcontrol` produced
`CAMPAIGN FLOW ERROR at 'is_a'` followed by SIGSEGV. `Scope.exclude_classes`
defaults to `("Object",)` for exactly this reason.

**2. Failed evals are not free.** The native port catches DTA SIGSEGV via
`sigsetjmp` (`native/src/platform/HttpServer.cpp:328-479`) but **does not unwind
`gCallStackPtr`**. Every failed eval leaks a DTA call-stack entry until
`MILO_ASSERT(gCallStackPtr - gCallStack < HANDLE_STACK_SIZE)`
(`src/system/obj/DataArray.cpp:47`) starts firing, after which the engine dies on
the **main thread** in unrelated code (observed: `CursorPanel::Poll` →
`Hmx::Object::Property` → `DataArray::FindArray`). The runner therefore aborts a
probe after `MAX_EVAL_FAILURES` (25) and says so, rather than producing data from
a destabilized engine. *This is an engine bug worth fixing — see below.*

**3. The `isa` gate must gate the reads**, not merely be reported beside them,
or every non-matching object still gets probed.

**4. Never guess a name.** Input to the DTA parser is trusted: a balanced script
naming a nonexistent object faults the title. Probes are strictly two-pass —
enumerate, then read only what came back. `assert_enumerated()` enforces this and
also rejects names containing characters that could break out of the quoting.

**5. Never attribute a short batch.** If a transport returns fewer results than
commands sent, every later value lands on the wrong object and the report is
confidently wrong. Both the console clients and this runner refuse to attribute
in that case and fail the batch loudly instead.

**6. Never let an empty result pass as a valid one.** A wrong class name, a
panel that never loaded and "there genuinely are no lights here" all produce
*zero objects*, and a snapshot of zero objects diffs as "everything is missing
on the other side" — maximally alarming and completely wrong. Every zero
(enumeration, per-class filter, exact `--classes` filter) is recorded as a
capture error naming what was tried and what was seen, and capture errors are a
`BLOCKER`. Sweep applies the same rule to its own noise floor: with
`--repeat 1` it reports `above_noise: null`, never `false`.

---

## Transport caps

Probes are **paged by construction** across both the field and object axes, so
nothing ever "dumps a tree in one request". Profiles (`--transport`):

| Profile | Max script | Reply | Notes |
|---------|-----------|-------|-------|
| `portable` *(default)* | **16383** | 32768 | Intersection of native and console. Use this. |
| `native_http` | 16384 | 32768 | Native accepts exactly 16384, rejects 16385 (verified live). |
| `post_eval` | 16383 | 32768 | RB3Enhanced `POST /dta/eval` rejects a body of *exactly* 16384. |
| `legacy_get` | ~120 | 1023 | RB3Enhanced `GET /execute?script=`. `request_path[250]`, URL-encoded so `{` costs 3 bytes. |
| `unlimited` | ∞ | ∞ | Native only. Brace validation still runs. |

Both endpoints now cap the script at `RB3E_DTA_SCRIPT_MAX` (16384) and the reply
at `RB3E_DTA_OUTPUT_MAX` (32768) — `native/src/platform/DtaEvalSupport.h:27`
deliberately mirrors the console constants, and both report an explicit 413
naming the limit rather than truncating silently.

**The boundary differs by one byte.** The console rejects a body of *exactly*
16384 (`len(raw) >= limit`, `tools/console/dc3_eval.py:160`); native accepts
16384 and rejects 16385. So the portable rule is `< 16384`, i.e. a max of
**16383**. A page sized to 16384 passes every local test and then fails exactly
once, confusingly, on hardware — which is why the profile exists.

The console caps are enforced **by default even on native**, so a native-only run
proves console viability. `--no-console-caps` opts out.

**`legacy_get` cannot carry any of these probes.** A single field read exceeds
~120 chars, so the budget layer rejects every probe under that profile — at
author time, in one second, instead of during an expensive live session.

> Historical note, so nobody re-derives a stale number: the native endpoint used
> to reject at 8192. That was never an intentional limit — it was cpp-httplib's
> `CPPHTTPLIB_FORM_URL_ENCODED_PAYLOAD_MAX_LENGTH` firing on url-encoded bodies
> and returning a bodyless 413 before the handler ran. Fixed in `26cc0088`.

Brace balance and unterminated strings are validated on **every** script
regardless of profile, because `DataReadString` faults (MILO_FAIL, C++ EH, not
catchable from the console server's C code) rather than returning an error.

---

## Console transport interface

Console targets do **not** need a bespoke subclass. `ConsoleTarget` adapts
anything exposing:

```python
client.eval(script: str) -> str
client.eval_batch(scripts: list[str]) -> list[str]   # exactly one result per input, in order
```

which is the interface shared by `tools/console/dc3_eval.py` in this repo (HTTP,
file and app-child transports) and `tools/rb3e_dta.py` in RB3Enhanced
(`feature/dta-eval-channel`). That pair is the only seam between this tool and
real hardware.

```python
from state_diff.transport import ConsoleTarget, make_target

target = make_target("console:%s" % os.environ["DC3_XBOX"])   # HTTP transport
# ^ use $DC3_XBOX, never a literal IP -- console addresses move and a stale one
#   sends the next reader debugging a dead host. See
#   docs/native/CONSOLE_HW_FINDINGS.md for how to find the live address.
# or wrap any transport object directly:
target = ConsoleTarget(FileTransport(...), name="dc3-xex")
```

Contract for an implementation:

1. Evaluation happens on the title's **main thread** — property reads walk live
   object graphs.
2. A raised error means **capture failed**; it must never enter a snapshot as
   empty state. Truncation is a sentinel sentence in the body; a parse error
   returns **HTTP 200** with body `!! parse error`. Both raise.
3. `eval_batch` returns exactly one result per input **or raises**. The reply is
   captured print output interleaved with `=> <value>` markers, and a refused
   element emits *no* marker — so marker count must be checked against command
   count and attribution refused on mismatch.
4. `describe()` returns stable build identity. Frame counters and uptime belong
   in `volatile`, which the normalizer elides.
5. `roster()` needs no override at all: the inherited implementation is pure
   DTA (`object_list`, or `iterate`/`iterate_self`), correct on the original
   binary and — since `4e4cf851` and `8c73183d` — natively too. It handles dir
   scopes, paging and the class-filter diagnostics for free.

Batching matters: the DC3 file transport accumulates into a variable and writes
once, so a whole probe is one round trip and one keypress. The runner dispatches
pages through `eval_batch` in chunks of 16 to exploit that while keeping the
eval-failure breaker able to intervene.

**Where to develop probes.** The DC3 file route executes through `RndConsole`,
which wraps evaluation in `MILO_TRY`/`MILO_CATCH` (`Console.cpp:434-444`), so a
balanced-but-bogus probe prints `Script error: …` instead of faulting. Do
exploratory probe development on **native or DC3-via-file**, and fire only
validated probes at the RB3 DLL channel, where a bad reference is an uncatchable
throw.

**Result payloads.** Probes return their payload as the command's return value
(a DTA string), never via a print side effect — see below.

**`print` portability.** DC3's debug build has a real `Debug::Print`, but on
**retail RB3** `Debug::Print` compiled to a bare `blr` and was ICF-folded onto an
empty stub, so every `TheDebug << ...` is a no-op there. All probes in this
library carry their payload in the command's **return value**, never in a print
side effect, so they work on both.

---

## Canonical snapshot format

```json
{
  "schema": 2,
  "probe": "draw_state",
  "target": "native",
  "meta":     { "target": "native", "screen": "main_screen" },
  "volatile": { "frame": 5898, "uptime_s": 196.6, "captured_at": …, "stats": {…} },
  "objects":  { "<name>": { "showing": true, "draw_order": 0.0, "_class": "Mesh" } },
  "scalars":  { "ui.current_screen": "main_screen" },
  "errors":   []
}
```

Normalization rules — everything that makes two logically-identical states
compare unequal is handled **here**, so the differ only ranks real differences:

* **Floats** are rounded per tolerance class, not uniformly: `translation` 1e-4,
  `rotation` 1e-2 (euler angles are *derived* from the matrix and fragile near
  gimbal lock), `count`/`color` exact. `-0.0` collapses to `0.0`; NaN/Inf become
  strings so they survive JSON.
* **Colours** unpack from the packed int to `{r,g,b,a}` — a report says
  "green 255 → 0" instead of "int 4278255360 → 4278190080".
* **Enums** decode to symbolic names (`blend=kBlendAdd`).
* **Paths** lowercase, `\`→`/`, drive/`game:` prefixes stripped, so Xbox and
  Linux paths compare equal.
* **Addresses** (`0x…`, long digit runs) are replaced with `<addr>`/`<num>`.
* **Collections** are sorted by `(type, name)`, never left in hash-table order.
* **`<absent>` and `<null>` stay distinct** — "this class has no such property"
  is a different fact from "this reference is null", and the differ ranks them
  differently (`schema` vs `unbound`).
* **Volatile fields** (frame, uptime, timers, beat) are recorded in `volatile`
  and never diffed.

---

## Sweep mode — when every state field is correct and the frame is still wrong

A state diff answers *"do the two sides hold the same values?"*. It cannot
answer *"is this object being drawn where those values say it should be?"* —
and the first real UI bug chased with this tool was exactly that shape: **every
state-visible field was correct on the broken build**, so a static diff reported
nothing. What broke it open was setting one property to a series of values and
watching the pixels. Sweeping `motd.lbl`'s `local_xfm x` and measuring which
screen columns stayed pinned separated *"the geometry is wrong"* from
*"something is occluding it / not advancing"* in a single pass.

```bash
python3 -m state_diff.sweep \
    --dir panel:main_panel --object motd.lbl \
    --prop 'local_xfm x' --values=-400,-200,0,200,400,600 \
    --region 40,578,1160,606 --bg-threshold 190 --repeat 3 \
    -o sweep.json [--save-frames /tmp/frames]
```

```
sweep motd.lbl [HamLabel] (local_xfm x) in panel:main_panel -> {main_panel loaded_dir}
  frame 1280x720  region [40, 578, 1160, 606]  original=96.038147  restored=True  repeat=3

         value      applied |    fg px    x0    x1    y0    y1 |     d px    x0    x1    y0    y1
    (baseline)    96.038147 |     2127    64   722   578   605 |        -     -     -     -     -
          -400         -400 |      810   690   722   578   605 |     1981    53   360   582   602
          -200         -200 |      810   690   722   578   605 |     1981    53   360   582   602
             0            0 |      810   690   722   578   605 |     1981    53   360   582   602
           200          200 |     3072   163   722   578   605 |     4268    53   681   582   602
           400          400 |     2013   451   953   578   605 |     4879    53   970   582   602
           600          600 |      982   690  1159   578   605 |     4564    53  1159   582   602

  delta: pinned=['x0', 'y0', 'y1']  moved=['x1(up,spread=799 >noise)', 'cx(up,spread=417.66 >noise)',
                                           'cy(down,spread=0.16 <=NOISE)', 'px(flat,spread=2898 >noise)']
       same-value noise floor: {'x1': 328, 'cx': 159.9, 'cy': 0.18, 'px': 2257}
```

**The per-edge summary is the finding**, not the images. `delta x0` is pinned at
53 through the whole sweep while `delta x1` tracks the property monotonically
and well clear of noise: the label's left edge is clipped, its right edge is
not. That is a different bug from "drawn in the wrong place", and no state field
distinguishes them.

Two measures per step, both reported in **absolute image coordinates**:

* **`fg`** — bounding box / pixel count / centroid of pixels whose channel
  maximum exceeds `--bg-threshold`.
* **`delta`** — the same, over pixels differing from a baseline frame captured
  at the property's original value. More robust: it needs no notion of
  "background", only "what moved".

If the `fg` bbox equals the whole `--region` at every step, the report says so
explicitly rather than calling all four edges *pinned* — a bright background
above the threshold is not a pinned object.

**`--repeat` is what makes a sweep trustworthy.** It captures each value N times
and reports median + spread, so the sweep measures **its own noise floor in the
same session, on the same object, under the same animation**. Without it an
animating element produces a series that looks exactly like signal — measured
here: `motd.lbl`'s marquee (unfrozen in `23727b3c`) gives a `delta x1` spread of
**328 px** across captures at one unchanged value. Every reported movement is
tagged `>noise` or `<=NOISE` against that. With `--repeat 1` the tool refuses to
imply a verdict: `above_noise` is `null`, never `false`, and the report carries
a warning saying the floor is *unmeasured* — which is not the same as zero.

**The original value is always restored**, in a `finally`: on success, on a
failed step, on a transport drop and on Ctrl-C. The read-back is verified and
reported in `restored`; a mismatch is an error. A sweep that died holding a
swept value would silently poison every capture taken afterwards.

The object is enumerated first, exactly as a probe would — a sweep is the one
place in this tool where a guessed name would *write*, so `assert_enumerated()`
gates it. Property paths must be bare identifiers, checked before they are
embedded in a `(…)` property expression.

---

## Noise floor — measured

A noise floor is only valid for the **scope** it was measured over, so it is
recorded in the profile (`scope_dir`) and re-measured per scope.

**`main` — 0 of 2421 field cells (0.00%)** varied across 5 consecutive captures
of all 12 probes, zero object churn. Unchanged by the panel-scope and
`object_list` work, which is the regression check that matters.

```
cameras        runs=5 objects=5   cells=80   unstable=0 (0.0%) churn=0
draw_state     runs=5 objects=1   cells=7    unstable=0 (0.0%) churn=0
environ        runs=5 objects=1   cells=26   unstable=0 (0.0%) churn=0
hierarchy      runs=5 objects=517 cells=2068 unstable=0 (0.0%) churn=0
lights         runs=5 objects=1   cells=15   unstable=0 (0.0%) churn=0
panel_dirs     runs=5 objects=2   cells=30   unstable=0 (0.0%) churn=0
panels         runs=5 objects=4   cells=24   unstable=0 (0.0%) churn=0
screen_state   runs=5 scalars=10  cells=10   unstable=0 (0.0%) churn=0
transforms     runs=5 objects=7   cells=161  unstable=0 (0.0%) churn=0
NOISE FLOOR: 0/2421 field cells (0.0%) varied across 5 runs
```

**`panel:main_panel` — 0 of 6197 field cells (0.00%)**, zero churn. Two and a
half times the coverage of `main`, on the scope that was previously invisible:

```
cameras        runs=5 objects=2   cells=32   unstable=0 (0.0%) churn=0
draw_state     runs=5 objects=45  cells=315  unstable=0 (0.0%) churn=0
hierarchy      runs=5 objects=620 cells=2480 unstable=0 (0.0%) churn=0
materials      runs=5 objects=49  cells=1029 unstable=0 (0.0%) churn=0
meshes         runs=5 objects=30  cells=300  unstable=0 (0.0%) churn=0
panel_dirs     runs=5 objects=20  cells=300  unstable=0 (0.0%) churn=0
textures       runs=5 objects=60  cells=420  unstable=0 (0.0%) churn=0
transforms     runs=5 objects=57  cells=1311 unstable=0 (0.0%) churn=0
lights / environ / panels: 0 objects (reported as an explicit capture error,
    not a silent empty capture)
NOISE FLOOR: 0/6197 field cells (0.0%) varied across 5 runs
```

**Read this honestly.** Both were taken on `main_screen` with no world or venue
loaded. A clean zero shows the *pipeline* contributes no noise of its own
(normalization, paging, ordering and batching are all deterministic) — so any
nonzero reading later is real engine variance, not a tooling artefact. It does
**not** mean the screen is static: `motd.lbl`'s marquee is scrolling throughout,
and the sweep measures **328 px of same-value spread** on it. Property state
holds still while pixels do not, which is precisely why sweep mode exists.
**Re-measure during gameplay** before trusting transform findings there.

Re-measure whenever the logical capture point or the scope changes:

```bash
python3 -m state_diff.noise --runs 5 --settle 1.0 -o noise_main.json
python3 -m state_diff.noise --runs 5 --settle 1.0 --dir panel:main_panel \
    -o noise_panel.json
```

A field that varies is recorded in the profile and **suppressed** by the differ
(or demoted to `INFO` with `--include-unstable`). A field unstable on ≥50% of
objects is generalized to a `"*"` entry so it also covers objects seen later.

---

## Differ: ranking heuristics

Findings sort by `(severity, -count, field)`.

| Tier | Meaning | Examples |
|------|---------|----------|
| **BLOCKER** | The comparison itself is invalid | different screens, different probes, capture errors |
| **CRITICAL** | Renders wrong or not at all | `showing`, `loaded_dir`, `mat`, `diffuse_tex`, `cam`, `environ`, `hud`, `trans_parent`, missing/extra objects |
| **HIGH** | Whole-surface or whole-screen effect | `blend`, `z_mode`, `cull`, `alpha_write`, `prelit`, camera frustum (`y_fov`, planes, `screen_rect`), `light_type`, `intensity`, `ambient_color`, `fog_enable`, `exposure` |
| **MEDIUM** | Visible but bounded | colours, `alpha`, `range`, grading params, `draw_order`, vert/face counts, texture dimensions, translation |
| **LOW** | Small / derived | rotation-only deltas, misc |
| **INFO** | Flagged unstable by the noise floor | — |

Transition-aware escalations beat the static table:

* An object reference going **to or from `<null>`** is always CRITICAL
  (`unbound`) — a null material renders nothing, which is worse than a swap.
* A field **present on one side and absent on the other** is HIGH (`schema`) —
  the sides disagree about the object's *class*, not just its value.
* `sphere_radius` **to or from 0** is CRITICAL (`culling`) — a collapsed bounding
  sphere disables culling or culls everything.
* `size_kb` **to or from 0** is CRITICAL (`texture_load`) — an asset that never
  got bits uploaded, the classic "everything is untextured" cause.
* Camera frustum divergence ranks HIGH *regardless of magnitude*: a wrong
  `y_fov` makes everything look wrong while every other probe reports clean.
* Rotation ranks below translation because euler angles are derived and fragile.

**Collapsing.** Findings are keyed by `(field, old, new)` and merged across
objects, so report length tracks the number of distinct *causes*, not affected
objects — "47 meshes lost the same material" is one finding.

`--fail-at {blocker,critical,high,…}` makes the differ exit non-zero, for CI.

---

## Proof of work

Run against `dc3-native` headless at `main_screen`, 2026-08.

**All 12 probes captured cleanly, zero errors, engine healthy afterwards:**

```
screen_state:  10 records,   2 requests, max script   653B, max reply  164B
panels:         4 records,   2 requests, max script  1939B, max reply  179B
cameras:        5 records,  17 requests, max script 16378B, max reply  502B
hierarchy:    517 records,  18 requests, max script 16331B, max reply 1371B
transforms:     7 records,  65 requests, max script 15468B, max reply  602B
panel_dirs:     2 records,  72 requests, max script 15391B, max reply  367B
draw_state / lights / environ / textures / materials / meshes: 0-1 records
    (no world or venue loaded on main_screen — expected)
```

**The same probes against `--dir panel:main_panel`, which was previously
unreachable** — same build, same screen, same run:

```
hierarchy:    620 records,  20 requests, max script 16353B, max reply 1190B
textures:      60 records,   3 requests, max script 16239B, max reply 1885B
transforms:    57 records,   8 requests, max script 15386B, max reply 1868B
materials:     49 records,   6 requests, max script 16049B, max reply 1324B
draw_state:    45 records,   2 requests, max script 16057B, max reply 1162B
meshes:        30 records,   2 requests, max script 15533B, max reply 1377B
panel_dirs:    20 records,   3 requests, max script 15251B, max reply 1127B
cameras:        2 records,   1 requests, max script  2744B, max reply  206B
lights / environ: 0 records, reported as an explicit capture error
```

`motd.lbl`, `parent_motd.trans`, `motd_clip_left.trans` and `motd_debloom.mesh`
— the whole cast of the bug the tool was first pointed at, and all four
invisible before — now come back with real values from every applicable probe.
Every page still fits the `portable` caps (max 16353B < 16384), and the two
enumeration back ends produce byte-identical snapshots.

Raising the script cap from 8192 to 16383 collapsed **every** probe to a single
field group and roughly halved the round trips (113 -> 58 for 60 objects;
`hierarchy` 35 -> 18, `transforms` 130 -> 65, `panel_dirs` 167 -> 72). No probe
needed hand-merging: paging is computed from the caps, not hardcoded, so the
field groups that existed purely to fit 8192 dissolved on their own. Round-trip
count is the dominant cost on hardware, so this is the change that matters most
there.

**Identical captures report nothing** (the differ does not surface its own noise):

```
$ python3 -m state_diff.diff cam_a.json cam_b.json --noise-profile noise_native.json
NO DIVERGENCE (within tolerance and noise floor).
```

**A real engine perturbation is surfaced and ranked correctly.** Changing
`[ui.cam]`'s FOV live via `{$o set (y_fov) 60.0}`:

```
1 finding(s):  HIGH=1
  1. [HIGH    ] camera         y_fov: 34.516 -> 60.0
       1 object(s): [ui.cam]
```

**Collapsing and multi-tier ranking** (perturbed snapshot: 47 identical
`owner_dir` changes, 1 class change, 3 deletions, screen mismatch):

```
4 finding(s):  BLOCKER=1  CRITICAL=3
  1. [BLOCKER ] context    targets are on DIFFERENT screens (main_screen vs song_select_screen)
  2. [CRITICAL] unbound    owner_dir: main -> <null>
       47 object(s): [default cam], [default env], … (+41 more)
  3. [CRITICAL] presence   3 object(s) present on native but MISSING on console
  4. [CRITICAL] hierarchy  class: HamScreen -> RndMesh
```

47 identical changes collapsed to **one** finding, ranked below the BLOCKER that
invalidates them.

**Untested:** the console transports themselves. `ConsoleTarget` is exercised by
unit tests (including short-batch refusal) but has never run against real
hardware from this lane.

```bash
python3 tools/state_diff/tests/test_state_diff.py   # 66/66 passed
```

---

## What we cannot reach from DTA

These need an engine-side hook to expose; none is currently available. Several
of them are reachable *through pixels* instead — see the note after the table.

| State | Why |
|-------|-----|
| **Raw transform matrices** | `PropSync(Hmx::Matrix3&)` only exposes derived `pitch`/`roll`/`yaw` + scale. Two numerically different matrices can yield very different euler angles near gimbal lock, so exact matrix comparison is impossible. Would need a `world_xfm_raw` propsync exposing the 12 floats. |
| **`RndLight` visibility** | `mShowing` has a `set_showing` action but **no getter and no propsync** (`Lit.cpp:141-186`), and RndLight is not an `RndDrawable`. A light hidden on one side is invisible to us. |
| **Per-light attachment lists** | `lights_real`/`lights_approx` sizes are readable via `{$env size (…)}`, but indexing an `ObjPtrList` past the end is **unchecked** (`PropSync_p.h:287`) and SIGSEGVs. Probes read only the count. |
| **Texture pixel data / actual GPU format** | `RndTex` exposes `width`/`height`/`bpp`/`size_kb` but not the decoded format, mip chain, or any content hash. "Right size, wrong pixels" is undetectable. Compare `/api/screenshot` instead. |
| **Actual draw order / cull results** | We read the `draw_order` *input*, never the sorted draw list the renderer produced, nor which objects were culled. |
| **Shader / pipeline state** | The WebGPU pipeline the native port selects has no DTA representation at all. |
| **`RndMesh` vertex data** | `get_vert_pos`/`get_vert_norm`/`get_vert_uv` write into out-params via message vars; usable one vertex at a time, far too chatty for a probe. |
| **Bone/skinning matrices** | Not propsynced. |
| **Anything during a UI transition** | `UIManager::Handle` short-circuits and returns 0 while `InTransition() || InComponentSelect()` (`UI.cpp:922-925`), so UI reads are suppressed rather than wrong. The probe captures `ui.in_transition` so the differ can tell. |

### …but pixels are reachable, headless, with no GPU

**`/api/screenshot` works under `MILO_HEADLESS=1` on a machine with no display
and no GPU.** Verified live from this lane: a 1280x720 RGBA PNG (~750 KB) per
request, rendered and returned on a box with no framebuffer at all. The capture
is queued and executed on the main thread after `EndDrawing()`
(`HttpServer.cpp:847`), so it is a fully rendered frame — and a screenshot
request therefore doubles as an "advance to the next presented frame"
primitive, which is how `--settle-frames` is implemented.

Nothing about it requires the windowed/GPU path. Earlier wording here implied
otherwise; that was wrong, and it mattered — the field agent who first used
this tool rendered ~60 verification frames this way and caught that their first
fix, landed on its own, would have made the frame *worse*.

The practical consequence: a **sandboxed agent with no display can close the
loop visually**. `sweep` mode (above) is built on exactly this, so several rows
of the table above — "right size, wrong pixels", actual draw order, whether an
object is occluded — are answerable after all, just not from DTA.

---

## Engine bugs found while building this

All were reported from this lane and have since been **fixed** by the owning
lanes; no `src/` or `native/src` changes were made here. Kept for the
reasoning, and because the first one still shapes the design.

**0. `DataArray::SortNodes` used a hardcoded LP64-wrong element size.**
*(Fixed in `8c73183d`.)* `qsort(..., 8, ...)` — `sizeof(DataNode)` on PPC32 but
**16** on LP64 native — so it strided over half-nodes, read garbage types,
corrupted arrays that did not crash, and only ever covered half the array.
`ObjectList` ends with `SortNodes(0)`, which is why
`{object_list <dir> <Class> TRUE}` SIGSEGVed and three of them killed the
engine. The blast radius is much wider than this tool: **any** in-game DTA that
sorted an array was affected. Fixed with `sizeof(DataNode)`; PPC codegen
verified unmoved by `run_objdiff`, so no `HX_NATIVE` gate.

> **Method correction (2026-08-31).** This used to read *"verified byte-identical
> at the object-file level"*. That check could not have been run: until
> `ee8902a22`, MSVC stamped a clock-derived COFF `TimeDateStamp` and CodeView
> `S_OBJNAME` signature into every object, so 980 of 989 objects differed on every
> rebuild and an object-file comparison would have said "differs" unconditionally.
> The ruler that was actually available, and that the gate waiver should cite, is
> objdiff. **The waiver itself is not in doubt** — `sizeof(DataNode)` is 8 on
> PPC32, which is the literal `8` it replaced, so the codegen provably cannot
> move; the bug was LP64-only, where `sizeof(DataNode)` is 16. State it that way:
> a constant-folding argument beats a hash here, and it is checkable by reading.
`object_list` is now this tool's default enumeration primitive.

**0b. Not a bug: `iterate` "returning zero" against a `PanelDir`.** Reported
from this lane and **withdrawn**. `ObjectDir::Iterate` is correct; the filter
was a C++ class name (`RndDrawable`), which resolves through the DTA `objects`
superclass graph to nothing. See "Class filters take DTA class names" above —
and note that this failure mode is silent, which is why the tool now ships a
vocabulary, validates against it, and reports every zero-result filter.

**1. `ObjectDir::Iterate` was mis-decompiled — `iterate` enumerated nothing.**
*(Fixed in `4e4cf851`, 96.6% -> 99.4%.)*
`src/system/obj/Dir.cpp:974` reads:

```cpp
if (bbb && (s2.Null() || it->Type() == s2)) {
```

`s2` is the **class** filter; the optional **type** filter is `s8`. RB3's
equivalent (`rb3/src/system/obj/Dir.cpp:790-799`) gates on `sym2` (the type) and
only when it is non-null. Because `s2` is never null when a class name is passed,
and `Type()` is the object's typedef symbol rather than its class,
**`{$dir iterate <Class> $var …}` and `iterate_self` execute their body zero
times in the native port.** Suggested fix: `bbb && (s8.Null() || it->Type() == s8)`.

Now that it is fixed, the inherited DTA `iterate` roster works on **both**
targets — verified live, `{main iterate Cam …}` returns all 5 cameras and
`{{main_panel loaded_dir} iterate Trans …}` all 57 panel transformables. That
makes enumeration genuinely portable, and it is a *safe* path: `iterate` filters
by class inside the engine, so it never messages the script objects that rule 1
above warns about. It is retained as `--enumerate iterate` and as the only
non-recursive back end; `object_list` is the default because it is sorted and
has a real cursor. `NativeHttpTarget` still overrides `roster()` with
`/api/objects` for `main` only, because that is one request instead of one per
class — it cannot serve a panel dir at all.

The wider impact is bigger than this tool: any in-game `.dta` relying on
`iterate` was silently a no-op natively.

**2. DTA eval SIGSEGV recovery leaked `gCallStackPtr`.** *(Fixed in `26cc0088`
with a scope guard declared before the `sigsetjmp`.)*
`native/src/platform/HttpServer.cpp:328-479` restores the signal handlers around
a recovered eval but not the DTA handle call stack, so each failed eval
permanently consumes an entry. After enough failures
`MILO_ASSERT(gCallStackPtr - gCallStack < HANDLE_STACK_SIZE)`
(`src/system/obj/DataArray.cpp:47`) fires continuously and the engine then dies on
the main thread in unrelated code. Two pieces of state remain unrepairable without `HX_NATIVE`-gated `src/`
accessors (`Debug::mTry` and a file-static conditional-nesting variable), so a
crash inside `MILO_TRY` still leaves try-depth incremented. The
`MAX_EVAL_FAILURES` breaker therefore stays, but the delayed death in unrelated
code should no longer occur.

**3. `/api/dta/eval` could not return strings.** *(Fixed in `26cc0088`.)*
The old serializer handled only int / float / symbol / object; `kDataString`
(what `sprint`/`sprintf` return) fell to `default:` and yielded
`{"type":18,"value":null}`, so every probe wrapped its payload in `{symbol …}`.
All 21 DataTypes now serialize with names and numeric type ids, arrays and
commands recurse (8 deep / 256 elements with an explicit `truncated` flag), and
non-UTF-8 bytes come back base64 with an `encoding` field. **The `{symbol …}`
wrapper has been dropped** — it was also interning every page's payload into the
global symbol table forever (the `hierarchy` probe alone leaked ~500 unique
symbols per run).

**4. Non-finite floats emitted bare `nan`/`inf`.** *(Fixed in `26cc0088`.)*
That is invalid JSON and would have crashed the capture parser outright the first
time a NaN transform appeared — a likely occurrence given the tool exists to find
exactly that class of bug. They now emit `null` plus a `"special"` field, which
`transport.decode_node()` maps back to `NaN`/`Inf`/`-Inf` so the value survives
into the snapshot instead of silently becoming null.

**5. Native body cap was 8192 bytes**, below the console's 16 KB. *(Fixed in
`26cc0088`.)* Not an intentional limit — cpp-httplib's url-encoded payload
default. Both ends now agree at 16384; see the transport table for the
one-byte boundary difference that remains.
