# Lane notes: `src/system/hamobj/HamSkeletonConverter.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetArm@HamSkeletonConverter@@IAAXW4SkeletonJoint@@000@Z`

`HamSkeletonConverter::SetArm` -- moved verbatim from `src/system/hamobj/HamSkeletonConverter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 301-316):

```text
    // OPEN (w25-gl, w25-gz): fuzzy 99.886; two fmuls/fmsubs rows (dir.y x
    // elbowToHand.x, dir.y x cross1.x) emit their operands the other way.
    // c2 sorts commutative operands by key, and scalar-replaced components
    // compare by sid (C2RS-BRIDGE 8.7).  Ours: dir = (26, 225, 227), eth.y
    // 237, c.y 231, and c.x/c.z/eth.x/eth.z minted adjacent at 242..245, so
    // dir.y sorts below everything.  The image needs dir.z < eth.x < dir.y <
    // eth.z and dir.z < c.x < dir.y < c.z, i.e. dir.y minted between an x and
    // a z component.  The w25-gu SetLeg alias (const Vector3 &d in an explicit
    // cross) moves all of dir's sids above eth/c (229/236/248) and flips row
    // b4 but breaks five others.  Open question: what orders the SR mints.
    // w26-f12: eight more spellings inert at 99.886 -- `const Vector3 &` bindings
    // on dir for each cross separately and for both, on elbowToHand, dir passed
    // through a reference to Normalize, dir and elbowToHand declared up front,
    // every Vector3 declared up front, and named copies of dir.y / cross1.x.
    // A reference to a LOCAL Vector3 folds, so the 8.8 binding lever has no leaf
    // to re-key here; the operands are the scalar-replaced component variables.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.886, 2 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.

## `?SetLeg@HamSkeletonConverter@@IAAXW4SkeletonJoint@@0000PBVBaseSkeleton@@H@Z`

`HamSkeletonConverter::SetLeg` -- moved verbatim from `src/system/hamobj/HamSkeletonConverter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 375-382):

```text
    // Accumulated y, z, x like CalcRotzBone's z, y, x: the image multiplies
    // dir.y*dir2.y first (824C9718 `fmuls f0, f0, f11` off 0x54/0x64), then
    // fmadds the z and x products.  Dot() is x, y, z and comes out z, y, x.
    // w21-at: written with the image's parentheses ((y + z) + x) -- Dot()'s
    // flat sum is ((x + y) + z) natively, so this is also a native fidelity
    // fix.  The operand order inside each product is what reproduces the
    // image's `dir.y, dir2.y` / `dir2.z, dir.z` / `dir2.x, dir.x` rows
    // (MSVC emits these operands reversed); all 8 orders measured.
```

Inside the body (was lines 402-419):

```text
            // One Plane::Set(nx, ny, nz, dist) with the Plane(point, normal)
            // ctor's `d` expression, NOT three member assignments followed by
            // `plane.d = -(...)` on the freshly stored members (89.4 -> 95.3,
            // w7-ba).  The four-float Set stores a/b/c late and lets the merge
            // at .L_824C97FC fall through instead of jumping over the else arm
            // (the image runs 824C97F8 `stfs f0, 0x6c(r1)` straight into
            // .L_824C97FC; the assignment spelling emits a `b` there, plus a
            // dead `addi r11, r25, 0x71` hoisted above it).
            // `plane = Plane(hipRel, pelvisZ)` is the same code plus a 4-word
            // temporary copy (91.2).
            // CLOSED (w21-at): the image's d is -((c*dz + a*dx) + b*dy) --
            // 824C97EC `fmuls f0, c, dz`, `fmadds f0, a, dx, f0`, `fnmadds f0,
            // b, dy, f0` -- a different ASSOCIATION from the flat sum, which
            // MSVC re-sorted to ((y + z) + x) and native evaluates as
            // ((x + y) + z).  Explicit parentheses fix both the 12 offset-swap
            // rows (0x60/0x64/0x68) and the native rounding.  (w7-an had found
            // the flat term orders all byte-identical: /fp:fast re-sorts a flat
            // sum but keeps parentheses.)
```

Inside the body (was lines 445-451):

```text
        // w18-b: the axis is INDEXED at each use, not held in a pointer local.
        // The image loads hz.x through the RotateTowards argument (824C98C0
        // `lfs f0, 0x0(r30)`, r30 = this + (side+0x73)*16) but hz.y/hz.z
        // through a re-derived `this + side*16` (824C98D8 `lfs f13, 0x734(r11)`,
        // `lfs f12, 0x738(r11)`) -- exactly what `(&mLeftHipZAxis)[side].y`
        // lowers to.  A `PaddedJointPos *hipZAxis` local read all three off r30
        // (95.25 -> 99.9).
```

Inside the body (was lines 467-500):

```text
        // Cross(hipZ, dir), NOT Cross(dir, hipZ): the image's y row is
        // hz.z*dir.x - hz.x*dir.z (824C98D0 `fmuls f8, f11, f0` = dir.z*hz.x,
        // 824C98F0 `fmsubs f12, f10, f12, f8` = dir.x*hz.z - that), which is
        // Vec.h's Cross with v1 = hipZ.  The other operand order negates all
        // three rows of mat.y, i.e. mirrors the thigh bone's frame.
        // NEGATIVE RESULT (w7-ba): Cross(*hipZAxis, dir, cross1) reading the
        // member through its conversion operator drops the 0x80 copy's early
        // `stfs f0, 0x80(r1)` and reorders the three fmsubs rows: 94.6 vs 95.3.
        // w18-b: Cross written out with the dir operand first in every product
        // (the image's operand order: `fmuls f8, f11, f0` = dir.z*hz.x,
        // `fmsubs f0, f11, f13, f6` = dir.z*hz.y - ...), same values as
        // Cross(hipZ, dir).  99.9 -> 99.96.  RESIDUAL (w18-b, 99.96): 4 rows,
        // 164/165/168/170 still put the hz operand first; they do not follow
        // source operand order (the Cross(hipZ, dir) spelling leaves the same
        // three hz.y/hz.z products hz-first and flips only the hz.x ones).
        // Cross((&mLeftHipZAxis)[side], dir, ...) is 95.8 and
        // `Vector3 hipZ = (&mLeftHipZAxis)[side];` (one 16-byte copy) is 93.1.
        // w21-at STOP (normalized 100, fuzzy 99.8): once the knee-angle dot
        // is written with the image's parentheses, ALL six cross rows
        // (160/164/165/168/170/172) put the hz operand first, whatever the
        // source operand order -- dir-first, hz-first, Cross(hipZ, dir),
        // three member assignments and a `const Vector3 &d = dir` alias all
        // emit the same rows.  Register-operand order of commutative
        // multiplies only; same values.  The flat Dot() spelling had 4 of
        // these rows but rounds the knee angle differently natively, so the
        // explicit dot stays.
        // CLOSED (w25-gu): c2 sorts each commutative multiply's operands by
        // key, and two scalar-replaced components compare by sid (higher sid
        // first).  `dir` was scalar-replaced early, at the knee-angle dot
        // (sids 17..22), so every hz component (sids 205..209) sorted first.
        // A fresh legDir alone mints its components inside this expression,
        // interleaved with hz's (2 of 6 rows right).  Reading it through the
        // reference `d` defers its scalar replacement past hz's (sids
        // 377..487), so dir comes first in all six products, as in the image.
```
