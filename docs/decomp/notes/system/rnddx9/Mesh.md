# Lane notes: `src/system/rnddx9/Mesh.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ScaleAddEq@@YAXAAVTransform@@ABV1@M@Z`

`ScaleAddEq` -- moved verbatim from `src/system/rnddx9/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 118-157):

```text
// The whole 28-byte gap is the prologue.  The image spills all three parameters
// across the `bl` (std r30/r31, stfd f31, then mr r31,r3 / mr r30,r4 / fmr f31,f1
// at 0x82620268-0x8262028C) and reads the vector part back through r30/r31.  We
// emit no callee-saved registers at all and read 0x30(r3) / 0x30(r4) AFTER the
// call, i.e. MSVC propagated the same-TU callee's real register usage --
// ScaleAddEq(Matrix3&) is a leaf that never writes r3/r4/f1 -- and kept the
// arguments live in the volatile registers.  MEASURED NEGATIVES: swapping the two
// definitions so the callee comes second is byte-inert (the propagation is not
// source-order dependent), and deleting the callee's body to force the
// conservative prologue is blocked by check_undefined_decomp_symbols.
//
// SOLVED (w8-p 2026-09-30), 63.267 -> 93.333: the callee does not have to be
// invisible, it has to be a COMDAT.  ham_xbox_r.map's flag column distinguishes
// the two, and we had been ignoring it: an ordinary out-of-line .cpp definition
// is bare `f`, a COMDAT (inline / in-class / template) is `f i`.  Both
// ScaleAddEq overloads are `f i` there (map lines 51286-51287, 826201c8 and
// 82620268), as is ?GreaterEq@PatchVerts@@IBAHH@Z (8263c030) -- while
// ?FaceCenter@@... (8263b860) and every RndMesh/DxMesh method we define
// out-of-line are bare `f`.  So the image compiled these as inline functions.
// A COMDAT callee may be replaced at link time by an equivalent definition from
// another TU, so MSVC may NOT assume its register usage: the caller has to spill
// to non-volatiles, which is exactly the 28-byte prologue gap described above.
// Marking the Matrix3 overload `inline` reproduces it -- the whole prologue
// (std r30/r31, stfd f31, mr r31,r3 / mr r30,r4 / fmr f31,f1) now matches, and
// the 9 delete + 2 insert rows collapse to 6.
// The same one-word change closed PatchVerts::HasVert (81.4 -> 100),
// PatchVerts::Add (96.8 -> 100) and RndMesh::OnSync (97.6 -> canonical 100) in
// rndobj/Mesh.cpp.  Check the map flag before certifying any residual of this
// shape as a clobber-propagation floor.
//
// w13-o: 93.333 -> 100. The translation is ScaleAddEq(tf1.v, tf2.v, f)
// (`v1.x += v2.x * f`), not ScaleAdd(tf1.v, tf2.v, f, tf1.v) -- same value,
// but the += form is what loads tf2 (r30) before tf1 (r31) at idx 10/12 and
// delays the `stfs f0, 0x34(r31)` past the z loads, as the image does.
// `inline` here matches the map (82620268, `f i`) and is kept for fidelity, but
// MEASURED (w8-p): it is inert for this function's own callers -- DxMesh::OnSync
// stays at 95.08458 and CacheFurTransform at 99.55705.  Same for marking
// DxMesh::FurWeight inline (also `f i`, 82622728): DrawFur stays at 98.45977.
// The COMDAT lever only pays where the caller's residual actually IS the
// conservative-prologue gap.
```


## `?GetMultimeshFaces@DxMesh@@QAAPAUD3DVertexBuffer@@XZ`

`DxMesh::GetMultimeshFaces` -- moved verbatim from `src/system/rnddx9/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 201-217):

