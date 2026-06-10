# 12 — Native Stub Intersection Census

## Question

`is_stub=1` is set on 2,686 decomp.db rows. The native port + web build run real gameplay,
so the audit asks: which of those stubs are in code the native build actually executes, which
are load-bearing at runtime, do any guess MILO_ASSERT/OBJ_MEM_OVERLOAD macros, do they explain
the IK feet bug, and how should we burn them down + surface them at runtime?

**Headline correction**: the 2,686 `is_stub` rows are a *decomp-target* (Xbox) signal, almost
entirely disjoint from the *native runtime* stub surface. The native runtime stub surface lives
in `native/src/engine_stubs_generated.cpp` (171 silent return-0/null stubs) and in
`#ifdef HX_NATIVE` substitution blocks inside src — NOT in the `is_stub` column.

## Method (commands run)

- `sqlite3 'file:decomp.db?mode=ro'` — census of `is_stub`, by unit, by symbol kind, by percent,
  IK units, unicorn divergence; `call_edges`/`xrefs` coverage.
- Extracted the native-compiled source list from `native/CMakeLists.txt` via
  `grep -oE '\$\{CMAKE_SOURCE_DIR\}/\.\./src/[...]\.cpp'` (757 units) and intersected with stub units.
- Verified xbox-platform exclusions: `grep -nE 'binkxenon|synth_xbox|rnddx9|...' native/CMakeLists.txt`
  → only the exclusion COMMENT at line 288, no compiled sources.
- Read `native/src/engine_stubs_generated.cpp`, `src/App.cpp` (smart stubs),
  `src/system/gesture/DrawUtl.cpp`, `src/system/synth/filterdesign.cpp` for HX_NATIVE guarding.
- `mcp__orchestrator__run_diff_inspect` diagnose on `CharIKFoot::DoFSM`.
- Read `docs/native/TODO.md`, `STUB_BURNDOWN.md`, `DECOMP_GAPS.md`.

## Findings

### F1 — `is_stub` conflates compiler boilerplate with hand-authored stubs

Breakdown of the 2,686 by symbol kind (sub-100% count in parens):

| kind | count | sub-100% |
|---|---|---|
| `fn_` EH funclet / unnamed | 1,384 | 26 |
| named C++ function (`?...`) | 656 | 525 |
| atexit dtor `??__F` (compiler) | 335 | 67 |
| C/extern function | 180 | 177 |
| vector deleting dtor `??_E` (compiler) | 101 | 101 |
| dynamic init `??__E` (compiler) | 30 | 28 |

1,728 of 2,686 are already at `current_percent=100` (they match — mostly EH funclets and
boilerplate). After removing compiler boilerplate (`??_E/??__F/??__E/??_9/??_7`), only **677
hand-authored stubs sit at 0%**, summing 207,552 bytes. The scout "2,686 stubs" overcounts the
real gap by ~4x.

### F2 — Of 677 real 0% stubs, only 42 are in native-compiled units; 635 are in units native replaces wholesale

`native/CMakeLists.txt:288` documents the exclusion set: `xdk/, lib/binkxenon, synth_xbox/,
rnddx9/, *_Xbox.cpp, *_Win.cpp`. Verified these substrings appear ONLY in that comment, never as
compiled sources. Intersecting the 677 real stubs against the 757 actually-compiled units:

- **real 0% stubs in native-compiled units: 42**
- **real 0% stubs in native-EXCLUDED units: 635** (binkxenon 62, PlatformMgr_Xbox 55,
  synth_xbox/Synth 40, ExternalMic 38, Mic 36, rrthreads 21, rnddx9/Mesh 20, json-c 14, …)

The 635 are Xbox audio HW (XAudio2), RAD Bink, D3D9, Xbox networking — the native port supplies
its own implementations via `../milo-native-engine` + FFmpeg + miniaudio + WebGPU, so these stubs
never execute natively. They are *decomp accuracy* targets, not native blockers (STUB_BURNDOWN.md
TIER 3 says exactly this for SpotlightDrawer_NG / Shader: "native WebGPU bypasses these entirely").

