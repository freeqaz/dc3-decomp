# A comment is (almost always) free here — the `__LINE__` shift class is structurally closed

**Measured 2026-09-13** in a worktree at `13f4091b5`, base report built in the same tree.

## The claim being corrected

A note in circulation says *"`MILO_ASSERT`, `MILO_FAIL` and the `MILO_NOTIFY` family embed
`__LINE__`, so adding a comment shifts every assert below it in that TU and costs match
percentage on neighbouring functions."*

**The premise is false for this repo.** Read `src/system/os/Debug.h`:

| macro | line source |
|---|---|
| `MILO_ASSERT(cond, line)` (`Debug.h:93`) | explicit **argument** — `MILO_ASSERT(ret, 0x19)` |
| `MILO_ASSERT_EXPR` / `MILO_ASSERT_IF` (`:118`, `:148`) | explicit argument |
| `MILO_ASSERT_RANGE` / `_RANGE_EQ` (`:196`, `:200`) | explicit argument (forwarded) |
| `MILO_FAIL` / `MILO_WARN` / `MILO_NOTIFY` / `_BETA` / `MILO_LOG` / `MILO_ASSERT_FMT` / `MILO_PRINT_ONCE` / `MILO_NOTIFY_ONCE` / `MILO_WARN_ONCE` | `__VA_ARGS__` only — **no line number at all** |
| `OBJ_MEM_OVERLOAD(line_num)` (`utl/MemMgr.h:110`) | explicit argument |

The line number is a hand-written literal *because* the decomp has to reproduce the target's
constants. Nothing in that family reads `__LINE__`, so nothing in it moves when a comment does.

## What the real exposure is, and how small it is

Only a literal `__LINE__` **in the `.cpp` itself, below the comment** can be shifted. A `__LINE__`
in a *header* is fixed by that header's own layout and is immune to any edit in the including
`.cpp`.

> **4 of 1,188 `.cpp`/`.c` files under `src/` contain a literal `__LINE__`. Two are compiled
> into the PPC build.**

- `src/system/synth_xbox/SynthSample.cpp:33` — `PhysicalFreeTracked(mem, __FILE__, __LINE__, "")`.
  The live one. The file already carries an in-source warning and keeps its explanatory note at
  end-of-file, plus 16 blank padding lines (9–24) that pin the immediate.
- `src/system/net/curl/lib/transfer.c:1034` — inside `DEBUGF(infof(...))`. The unit is fully
  matched (9,700 / 9,700 B, 18 / 18 functions), so its current line number is already the correct
  one; do not insert anything above it.
- `src/system/net/curl/lib/ldap.c`, `src/system/stlport/stl/_alloc.c` — not compiled.

`__COUNTER__` appears 0 times in `src/`.

## The audit that established it

Six merges (`239d4da7e`, `61052f31b`, `a132abde2`, `c6eaa4afc`, `18ff480a9`, `fcb2aebe7`) inserted
**89 comment-only lines across 13 files**. Probe: relocate all 89 to end-of-file *and* delete the 9
blank lines they came with — a strictly larger line shift than the comments alone cause — then full
`ninja` and diff `report.json` per function against a base report built in the same tree.

**Result: 0 of 48,365 functions changed, on `match_percent_normalized` *and* `fuzzy_match_percent`.**
`matched_code` 5,400,816 both ways. 10 of the 12 rebuilt objects were byte-identical; the tree
restored to the exact base hashes when reverted.

### Positive control (the probe can fail)

A 9-line comment inserted *above* `SynthSample.cpp:33` moved
`?SampleFree@@YAXPAXPBDH1@Z` **100.0 → 99.888885, −36 B** — and moved **exactly one** function
binary-wide. The instrument is sharp and has no background noise.

## Secondary mechanism: MSVC internal ordinals shift, and cost nothing

Two objects did change bytes with no `__LINE__` anywhere in them:

- `rndobj/Mesh.obj` — the `(section name, size, body, symbolic relocations)` multiset is
  **identical**; 6 of 2,323 sections merely sit at a different index. Pure COMDAT renumbering.
- `flow/FlowTrigger.obj` — same, except MSVC's per-TU EH/temp ordinals advanced
  (`__unwind$48720` → `__unwind$48785`, `$T48746` → `$T48811`, `$M4874x` → `$M4881x`). Section
  **bodies are byte-identical**; only the generated names and the symbol indices inside relocation
  records differ.

So a comment move *can* perturb an object without touching one instruction. Measured cost: zero.

> **Corollary for object-byte A/B testing:** a changed `.obj` hash is not evidence that codegen
> moved. Compare the section multiset with relocations resolved to **symbol names**, not indices —
> a section-order shift renumbers every relocation and makes an inert edit look real.

## Rule

Put a note where it explains the code. The only two files where placement matters are named above;
in `SynthSample.cpp` keep notes at end-of-file. Do not relocate comments elsewhere for safety — that
is churn, and this audit is the measurement that says so.
