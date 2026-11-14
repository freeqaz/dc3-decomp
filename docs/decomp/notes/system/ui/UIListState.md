# Lane notes: `src/system/ui/UIListState.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Scroll@UIListState@@QAAXH_N@Z`

`UIListState::Scroll` -- moved verbatim from `src/system/ui/UIListState.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 420-429):

```text
        // w16-d BEHAVIOUR FIX: skipActive does NOT return here. The image's
        // skipActive test (`clrlwi. r11, r30, 24` / `bne 0x12d4`, section
        // offset 0x122c) branches to 0x12d4 = `lwz r28, 0x54(r1)`, the first
        // instruction of the accept block, which then stores mTargetShowing
        // (`stw r30, 0x34(r31)` at 0x12dc, r30 = state.mFirstShowing loaded at
        // 0x1228) and runs the assert. The loop's IsActive exit enters one
        // instruction later (`bne 0x12d8`) because r28 already holds
        // state.mSelected there -- that is the "extra" reload an earlier lane
        // read as a CSE residual. We returned, so a circular list scrolled
        // with skipActive never moved.
```

Inside the body (was lines 471-518):

```text
                // RESIDUAL (w7-ak, 96.2 canonical): 4 of the 9 remaining rows are
                // here, and they are a CROSS-JUMP DIRECTION, not a source shape.
                // Both this arm and the mScrollPastMinDisplay arm below end in the
                // identical three-instruction tail
                // `bne cr6,<li 0> / li r11,0x1 / b <clrlwi>`; MSVC merges the two
                // copies either way.  The image keeps the FIRST copy (hosted here at
                // .L_827841C8, 0x827841C8) and has the later arm jump BACKWARD into
                // it with `b .L_827841C8` at 0x82784200; we keep the LATER copy and
                // branch forward into it.  Nothing about the two expressions is
                // asymmetric -- both are `<bool> && curSel == <int>` and both lower
                // to `cmpw cr6, r29, X` -- so there is no operand to reorder.  Note
                // the third arm (`curSel == 0`) is NOT part of the merge in either
                // build: its `cmpwi` frees a slot for `li r11,0x1` before the branch,
                // so it lowers to `li 1 / beq <done> / li 0` instead, and that arm
                // already matches instruction for instruction.
                //
                // w21-p: 97.80 -> 100 normalized (1 row left, a branch TARGET).
                // The mScrollPastMinDisplay/else pair as ONE conditional
                // expression, not an if/else of two assignments: the 4 merge
                // rows and both insert/delete pairs disappear, both builds are
                // 182 instructions.  The one remaining row is still the
                // cross-jump: the image's second arm ends `b 0x827841C8` (back
                // into the direction==1 arm's `bne / li 1 / b`), ours ends
                // `b` into the third arm's `li 1 / beq <done> / li 0`.  Same
                // value on every path: r11 = (curSel == mMinDisplay) into the
                // shared `clrlwi r30, r11, 24`.  Tried and worse: the RB3
                // nested-if `hitBoundary = 0; if (...) hitBoundary = 1;` shape
                // (91.6), `curFirst == maxFirst ? curSel == ScrollMaxDisplay()
                // : false` (99.0, cntlzw form), `atZero ? curSel == 0 : false`
                // in an if/else (96.8); `atZero && !curSel` is identical.
                //
                // w25-gm (c2rs scratch, ~25 spellings): MSVC's cross-jump here keeps
                // the LATER copy of a shared tail -- base: the second arm jumps
                // forward into the third arm's `li 1 / beq / li 0`; with the third
                // arm spelled `? true : false` (+3 instrs) the second arm turns into
                // the `bne / li 1 / b` diamond and the FIRST arm jumps forward into
                // it.  The image's backward jump therefore needs the direction==1
                // tail to be the later block in the cross-jump walk while staying
                // first in layout.  Open question: which IL shape (block order vs
                // layout pass) gives that.  Inert or worse: arm order inverted
                // (layout follows it), operand swaps, a named minDisplay, inline
                // atZero, a value select, if/else per arm.
                // w26-f15, 5 more spellings inert (still 182 insns, still the
                // single row 148 `b .+16` vs the image's `b .-56`): hoisting
                // both `curFirst`/`curSel` assignments above the if, hoisting
                // only `atZero`, moving `atZero` below the two shared
                // assignments, `direction != 1` with the arms swapped, and
                // hoisting `maxFirst` into a ternary above the if.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.973, 1 non-equal row(s): One
BRANCHDEST row (idx 148): the two arms' `cmpw` tails reach the same `li 0 / li 1 ->
clrlwi r30` result either way; cross-jump direction only (w7-ak). NOT A BUG.
