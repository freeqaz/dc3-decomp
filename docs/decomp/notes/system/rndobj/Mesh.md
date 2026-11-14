# Lane notes: `src/system/rndobj/Mesh.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?TransformNormal@@YA?AVVector3@@ABV1@ABVMatrix3@Hmx@@@Z`

`TransformNormal` -- moved verbatim from `src/system/rndobj/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 37-139):

```text
    // w8-h BEHAVIOURAL FIX: these were COLUMN dots (`result = normal * inv`), i.e.
    // the transpose was silently dropped, so every skinned/instanced normal was
    // rotated by inv instead of inv-transpose.  The image takes ROW dots -- for
    // result.x it loads inv.x.z at 0x58(r1) and inv.x.y at 0x54(r1)
    // (0x8263xxxx `lfs f9,0x58(r1)` / `lfs f13,0x54(r1)`, fed to
    // `fmuls f9,f11,f9` and `fmadds f0,f12,f13,f9`), where the old spelling
    // wanted inv.z.x at 0x70 and inv.y.x at 0x60.  Term order is the image's:
    // each row's first fmuls is the term written first here.
    // w8-p 2026-09-30, INDEPENDENT CONFIRMATION of the w8-h fix above, by
    // decoding all nine terms out of the image rather than re-reading three of
    // them.  `inv` is the stack Matrix3 based at 0x50(r1), so its rows are
    // inv.x = 0x50/0x54/0x58, inv.y = 0x60/0x64/0x68, inv.z = 0x70/0x74/0x78,
    // and r30 holds `normal` (nx=0x0, ny=0x4, nz=0x8).  The image computes:
    //   fmuls  f10,f12,f10  @8263B9A8   ny * inv.y.y (0x64)
    //   fmuls  f9,f11,f9    @8263B9B4   nz * inv.x.z (0x58)
    //   fmuls  f8,f12,f8    @8263B9B8   ny * inv.z.y (0x74)
    //   fmadds f10,f7,f6,f10 @8263B9D8  + nx * inv.y.x (0x60)
    //   fmadds f9,f7,f5,f9   @8263B9DC  + nx * inv.x.x (0x50)
    //   fmadds f8,f7,f4,f8   @8263B9E0  + nx * inv.z.x (0x70)
    //   fmadds f0,f11,f0,f10 @8263B9E4  + nz * inv.y.z (0x68) -> stfs 0x4(r31) = result.y
    //   fmadds f0,f12,f13,f9 @8263B9EC  + ny * inv.x.y (0x54) -> stfs 0x0(r31) = result.x
    //   fmadds f0,f11,f3,f8  @8263B9F4  + nz * inv.z.z (0x78) -> result.z
    // All nine terms, all three destinations and the y-before-x store order
    // (0x4(r31) at 8263B9E8 BEFORE 0x0(r31) at 8263B9F0) agree with the three
    // lines below exactly.  The w8-h row-dot fix is right and the term order is
    // the image's.
    //
    // RESIDUAL (w-mesh-tnorm 2026-11-10) 99.95 canonical / 95.07317 fuzzy,
    // 19 equal / 22 rows, all register permutation + one offset swap.  Was
    // 99.68293 / 94.31707 with 18 equal / 23 rows; see TNORM-RESOLVED below.
    // The image INTERLEAVES the three rows -- three
    // fmuls, then three nx-fmadds sharing f7=nx as the multiplicand, then the
    // three closing fmadds -- and holds nx in one register across all of it.
    // w26-a correction: we emit the SAME 3+3+3 grouping (three fmuls, then
    // three fmadds on one term, then three on the other); the two later groups
    // are TRANSPOSED, i.e. we fuse the row's third term second and its nx
    // second term third, whereas the image does nx second and the third term
    // last.  So this is not "we finish a row before starting the next" -- the
    // grouping agrees and only which term is fused at each of the two later
    // positions differs, which is a rounding difference (the fused product is
    // the unrounded one), not just a register rename.
    // Zero `[sym]` rows, i.e. nothing here is a wrong callee.
    //
    // ⚠ METHOD NOTE for whoever reads the numbers next: a large
    // (normalized - fuzzy) gap is NOT a wrong-callee tell.  This row has the
    // biggest such gap in these three units (5.37pp) and carries no relocation
    // charge at all; ConvertTextToWide's 3.87pp gap is a frame-pointer
    // difference.  normalized = diff_score - arg_diff_score, and arg_diff_score
    // collects register-only diff_arg rows as well as relocation penalties, so
    // the gap only says "there are diff_arg rows".  To hunt a wrong callee, look
    // for `[sym]` rows under name_check, or ask
    // query_functions(objdiff_pattern='WRONG_CALLEE') -- which, asked of all
    // three of these units, returns a measured empty set.
    //
    // w26-a: 27 more spellings, every one still 18/41 (or 17), i.e. c2
    // canonicalises them all to the same tree and the emitted order is
    // invariant under all of them.  Tried: explicit parentheses `(t1 + t2) +
    // t3`; partial-sum locals per row; a reused single partial-sum local;
    // three partial-sum locals declared together; compound `+=` in three
    // statements per row; every product named; only the FIRST / only the
    // SECOND / only the THIRD product named; the second and third named;
    // `normal.x` used directly so every multiplicand is a memory leaf; a
    // second named copy of nx; and a `Vector3 acc` accumulator copied out.
    // MECHANISM HYPOTHESIS: the two later fma positions are chosen by the c2
    // commutative operand sort (C2RS-BRIDGE 8.7), whose key is a hash over
    // the product's two leaves; the three products of a row are structurally
    // identical (one named local x one stack leaf), so every spelling above
    // reproduces the same three keys and the same order.  Moving it needs a
    // leaf whose identity actually changes -- and the one that moves (nx) is
    // shared by all three rows, so a per-row change is not available either.
    //
    // TNORM-RESOLVED (w-mesh-tnorm 2026-11-10): 18 -> 19 equal, 99.68293 ->
    // 99.95 canonical, 94.31707 -> 95.07317 fuzzy.  The hypothesis above is
    // REFUTED as stated: the two later fusion positions are NOT pinned by a
    // hash over the product leaves, because writing the two non-nx terms as
    // [closer, nx, seed] -- instead of the image's [seed, nx, closer] -- plus
    // declaring the locals nz, ny, nx makes c2 emit the image's grouping.
    // Rows x and y now fuse seed, then nx, then closer, exactly as the image
    // does, and the three stores come out in the image's y, x, z order instead
    // of x, y, z.  c2 is applying a fixed rotation to the WRITTEN addend
    // order; the original spelling was the image's own order and got rotated
    // away from it, so the fix is to pre-rotate the source.
    // ⚠ This is a ROUNDING change between the two spellings, and it moves
    // TOWARD the image: the fused (unrounded) product is now the same one the
    // image fuses.  It is not register-free -- the WRITTEN order is not the
    // semantics, the emitted order is, and the emitted order is now the
    // image's for rows x and y.
    // Row z is still seeded from inv.z.z where the image seeds from inv.z.y.
    // Pre-rotating row z too (`nz * inv.z.z + nx * inv.z.x + ny * inv.z.y`)
    // is equal-count NEUTRAL (19/41) and regresses every secondary signal --
    // canonical 99.95 -> 99.8, fuzzy 95.07317 -> 94.63415, stack slots
    // 2 -> 6 permuted -- because it regenerates the whole load order.  Left
    // alone deliberately.
    //
    // Measured INERT on this function (byte-identical, 18/41, so do not
    // re-derive): swapping the multiply operands to matrix-first
    // (`inv.y.y * ny`); reordering the three result statements x/y/z; and the
    // declaration order nx,ny,nz on top of the new addend order (that one is
    // 19/41 but scores worse: 99.7 canonical, 94.4 fuzzy, 8 stack slots
    // permuted vs 2).  Declaration order ny,nz,nx is 18/41.  c2 canonicalises
    // statement order and commutative operand order, so neither is a lever --
    // consistent with the Mtx.h Multiply note, which refuted the operand-order
    // swap the same way.