### F3 — The 42 "native-compiled real stubs" are mostly STL template instantiations + HX_NATIVE-guarded Xbox helpers, not gameplay logic

The 42 break down as: STL algorithm instantiations (`__lower_bound`, `random_shuffle`,
`sort_heap`, `__insertion_sort`, `_M_find`, `__uninitialized_fill_n`, `MakeString`) — these
*do* have real behavior from headers compiled in other TUs (STUB_BURNDOWN Phase 1 removed 34 such
dead stubs); gesture/Kinect depth helpers (`CopyDepth`, `CopyPlayerMask`, `YUVtoRGB`,
`NuiTransformSkeletonToDepthImage`, `DepthBuffer3D::DrawMesh`); and `?A0x...` anon-namespace
audio filter functions. Spot-check of guarding:

- `src/system/gesture/DrawUtl.cpp:47` wraps the stubbed helpers in `#ifndef HX_NATIVE` — the
  Xbox stub bodies are **not compiled on native**; native has a separate path.
- `src/system/synth/filterdesign.cpp:89` likewise `#ifndef HX_NATIVE`.
- `src/system/gesture/DepthBuffer3D.cpp` and `hamobj/MoveDir.cpp` use `#ifdef HX_NATIVE` blocks.

So even the 42 are largely not live native logic. **The DB `is_stub` flag is the wrong lens for
native runtime risk** — it reflects the matched-fork (Xbox) source the objdiff target wants.

### F4 — The REAL native runtime stub surface is `engine_stubs_generated.cpp`: 171 silent return-0/null stubs, only 1 warn/abort

`native/src/engine_stubs_generated.cpp` (503 lines) provides weak symbols that silently return
`0`/`nullptr`/`{}`. Categorized:

- **Kinect/NUI: 44 `Nui*` stubs** (`NuiTransformSkeletonToDepthImage`, etc.) — Kinect skeleton
  pipeline; native uses pose-server / ncnn instead.
- **json-c: 10 `json_object_*` stubs all `return 0`** — see F5, a real silent gap.
- **Bink: 7** (`BinkOpen`, `BinkGoto`…) — native uses FFmpeg.
- **Xbox SDK: Dm*, XNet*, XInput*, XContent*, XShowMarketplace…** — irrelevant on native.
- **Null singletons: `TheLeaderboards`, `TheMaster`, `TheSkeletonViz`, `TheFitnessGoalMgr`,
  `TheChallengeSortMgr`, `TheHAQMgr` = `void* 0`** — any deref that slips past a null guard
  faults; otherwise these features are silently absent.

Only **1** of these stubs references any warn/abort. `grep -c` for silent defaults = 171. There
is **no HX_STUB_TRACE / telemetry counter** anywhere in src/include/native (grep returned empty),
so an executed native stub is completely invisible — it neither logs nor counts.

### F5 — json-c is NOT compiled into native, so all RockCentral/leaderboard JSON silently parses to 0

A full json-c implementation exists at `src/system/net/json-c/json_object.c`, but
`grep -oE 'net/json-c/[...]\.c' native/CMakeLists.txt` returns **empty** — native compiles no
json-c `.c`. Therefore `json_object_get_string()`, `json_object_get_int()`,
`json_object_array_length()`, etc. all resolve to the weak `return 0` stubs in
engine_stubs_generated.cpp. Net effect: RockCentral / Leaderboards / MOTD / store-offer JSON
responses parse as empty at native runtime. This matches `docs/native/TODO.md:12,266`:
"RockCentral::ManageJob unstub (crashes on SendDropInDatapoint)". This is a genuine *silent
feature gap*, and is invisible precisely because the stubs don't trace.

### F6 — The IK feet bug is NOT a stub. CharIKFoot is fully implemented; the bug is (a) DoFSM logic divergence and (b) runtime empty-constraints wiring

