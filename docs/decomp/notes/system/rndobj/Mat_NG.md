# Lane notes: `src/system/rndobj/Mat_NG.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetRegularShaderConst@NgMat@@IAAX_N@Z`

`NgMat::SetRegularShaderConst` -- moved verbatim from `src/system/rndobj/Mat_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 154-159):

```text
    // w25-mb: defined here, at the top, so its symbol id is small.  c2 orders
    // a commutative multiply's operands by key (C2RS-BRIDGE section 8.7): a
    // named local keys 0x10000|sid<<5.  Defined where it is computed, it was
    // sid 446 (key 0x137c0), above the CSE temp for mWorldProjectionStartBlend
    // (0x11300), so blendOffset's fmuls came out range * start; the image has
    // start * range.  The 0 is dead: every read follows the assignment below.
```

Inside the body (was lines 218-225):

```text
    // Specular - copy to local Color structs.
    // w25-mb: the per-pixel scale is applied to the copies in place.  With six
    // separate rgb floats assigned in both arms of the if, each was a merge
    // variable, so COLOR ranked the spec rgb trio (pri 23) above spec2Pow (pri
    // 20): spec2Pow took f27 instead of the image's f30, which permuted 25 FPR
    // rows here and in the rim block, and moved the rim copy's word order.
    // Scaled in place, specPow/spec2Pow pop at 37/32, above the trio, and all
    // of it matches.
```


## `?RefreshState@NgMat@@IAAXXZ`

`NgMat::RefreshState` -- moved verbatim from `src/system/rndobj/Mat_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 363-393):

```text
    // Blend mode switch
    // w7-bj: 97.14 residual, three clusters, no lever found.
    //  (1) The five hoisted constants after `lwz mBlend` (0x8269D19C-0x8269D1AC)
    //      come out 4,3,2,0,1 in the image and 0,1,4,3,2 here.  Their order
    //      is the order of FIRST USE in source: moving the kDarken arm to the
    //      top of this switch re-emitted them 1,2,0,4,3 (and moved the arm's
    //      block, 96.11).  No arm order puts 4 before 0 while keeping the
    //      Dest arm first, so the image's order comes from something else.
    //  (2) The `lwz mTexGen` / `cmplwi` of the mTexGen switch sits AFTER the
    //      four mTexGenMatrix2 diagonal stores in the image (0x8269D3B4) and is
    //      hoisted above them here.  CLOSED by w26-r -- see the `const TexGen &tg`
    //      binding at the switch below.  (§8.8 base re-binding: a copy is
    //      propagated away and the load floats; the reference pins it.)
    //  (3) In the second mBlend switch the image stores the four floats and
    //      THEN mFadeOut in every arm (0x8269D654-D664), tail-merging the
    //      Multiply/Darken arm into the Add arm's `stw 2`; ours schedules the
    //      int store first and merges the two zero-fill arms instead.  Writing
    //      `mFadeOut = N` first in each arm is byte-identical.
    // w19-a (still 98.95): the statement order INSIDE each of the eleven arms is
    // not the lever for cluster (1) either -- a hill-climb over every single
    // move within every arm (132 builds) found no order better than this one.
    // w26-r (99.9768): what is LEFT is 9 diff_arg rows and nothing else -- the
    // five `li` constants of cluster (1) rotated (ours 0,1,4,3,2 vs the image's
    // 4,3,2,0,1; the value->register map is identical, only the emission slot
    // moves) and two call argument-setup pairs whose `addi`s are emitted in the
    // opposite order at 0x36c/0x370 (Normalize) and 0x4e4/0x4ec (MakeTex3).
    // Both are pure scheduling order, not values.  Block-scoped §8.8 bindings at
    // those two call sites (`Hmx::Matrix3 &dstMtx = xfmTmp.m;` around Normalize,
    // `Hmx::Matrix3 &dstMat = mTexGenMatrix;` around MakeTex3) were measured
    // byte-identical, so the argument-materialisation order is decided below the
    // source level here -- c2's scheduler, not the operand keys of §8.7.
```

Inside the body (was lines 529-534):

```text
    // w26-r: `tg` is a REFERENCE, not a copy, and that is the whole point.  The
    // image loads mTexGen (lwz 0x70(this)) AFTER the four mTexGenMatrix2 diagonal
    // stores; a plain `TexGen tg = mTexGen;` copy is propagated away and the load
    // is scheduled two stores early, while binding the base reference keeps the
    // load where §8.8 puts it.  Normalized 98.9201 -> 99.9510 from this line
    // plus the two swap branches below using it.
```

Inside the body (was lines 621-626):

```text
    // Second blend switch - fade-out mode and fade parameters.
    // w7-bt (97.14 -> 98.9 canonical): the four floats are one Vector4 set
    // through its inlined Set(); as four scalar stores MSVC hoists the
    // mFadeOut store above them and tail-merges the two zero-fill arms,
    // where the image keeps the floats first in every arm (0x8269D654-D664)
    // and merges the Multiply/Darken arm into the Add arm's `stw 2`.
```