```

p2-b99 (2026-11-14, name_check, 99-100 band audit): NOT A BUG. z = v.x*m.x.z + v.y*m.y.z + v.z*m.z.z on both sides; only the association order differs.


## `?Add@PatchVerts@@QAAXHAAVVertVector@RndMesh@@AAVVector3@@@Z`

`PatchVerts::Add` -- moved verbatim from `src/system/rndobj/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 151-175):

```text
// CLOSED (w8-p 2026-09-30): 96.77778 -> 100.0, by the COMDAT lever documented on
// PatchVerts::HasVert below -- `inline` on this definition and on GreaterEq.  The
// w7-az reading below was exactly right that the image "treats GreaterEq as an
// opaque call"; what it could not find was why, and the answer is in the map's
// flag column (`f i` = COMDAT).  Kept for the record:
// RESIDUAL (w7-az, 96.78, 23 rows) -- SAME CLASS as the HasVert note below,
// and the same callee.  The image treats GreaterEq as an opaque call and
// therefore parks everything it needs afterwards in non-volatiles: five of
// them, `bl __savegprlr_27` at 0x8263F23C, with `addi r29, r3, 0x10`
// (&mPatchVerts) at 0x8263F258 BEFORE the call and `mr r3, r29` at 0x8263F268
// after it, and the vertIdx home slot address `addi r6, r1, 0xac` computed
// AFTER the call at 0x8263F274.  We keep both in VOLATILE registers across
// the call (r7 and r6) and save only four non-volatiles, which only
// type-checks because MSVC propagated GreaterEq's clobber set into this
// caller -- exactly the propagation the HasVert note describes.  Every one of
// the 23 rows is downstream of that: the prologue helper, the 0x10 frame
// delta, and the r27<->r29 rotation.
// NEGATIVE (w7-az): the object-expression lever that took
// DefaultPhysicsManager::AddCollidable to 100 -- binding the member to a
// named reference and calling through it,
// `std::vector<int> &patchVerts = mPatchVerts;
//  patchVerts.insert(patchVerts.begin() + GreaterEq(vertIdx), vertIdx);` --
// does NOT work here: 96.78 canonical unchanged (raw 95.03 -> 94.92), same
// prologue, same rotation.  It reorders the two `addi`s and nothing else.
// Do not retry the source reorder either; see the REFUTED EXPERIMENT below.
```


## `?HasVert@PatchVerts@@QBA_NH@Z`

`PatchVerts::HasVert` -- moved verbatim from `src/system/rndobj/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 209-225):

