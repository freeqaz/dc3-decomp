# Lane notes: `src/xdk/nuiapi/nuidetroit.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?NuipCameraAdjustTilt@@YAKIMMMPAU_NUI_TILT_OBJECTS@@PAU_XOVERLAPPED@@@Z`

`NuipCameraAdjustTilt` -- moved verbatim from `src/xdk/nuiapi/nuidetroit.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 40b6979b6)

Inside the body (was lines 292-303):

```text
        // CLOSED (w7-bq, 2026-09-15): 0xb84 computes &LocalOverlapped ONCE
        // (`addi r11, r1, 0x60`) for both the hEvent store and the
        // runtime-state store; we emitted the addi twice, into r10 and r11.
        // w7-al had tried three spellings that all KEEP a `pRequest` local
        // (separate assignment, chained assignment, and writing hEvent through
        // the pointer) -- the first two byte-inert, the third a 6-register
        // renumbering -- and concluded the duplicate was a backend choice.  It
        // was not: the local itself was the duplicate.  `pRequest` is always
        // exactly `NuipDetroitRuntimeState.pOverlapped` on every path, which is
        // why the image RELOADS it at 0xba4 (`lwz r11, 0x1f0(r31)`) instead of
        // keeping a register.  Deleting the local and writing through the
        // runtime state removed the dead `addi`: 96.45 -> 97.04.
```

Inside the body (was lines 368-410):

```text
    // ONE `return dwResult`.  The image's two epilogues (0xd34 `mr r3, r31`
    // and 0xd48 `mr r3, r30`) are MSVC duplicating the epilogue for the phi
    // rather than materialising a common register -- which is also why
    // dwResult survives in the callee-saved r30 (`li r30, 0x3e5` at 0xbd4)
    // instead of being rematerialised as `li r3, 0x3e5` at an early return.
    // NEGATIVE RESULT (w7-al, 2026-09-14): the image's two epilogues return two
    // DIFFERENT registers (0x829C4914 `mr r3, r31` for the waited-on
    // LocalOverlapped.InternalLow, 0x829C4928 `mr r3, r30` for the pending
    // 0x3e5), which reads as two returns of two separate locals.  Spelling it
    // that way -- `if (pOverlapped != 0) return dwResult;` followed by a second
    // local returned at the bottom -- costs 96.4 -> 90.1: the early return
    // lengthens dwResult's live range past the whole tilt-state chain and
    // renumbers r28/r29 through it (12 register rows), and MSVC still folds the
    // two epilogues back into one.  The single `return dwResult` is kept.
    //
    // w7-bq, 2026-09-15: three more spellings, all 97.04 -> 95.53, all with the
    // SAME 20-row signature (r28<->r29 across 8 rows, `li r30, 0x3e5` at 0xbd4
    // deleted, the fmuls/rlwinm pair at 0xbc4 reordered).  (a) block-scoped
    // second local `DWORD dwWaitedResult = LocalOverlapped.InternalLow;`
    // returned from inside the if; (b) the same local hoisted to function scope
    // with the other declarations, in case declaration order pinned it to r31;
    // (c) no second local at all -- just an explicit `return dwResult;` as the
    // last statement of the if, so both returns name ONE variable.  It is the
    // RETURN STATEMENT inside the guarded block that costs it, not the extra
    // variable: any of them ends dwResult's live range early and MSVC then
    // declines to give it a callee-saved home at all.  Residual is 5 rows
    // (the second epilogue) plus 2 register rows; nothing else is charged.
    //
    // LEVER (w7-br, 97.04 -> 97.6 canonical, 8 -> 5 rows): the waited-on
    // result IS a second local, but it is COPIED into dwResult after the
    // CloseHandle rather than returned.  That reproduces the image's
    // `lwz r31, 0x60(r1)` at 0x829C48FC (r31 is free once the INFINITE
    // constant dies) and its `mr r3, r31` at 0x829C4914 exactly; what is left
    // is how the phi is resolved.  The image tail-duplicates the return block
    // (second epilogue at 0x829C4928 `mr r3, r30`, the target of the `bne` at
    // 0x829C48D8) where we emit one copy `mr r30, r31` and a single epilogue.
    // NEGATIVE (w7-br): `if (pOverlapped != 0) return dwResult;` ahead of the
    // wait with ONE variable and no second local, 90.7 -- the same
    // `li r30, 0x3e5`-deleted / r28<->r29 signature as w7-al's and w7-bq's
    // early returns.  Every spelling with a return inside or before the guarded
    // block constant-propagates the pending 0x3e5 into that return; the image
    // does not, so both of its returns are one statement reached through a
    // join, and the epilogue duplication is MSVC's phi resolution.
```
