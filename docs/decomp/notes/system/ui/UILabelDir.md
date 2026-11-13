# Lane notes: `src/system/ui/UILabelDir.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/ui/UILabelDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

File-level (was lines 204-238; no definition follows it):

```text
// w9-b 2026-09-30 -- CLOSED at 100.0% (normalized AND fuzzy, full ninja, 168 B).
// The w8-j note below was right about the symptom and wrong about the cause: it
// is source shape, and it took TWO independent changes that only pay together.
//
// The image's control flow is not one merge, it is an early return plus a merge:
//   row 27  lwz r3, 0xc(r11)      mColors[state] straight into the RETURN reg
//   row 28  cmplwi cr6, r3, 0     UNSIGNED -- a test of the LOCAL, not the member
//   row 29  bne cr6, <epilogue>   returns r3 directly, bypassing the merge
//   row 30  lwz r11, 0x2fc(r31)   mDefaultColor into r11
//   row 31  cmpwi cr6, r11, 0     SIGNED -- a test of the MEMBER
//   row 32  bne cr6, <row 35>
//   row 33/34 gColor into r11 as well
//   row 35  mr r3, r11            the two fallbacks merge in r11, then copy
// So the fallback pair needs a register OTHER than the one holding
// mColors[state], and the first arm must not go through the merge at all.
//
// Measured ladder (each a full ninja):
//   97.619  UIColor *color = mColors[state]; if (!color) { color = mDefaultColor;
//           if (!mDefaultColor) color = gColor; } return color;   <- the old shape
//   97.619  same with the inner test written as if/else -- byte-inert
//   96.190  fallback in its own local, still nested inside `if (!color)`  (= w8-j (a)/(b))
//   96.190  three bare `return` statements
//   28.762  color = mDefaultColor ? mDefaultColor : gColor;
//   85.238  the two fallback tests as sequential (non-nested) ifs
//   98.571  flat if / else if / else into ONE uninitialised local, single return
//           -- this is what buys rows 30..35: an uninitialised local forces all
//           arms to merge in r11 rather than coalescing into r3
//   100.0   the shape below: early `return color;` for mColors[state] (keeps the
//           unsigned test on the local and the direct r3 return) PLUS a SECOND
//           local for the if/else fallback pair (keeps the r11 merge).
// The two halves are not separable: with one variable MSVC coalesces the whole
// chain into r3 and rows 30..35 break; without the early return the first arm
// joins the merge and rows 27..29 break.
// Keep `if (mDefaultColor)` testing the MEMBER -- testing the local there emits
// `cmplwi` where the image has the signed `cmpwi` at row 31.
```