```text
// CLOSED (w8-p 2026-09-30): 81.40741 -> 100.0.  The diagnosis below was right
// about the mechanism and wrong about the knob.  The target keeps `this` and
// `vert` in r30/r31 across the GreaterEq call (108 bytes, saves two
// non-volatiles) because GreaterEq is a COMDAT there, not because of source
// order: ham_xbox_r.map's flag column separates an ordinary out-of-line .cpp
// definition (bare `f`) from a COMDAT emitted for an inline/in-class/template
// definition (`f i`), and ?GreaterEq@PatchVerts@@IBAHH@Z @8263c030 is `f i`
// (map line 51879) -- as are HasVert @8263e178, Clear @8263f1e8 and Add
// @8263f238.  ?FaceCenter@@YAXPAVRndMesh@@PAVFace@1@AAVVector3@@@Z @8263b860 is
// bare `f` in the same object, which is the control: the image really does mix
// the two, and it is the map, not guesswork, that says which is which.
// A COMDAT may be replaced at link time by an equivalent definition from another
// TU, so MSVC cannot assume the callee's register usage and the caller must
// spill to non-volatiles.  Marking these four `inline` reproduces that.
//
// The 2026-08-23 experiment (moving this definition ABOVE GreaterEq) was inert,
// and correctly so -- source order was never the gate.  Do not retry it.
```


## `?FaceCenter@@YAXPAVRndMesh@@PAVFace@1@AAVVector3@@@Z`

`FaceCenter` -- moved verbatim from `src/system/rndobj/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 254-267):

```text
// The interleaving below is load-bearing (w7-bp: 94.74 -> 100.0 canonical,
// 38/38 equal).  The image issues the two pointer loads at 0x8263F6A0 and
// 0x8263F6B4, i.e. `lwz r8, 0x148(r3)` FIRST and `lwz r9, 0x100(r8)` in the
// middle of the three `stfs f0` zero stores, not after them:
//     lis r9,__real@0 / lwz r8,0x148(r3) / li r11,3 / subi r10,r4,2
//     lfs f0,__real@0(r9) / lwz r9,0x100(r8) / stfs 0x0 / mtctr / stfs 0x4/0x8
// Two measured negatives, both with the declaration as ONE statement:
//   - both loads after all three zero stores (the original spelling): the
//     0x100 load lands at index 10 instead of 5 -- 1 insert + 1 delete, 94.74;
//   - both loads before all three zero stores: the 0x148 load moves to index 0
//     and drags the literal's `lis` with it -- 2I/2D + 2 arg diffs, 89.47.
// Splitting the deref into `owner` + `verts` and putting the `verts` load
// between `center.x = 0` and `center.y = 0` is what reproduces the image's
// schedule.  Do not "tidy" these four lines back together.
```


## `?GetDistanceToPlane@RndMesh@@UAAMABVPlane@@AAVVector3@@@Z`

`RndMesh::GetDistanceToPlane` -- moved verbatim from `src/system/rndobj/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 839-871):

```text
// RESIDUAL 99.91666 / 10 rows (w8-p 2026-09-30), and it is the SAME CLASS as
// RndMesh::MakeWorldSphere (99.98198 / 4 rows), RndMesh::SetVolume (99.98586 /
// 4 rows) and FillCompressedVertex in rndobj/MeshVertCompress.h (99.95683 /
// 8 rows).  In all four the residual is nothing but the ORDER in which MSVC
// loads the members of a component triple, so the rows are charged `lfs`/`lwz`
// OFFSETS -- real canonical points and real matched_code, not a forgiven
// register permutation.  Here the image reads p.c before p.b at idx 39/43 and
// starts the second Plane::Dot from the Z term (`lfs f12,0x58(r1)` +
// `lfs f0,0x8(r30)`), where we start from X.
//
// THE GENERALISATION, which is what makes this worth writing down: the load
// order of a triple is reachable from source EXACTLY WHEN the triple is spelled
// as explicit locals in the function being matched, and NOT when it comes out of
// an inlined shared helper.
//   * Reachable: DxMesh::CacheFurTransform's `float dx/dy/dz` -- brute-forcing
//     all six declaration orders moved fuzzy 98.08054 -> 98.49664, and the
//     permutation MSVC applies is not a fixed function of declaration order, so
//     it had to be enumerated rather than reasoned out.
//   * NOT reachable: everything here.  The loads belong to Plane::Dot
//     (math/Mtx.h:429), Dot(Vector3,Vector3) (math/Vec.h:283), ScaleAdd,
//     Vector3::operator*= and the vector's end() -- all correctly written in
//     natural x,y,z order, all shared by the whole engine.  Changing one to
//     chase a row here would move thousands of unrelated functions.
//     SUPERSEDED FOR GetDistanceToPlane by w26-a, and the reason is worth
//     keeping: the rows were never the LOAD order at all.  They are which of
//     the two y/z products c2 emits as `fmuls` -- a register/instruction pick,
//     not a load -- and it IS reachable from source, by changing a leaf's
//     CLASS rather than its position.  See the w26-a note in the body.  The
//     other three named here are untouched by that finding.
// MEASURED NEGATIVES on this class, each a full build, each EXACTLY inert on
// BOTH rulers: declaration reorder and argument inlining and commuting the
// combining operator (FillCompressedVertex); swapping -Dot's arguments and
// hand-writing the product explicitly right-associated (SetVolume).
```

Inside the body (was lines 880-900):

