# Lane notes: `src/system/gesture/HandInvokeGestureFilter.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?UpdateBodyPlane@HandInvokeGestureFilter@@AAA_NABVSkeleton@@M@Z`

`HandInvokeGestureFilter::UpdateBodyPlane` -- moved verbatim from `src/system/gesture/HandInvokeGestureFilter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 32-37):

```text
    // The squared-length test is spelled z + x + y on purpose: for a three-term
    // sum of products A + B + C this compiler emits `fmuls B; fmadds C; fmadds A`
    // (measured on four permutations, w7-bv), and the image at 0x82DFDED8 is
    // `fmuls f30,f30` (x) / `fmadds f28` (y) / `fmadds f29` (z).  The natural
    // `LengthSquared(shoulderVec)` and `x*x + y*y + z*z` are 100.0 canonical
    // but leave a 2-row f28/f30 swap; this order is byte-identical (117/117).
```

Inside the body (was lines 53-58):

```text
        // Body "side" vector = Cross(yAxis, smoothedBodyNormal), with Value()'s
        // result consumed as an UNNAMED temporary argument: that is what makes
        // MSVC read the components through the returned sret pointer
        // (`mr r11, r3` at 0x82DFDF54, `lfs 0x8/0x4/0x0(r11)`) and hoist
        // &unk40 into r31 before the call.  A named `Vector3` or a named
        // `const Vector3 &` both read the slot through r1 instead (w7-ap).
```


## `DotZYX()`

`DotZYX` -- moved verbatim from `src/system/gesture/HandInvokeGestureFilter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Attribution: no function named `DotZYX` in report.json (unit); filed under the source name

Above the definition (was lines 73-79):

```text
// w21-ao: the image sums both elevation dot products as x + (y + z), the z
// product first (0x82DFE234-0x82DFE260: `fmuls` uz*rz, `fmadds` y, `fnmadds` x;
// left arm 0x82DFE268-0x82DFE2A8).  The engine Dot() compiled to y + (x + z) for the right
// arm -- /fp:fast re-sorts a flat sum per context -- which is a different
// rounding on every target.  This helper pins the image's association; it
// takes references like Dot(), so MSVC still materialises the dead &unk40
// (`addi r11, r30, 0x40`).  The name is mine.
```


## `?CalcInPose@HandInvokeGestureFilter@@AAA_NABVSkeleton@@M@Z`

`HandInvokeGestureFilter::CalcInPose` -- moved verbatim from `src/system/gesture/HandInvokeGestureFilter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 84-106):

```text
// w20-p branch-landing row 292 (image `bne` onto `li r11, 0x1`, ours one row
// past it): ARTIFACT -- jump threading; on that edge r11 already holds 1.
// w21-n (99.02 canonical, unchanged): every value re-checked against the image
// (both elevations/forwards, the -0.0 atan2 denominators, GetBend's
// shoulder/elbow/hand args, the tilt acos) -- same operands, register and
// component order only.  The residual is ONE 16-byte frame slot (image 0x130,
// ours 0x120): the image never lets a later unk4.Value() temporary reuse a
// named slot (temps at 0x80/0x90; ours 0x70, 0x80, 0x80, 0x50).  Measured:
// naming only the 4th Value() result restores the 0x130 frame and the
// 0x80/0x90 temps but reads it through r1 (98.9); naming the 1st/2nd/3rd
// instead is 95.2-95.7; declaring `lateral` early (three placements) is
// inert; building the spine in `lateral` is 98.53 fuzzy (worse).
// w21-ao (99.0368 canonical after the DotZYX fidelity fix, 37 of 327 rows):
// still the 16-byte frame slot (image temps 0x80/0x90/0x90/0x80 for the
// spine/forward Value() calls; ours 0x70/0x80/0x80/0x50 -- MSVC packs our
// temps into lateral's not-yet-live slot and rightArmDir's dead one), plus
// the inner-pair pick of the FORWARD dots and the left elevation (image
// rounds the y product first in both forwards and the z product in the left
// elevation; ours z, z, y -- no text order of the inner pair moved it, MSVC
// canonicalises it) and the tilt-test bool tail.  Measured in a real-flag
// probe, none better: spine built in `lateral`, `lateral` declared before
// the spine, `lateral = spine; lateral -= proj`, a DotZYX-style helper on the
// forwards (57 vs 20 probe rows), Dot argument order on the forwards.
```

Inside the body (was lines 222-231):

```text
    // w27-m (2026-11-02), census row STRUCTURAL:opcode addi:3/stwu:1 vs
    // addi:3/clrlwi:1/stwu:1: adjudicated behaviour-equivalent.  The
    // instruction STREAMS are identical (327 vs 327); every diff_arg row is a
    // stack-slot or FPR number, and the frame difference (0x130 image vs 0x120
    // here) is the same five-vs-four 16-byte Vector3 slot allocation described
    // at the top of the function -- no instruction computes a value the other
    // side does not.  The one charged opcode, the image's extra
    // `clrlwi r10, r11, 24` before the tilt test, exists only because the image
    // parks leftInPose in r10 and tiltOk in r11 while we do the reverse; both
    // paths AND together the same three bools.
```
