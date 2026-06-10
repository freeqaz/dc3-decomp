# 11 — Native-Port Behavioral Bug Census (unicorn DIVERGENT ∩ native-compiled)

## Question
The native port (`native/`, x86_64 + WebGPU, plus web/WASM) compiles the same `src/` tree.
Any decompiled function that is *behaviorally* wrong (not just cosmetically mismatched) is a
live native bug. Using the unicorn behavioral-test columns in `decomp.db`
(`unicorn_verdict` / `unicorn_class`), census the real behavioral divergences, size the
coverage gap, prioritize by what the native port actually executes, and confirm a few are
real bugs.

## Method (commands run)
- `.schema functions` — found unicorn columns: `unicorn_verdict`, `unicorn_class`,
  `unicorn_confidence`, `unicorn_tested_at`, `unicorn_reason`, `unicorn_signal_version`.
- DB queried READ-ONLY: `sqlite3 'file:decomp.db?mode=ro' "..."` for verdict/class/confidence
  distributions, percent bands, coverage gap.
- Native source set extracted from `native/CMakeLists.txt` (`DC3_ENGINE_SOURCES` explicit list,
  757 `.cpp`), normalized `src/X.cpp` → DB unit `default/X` (`/tmp/native_units.txt`), then
  python-joined against the DIVERGENT set.
- HX_NATIVE-guard heuristic: counted `HX_NATIVE` occurrences per `.cpp` to flag files whose
  divergent body may be replaced by a native rewrite (so the Xbox decomp divergence is moot).
- Cross-checked 3 candidates with `mcp__orchestrator__run_objdiff` + `run_diff_inspect diagnose`:
  `ResetNormals`, `DecodeDxt5Alpha`, and inspected `MemMgr.cpp`/`HamSkeletonConverter.cpp` source.

## Findings

### F1 — There is NO `logic` class in this DB. The scout's framing is wrong.
`SELECT COUNT(*) WHERE unicorn_class='logic'` → **0**. The `query_functions` MCP enum lists
`logic` as a *possible* class, but the actual unicorn run never emitted it. The classes that
exist are: `build_env, call_arg, call_count, cap_exhausted, error, fpr_precision, merged_arg,
merged_call, object_memory, orig_error, regalloc, return_value, stack_layout`. The real-bug
classes (per the schema doc) are `call_count / call_arg / return_value / object_memory / error /
cap_exhausted`; the artifact classes are `build_env / regalloc / merged_call / merged_arg /
stack_layout / fpr_precision / orig_error`.

Full DIVERGENT distribution (`unicorn_verdict='DIVERGENT'`, 1,877 rows):
```
build_env|781   call_count|481   orig_error|248   stack_layout|152  merged_call|85
call_arg|32     error|21         regalloc|21      object_memory|18  return_value|13
cap_exhausted|12 fpr_precision|8  merged_arg|5
```
Real-bug classes = **577**; artifact classes = **1,300**.

### F2 — 68% of "real-bug" DIVERGENT functions are at 100% byte-match → unicorn false positives.
`WHERE unicorn_verdict='DIVERGENT' AND current_percent>=100 AND class IN (real)` → **395 of 577**.
By class at 100%: `call_count|329, call_arg|23, object_memory|13, error|13, cap_exhausted|9,
return_value|8`. A function that is byte-for-byte identical to the original CANNOT be a behavioral
bug — these are unicorn-harness artifacts (overwhelmingly `call_count`, likely from external-symbol
relocation/ICF fan-out resolving to different addresses in the emulator, or merged symbols: of the
386 100%-DIVERGENT real-bug rows, 111 have `merged_symbol_count>0`). **Trust unicorn DIVERGENT only
when `current_percent < 100`.**

### F3 — The actionable native-suspect set is 182 sub-100% real-bug DIVERGENT (≈120 KB).
`WHERE DIVERGENT AND current_percent<100 AND class IN (real)`:
```
call_count|152 (107,076b, avg 84.7%)   call_arg|9     error|8
return_value|5   object_memory|5        cap_exhausted|3
```
Confidence: 136/152 call_count are `stable_divergent` (all probe runs diverged); 16 `input_sensitive`
(less reliable). Percent bands: 95-100=78, 85-95=49, 70-85=32, 40-70=11, 0-40=9.

