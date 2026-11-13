# Lane notes: `src/system/gesture/StreamRenderer.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawToTexture@StreamRenderer@@IAAXXZ`

`StreamRenderer::DrawToTexture` -- moved verbatim from `src/system/gesture/StreamRenderer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 643-658):

```text
            // w26-s: the 0x52 site carries rows 662-667 (the image loads
            // x,y,z in ascending offset order, we load z,y,x) -- emission
            // ORDER only, every src/dst address pair identical.  MEASURED
            // INERT at 98.740 (byte-identical): binding
            // `const Vector4 &c5 = mCrewPhotoPlayerCenters[5]` (the section
            // 8.8 base-rebinding lever) and binding it through a pointer.  So
            // this is a scheduling phase, not a memory-operand key row.
            // Naming the three components is also inert; a whole-Vector4 copy
            // (`Vector4 c5src = ...`) costs 95.39, and swapping y/z in the
            // argument list is not a spelling (it changes the value).
            // w28-c: the one lowering w26-s did not try -- building center5 by
            // COMPONENT ASSIGNMENT (`Vector4 center5; center5.x = ...; ...`)
            // rather than by the 4-argument ctor -- is also byte-identical
            // here, as are three named float locals feeding the ctor.  So the
            // ascending x,y,z load order is not reachable by changing how the
            // value is assembled; it is the scheduler's phase.
```

Inside the body (was lines 666-684):

```text
        // NOTE: the target runs the two int->float conversions through ONE
        // temp slot (0x50, staged/reloaded alternately); we allocate a second
        // at 0x58. Hoisting the two Width()/Height() results into named float
        // locals first (the spelling blurRect below already uses) is
        // byte-for-byte INERT, so that is not the lever.  REFUTED as well
        // (wave 7, lane w7-y): a default-constructed Rect with four member
        // assignments -- the shape that would give the two conversions
        // sequential temp lifetimes, and hence one reused slot -- costs
        // 98.74 -> 96.70 and 9 extra instructions; MSVC then keeps the whole
        // rect in registers across the block and re-permutes 12 stack slots.
        // w8-r: two more spellings MEASURED INERT here, both byte-identical at
        // 98.740 -- constructing the Rect as an unnamed temporary inside the
        // DrawRect argument list, and hoisting Width()/Height() into named
        // `int` locals first (distinct from w7-y's named FLOAT locals).  The residual is that the image stages BOTH
        // int->float conversions through the SINGLE 8-byte temp at 0x50(r1)
        // (store W, `lfd f0, 0x50`, store H over it, `lfd f13, 0x50`, then both
        // fcfid) while we allocate a second temp at 0x58 and serialise the two
        // chains on f0.  The image itself uses TWO temps at the blurRect site
        // 80 instructions below, so one temp is not a property of Hmx::Rect.
```
