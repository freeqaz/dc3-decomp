# Lane notes: `src/system/utl/BinkIntegration.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ReadFunc@@YAXPAUBINKIO@@_N@Z`

`ReadFunc` -- moved verbatim from `src/system/utl/BinkIntegration.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 352-441):

```text
            // Decrypt the buffer data in-place using XTEA block cipher
            // NEGATIVE RESULT on the 53-instruction r29<->r30 permutation and
            // the residual schedule of the two interleaved 64-bit swaps below:
            // loading the two halves into named locals first
            // (`unsigned long long d0 = block->mData[0]; ... EndianSwap(d0)`)
            // is exactly neutral (89.2 either way).  The formula itself is
            // right -- SwapData in utl/BinStream.cpp inlines the SAME
            // EndianSwap(unsigned long long) at 100% -- so what differs here is
            // only how MSVC reassociates the 8-step recurrence when TWO of them
            // are interleaved, and that follows the register pressure: the
            // image spends one more callee-saved register (r24-r31 vs our
            // r25-r31, the whole frame delta) on the loop.  No source spelling
            // tried reaches it.
            //
            // NEGATIVE RESULT (w7-ap, 2026-09-14, 89.21 canonical): naming the
            // two OUTPUTS instead -- `unsigned long long d0 = EndianSwap(
            // block->mData[0]); ... block->mData[0] = d0;`, on the theory that
            // the image's adjacent `std r11, 0x0(r29)` / `std r10, 0x8(r29)`
            // at 0x82E5D43C-44 means both results are live at once and that is
            // what buys the extra callee-saved register -- costs 1.2pp
            // (89.21 -> 88.0) and widens the r29<->r30 permutation from 53
            // instructions to 79.  Naming the INPUTS was already known to be
            // neutral; naming the outputs is worse than either.
            //
            // w7-bu (89.21 -> 90.8 canonical): the extra callee-saved register
            // IS source-reachable -- swap the STATEMENT order.  With mData[1]
            // swapped first the image's r24 (0x82E5D3DC `rlwinm r24, r10, 0,
            // 8, 15`) appears and the prologue becomes __savegprlr_24 / frame
            // 0xd0 (0x82E5D294/9C), closing the 4 prologue/epilogue rows; the
            // two `std`s still come out in the image's 0x0-then-0x8 order
            // (0x82E5D43C/44).  What remains is the shape of the d0 chain:
            // the image opens BOTH chains with `rldicl rX, rN, 48, 16` +
            // `and` + `or` (0x82E5D374/78 for d0, 0x82E5D380 for d1); ours
            // gives mData[0]'s chain `rldicl r9, r11, 48, 24` + `rldicr r9,
            // r9, 0, 31` whichever statement comes first, and naming the two
            // inputs under this order is 90.6 (worse).
            //
            // w26-r2 (behaviour): the loop is VALUE-IDENTICAL on both sides.
            // Simulating the target's 56-instruction block and our 54
            // instruction block over 500 random 64-bit inputs, with the free
            // registers randomised, gives the correct 64-bit byte reversal in
            // 500/500 cases on BOTH, and the output is invariant across 50
            // different seed sets, so nothing is read uninitialised.  The
            // memory-access multiset over the whole function is identical.
            // The r29/r30 pair is `addi rX, r3, 0x80` (bf->pXTEADecrypter)
            // against a hoisted zero; the image gives the address r30 and the
            // zero r29, ours the reverse -- a callee-saved colour tie, and the
            // prologue is otherwise identical (__savegprlr_24, frame 0xd0).
            //
            // w26-r2 (variants): `BINKIOFILE *const bf` is byte-identical
            // (90.84762 unchanged).  Giving the loop a named `unsigned char
            // *end` cursor instead of recomputing
            // `(unsigned char *)bf->pBufBack + bytesRead` in the condition
            // moves fuzzy UP by 0.98 (88.20 -> 89.18) but moves canonical DOWN
            // by 0.10 (90.848 -> 90.752), so it is not a win and is not taken.
            // There is no unicorn row for this function.
            //
            // w31-b (2026-11-03): the whole two-word byte-swap block was
            // extracted from objdiff's own aligned rows (both sides, the 55
            // instructions between `li r12,0xff` and the two `std`s) and
            // EXECUTED symbolically over 2000 random (d0, d1) pairs with the
            // PPC subset the block uses (ld, rldicl, rldicr, rldimi, rlwinm,
            // clrrwi, and, or, li, addi): the two blocks produce the SAME two
            // 64-bit results on 2000/2000. Two sensitivity controls, each a
            // single-operand edit that changes no register number in a way
            // objdiff would mask, fire: `rldicr r12,r12,48,15` -> `...,56,7`
            // gives 0/2000 and `and r9,r11,r12` -> `and r9,r10,r12` gives
            // 11/2000. So the differently-ordered mask rotations and the
            // `rldicl ...,48,16` vs `rldicl ...,48,24` + `rldicr ...,0,31`
            // substitution are scheduling of the same computation, not a
            // different one. The only remaining instruction delta is the
            // r29/r30 callee-saved pair (bf vs the hoisted zero), which is a
            // colour tie, not arithmetic. Body is 2 instructions SHORT of the
            // image's 210, so nothing here can be banked as a gain either.
            //
            // w31-b: the tap names the tie. `c2rs stage cands` on this TU:
            //   id 6 pri 53 tie 73 ben 21 ref 13 sym 7/k04 -> r30
            //   id 4 pri 50 tie  4 ben 43 ref 22 sym 5/k04 -> r29
            // id 6 is the `int bytesRead = 0` local and id 4 is `bf`; the
            // image needs `bf` (id 4) to pop FIRST, i.e. its priority must
            // reach id 6's. Measured inert on the register (all 88.20476 /
            // 90.84762, unchanged): reading the DoingARead flag through `bf`
            // instead of `bink`, dropping `uBytesRead` and casting at each of
            // the four use sites, and an extra named `File *fp = bf->pFile;`
            // (folded away -- and it would be fabricated anyway). Declaration
            // order is not the lever either: moving `int bytesRead = 0;` up
            // to precede `BINKIOFILE *bf`, inside or outside the guard, costs
            // 1.9 canonical. So this is a priority inversion in the 50-53 band
            // with the two candidates in the same blocks; the §6.2 handle is
            // refs-per-block and no honest spelling moves the count.
