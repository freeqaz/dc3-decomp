# 09 — Sizing and Validating the og-dc3-decomp Port Lane

## Question

`/home/free/code/milohax/og-dc3-decomp` is an older parallel DC3 decomp (same XEX target). Verbatim ports
from it have repeatedly recovered matches; a prior claim says "~190 functions at 100% in og but open in ours."
(1) Is og's 100% comparable to ours (same objdiff config / same target objects)? (2) Recompute the lane:
functions at 100% in og where our `current_percent<100` — count, bytes, units; verify/correct the ~190 claim.
(3) Compute the whole-unit lane. (4) Sample 3 ports for difficulty + native-guard risk. (5) Recommend a
continuous og-coverage cross-reference.

## Method (commands run)

- `md5sum orig/373307D9/default.xex` in both repos → identical target binary.
- Compared `objdiff.json` config blocks (excluding `units`) and the ninja `report` rule / `objdiff_report_args` in both.
- `objdiff-cli --version` for both (our live fork vs og's pinned binary).
- `md5sum` of a shared target `.obj` (`system/obj/DirLoader.obj`) → og objects differ.
- Extracted all og functions with `fuzzy_match_percent` from `og/build/373307D9/report.json` → `/tmp/og_fns.tsv` (27,718 measured; 26,230 at 100%).
- Joined og-100% names against our DB `functions.symbol` (`sqlite3 file:decomp.db?mode=ro`) in `/tmp/join_og.py`, `/tmp/lane_detail.py`, `/tmp/nondb.py`, `/tmp/nullcohort.py`, `/tmp/static_check.py`, `/tmp/final_lane.py`, `/tmp/reconcile.py`.
- Whole-unit lane: `/tmp/whole_unit.py` (per-unit n100/frac from both report.jsons).
- `diff` of `StorePanel.cpp`, `CharEyes.cpp`, `json_object.c`; `wc -l` of candidate sources; grep for `PlatformMgr::Init` and `HX_NATIVE`.
- `grep PlatformMgr native/CMakeLists.txt` → native build substitutes `PlatformMgr_Native.cpp`.
- Fresh `mcp__orchestrator__run_objdiff` on `json_escape_str` and `PlatformMgr::Init` (ground-truth currency).

## Findings

### 1. og's 100% IS measured the same way — with one decisive caveat (target-object partitioning)

- **Same XEX**: both `orig/373307D9/default.xex` = `a658576e7c60f2ad107a5a90f26ca546`. Same build ID `373307D9`. og's `orig/` is a symlink, but it points at its own copy; the hash matches ours.
- **Same objdiff config**: the `objdiff.json` config blocks are byte-equivalent (`min_version 2.0.0-beta.5`, `custom_make ninja`, identical `progress_categories`). Both report rules use `objdiff_report_args = ` (empty → default diff config, no custom flags). So percentages are produced by the same scoring function.
- **Different objdiff-cli version**: ours is the live fork **`objdiff-cli 4.2.3`** (`../objdiff/target/release/objdiff-cli`); og uses a pinned **`3.7.1`** binary (`build/tools/objdiff-cli`). v4.2.0 added funclet pairing (MEMORY: +1,264 dc3 matched). Our newer version can match symbols og's cannot, so an og→ours port usually scores *at least* as well as og — but the two reports are not bit-identical pipelines.
- **DECISIVE CAVEAT — target objects differ**: the shared unit `system/obj/DirLoader.obj` is **122,240 B (ours) vs 86,805 B (og)**, different md5. The two projects' XEX splitters (jeff configs) partition functions into `.obj` files differently. Consequence: **external (mangled class-method) symbols are directly comparable; `static`/internal-linkage free functions are NOT** — they live in different object partitions, so og's 100% for a static symbol does not transfer. Proven below (json_escape_str).

### 2. The lane recomputed — the "~190" claim is right in magnitude but for the wrong reason

og has **26,230 functions at fuzzy ≥100%** (`/tmp/og_fns.tsv`). Joined against our 52,504 DB rows by `symbol`
(`/tmp/join_og.py`):

| cohort (og=100, ours not-100) | count | bytes | nature |
|---|---|---|---|
| present in our DB, `current_percent<100` (measured) | **384** | 155,336 | the comparable lane |
| present, `current_percent IS NULL` (never measured) | 158 | 14,328 | **100% template-shaped** (`??_G`/`_M_`/`merged_`) — noise |
| **not in our DB at all** | 99 | 8,916 | **100% template/dtor/ICF artifacts** (`??_G ObjRefConcrete<T>`, `_M_fill_insert_aux`, `merged_`) — noise |

The raw "og100 ∧ ours<100-or-missing" total is **641**, but the 158 NULL and 99 not-in-DB cohorts are
**entirely compiler-emitted template/destructor/ICF instantiations** (verified: `/tmp/nullcohort.py` → 158/158
template-shaped, 0 real candidates; `/tmp/nondb.py` → 99/99 absent from our report.json too, all `??_G`/`_M_`/`merged_`).
So the **comparable lane is 384 functions / 155,336 bytes**, not 641.

Within the 384 (`/tmp/lane_detail.py`):
- **159 in 95–100%** (119,808 B) — near-miss; **but see §4: our source is usually already AHEAD here, gap is a floor**.
- **179 "measured 0%"** (18,176 B) — of which **178 are `is_stub=1`** (Xbox/platform stubs). This is the real recovery.
- 29 in 85–95, 11 in 70–85, 5 in 40–70, 1 in 0–40.

**The actionable lane (genuine stubs where og has source) = `is_stub=1 ∧ og=100`** (`/tmp/final_lane.py`,
`/tmp/reconcile.py`): **359 functions / 41,984 B**, but **173 of those already read ≥100% in our DB** (is_stub flag is
stale post-fix) and 178 are at 0% + 8 partial. **Net-new = ~186 functions / ~22 KB.** *This* is the kernel of truth in
the "~190" claim — it is the **stub subset at 0%**, not "190 functions at 100% in og open in ours."

og covers only **359 of our 2,686 total DB stubs (13%)** — og is a partial, not a complete, stub-backlog solution.

### 3. Whole-unit lane is SMALL — our project has surpassed og

`/tmp/whole_unit.py` (og ≥90% of fns at 100, ours <50% done): only **6 units / 25 functions**, all DSP/synth:
`dsp/mkfilter/complex`(7), `synth_xbox/EnvelopeGenerator`(4), `dsp/DelayEffect`(4), `oggvorbis/VorbisMem`(4),
`dsp/CompressionEffect`(3), `dsp/Common_Xbox`(3) — all "NOT_IN_OUR_REPORT" (we lack the unit object). This matches
MEMORY's stream-2 harvest ("landed filterdesign/complex … dead small-units"). The whole-unit lane is NOT bigger than
the function lane.

**Why it's small**: our `src/` is **larger than og's** — 625,389 lines / 2,595 files vs og's 562,597 / 2,533
(`/tmp/volume.py`). We have generally out-developed og. The port value is in the *specific stub holes*, not whole units.

### 4. Three sampled ports — difficulty + native-guard procedure

**(A) `PlatformMgr::Init` — REAL port, native-SAFE.** `og/.../PlatformMgr_Xbox.cpp:606` has the full 12-line body
(`XOnlineStartup`, `XNotifyCreateListener`, `UpdateSigninState`, `SmartGlassInit`); **our project has no definition**
(fresh objdiff: "88 instructions, all insert, Stub (High)"). The XDK calls compile only against the XEX target — but
`native/CMakeLists.txt:771,1068,1231` shows the native build **excludes `PlatformMgr_Xbox.cpp` and substitutes
`native/src/platform/PlatformMgr_Native.cpp`**. So Xbox-only files (`*_Xbox`, `*360`, `*_Win`, `xdk/`, `rnddx9/`,
`synth_xbox/`, `tomcrypt`, `curl`) **never reach the native target → porting their stubs is native-safe without guards.**

**(B) `StorePanel::Handle` — DO NOT PORT (regression).** `diff` shows **our `StorePanel.cpp` = 807 lines vs og's 334**;
og still uses placeholder field names (`unk50`, `unk38`, `unk60`) where ours has proper names (`mNeedsCacheLoad`,
`mOffers`, `mAlbumTex`), an extra method (`MultipleItemsEnumCompleteMsg::OfferID`), and **2 `HX_NATIVE` guards** (og: 0).
og is the *less* complete source. Our DB has Handle at 99.5%; the residual is a floor, not an og-portable gap.

**(C) `json_escape_str` (json_object.c) — NON-COMPARABLE static, ignore.** The two `json_object.c` files are
**byte-identical** (`diff` exit 0, 545 lines each), yet fresh objdiff scores our `?json_escape_str@@YAHPAUprintbuf@@PAD@Z`
as "106 insert, Stub (High)" while og reports 100%. It is a **`static` free function**; our jeff partitions it into a
different object than og's, so og's match does not transfer. The DB `is_stub` flag here is a **partitioning artifact**, not
a missing body. (67 of 384 / 12,400 B in the lane are `?X@@YA` free functions with this static-risk; `/tmp/static_check.py`.)

**Port procedure (to preserve native safety — MEMORY: verbatim og ports drop guards, broke web song-load once):**
1. Classify the function's *our* unit. If the file is Xbox-only (excluded from `native/CMakeLists.txt`), port verbatim — native-safe.
2. If the file is cross-platform (compiled by both builds — `char/`, `rndobj/`, `meta/`, `ui/`, `lazer/`), **first `diff` our source vs og's**. If ours is larger/newer (the common case), **do not port** — the gap is a floor. If og genuinely has the body and we don't, graft it **under `#ifndef HX_NATIVE`** and provide/keep the native path under `#ifdef HX_NATIVE`.
3. Only ports of **external class methods** are object-comparable; skip `?X@@YA` free/static functions unless a fresh `run_objdiff` post-port confirms ≥ baseline.
4. Always re-`run_objdiff` after porting; never trust the og report's 100% as a guarantee of our score.

### 5. Measurement-correctness side-findings (relevant to the whole audit)

- **`current_percent=0` is overloaded**: of the 19,626 "0%" rows, only **1,537 are real-typed 0.0; 18,089 are NULL** (never measured) — `SELECT typeof(current_percent)` → `null|18089, real|34415`. Band censuses that lump NULL with 0 overstate "measured-zero."
- **`is_stub` is stale in both directions**: 173/359 og-stub-lane rows already read ≥100% (flag never cleared); and json_escape_str is flagged stub but the source is byte-identical to a matching one. Trust fresh `run_objdiff` over the DB flag.

## Implications for the roadmap

1. **The honest og-port lane is ~186 net-new stub functions (~22 KB), not 190 matches at large bytes.** It is real and aligns with the prior stub-harvest backlog (PlatformMgr_Xbox 59, BinkMovieImpl 49, NetworkSocket_Win 19, VorbisReader 19, Mic 15, json-c 13, FxSend* ~60). About half is native-safe (Xbox-only files), half cross-platform (needs HX_NATIVE hybrid).
2. **The 95–100 near-miss cohort (159 fns) is mostly NOT an og lane** — our source has surpassed og there; those are floors/permuter territory, not port territory. Don't spend port effort on them.
3. **og will not clear the stub backlog**: only 13% of our 2,686 stubs have an og 100% source. Bink/XAPO/PlatformMgr were already flagged blocked in MEMORY for native-API reasons; the og *decomp* source exists, so the block is native-port wiring, not source availability.
4. **The whole-unit lane is 6 small DSP units** — quick, low-risk, native-safe wins.

## Tooling gaps found

- **No continuous og cross-reference.** The lane is recomputed ad hoc. Build a script `scripts/og_coverage.py` (or a `decomp.db` view) that joins our `functions.symbol` against og's `report.json` per-function `fuzzy_match_percent`, emitting: `symbol, our_pct, our_is_stub, og_pct, our_unit, og_unit, xbox_only(bool), external_method(bool)`. Filter to `og_pct>=100 AND our_pct<100 AND external_method AND NOT (template/dtor/ICF-shaped)`. Refresh on each og report rebuild. Keep it READ-ONLY against decomp.db; emit a TSV/own sqlite, do not write decomp.db.
- **`current_percent` NULL-vs-0 ambiguity** should be normalized in any census tooling (report `measured_zero` and `unmeasured` separately).
- **Static/free-function comparability**: any cross-project porting tool must flag `?X@@YA…` symbols as partition-non-comparable and gate them on a post-port `run_objdiff`, never on the og report alone.
