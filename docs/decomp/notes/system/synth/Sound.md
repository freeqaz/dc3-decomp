# Lane notes: `src/system/synth/Sound.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/synth/Sound.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

File-level (was lines 18-26; nearest following definition: `Sound::Sound`):

```text
// Two SCALAR consts, not `const float sSpeedCaps[2]` (w21-af; the names are
// ours). The image anchors them as base + 4 (lbl_820B6C60 / lbl_820B6C64), which
// an array also gives, but MSVC treats a global ARRAY as aliased by any store
// through a pointer and a scalar as not (standalone cl.exe probe): with the
// array, Sound::SetSpeed's `mSpeed = clamped` store killed the clamp's caps
// loads, so the else-loop could not reuse them on entry and the whole register
// assignment shifted (91.17).
// Two scalars reproduce the image's guarded loop entry that jumps past the caps
// reloads, and SetSpeed goes to 100. Same values, same .rdata layout.
```


## `?SetSpeed@Sound@@QAAXMPAVObject@Hmx@@@Z`

`Sound::SetSpeed` -- moved verbatim from `src/system/synth/Sound.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 531-541):

```text
// It is the `speed` PARAMETER that gets clamped, not the fader transpose. The
// target saves f1 into f31 across the GetTranspose/CalcSpeedFromTranspose calls
// purely so the fsel chain can consume it; a version that clamps
// CalcSpeedFromTranspose(mFaders.GetTranspose()) instead ignores the argument
// entirely and still scores ~90%.
// w14-e/w20-r chased the else-loop entry and the register shift as loop-shape
// and scheduling artifacts; both were the caps being an array (see the comment
// on sSpeedCapMin). w21-af: 100 normalized, fuzzy 99.87 -- one commutative row,
// the obj-branch `fmuls f12, f30, f31` (image: transpose * clamped) where ours
// emits the operands the other way round. Both source orders of the product
// emit ours; `speed = Clamp(...)` too. Register-only, same product.
```