```

Inside the body (was lines 465-472):

```text
        // The image loads bytesAvail (0x82E5D4EC `lwz r11, 0x6c(r28)`) before
        // BufHighUsed (0x82E5D4F0), compares `cmplw r11, r10` and then RELOADS
        // bytesAvail for the store (0x82E5D4FC).  Written as one
        // `if (bytesAvail > BufHighUsed)` MSVC loads BufHighUsed first, and
        // `BufHighUsed < bytesAvail` keeps that order and inverts the branch
        // on top.  w7-bu: reading bytesAvail into a local FIRST fixes the
        // load order (that volatile read is sequenced before the other) and
        // the store's own `bink->bytesAvail` read keeps the reload.  3 rows.
```


## `?BinkFileReadFrame@@YAIPAUBINKIO@@IHPAXI@Z`

`BinkFileReadFrame` -- moved verbatim from `src/system/utl/BinkIntegration.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 548-568):

```text
    // w7-bu: bytesReturned is zeroed AFTER the timer read (0x82E5D6D4 bl
    // RADTimerRead, 0x82E5D6D8 mr r23, 0x82E5D6DC `li r27, 0x0`), and blockOff
    // is declared INSIDE the seek branch: its zero is the `mr r28, r27` at
    // 0x82E5D6F4, between the iFileBufPos `beq` and the `ble`, borrowing
    // bytesReturned's register.  Declared before the branch (the old shape)
    // the `mr` sat above both compares.  The seek branch's own
    // `bytesReturned = 0` was redundant and is gone.
    //
    // Still open (99.1 canonical): our `li` lands one slot early, between
    // the bl and the `mr r23, r3`, and the callee-saved colouring is the
    // pair-swap {dest,length}={r25,r24} / {bf,bytesReturned}={r26,r27} in
    // the image vs {r27,r26} / {r25,r24} here (32 rows, one cause).
    // Declaring bytesReturned before bf, and swapping the loop's
    // destPtr/remaining declarations, are both byte-inert.
    //
    // NEGATIVE RESULT: hoisting `bytesReturned` out of this inner block and
    // declaring it AFTER `blockOff` at function scope -- so that blockOff's
    // `li` is the one MSVC creates and bytesReturned shares it -- measured
    // 95.0, i.e. 0.6pp WORSE.  MSVC then keeps blockOff's zero in a
    // callee-saved register across the whole body and our `stw` of the
    // bytesAvail flush picks up r24 rather than a freshly created zero.
```

