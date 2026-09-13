# A symbol the report scores FEWER TIMES than the linker map lists is unmeasured by construction

*dc3, 2026-09-13. Instrument: `scripts/analysis/map_multiplicity_census.py` (10
sabotage-verified controls, `--selftest`).*

## The rule

> **Count how many DISTINCT addresses `ham_xbox_r.map` lists a CODE symbol at,
> and how many times `report.json` scores that name. Where scored < listed, one
> or more bodies is measured by nothing at all.**

`config/373307D9/symbols.txt` can bind a mangled name to exactly **one**
address. Every further definition is carved as `fn_<addr>`, so it never pairs
with our source symbol, is absent from the numerator and the denominator alike,
and can be arbitrarily wrong forever. Neither the match ruler nor the native
gate has any reason to look at it.

## This supersedes the "two out-of-line bodies" test

The weak form was *"a symbol defined out-of-line twice where retail has one
header definition"*. That is a real shape — `?FillCompressedVertex@@…`, two map
addresses of identical size, whose unscored copy truncated UVs where retail
half-encodes and packed colour ABGR where retail packs ARGB, both live native
rendering defects — but it is **not necessary**. Three of rb3-xenon's four real
hits are *genuinely distinct functions sharing a mangled name*, one per TU, with
different bodies **by design**. None of the double-definition preconditions
hold; the report still scores only one of each pair.

**Run the count comparison, not the double-definition test.**

## ⚠ The exposure COMPOUNDS: the call sites go unmetered too

A `fn_<addr>` target-side name is a **placeholder**, and `name_check` exempts
placeholder relocation names *before* the wrong-callee detector runs. So a call
site that passes a function pointer our tree **fabricated** — a symbol that
appears nowhere in the retail map — reads `100.0% canonical, all equal`.

Measured on `?Init@UIManager@@UAAXXZ`: two `diff_arg` rows,
`fn_8277AFC0` against a fabricated `?UITerminateCallback@@YAXXZ`, scoring
**100.0 before and after the fix**.

> unmeasured symbol + exempted call site = a wrong function pointer invisible to
> every ruler.

## dc3 census (whole binary, 2026-09-13)

| | |
|---|---|
| CODE symbol names in `ham_xbox_r.map` | 77,927 |
| listed at more than one distinct address | 1,393 |
| of those, scored fewer times than listed | **1,393** (all of them) |
| listed once and scored zero times (ICF fold members — *denominator only*) | 29,757 |
| scored function entries in `report.json` | 48,365 |

| class | n |
|---|---|
| `__unwind$` EH ordinal collisions | 1,367 |
| `??__E` / `??__F` per-TU init and atexit thunks | 11 |
| `LIBCMT` / `xaudio2` internals we do not compile | 7 |
| `operator delete` | 2 |
| `__catch$` | 1 |
| **REAL** | **5** |
| *benign total (the in-pass control)* | *1,388* |

The five: `DebugModal`, `FillCompressedVertex`, `PackVector`, `SampleAlloc`,
`TerminateCallback`.

**The benign majority IS the control.** If a run returns none of the thunk /
operator / EH rows, the map parse or the report join is broken — the tree is not
clean. The instrument exits **4** rather than print a null over an empty control
population.

## Adjudicating a hit

1. `symbols.txt` gives the unscored address a `fn_<addr>` name. Find the report
   unit that scores that name; retail's body is in
   `build/373307D9/asm/<unit>.s` under `.fn fn_<addr>`.
2. Read our compiled body with a self-diff of our own object
   (`objdiff-cli diff -1 X.obj -2 X.obj --allow-self-diff --full-listing <sym>`)
   and compare the instruction columns. There is no way to make objdiff *pair*
   two differently-spelled symbols, so this is a hand comparison.
3. **Resolve every differing relocation name in `ham_xbox_r.map` before calling
   it a defect.** Four of the five dc3 rows differed only on a `MakeString`
   instantiation name, and all of them resolve to `0x824D1870` — one proven ICF
   fold, not a wrong callee.
4. Expect the fix to be **metric-invisible**. Read the opcode rows, not the
   delta, and establish inertness **per function against a base report built in
   the same tree** — `main`'s `report.json` drifts under you as sibling lanes
   land.

## What the audit actually found on dc3

- `DebugModal` (os/Debug.cpp, unscored 0x825CCF20): **40/40 equal**. Correct.
- `SampleAlloc` (synth_xbox, unscored 0x82E42D48): **33/33 equal** modulo the
  fold. Correct.
- `TerminateCallback` (ui/UI.cpp, unscored 0x8277AFC0): **33/33 equal** modulo
  the fold — the *body* was correct, and the defect was at the **call sites**,
  which is exactly what nothing had reason to look at. `UIManager::Init`
  registered a fabricated `UITerminateCallback` while `UIManager::Terminate`
  removed `TerminateCallback`, so the remove was a no-op and `Debug::Exit` ran
  `UIManager::Terminate()` a second time. Retail's list is empty by then.
- `PackVector` / `FillCompressedVertex` (rndobj/Mesh, unscored 0x8263A168 /
  0x8263A360): retail's two copies are **byte-identical apart from relocated
  displacements**, and ours are identical because a prior lane moved both into
  `rndobj/MeshVertCompress.h` as `static`. The unscored twin therefore carries
  exactly the residual of the scored one (96.19 / 99.96) and hides nothing.
  Moving a duplicated definition into a header **structurally closes** this
  class for that symbol.

## Trap met on the way: a COMMENT is not inert

Adding a 9-line explanatory comment above `SampleAlloc` shifted
`SampleFree()`'s `__LINE__` and moved `?SampleFree@@YAXPAXPBDH1@Z` from
**100.0 to 99.888885 (-36 B)**. The note now lives at end-of-file. 36 B of
5,394,452 is **-0.000316 pp** — below the printed precision of any headline, and
visible only because the delta was taken per function.

See also: [relocation-names-are-unmetered.md](relocation-names-are-unmetered.md),
[rounded-100-hides-real-bugs.md](rounded-100-hides-real-bugs.md).
