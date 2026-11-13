# Lane notes: `src/system/rnddx9/Tex.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `HierarchicalZTiles()`

`HierarchicalZTiles` -- moved verbatim from `src/system/rnddx9/Tex.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Attribution: no function named `HierarchicalZTiles` in report.json (unit); filed under the source name

Above the definition (was lines 87-95):

```text
// Hi-Z tile count for a depth surface: an inline whose by-value parameters
// are overwritten with their aligned values.  w7-bt: this is what homes the
// 16-aligned height to 0x88 in the depth path (0x82C0D8xx `stw r11, 0x88`) --
// the write to `Height` materialises the parameter's slot, and the CSE with
// the colour-tile `(h + 15) & ~15` in the enclosing block gives it a second
// use (the same rule leaves `Width`, used once, in a register).  96.2 -> 97.6
// canonical, 110 -> 68 rows.  Refuted: the same treatment for the tile count
// (a `SurfaceTiles(w, h, fmt)` inline with written params, both paths) is
// 97.1 -- the colour path then re-schedules its `stw r11, 0x94` / `mr r27`.
```


## `??0DxTex@@IAA@XZ`

`DxTex::DxTex` -- moved verbatim from `src/system/rnddx9/Tex.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 115-119):

```text
    // 100 (w12-c): mLockedRect is cleared by its own LockedRect() ctor at its
    // init-list position (see Tex.h), which is where the image's 0x9c/0xa0
    // stores sit and what leaves the dead `addi r11, r30, 0x9c` at 0x82613844.
    // w7's measured alternatives (memset in the body, an explicit loop with or
    // without `mLockedRect()` in the init list, a pointer loop) all missed.
```


## `?StartCompress@DxTex@@QAAPAXW4AlphaCompress@RndTex@@@Z`

`DxTex::StartCompress` -- moved verbatim from `src/system/rnddx9/Tex.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 157-164):

```text
    // RESIDUAL (w7-ao, 99.87 canonical): the image anchors this loop's induction
    // register on &levels[i].textureSurface (desc+0x40, `subi r28, r30, 0x20`
    // for the scratch half) where we anchor on &levels[i] (desc+0x14). Same
    // addresses either way -- it is a base-register bias, worth 6 offset rows.
    // NEGATIVE RESULT (w7-ao, 2026-09-14): introducing `CompressLevel &level =
    // desc->levels[i];` here, the spelling DoCompress and FinishCompress both
    // use, moves the anchor to levels[i]+0x20 (closer) but costs a 16-byte frame
    // shift and 12 more register rows: 99.87 -> 99.13. Left explicit.
```


## `?MakeDrawTarget@DxTex@@UAAXXZ`

`DxTex::MakeDrawTarget` -- moved verbatim from `src/system/rnddx9/Tex.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 539-547):

```text
        // RESIDUAL (w7-ak, 91.3 canonical): 8 instructions, same multiset, different
        // schedule. The target hoists the whole `mType != kShadowMap` bool computation
        // above the `sCurrent` store; we emit the store in the middle of it. Hoisting
        // the bool into a named local moves the load but not the subi/subfe, and
        // swapping the two statements puts the `stb` before the `stw`, which the
        // target does not do.
        // w16-c: `int type = mType;` read before SetCurrent plus
        // `type - kShadowMap != 0` is inert (91.3) -- MSVC still sinks the
        // lwz below the sCurrent store, so no aliasing order is involved.
```


## `?SyncBitmap@DxTex@@MAAXXZ`

`DxTex::SyncBitmap` -- moved verbatim from `src/system/rnddx9/Tex.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 661-690):

```text
// RESIDUAL (w7-bm, 2026-09-14, 96.2 canonical, 110 rows; was 93.97): every
// call, store and arithmetic row matches.  What is left, in order of weight:
//  (a) the 4-byte MakeString temp class A sits at 0x80 in the image with the
//      PhysMemTypeTracker at 0x84; ours is the pure swap (tracker 0x80, A 0x84),
//      ~14 rows of displacement.  Declaring d3dcaps / params / colorTiles /
//      hzTiles at function top is byte-inert; so is a `D3DTexture *tex` local.
//  (b) CLOSED by w7-bt (97.6 canonical, 68 rows): the depth path's early,
//      homed 16-aligned height (`stw r11, 0x88`) is the written by-value
//      `Height` parameter of an inlined hi-Z helper -- see HierarchicalZTiles
//      above.  A textual duplicate of `(h + 15) & ~15` is worse (95.6), and
//      helper bodies that copy their params into locals instead of writing
//      them are byte-inert.
//  (c) prologue constant order (`ori r24` / `li r25, 0x1400` / `lis r22`) and
//      `stw r11, 0x94` (the tile count's home store) landing before, not after,
//      `mr r27, r11`.
//  (d) the DxRndTexMgr::CreateSurface argument load/store order and the
//      `addi r27, r30, 0x80` placement in its else branch.
//  (e) the final XGSetTextureHeader block: the image spills the `new` result
//      to 0x5c(r1) before loading the members and stores mTexture last; the
//      in-argument `new D3DTexture` form is byte-inert.
//  (f) two extra `b <epilogue>` trampolines.
// Levers that DID move it, kept above: the by-value CreateEdramSurface wrapper
// (MSVC homes an inlined callee's by-value parameters at the inline site),
// UINT offsets in XGSetTextureHeaderEx (xgraphics.h), the single `while
// (nextMip())` loop with both invariant stores inside it, a shared `tiles`
// local for both paths, and the D3DSURFACE_PARAMETERS field assignment ORDER
// (ColorExpBias, Base, HierarchicalZBase, HiZFunc) after the memset.  Also
// byte-inert or worse: 0xFFFFFFFF for the -1 offsets, a named `mip` local in
// the mip loop, `if (nextMip()) do {} while`, dropping the zero assignments
// after the memset (95.8), and w7-g's in-branch hoist of the tile math.
```
