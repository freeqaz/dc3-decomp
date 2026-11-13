# Lane notes: `src/system/hamobj/MoveGraph.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?FindVariantPair@MoveGraph@@QBA_NAAPBVMoveVariant@@0PBVMoveParent@@1PBV2@2VSymbol@@_N@Z`

`MoveGraph::FindVariantPair` -- moved verbatim from `src/system/hamobj/MoveGraph.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 262-316):

```text
        // Only p1 set
        // Nothing here is cached in a local: the image re-derives the vector
        // every time.  `vref1 = *variants.begin()` is two loads at 0x824F931C
        // (`lwz r10, 0x0(r11)` then `lwz r10, 0x0(r10)`), and the indexed read
        // reloads _M_start at the TOP OF THE LOOP each iteration
        // (`lwz r9, 0x0(r11)` at .L_824F9354).  A cached `vbegin` collapses
        // both.  The hit also `break`s rather than returning: 0x824F937C
        // branches to the same `li r3, 0x1` at .L_824F9314 the fall-through
        // and the s.Null() and empty-size exits all reach.
        // w21-r (still 98.10; 4 delete rows = the two begin reloads per
        // block, rest is volatile-register renaming): behaviour re-checked
        // (same exits, same stores).  Measured: `i < variants.size()` as the
        // loop condition (no count local) byte-identical; front() plus an
        // element local in the loop identical; both single-parent blocks
        // through one shared static inline helper 97.1 (worse, and the CSE of
        // the begin load is unchanged).
        // w14-b (still 98.10): all inert or worse -- no local (every use via
        // p1->Variants()), a vector pointer, front()/[0], size()==0 or
        // begin()==end() for the empty test, empty() via p1 then the local.
        // w21-au (still 98.10): `s == variants[i]->Song()` and a `Symbol
        // song` local are inert; int count/index 97.5.  Same no-CSE/no-LICM
        // reload of _M_start as HollaBackMinigame::SetMoveState (see the
        // w21-au note there: splitting member/reference spellings breaks the
        // CSE but MSVC then hoists the load).  Stopped.
        // w27-e (98.103 canonical, unchanged by 3 more spellings): mechanism --
        // the reload at the loop top exists because the loop calls `Song()`, so
        // the image re-reads `_M_start` after it.  Our c2 hoists that load OUT of
        // the loop (and keeps it live across the call); the image does not, and
        // the extra `lwz` plus the register renaming it forces is the whole
        // residual -- no value ever differs (nothing stores to the vector on
        // either side, same exits).  Re-measured inert: the whole block with no
        // named vector reference (every use through `p1->Variants()`), and a
        // hybrid that keeps the local reference but re-derives begin()/size().
        // An iterator-cursor loop is much worse (94.5).  Open question: what
        // makes c2 treat the vector's `_M_start` as call-clobbered here.
        // w28-a (still 98.103): the residual census puts the whole structural
        // delta at FOUR `lwz` -- the image re-reads `_M_start` at rows
        // 145/157/184/196 and we CSE it -- plus four `bl _M_find` name rows.
        // Those four are MASKED by icf_aliases.map (all four maps share the
        // tree-search body; the split object can only name the survivor), so
        // they are a fold artifact and cost no points, not a wrong callee.
        // No value differs: nothing stores to the vector on either side and
        // every exit lands on the same block.
        // w36-b (98.10329 -> 99.04225 canonical, 97.07042 -> 98.05634 fuzzy): of the
        // four `_M_start` reloads, the two at the `begin()` sites ARE reachable
        // from source and the two at the loop top are not.  Spelling that one
        // read as `p1->Variants().begin()` instead of through the bound
        // reference reproduces the image's `lwz r10,0(r11)` + `lwz r10,0(r10)`
        // pair exactly, while `variants.empty()` / `.size()` / `[i]` keep
        // reading through the reference.  Reading EVERY use off the call
        // (i.e. dropping the reference) also earns the loop-top reload but
        // moves the `size()` loads to start-then-finish instead of the
        // image's finish-then-start and lands at 98.009, so it is worse than
        // leaving those two rows alone.  The remaining two deletes are the
        // loop-top reloads.
```
