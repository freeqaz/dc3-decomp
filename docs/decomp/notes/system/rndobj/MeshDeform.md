# Lane notes: `src/system/rndobj/MeshDeform.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Load@RndMeshDeform@@UAAXAAVBinStream@@@Z`

`RndMeshDeform::Load` -- moved verbatim from `src/system/rndobj/MeshDeform.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Inside the body (was lines 103-131):

```text
    // MATCHED (w7-bs, 97.20089 -> 100.0 canonical, 99.96 raw, 896 B, 224/224
    // rows equal).  The image is four INLINED Vector3::operator== calls
    // (Vec.h: `x == v.x && y == v.y && z == v.z`) joined by one `&&` and
    // assigned straight to the bool, and every one of the recorded residuals
    // was that shape:
    //   - each row's compare ends in `li r11,1 / fcmpu / beq / li r11,0 /
    //     clrlwi. r11,r11,24 / beq END` (0x826DAAE4-AAF8 etc.): the inlined
    //     operator materialises its bool result, then the OUTER `&&` tests it;
    //   - all the false exits thread to ONE `li r11,0` at 0x826DABA4 because
    //     the whole thing is a single expression, and the last operand is
    //     re-materialised as the expression's value (`clrlwi.` / `li r11,1` /
    //     `bne` at 0x826DAB98-A0) -- that is the "u8 -> bool normalisation"
    //     w7-bp saw, and the branch-before-`lfs __real@3f800000` at
    //     0x826DAAF4-AB04 follows from the threaded CFG (the 1.0 literal is
    //     only needed on the fall-through path);
    //   - the x row reads `0x0/0x4/0x8(r30)` off the cached &mMeshInverse
    //     while v/y/z read off `this` because the inlined operator's `this`
    //     is `&m.x` = this+0x40, which CSEs with the `bs >> mMeshInverse`
    //     argument; `&m.y`/`&m.z` were derived from r30 (+0x10/+0x20) for the
    //     `>>` calls, not from `this`, so their reads fold to this+0x50/0x60.
    //     w7-bp's `const Hmx::Matrix3 &m` binding put y/z through the pointer
    //     too, which is why it charged 6 offset rows.
    //   - the register permutation (r27<->r30 etc., 63 rows) was the same
    //     thing: with one expression `this` is only live in the compares and
    //     lands in r27, below the three callee-saved temporaries.
    // Superseded, do not re-derive: the u8 accumulator split into four
    // statements (97.2), `if (isIdentity) isIdentity = ...` (97.2, more
    // rows), `if/else` store (94.1), `? true : false` (inert), the Matrix3
    // reference (97.18).  Behaviour is identical: an exact identity test.
```


## `?Print@RndMeshDeform@@UAAXXZ`

`RndMeshDeform::Print` -- moved verbatim from `src/system/rndobj/MeshDeform.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 160-165):

```text
        // w13-a (98.8 -> 100): indexed, weight read first.  The image walks
        // one pointer from cData-1 in steps of 2 and takes the update-form
        // `lbzu` on the BONE byte (the last load of the iteration) with the
        // weight as a plain `lbz 0x3` off the old pointer; reading bone then
        // weight puts the lbzu on the weight instead, and the two `*++w`
        // walk was two lbzu by 1.
```


## `?AppendWeights@VertArray@RndMeshDeform@@QAAHHQAHQAM@Z`

`RndMeshDeform::VertArray::AppendWeights` -- moved verbatim from `src/system/rndobj/MeshDeform.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 202-226):

```text
    // w21-be (95.2 -> 2 rows left): the count is the class's own NumVerts()
    // inline and the MemResizeElem insert position is end() -- both header
    // inlines that read mData/mSize straight off `this` (`lwz r11, 0x4(r24)` /
    // `lwz r10, 0x0(r24)` at 0x826D8D2C, `add r10, r11, r10` = mData + mSize
    // through the intptr_t add) instead of through a `&mData` reference.  That
    // one change resolved the whole r23/r24 + r21/r22 "allocator tie-break"
    // that w7-bi certified, the add operand order, and the MemResizeElem tail
    // scheduling.  Behaviour identical (same count, same insert pointer).
    // w25-fb: the last two rows were /fp:fast commutative operand orders
    // (`fadds f30, f0, f30` for the sum, `fmuls f12, f12, f30` for the scale).
    // Swapping the source operands is inert: c2 orders the operands itself (in
    // the IL, before regasg; the CAND list and registers do not change).  Each
    // of the four named locals below (merged, both `weight`s, clamped) flips the
    // orientation of a different subset of the three fadds/fmuls rows (the dedup
    // merge's fadds included); this set of four gives the image's three orders.
    // Found by search over named-local spellings, not predicted.  Open question:
    // the ordering key itself (C2RS-BRIDGE 8.3).  Same arithmetic throughout.
    //
    // One fused loop: the dedup scan, the negative-weight report and the sum all
    // live in the same `for (i)` -- 0x826D8D98..0x826D8E68 is a single loop with
    // one `cmpwi cr6, r31, 0x0` zero-trip guard at 0x826D8D68.  The inner scan
    // walks j FORWARD from i+1 and merges j into i (`stfsx f0, r8, r30` writes
    // back to weights[i] at 0x826D8DE4), then fills the hole from the tail and
    // steps j back; there is no `break`, the scan continues from the swapped-in
    // element.
```
