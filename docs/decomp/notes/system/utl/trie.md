# Lane notes: `src/system/utl/trie.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?store@Trie@@QAAHPBD@Z`

`Trie::store` -- moved verbatim from `src/system/utl/trie.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 12-39):

```text
// w21-ag: 100 canonical (fuzzy 99.5), 13 rows left, all register-only or a
// branch target, none a value difference:
//  - i <-> sibCount in r26/r27 (image i=r27, sibCount=r26), 12 rows.  Moving
//    `int i;` above `strLen` is inert.
//  - the zero-trip `blt` (diff idx 21): the image jump-threads it straight to
//    the inc_dup_count tail (curIdx is 1 there, so the `result == 0` test is
//    provably false); we land on that test, which then falls through.  Same
//    behaviour.
// Levers that took it 95.31 -> 100: the fast path is written with the trie's
// own two cursors (curIdx = new node in r30, parentIdx = its parent in r28,
// exactly the image's registers) instead of a fresh temp, and the insert path
// calls inc_count in each arm rather than through a shared local (the image's
// `mr r3, r31` sits in both arms around a cross-jumped `bl inc_count`).
//
// w26-f3 (2026-11-02): the c2rs colour dump now names the whole residue.  The
// two swapped values are colour candidates
//   i        id 7   pri 18  tie 142   -> r26   (image: r27)
//   sibCount id 12  pri 19  tie 27    -> r27   (image: r26)
// and the image has i popped first.  i's tie (142) already beats sibCount's
// (27), so priority alone decides it, and the gap is exactly ONE point:
// i 18 -> 19, or sibCount 19 -> 17.  Measured INERT this lane: `if ('\0' <
// sibCount)` -> `sibCount > 0`, swapping the `j` and `sibCount` declarations,
// and moving `char ch = str[i];` across `check_index(nodeIdx)`.
// `while (j < sibCount)` -> `while (sibCount > j)` costs a canonical point
// (99.96528), so the comparison's operand order is not free here.
// (Prior lanes: moving `int i;` above `strLen` is inert; the parentIdx
// declaration move was refuted.)  The open lever is any source edit that
// changes i's block-reference count without emitting an instruction.
```

Inside the body (was lines 47-52):

```text
    // w14-d: strLen is the plain strlen.  The hand-written walk this replaces
    // computed `(p - str) - 1` with p one PAST the terminator -- i.e. strlen --
    // and the image's `subi r11, r11, 0x1` / `clrrwi. r21, r11, 0` is exactly
    // MSVC's inline strlen, zero-extended into r21.  (An intermediate w14-d
    // commit wrote `strlen(str) - 1` here: that is one SHORT, a behaviour
    // change, and is corrected by this one.)
```


## `?remove@Trie@@QAAXI@Z`

`Trie::remove` -- moved verbatim from `src/system/utl/trie.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 154-231):

