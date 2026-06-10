# Unit-Level Gap Census + Data-Symbol Census (Audit Task 06)

## Question

Where does the remaining DC3 decomp work actually live, at the **unit** level and in
**data symbols**? Specifically: (1) which subsystems/units hold the unmatched bytes;
(2) which data symbols (vtables/RTTI/strings/initializers) differ and whether vtable
diffs reveal a structural header fix that flips many functions; (3) how much of the
"56% unmatched" is near-done units dragged by a few functions vs broadly-unported
units; (4) how many units are 1-3 functions from "complete"; (5) the exact checkable
definition of "done".

## Method (commands run)

- `python3` streaming `build/373307D9/report.json` (15 MB): per-unit `measures`
  (`total_code`/`matched_code`/`complete_code`/`fuzzy_match_percent`), per-unit
  `sections`, per-function `match_percent_normalized`. Grouped by
  `metadata.source_path`.
- `sqlite3 'file:decomp.db?mode=ro'` for verdict/percent/stub bands, schema, and the
  proposed done-query.
- `bin/objdiff-cli diff -p . -u <unit> "<vtable>" --include-data -f json-pretty` on
  `??_7RndFont@@6B@`, `??_7HamDirector@@6B@` (data census; report.json does NOT
  enumerate data symbols).
- `file` / `llvm-nm` / `nm` on a jeff-synthesized COFF (`build/373307D9/data/...`):
  confirmed only objdiff's fork reads them.
- Read `scripts/sync_match_percent.py:60-160` and `objdiff.json` to find the
  `complete` flag's provenance.

## Findings

### F1. The headline 43.8% is diluted ~2x by un-decompilable XDK/library bytes. Real game-code match is 77.76%.
`report.json measures.matched_code_percent = 43.80%`. But splitting units by whether
they have a `metadata.source_path`:
- SOURCED (game) units: `matched_code 4,983,704 / 6,409,408 = 77.76%`
- NO-SOURCE units: `matched_code 0 / 4,969,940 = 0.00%`

**77.7% of all unmatched code bytes (4,969,940 / 6,395,644) live in NO-SOURCE units**
— and every one has `matched_code == 0` (none are partially ported). These are
Microsoft Xbox SDK / runtime libraries with no source in the tree, broken down by
name prefix: `default/xdk/xgraphics` 1.28M, `default/xdk/nuispeech` 1.26M,
`default/xdk/d3dx9` 0.77M, `default/xdk/xaudio2` 0.35M, `default/xdk/d3d9i` 0.32M,
`default/xdk/nuiapi` 0.29M, `default/xdk/ST` 0.23M, `default/xdk/LIBCMT` 0.13M, ...
These are intentionally not authorable. Any "done" metric that counts them is
permanently capped near 44%. **Load-bearing: the true denominator for "done" is
sourced game code, and we are at 77.76% of it, not 43.8%.**

### F2. `complete_units` / `complete_code_percent` is a stale manual allowlist, not a byte-match metric. Over-reports.
`objdiff.json` carries `"metadata":{"complete":true}` on exactly **968** units —
hardcoded, not derived from the live build. In report.json, a complete-flagged unit's
`complete_code` is force-set to `total_code` (verified: 0/968 complete units have
`complete_code != total_code`). Consequences:
- `src/system/rndobj/Utl.cpp`: `matched_code = 27,388 / 51,980 (52.7%)` but
  `complete_code = 51,980 (100%)`, `complete=True`.
- `src/system/char/CharServoBone.cpp`: `complete=True`, unit fuzzy **99.9993%**, yet
  its `.rdata` section is at **33.3%** and `DoRegulate` at 99.99%.

`sync_match_percent.py:100-134` promotes a unit to complete if **all functions are
100% in report.json OR all functions are COMPLETE/AT_LIMIT verdict in the DB**. The
AT_LIMIT branch is the leak (see F3) and **data sections are never checked at all**.
So `complete_code_percent = 55.79%` and `complete_units = 968` systematically
over-report match progress and should not be used as a "done" signal.

