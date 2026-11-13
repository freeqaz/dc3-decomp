# Lane notes: `src/system/world/Spotlight.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SyncProperty@Spotlight@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`

`Spotlight::SyncProperty` -- moved verbatim from `src/system/world/Spotlight.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Attribution: definition is macro-generated (BEGIN_PROPSYNCS); symbol matched by the name the macro expands to

Inside the body (was lines 222-249):

```text
    // NOT a bug, despite how the inlined code reads. SetIntensity ->
    // SetColorIntensity(Color(), f) expands to `mColorOwner->mColor =
    // mColorOwner->mColor`, and the target really does emit that 16-byte
    // self-copy. Two rewrites were measured and are both WORSE, so leave it:
    //   SetColorIntensity(Color(), _val.Float())  -> 8 rows becomes 15
    //   binding `const Hmx::Color &c = Color();` inside SetIntensity
    //                                             -> 8 rows becomes 9 (adds a spill)
    // The 8 residual rows are all anchoring: the target keeps mColorOwner in
    // one register for the destination and mColorOwner+0x1b0 in another for the
    // source, plus two dead address computations; our build CSEs them into a
    // single anchor. Hmx::Color has no user-declared operator= in the target
    // (`??4Color@Hmx@@` appears in 0 of the target objects), so that is not it.
    //   SetColorIntensity(Hmx::Color(Color()), _val.Float()) -- RB3's spelling
    //                                             -> 8 rows becomes 25, 98.9,
    //                                                +0x10 stack frame
    // Decoded fully at 8282EDD0: the image emits exactly two instructions we do
    // not -- `mr r9, r11` (saving mColorOwner before the +0x1b0 clobbers it) and
    // a DEAD `addi r10, r9, 0x1b0` that its stores then fold into their own
    // displacements. That dead address computation is the tell: the image is
    // materialising BOTH sides of `mColorOwner->mColor = c` as pointers, the way
    // MSVC lowers an operator= call, while we lower it as one anchored struct
    // copy. 4764 B at 99.8287 == those two instructions and nothing else.
    // w26-g canonical decomposition: 204 of 119100 units = two DELETES (100
    // each, the `mr r9, r11` and the dead `addi r10, r9, 0x1b0`) plus four
    // IMMEDIATE anchoring rows (1 each: the stores are anchored 0x1b0(r9) in
    // the image and 0x0(r11) here -- the SAME effective addresses, a different
    // base register). So the "8 rows" is exactly two instructions; every
    // register row is forgiven by the canonical ruler.
```


## `?DrawShowing@Spotlight@@UAAXXZ`

`Spotlight::DrawShowing` -- moved verbatim from `src/system/world/Spotlight.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 518-574):

```text
    // w7-bo (2026-09-15): 97.56 -> 97.60 canonical, 21 mismatch rows -> 8.
    // Lever: SIBLING block scopes for `c` and `tracker`.  The image gives
    // `tracker` the SAME frame word as `c` -- it builds the colour at r31+0x60
    // (82828D08..D18, target idx 87 `addi r9, r31, 0x60`) and then passes
    // r31+0x60 to ??0RndEnvironTracker (82828D44, target idx 92 `addi r3, r31,
    // 0x60`) and to ??1RndEnvironTracker (82828FB0).  With `c` at function
    // scope our build put it at 0x60 and `tracker` at 0xa0; that one extra word
    // shifted `_at` 0x70->0x80 and the Sphere 0x80->0x90, i.e. all fourteen
    // [off:-16] diff_arg rows plus a frame Δ +0x10.
    // Measured negatives:
    //   - `c` alone in its own closing scope: byte-identical, 97.56 both ways
    //     (the earlier note here claimed the reuse was therefore unobtainable;
    //     it is not -- `tracker` has to be scoped TOO, so the two scopes are
    //     siblings and MSVC coalesces the slots).
    // Residual (8 rows) is a store-scheduling group around the tracker ctor --
    // the image sinks `stw r8, 0x50(r31)` past the two `stw`s our build emits
    // first, and hoists `addi r8, r10, 0x13c` (the RndEnviron vtable/field
    // pointer) above them:
    //   [95]  insert   stw  r7, 0x4(r9)        [96]  insert   stw  r6, 0x8(r9)
    //   [102] delete   stw  r8, 0x0(r9)        [103] delete   stw  r7, 0x4(r9)
    //   [104] delete   addi r8, r10, 0x13c     [105] diff_arg stw [reg:r6->r8, off:-8]
    //   [109] replace  stw r8, 0x50(r31) vs addi r8, r10, 0x13c
    //   [117] insert   stw  r8, 0x50(r31)
    // Same instruction MULTISET on both sides -- only the schedule differs.
    // Target spends r8 on `addi r8, r10, 0x13c` at idx 104, which forces
    // `stw r8, 0x0(r9)` ahead of it at 102 and the whole c-copy lands in
    // address order 0x0/0x4/0x8/0xc; our build keeps r8 holding the 0x1b0
    // word longer, fires 0x4/0x8 as soon as their loads retire (95/96), and
    // pushes the `addi`+home store past the fmuls.  Pure MSVC store
    // scheduling driven by r8's live range.
    // w26-x re-verified against the image: the eight rows are the same
    // instruction MULTISET on both sides with the same values (`stw r8,
    // 0x50(r31)` carries the same `addi r8, r10, 0x13c` result, just 8 slots
    // later), so there is no behaviour difference -- this is c2 store
    // scheduling, and no spelling tried in w26-x moved it.  Left at 97.605.
    // Further measured negatives, both 97.60 / 8 rows, byte-for-byte the same
    // eight rows:
    //   - `Hmx::Color c = Color();` copy-initialisation instead of direct-init
    //   - `UpdateTransforms();` moved INSIDE the scope (the RB3 sibling's
    //     shape -- rb3 Spotlight.cpp puts UpdateTransforms, c48 and tracker in
    //     one block; that block shape is what we had at 97.56/21 rows, so the
    //     sibling-scope split is the DC3-specific lever, not a port of rb3).
    // w27-f (97.60507 -> 98.53986 canonical, 97.58696 -> 98.25000 fuzzy, 8
    // rows -> 15 cheaper ones, one instruction SHORTER): C2RS-BRIDGE 8.8 on
    // the colour source.  `Color()` returns `const Hmx::Color &`
    // (mColorOwner->mColor), so binding it to a named reference makes the
    // copy-constructor's four loads hang off a distinct IL leaf instead of
    // the whole `Color()` call being re-evaluated at the temporary; the
    // c-copy's store group then re-shapes.  This is the 8.8 "bind, do not
    // copy" rule, and it is the first spelling in three waves to move this
    // row.  Same lever REFUTED here in its other forms (all exactly the old
    // 97.60507): naming the intensity, binding the intensity, binding
    // `mColorOwner` (97.59), binding the destination `c` before
    // SetAmbientColor, and `Multiply(src, Intensity(), c)` (91.83, it stops
    // reading the same object).  Residual is now pure register naming on the
    // copy scratch set (ours r7/r8/r9, image r8/r9/r10) plus the same two
    // early stores; no value, call, branch or store is missing.
