# The map's COMDAT flag gates same-TU clobber propagation

**Status:** measured 2026-09-30 (lane w8-p), one-word fixes, three functions closed.

> ## ⚠ MECHANISM CORRECTED the same day (lane w8-q) — read this before acting
>
> **The lever is real; the causal story below is not established.** Both halves
> are stated because only one of them is evidenced.
>
> **What stands.** Adding `inline` to a same-TU callee genuinely closed three
> rows in `rndobj/Mesh` / `rnddx9/Mesh` — 81.407 → 100.0, 96.778 → 100.0,
> 63.267 → 93.333 — confirmed by the coordinator in a whole-binary row diff
> against a baseline pinned to the lane's exact base. The map column is also
> real and discriminating: **79,320 bare `f` against 31,754 `f i`**.
>
> **What is refuted: "if the map says `f i` and we define it out-of-line, add
> `inline`" cannot be what matches the class.** MSVC/Xenon puts **every**
> function we compile into its own COMDAT, so no `.cpp`-vs-header spelling moves
> a symbol between the two map classes. The actual carrier of `f i` vs bare `f`
> is the COMDAT **selection type** in the section symbol's aux record at
> offset 14 — `f i` ↔ `IMAGE_COMDAT_SELECT_ANY`, bare `f` ↔ `NODUPLICATES`.
> Measured against this page's own control pair, our objects emit
> `NODUPLICATES` for **both**, so we reproduce **neither** class:
>
> ```
> ?GreaterEq@PatchVerts@@IBAHH@Z   image `f i`     ours COMDAT sel=NODUPLICATES
> ?FaceCenter@@YAXPAVRndMesh@@...  image bare `f`  ours COMDAT sel=NODUPLICATES
> ```
>
> Positive control that our toolchain *can* reach `ANY`: `Spotlight.obj` emits
> `sel=ANY` for `??$MakeString@…` and `?erase@?$vector@VFace@…`. And the
> dtk-carved **target** objects record no Selection byte at all (section `/50`),
> so the map is the only carrier — which is why this went unnoticed.
>
> **Two floors this lever was reached for, and did NOT move.**
> `Spotlight::BuildBeam` is the textbook symptom of the Symptom section below
> (image `bl __savegprlr_14`, frame `0x130`; ours `_16`, frame `0x120`), and
> after matching its one same-TU callee's selection type it is **byte-identical
> at 85.3415 with `__savegprlr_16` unchanged**.
> `SpotlightDrawer::ApplyLightingApprox` is out of reach **by construction**:
> same-TU clobber propagation needs a same-TU callee, and its callees live in
> `os:Debug.obj`, `rndobj:Trans.obj` and `rndobj:BoxMap.obj`. Its
> `__savegprlr_26` + 1 FPR against our `_27` + 2 FPRs **stands unexplained**.
>
> **How to use this page, in order.**
> 1. Establish the callee is **same-TU**. If it is cross-TU, stop — the whole
>    mechanism is unavailable, whatever the map says.
> 2. If same-TU, try `inline` and **measure**. It has worked three times and
>    nobody can presently explain it in terms of the emitted object, so treat it
>    as an empirical lever, not a theory.
> 3. Do **not** treat a prologue save-set difference as evidence *for* this
>    lever. See `BuildBeam` above.
> 4. For the real surface area, run the read-only
>    `scripts/analysis/comdat_selection_audit.py` (no ninja, no DB). Over four
>    units it found **7 mismatches among 1,396** mapped functions, only **4** in
>    the actionable direction (ours `NODUPLICATES`, image `ANY`). Closing those
>    four was **fidelity-only: 0 regressions, 0 score movement.**

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
