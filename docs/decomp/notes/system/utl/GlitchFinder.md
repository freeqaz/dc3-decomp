# Lane notes: `src/system/utl/GlitchFinder.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Dump@GlitchPoker@@QAAXAAVTextStream@@H@Z`

`GlitchPoker::Dump` -- moved verbatim from `src/system/utl/GlitchFinder.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 74-99):

```text
// 99.994600 (w8-l re-measured; the 99.99507 above was from an earlier tree), 5 rows,
// and every one of them is a two-load scheduling tie with
// the arithmetic already identical:
//  * [33]/[34] -- `mTime - smLastDumpTime`. The target loads mTime (0x0(r29))
//    first and smLastDumpTime second; we load them the other way round. The
//    consuming `fsubs f31, f13, f0` is equal on both sides.
//  * [90]/[91]/[92] -- `mTimeEnd - mTime` in the leaf test. Both sides compute
//    (0x44(r30)) - (0x0(r29)); they differ only in which value lands in f0 and
//    which in f13, which flips the (non-commutative) fsubs operand *registers*
//    while leaving the subtraction the same.
// Nothing in source selects which of two independent loads issues first, and
// the expression order is already the target's, so there is no lever here --
// this is register assignment, not a wrong field or a wrong operand.
// w8-l adds the discriminating observation the earlier note lacked: the SAME two
// expressions are emitted at four other sites in this function and we match all of
// them.  `mTime - smLastDumpTime` at line 99 ([72]-[74]) matches; the identical
// expression at line 89 ([33]/[34]) does not.  `mTimeEnd - mTime` at lines 110
// ([122]-[124]), 134 ([216]-[218]) and 170 ([351]-[353]) all match; the identical
// expression at line 103 ([90]-[92]) does not.  At both failing sites the target
// issues the `mTime` load (0x0(r29), r29 = this+0x40) FIRST and we issue it second,
// with byte-identical instructions on either side of the pair.  Since the winning
// and losing sites hold the same source expression, the order is not selected by
// operand order in the source, and no source spelling of those two expressions is
// available that changes only one of the five sites.
// w17-d (99.995): `-smLastDumpTime + mTime` / `-mTime + mTimeEnd` at the two
// failing sites are byte-inert (MSVC canonicalises the subtraction).
```
