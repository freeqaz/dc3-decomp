# Lane notes: `src/system/hamobj/DancerSequence.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Load@DancerSequence@@UAAXAAVBinStream@@@Z`

`DancerSequence::Load` -- moved verbatim from `src/system/hamobj/DancerSequence.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Inside the body (was lines 78-104):

```text
        // RESIDUAL (w7-ba, 99.96 canonical, 11 rows, all stack-slot addi operands).
        // The 91.4 floor recorded here by w7-ak was two things, both now closed:
        // (a) the Vector3 reads were chained on `d` (BinStreamRev's member
        //     template re-materialises r3 per call) -- see the `bs >>` chains
        //     below; and (b) the ms selection.  The image lets the `d.rev < 2`
        //     arm fall through into ONE `stw ms` (8249B740) and straight into
        //     SetDisplacementElapsedMs (8249B744), with the rev>=5 ReadEndian arm
        //     out of line at 8249B7C8 jumping back to the call.  Hoisting
        //     `int ms = -1` above the chain (91.9), or `ms = -1` in every arm
        //     before/after `skeletonRev = N` (90.5 / 91.1), never merged.  What
        //     does: leave the chain pure, then `if (skeletonRev >= 4) d >> ms;
        //     else ms = -1;` -- MSVC jump-threads the test through the constant
        //     arms.  The polarity matters: `if (skeletonRev < 4) ms = -1; else
        //     d >> ms;` reproduces the shared stw but lays the ReadEndian arm
        //     inline and branches forward to the call (91.6).
        // CLOSED by w21-at (key storage float[3]); the w7-ba analysis below
        // is kept for the record.
        // What was left: the nine Vector3-sized slots 0xb0..0x130 are the same
        // SET on both sides, permuted (image pos(rev<7)=0xc0 disp(rev>=7)=0xd0
        // pos(rev>=7)=0xe0 disp(rev<7)=0xf0 Key=0x100 v1=0x110 v=0x120; ours
        // disp(rev<7)=0xc0 pos(rev>=7)=0xd0 pos(rev<7)=0xe0 disp(rev>=7)=0xf0
        // v1=0x100 v=0x110 Key=0x120).  Four spellings were byte-identical on
        // this: swapping `pos, disp` to `disp, pos`; hoisting pos/disp out of
        // the `dataIdx == 0` arm; renaming the rev>=7 pair; hoisting the key
        // storage above the dataIdx loop.  Address-taken locals do not share
        // slots here, and neither name, declaration order nor scope moves the
        // order MSVC gives them.
```

Inside the body (was lines 187-197):

```text
                    // The storage is three floats -- Vector3-sized, which is
                    // what the image's slot is: 0x100 sits inside the 16-byte
                    // Vector3 block (0xf0 disp, 0x110 v1).  A two-float array
                    // lets MSVC pack the slot and shrinks the frame by 0x10
                    // against the image's 0x490; a four-float array (w7-ba,
                    // 99.96) keeps the frame but MSVC orders a 16-byte local
                    // differently from the 12-byte Vector3s, which permuted
                    // seven slots (key last instead of first, and both
                    // pos/disp pairs).  w21-at: float[3] -> 100, all 313 rows
                    // equal under name_check.  Key<float> is 8 bytes, so the
                    // read stays inside the storage on PPC and native alike.
```
