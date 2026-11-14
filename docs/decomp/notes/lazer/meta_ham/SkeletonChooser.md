# Lane notes: `src/lazer/meta_ham/SkeletonChooser.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetPlayerSkeletonNavData@SkeletonChooser@@AAAXHH@Z`

`SkeletonChooser::SetPlayerSkeletonNavData` -- moved verbatim from `src/lazer/meta_ham/SkeletonChooser.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 965-998):

```text
            // w20-i BEHAVIOUR FIX: the image uses the SAME thresholds for both
            // players (82909544 fcmpu cr6,f0,f30 / blt, f30 = __real@be19999a =
            // -0.15f, on the right side; 82909554 fcmpu cr6,f0,f31 / bgt,
            // f31 = __real@3e19999a = 0.15f, otherwise). Player 1's constants
            // were swapped.
            // w21-ba (99.941 -> normalized 100 modulo register permutation;
            // fuzzy 99.765 -> 99.88): the image's f30/f31 assignment of the two
            // hoisted literals follows the order in which MSVC's IR first meets
            // them (the first-met constant gets f31), while its block layout
            // puts the Right arm first.  A structured if/else ties those two
            // together, so every right-first spelling gave -0.15f f31 (the
            // w20-i/w21-ar list below and a standalone cl.exe sweep: ternaries,
            // switch on the side or on `== kSkeletonRight`, else-if chains,
            // defaults, bool temps, inline helpers, a 2-trip loop).  Only an
            // explicit forward goto to the Right arm keeps the Left arm first
            // in the IR (0.15f met first -> f31) and Right first in the layout
            // -- exactly 8290952C/82909530 + the 4 fcmpu rows.  Behaviour is
            // identical to the if/else.  Player 2's block does not matter (its
            // literals are already hoisted), so it stays structured.
            // w25-re (fuzzy 99.88 -> 100): the last 8 rows were r28<->r29
            // (side1 vs p2Skel).  The image tail-merges the two arms'
            // `side1 = 0` into one store (82909560 `mr r28, r23`), and
            // writing that merge out as the single shared `p1Zero:` store
            // takes one reference off side1: its colour priority drops 6 -> 4
            // while p2Skel's rises 6 -> 8, so p2Skel is coloured first and
            // takes r29.  The Right arm is spelled `if (!(x < -0.15f))` so
            // that the literal meeting order (and hence f30/f31) is
            // unchanged; jumping into the Left arm's else instead flips them.
            // Inert earlier: side1 declared at the top of the inMode block
            // (99.4, worse), SkeletonSide-typed side locals.
            // Earlier (w20-i), all inert or worse: `x > 0.15f` vs `0.15f < x`;
            // one `const float` threshold; two float locals declared 0.15
            // first; a static inline per-player helper (97.6); `side !=
            // kSkeletonRight` arm order (worse).
```


## `?ChoosePlayerSides@SkeletonChooser@@AAAXXZ`

`SkeletonChooser::ChoosePlayerSides` -- moved verbatim from `src/lazer/meta_ham/SkeletonChooser.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 1131-1158):

```text
        // RESIDUAL (w12-c, 99.4 canonical), two row groups left:
        //   * 82909B7C/82909B80 load `__real@be19999a` into f30 and
        //     `__real@3e19999a` into f31; we get the two constants in the
        //     other two registers (4 fcmpu rows).  Inert: thresholds as named
        //     `const float` locals declared -0.15f-first (w7-am), and
        //     inverting the side test so the +0.15 arm comes first (w12-c).
        // w21-ar (99.924, same 4 fcmpu rows + the 4 lis/lfs relocation-name
        //   rows they imply): standalone cl.exe probes of this block, all
        //   inert -- `-kT`/`kT` file const, two float locals, nested-if
        //   defaults, single ternary, `!(x < -0.15f)`, `-0.15f > x`,
        //   double literals, a prior TU use of -0.15f.  Every Right-arm-first
        //   layout gives the FIRST-compared constant f31; the image gives it
        //   f30.  Only the arm-swapped forms (switch, `!= kSkeletonRight`
        //   ternary) hoist -0.15 first, and they also flip the block order.
        //   Also checked: the TU's float pool differed from the image by one
        //   literal (5.0f, see kNavIdleResetSeconds); restoring the image's
        //   named const did not move this.
        //   * 82909CA8 keeps `mr r30, r3` and `cmplwi cr6, r3, 0x0` separate
        //     at the pPlayerSkeleton assert where we fuse them into `mr.`.
        // w21-bd (99.924 -> 100, all 263 rows equal): closed the f30/f31 rows
        // with lever (u) from SetPlayerSkeletonNavData -- an explicit forward
        // goto to the Right arm keeps the 0.15f (Left) compare first in
        // MSVC's IR (-> f31) while the layout stays Right-arm first.  Behaviour
        // is identical to the if/else.  Player 2's literals are already
        // hoisted, so its block stays structured.
        // Closed by w12-c: the swap tests are one `||` condition (the image's
        // fall-through from the Right test into the Left test), and
        // TheGestureMgr is read before the activeID select.
```

Inside the body (was lines 1194-1198):

```text
                // w18-e (99.30 -> 99.92; the f30/f31 constant swap noted above
                // remains): two calls that MSVC cross-jumps into the image's one
                // `bl` (r4 chosen by `bgt`), not one call on a selected ID.  The
                // merged call leaves the assert's `cmplwi cr6, r3, 0x0` in the
                // join block, so it is not fused into `mr. r30, r3` as before.
```


## `?DrawDebug@SkeletonChooser@@QAAXXZ`