```text
        // w22-a29 FLOAT-TOWARD-IMAGE: the image sums each plane distance as
        // (b*y + c*z) + a*x + d -- fmuls/fmadds on the y,z pair first, then
        // `fmadds f0,f10,f0,f13` (a*x) and `fadds f31,f0,f9` (+d), and the
        // same (pair) + a*x + d shape in the loop.  Plane::Dot's flat
        // a*x + b*y + c*z + d is re-sorted by /fp:fast on the Xbox but
        // evaluated left-to-right natively, so it is spelled out here with
        // the image's grouping.  Order INSIDE the pair is inert on MSVC
        // (both spellings emit the same fmuls/fmadds pick).
        //
        // w26-a CLOSED (99.9375 -> 100.0, 8 rows): the residual was NOT the load
        // order of Plane::Dot's triple but WHICH of the two y/z products c2
        // makes the `fmuls` (the other becomes the `fmadds`).  That is decided
        // by the c2 commutative operand sort (C2RS-BRIDGE 8.7), whose key for a
        // product is a hash of its two leaves -- so writing the pair the other
        // way round in source is inert (measured: 4 spellings, 88/96 unchanged),
        // exactly as the w22 note below records.  What DOES move it is changing
        // a leaf's CLASS: `p.c`/`p.b` are memory leaves (a load folded into the
        // tree), and naming them as locals makes them variable leaves with their
        // own sids, which re-orders the pair.  Each scope needs its OWN pair --
        // hoisting one pair over both expressions changes the shape (measured:
        // 96 -> 98 instructions).
```


## `?MakeWorldSphere@RndMesh@@UAA_NAAVSphere@@_N@Z`

`RndMesh::MakeWorldSphere` -- moved verbatim from `src/system/rndobj/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 918-938):

```text
// w16-a (99.982, 4 rows): the image loads mNumVerts (0x104) before mVerts
// (0x100) for the loop's end() and adds size*0x60 + begin; we do the
// reverse.  VertVector::end() spelled `mVerts + mNumVerts`, `&mVerts[size()]`
// and `begin() + size()` are all inert here, so the order is not end()'s.
//
// w26-a, 16 more spellings, still 107/111 (all at idx 78-82, the BACK-EDGE
// recompute of end(); the ENTRY copy of the same expression at idx 49-55
// already agrees with the image, in both builds): pointer and reference
// bindings of Verts() (`const`/non-const), `&Verts()[0]`/`&Verts()[size()]`,
// index loop, bare `Vert*` loop, `it++`, `it += 1`, reversed `!=` operands,
// a hoisted `begin()`, a hoisted begin+size pair, and the same loop rewritten
// as `while` and as a `for(;;)` with the increment moved into the body.
// MECHANISM HYPOTHESIS, stated as one: end() is `&mVerts[mNumVerts]`, so its
// two leaves are both r29-relative loads off the same base and differ only by
// the displacement fold (0x100 vs 0x104) in the c2 commutative-sort key
// (C2RS-BRIDGE 8.7).  That key is not reachable from this function -- the
// expression lives in the shared Mesh.h nested class -- and the two
// instantiations of it are coloured oppositely here, which is a property of
// the live set across the body rather than of the expression.  Nothing was
// found that flips the back-edge copy without either flipping the entry copy
// or changing the instruction count.
```

Inside the body (was lines 953-957):

```text
                // w22-a29 FLOAT-TOWARD-IMAGE: the image sums |v|^2 as
                // (y*y + z*z) + x*x (`fmuls f0,f0,f0` on y, then
                // `fmadds f0,f13,f13,f0` z, `fmadds f0,f12,f12,f0` x);
                // Dot()'s flat x+y+z is what native evaluated.  Xbox bytes
                // unchanged by this spelling.
```


## `?SkinVertex@RndMesh@@QAA?AVVector3@@ABVVert@1@PAV2@@Z`

`RndMesh::SkinVertex` -- moved verbatim from `src/system/rndobj/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 985-995):

```text
        // w26-a CLOSED (99.98165 -> 100.0, 14 rows): the residual was two
        // callee-saved pairs (r24<->r25, r26<->r27) plus the emission order of
        // the two `addi rX, r5, 0x48/0x20` base pointers.  `vert.boneIndices`
        // and `&vert.boneWeights.x` were `la`-class address expressions folded
        // straight into their loads, so which of the two got hoisted first --
        // and therefore which register each took -- was the scheduler's pick
        // and not something a declaration reorder could reach (measured: 5
        // spellings inert at 95/109, including moving the bone-index read out
        // of the compare and reordering the `&&` operands).  Naming them as
        // pointer locals gives each its own candidate with its own id, and
        // both the order and the registers fall out right (109/109).
```


## `?SetVolume@RndMesh@@QAAXW4Volume@1@@Z`

