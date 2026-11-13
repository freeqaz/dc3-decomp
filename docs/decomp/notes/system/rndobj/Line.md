# Lane notes: `src/system/rndobj/Line.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?UpdateLinePair@RndLine@@IAAXPAVPoint@1@0@Z`

`RndLine::UpdateLinePair` -- moved verbatim from `src/system/rndobj/Line.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 418-482):

```text
// 80.6% canonical (w7-ay: floor held). Tried, each measured against the
// image at 82678AF8-82678BE8: `if (len != 0) invLen = 1/len; else invLen = 0;`
// matches the image's `b`/`fmr f0, f11` join (82678B34/82678B38) but shifts
// the FPR assignment of the eight cap/body vertex writes so that the cap
// helpers re-read pos.y/pos.z instead of forwarding them (79.8); spelling
// the start-cap perp as `perp.x = -side1.y; perp.y = side1.x` reorders the
// mHasCaps test the way the image has it (82678B94-82678BA8) but the same
// cascade lands the caps at 77.9. The dir/side stage in the image forwards
// dir1.y and proj2.x but reloads dir1.x and proj2.y (82678B18, 82678AFC) --
// the mirror of what our spelling forwards -- and the residual is that
// register-allocation cascade, not a missing statement.
// w21-c (80.63 -> 82.59): the dir normalisation written as a call to an
// inline Vector2 normalize (NormalizeDir above; inv = 0, if (len != 0)
// inv = 1/len, y then x) in place of the hand-forwarded dirZ/dirX locals.
// Measured and worse: the `else inv = 0` form (81.49), x then y or Set()
// (80.42), `float x = in.x` local as rb3-xenon's out-of-line copy (78.95 /
// 79.83 with else), proj2.y stored before proj2.x (76.70).  Same helper in
// UpdateLine phase 2 LOSES there (85.35 -> 83.21 / 84.40), so UpdateLine
// keeps its hand-written spelling.  Remaining: the image reloads dir.x for
// the length and dir.y for the scale (82678B18 area), we reload dir.y for
// the length and dir.x for the scale; plus the cap register cascade.
//
// w29-d (2026-11-02): 82.58741 canonical / 79.33566 fuzzy.  Re-measured the
// w21-c sentence above rather than trusting it -- binding the three viewPos
// components to locals is still load-bearing (dropping them to direct member
// reads costs 5.0 canonical), and the NormalizeDir call is still the best
// spelling.  The row is NOT closeable by spelling and this is the measurement
// that says so: 120 equal / 135 diff_arg / 21 insert / 24 delete / 7 replace,
// body target 286 vs base 283 (ours is 3 SHORTER, so no section-14 artifact).
// Every one of those 187 rows is placement:
//   * the FPR file rotates f8..f13 through the cap/body writes as one block;
//     the image and we hand the same values to different registers throughout
//     idx 173-301, in runs of 8+ rows, and `addi r7, r11, 0x4` vs `0x8`
//     (idx 204/205) is that same rotation hitting two output pointers.
//   * `fsubs f11, f10, f11` vs `fsubs f11, f11, f10` (idx 324) is REAL but
//     benign: traced end to end, both sides compute viewPos.y - side.y; the
//     objdiff pairing simply swapped which operand register it calls first.
//   * the length stage differs by one forwarding decision: the image squares
//     dir.y from the register and RELOADS dir.x (idx 133 `lfs f13, 0x38(r7)`
//     after its own `stfs`), we forward both.  That is c2's store-forwarding
//     choice on an in-place NormalizeDir, and the dead `fmr f13, f12` the
//     image emits just before the reload (idx 132) is the tell that its
//     `in.y` binding was copied and then discarded -- no source statement
//     produces a copy whose only purpose is to be overwritten.
// Hypothesis for the remaining rows: regasg first-fit over the sched3 order,
// re-keyed by the dir1/proj2 forwarding difference above.  No source handle
// in the one spelling tried this round: reading viewPos1.x/.y/.z straight
// from the struct instead of binding them to locals costs 5.0 canonical
// (82.58741 -> 77.58741), which re-confirms w21-c's locals are load-bearing.
//
// w34-b (2026-11-04): behaviour verdict first -- no divergence.  The two
// streams are byte-identical through instruction 97 (the whole pt1 == pt2
// path and both projections), and after that every difference is one of:
// a register, an instruction ORDER, a branch displacement, or an address
// temp the two builds materialise differently.  The apparent +0x40
// displacement set (our `stfs 0x0/0x4(r9)` against the image's
// `0x40/0x44(r7)`) is exactly that: r9 = r7 + 0x40, so the EFFECTIVE
// addresses agree.  Callee multiset identical, and every store value lands
// on the same field, verified against the listing.  The per-side load
// multiset differs only where the image re-reads a value we forward -- the
// same class the CacheData row closed on -- but the forwarded thing here is
// already a member reference, so section 8.8 has no copy to break.
// Measured and rejected this lane: proj2 stores written y-then-x to match
// the image's emitted order (79.01049) and `const float &ix/&iy` inside
// NormalizeDir (79.43706).  The row is order, not value.
```


## `?UpdateLine@RndLine@@IAAXPAVPoint@1@0@Z`

`RndLine::UpdateLine` -- moved verbatim from `src/system/rndobj/Line.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 577-635):

