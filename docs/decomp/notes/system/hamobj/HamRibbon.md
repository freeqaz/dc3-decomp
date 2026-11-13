# Lane notes: `src/system/hamobj/HamRibbon.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?UpdateChase@HamRibbon@@QAAXXZ`

`HamRibbon::UpdateChase` -- moved verbatim from `src/system/hamobj/HamRibbon.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 149-191):

```text
// RESIDUAL (w7-an, 98.0 canonical): what is left is register permutation plus
// two addressing-fusion rows.  (a) `numKeys` lands in a callee-saved r26 in our
// build and in a volatile r11 in the image, which rotates ~60 register numbers
// downstream; (b) the image splits &back() out as `subi r30, r11, 0x44` and then
// addresses the key positively (0x30/0x34/0x38/0x40 off r30) while MSVC fuses
// ours into `subi r3, r11, 0x14` off _M_finish.
// NEGATIVE RESULT (w7-an, 2026-09-14): binding that as a named
// `Key<Transform> &last = mChaseKeys.back();` at the top of the while body DOES
// reproduce the image's dual-base addressing (frame read off _M_finish, frame
// write off &back()), but MSVC then keeps _M_finish loop-carried in r30 where the
// image reloads 0x90(r23) at the top of every iteration, and the resulting
// scheduling rotation costs more than the addressing gains: 98.0 -> 96.8.
// w20-t (98.1, branch-scan row 87 adjudicated ARTIFACT): the back-edge lands on
// the image's in-loop `lwz r8, 0x0(r31)` reload of _M_start (see w15-a below);
// the search loop stores nothing, so the hoisted value is the same.
// w21-t (98.12 -> 99.48 canonical): the dirty-key pass is a plain for loop
// (the old `if (firstDirty < size())` was MSVC's rotated guard) and the bend
// off-diagonal is one local.  Remaining rows: the w15-a search-loop _M_start
// reload (begin()[i], break form, int index, while form, local vector ref,
// hoisted cutoff: all inert or worse), the w7-an back() addressing, the
// prevDir/Dot association (the image is ((z + x) + y): source
// `dir.y*prevDir.y + dir.z*prevDir.z + dir.x*prevDir.x` reproduces it exactly
// but only moves fuzzy (+0.16), and would make native sum y,z,x instead of
// Dot's x,y,z -- not shipped; w22-a21 ships the parenthesised z,x,y form
// instead, see the dot below), and one commutative fmuls in the in-place
// Multiply(smoothDir, inv) (an inline expansion costs 0.26).
// w26-i (99.5 canonical, 28 rows across four groups, behaviour unchanged): the
// three mechanisms are independent.  (a) idx 67-87, the search loop: the image
// reloads `_M_start` from 0x8c(this) at the top of every iteration
// (`lwz r8, 0x0(r31)`), so its index lands in r9 and the strength-reduced byte
// offset in r10; ours keeps `_M_start` in r8 across the loop and the two come
// out swapped.  That reload is a c2 rematerialisation, not a front-end CSE
// difference: 15 loop spellings (break form, while, begin()[i],
// (begin()+i)->frame, data()[i], a named vector reference, a named size, a
// pointer cursor, one unified counter, post-increment in the step) all keep the
// hoisted load, or change the block shape and lose (99.0-98.2).  (b) idx
// 144-185: binding `Key<Transform> &last = mChaseKeys.back();` DOES reproduce
// the image's dual-base addressing (w7-an), but rotates the loop: re-measured
// this pass at 98.4 as a reference and 98.4 as a pointer, 96.5 when the
// binding is refreshed each iteration.  (c) idx
// 332-346: the two commutative products live inside Multiply(Vector3, Matrix3)
// in math/Mtx.h (PCH-reached), and their order is decided by the post-lowering
// cqlo3 sort (C2RS-BRIDGE 8.7), not by anything this file can express.
```

Inside the body (was lines 211-217):

```text
        // w15-a (97.97 -> 98.12 canonical): both loops are bounded by size() --
        // the image keeps the count in a VOLATILE r11 and reuses the copy loop's
        // last size() for the resize, so a cached `numKeys` pinned in r26 across the
        // memcpy calls was wrong; the shift-down is a struct assignment (image
        // computes &src before &dst).  Still open: the image reloads _M_start inside
        // this search loop (`lwz r8, 0x0(r31)`) where we hoist it; a `&&` loop
        // condition instead of the break is byte-identical.
```

Inside the body (was lines 281-287):

```text
                // w22-a21: the image's association, written out with explicit
                // parentheses so native sums in the same order (0x824C7D68..70:
                // `fmuls f0, f0, f12` = z*z, `fmadds f0, f28, f11, f0` += x*x,
                // `fmadds f0, f13, f10, f0` += y*y).  Dot() sums x, y, z.
                // 99.483 -> 99.498 normalized; MSVC now rounds the in-place
                // Multiply(smoothDir, inv)'s z term on the other product (one
                // fmadds operand pair, 0x824C7EF0), which native does not see.
