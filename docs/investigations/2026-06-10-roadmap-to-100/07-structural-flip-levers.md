# 07 — Structural Flip Levers

## Question
Are there STRUCTURAL fixes — a wrong struct layout, a wrong inline in a hot header, a
wrong vtable order, a wrong macro/compile flag — that would flip MANY functions at once?
A single such error can depress hundreds of functions a few percent each. Find them, rank
by blast radius, and (read-only) specify the exact fix + validation.

## Method (commands run)
- `sqlite3 'file:decomp.db?mode=ro'` band/unit/class histograms over `functions`.
- Python clustering of the 85-100 and 40-100 frontier by **true class name** parsed from the
  mangled symbol (`?method@Class@@...`), with per-class mean/stddev/min/max — low variance is
  the signature of a shared-cause depression.
- `mcp__orchestrator__run_diff_inspect mode=diagnose` on representative siblings of the
  densest clusters (HamNavList ×2, Spotlight ×2) to look for the SAME mismatch shape.
- `mcp__orchestrator__lookup_struct_offset` to test the offset-shift hypothesis.
- Streamed `build/373307D9/report.json` in Python: split total/matched/fuzzy by XDK vs game;
  computed the game-only fuzzy-band distribution.
- Cross-checked DB symbols vs report.json symbol set; read `scripts/sync_match_percent.py`
  to learn how `current_percent` is populated (it is `fuzzy_match_percent`, only for symbols
  present in report.json).
- Read `docs/decomp/patterns/at-limit-systemic.md` to align with already-cataloged systemic causes.

## Findings

### F1 (load-bearing) — The headline 43.8% is XDK-diluted; game code is at 77.5% matched / 93.7% fuzzy
Splitting `report.json` units into XDK (`name` starts with `xdk/` or contains `/xdk/`) vs game:
```
XDK   units=1224 total=4,951,700 matched=        888  matched%= 0.0  fuzzy%(code-wt)= 0.0
GAME  units=1000 total=6,427,648 matched=4,982,816  matched%=77.5  fuzzy%(code-wt)=93.7
HEADLINE incl xdk: 43.8%   HEADLINE excl xdk: 77.5%
```
The denominator behind the 43.8% headline is **43% Microsoft SDK code** (D3DXShader compiler,
XGRAPHICS, nuispeech, ST = Skeletal Tracking, LIBCMT, Bink). It is 0.0% matched and is not the
decomp target. This is the single most important correction to "what is done": **the game/engine
is at 93.7% fuzzy.** No structural lever competes with simply reporting the right denominator.