`SkeletonChooser::DrawDebug` -- moved verbatim from `src/lazer/meta_ham/SkeletonChooser.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 1236-1270):

```text
// w18-e (98.54): the image puts trackingID0/1 at 0x58/0x5c and the
// MILO_ASSERT line temp at 0x60; ours had the temp at 0x58 (closed by
// w24-c1's case 4 if/else, below).  Worse:
// `int &` for trackingID (96.0) or for both selects (95.4); making the
// sFloatN globals static (87.5 -- the image reloads them, so they are
// external).
// w26-f9 (fuzzy 99.788734, canonical 100.0, 1136 B): the twelve charged rows
// are three pairs over r15/r16/r17 and nothing else.  The image puts sFloat4 in
// r15, sFloat2 in r16 and &TheRnd in r17; we put sFloat4 in r16, sFloat2 in r17
// and &TheRnd in r15.  All three are hoisted `lis` address halves.
//
// Tap: three consecutive code-band mints, all pri -51 / tie 0 / ref 4, with
// `allowed` shrinking {r14..r17} -> {r14..r16} -> {r14,r15}:
//     CMINT 122 id 59 code reuse sym 98   (sFloat2) -> r17
//     CMINT 123 id 18 code reuse sym 99   (sFloat4) -> r16
//     CMINT 124 id 17 code reuse sym 100  (&TheRnd) -> r15
// C = 22 code mints, F = 13 demotions (four k04 `mr` copies of the four stack
// int locals plus nine k03 `#2af` reload temps).  The `ben` free list is
// ascending [15,16,17,18,59,60,68,78,79,80,81,82,83] and the code band pops it
// LIFO, so mint k gets f(14-k): #9/#10/#11 -> 59/18/17.  The pop order is
// therefore the mint order reversed, and sFloat2 pops first.
//
// The image needs &TheRnd first among the three, which C2RS-BRIDGE 8.4 reaches
// two ways: &TheRnd minted at k=9 (a sched1 reorder), or F <= 10 so mint #11 is
// fresh and takes the highest id.  F is 13 here, and the four `mr` demotions
// are the four locals the image itself keeps on the stack
// (lwz 0x58/0x5c/0x60(r1), see the w7-ag note in case 4), so dropping three
// demotions changes emitted code.
//
// Measured inert (row byte-identical, 8 variants): `Rnd &rnd = TheRnd;` bound
// at function scope, before the static colours, and immediately before the
// call, used everywhere it can be; the Rect built into a named / const /
// copy-initialised local; the static bgColor bound by reference.  Open
// question: what puts one hoisted `lis` half earlier in sched1 -- the same
// blocked tie-0 family as C2RS-BRIDGE section 5.
```

Inside the body (was lines 1331-1371):

```text
                    // Lane w7-ag, 2026-09-14: the image does NOT use the
                    // `trackingID` local here -- it re-derives BOTH arguments
                    // from one test of `i`, with the join block placed right
                    // after the i==0 arm and the i!=0 arm sunk to the end with
                    // a backwards branch:
                    //   build/373307D9/asm/lazer/meta_ham/SkeletonChooser.s
                    //     lwz   r11, 0x60(r1)        ; i
                    //     lwz   r3,  0x194(r1)       ; this  (hoisted!)
                    //     cmpwi cr6, r11, 0x0
                    //     bne   cr6, .L_82906720
                    //     lwz   r5,  0x5c(r1)        ; trackingID1
                    //     lwz   r4,  0x58(r1)        ; trackingID0
                    //   .L_82906714:
                    //     bl    ?IsBehindPlayer@...
                    //     ...
                    //   .L_82906720:
                    //     lwz   r5,  0x58(r1)
                    //     lwz   r4,  0x5c(r1)
                    //     b     .L_82906714
                    // Spelling both arguments as ternaries on the SAME
                    // condition -- IsBehindPlayer((i==0)?tid0:tid1,
                    // (i==0)?tid1:tid0) -- reproduces the single test and the
                    // two-value arms exactly (51 mismatch rows -> 29, stack
                    // 3 DIFFER -> 1 PERMUTED, register swaps 27 -> 18), but
                    // MSVC lays the join out AFTER the i!=0 arm instead of
                    // before it, which is 3 inserts + 1 diff_op the current
                    // shape does not pay: canonical 98.53521 -> 97.9.
                    // Writing it as an explicit if/else into two named locals
                    // is worse again (97.8) -- the extra locals re-shuffle the
                    // 0x58/0x5c slots.  Both reverted; the 4 structural rows
                    // that remain are that block placement.
                    // w24-c1 (98.54 -> 100 normalized, modulo an r15/r16/r17
                    // permutation of sFloat4/sFloat2/&TheRnd): the image's
                    // shape is TWO whole sprintf_s calls in an if/else on i,
                    // which MSVC tail-merges -- the else arm's `bl
                    // IsBehindPlayer` is cross-jumped into the then arm's,
                    // which is why the join follows the i==0 arm and the i!=0
                    // loads sit at the end with a backwards `b`.  The single
                    // ternary of two calls lays the join out after the else
                    // arm instead (98.6), and also fixes the trackingID0/1
                    // slots at 0x58/0x5c (the w18-e row).
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.789, 12 non-equal row(s): SYM rows:
lbl_82F1BD80..94 hold 0.03, 0.37, 0.2, 0.6, 0.1, 0.25 = sFloat0..5 in order, so every
float pairs with the right global; textColor/bgColor/$S20 pair via their ctor rows. The
4 register rows are pure permutation. NOT A BUG.
