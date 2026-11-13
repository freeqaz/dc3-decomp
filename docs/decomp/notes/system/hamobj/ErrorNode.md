# Lane notes: `src/system/hamobj/ErrorNode.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Displacements@BaseDisplacementNode@@IBA_NABUErrorFrameInput@@AAUDisplacementData@1@AAUHam1DisplacementData@1@@Z`

`BaseDisplacementNode::Displacements` -- moved verbatim from `src/system/hamobj/ErrorNode.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 244-279):

```text
// The sums below are written as accumulator statements on purpose: /fp:fast
// reassociates a one-expression sum per call site, and the statement form pins
// the association the target uses (docs/decomp/patterns/fixable-fsel-fma.md).
// RESIDUAL (w12-d, canonical 100 modulo register permutation): 7 fmuls rows
// carry swapped commutative operands (e.g. target `fmuls f9, f0, f12` for
// inv*y). Flipping the source operand order of every one of them was measured
// INERT -- MSVC canonicalises the operand order itself.
// w25-fc stop note (~440 variants through the c2rs tap): all 8 rows are fmuls/
// fmadds whose u{} operand order is fixed in the IL before both allocators
// (C2RS-BRIDGE 8.3); registers and candidate keys are already the image's.
// Naming the three jointDisp loads (jx/jy/jz below) flips the dot rows and
// the bn rows to the image (98 -> 94 of 98 equal; kept), but n.y and
// n.z then always share one orientation (the image has them opposite), and
// the three proj = n * dot rows keep `dot` (a regasg temp) first in every
// spelling tried: Scale/Set/per-component, plain-float n, named copies of
// dot, proj ref vs member, statement order. Open question: the commutative
// operand key for a colour candidate (n.x) against a non-candidate temp.
//
// w26-f3 (2026-11-02): 4 rows left (fuzzy 99.59184, canonical 100).  The c2rs
// regasg dump prints the emitted order directly as the RT u{} list, and every
// operand here is a regasg result, so this is a key ordering and not a source
// ordering.  Measured this lane (sid in parentheses):
//   n.x  RT40 u{ jx(480) inv(424) }    image jx first    equal
//   n.y  RT41 u{ inv(424) jy(479) }    image inv first   equal
//   n.z  RT42 u{ inv(424) jz(478) }    image jz first    ROW
//   proj RT54/56/58 u{ dot(423) n.* }  image n.* first   3 ROWS
// n.x and n.y keep the image's order while n.z does not, so the inv temp key
// sits between the jx and jz single-use loads; the proj rows are the same
// shape with dot (an fmadds result) above all three n results.
// Measured INERT this lane: source-order flip of n.z; `const float &`
// bindings of inv, jz, dot and each n component; dropping the jointDisp,
// baseDisp and proj aliases; per-component `proj.x = n.x * dot`;
// `proj.Set(...)`; reordering, grouping and reversing the jx/jy/jz
// declarations; and every pair of the above.  The open quantity is how many
// sids separate inv and dot from the loads here (C2RS-BRIDGE 8.9 shape); it
// is not measured.
```


## `?CalcError@PositionNode@@UBAXABUErrorFrameInput@@ABUErrorNodeInput@@AAVVector3@@@Z`

`PositionNode::CalcError` -- moved verbatim from `src/system/hamobj/ErrorNode.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 452-468):

```text
    // FIXED (w12-d): the w7-ak residual below was the r10/r11 assignment of
    // the two joint indices; binding the mJointPositions refs before the
    // Subtract statements puts mJoint in r11 as the image has it. What is left
    // (canonical 100 modulo register permutation) is two commutative operand
    // orders in the inlined Scale (`fmuls f13, f30, f0` vs ours `f0, f30`).
    // NEGATIVE RESULT: `scaledBaseDiff = baseJointDiff; scaledBaseDiff *= s;`
    // drops it to 75.4 (spills baseJointDiff instead of holding it in f28-f30).
    // Historical w7-ak note: all 14 rows came from ONE thing --
    // which of the two joint indices lands in r11 and which in r10.  The image
    // loads mJoint (0xc) into r11 and mBaseJoint (0x1c) into r10; we load them
    // the other way round, and every other row follows: the six `lfs` register
    // swaps and the `fsubs`/`fmuls` operand orders are the SAME ARITHMETIC on
    // both sides (target `f0 - f13` with f0=joint, ours `f13 - f0` with
    // f13=joint), verified component by component against the target listing.
    // This is NOT the "wrong field" the resolved-offset block reports.
    // NEGATIVE RESULT: reordering these two Subtract statements to match the
    // emission order (jointDiff first) makes it WORSE, 14 rows -> 15.
```