```


## `?SetColorIntensity@Spotlight@@QAAXABVColor@Hmx@@M@Z`

`Spotlight::SetColorIntensity` -- moved verbatim from `src/system/world/Spotlight.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 758-771):

```text
// FLOOR for ?SetColor@Spotlight@@IAAXH@Z: 94.091 canonical (w8-h).  SetColor's
// whole 8-byte shortfall is two instructions inside THIS body after inlining:
// the image keeps `mr r11, r9` (82827E14) and a DEAD `addi r9, r11, 0x1b0`
// (82827E24); MSVC drops that address temp when it inlines into SetColor, where
// `color` is a non-escaping local.  Standalone this function is already 100.0%
// and DOES emit the same dead addi (82827D90), so the source shape is right and
// only the inliner differs.  MEASURED NEGATIVE (w8-h): binding the destination
// here -- `Hmx::Color &dst = mColorOwner->mColor; dst = c;` -- leaves SetColor
// at 94.091 unchanged and drops Spotlight::SyncProperty 99.829 -> 99.744.
// Reverted.  Call-site respellings were already refuted by the note at ~222.
// w-field-call (2026-11-13, behaviour re-read): the sweep's "target-only
// `lfs f12, 0x1c0(r9)`" in SetColor is an ALIGNMENT artifact -- ours reads the
// same mIntensity one row later (`lfs f12, 0x1c0(r10)`), before the colour
// stores on both sides.  Same fields, same order, same values; not a bug.
```


## `?Poll@Spotlight@@UAAXXZ`

`Spotlight::Poll` -- moved verbatim from `src/system/world/Spotlight.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 977-988):

```text
        // w13-c: 97.389 -> 100.  Poll() is entered through the RndPollable
        // sub-object (r31 = this + 0x100); naming the full object once is what
        // makes the image materialise `subi r29, r31, 0x100` a single time
        // (82829758) and pass r29 to CalculateDirection / DoFloorSpot /
        // UpdateTransforms / CheckFloorSpotTransform / UpdateSlaves and load
        // mTarget through it (8282975C `lwz r30, 0x2e8(r29)`); without it we
        // re-derived the pointer at each call and saved one callee-save fewer
        // (frame 0xa0 vs 0xb0).  The `(int)` on the null test reproduces the
        // image's signed `cmpwi cr6, r30, 0x0` (82829768) -- an equality test
        // against zero either way (docs/decomp/patterns/fixable-casting.md,
        // "Signed Pointer Comparison Cast").  The trailing UpdateTransforms
        // stays on `this`: the image re-derives `subi r3, r31, 0x100` there.
```


## `?UpdateTransforms@Spotlight@@IAAXXZ`

`Spotlight::UpdateTransforms` -- moved verbatim from `src/system/world/Spotlight.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 1069-1073):

```text
        // w22-hx (port of rb3-xenon e7bbfb0f8): a cone keeps the identity
        // rotation, anything else takes `rot`.  DC3 image 0x828284D8 (objdiff rows 284-288):
        // `lbz r11, 0x1f8(r30)` (mIsCone); `mr r4, r28` (ident, initialised
        // first); `bne` keeps it; else `mr r4, r29` (rot).  The arms were
        // swapped here -- normalized scored the swap as a register permutation.
```


## `?BuildBeam@Spotlight@@IAAXAAUBeamDef@1@@Z`