### F3. AT_LIMIT marks 1,845 functions (470,624 bytes) "done" below 85%; 1,552 below 40% — many clearly mislabeled, hiding recoverable work.
`decomp.db`: `verdict='AT_LIMIT'` on 4,405 rows, but
`AT_LIMIT AND current_percent<85 → 1,845 fns / 470,624 bytes`, of which
`<40% → 1,552 fns / 294,404 bytes`. Sampling non-stub AT_LIMIT-at-<40%:
`NgEnviron::Select @11.4%`, `RndShader` virtuals @11-39%, `Hmx::operator*` @14.4%,
`MemAlloc @1.4%`, `RndPropAnim::F... @0.4%`. A genuine floor sits in the high-90s;
these are SIMD/intrinsic-heavy or genuinely-unported functions falsely certified.
**The AT_LIMIT label in the 40-85 (and below) band is unreliable as a done-gate**
(consistent with prior memory). Treating AT_LIMIT as done both over-reports
completion and hides ~470 KB of potentially recoverable work behind a "done" badge.

### F4. decomp.db (52,504 rows) vs report.json (48,413 fns) reconcile via the XDK plane.
DB carries **16,804 `default/xdk/%` rows** the audit's game-frontier queries must
exclude. Filtering to non-xdk/non-lib authorable code:
`total 35,513 fns / 6,569,316 bytes; >=100% → 31,047; <100% non-stub → 2,416 /
1,214,280 bytes; <100% AND is_stub → 739 / 153,864 bytes`. The DB's `31,056 >=100%`
and report's `29,236 matched_functions` differ because the DB counts xdk
report-pairing rows and uses `current_percent>=100` (incl. funclet/ICF pairings).

### F5. The work is overwhelmingly "polish near-done units," not "port new units."
Distribution of **sourced** unmatched code bytes by unit-level fuzzy bucket (units
<100% with code>0; total 1,425,704 bytes):

| unit fuzzy | unmatched bytes | %    | #units |
|------------|-----------------|------|--------|
| 99-100     | 195,532         | 13.7 | 174    |
| 95-99      | 674,408         | 47.3 | 253    |
| 90-95      | 213,736         | 15.0 | 75     |
| 80-90      | 82,812          | 5.8  | 27     |
| 50-80      | 68,624          | 4.8  | 21     |
| 20-50      | 84,928          | 6.0  | 22     |
| 0-20       | 105,664         | 7.4  | 27     |

**61.0% of remaining sourced-unmatched bytes are in units already ≥95% unit-fuzzy** —
near-done units dragged by a handful of stubborn functions. Only **18.2%** is in
broadly-unported (<80%) units. The roadmap is last-mile, not greenfield.

### F6. Top sourced units by remaining unmatched bytes (the prize map).
`unmatch / total / fuzzy / #fns<100 / source`:
- 27,424 / 27,424 / 0.0% / 79 — `src/xdk/LIBCMT/undname.cpp` (MSVC name undecorator;
  whole-file stub `// stub implementation`; not worth matching)
- 24,592 / 51,980 / 87.3% / 54 — `rndobj/Utl.cpp` (Rnd math/util grab-bag)
- 23,860 / 51,912 / 93.7% / 33 — `rndobj/Text.cpp` (RndText layout/measure)
- 21,164 / 35,144 / 96.1% / 32 — `hamobj/RhythmBattle.cpp`
- 20,688 / 81,040 / 96.2% / 46 — `hamobj/HamDirector.cpp` (largest at 3,368 lines)
- 19,280 / 46,768 / 95.1% / 23 — `rndobj/Mesh.cpp`
- 19,012 / 42,176 / 97.1% / 32 — `hamobj/HamNavList.cpp`
- 18,240 / 50,300 / 96.5% / 25 — `hamobj/MoveDir.cpp`
- 17,796 / 31,208 / 97.1% / 19 — `lazer/game/BustAMovePanel.cpp`
- 16,916 / 26,920 / **46.4%** / 87 — `os/PlatformMgr_Xbox.cpp` (Xbox platform glue,
  broadly unported)
