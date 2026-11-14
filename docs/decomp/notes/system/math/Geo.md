# Lane notes: `src/system/math/Geo.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

File-level (was lines 16-36; nearest following definition: `NumNodes`):

```text
// w8-e 2026-09-15: ??$?6VVector2@@V?$StlNodeAlloc@VVector2@@...TextStream (176 B,
// 0%) is a real same-TU gap.  ham_xbox_r.map contributes it from `math:Geo.obj`
// at 825346e0, and it is the TEXT-stream overload -- the BinStream twin of the
// same template lives at 826d4508 in rndobj:MeshAnim.obj, so the two are
// genuinely different instantiations and this one belongs here.  The generic
// body is src/system/utl/TextStream.h:37,
//   template <class T, class Allocator>
//   TextStream &operator<<(TextStream &, const std::vector<T, Allocator> &);
// and our Geo.cpp does not include TextStream.h at all, so nothing instantiates
// it and the row reads 0.0%.  The lever is a real `ts << someVector<Vector2>`
// use in this TU (a Print/Dump path), not an explicit instantiation -- adding
// the include alone emits nothing.
//
// w8-g 2026-09-15 CORRECTION: the last sentence is REFUTED.  An explicit
// instantiation does emit it and closes the row (0% -> 100%, measured by full
// ninja).  The reason it looked impossible is that the real blocker was one
// missing declaration: `TextStream &operator<<(TextStream &, const Vector2 &)`
// is DEFINED in Rot.cpp (100%, ham_xbox_r.map -> math:Rot.obj) beside its
// Vector3 twin but was never declared in math/Vec.h, so the element-wise body of
// the vector<> inserter could not compile in any TU.  Declaration added to
// Vec.h; the instantiation is at the bottom of this file.
```


## `?Clamp@Box@@QAA_NAAVVector3@@@Z`

`Box::Clamp` -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 142-151):

```text
    // w26-f5 (fuzzy 99.5, canonical 100.0): the two charged rows are the two
    // `or`s folding the three ClampEq flags.  The image emits `or r10, r9, r10`
    // then `or r11, r10, r11` -- ((x|y)|z) with the partial in src1; we emit
    // ((z|y)|x).  c1 evaluates the three calls x,y,z on both sides, so the only
    // difference is the order c2 re-emits the flattened chain in (C2RS-BRIDGE
    // 8.7), and source associativity is inert: explicit left- and right-nested
    // parens and `(bool)` casts each measured byte-identical.  A named
    // accumulator, `|=`, int flags and three named bools all change the emitted
    // shape (canonical 77.5-92.0).  The flags are branch-minted, so their sids
    // follow the clamp order in both builds; nothing tried moved the sort.
```


## `?Set@Plane@@QAAXABVVector3@@00@Z`

`Plane::Set` -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 190-194):

```text
// w17-c (99.887, 21 of 53 rows: the two Subtracts' load order and the cross
// store order y,z,x): a non-PCH probe sweep of all 72 explicit per-component
// Subtract orders (either difference first) x 6 explicit cross statement
// orders found nothing under 154/5300 against 161 for this spelling; the
// cross statement order is fully inert.
```

p2-b99 (2026-11-14, name_check, 99-100 band audit): NOT A BUG. The three cross-product components stored to the stack are identical expressions on both sides. The differing f6-f8 at the Normalize call are dead leftovers, not arguments.


## `?Intersect@@YAXABVTransform@@ABVPlane@@AAVRay@Hmx@@@Z`

`Intersect` -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 374-387):

```text
// w21-ac (91.51 -> 100 canonical): the test reads the two components back out of
// ray.dir after storing them (image 0x82534E04: fabs f9 of the stored dir.x
// register, the fmr of dir.y), which is what put the dotZ fmadds after the
// dir stores -- the residual an earlier lane called allocator scheduling.
// Every combination of ray.dir.N / dotN in the test and the two divisors was
// measured (16): only the test through ray.dir with the divisors through the
// locals reaches it.  Same values natively: ray.dir holds exactly dotX/dotY.
// The three dots are written in the image's association, (y + z) + x (read
// off the fmuls/fmadds chain: n.y*m.N.y + n.z*m.N.z, then n.x*m.N.x), so
// the native build evaluates them in the image's order (fidelity fix); on PPC
// that spelling costs one more commutative-operand row than Dot() (canonical
// 100 either way, modulo register permutation; fuzzy 99.72 vs 99.86 in the
// probe) -- the two rows left are `fmadds f13,f8,f6` / `fmadds f12,f8,f3`
// (n.x * m.N.x with the factors swapped), inert under all 8 operand orders.
```