`Spotlight::BuildBeam` -- moved verbatim from `src/system/world/Spotlight.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 1123-1217):

```text
// w7-bs (2026-09-15): 70.20 -> 85.3 canonical, 1628 B, three levers on top of
// w7-bo's two, each measured alone:
//  - pos.Set(x, y, z) per column (73.9): the image writes each column's pos
//    with ONE verts.begin() reload (0x8282D988 `lwz r9, 0(r30)` / `add` /
//    `stfs 0x8/0x0/0x4(r9)`); three pos.x/y/z statements reloaded begin
//    three times.
//  - the RB3-shaped index family `int c0 = 0; c0 += 4; c1..n3 = c0+1..c0+7`
//    with verts[c0..c3] (80.3): the image's vertex byte cursor is
//    0x60*(4i+2) (`li r27, 0xc0` at 0x8282D908, +0x180 at 0x8282DCE0) and
//    its face-index IV starts at exactly 6 (`mr r11, r28` at 0x8282D904,
//    sharing the resize divisor) -- both are MSVC re-biases of a c0 family.
//    `verts[i*4+k]` never produces a biased cursor: MSVC folds k*0x60 into
//    the displacement instead. The old `short s = 6; s += 4` was the
//    re-biased IV spelled as the source variable, which forced extsh.
//  - faces[i*6+k] instead of a separate `fi += 6` (85.3): the image's face
//    cursor r10 = 6*fi is UNbiased with folded displacements 0x0..0x22
//    (0x8282DB1C..0x8282DC54); a separate int fi re-biases to fi+3
//    (`li r6, 0x12`) and derives the other bases.
//
// MEASURED NEGATIVES, each alone from the 85.3 state:
//  - RB3's two-level derivation n1 = n0+1, n2 = n0+2, n3 = n0+3: 77.7.
//  - n0..n3 declared inside the `i != totalSections-1` block: INERT.
//  - `int c0 = i * 4;` as an expression instead of the += 4 IV: INERT (85.3,
//    128 vs 137 diff_arg rows, same structure).
//  - RB3's clamp `numSectionsTop = 4; if (rawTop > 4) numSectionsTop =
//    rawTop;`: 78.9 (gives `ble; mr`, the image is `mr r14, r11; bgt; li
//    r14, 4` at 0x8282D7BC-0x8282D7C8).
//  - `rawTop > 4 ? rawTop : 4` for the top clamp only: matches those four
//    rows but the extra live value cascades through the loop, 80.8.
//  - w7-bo's `int s` + `unsigned short c0..n3` re-measured from the pos.Set
//    state: 71.8, and the listing shows WHY it loses -- MSVC re-biases to c1
//    and applies the 16-bit modular addend to the 32-bit start (`lis r21, 1;
//    ori r21, r21, 1` = 0x10001), which is what proves the image's IV starts
//    at a plain 6 from a base of 0.
//
// RESIDUAL (77 register rows + ~30 structural): the face-index family's
// representative. Ours re-biases to c2 (`li r10, 0x2`; c0/c1 via two
// hoisted 0xfffe/0xffff addends); the image's is n2 = 4i+6 with SIX hoisted
// 16-bit addends r20-r25 (0x8282D90C-0x8282D920). Whatever rule picks the
// representative, it is not use count (c1/c2 tie at 11, n2 has 6), not
// declaration order or scope (INERT above), and not two-level derivation
// (77.7). The image also keeps lVar31 as its own IV (`neg r15, r14` at
// 0x8282D900, `addi r15, r15, 1` at 0x8282DCDC) where ours folds it into
// `add r9, r18, r23`; the two together are what push the image to
// r14-r31 and spill `this` to its home slot (0x8282D710 `stw r3, 0x144(r1)`,
// reloaded at 0x8282DCF0).
//
// w7-bo (2026-09-15, superseded above): 68.77 -> 70.20 canonical. Two levers, each
// measured alone: a plain counted `for` over totalSections (the image guards
// with `cmplwi cr6, r16, 0x0` at 0x8282D898 and then `mtctr r16` at
// 0x8282D8D4 -- our `int count` do/while cost a second IV), and inverting the
// halfWidth step so the BOTTOM arm is the fall-through (0x8282DCC4 is
// `cmplw cr6, r18, r14` / `blt cr6, <top arm>`; that closed the one blt/bge
// diff_op).
//
// MEASURED NEGATIVES, each alone from the 70.20 state:
//  - `unsigned short` for c0..n3 (69.60). This is what the image's TYPE is --
//    every index is materialised with `clrlwi ...,16` (0x8282DAF4) and
//    RndMesh::Face's members are unsigned short -- and it does remove the
//    frame-size delta and move the prologue from r16-r31 to r15-r31. It
//    scores lower because MSVC then rebiases the `s` induction variable from
//    4i+6 to 4i+1, so it hoists ONE modular addend (`addis r9,r21,1` /
//    `subi r9,r9,1` = +0xffff) where the image hoists SIX
//    (`ori r20, r7, 0xfffa` .. `ori r25, r8, 0xfffd`, 0x8282D90C-0x8282D920).
//  - `unsigned short s` as well (69.50). The image's `s` is an untruncated
//    32-bit IV (`addi r11, r11, 0x4` at 0x8282DCE4), so this is wrong anyway.
//  - `int s` + `short c0..c3` (69.80).
//  - spelling the addends `s + (unsigned short)-6` to force the modular
//    constants: MSVC folds it straight back to `s - 6`, byte-identical object.
//  - inverting the even/odd face arms to `if (i & 1)`: INERT (kept, because it
//    is what 0x8282DAEC `clrlwi. r9, r18, 31` / `beq .L_8282DBAC` reads as).
//  - declaring bottomSideBorderVal before verts/faces: INERT.
//
// RESIDUAL is a register-pressure cascade, not a row: the image saves
// r14-r31 + f26-f31 (18 GPR / 6 FPR), we save r16-r31 + f25-f31 (16 / 7). It
// spills `this` to 0x144(r1) (0x8282D710) so r27 can carry a vertex BYTE
// cursor biased +0xc0 (two Verts) and derive three more bases per iteration
// (`subi r8, r27, 0xc0` / `subi r7, r27, 0x60` / `addi r6, r27, 0x60` at
// 0x8282D98C/0x8282D9A4/0x8282D9AC); ours runs one cursor at bias 0 and folds
// k*0x60 into the store displacements instead. It also keeps lVar31 as its own
// IV (`neg r15, r14`, `addi r15, r15, 0x1` at 0x8282DCDC) where MSVC
// eliminates ours into `i - numSectionsTop`.
//
// The MakeString name difference under name_check is the known per-TU ICF
// alias, NOT a wrong string: the target's own relocations at 0x8282D730 and
// 0x8282D734 name "Spotlight.cpp" (_0O@ = 14) and
// "!SpotlightDrawer::DrawNGSpotligh..." (_0CF@ = 37), which is exactly the
// instantiation we emit.
// w21-d (2026-10-03): still 85.342 canonical / 83.179 fuzzy (137 diff_arg,
// 25 insert, 29 delete, 9 replace).  The image's face-index IV is n2 = c0+6
// with six hoisted 16-bit addends (0xfffa/0xfffe/0xffff/0xfffb/...); tried
// spelling the family off that representative directly (`int s = 6; c0 =
// s-6 .. n3 = s+1; s += 4`): 66.16, MSVC re-derives everything from s and
// loses the vertex cursor.  `unsigned int` for the whole c0..n3 family: 84.49.
// n0/n1/n3 derived from n2 (`n2 = c0+6; n0 = n2-2 ...`): 82.03.  Reverted.
```

Inside the body (was lines 1228-1233):

```text
    // w22-a03: `* (1.0f / 15.0f)`, not `/ 15.0f`.  The image multiplies by the
    // reciprocal (0x8282D774 `lis r11, __real@3d888889` / 0x8282D7AC `fmuls
    // f13, f13, f0` and 0x8282D7CC `fmuls f0, f31, f0`, before each fctiwz):
    // /fp:fast folds the constant division.  Same Xbox bytes either way; the
    // native build (no fast-math) otherwise truly divides, and a quotient just
    // under an integer can truncate to a different section count.
