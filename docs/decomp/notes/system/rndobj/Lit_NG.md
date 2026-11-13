# Lane notes: `src/system/rndobj/Lit_NG.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SphereConeTest@NgLight@@IAA_NABVVector3@@M@Z`

`NgLight::SphereConeTest` -- moved verbatim from `src/system/rndobj/Lit_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 60-109):

```text
// RESIDUAL (w7-as, 63.1 canonical): instructions 5..62 match EXACTLY -- both
// WorldXfm() expansions, `sc -= xfm1.v`, the three fmuls and both early
// returns.  Everything after that is one problem: we keep too much alive.
// Target prologue saves r27-r31 + f30/f31 with `stwu r1, -0x100`; ours saves
// r24-r31 + f26-f31 with `stwu r1, -0x140`.  The STACK SLOT SET is identical
// (0x50..0xb8, seven 16-byte Vector3 slots, all of them reused two or three
// times) -- `run_diff_inspect mode=stack-layout` reports 10 DIFFER / 11
// PERMUTED and 0 target-only / 0 base-only slots -- so this is not a
// declaration-count problem.  The image materialises every `addi rN, r1,
// <slot>` into a VOLATILE register next to the copy that uses it; we hoist
// four of them into r24/r25/r27/r31 and five float loads into f26-f30, which
// is the whole +0x38 of callee-saved area plus the register permutation that
// dominates the row count (90 instructions across 20 pairs).  Fixing it needs
// the copies re-ordered so each address dies immediately, not a slot removed.
// w21-c (2026-10-03): still 63.15 canonical / 60.33 fuzzy.  Re-read the image
// (0x826B91B8..0x826B9440): its post-Normalize op order is dirTop*=, topPoint+=,
// dirBot*=, axisRange*=, botPoint+=, conePoint=botPoint, toSphere-=topPoint,
// conePoint+=dirBot, closest=toSphere, edgeDir=conePoint, edgeDir-=topPoint.
// Writing the statements in exactly that order (and with toSphere-= before the
// conePoint copy) is BYTE-IDENTICAL to the order below -- MSVC schedules this
// block itself, so statement order here is inert.  The image never stores
// topPoint back (0x80 is reused by conePoint/edgeDir) and reuses 0xb0 for both
// closest and the scaled edge copy; ours keeps xfm2.m.y in r26-r28 and five
// floats in f26-f29 across the block.  Not a statement-order problem.
//
// w31-c (2026-11-03) -- GEOMETRY ADJUDICATED, no behaviour bug, and this is a
// PLACEMENT residual rather than a missing instruction.  Opcode multisets are
// EQUAL (309 vs 309): the only difference is the callee-save sequence -- the
// image has 2 `stfd`/2 `lfd` + `__savegprlr_27`, we have 2 extra `bl`s and
// `__savefpr_26` + `__savegprlr_24`.  Both `bge/bgt/blt/bne` counts match once
// they are read as the bf/bt forms llvm-mc prints.  Re-derived from the image
// instruction by instruction: sc = c - v; the three products; both range
// tests; perp = sc - m.y*(sum); Normalize; topPoint/botPoint/conePoint;
// `1.0f/Dot(edgeDir,edgeDir)` as fdivs f12,f7,f12 with f7 = __real@3f800000;
// `Dot(dir,closest) < 0` early true; `Length(closest) < sphereRadius` -- all
// identical, and the epsilon sits on the same side of both comparisons.
// The ONE source-level infidelity is the association of the axisProj sum: the
// image re-derives (px+pz)+py (826B910C `fadds f13,f13,f12` then 826B9150
// `fadds f13,f13,f11`), our `pz + (px + py)` gives (px+py)+pz.  Measured and
// REJECTED: `px + pz + py` 57.72, `(px + pz) + py` / `py + (px + pz)` /
// `float axisDot = px + pz` all 61.21, `const float &axisDot = px + pz` 62.38,
// `pz + px + py` and `const float &axisDot = px + pz + py` both 53.81 -- every
// alternative is worse than the 63.15 this spelling scores, and the fadds
// multiset is identical either way.  The remaining register assignment is the
// second allocator (none of f26-f29 appears in the `cands` list; all live only
// after the single Normalize call), so there is no colour decision to name.
// w35-b (2026-11-04): the two named radius locals kept mTopRadius/mBotRadius
// live from the Normalize call onward. Reading the members at the use site
// lets each load land next to it, as in the image (i117/i119).
// 63.14563/60.330097 -> 64.43366/61.56958, body 1236/1236 unchanged.
```

Inside the body (was lines 181-190):

```text
    // NEGATIVE RESULT (w7-as, 2026-09-14): this is a deliberate LOSS.  Faithful
    // reciprocal + faithful dirTop/dirBot order = 63.1 canonical; the unfaithful
    // `Dot(a,b) / Dot(b,b)` + reversed order scored 65.6.  Measured 4 ways:
    //   plain divide, dirBot first  65.6   (single fdivs, no 1.0f -- unfaithful)
    //   plain divide, dirTop first  62.3
    //   reciprocal,   dirBot first  61.9
    //   reciprocal,   dirTop first  63.1   <- kept
    // The reciprocal row itself MATCHES in the kept spelling; the 2.5pp is
    // paid in where MSVC schedules the `lis`/`lfs` pair and the regalloc that
    // follows it.
