# Lane notes: `src/system/synth_xbox/soundtouch/source/SoundTouch/FIRFilter.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?setCoefficients@FIRFilter@soundtouch@@UAAXPBMII@Z`

`FIRFilter::setCoefficients` -- moved verbatim from `src/system/synth_xbox/soundtouch/source/SoundTouch/FIRFilter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 164-169):

```text
// Throws an exception if filter length isn't divisible by 8
// w18-e (97.5, 2 rows): the image schedules `stw r26, 0xc(r30)`
// (resultDivFactor) after the `lis` of __real@40000000; we store first.
// Inert: pow(2.0f, (int)uResultDivFactor), the assignment folded into the
// pow argument, a const parameter.  The ??_V/RadFree and MakeString call-name
// rows are ICF folds.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: `stw
r26,0xc(r30)` placed two rows later.