`RndMesh::SetVolume` -- moved verbatim from `src/system/rndobj/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1400-1427):

```text
                    // RESIDUAL 99.98586 / 4 rows (w8-p): the image's -Dot lowers to
                    // `fmuls f10,f10,f9` on the Z pair, `fmadds f0,f0,f13,f10` on
                    // the Y pair and `fnmadds f0,f12,f11,f0` on X
                    // (0x82642900-0x8264290C), i.e. -(x*x' + (y*y' + z*z')); we emit
                    // the identical three instructions with Y and Z exchanged, so
                    // only the four `lfs` offsets at 0x826428E8-0x826428FC differ
                    // (0x4/0x74 <-> 0x8/0x78).  MEASURED NEGATIVES, each a full
                    // build, each EXACTLY inert at 99.98586 norm AND fuzzy:
                    // swapping the arguments to `-Dot(planePoint, vb0)`; and
                    // writing the product out by hand, explicitly right-associated
                    // the way the image accumulates it,
                    // `-(vb0.x*planePoint.x + (vb0.y*planePoint.y + vb0.z*planePoint.z))`.
                    // MSVC canonicalises all three spellings to the same bytes.
                    // OPEN (w25-mc), mechanism from the c2rs tap (C2RS-BRIDGE 8.7):
                    // c2 sorts the three products by key and emits the list's
                    // second entry innermost. Ours sorts [z 0x0104070e,
                    // y 0x01040686, x 0x0103a438]: x has count 3 (planePoint.x is
                    // a memory leaf on the planePoint variable itself), y and z
                    // count 4 (their bases are planePoint+4 / +8 temps), and z's
                    // hash is higher. The image needs [y, z, x]. Measured inert:
                    // named px/pz/py copies (folded back into the loads before
                    // the last sort), a named partial sum, a named result, a
                    // Vector3& alias for vb0. Open question: a spelling that
                    // changes the y or z product's tree shape (its count) without
                    // changing the loads. A hash-only fix needs z's hash to wrap
                    // past 0xffff while y's does not: a common shift in
                    // [0xf8f2, 0xf97a), about 100 field-sid steps away.  Same class as
                    // FillCompressedVertex in rndobj/MeshVertCompress.h.
```


## `?OnSync@RndMesh@@MAAXH@Z`

`RndMesh::OnSync` -- moved verbatim from `src/system/rndobj/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 1476-1529):

```text
// RESIDUAL (w7-bp): 96.91 -> 97.6 canonical (95.2 raw), 1196 B, 300/300
// instructions.  What is left is 118 arg diffs + 2 replace + 1 insert + 5
// delete, and EVERY one of them is the same single cause -- see below.
//
// TWO LEVERS APPLIED HERE (both worth reusing):
//  1. `u5` / `bestFaceIt` are assigned at the TOP of the `uvar16 < u5` arm,
//     before the FaceCenter call, not at the bottom.  They used to sit at the
//     bottom of both arms, textually identical, and MSVC tail-merged the two
//     copies into one; the image emits both (`mr r24, r30` / `mr r27, r7` at
//     0x82640958-5C in the `<` arm AND again at 0x826409E8-EC in the `==`
//     arm).  Hoisting them above the call breaks the textual identity and
//     stops the cross-jump.  96.91 -> 97.6, two deletes closed.
//  2. `DistanceSquared(v4c, v40)` in place of a named `Vector3 diff` +
//     `Subtract` + `LengthSquared`.  Measured BYTE-IDENTICAL (97.6 either
//     way) -- kept only because the image materialises no `diff` temp at all
//     (it fsubs straight into f0/f13/f12 at 0x82640964-84).
//
// MEASURED NEGATIVE: `unsigned short vertIdx` by value instead of
// `unsigned short &vertIdx` in the 3-vertex loop -- inert, 97.6 both ways.
//
// CLOSED (w8-p 2026-09-30) on the canonical ruler: 97.58194 -> 100.0 normalized,
// by marking PatchVerts::Clear / Add / GreaterEq / HasVert `inline` (see the
// COMDAT-flag note on PatchVerts::HasVert).  The w7-bp diagnosis below named the
// mechanism correctly.
// ⚠ NOT fully banked: fuzzy_match_percent is 99.83278, and report.json computes
// matched_functions from the NORMALIZED ruler but matched_code from fuzzy == 100,
// so this row adds +1 matched function and 0 of its 1196 bytes.  Do not read the
// normalized 100.0 as byte-identity here.
// THE FLOOR IS ONE MECHANISM: same-TU callee volatile-register propagation,
// exactly the effect already documented on PatchVerts::HasVert above (81.4%,
// and refuted there as a definition-order problem).  MSVC has already
// compiled FaceCenter / HasVert / Clear / Add when it reaches OnSync, so it
// knows which volatiles they really clobber and keeps live values there.  Our
// callees are leaner than the image's, so we get MORE free volatiles and the
// allocator never needs the high callee-saved registers:
//     target f28/f29/f30 hold v40.x/.y/.z and f31 holds `f68`  (rows 142-217)
//     ours   f7 /f8 /f9              ,,          f10    ,,     -- volatile
//     target r17/r18 hold two of the three HasVert results
//     ours   volatiles, so the whole GPR file is rotated by one (r22->r23,
//            r20->r21, r29->r30, ... 17 pairs, 124 instructions)
// Everything that is CHARGED follows from that and nothing else:
//     [2]/[3] + [297]/[298]  target `subi r12,r1,0x80` + `bl __savefpr_27`
//                            vs our single `stfd f31,-0x78(r1)`, and the
//                            matching `__restfpr_27` in the epilogue
//     [4]/[5]                frame 0x150 vs 0x120 -- 0x30 = the 6 extra saves
//     [246]/[248]/[249]/[256] the image re-materialises the vertex index from
//                            callee-saved r30 before each of HasVert and Add
//                            (`mr r4, r30` twice); we leave it in r4 across
//                            the HasVert call because we know our HasVert
//                            does not touch r4.
// So OnSync cannot cross 100 while HasVert is at 81.4 -- fix HasVert's own
// register usage first and re-measure this function.  No behavioural
// divergence: the arithmetic rows (142-217) are structurally identical and
// differ only in which FPR holds each value.
```


## `?InstanceGeomOwnerBones@RndMesh@@QAAXXZ`