- `CharIKFoot` has **0 stubs**, 25 functions, avg 95.9%. `CharIKFoot::Poll` is implemented and
  matching. So the memory/TODO phrasing "CharIKFoot::Poll never fires" is a *runtime wiring*
  problem (the function exists; it isn't being invoked / has nothing to anchor), not a missing
  decomp.
- `CharIKFoot::DoFSM` (the foot finite-state-machine) is DIVERGENT at 97.4%. `run_diff_inspect
  diagnose` shows ~75.6% raw equal, dominated by r29↔r30 regswap (22 instrs) **plus 4 real
  `replace` lines** at offsets 0x30/0x34: TGT uses `lwz`/`stw` (integer load/store) where SRC uses
  `lfs`/`stfs` (float). That is a **field-type mismatch in DoFSM** — likely a `Vector`/`Transform`
  field declared/handled as int vs float, which would corrupt ankle/toe placement math. This is
  the strongest single decomp-bug suspect for the feet bug and is exactly the kind of value
  divergence `docs/native/TODO.md:8-9` + the IK-memory note describe (ankle 4.39→1.0, toe below
  floor, "~1e16 leg/foot bone translations").
- `HamIKEffector::Poll` (orig_error 99.9%), `ComputeElbowPullAndQuat` (call_count 94.3%) are also
  divergent but elbow-related. `docs/native/TODO.md:8` notes `HamIKEffector::mConstraints` is empty
  — again a runtime-population issue, not a stub.

**Roadmap implication**: chasing IK as a "stub" is a dead end. The two live levers are
(1) close `CharIKFoot::DoFSM`'s int-vs-float field divergence (it's a real, diagnosable logic bug),
and (2) trace why `HamIKEffector::mConstraints` is never populated at native runtime.

### F7 — MILO_ASSERT / OBJ_MEM_OVERLOAD danger spots

No `is_stub` row in a native-compiled gameplay unit was found to *guess* a MILO_ASSERT or
OBJ_MEM_OVERLOAD argument: the real-stub set is STL templates + HX_NATIVE-guarded Xbox helpers
(F3), which carry neither macro. The danger lives instead in the *native substitution* blocks:
`src/App.cpp:67-198` defines `NativeSaveLoadStub`/`NativeProfileMgrStub`/`NativePlatformMgrStub`/
`NativeSpeechMgrStub` (`#ifdef HX_NATIVE`) — bare `Hmx::Object` subclasses returning sensible
defaults. These are deliberate, documented (STUB_BURNDOWN "smart stubs"), and low-risk, but they
are *behavioral* stubs invisible to the `is_stub` census. No evidence of guessed assert/overload
macros in them. (Caveat: this is a targeted check of the 42-unit set + App.cpp, not an exhaustive
src-wide audit.)

### F8 — fan_in / call_edges is too sparse to rank native load-bearing-ness

`call_edges` has only 40,185 edges, 12,635 distinct callees; `fan_in>0` on just 12,635 of 52,504
functions; `max(call_count)=1` (no weighting). Top stub fan_in maxes at 12
(`RndRenderState::SetTextureFilter`, an excluded rnddx9 unit). `xrefs` (20,756 rows) is richer but
still partial. **fan_in is not a reliable native-traffic signal**; the authoritative ranking of
native runtime stub impact must come from a runtime stub-hit counter (F4 tooling gap), not the DB.

## Ranked native-impact stub list (top, by reasoned native traffic — fan_in is unreliable, F8)

Ranking native-RUNTIME stubs (engine_stubs_generated.cpp + wiring), since DB `is_stub` rows are
mostly not executed natively (F2/F3):

