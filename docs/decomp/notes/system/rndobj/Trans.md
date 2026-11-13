# Lane notes: `src/system/rndobj/Trans.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SyncProperty@RndTransformable@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`

`RndTransformable::SyncProperty` -- moved verbatim from `src/system/rndobj/Trans.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: definition is macro-generated (BEGIN_PROPSYNCS); symbol matched by the name the macro expands to

Inside the body (was lines 103-111):

```text
    // ComputeLocalXfm's argument is mWorldXfm, NOT mLocalXfm. The target forms it as
    // `addi r4, r3, 0x48` off the `this` it just materialised for the call
    // (`subi r3, r28, 0xc4`), i.e. this+0x48 == mWorldXfm; we used to emit
    // `subi r4, r28, 0xbc` == this+0x8 == mLocalXfm. Passing mLocalXfm made the modify
    // self-referential -- mLocalXfm = mLocalXfm * inverse(parentWorld) -- so setting
    // world_xfm through a property never derived the local transform from the value
    // that had just been written.
    // w17-a: the 3-row &mWorldXfm CSE residual this used to carry (99.3) closes when
    // the modify step goes through an inline member (Trans.h); 100, all rows equal.
```


## `?SetLocalRotIndex@RndTransformable@@QAAXHM@Z`

`RndTransformable::SetLocalRotIndex` -- moved verbatim from `src/system/rndobj/Trans.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 593-607):

```text
// w25-oa: 82/83. The one row is the row-z fmuls of the inlined Scale(v68, m50,
// m50) (c2's commutative sort key, C2RS-BRIDGE 8.7). Every operand here is a
// variable leaf, a scalar-replaced element of v68 or m50 keyed 0x10000 | sid << 5,
// and the higher sid goes first. m50.z.z is V197 (0x118a0) and v68.z is V198
// (0x118c0), so v68.z goes first; the image has m50.z.z first, so it needs m50.z.z's sid
// above v68.z's. The z.x/z.y elements (V200/V202) are already above 198, which
// matches. The scalar-replacement pass mints these sids (195-202 plus reused low
// sids 8/10/11/12/16/27). Swapping the v5c/v68 declarations leaves them unchanged.
// Open question: what orders the scalar-replacement mints.
// Also measured (w25-ge, ~230 variants): `Hmx::Matrix3 &out = m50` as both Scale
// matrices fixes row z but inverts all of row x (80/83); a named ref to v68
// inverts row x x and row y x,y (79/83); a distinct matrix or result breaks the
// aliasing (55/83); the hand-expanded Scale reschedules (52/83 in all 48 row
// orders). DEG2RAD * f2, a named angle, mLocalXfm vs LocalXfm() and
// assert/decl order are inert.
```


## `?ApplyDynamicConstraint@RndTransformable@@AAAXXZ`

