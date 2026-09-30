# The map's COMDAT flag gates same-TU clobber propagation

**Status:** measured 2026-09-30 (lane w8-p), one-word fixes, three functions closed.

## Symptom

A caller is stuck well below 100% and the whole gap is its **prologue**. The image
spills parameters into non-volatile registers across a call; we emit no
callee-saved registers at all and read the same values back out of *volatile*
registers after the call. `run_objdiff` reports the difference as a pile of
`delete` rows for the target's `std r30/r31` / `stfd f31` / `bl __savegprlr_N`
plus a `stwu` frame-size `diff_arg`, and then a register-swap cascade downstream.

The correct diagnosis is easy to reach and was reached repeatedly: MSVC has
**propagated the callee's real register usage into the caller**, which it may
only do for a callee whose body it already knows. Hence "reaching the image
needs the callee to be invisible to this TU" — and since the callee is right
there in the same `.cpp`, the residual gets certified as a floor.

## The knob

`orig/<title>/ham_xbox_r.map` has a flag column that distinguishes the two cases,
and it had been ignored:

| flag | meaning |
|---|---|
| `f` | ordinary function in plain `.text` — an out-of-line definition in a `.cpp` |
| `f i` | a **COMDAT** — what MSVC emits for an `inline`, in-class, or template definition |

A COMDAT function may be replaced at link time by an equivalent definition from
another translation unit, so the compiler **may not assume its register usage**.
The caller therefore has to spill to non-volatiles. An ordinary `.text` function
in the same TU has exactly one body, and MSVC does propagate.

So: **if the map says `f i` and we define it out-of-line, add `inline`.**

## Worked example (dc3, `rndobj/Mesh.cpp`, `rnddx9/Mesh.cpp`)

```
8263c030 ?GreaterEq@PatchVerts@@IBAHH@Z                    f i   rndobj:Mesh.obj
8263e178 ?HasVert@PatchVerts@@QBA_NH@Z                     f i   rndobj:Mesh.obj
8263f1e8 ?Clear@PatchVerts@@QAAXXZ                         f i   rndobj:Mesh.obj
8263f238 ?Add@PatchVerts@@QAAXH...@Z                       f i   rndobj:Mesh.obj
826201c8 ?ScaleAddEq@@YAXAAVMatrix3@Hmx@@ABV12@M@Z         f i   rnddx9:Mesh.obj
8263b860 ?FaceCenter@@YAXPAVRndMesh@@PAVFace@1@AAV...@Z    f     rndobj:Mesh.obj   <-- control
```

`FaceCenter` is the control that makes this a measurement rather than a story:
the image genuinely **mixes** the two classes inside one object, so the flag is
carrying information and not just describing the whole build.

One `inline` keyword per definition:

| function | before | after |
|---|---|---|
| `PatchVerts::HasVert` | 81.40741 | **100.0** |
| `PatchVerts::Add` | 96.77778 | **100.0** |
| `RndMesh::OnSync` | 97.58194 | **100.0** normalized |
| `ScaleAddEq(Transform&)` | 63.26667 | 93.33334 |

Whole binary: 0 regressions over 48,367 rows.

## How to find candidates mechanically

For every non-template, non-thunk symbol the map attributes to your object,
compare the map flag against whether your source defines it inline. Skip
`?$` / `stlpmtx_std` / `??_` / `??$` / `$4` / `$R4` / `__unwind` / `__catch`.
Remember that constructors and destructors have **no return type**, so a regex
that requires one before `Class::method(` silently misses them — that bug hid
`??0StyleState@RndText@@` on the first pass.

## Caveats, both measured

1. **The lever is not universal.** It pays only where the caller's residual
   actually *is* the conservative-prologue gap. Marking `ScaleAddEq(Transform&)`
   and `DxMesh::FurWeight` inline (both `f i`, so correct for fidelity) is inert
   for their callers: `DxMesh::OnSync` 95.08458, `CacheFurTransform` 99.55705,
   `DrawFur` 98.45977, all unchanged. Same for
   `RndText::StyleState::StyleState`: every caller unchanged.
2. **A cross-TU caller means `inline` in the `.cpp` will not link** — the
   definition has to move into the header. `DxMesh::VertSize` / `VertFVF` are
   called from `MultiMesh.cpp`, so they moved to `rnddx9/Mesh.h`.

## What this retires

Two independently-written source notes in this repo certified this class as a
floor on the reading "the propagation is not source-order dependent, and the
callee cannot be made invisible to this TU". Both halves were true and the
conclusion was still wrong: the callee does not have to be *invisible*, it has
to be a *COMDAT*. Before certifying any conservative-prologue residual as a
floor, read the map flag.
