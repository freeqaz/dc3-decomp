# Lane notes: `src/system/rndobj/Wind.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SelfGetWind@RndWind@@IAAXABVVector3@@MAAV2@@Z`

`RndWind::SelfGetWind` -- moved verbatim from `src/system/rndobj/Wind.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 58-115):

```text
            // w7-bt (89.6 -> 96.3 canonical): the three 16-byte rows at
            // 0x50/0x60/0x70 are one Hmx::Matrix3, not three locals.  m.z is
            // the axis (the 4-word copy at 0x8266FDB4..FDCC), m.x the
            // projection, m.y the cross.  Normalize takes &m.y (0x60) out of
            // line at 0x8266FE34, so the m.x stores at 0x8266FDFC/FE04/FE0C
            // are the aggregate staying live across the call, and the axis
            // is reloaded from 0x70/0x74/0x78 afterwards instead of being
            // parked in f29..f31 (the w7-ab residual: 3 callee-saved FPRs
            // where the image saves 1).  A separate `proj` local, whatever
            // its spelling, has its stores elided: an unnamed `Vector3(...)`
            // temp bound to Cross's const-ref param and a TU-local
            // by-value-return helper were both byte-identical at 89.6.
            // The image's final rows are NOT `Multiply(result, m, result)`
            // with m.x = Cross(m.y, m.z) (92.7, and a `Vector3 mx` cross
            // plus explicit rows is 94.6): they are the explicit
            // right-associated rows below (result.y = rx*mx.y + (rz*z.y +
            // ry*c.y) at 0x8266FE98, etc.), with the row-x cross terms
            // spelled inline.
            // RESIDUAL (w7-bt, 96.3 canonical): fmuls/fmsubs operand order
            // inside the first cross (0x8266FE10..FE30) and fnmadds at
            // 0x8266FDF4 -- spelling the cross as explicit `m.y.Set(...)` in
            // the image's operand order is byte-identical to Vec.h's Cross,
            // so the order is the allocator's, not the source's -- and the
            // scheduler's load order after the Normalize call (image loads
            // c.z/z.y/c.y/z.x/ry first, we load c.y/z.x/c.z/z.y/rz).
            // w21-bf (still 96.29444, ~36 rows), measured: naming the three
            // m.x cross terms as float locals is canonical-inert (fuzzy
            // 94.60 -> 94.66); writing every product component-first instead
            // of scalar-first is byte-identical (the image's operand order is
            // scalar-first everywhere, ours mixed -- MSVC picks it); declaring
            // rx/ry/rz AFTER the cross terms 93.8; `Vector3 r = result;`
            // copy 88.9; Cross(m.y, m.z, m.x) + Multiply(result, m, result)
            // re-measured 92.7 (it does give scalar-first operands, but MSVC
            // re-sorts the flat sums x,y,z and stores x first).
            // w26-z (2026-11-02): 4 variants, no movement. Section 8.8's
            // reference lever does not reach this row: binding `m.z` to a named
            // reference is 93.8, and binding `m.y`/`m.z` after Normalize moves
            // only fuzzy (+0.056) with canonical unchanged. What is left is the
            // 8.7 commutative key on FPR temp leaves that are already loaded --
            // the fmuls/fmsubs in the first cross read the same two registers
            // in the other order -- plus sched0's post-Normalize load order.
            // w27-h stop note (96.29444 canonical / 94.68330 fuzzy).  Read end
            // to end against the image (8266FD80..8266FEA0): the three row
            // equations below are the image's exactly.  Expand with
            // M = m.y x m.z and the image computes
            //   result.y = rx*M.y + (rz*m.z.y + ry*m.y.y)
            //   result.z = rz*m.z.z + (rx*M.z + ry*m.y.z)
            //   result.x = rz*m.z.x + (ry*m.y.x + rx*M.x)
            // which is what our crossed terms evaluate to -- no wrong field,
            // constant or call.  What differs is (a) a uniform 4-byte shift of
            // the whole Vector3 temp block (image m.y at r1+0x70 / m.x at 0x50 /
            // cross at 0x60; ours one word lower) and (b) the 8.7 commutative
            // key on already-loaded FPR leaves: our `fnmadds f8, f12, f11, f8`
            // reads the same two registers as the image's in the other order,
            // and objdiff's alignment then reports one extra `lfs`+`fmuls` on
            // our side against three extra fmu ops on the image's.  w26-z and
            // w21-bf already refuted the reference-binding, scalar-first and
            // Cross/Multiply spellings here; w27-h did not find a new lever.
```