- 13,384 / 20,908 / **43.7%** / 59 — `synth_xbox/Synth.cpp` (DSP/voice mgmt)
- 12,104 / 19,444 / **65.7%** / 19 — `rndobj/Shader.cpp` (SIMD shader-const packing)
- 10,736 / 10,736 / **16.1%** / 13 — `synth_xbox/FFT.cpp` (uses
  `xdk/LIBCMT/vectorintrinsics.h` — SIMD, hard floor)
- 9,912 / 13,644 / **33.3%** / 41 — `synth_xbox/Mic.cpp`

The `rndobj/*` + `hamobj/*` cluster (Render + Ham gameplay) holds the bulk of the
high-fuzzy last-mile work; the low-fuzzy outliers (`PlatformMgr_Xbox`, `synth_xbox/*`,
`Shader`) are SIMD/platform-intrinsic-heavy and likely partial floors.

### F7. 153 units are ONE function from complete; 259 need ≤2; 331 need ≤3.
Counting sourced units <100% by #functions-below-100 (size>0):
`1→153, 2→106, 3→72, 4→49, 5→39, ...`. **71 of the 153 single-blocker units have a
blocker that report.json shows at 100.0%** (e.g. `CharServoBone::DoRegulate`
displays 99.99→rounds 100; `UsbMidiGuitar::Poll`, `Curl_http_readwrite_headers`).
These are rounding/recert lag — **a re-sync/recert pass alone flips ~71 units to
complete** with zero decomp work. Of the genuine single-blocker units, 27 have a
blocker at 50-90% and 55 at <50% (real work). These ≤3-function units are the cheap
unit-completion wins.

### F8. Data sections are essentially uncertifiable today — `matched_data=0.08%` is dominated by jeff target-symbol-resolution + ICF artifacts, NOT real fixable data bugs.
`measures.matched_data = 6,132 / 7,748,181 = 0.079%`. Per-section: `.rdata` 844/1343
sections <100%, `.data` 624/899 <100%; `.bss`/`.pdata` always 100%. report.json does
NOT list data symbols, so I diffed vtables directly. `??_7RndFont@@6B@` = **63.7%**,
target_size 148 vs base 144 (one extra slot). But classifying its 26 "replace" reloc
slots:
- **18** = `base=-` reloc-address noise (same symbol, address only differs)
- **6** = `OnlyReturns`/`merged_*` stub-collapse (ICF folds many distinct virtuals to
  one stub address)
- **2** = cross-class ICF alias (target slot named `?Album@HamSongMetadata@@` etc. —
  jeff names a vtable pointer by whatever symbol shares its resolved address)
- **0** = genuinely-different same-class symbol (i.e. no real wrong-virtual-order bug)

`??_7HamDirector@@6B@` is identical in character: target slots resolve to nonsense
like `?SetEngine@CTrigramStore@NUISPEECH@@` and `?CharAdvance@RndFontBase@@` while the
base side shows the correct `RndDrawable` virtuals in order. **The vtable "diffs" are
overwhelmingly target-side resolution artifacts, not header virtual-order bugs.** The
only consistent real signal is a 4-byte size delta (one extra target slot) which may
itself be padding/alignment. **Conclusion: data byte-equality cannot be certified or
meaningfully measured with the current jeff vtable target-symbol resolution + objdiff
data normalization. Exclude data from the "done" definition until that pipeline is
fixed.** This is a measurement-correctness gap, not a backlog of fixable data bugs.

### F9. is_stub = 739 game functions (153,864 bytes) deliberately unimplemented.
`is_stub=1` on 2,686 rows total, all non-xdk (317,608 bytes); 907 at 0%. Restricting
to the non-xdk/non-lib authorable set: 739 stubs / 153,864 bytes. These are the
deliberately-stubbed game functions — correctly excluded from "done" as
not-yet-authored rather than not-matching.

