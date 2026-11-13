# Lane notes: `src/system/synth/WavReader.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@WavReader@@UAAXM@Z`

`WavReader::Poll` -- moved verbatim from `src/system/synth/WavReader.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 101-121):

```text
                // w25-gj stop note: the image emits both stores below as sthx (index,
                // base); we emit (base, index), while the mono loop above is (base,
                // index) in both builds. c2's commutative-operand sort (C2RS-BRIDGE 8.7,
                // cqlo3) puts the &mInputBuffers[k] address add as M6[base V338 or V342]
                // key 0x18008 (sid 2 mod 4) ahead of the strength-reduced index temp V492
                // (0x17b00); the mono loop's index V475 (0x176c0) loses to the same V338.
                // One shared V338 cannot sit on both sides, so the image either has a
                // separate base temp here or index temp sids past 512 (21+ more temps).
                // Open: what splits the CSE of this+0x28 across the two loops. Inert:
                // pointer arithmetic, (*mInputBuffers)[i], named out pointers,
                // EndianSwap/SwapBytes.
                // w26-f12: four more spellings measured on the same two rows, all
                // inert at 99.868 -- `*(mRawInputBuffer + i * 2)` reads,
                // `*(mInputBuffers[k] + i)` stores, a named `int j = i * 2`, and
                // hoisting the doubled index.  Reading both halves before storing
                // either changes the code (89.662).  Worth recording: the mono loop's
                // store is (base, index) in BOTH builds (`sthx r9, r8, r11`), while
                // the stereo loop's is (index, base) only in the image, so what is
                // being re-keyed is the base/index pair inside the indexed operand,
                // which is not the 8.7 use-list sort and did not follow any of the
                // spellings that move a use-list key.
```
