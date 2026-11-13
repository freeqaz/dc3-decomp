# Lane notes: `src/system/rndobj/Console.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?MoveLevel@RndConsole@@QAAXH@Z`

`RndConsole::MoveLevel` -- moved verbatim from `src/system/rndobj/Console.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 289-340):

```text
        // RESIDUAL (w9-c 2026-09-30): 88.469 canonical / 88.265 fuzzy, 8 rows of
        // 50.  ONE cause, in two halves: the image treats mLevel as MEMORY on both
        // reads after the `+=` store, and MSVC forwards the stored value to us
        // instead.
        //   - Clamp's `value` argument: the image emits `add` + `stw` +
        //     `clrrwi r11, r11, 0` + `cmpwi cr6, r11, 0` -- the clrrwi is
        //     `rlwinm r11,r11,0,0,31`, the zero-extension a 32-bit `lwz` would
        //     have produced, left behind by a store-to-load forward.  We propagate
        //     the sum straight into the compare, which lets MSVC fuse it as
        //     `add.` and branch on CR0, costing rows 10/13/17/19.
        //   - the array index: the image re-reads `lwz r10, 0x5c(r3)` after
        //     storing the clamped value (rows 27-29), loading gCallStackPtr first;
        //     we keep the clamped value in a register and compute `mLevel - 2`
        //     from it before the store.
        // The single `stw` at the branch join proves the image assigns the result
        // of one expression (the Clamp/ternary form kept below), not an in-place
        // if/else-if.
        //
        // Ten spellings measured, one full ninja each:
        //   88.469 (inert): the `limit` local hoisted ahead of the Clamp; the `+=`
        //     folded into the Clamp argument (`Clamp(lo, 0, mLevel += level)`);
        //     `(int)mLevel` cast on the value; fully `this->`-qualified; the Clamp
        //     result via an intermediate local; the ternary written out by hand;
        //     all three accesses through `int *lvl = &mLevel` (88.061 fuzzy) or
        //     `int &lvl = mLevel` (88.061 fuzzy)
        //   75.816 (much WORSE): in-place if/else-if with the limit expression
        //     repeated in the body rather than hoisted
        //   90.510 (HIGHER but structurally wrong -- do not adopt): in-place
        //     if/else-if over a hoisted `limit` local.  That form DOES buy the
        //     image's memory reload of mLevel for the array index, which is the
        //     useful finding here: the reload appears exactly when the stored
        //     value arrives from a control-flow join MSVC cannot forward.  It pays
        //     for it with a SECOND `stw 0x5c(r3)` inside the `mLevel = 0` arm,
        //     which the image does not have, plus an r8/r10 allocation shift.
        // Getting both halves at once needs one tail-merged store AND a
        // non-forwardable load, which are the same condition pulling opposite
        // ways; on the evidence this is MSVC's copy-propagation cleaning up an
        // inlined by-value template parameter that the image's build left in
        // place.  Faithful Clamp form kept.
        //
        // w34-a (2026-11-04, behaviour verdict): AGREES, and the forwarding that
        // costs these rows is safe here.  Both sides add level into mLevel,
        // store it at 0x5c, test it against zero, clamp against
        // gCallStack - gCallStackPtr + 2, store the clamped value, and load
        // gCallStackPtr[mLevel - 2] into 0x58.  The image reloads mLevel from
        // 0x5c where we forward the register, and no store or call lies between
        // that store and the read, so both produce the same index.  The image's
        // `add` + dead `clrrwi` + separate `cmpwi cr6` is our fused `add.` with
        // the CR0 branch: the same value tested against the same zero.
        // Mechanism hypothesis: copy propagation drops the inlined Clamp
        // argument's home slot in our build and keeps it in the original's,
        // which is also what decides whether the index re-reads memory.
```