`RndTransformable::ApplyDynamicConstraint` -- moved verbatim from `src/system/rndobj/Trans.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 682-767):

```text
// RESIDUAL (w7-bf, 95.87871 canonical, 1484 B): ONE register-allocation rotation,
// no source lever found.  The image parks `refWorld` in r30 (0x82646CA8
// `addi r30, r3, 0x48` / 0x82646CB4 `mr r30, r3`); we park it in r29 and shift the
// two &mWorldXfm.m.* address temporaries down with it (0x82646DEC target
// `addi r29, r31, 0x58` vs our r30; 0x82646EBC target `addi r29, r31, 0x58` vs our
// r28).  That rotation is 22 of the 88 register-swap rows, and it drags the FPR
// numbering (f13->f11, f12->f13, f11->f10) and the load SCHEDULE with it: for every
// Add/Subtract in the switch the image interleaves one mLocalXfm/mWorldXfm load with
// one refWorld load per component (0x82646D0C..0x82646D2C: local.z/ref.z/fadds,
// local.y/ref.y/fadds, local.x/ref.x/fadds), while we hoist BOTH refWorld loads off
// the single base register first and add afterwards.  Component evaluation order is
// already right on both sides (z, then y, then x -- MSVC evaluates Set()'s arguments
// right to left), so the fadds/fsubs operand order is NOT the cause; only the
// interleave is, and the interleave follows the base register, not the source.
//
// REFUTED (bit-for-bit, 95.87871 -> 95.87871 with 73 diff_arg instead of 71 and 3
// commutative rows instead of 1, i.e. strictly worse): collapsing the two named
// locals into one, `const Transform &refWorld = (mTarget ? (RndTransformable *)mTarget
// : (RndTransformable *)RndCam::Current())->WorldXfm();`.  Keep the two-local
// spelling below -- it is also the one the image's block layout matches.
//
// The tail `Scale(scaleVec, mWorldXfm.m, mWorldXfm.m)` (0x82647000..0x82647080) is
// the same class: row 0 of the matrix matches instruction for instruction, rows 1 and
// 2 differ only in which of the three components is loaded first and in one
// commutative `fmuls f13, f12, f13` vs `fmuls f13, f13, f12` at 0x82647078.  The
// image's three rows do NOT share a schedule with each other, so no single spelling
// of Scale() reproduces all three.
//
// w7-bn (95.87871 -> 95.87871, 71 diff_arg / 7 insert / 8 delete unchanged): the
// rotation is an allocation ORDER, not a use-count difference -- refWorld has 25 uses
// on both sides once the image's reuse of r30 for &mWorldXfm.m (0x82646BE8,
// 0x82646E30, 0x82646F08, after refWorld is dead) is excluded.  The image ranks
// refWorld first (r30) and gives the two per-case address temps the SAME registers in
// both billboard cases (&m.y -> r29 at 0x82646DEC and 0x82646EBC, &m.z -> r28 at
// 0x82646E58 and 0x82646F10); ours ranks &m.y first (r30 in BillboardZ, r28 in
// BillboardXZ) and splits &m.z (r28 / r29).  The one structural tell is a DEAD
// `addi r11, r31, 0x68` at 0x82646D94 in the BillboardXYZ case -- the image
// materialises the destination address of `mWorldXfm.m.z = refWorld.m.z` and then
// folds the four lwz/stw onto r31 anyway; we never materialise it (the single
// `delete` row outside the FPR schedule).  Refuted, full ninja each: switch cases in
// ascending enum order (95.9, inert -- MSVC lays the blocks out descending 0xc, 0xb,
// 8, 7, 6, 5, 3 whatever the source order); `Vector3 scaleVec` declared before
// refObj/refWorld as RB3 has it (95.9, inert); `mWorldXfm.m[2] = refWorld.m.z`
// through Matrix3::operator[] to force the address temp (95.9 with 73 diff_arg and 3
// commutative rows -- strictly worse, and the FPR schedule did not move).
// w21-y (95.88 -> 99.93 normalized): the "allocation rotation" above FELL to three
// call-site spellings, none of which changes a value:
//   - the three billboard cases build m.y with an explicit `m.y.Set(v.x - ref.x,
//     ...)` instead of Subtract() (rb3-xenon's spelling; 95.88 -> 97.75 alone);
//   - `mWorldXfm.m.z = refWorld.m.z` goes through a block-scoped `Vector3 &z`
//     (rb3-xenon again) -- that is the image's dead `addi r11, r31, 0x68` at
//     0x82646D94 (-> 98.30 with the Set spelling);
//   - the two SkyBox Adds read mLocalXfm.v through a block-scoped const ref
//     (lever (a): bind the sub-object once) -- refWorld moves to r30 and the
//     address temps line up (-> 99.93).
// Left (40 diff_arg, all register/load-order only): each Subtract/Add pair loads
// the refWorld component before the local one (image: local first; same fsubs /
// fadds operands), and the tail Scale() rows (see above).  MEASURED NEGATIVE on
// top of this: const-ref binding mWorldXfm.v or refWorld.v for the Subtracts
// (94.80 / 96.97), non-const `Vector3 &pos = mWorldXfm.v` anywhere (96.27 at
// best), Set spelling for the Adds (97.50), Add(refWorld.v, local, ...) (inert).
//
// w26-f BEHAVIOUR VERDICT: all 40 rows are equivalent. Checked against the target
// listing (name_check, full): the register pairs f13/f0 and f12/f11 swap together
// with their loads, so every fadds/fsubs still sums/subtracts the same two
// operands.  Listing rows 145-147: image `fadds f0,f13,f0` / `lfs f12,0x3c(r31)`
// / `lfs f13,0x34(r30)` vs ours `fadds f0,f0,f13` / `lfs f12,0x34(r30)` /
// `lfs f13,0x3c(r31)` -- the same two addresses, read into the other two
// registers.  Rows 176-179 do the same for the Subtract arm (`fsubs f0,f13,f0`
// vs `fsubs f0,f0,f13`, with 0x80(r31)/0x38(r30) exchanged). The three stores
// and the tail Scale() rows likewise differ only in emission order.
// w26-f MECHANISM (c2rs §8.7, measured with `c2rs stage cands --regasg` on this
// TU, 325 `cqlo3` tuples): the leftovers are the post-lowering commutative sort
// -- one descending stable sort on an operand key.  The operands here are loads,
// and a load from a temp-relative address prints as
// `M6{key}[d00000000 w029f b V<sid>]` with key
// `0x10000 | (base sid & 3) << 14 | 8`.  The keys this function actually prints
// are 0x10008 (29 leaves), 0x12008 (2), 0x14008 (11), 0x16008 (1), 0x1c008 (3)
// and 0x1e008 (3), on bases V18..V1620 -- function-local temp ids, not named
// source objects.  So which arm names which operand first is decided by where
// each load's base temp lands in sid mod 4.  §8.7's only source lever for that
// is "add or drop upstream temps so a base sid moves by one"; every base moves
// together, so it cannot be aimed at the Add arms while the switch prelude and
// the tail Scale() keep the layout they have.  No spellings were spent on it in
// w26-f (every lever already listed above was negative); re-open it if a
// disp-driven leaf key is ever measured.
```

## `RndTransformable::ComputeLocalXfmFromWorld()`

`RndTransformable::ComputeLocalXfmFromWorld` -- moved verbatim from `src/system/rndobj/Trans.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Attribution: no function named `RndTransformable::ComputeLocalXfmFromWorld` in report.json (whole report (header)); filed under the source name

Above the definition (was lines 171-176):

```text
    // w17-a: the world_xfm property's modify step.  An inline member, not
    // `ComputeLocalXfm(mWorldXfm)` written at the sync site: the image forms the
    // argument as `addi r4, r3, 0x48` off the `this` it materialised for the
    // call (subi r3, r28, 0xc4), i.e. this-relative through an inlined callee's
    // own `this`.  Written at the sync site, MSVC folds &mWorldXfm to
    // r28-0x7c and CSEs it with PropSync's member argument (3 rows).
```