## `?Intersect@@YA_NABVTriangle@@ABVBox@@@Z`

`Intersect` -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 511-524):

```text
    // NEGATIVE RESULT (component grouping is right, vertex grouping is not).
    // The image's fadds order is v1y, v1z, v2z, v2y, which reads like the three
    // vertices are declared as units; spelling it that way -- v0x/v0y/v0z,
    // v1x/v1y/v1z, v2x/v2z/v2y -- REGRESSES 87.30 -> 82.61 canonical (18 inserts
    // and 18 deletes instead of 13 and 13), because it also splits the single
    // twelve-load block the image emits before the first fsubs.  Likewise the
    // commutative spelling of the two x adds is inert: writing them origin-first
    // to match `fadds f10, f11, f0` at Geo.s idx 13 leaves rows 13 and 21
    // unchanged, so MSVC canonicalises the operand order here.
    // w7-bt: the untranslated vertices ARE units, but as Vector3 locals built by
    // Add(), not as grouped scalars.  MSVC folds the two locals into registers
    // (no 16-byte copy -- their source is computed, not foreign memory) and the
    // twelve-load block then comes out in the image's order: v1y, v1z, v2z, v2y
    // (Geo.s 0x82536780..0x825367C8), closing rows 26-33/40 of the old diff.
```

Inside the body (was lines 539-549):

```text
    // Per-axis separation test: the triangle's [min, max] extent along the axis
    // against the box's half-extent.  w7-bt (87.32 -> 98.37 canonical): the
    // extent is a Vector2 AGGREGATE (x = min, y = max), not two scalars.  As
    // scalars, `mx` is a single-use temporary and MSVC forward-substitutes its
    // whole fsubs/fsel tree into the second `||` arm, past the `bgt` on mn
    // (rows 63-70 of the old diff); the image computes both fsels before the
    // first compare (Geo.s 0x825367FC..0x82536808, then `fcmpu f13, f22; bgt`
    // at 0x8253680C).  A field store is not a temporary, so it stays put.  The
    // ternary spelling, Utl.h Min/Max, a MinMax3(float&, float&) helper and a
    // function-scope `float mn, mx` reassigned per block were all byte-identical
    // to each other and all sunk.
```

Inside the body (was lines 580-587):

```text
    // Edge cross product axes (9 tests)
    // RESIDUAL (w7-bt): the scheduler's order of these nine fsubs and of the
    // 27 axis stores.  The image computes e0x, e0z, e1z, e0y, e1x, e1y, e2x,
    // e2y, e2z (0x825368B8..0x825368F8); we compute e0x, e0z, e1y, e1z, e1x,
    // e0y, ... and the `lis` of __real@0 lands one row later.  Declaring the
    // deltas in the image's order, or ordering the Set() calls by the image's
    // first-store order (0x90, 0xc4, 0xa0, 0x68, 0x78), reshuffles the block
    // without closing it (both 98.4, raw 96.9 / 97.1).
```

Inside the body (was lines 614-633):

```text
        // The image negates each loaded component IN PLACE (`fneg f11, f11` at
        // 0x8253698C) and RELOADS all three for the projections (`lfs f10,
        // -0x4(r11)` / `lfs f8, 0x4(r11)` / `lfs f6, 0x0(r11)` at 0x825369B0/
        // BC/C4), so the abs loads and the projection loads were never
        // value-numbered together.  w7-bt: that only happens when the two
        // groups reach the axis through DIFFERENT address families -- here the
        // loop-carried float pointer for the abs test and the i-indexed
        // `axes[i]` for the projections.  Reading both groups through pfAxis,
        // both through axes[i], through `&axes[i].y` derived from i, through a
        // Vector3& cast from pfAxis, or through two loop-carried pointers
        // (float* and Vector3*, which MSVC coalesces) all merge the loads and
        // emit three `fmr` copies instead (93.9-94.0 canonical).  A Vector3
        // copy of axes[i] survives as a real copy (89.8); advancing pfAxis
        // between the groups splits the IV into two registers (94.8); putting
        // axes and radii in one aggregate does reload, but as a barrier AFTER
        // the radius store (92.0), where the image's reloads precede it.
        // RESIDUAL: this spelling costs a fourth induction register (the image
        // walks r9 = i, r10 = radii, r11 = &axes[0].y; we add `addi r8, r1,
        // 0xf0` / `addi r8, r8, 4` for the radii pointer and base the abs
        // family at 0x68 instead of 0x64), which is the 2 extra rows at 98.37.
