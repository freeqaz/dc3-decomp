# Build-Environment Audit: Per-Unit Systematic Mismatch Risk

## Question

Are there units where WE compile with the wrong flags / PCH / defines, making *every*
function in them mismatch for non-source reasons? If so, fixing the build env flips many
functions at once. Conversely, is the measurement chain (jeff -> objdiff -> ninja ->
report.json -> decomp.db) free of stale-object and nondeterminism artifacts?

**Bottom line up front: NO systematic per-unit flag/PCH corruption exists.** The build is
flag-uniform with three intentional, correct per-object overrides. The match distribution
is bimodal (95.4% of paired functions at 0% or 100%), which is the *opposite* of the
flag-corruption signature (a unit-wide tight sub-100 band). Measurement is not poisoned by
stale objects, and MSVC compilation is deterministic modulo the COFF timestamp header.

## Method (commands run)

- Read `objdiff.json`, `configure.py`, `config/373307D9/config.json`,
  `config/373307D9/objects.json`, `tools/project.py` (flag/PCH emission), `src/system/decomp_pch.h`.
- `python3` aggregation over `build/373307D9/report.json` (15 MB, streamed): per-unit
  fuzzy_match bands, narrow-band detection, global bimodality histogram.
- `scripts/clean_stale_objects.sh --dry-run`; manual obj-vs-cpp mtime comparison over 861
  obj/cpp pairs; PCH-implicit-input check in `build.ninja`; depfile grep for `system.pch`.
- Determinism test: saved `keygen_xbox.obj`, `touch`ed source, `ninja`-rebuilt, `cmp -l`.
- `git log` in `../jeff`, `../objdiff`, `../wibo` for tool versions.
- `mcp__orchestrator__run_diff_inspect mode=diagnose` on `?opaquePredicate@@YAXXZ` and
  `?random@@YAJJ@Z` (the two lowest keygen functions).
- `sqlite3 'file:decomp.db?mode=ro'` band/stub reconciliation.

## Findings

### 1. Flag determination is per-LIBRARY, uniform, and empirically confirmed correct

Flags come from `config/373307D9/config.json` `cflags` blocks with inheritance
(`configure.py:216-264`), applied per *library* in `objects.json`. The base is:

```
/nologo /wd4355 /wd4164 /c /GR /O1 /Oi /EHsc
```

Library deltas (object counts): `xdk`=1224 obj (base), `engine`=690 (base), `base`=175,
`curl`=62 (+`/TC /GS /D_XBOX360 /DCURL_STATICLIB`), `jpeg`=41 (+`/TP`), `net_xbox`=19 (+`/GS`).
`/TP` (C++) vs `/TC` (C) is auto-selected by file extension in `tools/project.py:1097-1100`.

**Only THREE per-object `extra_cflags` overrides exist** (`objects.json`):
- `keygen_xbox.cpp`: `["/Od"]`  (disable optimization)
- `system/synth/tomcrypt/ctr.c`: `["/TP"]`  (force-compile-as-C++)
- `system/zlib/ZlibLicense.c`: `["/TP"]`

All three are deliberate and look correct. There are **zero** per-object `cflags`
replacements.

**Evidence the global flags are right (load-bearing):** `docs/decomp/TECHNICAL_NOTES.md:970-977`
states the target is a *debug build* (XBDM present, **no LTCG**, NOT `/Od`), and that
`/O1 /Oi /GR /EHsc` is *"empirically confirmed correct -- /O2 breaks matches, /fp:fast has
no effect"*; `/Ou` (prescheduling) and `/Oc` were tested and rejected. The proof is in the
outcome: **29,380 paired functions reach byte-exact 100%** under these uniform flags
(report.json). You cannot get 29k functions to byte-match with the wrong optimization
level. "Debug build" here means XBDM/no-LTCG, **not** `/Od` -- the original was an
optimizing (`/O1`) build.

### 2. NO unit shows the flag-corruption signature (tight sub-100 band)

A flag/PCH bug would make *every implemented function in a unit* cluster in a narrow
sub-100 band (e.g. everything 60-80%). I scanned all 2,224 units in report.json for units
where >=70% of implemented (>0%) functions sit sub-100 with low standard deviation.

- With sd<=14 and 30<=mean<=90: **0 units** (the falsifiable test for systematic env error).
- Relaxing to "any mostly-incomplete unit, lowest sd first": **only `keygen_xbox`**
  qualifies, and its band is 71.2-98.8% (sd=7.6), which is too wide and too high to be a
  wrong-flag artifact -- a wrong opt level produces a *low, tight* band.