```text
// 85.3% canonical (w7-ay, from 76.9). Residuals, all tried and consistent
// with the phase notes below: the phase-1 IV is derived from &start->ViewPos()
// and then biased (82678404 `addi r11, r4, 0x20`, then `subi r9, r11, 0x34`)
// where we bias start directly; phase 2 reloads dir.y for the normalisation
// multiply and side.x for the width scale but forwards dirX and side.y
// (partial store-forwarding -- spelling the re-reads as `dir.y * invLen` /
// `side.x * mWidth` forwarded MORE, not less, 81.8); the last-point copy
// batches its four loads into r7/r6/r9/r8 before storing; the two phase-4
// copy loops count with `subic.`/`bne` (82678694, 826786F0) and `stw`+`addi`
// where every counted spelling here (for, do/while, hoisted bound) gives
// `mtctr`/`bdnz` with `stwu`; the end cap re-reads pos.x after the Subtract
// where the image keeps it in f0.
//
// w7-bn (85.3, no net gain, full ninja each): the phase-4 `subic.`/`bne` loops
// ARE reproducible -- a STRUCT copy in the loop body is what stops MSVC from
// converting the counter to ctr.  `*(Vector2 *)&p->unk[8] = *(Vector2 *)&end
// ->unk[8]; p->ViewPos() = endView;` (endView a `Vector3 &`) gives both loops
// row for row: `subic.` at the top, `stw`+`addi r11, r11, 0x48`, the IV biased
// to p+0x20 (82678680 `addi r11, r11, 0x20`), the side words read off the
// end/start base (`lwz r8, 0x40(r29)`) and the view words off a hoisted
// end+0x20 (rows 186-207 and 214-229 all equal).  The Vector2 copy alone
// keeps `subic.` but leaves the IV `stwu`-biased (83.9); int side + Vector3
// view keeps `subic.` with the IV at p (84.2).  What it costs is phase 5: with
// any struct copy in phase 4 the start cap stops store-forwarding the
// Subtract result into the Add (`lfs f11, 0x8(r11)` / `lfs f10, 0x0(r11)`
// reloads instead of the image's forwarded `fadds f12, f0, f12` at
// 826787A4, 6 inserts / 3 deletes where this spelling has 2 / 1), so the net
// is 84.9.  The two have to be solved together; the int-copy spelling below
// keeps the better total.
//
// w21-c (2026-10-03): still 85.35 canonical / 82.27 fuzzy.  Re-read phases 3-5
// against 82678548-826787A0 (fold test, flip, prevRay/oldPrevRay copy order,
// Intersect(new, old, side) argument order, both phase-4 bounds, start-cap
// perp): no behaviour difference found.  Measured: the inline Vector2
// normalize that lifted UpdateLinePair, in phase 2 here: 83.21 (inv = 0 form),
// 84.40 (else form), 84.70 / 85.35 with rb3-xenon's `float x = in.x` local --
// none better than this hand-written spelling, whose reload pattern (dir.y
// reloaded for the scale, dirX forwarded) already matches the image row for
// row.  Phase-1 proj addressed off pt->unk[4] instead of &viewPos + 1: inert.
//
// w26-j (2026-11-01) behaviour verdict: unicorn records DIVERGENT for this
// symbol, class `wild_jump_match` (stable_divergent, harness v4) -- both sides
// crash somewhere DIFFERENT, which is the documented whole-function-rewrite
// signal for that harness class, not a defect.  It agrees with the listing read
// above.  Re-read the name_check listing this lane:
//   - no wrong constant: every `__real@` pairs on its own row.
//   - no field error: the 10 OFFSET_SWAP rows are pairs like -0x50(r30) /
//     -0x8(r30) that appear in BOTH listings, same base register, in the
//     opposite row order -- a load-ordering difference inside phase 2/3, not a
//     different displacement.
//   - the 2 CONTROL_FLOW rows (idx 203/227) are the phase-4 copies written
//     above.  The image emits `subic.` + `bne` (a condition loop) and walks the
//     destination at +0x0..+0xc; we emit `mtctr` + `bdnz` (a CTR loop) and walk
//     it at +0x3c..+0x48, i.e. we keep the index base 0x3c (= 0x48 - 0xc)
//     folded into the pointer.  That bias is also where the phase-4 addr
//     relocation rows come from.  Making MSVC pick the condition loop over the
//     CTR loop is the c2 loop-rotation heuristic; the count is already the
//     `divwu` quotient in both listings, so the trip count and the semantics
//     are identical.
```

Inside the body (was lines 723-736):

```text
            // w46-a (2026-11-06): two member stores, not `side.Set(x - px, y - py)`.
            // `Set` computes both operands before either store; writing the
            // members lets each `fsubs` feed its own store, and the phase-5
            // vertex code downstream of it re-aligns: canonical 90.328400 ->
            // 92.343193, fuzzy 87.82840 -> 89.66568.  The store order is the
            // image's (idx 148/149: `stfs f13, 0(r28)` then `stfs f0, 0(r30)`),
            // and the body does not shrink (1360 B, 340 instructions, before
            // and after; the image is 1352 / 338).
            // Equal-row accounting, stated rather than hidden: EQUAL 189 -> 187
            // of a total that also drops 355 -> 351.  The eight one-sided rows
            // (insert 17 -> 13, delete 15 -> 11) become aligned rows; the two
            // lost exacts are the aligner pairing across a permutation in the
            // phase-5 loops (idx 305 `lfs` vs `addi`, idx 340 `fadds` vs `fmr`),
            // not instructions that stopped matching.
```

Inside the body (was lines 748-761):

```text
    // Phase 4: points outside [start, end] copy the side vector and view
    // position of the nearest visible point. `mPoints.back()` is evaluated
    // once per block (the image hoists the bound out of the first loop and
    // counts with divwu, then re-evaluates it before MapVerts).
    // w46-a (2026-11-06): written as a STRUCT COPY, not six int stores.
    // Copying `Vector2` (unk[8..9]) and `Vector4` (unk[0..3]) moves exactly the
    // same 8 and 16 bytes, but the wider copy is what stops MSVC converting the
    // counted loop into a CTR loop: with the six int stores it emitted
    // `mtctr`/`bdnz`/`stwu`, where the image has `addic.` + a condition branch
    // and walks the destination at +0x0..+0xc.  Per loop the change removes an
    // `mtctr`, a `bdnz` and a `stwu` and adds an `addic.`, and the six stores
    // then land in the image's order.  Canonical 88.23669 -> 90.32840, fuzzy
    // 85.39645 -> 87.82840, equal rows 167 -> 189 of 357, and the body does not
    // shrink (1356 -> 1360 B against the image's 1352).
```