```


## `?Intersect@@YA_NABVSegment@@ABVTriangle@@_NAAM@Z`

`Intersect` -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 661-677):

```text
// w29-a (2026-11-02): PERMUTATION CLASS -- the RESULT is equal. 108
// instructions / 432 B on BOTH sides and no diff_op row; the mnemonic census
// differs only in disassembler notation (`ble` vs `bf`, `subi` vs `addi`) and
// branch-label forms. What is left is 3 COMMUTATIVE_OP_ORDER rows (idx 36, 57,
// 68), 3 OFFSET_SWAP rows, and 4 insert / 4 delete in 4 clusters -- every
// cluster a re-ordering of the SAME opcodes, i.e. the scheduler's order of the
// nine fsubs and the commutative operand pairing. The previous lanes' stop
// notes for idx 36-38 (commutative canonicalisation) and idx 47-58 (scheduler
// order) name exactly these; nothing new to try. Left as-is.
// w47-a (2026-11-06): independent certificate for that verdict. OPCODE,
// DISPLACEMENT and RELOCATION-SYMBOL multisets are all EQUAL (108 instructions
// on each side, 432 B = 432 B), so nothing observable differs -- this is a
// permutation, not a defect.  Seven more spellings this wave, all INERT at
// 92.52778: `const float &` and plain-copy bindings of the tempDot frame
// components, `const float &` bindings of tri.origin.x/y and of seg.start.x/y,
// `Set()` in place of `Scale()`, and swapping the x/y translate statements.
// Binding dotXX/dotYY's shared frame.y costs 0.58 (91.94444).
```

Inside the body (was lines 740-744):

```text
    // w22-a10: explicit ((x + y) + z) association, not Dot(). The image
    // (idx 81-88) builds both projections as fx.x*hx, fmadds fx.y*hy, then
    // fmadds fx.z*hz; under /fp:fast Dot()'s flat sum was re-sorted to
    // (x + z) + y (`fmadds f10, f28, f11, f10` = fx.z*hz first), a different
    // rounding. Native (left-to-right) already had this association.
```

w-low-ham-math (2026-11-13), lane D:

```text
NOT A BUG, independently re-derived against 0x82534E68: normal = frame.z (0x30..0x38);
|n.d| < 1e-4 or (b && n.d > 0) rejects; t = -(n.(start - origin))/(n.d) is
stored to out and must lie in [0, 1]; the hit point minus origin is projected
onto frame.x/frame.y (0x10/0x20) with the Gram terms dotXX/dotYY/dotXY; then
k = (dY*XY - dX*YY)*inv and j = (dX*XY - dY*XX)*inv with k in [0, 1], j >= 0
and j + k <= 1. Every operand matches.
```

## `?GrowToContain@Sphere@@QAAXABV1@@Z`

`Sphere::GrowToContain` -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 892-899):

```text
        // w26-f5: binding invDist as a reference moves the commutative
        // operand sort (C2RS-BRIDGE 8.7) for the three Scale products below
        // and for one of the dir products: 5 charged rows -> 3, fuzzy
        // 99.462 -> 99.677, canonical still 100.0.  The three left (the x and
        // y `dx*inv` products and the y Scale product) do not respond to
        // binding the source components, naming the products, reordering the
        // Vector3 declarations or re-spelling the Set as three component
        // stores -- all measured inert.  See the lane note for the residual.
