# Lane notes: `src/system/rndobj/Morph.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?InterpWeight@RndMorph@@QAAMABV?$Keys@MM@@M@Z`

`RndMorph::InterpWeight` -- moved verbatim from `src/system/rndobj/Morph.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 111-154):

```text
    // RESIDUAL (w7-az, 94.41, 17 rows).  The image reads prev->value ONCE,
    // ahead of the mSpline test, and lets the one register feed both arms:
    // `lfs f0, 0x0(r11)` at 0x82716DF8 sits ABOVE `cmplwi r10, 0x0` /
    // `beq .L_82716E64` at 0x82716DFC-0x82716E00, and that same f0 is what the
    // spline arm's closing `fmadds f1, f13, f12, f0` (0x82716E5C) and the Interp
    // arm's `fsubs f12, f12, f0` (0x82716E70) both consume.  We load it
    // separately in each arm, which also puts prevVal in f12 and next->value in
    // f0 where the image has them the other way round -- that is the whole
    // f0<->f13 cascade.
    // NEGATIVE (a 3.7pp LOSS, 94.41 -> 90.74): hoisting `float prevVal =
    // prev->value;` above `if (mSpline)` does move that load to the right slot,
    // but it frees `prev`'s register early and MSVC then hoists the `next`
    // pointer load with it -- `lwz r11, 0x50(r1)` jumps above the branch and the
    // Interp arm stops reloading it, where the image loads that slot twice,
    // 0x82716E08 in the spline arm and 0x82716E64 in the Interp arm.  Net 22
    // rows instead of 17.  Writing the else arm as
    // `Interp(prev->value, ...)` rather than `Interp(prevVal, ...)` makes no
    // difference: 90.74 either way.
    // w21-x (94.41, same 17 rows): behaviour re-checked against the image --
    // null prev -> 0; spline arm returns 0 when both values compare equal to
    // 0 (NaN takes the body on both sides) and otherwise
    // ((f2*ref + (f2*-3)*0.5)*ref)*ref + prev; Interp arm (next-prev)*ref+prev.
    // Mechanism, from a standalone cl.exe probe that reproduces our listing
    // exactly: the Interp arm's register choice is already the image's
    // (prev in f0, ref f13, next f12); what differs is the SPLINE arm, where
    // MSVC gives prevVal f12 / ref f13 / the f2 chain f0 while the image has
    // prevVal f0 / ref f12 / f2 chain f13.  Once both arms load prev into f0
    // the image's post-allocation hoist of that one `lfs f0` falls out.
    // Probe-measured inert (spline-arm allocation never moves): RB3's shared
    // function-scope `prevVal` assigned in each arm (also 88.9 in-tree: its
    // `f2 * -3.0f * 0.5f` folds to -1.5); `ret` declared first; prev->value
    // read directly; an early `return 0`; a nextVal local; the result written
    // `(X) * ref * ref + prevVal` / `prevVal + ...` / `ref * (ref * X)`;
    // `2.0f * prevVal`; `!= 0` tests; a __forceinline spline helper taking
    // (Key*, Key*, float), (const float &, const float &, float),
    // (float, const float &, float) or (ref, prev, next).  Worse: hoisting
    // prevVal above `if (mSpline)` (prev lands in f12 and the next-pointer
    // load hoists with it, whatever the else arm reads), `if (!mSpline)`
    // first, or a shared `ret` with one exit.
    // Native note: the image multiplies (X*ref)*ref; `ref * ref * (X)` is the
    // same instruction stream here but clang evaluates (ref*ref)*X, so the
    // native result can differ in the last ulp (it already does through the
    // fused fmadds).  Left as is: no PPC gain, and the rule is no native
    // behaviour change without one.
```

Added by lane w-low-rndobj, 2026-11-13:

```text
BEHAVIOUR VERDICT: AGREES (94.41 / 92.09, unchanged).  AtFrame(frame, prev,
next, ref) slot mapping 0x58/0x50/0x54; null prev -> 0; mSpline 0x35; spline
arm returns 0 when both values are 0, else ((f2*ref + (f2*-3)*0.5)*ref)*ref +
prev with f2 = prev*2 - next*2; linear arm (next-prev)*ref + prev.  Re-measured
the hoisted prevVal (90.74, as w-notes above say) and reverted.
```


## `?SetFrame@RndMorph@@UAAXMM@Z`

`RndMorph::SetFrame` -- moved verbatim from `src/system/rndobj/Morph.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 207-213):

```text
                // w21-y: targetIt declared FIRST (99.06 -> 100 normalized).  With
                // the pose pair first, MSVC strength-reduced the Scale loop with
                // the TARGET pointer as the primary induction (pose.x via a
                // derived index) and swapped the vertEnd/itVertEnd callee-saved
                // registers; the image indexes target.x off the pose pointer
                // (`stfsx f11, r9, r11`).  Two fmuls/fmadds commutative operand
                // swaps remain (fuzzy 99.90); same products bit-for-bit.
```

Inside the body (was lines 221-226):

```text
                    // w25-gn: the named poseWeight adds one IL temp ahead of the
                    // Scale loop's strength-reduced cursor, moving that cursor's
                    // temp sid by one.  norm.y in the Scale loop is a memory leaf
                    // on that cursor (key 0x10008 + (sid & 3) << 14), and needs
                    // to outrank intenseInterp (V16, 0x10200) to give the image's
                    // `fmuls f12, f12, f0`; it now keys 0x14008.
```

Inside the body (was lines 249-265):

```text
                        // w25-gn, open: the norm.z product below emits `fmadds f13,
                        // f13, f0, f12` where the image has `f0, f13` (same value).
                        // c2's last commutative sort (cqlo3) still sees norm.z's
                        // source address as cursor + index (V1720 + V1358), a
                        // subexpression that outranks intenseInterp; the image
                        // needs a leaf there keyed below 0x10200, i.e. a different
                        // strength-reduction rebuild of this loop's cursors.
                        // 23 spellings tried (loop/branch order, named locals).
                        // w26-f14 (c2rs tap): the multiply use lists carry the
                        // source leaf at M6{0x1c008} / M6{0x14008} -- memory
                        // operands on a temp base whose sid%4 is 3 and 1 -- against
                        // intenseInterp V16{0x10200}, so they sort first; the image
                        // needs this one leaf on a base with sid%4 == 0 (key
                        // 0x10008).  Bindings measured here: `const Vector3 &nv =
                        // itVert->norm` 99.899, `Vector3 &dv = targetIt->norm`
                        // inert, hand-expanding the three components (with a
                        // `const float &srcZ`) 99.798 and canonical 99.899.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.949, 1 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.