### F4 — 153 of the 182 are in native-compiled subsystems; the 29 non-native are correctly Xbox/platform.
Joining against `native/CMakeLists.txt`'s `DC3_ENGINE_SOURCES`: **153 native-compiled (110 KB),
29 not-compiled (10.7 KB)**. The non-native 29 are dominated by exactly the excluded code the CMake
comment names (`xdk/nuispeech/mmio`×7, `rnddx9/Rnd_Xbox`×3, `keygen_xbox`, `Cache_Xbox`,
`synth_xbox/Mic`, `net/curl`, `*_Xbox.cpp`, `*_Win.cpp`) — confirming the join is sound.
Native-compiled subsystem breakdown:
```
hamobj|34  rndobj|28  char|20  utl|15  meta_ham|9  gesture|8  os|8  world|7
ui/flow|6  math|6  net|3  obj|3  meta|3  midi|1  synth|1  lazer/game|1
```

### F5 — HX_NATIVE-guarded files over-count "live native bug." 53 functions are in pure-shared, no-guard files.
The native-unit join counts a function as native if its *file* is compiled, but ~65% of those files
(100 of 153 divergent functions) contain `HX_NATIVE` guards — and in some the divergent body is
exactly what the guard replaces. Confirmed example: **`MemMgr.cpp` is natively rewritten under
`#ifdef HX_NATIVE`** (4 guards at lines 46/127/149/313). Its two highest-fan_in divergences —
`MemOrPoolAllocSTL` (fan_in=533) and `MemAlloc` (fan_in=354, 1.4% match) — are therefore **NOT live
native bugs**; the native build takes the guarded path. These two would otherwise top any
fan_in-ranked dashboard, so the guard filter is load-bearing.
- **53 divergent functions (43 KB) live in files with ZERO HX_NATIVE guards** = the most defensible
  "definitely-live native bug" set (pure shared decomp, no native override). Includes
  `HamSkeletonConverter::Set` (73.6%, IK/skeleton), `CSHA1::Transform` (55.7%),
  `ClipCollide::Collide` (99.9%), `RndWind::SelfGetWind` (84.6%), `CharClipDisplay::SetStartEnd`
  (98.3%, object_memory), `RndLight::Load` (99.7%, error).
- The 100 guarded-file functions need per-function verification (the divergent line may or may not be
  under a guard) before they count as bugs.

### F6 — Coverage gap: 60.9% of sub-100% functions are unicorn-untested; the 85-99 band has 314 native untested fns (184 KB).
`WHERE current_percent<100 AND size>0` → 3,359 functions; **2,047 (60.9%) have NULL unicorn_verdict.**
The high-value 85-99% band (code most likely shipped to and exercised by native):
```
1,456 total | 351 untested | 845 EQUIVALENT | 363 DIVERGENT (size>0 subset: 845/467/2047)
```
Of the 351 untested 85-99 functions, **314 (184,528 bytes) are native-compiled** — unquantified
native risk. Across ALL sub-100 untested, **1,191 are native-compiled (352 KB)**. Of the 30,977
native-compiled functions (size>0): 23,358 EQUIVALENT, 1,785 DIVERGENT, **5,834 untested**.
The unicorn run is also stale: `unicorn_signal_version` is NULL for 27,300 rows (only 43 are v2/v3),
last `unicorn_tested_at` = 2026-05-14.

### F7 — Cross-check: 2 of 3 confirmed real divergences with characterized bug shape.
- **`DecodeDxt5Alpha` (rndobj/Bitmap, 71.5%, call_count) — CONFIRMED real logic bug.**
  `run_diff_inspect diagnose`: `diff_op` at idx 108 `TGT bne cr6,0x1a4` vs `SRC beq cr6,0x1bc`
  (branch-polarity inversion), plus a `b↔beq` replace at idx 60. DXT5 alpha decode runs in the
  native renderer; a flipped branch produces wrong alpha bytes. **Bug shape: wrong comparison /
  branch polarity in the alpha-block decode loop.** (Bitmap.cpp has 1 HX_NATIVE guard — verify the
  divergent line is not guarded, but objdiff confirms the decomp body itself diverges.)
- **`ResetNormals` (rndobj/Utl, 67.5%, call_count) — partly real, mostly regalloc cascade.**
  diagnose: 275 reg-swaps across 77 pairs + offset shift -16 + structural stack Δ+0x10, but also
  **24 real replaces incl. bne↔beq at idx 296/306 and `cmpw cr6,r11,r10` vs `cmplw cr6,r10,r17`
  (signed vs unsigned compare!) at idx 224.** The signed/unsigned compare swap and branch flips are
  behaviorally meaningful (mesh normal reset). Characterization: register-allocation floor *with*
  embedded comparison/branch divergences.
