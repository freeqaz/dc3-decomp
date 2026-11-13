# Lane notes: `src/lazer/meta_ham/NavListSort.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetHighlightID@NavListSort@@QAA_NPAVDataArray@@@Z`

`NavListSort::SetHighlightID` -- moved verbatim from `src/lazer/meta_ham/NavListSort.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 95-173):

```text
// NOTE (w7-ai): residual at 89.8%.  Two items, neither source-reachable from
// here.  (1) Slot colouring: retail's first Symbol temp sits at 0x58(r1) and
// ours at 0x54, shifting ten rows by -4, because retail reserves 0x54 for an
// int that only lives in the deep else arm; our build also spills one extra
// 4-byte slot at 0x6c.  Collapsing token/token1/token2/token3 into a single
// reused Symbol -- the obvious way to drop a slot -- costs 0.4pp and adds a
// 0x10 frame delta (measured 2026-09-14), so the four named locals stand.
// NEGATIVE RESULT (w7-bq, 2026-09-15), a second attack on that same slot:
// eliminating all four named temps outright -- `NodeFind(a->Sym(0..3))`
// constructed inline at each of the five find_if call sites, so no Symbol
// local exists at all -- reads **80.7** canonical (89 mismatch rows, 14
// inserts / 13 deletes) against the 89.8 of the named form, with the same
// frame delta of -0x10 the collapse experiment produced.  Removing the
// temps does not remove the slots: MSVC still materialises a Symbol
// home slot per call site for the by-value NodeFind member, and losing the
// names costs it the r25/r28 colouring as well (REGISTER_SWAP grows to 20
// instructions across 5 pairs, from 3 pairs in the named form).  The four
// named locals stand.
// (2) Retail materialises `li r3,0` at the aSize==0 site and branches to the
// epilogue, and duplicates the epilogue inline at the aSize==1 success site;
// our build funnels every return through one shared tail.  That is MSVC's
// tail-merge choice, not a spelling.
// The MakeString rows in the Function Call Diff are an ICF fold, not a bug:
// both sides pass the SAME ??_C@ string data (NavListSort.cpp [16] and the two
// condition strings [0x22]/[0x20]); only the instantiation name differs, and
// MakeString's body is identical for every array-size triple.
//
// LEVER (w7-br, 89.8 -> 93.6 canonical, 55 -> 18 rows, every stack offset now
// equal).  The slot map was readable from the image once the else arm was
// looked at as a whole: 0x54 is written by `addi r3, r1, 0x54` (Sym(1), at
// 0x82977200) and then by `stw r6, 0x54(r1)` at 0x829772C0 and 0x82977314 --
// the Sym(2)/Sym(3) results come back in the 0x64 temp (`addi r3, r1, 0x64`,
// then `lwz r6, 0x0(r3)`) and are COPIED into 0x54.  That is one Symbol
// variable initialised from Sym(1) and ASSIGNED from Sym(2) and Sym(3), so
// `token1` is reused and `it` with it (all three list results land at 0x5c).
// The other half is that the image keeps two DIFFERENT slots for the two
// Sym(0) tokens, 0x58 in the aSize==1 arm and 0x68 for the multi-token path:
// with `if (aSize == 1) { ... } else { ... }` MSVC colours the two sibling
// block-scoped tokens into one slot (93.4, frame 0xb0 vs 0xc0); with the
// multi-token path written at FUNCTION scope after the aSize==1 early return,
// the outer `token` cannot share with the nested one and the frame and every
// slot match (93.6).  w7-ai's "collapse all four" (89.4) collapsed the
// Sym(0) token too, which the image does not do.
// NEGATIVES (w7-br): inlining `NodeFind(a->Sym(0))` in the aSize==1 arm only,
// 91.5 (the temp comes back through `mr r10, r3; lwz r6, 0(r10)`, the image
// loads 0x58 directly -- it is a named local); `if (aSize == 0) return false;
// else if (aSize == 1)`, byte-inert; the multi-token path written FIRST under
// `if (aSize != 1)` with the single-token path last, 78.2 (whole layout
// inverts).
// RESIDUAL (w7-br, 18 rows), all block placement of the two shared tails:
// the image keeps the aSize==0 `return false` INLINE as the fall-through of
// `bne .L_82977108` (0x829770FC; `li r3, 0` / `b .L_82977160` at
// 0x82977100) and every not-found path jumps back up to it, while the ONE
// epilogue at 0x82977160 belongs to the aSize==1 success arm (`stw r11,
// 0x50(r28)` at 0x82977158 falls into it); we invert to `beq`, sink the
// return-false block to the end and hang the shared epilogue off it, so the
// aSize==1 success is `li r3, 1; stw; b` instead of `stw; li r3, 1;
// epilogue`.  The `mr r4, r30` at 0x82977110 the image hoists above the
// aSize==1 test is a copy of an argument register that still holds `a`; we
// use r4 as-is.  Both are MSVC's tail-merge/canonical-return choice, not a
// spelling; the ggIt tail's `b .L_8297714C` (0x82977328) into the aSize==1
// compare is reproduced.
// w14-e (93.6, unchanged): routing every not-found exit through a
// `notFound:` label inside the aSize==0 arm (so the image's inline
// `li r3, 0; b epilogue` block exists in source) is byte-identical -- MSVC
// re-sinks the shared return-false block regardless.
// w18-e: `switch (aSize) { case 0: return false; case 1: {...} }` is
// much worse (66.6, the dispatch becomes cmplwi/blt).  Note the image's
// `cmpwi r24, 0x0` is a cr0 compare.
// w20-s (93.6): rows 113/134 land on the image's inline `stw r11,0x50(r28);
// li r3,1; epilogue` block vs our `li r3,1; stw; b epilogue` -- same store, same
// return; epilogue tail-duplication only.  Every return-false edge lands on
// `li r3,0` on both sides.  ARTIFACT, no behaviour difference.
// w21-r (93.6 -> 100, 151/151 equal): the two tails are MSVC tail merges and
// each merged copy lands where the LAST-written copy of that tail sits.  The
// aSize==1 arm returns true INSIDE a positive `if (it != end)` and false
// after it; the four-token path ends `if (it == end) return false;` then the
// store and `return true`.  Same behaviour as before (every not-found path
// returns false, every found path stores the found node and returns true).
```


## `?ChangeHighlightHeader@NavListSort@@QAAXH@Z`

`NavListSort::ChangeHighlightHeader` -- moved verbatim from `src/lazer/meta_ham/NavListSort.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 260-264):

```text
    // w14-e: the image only wraps the index on the two arms that actually move
    // it (`addi r10, r31, 1; b` / `subi r10, r31, 1` falling into the inlined
    // Mod at 0x8297689C); every path that decides not to move branches past the
    // Mod straight into the IsActive scan.  The old unconditional
    // `nextIdx = Mod(nextIdx, size)` also wrapped an unmoved index (94.1).
```