```

Inside the body (was lines 325-331):

```text
                // The bend is in the X-Z plane, NOT X-Y: the image writes the
                // off-diagonal s*(1-invCos)/2 terms to m02 (0x128) and m20
                // (0x140) and leaves row 1 as the identity row
                // (0, 1, 0) at 0x130-0x138.
                // w21-t: the off-diagonal term is ONE value -- the image computes
                // it once and stores the same f0 to 0x128 and 0x140; written out
                // twice, MSVC multiplies it twice (99.0 -> 99.5).
```


## `?ConstructMesh@HamRibbon@@QAAXXZ`

`HamRibbon::ConstructMesh` -- moved verbatim from `src/system/hamobj/HamRibbon.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 407-541):

```text
        // NEGATIVE RESULTS (w7-ba, 90.2): the two residual regions are pure
        // scheduling.  Face loop: the image computes idxUp, then andc/twllei,
        // then nextIdxUp and all three clrlwi before any sth (824C8658..
        // 824C868C); we store two shorts before the trap.  Declaring idxUp
        // before nextIdxUp is byte-identical.  Vert loop: the image runs the
        // (float)boneIdx extsw/std/lfd/fcfid chain first after Normalize
        // (824C889C) and sign-extends the bone index with extsh (824C88A8)
        // before the sth; a `short` local for the index is byte-identical.
        // w21-t (stop at 90.97): also inert -- Face field assignments (85.6,
        // worse), the RndRibbon v0..v3 spelling, a named float for
        // (float)boneIdx before the norm copy, `int s = (short)boneIdx`,
        // (int)(short) casts, a named texX.  Remaining: the face-loop store/
        // trap scheduling above, the vert-loop fcfid/extsh placement, and
        // the norm/boneWeights copy word order (image 4,8,0,c).
        // w26-r2: no behaviour bug.  The two divide traps are in the same order
        // on both sides (twllei r11,0x0 before twi 5,rX,-0x1); the diff renders
        // them as insert/replace only because the surrounding adds are
        // scheduled differently, and the memory-access multiset over the whole
        // function is identical (no target-only or base-only entry).  Oracle
        // verdict: EQUIVALENT (unicorn harness 4).  No variants were spent --
        // w7-ba and w21-t already measured the declaration order, the Face
        // field assignments, the RndRibbon spelling and the short/cast forms,
        // and the residual is 73 rows across 33 clusters of store scheduling.
        // w29-b (2026-11-02) re-derived, 90.96605 / fuzzy 90.25617: target 324
        // instructions vs base 322, and with the branch destinations excluded
        // the multiset delta is EXACTLY two rows -- one `addi rN,rN,0x6` (the
        // +sizeof(Face) re-point of the second face, which the image material-
        // ises as its own address and we fold into the `sth` displacements) and
        // one `clrlwi rN,rN,16`.  So the image is two LONGER, not us: any gain
        // from here has to add both back (bridge 14), and the remaining 73
        // diff_arg rows are c2 store/trap scheduling around the two divide
        // guards, which w7-ba/w21-t measured against declaration order, the
        // Face field-assignment spelling, the RndRibbon v0..v3 form and the
        // short/cast forms.  No source lever found for "materialise the
        // incremented face pointer".
        //
        // w48-a (2026-11-06): EQUIVALENCE CERTIFICATE for the whole function --
        // 90.96605 canonical / 90.25617 fuzzy, and there is NO behaviour defect.
        // The memory-access multiset (mnemonic, displacement) is IDENTICAL on
        // both sides: target-only {} and base-only {} over the whole body, so
        // every load and store in the image has a counterpart at the same field
        // and the same width.  Face values re-derived from the target listing
        // (0x19c..0x230): face0 = (idx, nextIdx, nextIdxUp), face1 =
        // (nextIdxUp, idxUp, idx) -- the same twelve halfwords our two Set()
        // calls write; only the ORDER of the three independent `sth` inside one
        // face differs.  Both divide guards are present on both sides
        // (`twi 6,rX,0` and `twi 5,rX,-1`); objdiff's two `replace` rows are
        // alignment artefacts of the same pair.
        //
        // The body is TWO INSTRUCTIONS SHORT of the image (1288 B vs 1296 B,
        // 322 vs 324) and both missing ones are IMAGE-ONLY AND DEAD:
        //   * `addi r8, r11, 6` at 0x220 materialising face1's address -- r8 has
        //     no use before its next definition, and the three stores that
        //     follow are all `6/8/10(r11)`;
        //   * a fourth `clrlwi rX,rX,16` (for nextIdx) where the following `sth`
        //     already truncates.
        // Adding either back means shipping a statement with no behaviour, which
        // is fabricated code; the honest number for this row is the one above.
        // The `addi +1 / clrlwi +1` mnemonic-census delta independently
        // reproduces w29-b's finding.
        //
        // REFUSED, do not bank: naming the four face indices as `unsigned short`
        // locals (`uIdx`, `uNextIdx`, `uNextIdxUp`, `uIdxUp`) reads 91.58025
        // canonical / 90.71605 fuzzy -- +0.61 -- with equal rows 264 -> 259 and
        // diff_arg 43 -> 49 at an unchanged body size.  That is the
        // pattern_score_up_while_reproducing_less artefact (w39-g refused the
        // same shape on TessellateMesh); row by row it gains equal at idx
        // 111/118 and loses it at 99/103/104/120, i.e. it moves which neighbour
        // carries the charge.  All 24 declaration orders of the four shorts are
        // byte-identical, so the order is inert; the NAMED SHORT is the lever.
        // Nine further spellings measured inert or worse: `f[0]/f[1]` 86.37,
        // two pointers 86.37, `begin()+faceIdx` 86.37, hoisting the Faces()
        // reference 85.12, two-shorts-only 87.88, `unsigned short nextIdx`
        // 88.35, casts at the call site inert.
        //
        // w-hamribbon (2026-11-09): re-derived on objdiff 4.2.9 and CONFIRMED
        // w48-a exactly -- 90.96605 canonical / 90.25617 fuzzy, 1296 B vs
        // 1288 B, 324 vs 322 instructions, 264 equal / 43 diff_arg / 2 replace
        // / 13 insert / 15 delete.  Nothing has drifted and there is still no
        // behaviour defect: after the two guard rows are paired up the en-bloc
        // multiset delta is again EXACTLY two rows, one `clrlwi` and the
        // `addi r8,r11,0x6`.  The guards are only *scrambled*: the image's
        // `twllei` (idx 129) sits against our `twi 5,r11,-1`, and our `twllei`
        // (idx 124) sits against the image's `twi 5,r4,-1` (idx 139) -- the two
        // traps are identical in both and net to zero.  So "13 inserts + 15
        // deletes means the control flow or a block's contents differ" is
        // FALSE; it is one register-rotation.
        //
        // The mechanism, named (this is the part earlier notes left as "pure
        // scheduler"): the face loop's loop-carried/allocation seed is a single
        // coin-flip.  Ours: seg=r28, hoisted `1-base`(subfic)=r29, `%` mask
        // temp=r30, idx=r27.  Image: seg=r29, `1-base`=r30, idx=r28,
        // nextIdxUp=r27, and the mask temp in VOLATILE r4.  Because the image's
        // temp is r4, `andc r4,r11,r4` cannot fold into the divisor and r11
        // (mNumSides) SURVIVES it -- which is what lets the image emit
        // `add r11,r11,r6` (nextIdxUp) at 0x64, AFTER `twllei`, and what pushes
        // the guard later.  Ours writes `andc r11,r11,r30`, overwriting the
        // divisor in place: "divisor dies here", so nextIdxUp is pulled up to
        // idx 119 and every one of the 22 insert/delete rows plus all 43
        // diff_arg rows from idx 94 on follow from it.  The verts loop carries
        // an independent 2-cycle of the same kind: ours geom=r27, v=r30,
        // vertIdx=r29, boneIdx=r28; image geom=r28, v=r29, vertIdx=r30,
        // boneIdx=r27.  Those 28 diff_arg rows are the same rotation.
        //
        // New levers measured THIS lane, all reverted -- do not repeat:
        //   * moving `int nextIdxUp` out of the declaration group, to just
        //     above the first Set(): BYTE-IDENTICAL.
        //   * `f = &mMesh->Faces()[faceIdx] + 1;` instead of
        //     `&mMesh->Faces()[faceIdx + 1]` -- the specific hypothesis for the
        //     image's dead `addi r8,r11,0x6` (a pointer+1 materialisation).
        //     MSVC folds both to the same 6/8/10(r11) displacements:
        //     BYTE-IDENTICAL.  So the dead addi is scheduling, not a different
        //     spelling, which confirms w48-a's stance by construction.
        //   * swapping the `int vertIdx` / `int boneIdx` declarations in the v
        //     loop: BYTE-IDENTICAL (the allocator is insensitive to declaration
        //     order here -- matches w48-a's 24-order result).
        //   * `int vertIdx = v * mNumSides + vertBase;` (commutative reorder):
        //     BYTE-IDENTICAL.
        //   * `RndMesh::Vert &vert = verts[vertIdx];` then vert.pos/norm/
        //     boneIndices/boneWeights/tex: 83.7 canonical / 82.3 fuzzy, equal
        //     264 -> 228, 343 rows.  Confirms the image genuinely re-derives
        //     `_M_start` FIVE times (`lwz r11,0x100` + `add`), so the
        //     `verts[vertIdx].field` spelling is already correct -- do not
        //     "tidy" it into a reference.
        // Cross-check against og-dc3-decomp's HamRibbon.cpp: it writes
        // `(seg * mNumSides + side) * 2` and three `Faces()[faceIdx].v1/v2/v3`
        // field stores.  The image's `slwi r3,r10,1` hoisted out of the side
        // loop plus the per-iteration `addi r10,r10,1` prove the image uses
        // OUR base-hoisted spelling, so the reference is not an improvement
        // (and the field-store form is already refuted at 85.6/86.37).
        //
        // Conclusion: the DAG is identical on both sides; only MSVC's
        // callee-saved assignment seed differs.  No source lever exists that
        // changes the DAG without making the code worse, so 90.96605 is the
        // honest number for this row.
```
