# `cap_exhausted_decomp` mining, 2026-08-18 — 0/59 real, and why that is the good outcome

Mined the `cap_exhausted_decomp` class (our side exhausted the 50k instruction cap, the
original didn't) on the theory that it names a loop/termination asymmetry: a real decomp
bug where our code runs away on inputs the shipped code handles. **Signal-to-noise: 0%.**
All 59 rows are one harness defect wearing 59 faces.

## The defect (two stacked)

**Root cause — the harness stubs MSVC's register-save/restore helpers.**
`scripts/unicorn_runner/memory_map.py:24` gives every external REL24 target a
`li r3,0; blr` TRAMPOLINE_STUB. `__savegprlr_N` / `__restgprlr_N` / `__savefpr_N` /
`__restfpr_N` are external REL24 targets, so:

- `bl __savegprlr_29` **zeroes r3 (`this`/arg0) at instruction #2** of the prologue;
- `b __restgprlr_29` is a **tail branch**, and the real helper reloads LR from the frame
  before its `blr`. The stub doesn't — so the epilogue returns to whatever LR holds,
  which after any `bl` in the body is an address *inside* the function.

**Every helper-using function that makes at least one call is an infinite loop under
emulation.** Traced on `DxRnd::D3DFormatForBitmap`:
`+0xB0 → +0x148 → +0x14C → +0x150 → +0xB0 …` (`b 0x148` / `mr r3,r29` /
`addi r1,r1,0x80` / stubbed `b __restgprlr_29`) — four instructions per stubbed call,
which is why call counts scale linearly with the cap.

**Secondary — `cap_exhausted` detection is PC-range-gated.** `engine.py:479` sets
`cap_exhausted` only if PC is inside the *root* function's byte range when the cap fires.
Under co-loading, a side spinning just as hard but sitting in a stub trampoline
(`0x80010000+`) at that instant is recorded `terminated_normally=True` instead. That is
why 52/59 symmetric infinite loops were mislabelled as one-sided.

## Evidence

`scripts/cap_helpers.py` (prototype monkeypatch) rewrites helper sites into the sequence
the original open-codes. Re-running all 59:

| | before | after helper emulation |
|---|---:|---:|
| still `cap_exhausted_decomp` | 59 | **0** |
| EQUIVALENT outright | 0 | **30** |
| reclassified elsewhere | — | 29 (data_layout 6, call_count 6, stack_layout 4, cap_exhausted 3, …) |

The 2 rows still carrying the class (`Rnd::DrawPreClear`, `JsonToDta`) are ORIG_FAULT —
the *original* crashes on the fixture, so it cannot be "the side that terminated".

**Blast radius** (`scripts/cap_blastradius.py`, static reloc scan of the full 1,851-row
2026-08-18 sweep): 97.6% of the 1,041 `cap_exhausted`(both) rows touch a helper, vs a
67.6% base rate among EQUIVALENT rows. Emulation sample of 60 random `cap_exhausted`
rows: **32 become EQUIVALENT, 43 stop hitting the cap**. So **~70% of the DB's largest
divergence class — 1,147 rows across the three cap classes, 62% of the sweep — is this
one defect.** The real fix is owned by `fix/unicorn-helper-stubs`.

## Refuted going in

- **The three `ObjPtrVec<T>::sort` rows share no bug.** `ObjPtrVec<CharClip>::sort<Alphabetically>`
  and `ObjPtrVec<RndDrawable>::merge` are EQUIVALENT once helpers are emulated; the other
  two are `stack_layout`, which the harness's own `_ARTIFACT_CLASSES` already calls
  cosmetic. No MemTracker-shaped comparator win here.
- **The calibration row does not reproduce as this class at all.** `FlowRun::OnTargetDirChange`
  classifies `data_layout`/`call_arg` today, both sides logging identical 12,497 calls. It
  came from a separate 8-row sweep (18:46:24), not the 1,843-row one (19:39:16).
- **The first-pass neutraliser manufactured a false positive.** Keying the rewrite on the
  link bit misread `bl __restfpr_28` (LK=1, but a *restore*) as a save, corrupted the LR
  slot, and produced decomp-only `UC_ERR_EXCEPTION` in exactly the FPR-spilling functions
  — reported as class `error`, the same shape as the real MemDiffEntry win found the same
  day. Fixed by classifying on symbol name. `wild_jump_match` 7→0, `error` 3→1.
  **Any future helper work must classify on symbol, not the link bit.**

## Residual

`docs/analysis/cap-mining-20260818/cap_triage_helpers.json` — 59 rows with per-row
PC/call/verdict at 1× and 10× cap. None
is a loop bug. The 29 still-DIVERGENT rows now sit in other classes and should be
re-ingested only after the harness fix lands.

## One genuine decomp lead found in passing (not behavioral)

`DxRnd::D3DFormatForBitmap` (`src/system/rnddx9/Rnd.cpp:342-344`, 77.1%): the target emits
the second `MILO_ASSERT`'s fail block *unconditionally* (no `cmpwi r31,0xff; bne` guard —
the condition folds to a compile-time constant), and its `MakeString` instantiation takes a
**35-byte** expression array where ours takes 22 (`"fmt != D3DFMT_UNKNOWN"`), plus a
duplicated epilogue. Our assert expression text is wrong.
