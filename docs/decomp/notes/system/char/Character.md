# Lane notes: `src/system/char/Character.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawShadow@Character@@UAAXABVTransform@@M@Z`

`Character::DrawShadow` -- moved verbatim from `src/system/char/Character.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 372-388):

```text
        // The shadow plane is Plane(point, normal) with normal (0,0,1) and the
        // point's z already offset by planeD, so its constant is
        // `-(0*p.x + 0*p.y + 1*(p.z + planeD))`.  The image keeps the
        // zero-weighted products (`fmuls y*0` / `fmadds x*0`) and the offset as
        // its OWN term (`fadds z,planeD` then `fadds dot,that`), so both the
        // offset and the products have to survive c1 as separate values.
        // w39-f: writing the offset as a POINT TEMP whose z is advanced in
        // place, and building the dot from that temp's components, reproduces
        // the image exactly (143/143 instructions, 572 B, canonical and fuzzy
        // 100.0).  A named `float zOff` instead produces the same shape but
        // re-schedules it -- the offset is hoisted above the `fmadds` and the
        // component loads come out z,y,x -- for 97.18.  Refuted, do not land:
        // `float dot = 0.0f * worldPos.y + 0.0f * worldPos.x;` as ONE statement
        // reads HIGHER (98.86) on a body one instruction SHORTER than the
        // image (142 vs 143), because /fp:fast factors the two zero-weighted
        // products into `(x+y)*0` and drops the image's `fmuls`; objdiff pays
        // for that as a cheap `replace` rather than a delete+insert pair.
```