### F2 (load-bearing) — The scout's "19,626 @ 0% / 5.4M bytes" is NULL-percent (UNMEASURED), not 0% (measured no-match)
The DB currently has **only 1,537 rows at `current_percent = 0`** (277,572 bytes), and
**18,089 rows with `current_percent IS NULL`** (the scout's "0%" bucket). Bucketing those NULL rows
against report.json fuzzy:
```
xdk_0        15941   (XDK fns measured at 0% fuzzy, never synced into current_percent)
absent        2059   (symbol not in report.json at all — jeff never paired; EH funclet / artifact)
game_0          82   (genuine untouched game functions among NULLs)
xdk_nonzero      7
```
`scripts/sync_match_percent.py:251-258` only updates `current_percent` for symbols **present in
report.json**, using `fuzzy_match_percent` (line 78/84). 4,142 DB symbols are absent from report
(`DB symbols NOT in report.json: 4142`); these stay NULL forever. **NULL ≠ 0%.** The roadmap
must not treat 19,626 as a game-function backlog — the true untouched game frontier among NULLs
is ~82 functions.

### F3 (load-bearing) — There is NO single undiscovered struct/vtable/sizeof lever in the game frontier
Clustering the 85-100 frontier by true class name (1,415 functions) yields **many classes with
few functions each, all averaging 93-97%** with HIGH within-class variance:
```
HamDirector  n=23 avg=96.6 min=87.6 max=100   RndText  n=15 avg=93.7 min=85.7 max=100
HamNavList   n=21 avg=95.0 min=85.6 max=99.6   MoveDir  n=14 avg=93.9 min=86.0 max=99.5
Spotlight    n=10 avg=95.3 min=87.3 max=99.9   RndMesh  n=13 avg=96.4 min=88.1 max=99.8
```
Extending to the 40-100 band and sorting by lowest mean, even the lowest clusters
(`Rnd` n=6 avg=82.2 min=60.1 **max=97.8**, `RndSpline` n=5 avg=83.1 min=60.7 **max=99.0**)
have a member at/near 100%. **A real shared-cause structural error (wrong field offset, wrong
vtable slot, wrong sizeof) produces UNIFORM, LOW depression across ALL members of a class — every
function touching the bad field is wrong by the same delta.** That signature is absent: the
high max in every cluster proves the shared layout is correct and the divergence is per-function.

### F4 — Representative diagnose confirms per-function regalloc, not shared layout
`HamNavList::SendHighlightMsg` (89.2%) and `SendHighlightSettledMsg` (85.6%) — sibling
message-send wrappers — share a shape, but it is a regalloc shape, not a layout error:
- Dominant offset delta `+8` (3 instrs); the recurring `0x54`/`li r5,0x1`/`addi rN,rN,0x54`
  cluster is the **`Message`/`DataNode` ctor inline** (`NavHighlightMsg msg(dataSym,i,this,canSel)`
  at HamNavList.cpp:780/727). `lookup_struct_offset(HamNavList, 0x54)` returns **"No field found"**
  — confirming 0x54 is a Message Node offset, i.e. the shared inline is expanding CORRECTLY.
- Register swaps are adjacent-allocator pairs: `r28<->r29` (×7), `r29<->r30` (×9), `r11<->r31`.
  This is the canonical post-regalloc coalescing floor (see MEMORY: stream3 commutative floor).

`Spotlight::BuildCone` (43%) and `BuildNGSheet` (58%) are large vertex-buffer math functions with
**46 and 59 distinct register-swap pairs** and offset shifts of `+1536`/`+3072` (vertex strides).
These are deep per-function regalloc/scheduling divergence in big functions — no shared lever.

### F5 — Imperfect game DATA symbols are static/BSS blobs, NOT mis-ordered vtables
891 game units have `matched_data < total_data`, total gap 4.6M bytes, but the top offenders are
huge uninitialized/static data, not vtables:
```
os/Memcard_Xbox 1,250,964 @0.0   utl/GlitchFinder 293,692 @0.0
rndobj/VelocityBuffer 226,400 @0.0   oggvorbis/registry 57,216 @0.0
synth/BinkReader 42,364 @0.0   auto_*_BINKDATA/BINKBSS 20-23K @0.0
```
These are BSS/static tables and Bink blobs the decomp does not (and need not) reproduce; they do
not gate function matching. No vtable-ordering structural error surfaced. (`??_7` vtable data is
matched per the 99.5% `matched_data` on representative game units like keygen_xbox.)

### F6 — The systemic causes that DO exist are already cataloged and mostly UNFIXABLE
`docs/decomp/patterns/at-limit-systemic.md` already enumerates the project-wide levers:
`__FILE__` path remap (FIXED via WIBO_PATH_MAP), `_MemAllocTemp` vs `MemAlloc` (FIXED),
ICF/LINKER_MERGED (UNFIXABLE), `DoneLoading`→`OnlyReturns` ICF (UNFIXABLE), FormatString-in-
MILO_NOTIFY stack-frame divergence (UNFIXABLE — `__forceinline` REGRESSED 96.7→87.2), and
block-sinking (361 fns, PGO-gated, UNFIXABLE after c2.dll RE + binary patching). The high-value
structural fixes have already been harvested; what remains is a regalloc/scheduling floor.

### F7 — Game near-miss frontier sizing (from report.json, authoritative)
```
band     count        bytes
100      29016    4,982,816
99-100     445      318,184
95-99      471      303,092
90-95      305      200,024     <- 90-99.99 routable near-miss = 1,221 fns / 821,300 bytes
80-90      320      200,864
50-80      185      120,980
0-50        27       21,744
0         1669      279,944     <- includes 2,686 stubs + unpaired
```
The realistic remaining game frontier is **~1,221 near-miss functions (90-99.99%, 821K bytes)**
plus the untouched/stub tail. There is no lever that flips a large fraction of these at once;
they must be ground down per-function (permuter/asm-archaeology) or are floors.

## Implications for the roadmap
1. **Fix the measurement first (biggest "lever" of all).** Report a game-only headline that
   excludes `xdk/*` units. 43.8% → **77.5% matched / 93.7% fuzzy** is the honest game-progress
   number. This is a reporting change, not a code change, and it reframes the entire backlog.
2. **Stop treating ~19.6K as a game backlog.** ~16K of it is XDK vendor code (decide explicitly:
   out-of-scope) and ~2K is unpaired EH-funclet/jeff artifacts. The genuine untouched *game*
   frontier is ~82 NULL + ~1,669 zero-fuzzy (mostly stubs/tiny). "Done" for the game = the 1,221
   near-miss band closed, the ~1,669 zero/stub functions implemented, minus the documented floors.
3. **No structural flip lever exists to find here.** The shared-cause errors were already found
   and either fixed or proven unfixable (F6). Effort is better spent on (a) the per-function
   permuter/archaeology grind of the 1,221 near-misses and (b) implementing the zero/stub game tail.
4. **The one remaining "structural" win is a tooling/pairing win, not a source win** (see gaps).

## Tooling gaps found
- **NULL-vs-0% conflation in the DB.** `current_percent IS NULL` (never synced) reads
  indistinguishably from `= 0` (measured no-match) in naive `WHERE current_percent < 1` queries,
  producing the phantom "19,626 @ 0%." Fix: sync should write a measured 0% for XDK fns and leave
  a distinct sentinel (or a `measured_at` column) for truly-unpaired symbols; reports should
  COALESCE NULL→"unmeasured" not →0.
- **No XDK exclusion in the headline metric.** report.json `matched_code_percent` mixes vendor
  and game code. There is no `is_vendor`/scope flag per unit. Add one (unit-name prefix is enough)
  and emit a game-scoped measure.
- **2,059 DB symbols absent from report.json** — jeff pairing gap. These are likely MSVC EH
  funclets (`fn_<addr>`) per MEMORY (objdiff v4.2.0 funclet pairing recovered +1,264 dc3); a
  re-run / pin-bump of the objdiff funclet pairing may reconcile these and is the only structural
  pairing lever left.
- `verdict_reason` confirmed stale (per audit constraints; not used here).
