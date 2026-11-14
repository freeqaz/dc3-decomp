# Lane notes: `src/system/char/ClipDistMap.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?FindBestNode@ClipDistMap@@IAA_NMMMAAUNode@1@@Z`

`ClipDistMap::FindBestNode` -- moved verbatim from `src/system/char/ClipDistMap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 204-253):

```text
                // w19-c: still the 6-row residual (93.41): the image stores
                // curBeat and loads mBStart two slots later than we do.  Inert
                // or worse: plain for-loops (this spelling, same rows as the old
                // hand-rotated do/while), BeatB into a local first (90.53),
                // nextBeat stored before curBeat (90.53); w13-c: BeatB spelled
                // inline as `mBStart + (float)rowIdx / (float)mSamplesPerBeat`.
                // w21-q (93.41, same 6 rows): standalone cl.exe probe that
                // reproduces this build exactly; 28 inner-loop combinations
                // (foundBetter as bool / int / if-else / inline test x seven
                // nextBeat spellings: locals for (float)rowIdx, (float)spb,
                // mBStart or the quotient, before or after the curBeat store)
                // plus `Node &`, `Array2d &`, `const float &aStart` (RB3),
                // an outer do/while and RB3's curBeat: none moves the curBeat
                // store past the extsw or the mBStart load past the fcfids.
                // w21-ai (93.41, same 6 rows, w21-q's probe, ~35 more
                // variants): function-scope curBeat / rowIdx; `const float &`
                // to mBStart; mBStart, mSamplesPerBeat, (float)rowIdx or the
                // quotient read into a local before the curBeat store (reading
                // mBStart first DOES put the extsw ahead of the stfs, as in the
                // image, but then hoists the lfs to the top, 87/91 either way);
                // `nb = q; nb += mBStart`; a `Node &` alias; inline
                // SetNode(node, curBeat, rowIdx) helpers (int or float param);
                // `__restrict` on the Node (ignored on a reference, hoists
                // everything on a pointer).  The image's lfs mBStart behaves as
                // if it depended on the stfs (it issues last) while the stfs
                // itself is NOT prioritised -- no spelling found gives both.
                // w31-d (2026-11-03): opcode multiset is IDENTICAL (91 = 91
                // instructions, no insert/delete/opcode delta), so this is a
                // placement residual and not a form one: exactly three
                // instructions move -- the `stfs f0,0x0(r7)` (curBeat) is
                // emitted BEFORE `extsw r10,r9` here and AFTER it in the image,
                // and the `lfs f13,0x14(r3)` (mBStart) is emitted before the
                // `lfd`/`fcfid` pair here and after both conversions in the
                // image.  The stored values are the same fields at the same
                // offsets with the same fdivs/fadds.  No behaviour defect.
                // FIXED (93.40659 -> 100 canonical, 99.45055 fuzzy, 364 B =
                // the image's 364 B): binding the CONVERTED sample rate to a
                // const reference (C2RS-BRIDGE 8.8 -- bind, do not copy) is
                // what gives both moves at once.  The reference keeps
                // `(float)mSamplesPerBeat` a distinct IL value, so the
                // int->float conversion chain and the mBStart load are no
                // longer folded into one operand pattern: mBStart issues after
                // both fcfids (the image's slot) and the curBeat store is
                // deferred past the extsw.  A plain named `float` is inert
                // (93.40659, w21-q's local), and `const float &` to mBStart or
                // to the quotient is inert or worse; only the RATE binding
                // moves it.  Residual: one FPR pair swap (our converted rowIdx
                // lands in f13 and mBStart in f12, the image the other way
                // round) -- regasg first-fit, unchanged by five further
                // spellings, so fuzzy stays 99.45055 while canonical is 100.
```


## `?FindBestNodeRecurse@ClipDistMap@@IAAXMMMMM@Z`

`ClipDistMap::FindBestNodeRecurse` -- moved verbatim from `src/system/char/ClipDistMap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 264-277):

```text
// BUG FIX (w19-x): every use was shifted one parameter to the left -- the
// assert, the break test, both search bounds, the recursion offset and the new
// endBeat used param 1 where the image uses param 2 and param 2 where it uses
// param 3, and param 3 was only ever forwarded.  The image (prologue binds
// f26=p1 f30=p2 f29=p3 f28=startBeat f31=endBeat):
//   0x823E5EC0 `fcmpu cr6, f30, f25`   MILO_ASSERT(p2 > 0) -- its message is
//                                       the image's "minDist > 0" string
//   `fsubs f0, f31, f28` / `fcmpu cr6, f0, f29`  break if end - start <= p3
//   `fadds f0, f30, f29` / `fadds f0, f0, f28`   searchEnd = p2 + p3 + start
//   `fsubs f13, f31, f29` / `fsubs f13, f13, f30` searchStart = end - p3 - p2
//   `fmr f1, f26`                                FindBestNode(p1, ...)
//   0x823E5F8C `fadds f4, f27, f30`, `fsubs f31, f27, f30`: recurse at cur + p2, end = cur - p2
// RB3's FindBestNodeRecurse(maxError, searchRadius, minGap, ...) has the same
// shape.  The caller passes (minErr, 0.45 * maxDist, maxDist - 0.9 * maxDist).
```