```

Inside the body (was lines 1281-1285):

```text
            // w29-c: same lever as BuildCone's borderRadius -- the image's
            // `fmadds f30, f4, f1, f29` (0x8282D9C0) puts the border
            // difference first and `yFrac` second.  Binding the difference by
            // reference keeps it out of the single-use subexpression class and
            // restores that order: fuzzy 83.17936 -> 83.20393.
```


## `?BuildCone@Spotlight@@IAAXAAUBeamDef@1@@Z`

`Spotlight::BuildCone` -- moved verbatim from `src/system/world/Spotlight.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 1372-1430):

```text
// w7-bs (2026-09-15): 91.76 -> 92.6 canonical (fuzzy 89.66 -> 91.19), 1140 B,
// 292 rows: 81 diff_arg / 3 diff_op / 11 replace / 7 insert / 7 delete. Two
// levers, both behaviourally identical to what was here (same reads, same
// stores, same order):
//  - the two IN-LOOP length uses read `def.mLength` (`borderY / def.mLength`
//    for verts[s-1].tex.y, `def.mLength` as verts[s+15].pos.y) like the tail
//    ones already did. The image reloads `lfs 0x8(r29)` inside the loop and
//    re-divides; with this our callee-saved FPR set is f23-f31 EXACTLY
//    (`__savefpr_23` / `__restfpr_23` both sides, vs _21 before). w7-bo's
//    "61.5" for this lever alone is real but is NOT a regression of the loop
//    body: alone it collapses the three vertex rows onto one cursor at verts[s]
//    with folded displacements (-0xc00/-0x630/-0x60) while the image keeps one
//    cursor at verts[s-1] (`li r28, 0x600` 8282D3A8) and DERIVES the top and
//    bottom rows (`subi r10, r28, 0x600` 8282D3EC / `addi r9, r28, 0x600`
//    8282D40C), so every store row mis-pairs. Measured alone from HEAD: 60.4.
//  - `unsigned int top = s - 17; mid = s - 1; bot = s + 15;` index locals for
//    the vertex rows only (faces keep the inline expressions). This gives MSVC a
//    byte-offset register per row like the image (rows 100-160 now mostly
//    equal: `add r11, r11, r2x` after each `lwz r11, 0(r31)` verts.begin
//    reload), which is what turns the 60.4 into 92.6.
//
// MEASURED NEGATIVES, each from the state above:
//  - `unsigned short s` loop variable (the image's arithmetic is 16-bit modular
//    off r30: s-1 = `add r8, r30, r25` 8282D514 with r25 = 0xffff hoisted at
//    8282D3AC; s-17 = `addis r10, r30, 1; subi r10, r10, 0x11` 8282D50C): 74.6.
//  - one `mid` index local with `verts[mid - 16]` / `verts[mid + 16]` for the
//    other rows, aiming at the image's derived-cursor form: 61.8 (336 rows,
//    51 insert / 53 delete -- MSVC folds it back to the single-cursor shape).
//  - `RndMesh::Vert *mid = &verts[s - 1]; top = mid - 16; bot = mid + 16;`
//    pointer locals: 78.9 (loses the per-Set `lwz r11, 0(r31)` reload the image
//    does before EVERY Set on all three rows).
//  - w7-bo's `unsigned short` face-index locals: 47.3 (their measurement).
//
// RESIDUAL (all 109 non-equal rows accounted for):
//  - ours keeps THREE induction variables for the vertex rows (`addi r27/r26/
//    r25, +0x60` at the loop bottom, rows 200-204) where the image keeps ONE
//    (`addi r28, r28, 0x60` 8282D578) and derives the other two; prologue is
//    therefore r21-r31 (`__savegprlr_21`) vs the image's r23-r31 (8282D284),
//    frame 0x100 vs 0xf0, and the loop-carried GPRs relabel (r24<->r26 etc.,
//    45 rows).
//  - the face-index arithmetic and loop test are rebuilt around an i-biased
//    face base (r30) and a 0x10000 bias register (r23, `subf r4, r23, r10`
//    loop test vs the image's `subi r6, r30, 0x11; cmplwi r6, 0xf` 8282D584):
//    face args become `addi r10, r30, 0x10/0x11/0x1/0x20/0x21` and the sth
//    displacements -0x6/-0x2/-0x4 instead of 0xe/0x10/0xc (rows 161-206).
//  - mid row pos store order: image 0x0, 0x4, 0x8 (f13, f29, f11); ours
//    0x8, 0x0, 0x4 (rows 123-125). Not source-controlled -- explicit z,x,y
//    scalar stores schedule identically to Set() (see Text.cpp SetupCharacter).
//  - tail rows 212-215: verts[15]/verts[31] first-write store order swapped.
// Nothing here reads or writes anything the image does not; do not chase the
// score with a semantic change.
//
// w7-bo (2026-09-15, superseded above): 90.22 -> 91.83 canonical. The two TAIL
// uses of the length read `def.mLength` fresh -- the image reloads it at
// 0x8282D6xx for verts[31].tex (`lfs f0, 0x8(r29)` / `fdivs f0, f29, f0`,
// target idx 257-258) and again for verts[47].pos.y (`lfs f13, 0x8(r29)`, idx
// 263). The duplicated verts[15]/verts[31] writes below are REAL: the image
// writes both twice, the second verts[31] differing only in tex.y (1.0 vs
// borderY/mLength).
```

Inside the body (was lines 1449-1456):

```text
    // w29-c: C2RS-BRIDGE 8.8 reference binding.  The image contracts this as
    // `fmadds f28, f0, f11, f12` -- the `borderY/len` quotient is the
    // MULTIPLICAND and the radius difference the multiplier (0x8282D3C4).
    // Written as one expression both factors are single-use subexpressions and
    // the emitted order follows the difference, not the quotient.  Binding the
    // difference by reference keeps it out of the single-use subexpression
    // class (a plain named `float` is folded away and is byte-inert), which
    // restores the image's operand order: fuzzy 92.85965 -> 92.89474.
