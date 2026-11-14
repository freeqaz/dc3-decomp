# Lane notes: `src/system/char/CharIKFingers.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?CalculateHandDest@CharIKFingers@@IAAXHH@Z`

`CharIKFingers::CalculateHandDest` -- moved verbatim from `src/system/char/CharIKFingers.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 242-272):

```text
            // RESIDUAL (w7-ao, 93.53 canonical): the largest cluster is inside
            // this inlined Multiply(Vector3, Matrix3, Vector3), not in this
            // function. The image accumulates the three rows in z, x, y order
            // (0x823827A4: `fmuls` off m.z, then `fmadds` off m.x, then off
            // m.y) and reuses ONE register for the two zero components of
            // `sideOffsetBase`; we emit x, y, z. That term order lives in
            // math/Mtx.h, a PCH-reached header, so it is out of this lane's
            // scope -- and `Multiply` there is on the wave DO-NOT-WORK list.
            // NEGATIVE RESULT (w7-ba, 2026-09-14): the cluster is not "x, y, z
            // order" -- it is FACTORING.  Ours emits `(m.y + m.z) * zero +
            // m.x * x` (one fadds per component, then fmadds), because y and z
            // are the same opaque literal value; the image keeps three
            // separate products (z*0 first at 823827A4, +x*x, +y*0 last).
            // Neither `Zero(); x = ...; x *= -1` nor hoisting the scalar
            // (`float off; if (!right) off *= -1; Vector3(off, 0, 0)`) moved a
            // row (93.53 both).  The lever is not the assignment order here.
            // NEGATIVE RESULT (w7-ao, 2026-09-14): two reorderings of the
            // Scale/Add pair in the loop below were measured against the
            // hypothesis that the fmuls/fadds cluster at idx 48-53 was ours:
            // hoisting the Scale above the Add is inert on the cluster and adds
            // 3 commutative rows; folding the target position into sideScaled
            // costs 93.53 -> 92.04. The cluster is the header's.
            // w21-i (93.53 -> 100 canonical): the cluster above WAS the
            // factoring, and it is source-addressable at the call site.  With
            // the Mtx.h Multiply inlined, MSVC sees y and z of sideOffsetBase
            // are the same zero and folds `m.y.c*0 + m.z.c*0` into one
            // `(m.y.c + m.z.c) * 0`.  The image (0x823827A4..) computes
            // m.z.c*0 first, fmadds m.x.c*off onto it, and adds m.y.c*0 last:
            // `m.y*y + (m.x*x + m.z*z)`.  Spelled that way here (same values,
            // the image's association), the two zero products stay separate.
            // Writing it as a flat `z + x + y` sum was inert (93.53).
```

Inside the body (was lines 290-298):

```text
                    // w21-i: avgPos first -- the image's x add is `fadds f11, f27, f11`
                    // (avg.x + pos.x); closes one commutative row.
                    // w25-gn: the side.z/side.y products (`fmuls f10, f30, f12`) need
                    // the scale factor's operand key between side.x (V15) and
                    // side.y/z (V303/V299); a named local (sid ~21) gives that, where
                    // Scale's inlined parameter (V309) outranks all three.  The
                    // avg.x add above then needs `finger`'s sid to stay 0 or 1 mod 8
                    // (memory-leaf key), which dropping the unused Transform copy at
                    // the top of the function restores.
```


## `?CalculateFingerDest@CharIKFingers@@IAAXW4FingerNum@1@@Z`

`CharIKFingers::CalculateFingerDest` -- moved verbatim from `src/system/char/CharIKFingers.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 374-386):

```text
                // The two 16-byte copies out of f1Xfm are DIFFERENT KINDS of copy,
                // and MSVC lowers them differently.  f1z is default-constructed
                // and then ASSIGNED: copy-assignment is emitted as lwz/stw pairs
                // that the scheduler interleaves with the Subtract above
                // (82381D34 `addi r11, r1, 0xa0` sits between the Subtract's
                // loads and its fsubs).  f1x is copy-CONSTRUCTED: that lowers to
                // a block copy that stays below the toTarget stores (82381D7C).
                // Both as copy-ctors (w7-ak) read 90.4 -- the f1z loads were
                // hoisted into the f1x block copy and the eight words needed
                // r28/r30 (`__savegprlr_28` vs the image's `_29`); declaring f1z
                // after the Length() calls did not stop that hoist.  Both as
                // assignments reads 91.0 (the copies interleave with each other
                // and float above the Subtract).  This spelling: 100.0.
```


## `?MeasureLengths@CharIKFingers@@QAAXXZ`

`CharIKFingers::MeasureLengths` -- moved verbatim from `src/system/char/CharIKFingers.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 501-530):

```text
// w14-b (still 98.795): the residual is one target-only `fmr f12, f11` before
// the forearm length plus f10/f11 colouring in the finger loop. Tried: the two
// mInv2ab/mAAPlusBB statements swapped (fuzzy +0.6, canonical unchanged),
// multiply-then-invert, an unrolled two-bone loop (78.0), five association
// orders of the finger-length sum (<= 97.7 fuzzy).
// w21-q: 98.795 -> 100 canonical with the reused `len` in the hand/forearm
// tail (the image's `fmr f12, f11`).  Left: 9 register-only rows -- 8 in the finger
// loop (tip.x / f2.y and tip.z / f2.x land in f10/f11 and f7/f8 swapped) and
// one commutative fmuls operand swap (f0/f11) in the hand/forearm tail.  Probe
// (standalone cl.exe, all 12 orderings/associations of the three-length sum,
// all 6 declaration orders of f2/f3/tip, no FingerDesc ref, a function-scope
// `len` shared with the loop): none closer than the current spelling, which
// already carries the image's association (f3 + tip) + f2.
// w25-ge: the (f3 + tip) partial sum is a named local (og-dc3's spelling).  That
// changes no loop instruction, but it flips the tail's `mInv2ab * len` fmuls to
// the image's (mInv2ab, len) operand list: the commutative operand order is fixed
// in the IL before either allocator runs and depends on function-wide symbol
// ordinals, so a local in the loop reaches it.  The 8 loop rows left are first-fit
// regasg picks: the image's registers imply its sched3 order loads f2.y before
// tip.x and f2.x before tip.z (f2 ahead of tip in the x and z groups), then sched0
// puts tip first as ours does.  Open question: which IL shape gives that sched3
// order (the same sched3 -> sched0 question as DxCam::ProjectZ).  Inert (c2rs tap,
// regasg dump): all 6 declaration orders of named f2/f3/tip lengths, the sum
// spelled tip + f3 + f2, a named f2 length only, dropping the totalLen ref.
// w26-f re-measured: still fuzzy 99.157 (canonical 100.0), the same 8 rows.
// Also inert with nsearch: `f3, f2, tip` and `f2, f3, tip` declaration orders,
// and naming `&LocalXfm()` for the two distal bones.  Measured WORSE: swapping
// the distal sum's operands (fuzzy 98.795) and folding `distalLen` away
// (98.675).  Declaring tip first drops canonical to 99.976 -- a real
// regression, reverted rather than kept.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.157, 8 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.
