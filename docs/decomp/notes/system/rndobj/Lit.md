# Lane notes: `src/system/rndobj/Lit.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Projection@RndLight@@QAA?AVTransform@@XZ`

`RndLight::Projection` -- moved verbatim from `src/system/rndobj/Lit.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 125-158):

```text
        // HISTORY (kept so nobody re-derives it):
        // NEGATIVE RESULT (w7-ao, 2026-09-14): declaration ORDER of the
        // xRow/yRow/pos Vector3 copies does not move their 16-byte frame slots
        // (`pos` stayed at 0xa0 with either order).
        // NEGATIVE RESULT (w7-bi, 2026-09-14): reading pos before yRow through
        // hoisted `_fprN` float locals is inert too (93.3 either way).
        //
        // MECHANISM (w7-bn, 2026-09-15, 93.3 -> 100.0 canonical, 191 rows all
        // equal): the (0xa0,0xb0) slot swap and the whole 85-139 fmuls/fmadds
        // cluster were ONE cause -- the association order of the three dot
        // products below.  MSVC lowers `A + (B + C)` (all products) as
        // fmuls(B or C) / fmadds(the other) / f[n]madds(A), and picks which of
        // B/C is the fmuls by VALUE NUMBER: the product whose pos.* operand was
        // first numbered comes first.  The image's order is y, z, x for v.x,
        // v.y and v.z alike (0x826BAF28 fmuls pos.y; 0x826BAF34 fmadds pos.z;
        // 0x826BAF88 fnmadds pos.x), which needs x as the OUTER term and pos.y
        // numbered before pos.z -- so pos.* must be read directly in v.x in
        // x, y, z order, not hoisted into locals declared z, y, x.  The slot
        // pair follows from that: whichever copy's LAST load is scheduled
        // earlier gets 0xa0, and the load schedule follows the dot-product
        // order (yRow.x at 0x826BAF40 is the last yRow read, pos.x at
        // 0x826BAF4C the last pos read).  Two more rows are operand order:
        // an fmuls puts the higher-numbered operand first, so the v.x products
        // are spelled `xRow.* * pos.*` (0x826BAF28 `fmuls f6, f0(pos.y),
        // f12(xRow.y)`) while v.y/v.z keep pos first because nz*/the columns
        // are numbered earlier.  The last row, `fneg f28` between the yRow
        // `cmplwi` and its `bne` (0x826BAE78/0x826BAE7C), is the three
        // negations being one statement (`Negate`) rather than three.
        // Refuted on the way, full ninja each: flat `x + y + z` for all three
        // (98.9: the outer term became y); hoisted pos locals declared y, z, x
        // with `x + (y + z)` (99.0, pos-second operand order); `nzy` declared
        // before `nzx` (98.9, f29<->f30); `nzz` computed after the yRow copy
        // (93.3, wz kept in r30 across the call); `x + (y + z)` alone with
        // the old z, y, x locals (92.4: slot swap fixed, fmuls order not).
```