```

Inside the body (was lines 1470-1497):

```text
        // w27-j: the three vertex indices anchor on `bot`, and the other two are
        // DERIVED from it by a constant.  c2 strength-reduces one index and
        // expresses the others as +/-k element offsets; written as three
        // independent `s +/- k` locals it instead builds three separate
        // induction variables, which costs two extra callee-saved GPRs
        // (__savegprlr_21 vs the image's _23), a 0x10-larger frame, and the
        // whole r24<->r26 cascade.  Anchoring on `bot` gives ONE induction
        // variable and closes the prologue.  Measured 2026-11-02, canonical /
        // fuzzy: three independent `s +/- k` 92.59649 / 91.19299; anchor on
        // `top` 92.78245 / 92.23859; anchor on `mid` 92.78245 / 92.23859;
        // anchor on `bot` 93.29825 / 92.85965 (kept).  Declaration ORDER is
        // inert (all six permutations byte-identical); it is the derivation
        // that matters.
        //
        // w45-a (2026-11-06): name the face index `i = s - 17` and express the
        // four face corners in terms of it.  Written as `s - 17` / `s - 1` /
        // `s + 15` / `s + 16` the same value is reached by FOUR different
        // expressions, so c2 carries `s - 17` as a biased induction variable
        // (0x10000 bias, `addi r9,r30,0x11` to recover s) and rebuilds every
        // corner from it, which is the whole 161-203 row cluster plus the
        // r24<->r26 register cascade.  With one named `i` it keeps a single
        // value and derives the corners by +1/+16/+17/+32/+33, matching the
        // image's derived-corner form.  Measured canonical / fuzzy:
        // 93.29825 / 92.89474 -> 95.07719 / 93.84912; object 1132 -> 1140 B
        // (the image's size).  Negative control: declaring the same `i` but
        // leaving the face arguments in `s` form is byte-identical to the old
        // build (93.29825 / 92.89474), so the gain is the corner expressions,
        // not the extra declaration.
```


## `?BuildNGCone@Spotlight@@IAAXAAUBeamDef@1@H@Z`

`Spotlight::BuildNGCone` -- moved verbatim from `src/system/world/Spotlight.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 1551-1583):

