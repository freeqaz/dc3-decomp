# Adding a comment to a .cpp is inert — except above a literal `__LINE__`

**Whole-binary measurement, 2026-09-13, branch `w3-r`. The class is ONE function
of 48,365, worth 36 bytes.**

## The claim that prompted this, and why it is wrong

A wave-3 lane was told that `MILO_ASSERT`, `MILO_NOTIFY`, `MILO_FAIL` and friends
embed `__LINE__`, so any line added to a `.cpp` shifts every assert constant below
it and costs mismatch rows. **No MILO macro reads `__LINE__` in this repo.**
`src/system/os/Debug.h` takes the line number as an *explicit argument* —
`MILO_ASSERT(ret, 0x19)` — precisely so the decomp can reproduce the target's
constant. The same holds for `MILO_ASSERT_EXPR`, `MILO_ASSERT_IF`,
`MILO_ASSERT_RANGE(_EQ)` and `OBJ_MEM_OVERLOAD`. `MILO_FAIL`, `MILO_WARN`,
`MILO_NOTIFY`, `MILO_LOG`, `MILO_ASSERT_FMT` and the `_ONCE` family carry no line
number at all. A comment cannot shift a constant that is written in the source.

`grep -rn __LINE__ src --include='*.h'` finds it only in third-party headers
(curl `memdebug.h`, oggvorbis `misc.h`, stlport `_debug.h`, tomcrypt) and in
`utl/Std.h`, where `container_##__LINE__` is a **token paste** that produces the
identifier `container___LINE__` — not a number.

The full census of `src/system/os/Debug.h`, macro by macro:

| macro | line source |
|---|---|
| `MILO_ASSERT(cond, line)` (`Debug.h:93`) | explicit **argument** — `MILO_ASSERT(ret, 0x19)` |
| `MILO_ASSERT_EXPR` / `MILO_ASSERT_IF` (`:118`, `:148`) | explicit argument |
| `MILO_ASSERT_RANGE` / `_RANGE_EQ` (`:196`, `:200`) | explicit argument (forwarded) |
| `MILO_FAIL` / `MILO_WARN` / `MILO_NOTIFY` / `_BETA` / `MILO_LOG` / `MILO_ASSERT_FMT` / `MILO_PRINT_ONCE` / `MILO_NOTIFY_ONCE` / `MILO_WARN_ONCE` | `__VA_ARGS__` only — **no line number at all** |
| `OBJ_MEM_OVERLOAD(line_num)` (`utl/MemMgr.h:110`) | explicit argument |

`__COUNTER__` appears 0 times in `src/`. A `__LINE__` in a *header* is fixed by
that header's own layout and is immune to any edit in the including `.cpp`.
`src/system/net/curl/lib/ldap.c` and `src/system/stlport/stl/_alloc.c` hold the
only other literal hits, and neither is compiled.

## The real mechanism, stated correctly

Only a **literal `__LINE__` written in a `.cpp`, that survives preprocessing**,
is sensitive. And the function that pays is the one **containing** the expansion,
not "every function below the comment". Inserting N lines above line L changes the
immediate that the containing function loads; functions that merely sit lower in
the file are untouched.

## Whole-binary enumeration (the measurement, not an argument)

Probe: prepend one comment line to **every** one of the 1,188 `.cpp`/`.c` files
under `src/`, full `ninja`, diff `report.json` per function against the same tree
unprobed.

| | |
|---|---|
| files probed | 1,188 |
| report functions compared | 48,365 (symbol-set delta 0) |
| **functions moved** | **1** |
| | `?SampleFree@@YAXPAXPBDH1@Z` 100.0 → 99.888885, 36 B |
| headline | 5,400,816 → 5,400,780 (−0.000316 pp) |

`src/system/synth_xbox/SynthSample.cpp:33` is the only live site:
`PhysicalFreeTracked(mem, __FILE__, __LINE__, "")`. Lines 9–24 of that file are
**16 deliberate blank lines** that pin `__LINE__` to 33 — do not "tidy" them.

**`src/system/net/curl/lib/transfer.c:1034` is a false positive** and the probe
proves it: the token is inside `DEBUGF(infof(...))`, and `DEBUGBUILD` is not
defined, so `setup_once.h:343` expands `DEBUGF(x)` to `do {} WHILE_FALSE`. The
`__LINE__` never reaches codegen. All 18 functions in that unit are at 100.0 and
none moved under the probe. Treat the grep hit as inert unless `DEBUGBUILD`
appears in the build.

