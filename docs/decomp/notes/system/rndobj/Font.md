# Lane notes: `src/system/rndobj/Font.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/rndobj/Font.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

File-level (was lines 16-27; nearest following definition: `KerningTable::KerningTable`):

```text
// w8-e 2026-09-15: ??$PropSync@VRndMat@@@@YA_NAAPAVRndMat@@... (240 B, 0%) is a
// real same-TU gap.  ham_xbox_r.map shows FOUR PropSync<RndMat> instantiations
// in the image, discriminated only by the container they sync:
//   AAV?$ObjPtr@VRndMat@@       824987b0  hamobj:CharFeedback.obj
//   AAPAVRndMat@@  (raw ptr)    82703fd8  rndobj:Font.obj   <-- this row
//   AAV?$ObjPtrVec@VRndMat@@    82707210  rndobj:Font.obj   (we do emit this)
//   AAV?$ObjPtrList@VRndMat@@   827ab310  ui:UIFontImporter.obj
// So the image PropSyncs a BARE `RndMat *` here, not an ObjPtr<RndMat>.  Our
// Font.cpp only ever syncs the ObjPtrVec form, so the raw-pointer overload is
// never instantiated and the row reads 0.0%.  The lever is a PROPERTY that holds
// a plain RndMat* (not an ObjPtr) reached through SyncProperty/PropSync here.
// Not investigated further; left open.
```

File-level (was lines 912-916; no definition follows it):

```text
// w8-k: the raw-pointer PropSync<RndMat> COMDAT at 0x82703FD8 (ham_xbox_r.map,
// rndobj:Font.obj) is an orphan instantiation -- retail inlined its call site and
// still emitted the COMDAT, exactly as Waypoint.cpp:249 documents for
// PropSync<Waypoint>(Waypoint *&, ...).  Explicit instantiation is the lever;
// the ObjPtrVec spelling beside it (0x82707210) already reads 100%.
```


## `?SetKerning@KerningTable@@QAAXABV?$vector@VKernInfo@RndFontBase@@V?$StlNodeAlloc@VKernInfo@RndFontBase@@@stlpmtx_std@@@stlpmtx_std@@PAVRndFontBase@@@Z`

`KerningTable::SetKerning` -- moved verbatim from `src/system/rndobj/Font.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 79-97):

```text
// w15-i1 (99.978 canonical, 2 rows): the image reloads mFirstChar (lhz 0x0) then
// mSecondChar (lhz 0x2) for the table index; we load them the other way round.
// Byte-inert: xor is commutative and the result is masked by 0x1F, so both
// orders compute the same index.
// Mechanism (c2-rs §8.7): this is the COMMUTATIVE OPERAND-ORDER key, not a
// register-assignment row.  Both operands arrive at the last sort (cqlo3) as
// folded single-use temps of the same shape, and that sort is descending by key
// and stable, so their order is fixed by the key of the tuple each temp was
// folded out of.  Measured inert (one rebuild each, all exactly 90/92 rows):
// swapping the two TableIndex arguments, inlining the xor in both operand
// orders, naming mFirstChar first, naming mSecondChar first, and naming both
// before the `Key` call.  That is §8.3's finding -- c1 canonicalises the
// operand list of a commutative tuple in the IL, before the second driver ever
// sorts it, so the two temps keep the mint order c1 gave them.  Flipping this
// needs a change of IL NODE for one operand (a different expression producing
// it), not a change of operand position.
// w16-a: re-subscripting `info[i].mSecondChar, info[i].mFirstChar` for the
// table index (instead of curInfo) does not change the load order and costs a
// callee-saved renumbering (97.8).
```


## `?SetCharInfo@RndFont@@IAAXPAUCharInfo@1@AAVRndBitmap@@ABVVector2@@H@Z`

`RndFont::SetCharInfo` -- moved verbatim from `src/system/rndobj/Font.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 745-772):

```text
// w7-al (95.9 canonical then, 95.6 at w26-b): 34 rows, and 24 of them are one
// consistent callee-saved permutation.  The image ranks the nine live values
// cursor > left > right > info > this > bmap > top > bottom > pos (r31..r23);
// we get left > right > cursor > info > top > bottom > this > pos > bmap.
// Every instruction is otherwise in the same order with the same opcode.
// Mechanism (c2-rs candidate tap, fn 368): this is COLOR's pop order, which is
// priority descending then tie descending.  Our worklist pops
// id13(pri 21, sym 11/k04), id7(pri 21, sym 7/k04), id10(pri 18, sym 9/k04),
// id14(pri 17, sym 11/k04 second live range), id11(pri 12, sym 9), id8(pri 10,
// sym 7) -- i.e. the two live ranges of each of the three scan variables sit
// next to each other, which is why `left`/`right` outrank `cursor`.  Priority
// is refs_in_block x pressure, so closing this means moving references between
// basic blocks (§6 lever 2), not reordering declarations.  Merging the two
// cursors into one (which is what the image does) moved the structure but not
// the ranking.  The last rows are the scheduler placing bmap.Width()'s `lhz`
// before the cursor's `extsw` instead of between the `fcfid` and the `frsp`,
// which duplicates one `frsp` and one `lfs pos.x` out of the cross-jumped tail.
// NOT a template mismatch: objdiff's call diff names the image's `_M_find`
// instantiated over `pair<const unsigned short, RndFont::CharInfo>` where ours
// is over `RndFont3d::CharInfo *`.  Checked in the target object directly: the
// image loads a POINTER out of the map node at 0x14 and dereferences 0x10/0x20/
// 0x30 through it, so its map value is a pointer too; the name is the linker's
// ICF survivor (every unsigned-short-keyed `_M_find` folds to one body).  The
// `MakeString` in the trailing MILO_ASSERT differs the same way and is the same
// kind of artefact: the target object contains only
// MakeString<char const[19], int, char const[5]> and passes it the 21-byte
// "info->charWidth >= 0" literal and the 9-byte "Font.cpp" literal, which no
// single translation unit can type-check -- it is a merged COMDAT name.
```

Inside the body (was lines 781-788):

```text
        // w26-z (2026-11-02): declaration order is measured inert on this row.
        // `int col;` declared before the bounds, `left/right` ahead of
        // `top/bottom`, and `top/bottom` ahead of `left/right` are all exactly
        // 95.600. The residual is the nonvolatile-GPR priority order (image:
        // cursor > left > right > info > this > bmap > top > bottom > pos;
        // ours: left > right > cursor > info > top > bottom > this > pos >
        // bmap), i.e. priority = refs-in-block x local pressure, together with
        // the stack-slot shaping of the two cross-jumped tails.
```

Inside the body (was lines 798-806):

```text
        // The first compare in each scan is against the RAW bound, not the
        // cursor (0x82703688 `cmpw r30, r29` is left vs right, while the loop
        // latch at 0x827036C4 is the cursor vs right).
        // BEHAVIOURAL FIX (w12-a): ColumnNonTransparent is re-called for every
        // column -- the latch `bne cr6, .L_82703690` at 0x827036C8 jumps back to
        // the argument setup before the `bl` at 0x827036A4 (same at 0x8270372C
        // for the right scan).  The previous spelling evaluated it ONCE into a
        // temp, so a transparent first column walked the cursor straight to
        // the far bound without testing any other column.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.