Inside the body (was lines 608-620):

```text
                // w7-bu: the store order IS source-reachable, just not in
                // source order.  MSVC emits an independent store pair from
                // this struct REVERSED (the second memcpy block below proved
                // it first), so the image's 0x6c/0x98/0x90/0x94 sequence at
                // 0x82E5D780-8C comes from writing bytesAvail LAST and
                // iBufEmpty before it; the two pBuf stores are one value and
                // keep their order.  This also satisfies the volatile rule:
                // BufSize (volatile) is loaded at 0x82E5D778 BEFORE the
                // volatile bytesAvail store at 0x82E5D780, which source order
                // `bytesAvail = 0; iBufEmpty = BufSize;` can never emit.
                // Written the image's way (0x6c, 0x98, 0x90, 0x94) MSVC emits
                // 0x6c, 0x94, 0x90, 0x98 and loads pBuffer before BufSize:
                // 95.6 vs 97.1 here.
```

Inside the body (was lines 627-642):

```text
                    // Align to XTEA block boundary.
                    // w7-bu: iHeaderSize is read ONCE (0x82E5D79C `lwz r9,
                    // 0x28(r26)`, used by both the subf and the add at
                    // 0x82E5D7B8).  Read twice through bf across the pBufPos
                    // store, MSVC reloads it (BINKIOFILE may alias).  blockOff
                    // is assigned from the CSE'd `rawOff & 0xf` AFTER the
                    // pBufPos store: the image computes the mask into r10
                    // (0x82E5D7AC), adds it to pBuffer (0x82E5D7B4) and only
                    // then copies it to its home `mr r28, r10` (0x82E5D7C8)
                    // before the SetNonce call.  Naming blockOff first puts
                    // the clrlwi straight into r28 and drops the `mr`;
                    // assigning it after the call costs an extra callee-saved
                    // register (97.9).  The hdrSize-then-blockOff-then-seekPos
                    // order is what places the `mr` before the seekPos
                    // `addi` (0x82E5D7CC); seekPos before blockOff swaps
                    // those two rows.
```

Inside the body (was lines 662-666):

```text
            // w7-bu: nr keeps its own register (0x82E5D830 `mr r29, r3`) and
            // is copied into bytesReturned only AFTER the short-read test
            // (0x82E5D848 `mr r27, r29`); the counters below add nr, not
            // bytesReturned.  Assigned before the test, MSVC coalesces the
            // two and the `mr` disappears.
```

Inside the body (was lines 700-706):

```text
                        // w7-bu: written pBufPos-then-iBufEmpty so that MSVC
                        // emits iBufEmpty first (0x82E5D938 lwz 0x98, 0x82E5D93C
                        // lwz 0x90, stores 0x82E5D94C/50 in the same order) --
                        // an independent RMW pair on this struct comes out
                        // reversed.  The first memcpy block above is not a
                        // pair (pBufPos = pBuffer is a plain store) and keeps
                        // source order.
```