`RndMesh::InstanceGeomOwnerBones` -- moved verbatim from `src/system/rndobj/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 1656-1686):

```text
// CLOSED (w21-be): 100 modulo register permutation -- the two "unreachable"
// scheduling rows below were a `?:` that the original wrote as two calls; see
// the per-arm SetTransParent at the bottom.  History kept for the levers:
//
// RESIDUAL (w7-bp): 97.38 -> 98.4 canonical (97.7 raw), 1012 B, 255/255
// instructions.  Remaining: 35 arg diffs (all register permutation, which the
// canonical ruler forgives) + 2 insert + 2 delete, and nothing else.
//
// LEVERS APPLIED (each measured, in order):
//   97.38 -> 98.2  bind `RndBone &bone = bones[i]` BEFORE the `?:` (see below)
//   98.2  -> 98.2  route every mBones use through the `bones` reference --
//                  score-neutral, but it turned target row 64 from a real
//                  offset mismatch (`lwz r5, 0x4(r26)` vs our
//                  `lwz r5, 0x154(r30)`) into a mere register permutation
//   98.2  -> 98.4  `RndMesh *owner = mGeomOwner` for the cmplwi (see below)
//
// THE TWO STRUCTURAL ROWS LEFT are both pure MSVC scheduling and I could not
// reach either from source:
//   target [231] `li r5, 0x0` sits between the `cmpwi cr6, r3, -0x1` and the
//     `bne` that selects the parent; ours lands at [241], after the join.
//     MEASURED NEGATIVE: hoisting the `false` into a named `bool keepWorldPos`
//     bound before the `?:` is completely inert -- 98.2 before and after, same
//     37 rows.
//   target [245] `addi r25, r25, 0x1` (the i++) is issued between the two
//     `bones` end/begin loads; ours issues it after both, at [247].
//
// NOT A DEFECT: target row 222 calls
// `ObjRefConcrete<SpotlightDrawer,ObjectDir>::SetObjConcrete` where we call the
// `RndTransformable` instantiation.  Those two are ICF-folded to one address
// (build/373307D9/icf_aliases.map), and objdiff leaves the row UNCHARGED --
// dtk just picked the other name.  Do not "fix" it.
```

Inside the body (was lines 1712-1723):

```text
        // CLOSED (w7-bp, 98.2 -> 98.4): the image's test here is `cmplwi`
        // (0x82642F6C), an UNSIGNED raw-pointer test, where the two earlier
        // mGeomOwner tests are `cmpwi` and match.  Testing the ObjPtr itself --
        // `if (mGeomOwner)`, `mGeomOwner.Ptr()`, `mGeomOwner.Ptr() != nullptr`,
        // all measured -- lowers to `cmpwi` every time, because the null test
        // goes through ObjPtr's conversion and MSVC treats that result as
        // signed.  Binding the ObjPtr to a RAW `RndMesh *` local first and
        // testing the local is what emits `cmplwi`; compare row 71
        // (`cmplwi cr6, r31, 0x0`), which already matched because `parent` is a
        // raw pointer.  The local also feeds `addi r4, r11, 0x150` at the
        // assignment, so it costs no extra load.  Reusable lever: an unsigned
        // pointer compare needs a raw pointer lvalue, not a smart-pointer one.
```

Inside the body (was lines 1780-1807):

```text
        // w21-be (98.42 -> 100 modulo register permutation): TWO calls, one per
        // arm, not one call on a `?:` result.  MSVC hoists the instructions the
        // arms share -- `li r5, 0x0` and &bones[i] -- above the `bne` at
        // 0x82643238 and tail-merges the `lwz r3, 0xc(r11)` / `bl SetTransParent`
        // join, which is exactly the image's shape (the "unreachable scheduling"
        // rows w7-bp recorded: `li r5` before the branch, and the i++ issued
        // between the two `bones` loads).  Also removes the HX_NATIVE `?:` cast.
        // Arm order matters: `!= -1` first flips the bne and moves the
        // newRoot arm (measured 98.4).  Left: r9/r10 swapped on the three
        // mGeomOwner->mBones[parentIdx] address rows (volatile only).
        // w25-rl stop note (c2rs tap): the else arm's address add has two
        // code-band colour candidates, all pri 14 tie 0 with ref 2: the
        // owner's mBones begin load (code#113, id 66) and parentIdx*84
        // (code#114, id 67); bones' begin (code#115, id 68) is the third.
        // C=30 code mints, F=22 `ben` frees, so all three take fresh ids
        // and pop in reverse mint order: 68 r11, 67 (mulli) r10, 66 r9.
        // The image needs begin popped before the mulli (begin r10, mulli
        // r9; its add also lists begin first), i.e. begin minted after the
        // product, or C-F <= 1 so both recycle and pop in mint order.
        // Inert (11 spellings): index-first `parentIdx + begin()`,
        // `parentIdx[begin()]`, BoneTransAt(), a named owner/begin/bone/
        // parent local, `RndBone &` ref, `!= -1` arm order. w26-f13 also
        // measured inert: a named `RndBone *`, a named
        // `RndTransformable *ownerParent`, and a named `unsigned int` index.
        // Binding the owner's array as an `ObjVector<RndBone> &` is NOT inert
        // -- it costs 1.64 fuzzy / 1.60 canonical, so the base load has to
        // stay a direct member access. Open question: what puts the product
        // ahead of the base in the code-band walk.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.921, 3 non-equal row(s): NOT A BUG.
Every row is register-only; `scripts/analysis/regperm_equivalence.py` raised 1 lead(s),
each read by hand against the listing: pure register permutation, no operand swap.
(CALLARG? leads read by hand: SetTransParent takes r3-r5; the r9 lead is a dead
leftover.)

