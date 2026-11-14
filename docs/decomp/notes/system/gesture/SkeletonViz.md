# Lane notes: `src/system/gesture/SkeletonViz.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?PreLoad@SkeletonViz@@UAAXAAVBinStream@@@Z`

`SkeletonViz::PreLoad` -- moved verbatim from `src/system/gesture/SkeletonViz.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 106-112):

```text
// w15-a (98.45 canonical): the whole residual is the INIT_REVS anchor pick
// (image anchors &gAltRev and reaches gRev as -4; we anchor &gRev) -- see the
// w9-f note in rndobj/Spline.cpp.  Of the 18 image functions that anchor on
// gAltRev, 8 already do so in our build at 100% (CharClip, HamIKEffector,
// HamListRibbon::PreLoad, HamRegulate, CharLookAt, ThreeDSound, InlineHelp,
// RndShockwave), so it IS reproducible from source; the discriminator is
// still unknown.
```


## `?SetCamera@SkeletonViz@@AAAXABUSkeletonFrame@@ABVTransform@@M@Z`

`SkeletonViz::SetCamera` -- moved verbatim from `src/system/gesture/SkeletonViz.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 317-362):

```text
    // RESIDUAL (SetCamera, 95.1 canonical / 95.0 raw): frame delta +0x10.
    // The image shares ONE 16-byte slot at r1+0x50 between the `pos` of the
    // mUsePhysicalCam branch (stores at 0x82440650..0x8244065C, re-read as a
    // 16-byte copy at 0x82440690..0x824406AC) and `plane` here (stores at
    // 0x82440938..0x82440948, `addi r3, r1, 0x50` at 0x8244096C).  We give
    // plane 0x50 and pos 0x60, which pushes every later slot up by 0x10 and
    // costs 30 offset rows plus the 6I/6D schedule cluster inside the inlined
    // SetLocalPos.  NEGATIVES, both measured at exactly 95.1/95.0 (inert):
    //   - wrapping this block's body in an extra `{ }` to match pos's lexical
    //     depth;
    //   - hoisting `Vector3 pos` out to the `if (mUsePhysicalCam)` scope so the
    //     two locals sit at the same depth.
    // Vector3 is 12 bytes and Plane is 16 (math/Vec.h, math/Mtx.h:357), so the
    // packer may simply refuse to merge unequal sizes; needs the permuter.
    //
    // w7-by (still 95.083336): the size theory is wrong -- Vector3 carries a
    // PAD word (Vec.h:140), both are 16 bytes, and the SetLocalRot by-value
    // temp (0x50..0x5f, `ld r4/r5` at 0x824406D8/0x824406EC) already shares
    // 0x50 with `plane` in OUR build too.  The +0x10 is `pos` alone, and what
    // keeps it out of the shared slot is its address reaching the OUT-OF-LINE
    // `RotateAboutZ` (0x82440674): deleting that call (measurement only) puts
    // pos on 0x50 and the frame at 0x140.  Every spelling that keeps the call
    // leaves pos distinct: direct-store init, `Vector3 pos(0,-d,0)`, a
    // whole-object copy init, pos in its own inner `{ }`, a copy temp handed
    // to SetLocalPos, out-of-line SetWorldPos in place of the inlined
    // SetLocalPos, an inline helper returning pos by value, an inline helper
    // owning pos as a named local (95.5 -- reloads mPhysicalCam into r30).
    // The one shape that DOES land pos on 0x50 with frame 0x140 is a by-value
    // `Vector3` parameter of an inlined static helper -- but the callee writes
    // it, so the 16-byte argument copy survives (+16 rows, 91.6; the same with
    // an uninitialised `Vector3()` argument, 87.6), and a reference-taking
    // helper is not inlined at all (62.4).  `plane` itself is not the lever:
    // it is passed to out-of-line Multiply/UtilDrawPlane exactly like pos and
    // shares in both builds.  Whatever the original wrote for pos, it is not
    // a named local whose address escapes.
    //
    // w21-bk (still 95.083336): standalone cl.exe probes of the rule itself --
    // two address-escaping 16-byte locals in SEQUENTIAL scopes never share a
    // slot (if/if, nested if/if, plain struct or with ctor, via an inline
    // helper's named local: frame always +0x10), while the two ARMS of one
    // if/else do share.  So in the image pos and plane cannot both be named
    // escaping locals in sequential scopes.  Measured in the tree: reusing pos
    // for the SetLocalRot argument (`pos.Set(tilt, 0, rot); SetLocalRot(pos)`)
    // lands pos on 0x50 but pushes plane off it (93.6, worse; reverted).  The
    // two fmadds/fadds operand-order rows in the floor-normal block are
    // commutative-only (same association as the image); `normal += v` inert.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: frame is
0x10 larger on our side (one extra 16-byte temp); all heap/member stores equal.


## `?DrawJoints@SkeletonViz@@AAAXABVBaseSkeleton@@PAVVector3@@1_N@Z`

`SkeletonViz::DrawJoints` -- moved verbatim from `src/system/gesture/SkeletonViz.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 455-468):

```text
    // w21-bm: 98.45 -> 100 canonical (one register-only row left: the
    // SetWidth `fmuls f0, f0, f24` comes out with its operands swapped; writing
    // `baseWidth * mLineWidthScale`, or reusing `c` for the width, is inert).
    // Levers: both loops plain and indexed (the image's signed `cmpw` bone
    // latch and the joint loop's `li r30,0 / mr r31,r26` are MSVC's strength
    // reduction), the confidence colour as a flat if/else-if/else, baseScale
    // built straight from the local diagonal with Scale() for scaledScale,
    // `len4 + len3 + len2` as one sum, and one reused `c`.
    // w25-gw: the SetWidth row is c2's commutative key (C2RS-BRIDGE 8.7):
    // the memory leaf mLineWidthScale is keyed by its base temp's sid mod 4.
    // The redundant parentheses around `len4 + len3` minted one extra temp,
    // putting that base on sid 652 (key 0x10008, below baseWidth's 0x103c0);
    // without them it is 651 (key 0x1c008) and the load sorts first, as in
    // the image.  Same left-to-right association, so the sum is unchanged.
```