## Negative control (that the probe is not just insensitive)

9 comment lines at the top of `src/system/rndobj/Mesh.cpp` (38 assert/notify
sites) and `src/system/obj/Dir.cpp` (30 sites), full `ninja`: **0 functions
moved**, headline identical to the digit. That is the direct disproof of the
assert-shifting story.

## Positive control (that the instrument can fire)

9 comment lines above `SampleAlloc` in `SynthSample.cpp`, full `ninja`:
`?SampleFree@@YAXPAXPBDH1@Z` 100.0 → 99.888885, −36 B, headline 5,400,780.
Reproduces `e0a4e2a67` exactly. Reverting restores 5,400,816 to the digit, three
times over the session — the build is byte-deterministic, so a null result here
is a measurement and not a stale tree.

## Audit of the wave that raised the alarm (`08b8e8ed7..13f4091b5`)

Baseline built **inside the same worktree** at `08b8e8ed7` (main's `report.json`
drifts as lanes land, so it is not usable).

| | |
|---|---|
| `.cpp`/`.c` under `src/` with a line-count-changing hunk | **163** |
| of those, diffs that are purely comment/blank lines | **3** (`math/SHA1.cpp`, `math/mtx.cpp`, `synth_xbox/SynthSample.cpp`) |
| functions sitting below such a change | **2,250** source definitions; the 163 units hold 11,228 report functions / 2,468,788 B |
| **functions that moved because of a line shift** | **0** |
| bytes lost to the class | **0** |

All three comment-only files show **zero** movement on every field, `fuzzy`
included. The wave's 15 real regressions (0 of them crossing out of 100%) all sit
in files with genuine code changes, and 3 are in files with no line-count change
at all — they are content effects, not line effects.

**Non-vacuity controls**, because an empty result and a broken query look the
same: the file query returns 253 changed `src/` files and 163 line-count-changing
`.cpp`/`.c`, not zero; the set is a strict superset of the 152 files found by an
independent `git log --no-merges --numstat` walk, with **0** files in the walk set
missing from it. (A two-point `git diff A..B` was used throughout — `git show
<merge>` returns an empty diff for a merge commit and would have read as a clean
wave.)

## Second, independent audit — relocating comments rather than adding them

A separate lane (worktree at `13f4091b5`, base report built in the same tree)
probed the *inverse* edit over a different wave. Six merges (`239d4da7e`,
`61052f31b`, `a132abde2`, `c6eaa4afc`, `18ff480a9`, `fcb2aebe7`) had inserted **89
comment-only lines across 13 files**; the probe relocated all 89 to end-of-file
*and* deleted the 9 blank lines they came with — a strictly larger line shift than
the comments themselves cause.

**0 of 48,365 functions changed**, on `match_percent_normalized` *and*
`fuzzy_match_percent`; `matched_code` 5,400,816 both ways. 10 of the 12 rebuilt
objects were byte-identical, and the tree restored to the exact base hashes when
reverted. Its positive control is the same one: 9 comment lines above
`SynthSample.cpp:33` move `SampleFree` −36 B and move exactly one function
binary-wide.

## Secondary mechanism: MSVC internal ordinals shift, and cost nothing

The two objects that *did* change bytes with no `__LINE__` anywhere in them:

- `rndobj/Mesh.obj` — the `(section name, size, body, symbolic relocations)`
  multiset is **identical**; 6 of 2,323 sections merely sit at a different index.
  Pure COMDAT renumbering.
- `flow/FlowTrigger.obj` — same, except MSVC's per-TU EH/temp ordinals advanced
  (`__unwind$48720` → `__unwind$48785`, `$T48746` → `$T48811`, `$M4874x` →
  `$M4881x`). Section **bodies are byte-identical**; only the generated names and
  the symbol indices inside relocation records differ.

So a comment move *can* perturb an object without touching one instruction.
Measured cost: zero.

> **Corollary for object-byte A/B testing:** a changed `.obj` hash is not evidence
> that codegen moved. Compare the section multiset with relocations resolved to
> **symbol names**, not indices — a section-order shift renumbers every relocation
> and makes an inert edit look real.

## What to do

Put a cause note wherever it reads best. The only file in the tree where
placement matters is `src/system/synth_xbox/SynthSample.cpp`, whose note is
already correctly parked at end-of-file. If you add a `__LINE__` call site to a
`.cpp`, say so at the top of that file.
