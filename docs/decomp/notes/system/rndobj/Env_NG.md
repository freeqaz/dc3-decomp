# Lane notes: `src/system/rndobj/Env_NG.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/rndobj/Env_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

File-level (was lines 10-16; nearest following definition: `ClearLightTransforms`):

```text
    // w17-a: a Matrix4 built from four Vector4 rows, not a Transform.  The
    // target object (lbl_830A8870, 0x40, unnamed so its type is not in the map)
    // is initialised with word copies of four 16-byte temporaries -- (1,0,0,0),
    // (0,1,0,0), (0,0,1,0) and (0,0,0,1): the 4th row's pad word is 1.0f, which
    // a Transform's Vector3 v can never hold -- and no memcpy (the implicit
    // Matrix3 copy in Transform(const Matrix3 &, const Vector3 &) is one).  Both
    // users already read it as a Matrix4.  62.88 -> 100.
```


## `?Select@NgEnviron@@UAAXPBVVector3@@@Z`

`NgEnviron::Select` -- moved verbatim from `src/system/rndobj/Env_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 279-292):

```text
    // The image consumes numProj itself as this loop's countdown
    // (`subic. r28, r28, 0x1` at 826A4B60) behind an UNSIGNED zero guard
    // (`cmplwi cr6, r28, 0x0` at 826A4B20), and that is what ranks numProj
    // into r28 above the four hoisted MILO_NOTIFY addresses (r27..r24): an
    // up-counting `for (i = 0; i != numProj; i++)` made MSVC copy numProj into
    // a fresh countdown register, leave numProj at r23 and rotate all six
    // callee-saved GPRs (96.30). A guarded do/while on an unsigned copy of
    // numProj coalesces onto numProj's register (`--projLeft != 0` is the
    // subic.), and the unsigned copy is what makes the guard cmplwi; the
    // point loop's guard (`cmplwi cr6, r21, 0x0` at 826A4B70) only needs its
    // counter unsigned. (w7-bw, 96.30 -> 96.88; refuted on the way: a
    // for-loop decrementing numProj in place still copies, 95.1; the same
    // countdown as `for (unsigned n = numProj; n != 0; n--)` coalesces but
    // evaluates the pointer/index inits ahead of the guard, 95.7.)
```

Inside the body (was lines 323-366):

```text
    // RESIDUAL (w27-i, 96.899796 canonical): in the image TheShaderMgr's
    // address and its vtable pointer are formed SEPARATELY inside each fog
    // arm -- `lis r9, TheShaderMgr@h` / `addi r30` / `lwz r3, TheShaderMgr@l(r9)`
    // in the FogEnable arm, and `lis r10` / `addi r30` / `lwz r3, 0(r30)`
    // again in the else arm.  Ours HOISTS `lis / addi r30 / lwz r3` above the
    // FogEnable branch and shares it, which is precisely the 3-insert
    // (208-212) / 3-delete (253-255) cluster.  Both arms open with the same
    // `TheShaderMgr.` expression, so c1 keeps one global read for both; the
    // image's version evidently did not offer it the same node.  Measured
    // inert: a `RndShaderMgr &` bound in the FogEnable arm (byte-identical
    // per-object).  Worse: the same binding in the else arm, 95.669; a
    // redundant `fogParams.x = 0.0f;` after the constant vector, 96.812.
    // SOLVED (w31-c, 2026-11-03), and the lever was the POINTER, not the
    // reference: `RndShaderMgr *mgr = &TheShaderMgr;` in the FogEnable arm,
    // with BOTH calls going through `mgr->`, is 96.899796 -> 98.55215
    // canonical and 96.50102 -> 98.26585 fuzzy.  A reference folds back to the
    // global expression and is byte-inert; the pointer is a distinct IL value,
    // so c1 stops sharing the FogEnable arm's TheShaderMgr node with the else
    // arm and the image's own per-arm formation comes back.  BOTH calls have
    // to move: the pointer on SetVConstant alone or on SetPConstant alone is
    // the 96.90 baseline exactly.  Inert refinements measured: declaring mgr
    // above the Vector4 (98.47034 per-object, same), `RndShaderMgr *const`
    // (same), a second `mgr2 = mgr` (same); the scoped `{ RndShaderMgr *mgr
    // = ...; }` form is much worse (96.81799, i.e. no gain) because each
    // short range re-folds, and a pointer in the ELSE arm too is 94.27.
    // What is left is scheduling of the same instructions: we copy the loaded
    // pointer into r29 (`mr r29, r3`) and use it across the SetVConstant
    // call, where the image reloads `lwz r3, 0(r30)` from the address at each
    // site; plus the Vector4 store order and two `lwz r3, 0(r30)` placements.
    // The remaining rows are the Vector4 ctor's store order (image x,z,w
    // with y last; ours w,x,z with y last) and the fade/LRFade block.
    //
    // w31-c (2026-11-03): the fade/plane arithmetic is FAITHFUL -- re-derived
    // from 826A4DD0..826A4F7C.  fadeRefDot is the y,z,x `fmadds` chain in f10;
    // leftPlane stores 0,0,0,1 (f31/f30 = 0.0f/1.0f), tests lrFade.x != .y,
    // scale = f30/(y-x), x/y/z as `fadeDir.k * scale` (fadeDir FIRST, matching
    // the source's operand order), w as `fneg((x + dot) * scale)`.  rightPlane
    // reuses the SAME f10 dot, tests .z != .w, and multiplies scale-first
    // (`fmuls f13,f10,f13`), which is also how the source writes it.  Nothing
    // in this block changes a value.  After the pointer fix above, the whole
    // function's insert/delete set is 7 instructions in 5 clusters: the two
    // `mr r29, r3` / `lwz r11, 0(r29)` copies of the shader manager, two
    // `lwz r3, 0(r30)` placements, and the Vector4/plane store ORDER (same
    // four slots, same values).
```

Inside the body (was lines 398-406):

```text
        // RESIDUAL (w7-bw, 96.88): the image multiplies the y terms first
        // (826A4E48..826A4E84: y, z, x), ours z, y, x. Inlining the dot as a
        // flat sum in any of three term orders (x,y,z / y,z,x / x,z,y) keeps
        // z first and additionally flips every fmuls to scale-first (95.5
        // each), so Dot() stays. Also refuted: binding `RndShaderMgr &` before
        // the fade block (the image reloads TheShaderMgr through r30 at every
        // call, `lwz r3, 0x0(r30)`; a bound reference holds the object and
        // emits `mr r3, r30`: 91.8) and a local copy of mNumLightsApprox in
        // the early-return tail (moves both stats loads, not one).
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual:
DebugNotifyOncer static named lbl_ on target side; stores equal.