```


## `?BlurShadowRT@NgLight@@MAAXXZ`

`NgLight::BlurShadowRT` -- moved verbatim from `src/system/rndobj/Lit_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 448-491):

```text
// w7-bt (92.68 -> 100.0 canonical, 143 rows all equal; the 99.9 raw is the
// SetObjConcrete ICF fold adjudicated below).  The w7-ai residual -- one
// extra callee-saved register holding `0x9c + i` across the first
// SetPConstant call (`subi r4, r26, 0x10` for the first index, `mr r4, r26`
// for the second) -- was two source shapes, not register allocation:
//
//   1. The loop is a zero-based counted `for (k = 0; k < 5; k++)` with the
//      tap offset derived as `i = k - 2`, NOT a do/while over `i = -2` with
//      an explicit `taps` down-counter.  MSVC keeps `i` as the register IV
//      (r31: `li r31, -0x2` at 826B9ED4, `extsw r11, r31` at 826B9EF4 for
//      the float conversion) and rewrites both indices `0x8a + k` /
//      `0x9a + k` against it -- `addi r4, r31, 0x8c` at 826B9F18 and
//      `addi r4, r31, 0x9c` at 826B9F5C, each computed from r31 in volatile
//      r4 -- and manufactures the down-counter itself (`li r28, 0x5` at
//      826B9EDC, `subic. r28, r28, 0x1` at 826B9F74).  Written directly as
//      `0x8c + i` / `0x9c + i` on a source-level `i`, the optimizer
//      reassociates the pair into one shared `i + 0x9c` that has to live
//      across the call (the r26 of the w7-ai note); the same reassociation
//      is what the image itself does in DOFProc_NG's SetVHBlurWeights
//      (826ABFB0 `subi r4, r31, 0x10`), where `i` has no other use.
//      Probe ladder: `i + 0x8c` operand order, `0x8cu + i`, a separate
//      `idx` IV (folded back to `i + 0x8e` in r26, 98.41), and
//      `for (i = -2; i < 3)` (drops the down-counter: `cmpwi r31, 3` /
//      `blt`, 95.5) all keep the shared register; `k`-based: 99.15.
//   2. The weight is `kWeights[k]`, not a `pWeight` walked by hand.  With
//      `pWeight = kWeights - 1` and a pre-increment, MSVC hoists the whole
//      `kWeights - 4` into the prologue (r18, the __savegprlr_18 half of
//      the residual); with `*pWeight++` from the bare array it keeps the
//      array address in r20 (826B9E70) and re-derives `subi r27, r20, 0x4`
//      per outer pass (826B9ED8) but orders it before `li r31, -0x2`
//      (99.15); the subscript form gives the image's order and the same
//      `lfsu f0, 0x4(r27)` (826B9F44) -- 99.99.
//
// The last two rows were the order of the two zero stores into the work
// material: the image writes mTexWrap (0xb8, 826B9FF0) before mZMode
// (0x6c, 826B9FF4), and the scheduler only reproduces that with mZMode
// assigned BEFORE mTexWrap in source (the two stores share r21 = 0 and are
// otherwise unordered).
//
// Adjudicated and NOT a wrong callee: row 111 pairs
// SetObjConcrete<ObjRefConcrete<AnimTask,ObjectDir>> against our
// SetObjConcrete<ObjRefConcrete<RndTex,ObjectDir>>. Both are at 82401CD0 in
// icf_aliases.map -- a proven ICF fold, so the target-side name is only the
// fold representative. `workMat->SetDiffuseTex(srcTex)` is correct as written.
```