| # | symbol / target | source | why it matters natively |
|---|---|---|---|
| 1 | `json_object_get_*` (10 fns) | engine_stubs_generated.cpp:56-65 | All net JSON → 0; RockCentral/leaderboards/MOTD/store silently empty (F5) |
| 2 | `CharIKFoot::DoFSM` (divergent, not stub) | src/system/char/CharIKFoot.cpp | int-vs-float field bug at 0x30/0x34 → feet-in-floor (F6) |
| 3 | `HamIKEffector::mConstraints` empty (wiring) | hamobj/HamIKEffector | IK solver unanchored → toe below floor (F6, TODO.md:8) |
| 4 | `TheLeaderboards` / `TheMaster` = null | engine_stubs_generated.cpp:164,166 | online features absent; deref past a null-guard faults |
| 5 | `RockCentral::ManageJob` (HX_NATIVE delete-and-return) | net_ham/RockCentral | crashes on SendDropInDatapoint per TODO.md:266 |
| 6 | `TheMoveMgr` not initialized | lazer/game/Game.cpp:556/677 | Game::IsLoaded state 1 needs move merger; skipped via null guard (TODO.md:184) |
| 7 | `TheSkeletonViz` = null | engine_stubs_generated.cpp:177 | skeleton debug viz absent (was a Session-63 blocker) |
| 8 | 44 `Nui*` Kinect stubs | engine_stubs_generated.cpp | Kinect skeleton path; native uses pose-server, so OK if input routed there |
| 9 | gesture `CopyDepth`/`CopyPlayerMask`/`YUVtoRGB` | gesture/DrawUtl.cpp:47 (#ifndef HX_NATIVE) | NOT compiled native; native has own path — low risk |
| 10 | `TheFitnessGoalMgr`/`TheChallengeSortMgr`/`TheHAQMgr` = null | engine_stubs_generated.cpp | fitness/challenge/HAQ meta features absent |

(Items 9 onward are largely inert on native; included to show where the DB-stub census points vs
where the real risk is.)

## Implications for the roadmap

1. **Re-scope "stub burndown" to two planes.** Decomp `is_stub` (677 real, 635 in xbox units) is a
   *matching* backlog; native runtime stubs (171 in engine_stubs_generated.cpp + wiring) are a
   *quality* backlog. They barely overlap (42 units, mostly inert). Track them separately.
2. **The native-quality wins are in F5 (json-c) and F6 (IK), not in DB stubs.** Compiling
   `src/system/net/json-c/*.c` into native (or providing a real JSON impl) unblocks online meta
   silently-empty data. Fixing `CharIKFoot::DoFSM`'s int/float field is a concrete, diagnosable
   feet-bug lever.
3. **Stubs are silent.** 171 native stubs, 1 warn. Any of them can be hit in gameplay with zero
   signal. A runtime stub-hit tracer would convert "unknown unknowns" into a ranked worklist and
   directly validate the F5/F6 hypotheses.

## Tooling gaps found

- **No stub-hit telemetry.** No `HX_STUB_TRACE`/counter exists (grep empty). Need a macro that
  logs first-hit + increments a per-symbol counter, dumpable via the HTTP debug server
  (`/api/stubs`). This is the single highest-leverage tooling item: it turns the 171 silent
  native stubs into an evidence-ranked burndown list and proves which actually execute.
- **`is_stub` is a misleading native signal.** It tracks the Xbox decomp target, not native
  compilation. An audit view should join `is_stub` against the native-compiled unit set AND strip
  compiler boilerplate; today nothing does, so "2,686 stubs" reads as a native crash surface when
  the real native surface is ~171 silent functions + a handful of null singletons.
- **fan_in / call_edges is too sparse** (12,635/52,504 populated, unweighted) to rank native
  traffic. Either enrich from a runtime trace or stop using fan_in for native-impact ranking.
- **No CI check that json-c (and similar real impls) are linked, not stubbed.** A weak `return 0`
  silently shadowing a real implementation that simply wasn't added to the source list is exactly
  the F5 failure mode; a link-time "this weak stub is the final definition of a symbol that has a
  real .c in tree" warning would catch it.