## `?CalcWidth@ClipDistMap@@IAAHXZ`

`ClipDistMap::CalcWidth` -- moved verbatim from `src/system/char/ClipDistMap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 360-369):

```text
    // NEGATIVE RESULT (w13-c, 95.506): the 7 rows are the image converting
    // mSamplesPerBeat (std/lfd/fcfid) BEFORE loading mAStart for the
    // subtraction.  Inert, byte-identical: dropping both locals for
    // `(mAEnd - mAStart) * (float)mSamplesPerBeat`, and the commuted
    // `spb * (mAEnd - mAStart)`.
    // w21-i (stopped at 95.506, same 7 rows): hoisting the conversion into
    // its own local ahead of aStart (`float fspb = (float)mSamplesPerBeat;`)
    // is byte-inert; RB3's final line `aStart + (float)(width - 1) / spb`
    // costs 95.5 -> 86.5 (the image reloads mAStart and mSamplesPerBeat
    // after the floor call, so the members, not the locals, are read there).
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: load
placement only (lfs/fsubs of 0xc(r31) one block later).


## `?FindDists@ClipDistMap@@QAAXMPAVDataArray@@@Z`

`ClipDistMap::FindDists` -- moved verbatim from `src/system/char/ClipDistMap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 435-439):

```text
// w20-j: normalized 100 / fuzzy 99.985. One row: idx 529 (the final
// `mDists(i, j) = err` store) is `mullw r11, r22, r11` (j * width) in the
// image vs `mullw r11, r11, r22` here; same value, the other two mDists(i, j)
// sites emit width * j on both sides. Commutative operand order; spelling the
// index out explicitly made it worse (99.4).
```

Inside the body (was lines 530-536):

```text
                // w25-ha: numWeights, meanSq and lastDiff are named on purpose.
                // They emit no code; each shifts c2's lowering temp ids, and the
                // three together put the final mDists(i, j) store's mWidth
                // memory operand on a base temp at the residue whose cqlo3 key
                // (0x10008) sorts below j (0x12380), giving the image's
                // `mullw r11, r22, r11`, while the other two stores keep mWidth
                // first (C2RS-BRIDGE 8.7, memory leaf vs variable).
```


## `?Draw@ClipDistMap@@QAAXMMPAVCharDriver@@@Z`

`ClipDistMap::Draw` -- moved verbatim from `src/system/char/ClipDistMap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 574-642):

