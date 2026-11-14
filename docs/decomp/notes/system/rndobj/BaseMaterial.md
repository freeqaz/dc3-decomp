# Lane notes: `src/system/rndobj/BaseMaterial.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?PropValDifferent@BaseMaterial@@IAA_NVSymbol@@PAV1@@Z`

`BaseMaterial::PropValDifferent` -- moved verbatim from `src/system/rndobj/BaseMaterial.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 241-280):

```text
// RESIDUAL (w13-a, 98.82 canonical, 2 rows): the assert's re-test of the
// gDefaultMat load is `cmplwi cr6` in the image and `cmplwi` (cr0) here.
// Inert: moving the MILO_ASSERT inside the `if (!base)` arm.  A ternary
// `base ? base : gDefaultMat` gets the cr6 compare but un-threads the first
// branch (86.6).
// w26-s: the CR field follows the SYMBOL the condition names, not the spelling
// of the condition. `MILO_ASSERT(gDefaultMat, 0x133)` inside the if arm emits
// the image's `cmplwi cr6` (99.90, exactly one row left: its MakeString cond
// string is "gDefaultMat" where the image's is "base"); every form that keeps
// the message "base" emits cr0.  Also INERT (98.82): `base != 0`,
// `base != nullptr`, `NULL != base`, `!!base`, a named `baseOk`,
// `if (base == 0)`, an `if (base) {} else {}` arm, and the assignment spelled
// through a cast (`(BaseMaterial *)`, `static_cast`), a double deref
// `*(&gDefaultMat)` or a `BaseMaterial *const *` slot.
// The obvious reconstruction -- bind the arm's value to a local NAMED `base`
// so the message stringifies correctly -- was measured and does NOT work:
// `BaseMaterial *base = baseIn;` (parameter renamed) with the assert on the
// local gives 96.86, because c1 does not coalesce that copy; declaring the
// local uninitialised and filling it in the arms gives 95.88 (assert in the
// null arm) / 86.67 (assert after the join); and binding the global to a
// throwaway local before `base = fallback` is BYTE-IDENTICAL to the current
// form, i.e. the copy folds and the assert still tests the variable -> cr0.
// So the cr6 only survives while the condition IS the global's own load node,
// and any binding through a variable collapses it.  Open question: which
// source construct keeps the load node while `#cond` still reads "base".
// w31-e 2026-11-03 (98.824, 2 rows): the residual is ONE CR FIELD.  All 102
// instructions align 1:1, every opcode and constant agrees, and the only
// difference is row 17/18 -- image `cmplwi cr6, r30, 0x0` / `bf 26`, ours
// `cmplwi r30, 0x0` / `bf 2` -- the compare's CR field, on the SAME register
// (r30), at the SAME position, branching to the SAME target.  Nothing in the
// dataflow differs: both builds load gDefaultMat into r30 and test r30.
// NOTE for the next lane: this round's brief cites `MILO_ASSERT(gDefaultMat,
// 0x133)` as "reaching 99.90".  RE-MEASURED IN THIS TREE and it does not:
// 90.39216 canonical / 89.55882 fuzzy, against 98.82353 for the current form.
// Removing `base` from the assert makes c1 test a fresh load of gDefaultMat
// instead of r30, which costs far more rows than the CR field is worth.
// Also measured inert here: unbraced null arm (byte-identical), `base !=
// nullptr` (98.726, the string change costs one row), assert moved before the
// null arm (94.069), ternary assignment (89.608).  So the lever, if it exists,
// is not the assert's spelling; treat this as a placement residual.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.
