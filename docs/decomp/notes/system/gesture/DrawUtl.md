# Lane notes: `src/system/gesture/DrawUtl.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `CopyDepth()`

`CopyDepth` -- moved verbatim from `src/system/gesture/DrawUtl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Attribution: no function named `CopyDepth` in report.json (unit); filed under the source name

Inside the body (was lines 58-90):

```text
                // Brightness lerps 0.6 -> 1.0 across each metre. The span must be
                // spelled `1.0f - 0.6f`: the target's constant is 0x3ECCCCCC, the
                // float value of that subtraction, not 0.4f (0x3ECCCCCD).
                // w14-f (99.67, 2 rows + 6 relocation-name rows under name_check):
                // the image assigns the loop-invariant constants f11 = 0.4,
                // f12 = 0.001, f13 = 0.6; ours f11 = 0.6, f12 = 0.4, f13 = 0.001.
                // Bit-identical: `(1.0f - 0.6f) * meters + 0.6f`,
                // `Interp(0.6f, 1.0f, meters)`, `/ 1000.0f`, a `minBright = 0.6f`
                // local.  Inlining meters into one expression folds 0.001 * 0.4
                // into one constant (92.7, worse).
                // w21-bl (still 99.67): standalone cl.exe probe reproduces ours
                // (f11 0.6 / f12 0.4 / f13 0.001) for 15 spellings -- 0.6f
                // first (`0.6f + meters * 0.4f`), `ramp = 0.6f; ramp += ...`,
                // function-scope lo/range/scale locals (const and not), operand
                // order flips, Interp(0.6f, 1.0f, meters). The image's order
                // (0.6, 0.001, 0.4 from f13 down) never appears; an inline
                // Interp over the whole expression folds 0.001*0.4 (0x39d1b717).
                // w26-h, MECHANISM (c2rs tap): these five constants are not COLOR
                // candidates, they are `lau` address temps allocated first-fit by
                // regasg (C2RS-BRIDGE 8.2) over the sched3 tuple order, and that
                // order is the temp sid descending: sid 164 (64.0) -> r11, 162
                // (128.0) -> r10, 152 (0.6) -> r8, 143 (0.4) -> r7, 141 (0.001)
                // -> r5.  The image ends 0.4, 0.001, 0.6, so ITS sched3 was
                // 164, 162, 143, 141, 152 -- the 0.6 address must be lowered LAST.
                // Every spelling below leaves the sids 164/162/152/143/141
                // IDENTICAL (0.6 first in the ramp line, `ramp = 0.6f` then
                // `ramp += ...`, a `minBright = 0.6f` local, an `int millimetres`
                // local for the modulo): c1 puts the constant on the right of the
                // commutative ops and folds named constant locals, so the 0.6 use
                // cannot be moved ahead of the 0.001 one.  Inlining `meters` does
                // move it but folds 0.001*0.4 into one constant (92.7).  Open:
                // what source shape lowers the 0.6 constant's address first
                // while keeping the 0.001 multiply separate.
```


## `?CopyPlayerMask@?A0xad24ca77@@YAXHPIAGPIBGH@Z`

`CopyPlayerMask` -- moved verbatim from `src/system/gesture/DrawUtl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 125-139):

```text
        // w8-i: the two lines below are deliberately spelled DIFFERENTLY, and the
        // asymmetry is the whole residual -- 87.4 -> 100.0 (152 B).  Both arms are
        // lowered branchlessly as the mask select `b + (mask & (a - b))`, with the
        // mask coming from the `subfic r11, r6, 0` / `subfe r11, r11, r11` pair.
        // For colors[1] the select is the LIVE one and the cast must sit INSIDE
        // the arms: then b is 0xFFFF, a - b is 0x3000 - 0xFFFF = 0xFFFF3001, which
        // is not a contiguous bit run, so MSVC materialises it as the image's two
        // `rlwinm` masks (0, 31, 19 then 0, 18, 15) plus `addis r11, r11, 1` /
        // `subi r11, r11, 1` -- and the result is already 16-bit, so nothing
        // truncates at the end.  Cast OUTSIDE, b is the int -1, a - b collapses to
        // 0x3001, and the value needs a trailing `clrlwi r11, r11, 16` that the
        // image does not have: that one instruction was the entire 6-row cluster.
        // colors[2] keeps the cast OUTSIDE because its select is never computed --
        // `cmpwi cr6, r6, 0x1` / `beq` branches over it to a `li r10, 0x3000`, so
        // only the narrowing survives and the inner spelling is unobservable.
```


## `?ScreenSpace@?A0xad24ca77@@YAXAAVRect@Hmx@@@Z`

`ScreenSpace` -- moved verbatim from `src/system/gesture/DrawUtl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 156-183):

```text
        // w16-c, measured: `rect.x = V.x;` first then y/z through locals
        // reproduces the image's lfs/stfs x, lfs y, lfs z, stfs, stfs block
        // exactly but drags the TheRnd lwz below it, 83.3 (same with an
        // explicit `Rnd &rnd = TheRnd;` first); three plain member copies
        // 74.9; computing scale first 64.9.
        // MECHANISM (w7-av; w33-c): 94.42 is a one-slot store-order conflict.  The image stores
        // rect.x immediately after loading DrawUtlVec3.x (`lfs f0` / `stfs f0,
        // 0x0(r3)`) and only then loads y and z; we hoist all three loads.  Do
        // NOT try to fix it by writing the x store earlier: both
        // `rect.x = DrawUtlVec3.x;` as the first statement and `float vx = ...;
        // rect.x = vx;` as the first two score 83.33, because moving the store
        // ahead of the other loads also pushes the `__real@3faaaaab` anchor
        // materialisation past the DrawUtlVec3 anchor and costs an extra `lis`.
        // w21-bk (94.42, 4 rows: idx 5/6/10/11 -- the image's `stfs f0, 0x0(r3)`
        // straight after the x load, ours after the y store): real-TU probe
        // search, none reproduces the image -- all 90 dependency-valid orders
        // of {load x/y/z into locals, store x/y/w}; Rnd&/Rnd* aliases of
        // TheRnd; a Hmx::Rect& alias; rect.Set(); Vector2 xy pair; a
        // `const Vector3 &` alias; Min(rect.w, 4/3 / scale) (math/Utl.h's float
        // Min is the same fsel); fsel operand from rect.w vs vz.  The shapes
        // that store x before loading y/z all move `lwz TheRnd` below that
        // store (the image keeps it above), the rest leave stfs x after stfs y.
        // w33-c behaviour verdict: EQUIVALENT.  The same three values land in
        // rect.x/y/w and the scale/fsel arithmetic is identical; only the store
        // order differs (image stores x first, we store y first), with equal
        // instruction counts (36 = 36).  Re-measured on this tree: storing x
        // directly 83.06; direct store plus `Rnd *rnd = &TheRnd;` also 83.06 --
        // forcing the x store drags the TheRnd load off row 4 either way.
```


## `?UpdateBufferTex@@YA_NPAVLiveCameraInput@@PAVRndTex@@W4BufferType@1@PAVGestureMgr@@@Z`

`UpdateBufferTex` -- moved verbatim from `src/system/gesture/DrawUtl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 305-310):

```text
    // w20-c: the colour and player-colour blits advance `texels` itself; a
    // separate dst/dstRow copy cost a callee-saved register and recoloured
    // r26..r30 through the whole function (fuzzy 98.91, 62 register-only rows,
    // every value adjudicated consistent).  texels is not read after either
    // blit, so this is the same behaviour.  The remaining relocation rows are
    // the ICF-folded MakeString<char[N],int,char[M]> spellings.
```