```text
// w7-bh floor note: 96.58% canonical / 1732 B.  13 of the 31 residual rows are
// two instruction-scheduling groups and the rest are a forgiven register
// permutation (r10/r11, r28-r30):
//   * idx 263/265: `lfs f11, 0x20(r31)` moves two slots.
//   * idx 272-282: the image emits the int-to-float triple
//     `lfd f0, 0x58(r1)` / `fcfid f0, f0` / `frsp f11, f0` BEFORE the `lfs`
//     that feeds the following `fadds`; we emit it after, into f13.  Every
//     instruction is present on both sides -- this is the order MSVC picked,
//     not a different expression.
//   * idx 316/319: `cmpwi cr6, r11, 0x0` moves three slots.
// No source spelling attempted moved any of these; recorded as a scheduling
// floor rather than chased further.
//
// w7-bn (2026-09-15), still 96.6: the 366-393 block is the mNodes loop and is
// NOT pure regalloc -- the image carries `_M_start` in r11 around the back
// edge (loaded at 0x823E4D60 for size(), reused at 0x823E4D34 for the element
// address) and lowers the two field reads from DIFFERENT address expressions:
// curBeat is an unshared indexed load `lfsx f3, r11, r30` (0x823E4D58) while
// nextBeat materialises the sum (`add r10, r11, r30` / `lfs f4, 0x4(r10)`).
// We reload _M_start at the loop top and share ONE sum for both `lfs`.  Refuted,
// full ninja each: `Hmx::Color(...)` as a temporary in the call (96.6, inert);
// `int i` (96.6, inert); `float nb = mNodes[i].nextBeat` hoisted above the
// colour ctor (94.9, home slot); `float cb = mNodes[i].curBeat` hoisted (94.9);
// both hoisted after the ctor (96.6 -- flips our `add` to begin+off like the
// image but the sum stays shared).  Whatever made the image not CSE the two
// `begin + i*12` sums is not the evaluation order of the two reads.
// w26-x: the cell loop's residual is ONE scheduling permutation, not a value
// difference.  Every instruction of the int->float chain is on both sides
// (`std <slot>` / `lfd` / `fcfid` / `frsp` / `fadds` / `stfs 132(r1)`); ours
// issues `lfd/fcfid/frsp` right after the `std` and before the `fdivs` and the
// three colour stores, the image issues them after the stores.  Measured in w26-x:
//   - moving the `cellRect.y = ...` statement itself (before the colour stores,
//     before `float c`, or after `alpha`) is BYTE-IDENTICAL -- c2 list-schedules
//     the block and source order carries no edge here;
//   - naming the int (`int invRow`), the float (`float yf`), writing
//     `y + (float)(...)`, and `mDists.mHeight - row - 1` are all byte-identical;
//   - what DOES move it is adding one spare IL node to the block (a second
//     `cellRect.y = ...`, or `float cy = (float)(...)` between two stores):
//     that scores 98.855 fuzzy / 99.016 canonical but also emits an extra
//     `lwz r11, 60(r31)`, i.e. it buys the schedule with a statement the image
//     does not have.  Not landed (invented code to move a scheduler counter).
// Mechanism hypothesis: c2 scheduling height -- the image's conversion chain has
// a lower height than the fdivs + three colour stores, ours higher.  Open
// question: which real IL edge would sink it.
// w15-i1 (97.69 canonical after the node-reference fix): remaining rows are the
// cell loop's int->float of cellRect.y (the image converts it AFTER the three
// colour stores and loads mLastMinErr later; we hoist both), one cmpwi placement,
// and the node loop's `add r10, r11, r30` operand order.  Tried: cellRect.y
// assigned before the colour (inert); `Node *` / `const Node &` / begin()[i] /
// *(begin()+i) (all inert); a float local for curBeat (back to 96.58).
// w27-f (97.69053 -> 98.15243 canonical, 97.63280 -> 98.14088 fuzzy, 13 rows
// -> 9): C2RS-BRIDGE 8.8 on the cell-loop members.  Binding the two members
// the cell test reads to named references --
//     const float &worstErr = mWorstErr;
//     const float &c = (worstErr - err) / worstErr;
// -- removes three whole clusters without changing a single value:
//   - the local-minima loop's `cmpwi cr6, r11, 0` moves back onto the
//     image's slot (it had been pushed below the fcfid);
//   - the node loop's `add r10, r11, r30` / `lfsx f3, r11, r30` operand
//     ORDER comes back to the image's (we had the strengthened index first);
//   - fuzzy gains a further 0.046 from `c` alone with canonical unchanged.
// The remaining 9 rows are two scheduling clusters and are pure order: the
// multiset is identical (433/433 instructions, no opcode-count difference).
// Mechanism: the bound reference gives the member load its own IL leaf, so the
// division and the comparison stop being scheduled off one shared base.
// Measured inert on top of this: binding mLastMinErr, the mDists.mHeight int,
// the (float)(...) result in either loop, moving cellRect.y above the colour
// stores, and `cellColor.alpha` hoisted; `float err = mDists(col,row)`
// (non-const) raises canonical but costs 0.17 fuzzy.
```

Inside the body (was lines 691-708):

```text
    // Draw distance map cells
    // w28-b (2026-11-02): Draw's residual is 8 of 437 rows (canonical
    // 98.15243, fuzzy 98.14088 -- the worklist's 97.691 predates a landing).
    // Both halves are sched0 placement inside this inner block, not values:
    //   (a) `lfs f11, 0x20(r31)` (mLastMinErr) sits one slot earlier here
    //       than in the image, before `fsubs f12,f13,f0` instead of after;
    //   (b) the int -> float conversion of `(mDists.mHeight - 1 - row) * 2`
    //       (lfd/fcfid/frsp) is hoisted to just after the `std`, where the
    //       image leaves it in the last three slots before its `fadds`
    //       consumer.  The two are linked: the image's chain targets f13,
    //       which is still live for `fdivs` until slot 275, so it cannot be
    //       scheduled early; ours lands in f11, free from slot 267.
    // MEASURED INERT (byte-identical): moving `cellColor.alpha = 1.0f` below
    // `cellRect.y`; naming the loaded row offset as its own `float rowY`
    // before the add; naming `float lastMin = mLastMinErr` for the compare;
    // moving the `cellRect.y` store above the three colour stores.  Worse:
    // `float c` instead of `const float &c` 98.09 (canonical unchanged), and
    // dropping the `const float &worstErr` binding 97.69.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual:
lbl_820211C4 = 1e30f, equal to our __real@7149f2ca; regalloc only.
