# Lane notes: `src/system/utl/Cache_Xbox.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ThreadGetFileSize@CacheXbox@@IAAHXZ`

`CacheXbox::ThreadGetFileSize` -- moved verbatim from `src/system/utl/Cache_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 333-369):

```text
        // The slot half of the 2026-09-13 note is now FIXED, by hoisting `err`
        // to function scope (see above) rather than by reordering the two
        // declarations in this block -- a reorder here really is inert, because
        // `err` has no initialiser and MSVC places it at first use, which is
        // after `&fileSize`.  Both stack rows (r1+0x50 / r1+0x54) are equal now.
        //
        // The block half is now fixed too, and the w7-ap negative that said it
        // could not be is SUPERSEDED -- it was a negative about `&&`/`||`, not
        // about block placement.  The image lays the store block out FIRST as
        // the fall-through (0x827FF420), with `bne .L_827FF42C` steering
        // err != 0 forward to the notify.  Every spelling built out of a single
        // short-circuit expression measures byte-identical, in EITHER polarity:
        // MSVC applies De Morgan to `if (a || b) B else A` and re-emits it as
        // `if (!a && !b) A else B`, so the notify block lands first no matter
        // which arm you write first.  Only splitting the condition into two
        // separate `if`s with explicit gotos, so the two blocks are two
        // independent layout items rather than the arms of one expression,
        // survives that rewrite.  87.43 -> 95.83.
        //
        // The 95.83 residual is a 3-cycle of callee-saved registers, and it is
        // NOT a spelling: the image holds this=r30, file=r28, ret=r29, we hold
        // this=r29, file=r30, ret=r28.  The scheduling row that used to sit
        // next to it (the `lwz 0x1c` hoisted above `mr r31, r3`) is closed by
        // reading the device id through ContentData() in the CreateFile arm
        // (w12-c): 100 modulo register permutation.
        //
        // w26-f2: that 3-cycle is what is left, and the tap says the image's
        // colouring wants err > this > ret > file, which is not ours.  Ours
        // (priority, tie, candidate id from `c2rs stage cands --regasg`):
        // err(9, 46, id7)->r31, file(6, 3, id3)->r30, this(4, 13, id2)->r29,
        // ret(2, 61, id6)->r28 -- err already agrees, the other three are the
        // cycle.  Target needs this at +3..+5 over its current 4 and ret at
        // +4 over its 2, with file's 6 unchanged; nothing tried moves those two
        // without moving file as well.  Measured inert or worse: hoisting
        // `ret`'s declaration to function scope (92.49-92.83 -- it also
        // re-places the stack slot the note above depends on) and hoisting
        // `fileSize` (92.83).  Fuzzy 99.236115, canonical 100.0.
```