```text
// Residual 13 rows (98.3051), of which exactly ONE is charged by the canonical
// ruler: a target-only `clrrwi r3, r3, 0` (rlwinm r3,r3,0,0,31 -- a 32-bit
// zero-extending self-move) sitting between `stw r3, 0x1b0(r30)` and
// `bl D3DVertexBuffer_Lock`.  The other 12 are an r29<->r30 exchange between
// `this` and the lock result, which normalization forgives.
// TWO REFUTATIONS, both measured (w9-e 2026-09-30):
//   reading the VB back through the member instead of a `vb` local -- MSVC CSEs
//     the store-then-load into the same register with no re-truncation: INERT.
//   holding it as `unsigned int vb = (unsigned int)D3DDevice_CreateVertexBuffer(
//     ...)` and casting back at the call -- also INERT; MSVC knows the value is
//     already a valid 32-bit pointer and emits no rlwinm.
// Both D3DVertexBuffer_Lock's free-function form and D3DVertexBuffer::Lock are
// declared in src/xdk/d3d9i/d3d9.h; the member form would round-trip the result
// through memory, which the image does not do (it takes r3 straight into r29).
// w20-r branch-landing scan (row 25, `bne` on unk1b0 != 0): ARTIFACT, the
// image keeps the loaded unk1b0 in r3 and branches straight to the epilogue;
// we land one row earlier on a reload of the same member. Same return value.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.


## `?CacheFurTransform@DxMesh@@IAAXABVTransform@@HM@Z`

`DxMesh::CacheFurTransform` -- moved verbatim from `src/system/rnddx9/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 265-299):

```text
    // w16-c (99.57047 -> 100.0 canonical, 6 commutative-fmuls rows left in the
    // *= block, fuzzy 99.597).  Three levers, each measured:
    //   * the distance test is Subtract(cached.v, xfm.v, delta) +
    //     LengthSquared(delta), not three hand-written dz/dy/dx temporaries.
    //     w8-p brute-forced all six declaration orders of the temporaries
    //     (best 99.57047); none loads cached.v and xfm.v in the image's x,y,z
    //     order, the helpers do.
    //   * xfm.v is named once (`pos`) and used for the copy and the wind query:
    //     that is what puts xfm in r29 and &xfm.v in r30 as the image does (the
    //     whole r29<->r30 swap, 19 rows).
    //   * the wind null test is SIGNED: the image is `cmpwi cr6, r29, 0x0` at
    //     0x82620D10 right after `lwz r29, 0x94(r11)` (RndFur::mWind's pointer);
    //     a plain pointer test emits `cmplwi`.  Spelled as an intptr_t test, the
    //     same idiom CanDraw above uses for its buffer test; identical truth
    //     value on both builds (intptr_t is pointer-sized, so native does not
    //     truncate).
    // NEGATIVE (w16-c): with `pos` but the temporaries kept, the six orders
    // read zyx 99.97 / xyz 99.99 / xzy 99.97 / yxz 99.97 / yzx 99.96 / zxy 99.96.
    //
    // w26-f3 (2026-11-02): 6 rows left (fuzzy 99.59731, canonical 100).  The
    // c2rs u{} order is the emitted order, and it shows the block is a
    // per-component key straddle around invWeight's temp (s88), not a source
    // ordering (L = load first, I = invWeight first):
    //   cached.m    RT76/78/80   L,I  I,L  I,L   image the same      equal
    //   cached.v    RT85/86/88   (z)L,I (y)I,L (x)L,I   -> image (z)I,L (y)L,I (x)L,I
    //   ScaleAddEq  RT94/95/97   L,I (all three)         -> image I,L on three
    // i.e. y and z are exchanged in the v block and the ScaleAddEq products
    // are inverted, so the resolution is a shift of the keys on one side of
    // s88, not a respelling of this block.
    // Measured INERT this lane: `const float &iw = invWeight;`,
    // `Vector3 &cv = cached.v;`, `const float &w = weight` / `weightRef`, and
    // naming the two Dot rows (`const Vector3 &xfmY`, `const Vector3 &cachedY`).
    // Measured WORSE, do not retry: explicit per-component for cached.v
    // (canonical 99.97315) and splitting ScaleAddEq into its m and v halves
    // (canonical 98.57718).
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.597, 6 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.

## `?OnSync@DxMesh@@MAAXH@Z`

`DxMesh::OnSync` -- moved verbatim from `src/system/rnddx9/Mesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 443-462):

```text
    // RESIDUAL (w7-ai, 95.1%): apart from the face loop below (w7-z's note),
    // everything left follows from ONE missing dead home store. The image emits
    // `stw r24, 0x54(r31)` TWICE around this pair of declarations -- rows 35 and
    // 38, with `addi r25, r24, 0x100` (the verts reference) between them -- off a
    // SINGLE `lwz r24, 0x148(r30)`. We emit only the second. Two dead stores of
    // one value around one load is the "call written twice, CSE'd" signature:
    // the image spells this as `GetGeomOwner()->Verts()` with an RndMesh::Verts()
    // that returns `mVerts`, so GetGeomOwner and Verts are two inline levels and
    // each materialises the receiver. Our RndMesh::Verts() is
    // `return mGeomOwner->mVerts;`, which folds both into one level, and the
    // missing level costs a callee-saved register, which is the whole r25<->r26
    // renaming through rows 41-97.
    //
    // NOT ATTEMPTED, deliberately: the faithful spelling needs
    // RndMesh::Verts() changed to `return mVerts;` in rndobj/Mesh.h and every
    // caller switched to GetGeomOwner()->Verts(). That is a shared-header
    // SEMANTIC change (Verts() on a non-owner mesh currently returns the
    // owner's vector), owned by the rndobj/Mesh lane, with a binary-wide blast
    // radius -- not something this call site can express, since `mVerts` is
    // protected and unreachable through a RndMesh*.
```

Inside the body (was lines 506-512):

```text
            // w7-z: the image runs this loop off a single induction variable --
            // the dst pointer, biased by +4 -- and rederives &mFaces[i] from it
            // (`addi r11,r10,4` / `subfic r8,r10,-4` in the preheader, then
            // `add r10,r11,r10; add r10,r10,r8` per iteration).  Our build keeps a
            // separate byte-offset IV and uses `lhzx`/`sthu`.  Refuted spellings:
            // `*dst++ = face.vN;` x3 (moves the `lwz r10,0x68(r31)` reload of
            // lock.mDataAddr INTO the loop, 95.08 -> 94.2).
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: index
copy loop: target uses a dst-relative cursor, ours a src offset; both write
buf[i*6+0..4] = faces[i].v1..v3.