```


## `BehindPlane()`

`BehindPlane` -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Attribution: no function named `BehindPlane` in report.json (unit); filed under the source name

Above the definition (was lines 943-953):

```text
// w15-i2 2026-10-02: 98.280 -> 100.0 (modulo three commutative fmadds operand
// swaps).  The image's shape is a short-circuit `||` over six inline
// bool-returning predicates: each step materialises its bool in r11
// (`li r11,1 / blt / li r11,0 / clrlwi.`) and the chain's own result is
// normalised once at the tail (0x82535D90 `li r11,0 / beq / li r11,1 /
// clrlwi r3,r11,24`).  The negated radius is computed ONCE (`fneg f11` at the
// top) and handed to every predicate -- spelling the predicate as
// math/Mtx.h's `s < plane` recomputes -s.GetRadius() per operand (91.4), and a
// bare `Dot(...) < negR || ...` chain never materialises the bools (72.2).
// The w8-o note's nested if-chain (98.28) reproduced every step but not the
// tail, because its literal returns compile to `li r3 / beqlr`.
```


## `??O@YA_NABVSphere@@ABVFrustum@@@Z`

`operator>` -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 960-965):

```text
    // w26-f5 a binding per plane leaves only the `top` Dot's a*centre.x pair in
    // the wrong operand order: five references occupy five consecutive symbol
    // ids, and the image needs the four plane-first instances (back, left,
    // right, top) in the low band with `bottom` above it.  Leaving `f.front`
    // inline frees the first slot, so the five bindings land where the image's
    // keys put them.  References only -- behaviour unchanged.
```


## `?Intersect@@YA_NABVSegment@@PBVBSPNode@@AAMAAVPlane@@@Z`

`Intersect` -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 1029-1033):

```text
    // w25-gq: the named `plane` and `end` references are what put a*end.x in
    // the image's operand order (`fmadds f0, f10, f6, f0`).  c2 sorts the
    // multiplicands by key (C2RS-BRIDGE 8.7); end.x is a memory leaf on a
    // lowering temp base, keyed by that temp's sid mod 4, and the two extra
    // locals shift it below plane.a's key.  Either reference alone is inert.
```

Inside the body (was lines 1052-1059):

```text
        // w17-c: this tail is an `else` block.  The assert's int temp and t2
        // share frame slot 0x50 in the image, which needs t2 in a scope
        // disjoint from the assert's (stack-slot-sharing.md); at function
        // scope t2 enclosed it and the temp was pushed to 0x54.
        // `t2` must be declared BEFORE denom: it owns the lowest local slot (0x50)
        // in the image, and both alternatives cost ~1pp -- declaring it after `frac`
        // and hoisting it above startDot each give 99.0 canonical (16 f28<->f29
        // swaps plus a moved `stfs`) against 99.99 for this order.
```


## `?Intersect@@YA_NABVVector3@@0ABVTriangle@@AAM@Z`

`Intersect` -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 1115-1124):

```text
    // Moller-Trumbore using shifted views into the frame matrix:
    // e1 = {fx.y, fx.z, fy.x} (elements 1-3) and e2 = {fy.z, fz.x, fz.y} (elements 5-7)
    // w26-f5 (fuzzy 98.784, canonical 100.0): all nine charged rows are
    // multiplicand order, and in every one the image emits the FIRST-written
    // factor first (`fmuls f12, f8, f9` for e2.x*v2.y, etc.).  Swapping the
    // factors in source is byte-identical -- c2 sorts each commutative use list
    // by key, so source order only survives a tie -- and naming every e1/e2/v2
    // component regresses canonical to 81.0 because it re-hoists the twelve-load
    // block.  The residual sits on the memory-leaf base temp, whose sort key is
    // fixed by that temp's sid (C2RS-BRIDGE 8.7), not by the spelling.
```


## `?Update@BSPFace@@QAAXXZ`

`BSPFace::Update` -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 1190-1200):

```text
// w17-c (99.966): in the area loop all 64 operand orders of the six products
// are inert.
// w22-a26: the image's association is (A + C) + B, not (A + B) + C, with
// A = anchor x prev, B = prev x curr, C = curr x anchor (0x82536B2C.. area
// loop: `fmsubs f10` = A, `fmsubs f11` = C, `fmsubs f13` = B, then
// `fadds f12, f10, f11` / `fadds f13, f12, f13`).  MSVC /fp:fast re-sorted
// the flat A + B + C into that order on its own, so the old spelling matched
// the Xbox bytes but native (no fast-math) summed (A + B) + C.  The explicit
// grouping keeps the image's order on both builds (99.966 -> 99.98 normalized,
// fuzzy 99.448 -> 99.690).  Swapping the edge plane's (y, z) pair in `d`
// below is inert (measured).
```

Inside the body (was lines 1243-1250):

```text
            // Statement order y, z, x is MEASURED, not stylistic: all six
            // permutations were built and scored (w9-e 2026-09-30), norm/fuzzy --
            //   YZX 99.9655 / 99.4483   YXZ 99.9655 / 99.4483
            //   ZYX 99.9425 / 98.9655   ZXY 99.9425 / 98.9655
            //   XZY 99.9310 / 99.1264   XYZ 99.9310 / 99.1264
            // It sets the load order of the zAxis triple (the image loads
            // 0x0(r30), 0x8(r30), 0x4(r30) -- x, z, y) and the store order of
            // `normal`.