## `?LoadVertices@RndMesh@@UAAXAAVBinStreamRev@@@Z`

`RndMesh::LoadVertices` -- moved verbatim from `src/system/rndobj/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 2034-2129):

```text
// w7-bs (2026-09-15): 96.99 -> 98.5 canonical (95.1 -> 97.3 raw), 202 rows,
// 151 equal / 48 register-permutation diff_arg / 1 insert / 2 delete.
//
// THE r31 FRAME BASE IS NOT AN ALLOCATOR DECISION, IT IS C++ EH.  The image's
// Mesh.obj carries `except_record_8263C3A8` (FuncInfo magic 0x19930522) for
// this function: ONE unwind-map entry (state 0 -> `__unwind$150236` at
// 0x8263C6CC, 0x28 bytes, whose body is `addi r3, r31, 0x1084; bl
// ??1MemDoTempAllocations@@QAA@XZ`) and a 7-entry IP-to-state map at
// lbl_8208C800 that enters state 0 at 0x8263C5C8 -- the `bl ??_U@YAPAXI@Z`
// (operator new[]) -- and leaves it at 0x8263C5D0, the MemPopTemp call.  So
// the original scopes the allocation with the RAII pair, not the bare calls:
//
//     { MemDoTempAllocations tmp; mCompressedVerts = new unsigned char[n]; }
//
// An EH-bearing local forces MSVC to address the frame through r31 (the
// funclet rebuilds it from r12: `subi r31, r12, 0x10f0`), which is exactly
// what w7-bp measured as "r31 reserved as a frame base": the extra
// callee-saved r21, the 0x10f0 frame (the 1-byte object is homed at 0x1084,
// just past the FormatString at 0x70+0x1014), and the 34 r1<->r31 rows.  All
// of that closes with the one-line change; the prologue is now row-equal
// (`bl __savegprlr_21; subi r31, r1, 0x10f0; ld r12, -0x1000(r1); stwu`).
// HOW TO SEE IT NEXT TIME: a `subi r31, r1, <frame>` BEFORE the `stwu` with no
// alloca means "find the except_record_/pdata entry and read the funclet" --
// the funclet names the object.
//
// RESIDUAL 98.5 (all scheduling / register permutation, no offset or callee
// rows): [24]/[26] the zero-init stores of loadedCompressedSize (0x58) and i8c
// (0x64) are emitted in the other order (w7-bp already measured statement
// order as inert -- the slots themselves agree); [29]/[31] `lis r24,
// kAssertStr@h` two rows earlier on our side; [174] `mr r28, r21` -- the
// image copies the hoisted zero register into a fresh register for the vertex
// loop counter i5, we coalesce i5 onto the zero register itself.
// MEASURED (w7-bs) on that last row: `int i5 = 0` moved above
// `mVerts.resize` -- inert, identical 98.5; `int i5;` at the top with
// `i5 = 0` before the loop -- inert, identical 98.5; `int i5 = 0` hoisted into
// the top zero-init group -- 99.5 canonical BUT the compiler then keeps i5
// live in its own register for the whole function and the prologue becomes
// `__savegprlr_20` (r20-r31 vs the image's r21-r31, `mr r22, r30` at row 31).
// The score rises only because the canonical ruler folds the save-helper name;
// the shape is wrong, so it was NOT kept.
//
// (w7-bp's earlier reading, kept for the record: the SLOT ASSIGNMENT is
// identical on both sides -- 0x58 loadedCompressedSize, 0x5c numVerts, 0x60
// loadedVersion, 0x64 i8c, 0x68 i88, 0x70 the FormatString -- so reshuffling
// declarations cannot reach any of it.)
//
// MEASURED NEGATIVE (w7-bp): splitting the six zero-initialisers into bare
// declarations plus assignments written in the image's own store order
// (loadedVersion, loadedCompressedSize, i8c, i88, i9, b3 -- target indices
// 22/24/26/28 store 0x60, 0x58, 0x64, 0x68 where we store 0x60, 0x64, 0x58,
// 0x68) is completely INERT: 97.0 / 95.1 and the identical 73 rows, with the
// OFFSET_SWAP(0x58,0x64) still reported.  The store order is scheduling, not
// statement order.
//
// The structural rows w7-bp listed were scheduling, not missing code; the
// [141]/[144] ReadChunks argument-order pair closed with the RAII change above
// (the `lwz r4, 0x184(r22)` reload is now row-equal), [29]/[31] and [174]
// remain as described in the w7-bs residual.
//
// w21-be STOP at 98.50 (same 48 diff_arg / 1 insert / 2 delete): the whole
// residual is one allocator decision -- the image keeps the hoisted zero
// (`li r21, 0x0`, row 12) in its OWN lowest-priority register and copies it
// into the loop counter (`mr r28, r21`, row 174); we coalesce i5 onto the zero
// web, which lifts that web to r28 and shifts this/d/kAssertStr/Mesh.cpp/c8/i9
// down by one.  MEASURED INERT this wave: `if ((++i5 & 0x1FF) == 0)`;
// declaring i9 before i88.  WORSE: `while (it != end()) d >> *it++;` (97.5).
// Not tried on purpose: seeding i5 from another zero-valued local (i9/i88)
// would invent a use -- flagged, not shipped.
//
// w26-a CONFIRMS the w21-be reading by decoding the permutation out of the
// diff rather than eyeballing it, and it is exactly ONE cyclic shift of eight
// registers, not a scattering of 51 independent rows:
//     value   image   ours        value    image   ours
//     zero     r21     r28         c8/bool  r26     r25
//     this     r22     r21         i9       r27     r26
//     d        r23     r22         TheDebug r28     r27
//     kAssert  r24     r23
//     Mesh.cpp r25     r24
// So every other row is the same shift seen from a different value; nothing
// else about the function differs, and the image's extra instruction is the
// single `mr r28, r21` at row 174 (804 vs 800 bytes on both sides is that one
// instruction).  Five more spellings MEASURED INERT this wave at 101/210 --
// all six initialisers collapsed onto one line, b3 declared first, i8c/i88/i9
// grouped onto one line, the five ints declared in reverse, and int instead of
// unsigned for i8c/i88/i9.  c2 canonicalises the declaration forms, so the
// coalescing decision is not reachable from how the zero-initialisers are
// spelled; it is a c2rs §8.4 tie-0 pop-order / §8.6 `cost` preference question
// (`CAND asg id 1 reg r28 cost r10:-1 r26:-1 r28:-1 r29:-1 r30:-1` -- the zero
// web is pulled to r28 by copy preferences, not by its priority of -10, which
// would have popped it above the pri -36/-37 pair).
//
// NOT A DEFECT: rows 62 and 159 call different MakeString instantiations
// (`$$BY0BD@...$$BY04` vs our `$$BY08...$$BY0DH@`) while referencing the SAME
// `??_C@` string symbols on both sides.  Those instantiations are ICF-folded
// and objdiff leaves both rows marked equal -- per-TU MakeString divergence is
// not charged.  Do not chase it.
```

Inside the body (was lines 2131-2146):

```text
    // RESIDUAL (w27-i, 98.49751 canonical / 97.25373 fuzzy): the three
    // MILO_ASSERTs below mint THREE different MakeString instantiations where
    // the image calls ONE, three times:
    //   image  MakeString<char const[19], int, char const[5], char const *>
    //   ours   <char const[9], int, char const[19]>  (0x29C)
    //          <char const[9], int, char const[24]>  (0x2D4)
    //          <char const[9], int, char const[55]>  (0x2E7)
    // i.e. the message literal's array LENGTH is a deduced template argument,
    // so each assert site mints its own symbol.  This is name-only: objdiff
    // scores all three `bl`s EQUAL under the canonical ruler and the emitted
    // argument setup is identical on both sides -- the relocation name is
    // what costs the fuzzy points.  Reproducing one instantiation means
    // stopping the message literal from deducing as an array, which needs a
    // different call signature and therefore different arguments; not
    // reachable from MILO_ASSERT as written.  The rest of the residual is a
    // 7-pair callee-saved cascade (46 rows) behind that.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.


