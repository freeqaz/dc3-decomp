# Emulating MSVC's register save/restore helpers — A/B, 2026-08-19

Branch `fix/unicorn-helper-stubs`. Harness only; `src/` and `include/` untouched.
The live `decomp.db` was **not** written — re-ingest is the orchestrator's step on
main (`scripts/unicorn/apply_refresh.py --apply`).

## The defect

`memory_map.TRAMPOLINE_STUB` (`li r3,0; blr`) was given to every external REL24
target. Xenon MSVC's out-of-line prologue/epilogue helpers — `__savegprlr_N`,
`__restgprlr_N`, `__savefpr_N`, `__restfpr_N`, `__savevmx_N`, `__restvmx_N` — are
external REL24 targets, so:

1. `bl __savegprlr_N` ran `li r3,0`, destroying `this`/arg0 at the **second
   instruction of the function** on both sides;
2. `b __restgprlr_N` is a **tail** branch and the real helper is what reloads LR
   from `-0x8(r1)`. The stub's `blr` returned to whatever LR held — the address
   just past the last stubbed call — so the epilogue re-entered the body.

Net: every helper-using function that made a call was an infinite loop, and its
recorded verdict described the spin. **1,609 of the 1,838 swept functions (87.5%)
have a helper on both sides.**

Secondary: `cap_exhausted` was set only when PC sat inside the **root** function's
byte range at the instant the cap fired, so a side spinning just as hard but caught
in a trampoline was recorded `terminated_normally=True`.

## The fix

`scripts/unicorn_runner/save_helpers.py` installs the helpers' **real bodies** at
fixed addresses in a new HELPER region (`0x80040000`, one unmapped 64KB guard
above RDATA), and
`patcher.assign_addresses` points the relocation there instead of at a stub. The
code under test is not edited. All 72 GPR/FPR entry-point bodies are asserted
byte-for-byte equal to the shipped image at their `symbols.txt` addresses;
`std`/`ld` are lowered by the same `rewrite_ppc64_insns` that already lowers every
function body. VMX is the one documented approximation (no vector state is
modelled; only r11's final `-0x10` is reproduced).

Two earlier prototypes (`scripts/cap_helpers.py`,
`scripts/unicorn_runner/prologue_helper_probe.py`) rewrote the **call sites**
instead. That restores control flow but drops the r14-r31 / f14-f31 spill, which
is wrong under co-loading: a co-loaded callee's spill becomes a `nop`, so it keeps
the caller's r29 on return. `prologue_helper_probe.py` is removed (its `--plain`
A/B no longer means anything now that production emulates the helpers) and
`cap_helpers.install()` is marked superseded.

Classification is by **symbol name**, never the link bit: MSVC emits
`bl __restfpr_28` (LK=1) immediately before `b __restgprlr_26` (LK=0), and the
first prototype read the FPR restore as a save, corrupted the LR slot and
manufactured `error`-class divergences in exactly the FPR-spilling functions.

## A/B: full frontier sweep, same box, same day

`refresh_frontier.py --run -j 8`, 455 units, 1,838 functions, both trees at
`5f3c79c17` for source. Base = main (`5f3c79c17`), fix = `acc6866bc`.
Wall clock **46.6 s → 22.2 s** — the sweep was spending half its time spinning.

| verdict | class | base | fix | delta |
|---|---|---:|---:|---:|
| DIVERGENT | cap_exhausted | 1037 | 328 | **−709** |
| EQUIVALENT | — | 472 | 898 | **+426** |
| DIVERGENT | data_layout | 126 | 420 | +294 |
| DIVERGENT | cap_exhausted_decomp | 58 | 3 | −55 |
| DIVERGENT | wild_jump_match | 52 | 46 | −6 |
| DIVERGENT | cap_exhausted_orig | 46 | 4 | −42 |
| DIVERGENT | stack_layout | 25 | 81 | +56 |
| DIVERGENT | call_count | 12 | 20 | +8 |
| DIVERGENT | call_arg | 1 | 8 | +7 |
| DIVERGENT | merged_call | 2 | 7 | +5 |
| DIVERGENT | return_value | 0 | 5 | +5 |
| DIVERGENT | orig_error | 3 | 5 | +2 |
| DIVERGENT | unmapped_access_mismatch | 1 | 3 | +2 |
| DIVERGENT | object_memory | 0 | 2 | +2 |
| DIVERGENT | regalloc | 0 | 2 | +2 |
| DIVERGENT | build_env | 1 | 2 | +1 |
| DIVERGENT | merged_arg | 0 | 1 | +1 |
| DIVERGENT | error | 0 | 1 | +1 |
| SKIPPED | — | 2 | 2 | 0 |