```text
// w7-bj (2026-09-14): 70.76 -> 77.78 canonical. Two behavioural fixes against
// the image (csAngle reset per segment at 0x8282C510; cap faces at [4n+seg] /
// [5n+seg], 0x8282C4B8/0x8282C7AC) plus two shape levers (own divisor temp for
// segU, accumulator Multiply in the v==2 arm). Residual is a register/slot
// cascade, not a single row: the image keeps halfAngle (0x50) and flip (0x54)
// memory-resident, hoists only 8 matrix elements (x.x reloaded from 0xb0) and
// gives f23/f24 to uvV/segU, spills numVerts to 0x80 and rematerialises it as
// r29-2, and walks ONE vertex byte cursor (r30) across segments where we get an
// extra outer IV (+0x120). Refuted here, each measured alone: capBase/topBase
// declared after faces.resize (71.99), before the loop (72.52); arm-1 Set as
// three explicit y/x/z stores (74.60, re-indexes verts per store); orientMtx
// declared first (inert, slots do not follow declaration order); halfStep
// declared inside the seg loop (inert); accumulator statement order X,Z,Y
// (inert). Do not respell the face indices as short/unsigned short -- the
// image's cur/nextRow arithmetic is untruncated int (add r6,r7,r11 at
// 0x8282C6D4) and the wave-1 archaeology pass already measured that regression.
// w21-d (2026-10-03): still 78.967 canonical / 75.080 fuzzy, 466 rows (224
// diff_arg, 39 insert, 43 delete). Behaviour re-checked row by row against
// the target listing and FAITHFUL: side vertices (x=r*cosH, z=r*sinH), the
// cross-section arm, tex (segU at +0x40, 0.5*v at +0x44), both winding arms
// of the face loop (tail-merged `sth 0x8`), cap faces (baseIdx-2,
// baseIdx+sideWidth-2, numVerts) / (baseIdx+sideWidth, baseIdx, numVerts+1).
// Structural residual: the image keeps ONE vertex byte cursor r30 across
// segments (we derive a per-segment IV r15 += 0x120), recomputes nextRow as
// (sideWidth-1)+cur each pass (we strength-reduce it into its own IV), keeps
// halfAngle memory-resident at 0x50 and spills numVerts at 0x80.  Measured
// here, all inert or worse: capBase/topBase/baseVertIdx after faces.resize
// (78.28); baseVertIdx at its use (77.74); nextRow spelled cur+sideWidth-1 /
// sideWidth-1+cur / cur+(sideWidth-1) (inert, canonicalised); short or
// unsigned short for cur (75.87/76.56), nextRow (77.56), baseIdx (78.12),
// sideWidth (78.78); `unsigned v` hoisted out of the seg loop (inert);
// unsigned iVert (inert); int v (78.70); verts[iVert++] (78.96); iVert++ in
// the for-increment (inert).
```

Inside the body (was lines 1602-1610):

```text
    // MEASURED NEGATIVE (w8-q): the target holds orientMtx at frame 0xb0 and
    // identMtx at 0xe0; we hold them the other way round (16 SWAPPED slots,
    // every `stfs` in the identity init carries off:-48). Hoisting
    // `Hmx::Matrix3 orientMtx;` above identMtx -- so declaration order matches
    // the target's slot order -- is BYTE-INERT: 79.0% canonical and the same
    // 318 mismatch rows before and after. MSVC/Xenon assigns these slots by
    // liveness/first-use, not by declaration order, and all three matrices have
    // identical type, size and alignment, so nothing in the declaration list
    // discriminates them.
```

Inside the body (was lines 1702-1729):

```text
            // ONE flip variable, not two (w8-q, 77.766 -> 79.0 canonical). The
            // image reads the flip counter from its stack home, tests bit 0 and
            // writes back the incremented value in three adjacent instructions
            // at 0x8282C6C8/C6CC/C6D0/C6D8 -- `lwz r8,0x54(r1)`,
            // `clrlwi. r6,r8,31`, `addi r8,r8,1`, `stw r8,0x54(r1)` -- i.e. ONE
            // object read-tested-incremented, not a `flip = curFlip + 1` /
            // `curFlip = flip` pair. The old two-variable spelling kept the copy
            // live in r18 and forced a genuine spill in the else arm
            // (`sth r10,0x50(r1)` / `lhz r3,0x50(r1)`), which is now gone.
            //
            // w46-b (2026-11-06): the INCREMENT belongs INSIDE the test.
            // Written as `if (flip & 1) { ... } flip++;` MSVC re-reads the
            // counter at the end of the body (`lwz r10,0x54(r1)`, `addi`,
            // `stw`) -- one extra load that the image does not have, and its
            // absence is what re-keys the whole loop. `if (flip++ & 1)` emits
            // the image's exact four-instruction run (load, `clrlwi.`,
            // `addi`, `stw`) before the branch. A post-increment tests the
            // pre-increment value, so the winding sequence is unchanged;
            // checked store by store against the target listing: both arms
            // still write the same six face indices per pair. Measured
            // canonical 78.96722 -> 80.14052, fuzzy 75.07963 -> 76.26464;
            // equal rows 148 -> 152, inserts 39 -> 36, deletes 43 -> 41 over
            // the same 427-instruction image.
            // Residual note: the ELSE arm still spills a face index
            // (`sth r10,0x50(r1)` / `lhz r3,0x50(r1)`) where the image uses
            // `mr r6, r5`. That is the per-segment vertex IV's register
            // pressure (r15 += 0x120 once a segment, versus the image's
            // r30 += 0x60 once a vertex), not the flip counter.
```


## `?BuildNGSheet@Spotlight@@IAAXAAUBeamDef@1@@Z`

`Spotlight::BuildNGSheet` -- moved verbatim from `src/system/world/Spotlight.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 1833-1837):

```text
            // w21-bi: the position goes through the same inline Multiply as
            // the normal. The image's pos and norm blocks (0x8282CBE4..,
            // 0x8282CC48..) are the same 15 instructions, loads z, x, y; the
            // hand-expanded sum this replaced scheduled x, y, z and kept both
            // blocks off by 15 rows each.