```

p2-b99 (2026-11-14, name_check, 99-100 band audit): NOT A BUG. The plane d term is -(n.x*p.x + n.y*p.y + n.z*p.z) on both sides; only the association order differs.


## `?MakeBSPTree@@YA_NAAPAVBSPNode@@AAV?$list@VBSPFace@@V?$StlNodeAlloc@VBSPFace@@@stlpmtx_std@@@stlpmtx_std@@H@Z`

`MakeBSPTree` -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 1285-1324):

```text
// The residual here is now the remaining register-swap cascade, not the
// addressing mode.
//
// NEGATIVE RESULT: rotating the inner plane loop to
// `if (planeIt != end) do { ... } while (++planeIt != end);` REGRESSES
// 90.2 -> 90.0.  MSVC still emits the `b` to the bottom test and additionally
// drops one home store.  Reverted.
//
// w7-bt (90.92701 -> 99.02676 canonical, 172 -> 50 rows).  Five source
// facts read off Geo.s, in order of weight:
//  1. The tail is three early returns and NO explicit clear(): the five
//     `clear` calls (0x82538A9C/AA4/ACC/AD8/AE8) are the two list
//     destructors on three return paths, tail-merged onto one frontFaces
//     clear with the result in r30.  Explicit clears were seven calls.
//  2. `totalFaces = faces.size()`: list::size() is distance(begin(), end())
//     and its const-ref `__first` homes the begin() temporary at 0x60
//     (0x82538568) before walking a register copy of it.
//  3. The candidate iterator is FUNCTION-SCOPE and ASSIGNED in the for-init
//     (`it = faces.begin()`), the same variable the split loop reuses.  That
//     is what rotates the planes loop into the image's top-tested shape
//     (0x825385C8 `cmplw r25, r10` / 0x825385CC `beq`, then the hoisted end
//     in r20) and homes each `it->` deref at 0x60 (0x825385C0/C4/D0).  A
//     for-scoped iterator, or a function-scoped one COPY-initialised, is the
//     unrotated `b`-to-bottom form at 98.09; the OLD negative above rotated
//     the loop by hand and lost on the home stores.  Making planeIt
//     function-scope the same way is 98.6: it gets its own slot and grows
//     the frame by 0x10.
//  4. The five per-plane counters are zeroed BEFORE `cmpwi r24, 1`
//     (0x825385D8..E8), so they are declared above the single-face test.
//  5. `const Plane &plane = *planeIt` inside the coplanar arm is the
//     `stw r28, 0x60(r31)` at 0x8253863C; the FRONT power is the named pow.
// Also read off the image and kept: the split arm advances `it` only inside
// the size() > 2 branch (see the note there).
// RESIDUAL (w7-bt): the assignment's own home store (ours has one more
// `stw r11, 0x60` after the size() loop, the image has one more `it->` home
// before `addi r20`), the `cmplwi cr6, r11, 0` on `front` scheduled above
// the backArea fadds at 0x82538680 (an if/else respelling is byte-identical),
// and the callee-saved naming (faces r23/it r22, planeIt r25, the counters)
// -- all register rows the ruler forgives.  Byte-inert probes: `(*it).planes`
// for `it->planes`, post-increment `it++`.
```

Inside the body (was lines 1449-1459):

```text
            // The split arm works on `it` itself and only advances it inside
            // the size() > 2 branch (the splice's `it++`, 0x82538A18..A24).
            // w7-bt: the image has NO advance on the `ble` path at
            // 0x82538A08 -> 0x82538A6C -> 0x82538A74 (`cmplw r30, r23` with
            // r30 untouched), so a face whose back half clips to <= 2 points
            // stays in `faces` and is re-examined on the next pass with its
            // polygon already cut to the back side, where OnSide no longer
            // reports it in front and the back-only / erase arms take it.
            // A `cur = it++` hoisted to the top of the arm (the old spelling)
            // skipped that pass and left the degenerate face behind in
            // `faces` instead.
