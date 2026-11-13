# Lane notes: `src/system/char/Char.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DisplayObject@CharDebug@@AAAXPAVObject@Hmx@@@Z`

`CharDebug::DisplayObject` -- moved verbatim from `src/system/char/Char.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 219-245):

```text
// 100% (w7-br; w7-bl left it at 88.89 / 54 rows).  Two levers, both
// source-addressable:
//  * The image re-loads the `mesh` / `mat` function-local statics before
//   every use (0x82340C24, 0x82340CC0 inside the vertex loop, 0x82340D2C,
//   0x82340D54, 0x82340D6C; `stw r3, mat` immediately after New at
//   0x82340BCC and a reload for CreateAndSetMetaMat at 0x82340DA0) because
//   in the original their ADDRESS IS TAKEN, so every store through an
//   unknown pointer may alias them.  A plain `static RndMesh *mesh` whose
//   address never escapes is proven alias-free and hoisted into r30 -- that
//   was the 5th callee-saved GPR, the 0x90 frame and the r27..r31 cascade.
//   Binding a reference (`RndMesh *&meshRef = mesh`) marks the static
//   address-taken and reproduces every reload: 88.89 -> 93.4 for mesh,
//   -> 99.3 raw with mat as well.  (The mangled names prove they are
//   function-local statics, `?mesh@?8??DisplayObject@...`, so the
//   file-scope-static spelling the brief suggested would be a wrong name
//   under name_check -- not tried for that reason.)
//  * The vertex position is stored y, z, x (`stfs f0, 0x4` at 0x82340CEC,
//   `stfs f12, 0x8` at 0x82340CF4, `stfs f11, 0x0` at 0x82340CF8, with the
//   8-byte tex copy between): MSVC keeps the source order of independent
//   component stores, so `pos.Set(x, y, z)` can never produce it.  Writing
//   the three components in that order closes the last five rows (the two
//   Face::Set store orders follow once the loop body matches).
//   NEGATIVE: `vert.pos = Vector3(...)` builds the temp on the stack and
//   copies it (85.9, 47 rows); moving `vert.tex = uv` above pos is inert.
// The `SetObjConcrete<AnimTask>` vs `SetObjConcrete<RndTex>` name in the
// Function Call Diff is an ICF fold -- ObjRefConcrete<T,ObjectDir>::
// SetObjConcrete is the same machine code for every T -- not a wrong callee.
```