```

Inside the body (was lines 1852-1860):

```text
    // w21-bi (96.30 -> 100 normalized, 1 register-only row left: the
    // commutative `add r9, r4, r3` forming base, operand order inert to
    // spelling). The w7-bw negative results recorded here were refuted:
    // (1) plain-int indices DO match once base is `row * numCols + col`
    //     rather than a hand-carried rowStart; (2) the pos/norm sums are not
    //     a lowering floor -- the pos transform is the same inline Multiply
    //     as the normal (above). (3) still stands: the two MakeString rows
    //     are the target's whole-TU ICF representative
    //     MakeString<char[19], int, char[5]> vs our <char[14], int, char[19]>.
```

Inside the body (was lines 1864-1886):

```text
            // w21-bi: the vertex index is `row * numCols + col`, all four
            // indices plain ints (as in RB3). The image keeps row*numCols as
            // its own induction variable (seeded AFTER the numSections guard,
            // `add r4, r4, r27` per row) and re-adds col every column
            // (`add r9, r4, r3`); a hand-carried `rowStart` let MSVC merge the
            // two into one IV (the 94.4 an earlier lane measured), which the
            // old `unsigned short base` cast only papered over. The u16 clrlwi
            // pattern in the arms comes from Set()'s int parameters, not from
            // casts here (inert either way, measured).
            // w25-rl stop note (c2rs tap): `add r9, r3, r4` lists col (named
            // local, id 34, sym 36) before the row*numCols induction variable
            // (strength-reduction temp, id 111, sym 111); the image lists the
            // IV first. Both are colour candidates, and the order is not the
            // higher-id-first rule seen elsewhere, so it is set when strength
            // reduction rebuilds the add. Inert (9 more spellings): operand
            // and factor order, `numSegments + 1`, a per-column rowBase local,
            // an outer `row`; a per-row rowBase merges the IVs (+2 insns).
            // w26-f14: also inert (exact, per-object + patch chain) -- col-first,
            // `row * numCols; base += col;` across a statement boundary, a
            // per-column rowStart, an explicit int cast, and `numCols * row`.
            // The tap does not expose this add as a sortable `CQT cqlo3` tuple,
            // consistent with the strength-reduction rebuild (0x10b84869) setting
            // the order after the last lowered sort.
```


## `?BuildNGQuad@Spotlight@@IAAXAAUBeamDef@1@W4Constraint@RndTransformable@@@Z`

`Spotlight::BuildNGQuad` -- moved verbatim from `src/system/world/Spotlight.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 1911-1942):

```text
// w48-a (2026-11-06): EQUIVALENCE CERTIFICATE for Spotlight::BuildNGQuad --
// 90.63071 canonical / 87.33195 fuzzy, NO behaviour defect.  Placed here, after
// this file's last MILO_ASSERT, so no assert id or static ordinal can move.
//   1. VERTEX LOOP: the only instruction-selection difference is that the image
//      holds -1.0f in f9 and emits `fmadds f4,f4,f9,f1` where we emit
//      `fsubs f4,f31,f4` after three `fmuls`.  Confirmed structurally: the
//      target loads FOUR float literals in the loop preheader (0x138..0x14c)
//      and we load THREE (0x134..0x140); the missing one is -1.0f.  The values
//      are identical -- b * (-1.0f) is an exact sign flip, so a + b*(-1.0f) and
//      a - b are the same IEEE operation -- and this is the ONLY memory-multiset
//      difference in the function (`('lfs','0')` target-only x1, `('sth','2')`
//      base-only x1).
//   2. FACE LOOP: the image runs 5+5 stores with ONE SHARED tail where we run
//      6+6.  Adjudicated store by store: `bt 2` branches on `(iFace & 2) == 0`,
//      so 0x2f0 is the IF arm and 0x320 the ELSE arm (it reads backwards until
//      the register roles are followed).  IF f0 = (uBaseN, uPrev, base+n),
//      f1 = (base+n, uPrev, -); ELSE f0 = (uPrev, base, uBaseN),
//      f1 = (uBaseN, base, -).  The shared instruction is `sth r30,0x2(r11)` at
//      0x348, reached by both arms with r30 holding uPrev on the IF path and
//      base on the ELSE path -- exactly the two values the two Set() calls put
//      there.  Same twelve halfwords either way.
//   3. BuildCone's corner-derivation lever does NOT transfer to this row.
//      BuildCone wrote four corners as four expressions over a BIASED induction
//      variable; here `base`, `base+n` and `base+n-1` already derive from one
//      named `base`.  Naming every corner (with `unsigned short` corners
//      87.18 / body 952 B; ints only 88.24 / body 940 B) lets c1 fold more
//      away and SHORTENS the body, which is the wrong direction outright.
//   Eight spellings measured, none a gain: row-wise `rot.x/y/z.Set` 90.03,
//   function-static const matrix 72.37 (guard machinery), file-scope const
//   matrix 80.70, int literals in `rot.Set` exactly inert, `rowFrac`/`colFrac`
//   scoped into their loops exactly inert, `norm.Set` hoisted above the pos
//   Multiply 85.08.
```

Inside the body (was lines 1946-1951):

```text
    // w29-c: the reference bindings follow the image's emission order.  Both
    // `Verts()` and `Faces()` fold to one load of 0x148(mesh) plus an `addi`,
    // and the image emits the verts `addi r31, r11, 0x100` first and the faces
    // `addi r29, r11, 0x110` second (0x8282CE98 / 0x8282CE9C).  Declaring
    // `verts` first reproduces that; `faces` first (the previous order) emits
    // the pair the other way round.
```

Inside the body (was lines 1971-1998):