## Implications for the roadmap

1. **Re-anchor the progress headline to sourced game code (77.76%), not 43.8%.** The
   XDK 4.97M-byte denominator is permanent dead weight; reporting it as "unmatched"
   makes the project look half-done when game code is three-quarters matched.
2. **The remaining game-code frontier is 2,416 non-stub functions / 1.21 MB, and 61%
   of unmatched bytes sit in ≥95% units.** Prioritize last-mile function polish in the
   `rndobj/` + `hamobj/` cluster (Utl, Text, Mesh, HamDirector, HamNavList, MoveDir,
   RhythmBattle) over greenfield porting.
3. **Cheap unit-completion wins exist:** ~71 single-blocker units are already at
   100%/rounding and just need a recert/sync; another ~80 single-blocker units + 106
   two-blocker units are small targeted jobs.
4. **Carve out the SIMD/platform floor explicitly:** `synth_xbox/FFT.cpp` (16%),
   `Mic.cpp` (33%), `Synth.cpp` (44%), `rndobj/Shader.cpp` (66%),
   `os/PlatformMgr_Xbox.cpp` (46%) are intrinsic/platform-heavy partial floors — they
   should be diagnosed-then-certified-floor, not chased to 100%.
5. **Do not put data byte-equality in the v1 "done" bar.** It is currently
   unmeasurable due to F8.

### Proposed "done" definition (checkable)

> **DONE** = every authorable game-code function (unit NOT in `default/xdk/%` or
> `default/lib/%`) is either (a) `current_percent >= 100`, (b) `is_stub = 1`
> (explicitly deferred), or (c) certified-floor with a stored `run_diff_inspect
> diagnose` rationale. Data symbols are **out of scope for v1** until the jeff vtable
> target-symbol resolution and objdiff ICF-aware data normalization (F8) land.

Checkable "not-done" query (the burndown number to drive to 0, today = **2,416 fns /
1,214,280 bytes**):

```sql
SELECT COUNT(*), SUM(size) FROM functions
WHERE unit NOT LIKE 'default/xdk/%' AND unit NOT LIKE 'default/lib/%'
  AND is_stub = 0 AND current_percent < 100;
-- today: 2416 | 1214280
```

A stricter "no floor escape hatch" variant subtracts only verified floors (requires a
`floor_certified` column the DB lacks today — see tooling gaps). The honest
sourced-byte burndown from report.json:
`sum(matched_code)/sum(total_code) over source_path units = 77.76%`.

## Tooling gaps found

- **`complete` flag is a stale hardcoded allowlist in `objdiff.json` (968 units).** It
  drifts from the live build (CharServoBone complete=True at 99.99% with 33% rdata).
  There is no automated "uncomplete on regression" path and the gate ignores data.
- **AT_LIMIT verdict is used as a done-gate (`sync_match_percent.py:106`) but is
  unreliable below 85%** (1,845 fns <85%, 1,552 <40%). No `floor_certified` /
  `diagnose_rationale` column exists to distinguish a true certified floor from a
  mislabeled low-percent function.
- **report.json does not enumerate data symbols at all** — vtable/RTTI/string/init
  match% is invisible to every report-level tool (`objdiff-cli report query`,
  `measure_progress.sh`, the MCP wrappers). Data census requires per-symbol
  `objdiff-cli diff --include-data` calls (~5s each), which does not scale.
- **jeff resolves target data-relocation slots to whatever symbol shares the resolved
  address** (ICF / `OnlyReturns` / `merged_*` collapse), producing cross-class
  nonsense in vtable diffs (`?Album@HamSongMetadata@@` in `RndFont`'s vtable). This
  makes `matched_data=0.08%` meaningless and data byte-equality uncertifiable.
- **No subsystem/sourced-vs-xdk split in any progress tool.** The 43.8% headline is
  reported without the 77.76% sourced denominator, mis-framing the project's state.
