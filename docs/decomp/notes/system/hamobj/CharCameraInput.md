# Lane notes: `src/system/hamobj/CharCameraInput.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ResetSkeletonCharOrigin@CharCameraInput@@QAAXXZ`

`CharCameraInput::ResetSkeletonCharOrigin` -- moved verbatim from `src/system/hamobj/CharCameraInput.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 45-54):

```text
    // 100 (w21-g, was 95.55 canonical / 13 rows).  The image's two dead
    // `addi r10, r11, 0x10` / `addi r11, r11, 0x20` after `addi r11, r3,
    // 0x23f0` are &m.y and &m.z formed off a MATRIX reference: the three
    // Set() calls go through a local `Hmx::Matrix3 &m`, while Reset() is
    // still called on the member.  That one binding also fixes the final
    // 4-word `mNatalXfm.v = worldPos` copy schedule (rows 77-85, which the
    // earlier note filed as "pure scheduling, no source order expresses it").
    // Binding the whole Transform instead (`Transform &xfm`) parks the base
    // in r30 across the DrawScale() calls -- 90.0; `m.Identity(); v.Zero();`
    // spelled at the call site -- 94.4.
```
