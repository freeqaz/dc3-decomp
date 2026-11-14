# Lane notes: `src/system/obj/PropSync.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?PropSync@@YA_NAAVMatrix3@Hmx@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`

`PropSync` -- moved verbatim from `src/system/obj/PropSync.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 89-98):

```text
// w8-l: 99.979004 normalized, the only function short of 100% in this unit
// (11/12).  12 charged rows out of 381: four fmuls with their two operands
// exchanged ([186] f11/f13, [197] f13/f12, [314] and [371] f12/f0) and six
// lfs's reading the sibling component (+4/-8), all inside the y_scale and
// z_scale arms.  The x_scale arm, which is spelled component-wise
// (_m.x.x/_m.x.y/_m.x.z), already matches.
// Refuted: spelling y_scale and z_scale component-wise too, to mirror x_scale,
// is a large REGRESSION -- 99.979004 -> 98.745410.  The image really does use
// the whole-vector `_m.y *= ratio` / `_m.z *= ratio` form for those two arms
// and the per-component form only for x; the asymmetry below is deliberate.
```

Inside the body (was lines 132-144):

```text
                // w18-d: the three lengths are spelled out rather than through
                // Length(): the extra inline level made MSVC lead the sum with z
                // (y/z_scale) instead of the image's y-first schedule. 99.979 ->
                // 99.995; x_scale still loads z before x (0x825BDD80/84), and the
                // y.y/z.y products of Scale() and of `_m.y *= ratio` keep the
                // other operand order (4 commutative rows); per-component
                // `_m.y.x *= ratio` loses the r29 = &_m.y binding (97.8).
                // w22-a25: the image sums (y*y + x*x) + z*z (0x825BDD74..
                // 0x825BDD9C: fmuls y*y, fmadds x, fmadds z); the flat spelling
                // let /fp:fast build (y*y + z*z) + x*x instead. The parens keep
                // the image's association (normalized 99.890 -> 99.995); only
                // which of x*x / y*y is fused remains. Native (no fast-math,
                // no FMA) computes (x*x + y*y) + z*z either way.
```

p2-b99 (2026-11-14, name_check, 99-100 band audit): NOT A BUG. The Length() sum is the same three squares; only the association order differs (x,y vs y,x under z).