```

Inside the body (was lines 1478-1484):

```text
    // No explicit clear(): the five `clear` calls in the image are the two
    // lists' destructors (backFaces at 0x58 first, frontFaces at 0x68 second,
    // reverse declaration order) on three return paths, of which MSVC
    // cross-jumps the last two onto one `frontFaces` clear with the result
    // parked in r30 (`mr r30, r18` at 0x82538AD0 / `li r30, 1` at
    // 0x82538AEC).  Explicit clears plus the destructors were seven calls
    // and a `bool ok` -- the largest of the five w7-bt levers in the note above.
```


## `SubV2()`

`SubV2` -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Attribution: no function named `SubV2` in report.json (unit); filed under the source name

Above the definition (was lines 1554-1558):

```text
// w22-a09: rb3-xenon's Clip shape (98.39 there).  Stands in for a Vector2
// subtraction the original read `.y` off (math/Vec.h has no Vector2 operator-);
// the y-differences go through it and MSVC homes the temporary's y in the
// image's 0x54 slot.  Numerically identical to the plain spelling (the two
// products and their sum are the same, fp add commutes).  96.09 -> 98.4.
```


## `?Clip@@YAXABVPolygon@Hmx@@ABVRay@2@AAV12@@Z`

`Clip` -- moved verbatim from `src/system/math/Geo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 1581-1618):

```text
    // LEAD for the next lane (w8-o 2026-09-30, 96.094, 512 B target vs 492 B
    // base -- FIVE instructions the image has and we do not, and they are all
    // DEAD HOME STORES of these two loop-carried variables).  r31+0x50 holds
    // lastPoint and r31+0x54 holds lastDot; both sides store 0x50 twice
    // (rows 43 and 47 of the aligned diff are equal), but the image ALSO
    // emits, target-only:
    //   subi r10, r11, 0x8      a SECOND copy of the `end()-1` computation
    //   stw  r9,  0x50(r31)     an extra home of lastPoint
    //   mr   r9,  r11
    //   stfs f0,  0x54(r31)     an extra home of lastDot (first one)
    //   stfs f11, 0x54(r31)     an extra home of lastDot (second one)
    // That is the documented signature of an expression WRITTEN TWICE in the
    // source and CSE'd by MSVC, which homes the slot once per textual
    // occurrence -- see docs/decomp/patterns/repeated-call-expression-home-stores
    // and dead-home-slot-store.  The shape to try is spelling
    // `poly.points.back()` (and the lastDot initialiser) more than once rather
    // than through these two variables.  NOT attempted here, so it is a lead
    // and not a refutation.
    // REFUTED (w9-e 2026-09-30): the two remaining commutative rows -- the
    // image's `fmadds f0, f11, f13, f0` and `fmadds f31, f8, f10, f11` take
    // ray.dir.x as fA where ours takes the x-difference -- are NOT source-
    // reachable.  Writing both products as `(p->x - ray.base.x) * dirPtr->x`
    // is byte-for-byte inert (96.0938 / 95.8984 and the identical 8-row table),
    // which is the same commutative-operand floor recorded for fmuls elsewhere.
    // The five dead-home-store deletes above remain the only live lead.
    // w-homeslot (2026-11-13, 98.39 canonical / 98.125 fuzzy, 6 rows: the two
    // image-only `stfs 0x54(r31)` homes of the y-differences + 4 commutative
    // rows).  The homed value is the Y difference (back().y - base.y, then
    // i->y - base.y), not lastDot.  Measured, none produces a store:
    //   - named `Vector2 diff = SubV2(..)` using diff.x and diff.y: 98.3, 24 rows
    //     (the x/y loads swap order -- six lfs offset rows);
    //   - `const Vector2 &diff = SubV2(..)` (temp bound to a reference), and
    //     SubV2 rewritten as an NRVO local (`Vector2 r; r.x = ..; r.y = ..`):
    //     both 98.39 canonical / 97.6 fuzzy, 20 rows, same lfs offset swaps;
    //   - writing the y-difference into `v.y` (v is the 0x50 Vector2, so its y
    //     IS the 0x54 slot): 96.8, 23 rows, and still no 0x54 store at [46]/[65].
    // So a Vector2 temporary's y does not home here under any spelling tried;
    // whatever the original wrote, MSVC kept its y in memory and its x not.
```