```text
    // SURVEY 2026-09-14 (w7-ae), 88.1% canonical, 145 mismatch rows, no edit.
    // The pos matrix-multiply block (diff rows 113-127) is structurally IDENTICAL
    // to the image, term for term and store for store: both sides load y,z,x,
    // both multiply by the ZERO elements rather than folding them away, both
    // associate the three-term dot product left to right, and both store z,y,x in
    // that order.  Exactly ONE row differs, and it is instruction selection for
    // the -1 element:
    //     target  .L_82684f80  fmadds f4, f4, f9, f1     (f9 = -1.0, hoisted from
    //                                                     __real@bf800000 at
    //                                                     .L_82684edc/.L_82684ee4,
    //                                                     BEFORE the loop)
    //     ours    0x10fc8      fsubs  f5, f2, f5
    // i.e. the image carries the nine matrix elements in REGISTERS across the
    // whole loop (f0 = 0.0, f13 = 1.0, f9 = -1.0) and therefore multiplies by a
    // register, while our build still knows the multiplier is literally -1.0 at
    // the multiply site and strength-reduces `a + b * -1.0f` to `a - b`.  That
    // one choice is what forces the image to hold TWO callee-saved FPRs where we
    // hold one (`stfd f30`/`stfd f31` vs `stfd f31`) and one extra GPR
    // (`bl __savegprlr_22` vs `__savegprlr_23`), which is the whole reported
    // frame delta of -0x10 and nearly all 21 register-swap pairs -- so the single
    // fsubs row is worth ~12pp of renaming behind it.
    // (w7-bw correction: the store order does NOT already match -- the image
    // stores z,y,x (stfs 0x8/0x4/0x0 at 8282D048/50/58) and ours stores x,z,y,
    // and the x' / y' sums associate differently under /fp:fast.  Same root:
    // the literal -1 is visible at our multiply site and not at the image's.)
    // NOT a spelling of Multiply(): what would have to change is whether MSVC can see the
    // literal at the multiply, and no value-preserving source form of a
    // Matrix3 built from literals was found that hides it.  Recorded, not fixed.
```

Inside the body (was lines 2026-2062):

```text
    // RESIDUAL (w7-bw, 90.62 canonical, face loop 8282D100..8282D1C0): ours
    // computes base + n once at the loop top as the next IV value and then
    // derives uPrev from it as (0xffff - n) + (base + n); the image adds
    // 0xffff to base directly (8282D13C) and forms base + n inside each
    // branch (8282D150 / 8282D190), feeding the IV update from that register
    // (mr r11, r8 at 8282D1B0).  Refuted, each measured whole-function:
    // explicit `int base = row + 1` + `col++, base += n` gives two bottom-
    // updated IVs and a down-counted outer loop (88.5, one more GPR saved);
    // `(unsigned short)base - 1` for uPrev is inert; the RB3 ibase form
    // (uPrev = ibase, uBase = ibase + 1, ...) makes ibase the IV and derives
    // uBase from uBaseN + (1 - n) instead (88.6); a hoisted `int nm1 = n - 1`
    // is inert; unsigned short locals for all four values pre-computed
    // before the branch is the 88.4 state.  The rot lowering in the vertex
    // loop (fmadds f9 vs fsubs, and the store order z,y,x vs x,z,y) is in
    // the off-limits math header's Multiply and is the ae-recorded floor.
    // w20-p: branch-landing rows 207/224 re-adjudicated value by value --
    // ARTIFACT (shared [2] store tail in the image, loop-top base+n in ours);
    // all six halfwords per quad carry the same values on both arms.
    // w26-r2 read the image at instruction level (8282D150..8282D1AC) and
    // that verdict holds.  The if-arm (8282D150) writes face0's three indices
    // and TWO of face1's, then jumps to 8282D1A8; the else-arm (8282D180)
    // writes face0's three and face1's outer two and falls through to the SAME
    // 8282D1A8.  The shared instruction is `sth r30, 0x2(r11)` -- face1's
    // middle index -- and r30 holds uPrev on the if-arm (8282D158 clrlwi of
    // base-1) and base on the else-arm (8282D188 clrlwi of r11), exactly the
    // two values the two Set() calls put there.  So the image runs 6+5 stores
    // and ours 6+6 with identical values: a CSE of the common store, not a
    // missing one.  That is the 35-row cluster at diff row 180.
    //
    // w26-r2 also re-measured the vertex-loop row: the -1.0f element is not
    // hideable from source.  `float negOne = -1.0f;` before the Set and
    // `float negOne = -(float)1.0f;` both compile to byte-identical code
    // (90.62241, unchanged); building the positive matrix and negating one
    // element is 89.19, and two non-const locals (`zero - one`) is 88.95.
    // The image's `fmadds f4, f4, f9, f1` at 8282F80 multiplies by a register
    // because the literal's live range is spilt by a wider one, not because
    // the literal is spelled differently.
```

w-low-world (lane B, 2026-11-13): face-loop arms swapped to `if (!(iFace & 2))
{ else-arm } else { if-arm }` -- same twelve halfwords, same condition, only the
block order changes.  90.63071 -> 91.4 canonical, fuzzy 87.33195 -> 88.12033;
equal 131 -> 131, insert/delete 11/10 -> 10/9, total rows 252 -> 251 (target
251-ish), one new beq/bne diff_op at the branch itself.  Found by decomp-synth
`brpol_0` (hill_climb); its FACT_AGREED validator flagged 5/6, so it was
re-applied by hand and checked arm by arm against the listing.