```text
// RESOLVED 2026-09-15 (lane w7-bn): 88.61 -> 100.0 canonical, 924 B both sides,
// 231 rows all equal.  Everything below this paragraph is the history of how the
// residual was misread as "regalloc / loop-rotation floor"; it is kept because
// the mechanism is general and the refutations were real.
//
// The two un-rotated loops (root sibling scan at 827FE588, child re-parent at
// 827FE60C) are NOT a rotation heuristic we cannot reach.  MSVC duplicates a
// loop header into the latch only when the header is ONE basic block; a
// `check_index(1)` with a constant argument is inlined and its MILO_ASSERT
// folds to nothing, but the fold leaves the header split across blocks, and
// the rotation is skipped.  That is exactly the trie's own idiom -- every
// NodePtr(this, n) read is preceded by check_index(n) -- so the original had
// `check_index(1)` before its `SiblingCount(NodePtr(this, 1))` /
// `FirstChild(NodePtr(this, 1))` reads and we had dropped them because they
// emit no code.  Adding them back: 88.61 -> 93.5 (scan loop) -> 97.4 (both).
// The r25/r26/r27 rotation went with it (it trailed the peeled guards).
// The last 2.6 was block ORDER, not the tail-merge itself: both compilers
// cross-jump the two `bl delete_node; b exit` tails, but each keeps the copy
// that is reached by FALL-THROUGH.  The image keeps the not-root path's
// (.L_827FE708) and makes the root stub `li r4, 1; b .L_827FE708` (827FE664);
// we kept the stub's.  `if (curIdx != 1) { ...; return; } delete_node(1);`
// places the stub after the loop (99.1) and a `goto delete_root` to a label
// AFTER the not-root path makes that path's copy the fall-through one (100).
// Refuted on the way, all measured: an explicit `goto`-loop spelling of the
// scan loop is byte-identical to `while` (88.61); `#pragma optimize("t", on)`
// on remove inlines every check_index (3.5); `#pragma optimize("g", off)` is
// /Od (2.3) -- the TU is /O1 and the image agrees.
//
// WRONG_CALLEE row on this function (target `check_index` vs base `delete_node`):
// NOT settled as an artifact.  Emitted-callee multisets, measured 2026-09-14 with
// scripts/analysis/callee_multiset.py (objdiff JSON, both sides counted
// independently of row pairing):
//
//     delete_node   target 4  base 4   <- equal; no call is missing
//     check_index   target 29 base 30  <- WE EMIT ONE EXTRA BOUNDS CHECK
//     everything else identical (37 vs 38 calls total)
//
// The extra `check_index` is the function's ONLY count asymmetry and it is the
// accused callee itself, so this row is a live lead, not alignment noise.  The
// source has 29 textual `check_index(` sites and the object emits 30, so the
// 30th is compiler-duplicated (loop rotation peeling a call out of a latch is
// the likely shape) or comes from an inline in trie.h.
//
// SETTLED 2026-09-14 (lane w7-bh): it is the LOOP-ROTATION LATCH COPY, not a
// source defect, and the listing pins it exactly.  The 30th call is the one we
// emit at diff index 195, in the "update parent pointers of children" loop.
// The image keeps ONE top-tested copy of that loop's header and an
// unconditional back-edge:
//     827FE60C  lwz r28, 0x11(r31)      <- childIdx = FirstChild(NodePtr(this,1))
//     827FE618  bl check_index          <- the ONLY check_index(childIdx)
//     827FE624  lbz r11, 0xf(r11)       <- SiblingCount
//     827FE628  cmplw cr6, r29, r11  /  827FE62C bge  -> exit
//     ...body (2 more check_index)...
//     827FE660  b .L_827FE60C           <- unconditional back-edge
// Ours rotates it: the entry test is PEELED and constant-folded (`cmplwi
// r11, 0x0` / `beq`, because updateCount is provably 0 there, so
// `0 >= n` collapses to `n == 0`), and the whole header -- `lwz 0x11(r31)`,
// `bl check_index`, `mulli`, `add`, `lbz 0xf`, `cmplw`, `blt` -- is DUPLICATED
// into the latch at diff indices 192-200.  That duplicate `bl` is the 30th.
// Same class as the already-recorded root-scan rotation below, and the same
// class the wave-7 brief records as refuted across six spellings.  Nothing in
// the source is missing a call and nothing emits one too many.
// (w7-bn: the "six spellings" were all loop SYNTAX; the lever was a folded
// call in the header -- see the RESOLVED paragraph at the top.)
//
// MEASURED NEGATIVE (w7-bh, 2026-09-14): dropping the `auto _tmp2 =
// NodePtr(this, 1);` temp and writing `unsigned int updateIdx =
// FirstChild(NodePtr(this, 1));` inline -- on the theory that it is the CSE of
// that preheader read against the loop's first read (`mr r30, r28` where the
// image emits a second `lwz r30, 0x11(r31)` at 827FE608) that lets MSVC peel --
// costs 88.61 -> 88.20.  Reverted.
//
// ⚠ An earlier revision of this note claimed "5 delete_node call sites here, 4
// in the image" and blamed retail tail-merging for the row.  That was wrong: it
// compared the image's EMITTED `bl` count against SOURCE call sites, which is
// apples-to-oranges.  Our object emits 4 as well.  The tail-merge itself is
// real (see the note at the `delete_node(1)` site) but it produces no count
// difference and does not explain this row.
```

Inside the body (was lines 277-285):

```text
        // NEGATIVE RESULT (w7-as, 2026-09-14): the r25/r26/r27 three-way
        // rotation (16 of the 54 residual rows) is NOT reachable by declaration
        // order.  Image: firstChildIdx=r25, prevSib=r27, traverseCount=r26;
        // ours: r27 / r26 / r25, i.e. plain descending decl order.  Moving
        // `prevSib`/`traverseCount` above `firstChildIdx` *with* their `= 0`
        // initialisers sinks both `li` to the top of the block and costs
        // 88.61 -> 86.90; moving the bare declarations up without the
        // initialisers is exactly byte-identical (a slot is claimed at the
        // first STORE, not at the declaration).
```

Inside the body (was lines 345-352):

```text
            // Retail does NOT carry `curNode` across this loop: the body
            // recomputes NodePtr from the *current* index (mulli r30,0x11 /
            // add r11,r31 at 827FE550) and reads NextSibling out of that, and
            // curNode is rebuilt from scratch after the loop at 827FE5C8.
            // NEGATIVE RESULT (w7-as, 2026-09-14): writing this as a `for` with
            // the increment in the latch is byte-identical (88.61 both ways);
            // (w7-bn) so is an explicit `scan_top: if (..) { ..; goto scan_top; }`.
            // The rotation was never reachable by loop syntax -- see the macro.
```