DIVERGENT 1364 → 938. Flips: **473 DIVERGENT→EQUIVALENT, 47 EQUIVALENT→DIVERGENT.**

Artifact rate per base class (rows whose verdict-or-class changed):

| base class | n | changed | % | → EQUIVALENT |
|---|---:|---:|---:|---:|
| cap_exhausted_orig | 46 | 45 | 97.8% | 16 |
| cap_exhausted_decomp | 58 | 56 | 96.6% | 29 |
| call_count | 12 | 10 | 83.3% | 6 |
| cap_exhausted | 1037 | 749 | 72.2% | 409 |
| wild_jump_match | 52 | 12 | 23.1% | 7 |
| stack_layout | 25 | 5 | 20.0% | 2 |
| data_layout | 126 | 24 | 19.0% | 4 |

`call_arg` and `merged_call` changed 100%, on 1 and 2 rows.

## Every EQUIVALENT→DIVERGENT flip, explained

All **47** are functions where **both** sides call the helpers. Zero of the 214
helper-free functions flipped EQUIVALENT→DIVERGENT (none changed verdict at all in
that direction).

Re-running all 47 under the **base** harness at 50k and 500k caps: **not one of
them reached the return sentinel on either side.** The old EQUIVALENT was vacuous
in all 47 cases, by one of three mechanisms:

* **7 rows — symmetric spin parked in a trampoline.** Work scales linearly with
  the cap (`??1HamNavList` 12,405 → 50,000 calls; `?PushRoutineBuilderClip`
  16,661 → 50,000; `??$_M_range_insert_realloc` 4,998 → 49,998). Both sides were
  `cap_exhausted` in fact, but PC was in a stub at that instant, so the PC-gated
  check recorded `terminated_normally=True` on both and the identical garbage
  state compared equal.
* **10 rows — both sides wild-jumped to a low address (0x00000004) and crashed identically**
  (`?SynthPoll@Sound@@` : "both sides hit identical error: UC_ERR_EXCEPTION").
  EQUIVALENT only via the comparator's "matching error at matching PC" rule.
* **30 rows — both sides ran off the end of the code buffer** after the stubbed
  epilogue returned into the body (`?Copy@RndEnviron@@` : "both sides hit
  identical error: UC_ERR_MAP"), stopping at the `emu_start` terminator.

So the 47 are unmaskings, not new divergences: functions that never executed their
body are now executing it and disagreeing. Nine land in `cap_exhausted_both`,
which is the honest label for "both sides truncated"; the other 38 now carry a
concrete reason (`call_arg_mismatch` in 31 of them, mostly `data_layout` /
`stack_layout` — differing frame or data-section addresses).

## Predictions reproduced

The six rows `fix/pinpoint-divergences-a` predicted would flip DIVERGENT→EQUIVALENT
all do, with zero memory diffs and identical return values:
`?AdjustSaturation@RndColorXfm@@`, `?Poll@CharDriver@@`, `?Poll@CharSignalApplier@@`,
`?SetCrewPhotoPlayerCenters@StreamRenderer@@`, `?SetDiskError@PlatformMgr@@`,
`?OnSync@RndMesh@@`. `?D3DFormatForBitmap@DxRnd@@` stops looping (2 vs 4 calls
instead of a 4-instruction cycle). `?IsValidSwipePosition@...@@`'s
16,616-iteration spin becomes a plain stack-address difference. `this` is
`OBJECT_BASE` again instead of 0, so field-access maps stop degenerating to
`READ 0x000`.

## What is left

335 rows are still in a cap class (319 of them helper-using). The sample is
dominated by STL `_M_insert_overflow_aux` / `__uninitialized_copy`: vector-growth
loops whose trip count comes from uninitialised fixture memory (0xCD fill). That is
a separate, known fixture artifact — a loop bound read from fill — not this defect.

**Out of scope, noted:** the trampoline stub still returns 0 where a constructor
returns `this`, which produces bogus `unmapped_access_mismatch` rows. The helper
work makes that *easier* to fix, not harder: the HELPER region establishes the
pattern of "give this external symbol a real body at a fixed address outside the
call-logged region", and a ctor-returns-`this` stub (`blr` with r3 untouched)
would be the same mechanism keyed on a different symbol predicate.

## Reproduce

```sh
python3 scripts/unicorn/refresh_frontier.py --run -j 8 \
    --out-db /tmp/helper-fix/refresh.db --json /tmp/helper-fix/refresh.json
python3 -m pytest scripts/unicorn_runner/tests/      # 199 passed, 15 skipped
```

Unicorn needs RWX `mmap`, so run unsandboxed.
