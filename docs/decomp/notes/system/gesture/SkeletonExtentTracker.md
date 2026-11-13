# Lane notes: `src/system/gesture/SkeletonExtentTracker.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?GetViewBox@SkeletonExtentTracker@@ABA?AVRect@Hmx@@XZ`

`SkeletonExtentTracker::GetViewBox` -- moved verbatim from `src/system/gesture/SkeletonExtentTracker.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 60-67):

```text
        // w8-i BUG FIX: the y component is mMin.y, not mMax.y.  0x82DFE79C loads
        // `lfs f0, 0x30(r4)` (mMin.y) for the FLT_MIN/FLT_MAX guards and the image
        // stores that same f0 into the rect at 0x82DFE7BC (`stfs f0, 0x4(r3)`);
        // mMax.y (0x38, loaded at 0x82DFE7B0) is consumed only by the
        // `fsubs f12, f12, f0` height on the next line and never stored.  We
        // passed mMax.y, so every view box sat at the TOP of the tracked extent
        // instead of the bottom -- a full box-height offset on the mesh UVs that
        // ApplyToMeshVerts lays out.
```
