# Stream 2 — Stub-Port Harvest (og-dc3 → implement 0% stubs to 100%)

**Read `docs/plans/port-harvest/WORKFLOW.md` first** — full worktree + subagent + merge mechanics.

## Goal
Implement our **unimplemented stub functions (currently 0%)** by porting og-dc3's full,
already-matched implementation. These recover **full function size** when matched, so this is
the real **fuzzy%** lever (vs Stream 1's near-done partials). Harder per function: you bring the
whole impl plus its dependencies (helpers, headers, types), and many are Xbox/SDK-gated.

## Scope (as of 2026-05-31, regenerate to refresh)
**358 functions / 63K bytes across 48 units** where og-dc3 is at 100% and we have nothing.
Worklist: `docs/plans/port-harvest/stream2-stub-ports.json`.

Top units by bytes (the fuzzy payoff):
BinkMovieImpl(14k), PlatformMgr_Xbox(13k), VorbisReader(5k), link_glue(5k), Mic(3k),
filterdesign(2k), NetworkSocket_Win(2k), FxSend(1k), json_object/json-c(1k), FxSendWah(1k),
FxSendEQ/Delay/Chorus/MeterEffect(<1k each), …

## TRIAGE FIRST — split clean vs SDK-gated
Not all stubs are equal. Before porting, classify each unit:
- **CLEAN (do these):** self-contained logic with deps we already have. e.g. `system/net/json-c/*`
  (json_object/json_escape_str — pure C), `system/synth/filterdesign` (DSP math), parts of
  `system/synth/VorbisReader`. og-dc3 has these fully; their deps are in-tree.
- **SDK-GATED (defer / harder):** wrap Xbox XDK APIs that may need XDK headers/stubs:
  `system/moviebink/BinkMovieImpl` (Bink SDK), `system/synth_xbox/*` (FxSend*/Mic/Voice = XAudio2),
  `system/os/PlatformMgr_Xbox` + `NetworkSocket_Win` (Xbox OS/net). og-dc3 may have ported the SDK
  shims too — check `../og-dc3-decomp/src/xdk/` and its synth_xbox headers. If og-dc3 has the SDK
  headers and they compile for us, these become portable; if not, defer.
- **link_glue:** ICF/duplicate glue — usually not individually authorable; skip unless trivial.

Recommended order: json-c → filterdesign → VorbisReader → (assess XDK availability) → synth_xbox
FxSend* family (they're small + repetitive once one works) → BinkMovieImpl / PlatformMgr (biggest
bytes, hardest, last).

## Method
Standard port recipe (WORKFLOW.md), but expect to ALSO port:
- helper functions og-dc3's stub calls (check they exist in our tree; port if missing),
- any header/type/struct og-dc3 introduced (check blast-radius; prefer additive),
- for SDK units: the XDK header shim from `../og-dc3-decomp/src/xdk/` (verify it compiles under
  our build + doesn't break the native HX_NATIVE path — guard with `#ifndef HX_NATIVE` if needed).

Because a stub starts at 0%, the agent's job is "make this function exist and match" — give it the
og-dc3 source for the function AND tell it to resolve missing deps within the worktree. Validate
with run_objdiff; a stub that reaches 100% is a clean win, a partial (e.g. 60%) is still progress
but assess whether it's worth merging (it shifts the stub off 0%).

## Watch-outs specific to stubs
- Implementing a stub adds a real symbol where there was none → can change the unit's `??__E`/`??__F`
  init/atexit boilerplate and shift sibling layout (the codec.h/Stream-history lesson). Batch-sweep.
- A header/type change to support a stub (e.g. MeshVertCompress.h int→float) can ripple to other
  users — check blast-radius, rely on the build+sync gate.
- Don't merge a half-working SDK stub that pulls in XDK headers if it risks the native build —
  HX_NATIVE-guard the Xbox path.

## Done = success criteria
Per unit: stubs that reached 100% (run_objdiff-confirmed) merged; build+sync gate shows the
fuzzy% jump (this stream should move fuzzy% more visibly than Stream 1) and 0 complete-functions
broken. Track which SDK units were deferred and why.