## `?SaveVertices@RndMesh@@UAAXAAVBinStream@@@Z`

`RndMesh::SaveVertices` -- moved verbatim from `src/system/rndobj/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 2269-2280):

```text
// RESIDUAL 96.77778 / 27 rows (w8-p re-read 2026-09-30).  Three of the rows are
// the only structural ones: `b` + `li r31,0x24` + `li r30,0x1` -- the sunk
// `else { compressedSize = 0x24; isXBox = 1; }` arm -- is placed at instruction
// 115 in our build and at 155 in the image, i.e. the image emits it AFTER the
// vertex loop and we emit it before.  Same three instructions, same order,
// different block placement; that is the block-sinking class
// (docs/decomp/patterns/block-sinking.md), decided by MSVC's layout pass.
// The other 24 rows are a consistent pairwise exchange of two callee-saved
// pairs, target r27<->our r26 and target r25<->our r24: the image gives the
// higher register of each pair to the flag defined first (`cached`) and the
// lower to TheDebug / the format string, and we do the opposite.  Nothing here
// is a missing or wrong instruction.
```

Inside the body (was lines 2283-2302):

```text
    // The image writes each of these three flags STRAIGHT into its final
    // register -- `li r27, 0x1` / `li r27, 0x0` at 8263B048/8263B050 for
    // cached, `li r10, 0x0` / `li r10, 0x1` for hasMeshData, `li r31, ...` for
    // doCompress -- with no normalising `clrlwi rX, rY, 24` at the definition;
    // the mask shows up only at each USE (8263B0A0, 8263B0AC, 8263B0F0,
    // 8263B1F0).  `bool b = <&& expression>;` makes MSVC materialise the 0/1
    // in a scratch register and then mask-and-move it into b, one extra
    // instruction per flag.
    // w27-h stop note (96.79144 canonical / 96.14973 fuzzy).  Every value,
    // store, branch and call on this row matches the image; the multiset is
    // equal except for ONE block that the image places differently.  The
    // `compressedSize = 0x24; isXBox = 1;` arm sits out of line at the very end
    // of the image (8263B23C, among the loop's cold blocks) and branches BACK
    // to the join at 8263B1A4; ours leaves it inline immediately after the
    // assert arm, so objdiff charges 3 inserts + 3 deletes.  That is c2 block
    // ordering, not source: arms swapped (96.76471), no initialisers (94.61),
    // assignment order, nested scope, comma-operator, a named `notXBox` flag and
    // a `(int)` cast on the write were all measured, 8 of 9 exactly inert.  The
    // remaining 22 register-swap rows (r26/r27, r24/r25, r10/r11) follow the
    // same placement: once the arm moves, `cached` and &TheDebug swap.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: block
placement only: the XBox arm (li 0x24 / li 1) sits out of line at a different spot.