- **`MemAlloc` (utl/MemMgr, 1.4%, call_count, fan_in=354) — NOT a native bug** (F5): native build
  uses `#ifdef HX_NATIVE` allocator path; the Xbox decomp body is dead on native.

### F8 — milo-tests exists (~50 test files) but none target a confirmed divergence.
`native/tests/` has test_movegraph, test_charclipgroup, test_foot_bone_invariants,
test_charbones_serialization, test_mesh_loading, etc. — relevant *subsystems* but no test directly
pins `DecodeDxt5Alpha`, `HamSkeletonConverter::Set`, `ResetNormals`, or any confirmed-divergent
function. There is no regression policy linking unicorn DIVERGENT → a native unit test.

## Implications for the roadmap
1. **"Done" must exclude unicorn false positives.** Any "100% matched, only cosmetic floor" success
   criterion must NOT count the 395 100%-and-DIVERGENT rows as bugs (F2). Conversely, a native-bug
   burn-down should track the **182 → 153 → 53** funnel (sub-100 real-bug → native-compiled →
   no-HX_NATIVE-guard).
2. **The native-bug frontier is small and tractable:** ~53 high-confidence live bugs (43 KB) plus up
   to ~100 more pending per-function guard verification. This is a finite, fixable list — unlike the
   matching frontier (thousands).
3. **The bigger native risk is the coverage gap, not the known divergences:** 314 untested native
   functions in the 85-99 band (184 KB) and 1,191 untested native functions overall. These are
   *unquantified*. Re-running unicorn on the current `src/` (it is 4 weeks stale, signal_version
   mostly NULL) is the single highest-leverage measurement action.
4. **call_count is the dominant signal and the noisiest.** It conflates true call-graph divergence
   with relocation/ICF artifacts (F2) and template-inlining differences (many sub-100 call_count rows
   are STL `_Param_Construct` / `swap` / `__adjust_heap` / scalar-deleting-dtor helpers, not gameplay
   logic). Triage call_count by (a) current_percent<100, (b) named gameplay function not STL helper,
   (c) presence of bne↔beq / cmpw↔cmplw in diagnose.

## Tooling gaps found
- **No HX_NATIVE-guard awareness in the native-relevance join.** The DB has no column for "this
  function's body is replaced on native." A file-level grep over-counts (MemMgr) and a line-level
  check is needed to know if the *divergent* instructions are under a guard. → add a
  `native_body_overridden` flag (per-function) computed by checking whether the function's source
  span is inside an `#ifdef HX_NATIVE` / `#ifndef HX_NATIVE` block.
- **Stale + sparse unicorn coverage.** 60.9% of the sub-100 frontier and 5,834 native-compiled
  functions are untested; `unicorn_signal_version` is NULL for nearly all rows. No freshness gate
  ties verdicts to the current source hash.
- **`unicorn_class` lacks a `logic` value** despite the schema enum advertising it; the real
  bug-bearing classes are call_count/call_arg/return_value/object_memory/error. Any agent filtering
  on `unicorn_class='logic'` gets zero rows silently. → fix the enum doc or re-classify.
- **No native-bug dashboard / regression-test policy** linking DIVERGENT ∩ native-compiled ∩
  no-guard to a milo-tests unit test.

## Recommended native-risk dashboard query
```sql
-- DIVERGENT ∩ native-compiled ∩ sub-100 ∩ real-bug class, ranked by fan_in then size.
-- (Filter native-compiled units via the DC3_ENGINE_SOURCES list; exclude HX_NATIVE-rewritten
--  files in a post-step — see tooling gap.)
SELECT unit, demangled, current_percent, size, fan_in, unicorn_class, unicorn_confidence
FROM functions
WHERE unicorn_verdict='DIVERGENT'
  AND current_percent < 100
  AND unicorn_class IN ('call_count','call_arg','return_value','object_memory','error','cap_exhausted')
  AND unicorn_confidence='stable_divergent'
ORDER BY fan_in DESC, size DESC;
```
Policy: for every confirmed logic divergence (objdiff diagnose shows diff_op/replace = branch
polarity or compare-signedness, not pure regalloc), add a focused milo-tests unit test that pins the
behavior to the original (use RB3/RB2 reference or the Xbox-emulated ground truth). Start with the 53
no-guard functions; `DecodeDxt5Alpha`, `HamSkeletonConverter::Set`, `ClipCollide::Collide`,
`CharClipDisplay::SetStartEnd` are first candidates.