Global per-function histogram (30,798 functions with a percent in report.json):

```
  0%          1    0.0%
  0-40       24    0.1%
  40-70      91    0.3%
  70-85     232    0.8%
  85-95     504    1.6%
  95-100    566    1.8%
  100%    29380   95.4%
```

**95.4% are at 0% or 100% -- the distribution is strongly bimodal.** This is the healthy
signature: divergence is *per-function* (source not yet written/perfected), not *per-unit*
(wrong env). Only ~1,418 functions (4.6%) are partial -- the real frontier, consistent with
MEMORY's "real workable frontier ~1,356 fns."

### 3. keygen_xbox: the one banded unit -- flag is RIGHT, residual is /Od source-shaping

keygen_xbox builds with `/Od` (confirmed: `cl : Command line warning D9025 : overriding
'/O1' with '/Od'` during recompile; flag in `objects.json:12`). Its functions are
deliberately obfuscated key-gen routines (`opaquePredicate`, `supershuffle`, `mash`,
`getMasher`) -- exactly what you'd compile `/Od` to preserve the obfuscation.

`run_diff_inspect diagnose` on the two lowest (`?opaquePredicate@@YAXXZ` 71.2%,
`?random@@YAJJ@Z` 78.3%) shows a uniform, recognizable `/Od` lowering pattern, NOT a flag
mismatch:

```
idx 2: TGT lwz r11, 0x0, r11, lbl_82F5E180     <- /Od: materialize global addr, deref @0x0
       SRC lwz r10, ?lbl_82F5E180@@3JC, r11      <- our source folds reloc into displacement
```

The target reloads the global's address into a register and dereferences with a `0x0`
displacement (the `/Od` "global reload" pattern from MEMORY's lever catalog); ours folds
the relocation directly. Every keygen mismatch is this pattern plus the GPR swaps it
cascades (r10<->r11, etc.). **This confirms the `/Od` flag is correct** -- if the flag were
wrong the whole function would diverge in op selection, not just in global-access lowering.
The residual is a *source-shaping* problem (force the volatile reload), routable per-function,
not a build-env fix.

### 4. PCH is correctness-neutral and ninja tracks it properly -- no stale risk for eligible files

`config.pch_eligible_dirs` (`configure.py:286-289`) = {rndobj, hamobj, char, synth, ui,
flow, gesture, world, meta, obj, os, utl, movie}. PCH use is gated on: plain `msvc` rule +
C++ + not-`/TC` + directory in that set (`tools/project.py:1163-1177`).

- `src/system/decomp_pch.h` contains **only** `#include "obj/Object.h"` and
  `#include "os/Debug.h"` -- **no `#define`s**. A forced-include (`/FI"decomp_pch.h"`) of a
  header is codegen-identical to an explicit `#include` of the same header. 934 source files
  already `#include "obj/Object.h"` explicitly. **The PCH cannot introduce per-unit
  divergence** -- it is purely a build-speed optimization.
- The `.pch` binary IS an explicit implicit input on every `msvc_pch` edge
  (`build.ninja:248`, `tools/project.py:1177`), and the PCH create rule uses
  `/showIncludes` + `deps = msvc` (`build.ninja` `rule msvc_pch_create`). So when
  Object.h/Debug.h (or anything transitively included) changes, the PCH rebuilds and
  cascades to all eligible objs. CLAUDE.md's "Automatic header tracking" claim is accurate.

### 5. Stale-object risk: currently ZERO true staleness; the dry-run flags only false positives

`scripts/clean_stale_objects.sh --dry-run` reported 26 "stale" objects. Its heuristic is
`obj_mtime < pch_mtime` (`clean_stale_objects.sh:78`). **All 26 are in NON-PCH-eligible
directories** (math, synth_xbox [note: `synth_xbox` != eligible `synth`], net, zlib,
oggvorbis, xdk, Main, ChecksumData). These files don't use the PCH, so being older than the
PCH (mtime 2026-06-02) is irrelevant -- pure false positives.

Authoritative check (obj older than its OWN .cpp = real ninja-needs-rebuild staleness):
**0 of 861 obj/cpp pairs are stale.** Current percentages are NOT stale-object artifacts.

### 6. wibo / build determinism

- wibo in use: `1.0.1-9-g6a7c37e` (binary built 2026-05-28). This is exactly the version
  MEMORY records as fixing the code=287 (128+SIGSYS seccomp) bug. The case-resolution bug
  is fixed; no open determinism concerns found in wibo docs.
- jeff: `a422812` (2026-06-09). objdiff fork: `444096c` (v4.2.3, 2026-05-30); `objdiff.json`
  pins `v4.2.2` -- minor skew, both have the v4.2.0 funclet-pairing.
- **Determinism test:** recompiled `keygen_xbox.obj` twice, byte-compared. Differed at
  **only 3 bytes: offsets 4-6 = the COFF `TimeDateStamp` header field** (`0546276a` vs
  `652c296a`). Code, symbol table, and relocations are byte-identical. objdiff diffs
  sections/symbols, not the COFF timestamp, so this is invisible to measurement. **MSVC
  compilation is deterministic in every measurement-relevant respect.**
  - ⚠️ **CORRECTED 2026-08-31 — this test under-reported, and the shape of the error is
    the lesson.** Two tells were in the numbers all along: `TimeDateStamp` is a **4**-byte
    field at offsets **4–7**, not 3 bytes at "offsets 4-6", and a byte differ that
    mis-states the width of the one field it did find is summarising, not exhaustive.
    `keygen_xbox.obj` also carries a clock-derived **CodeView `S_OBJNAME` signature word**
    in `.debug$S` — verified 2026-08-31 at file offset **`0x1e8`** — which this comparison
    never reported. So "differed at only 3 bytes" was never the whole difference, and the
    conclusion drawn from it, **"MSVC compilation is deterministic in every
    measurement-relevant respect"**, was overstated in exactly the way that matters: it is
    true of the *objdiff* plane and false of the *object-bytes* plane. Measured 2026-08-31,
    two full rebuilds of identical source in one tree at one path: **980 of 989 objects
    differ.** Fixed at the cause in `ee8902a22`. The audit's downstream implication —
    "measurement is trustworthy for the build-env dimension" — survives, because objdiff
    scores genuinely cannot see either field; what does not survive is any byte-identity
    A/B built on top of this row.

### 7. report.json vs decomp.db planes (measurement-correctness note)

The scout's "19,626 at 0%" came from a snapshot during sync; the live read shows the DB
bands as: 0%=1,537 / 0-40=31 / 40-70=95 / 70-85=240 / 85-95=508 / 95-100=19,037 /
100=31,056 (total 52,504; is_stub=1 on 2,686). report.json has 30,798 *paired* functions
(only symbols present in the .obj). The two planes count different populations
(report.json = source-paired symbols; decomp.db = full XEX function inventory). Neither is
"wrong," but **roadmap math must state which plane it uses** -- the real workable frontier
(partial band) is ~1,418 (report.json) / ~874 (DB 0.5-99.5 excluding the 19k at 95-100).

## Implications for the roadmap

1. **Do NOT spend effort hunting for "wrong-flag units."** The falsifiable test (tight
   sub-100 band) returns zero candidates. There is no large-flip-from-build-env win
   available. Flag uniformity + 95.4% bimodality proves divergence is per-function.
2. **keygen_xbox** is the only banded unit; its 17 sub-100 functions are routable via the
   `/Od` global-reload source-shaping lever (MEMORY: "global reload" / "cached-stack-address"
   levers). Worth ~17 functions, low value, but a clean test case for the `/Od` lever since
   the flag is confirmed correct.
3. **Measurement is trustworthy** for the build-env dimension: no stale objects, no
   meaningful nondeterminism, PCH is correctness-neutral and properly tracked. Roadmap
   percentages are not build-env artifacts.
4. **Plane discipline:** every roadmap "X functions remaining" claim should cite report.json
   vs decomp.db, because they disagree by ~20k.

## Tooling gaps found

1. `clean_stale_objects.sh` compares every obj's mtime against the PCH, but ~half the tree
   (non-PCH-eligible dirs) never uses the PCH. **Fix:** only check objs whose build edge is
   the `msvc_pch` rule (parse build.ninja), and add a second pass comparing obj vs its own
   .cpp (the real staleness signal -- 0 found this run). Current output is mostly false
   positives, which trains agents to ignore it.
2. **No per-unit flag-divergence detector exists.** There is no tool that asserts "every
   unit's compile command uses the expected library-inherited flags + only the sanctioned 3
   overrides." A drift in objects.json (or a future per-object override bug) would go
   unnoticed. Build a `scripts/audit_unit_flags.py` that diffs each build.ninja `cflags`
   line against the config.json-derived expected set and lists deviations.
3. **No build-determinism gate.** The COFF-timestamp diff masks the determinism signal from
   naive `cmp`. A check should compile a sample TU twice and compare *after zeroing
   offsets 4-7* (or compare via objdiff section/symbol bytes), so real nondeterminism
   (should it ever appear from wibo) is caught in CI rather than silently corrupting a
   measurement.
