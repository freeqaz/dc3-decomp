# Lane notes: `src/system/rndobj/PostProc.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetEmulateFPS@ProcCounter@@AAAIH@Z`

`ProcCounter::SetEmulateFPS` -- moved verbatim from `src/system/rndobj/PostProc.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 46-80):

```text
// Configure frame rate emulation by calculating frame skip pattern
// For 24fps on 60Hz: 120/24=5 -> switch every 2-3 frames (alternating via mOdd)
// For 30fps on 60Hz: 120/30=4 -> switch every 2 frames (mOdd=0)
// RESIDUAL (96.55173 canonical = fuzzy = raw; 59 instr, equal 57, insert 1,
// delete 1): the whole residual is ONE instruction, `mr r3, r4`, the int->unsigned
// return conversion of `fps`.  The image issues it at index 34 -- in the block that
// computes 120.0f/fps, between the `lis @3f000000` constant setup and `fdivs` --
// and we issue it at index 54, in the tail block after the `cmpw` against mSwitch.
// Everything before and between (0-33 and 35-53) is instruction-for-instruction
// equal, so the copy's PLACEMENT is the only divergence.
//
// CLOSED 2026-11-10 as a c2 copy-placement tie, not a source gap.  The copy is
// mandatory (r3 holds mFPS for the `fps == mFPS` guard at idx 20-21, so fps must
// stay in r4 and cannot coalesce into the return register), so c2 must materialise
// it somewhere; it hoists it into the predecessor block in the image and sinks it
// into the block containing both tail returns here.  Both placements are legal and
// behaviourally identical.  Six spellings that reach the copy all compiled
// BYTE-IDENTICAL to the baseline (equal 57 / ins 1 / del 1 / 96.55173), i.e. the
// front end coalesces the variation away before c2 ever places the copy:
//   (1) `unsigned int ret = fps;` after the guard, `return ret;` at both tails
//   (2) `unsigned int ret = fps;` before the Clamp, `ret = fps;` after it
//   (3) drop the redundant `auto _tmp0` (Clamp is `inline T Clamp(T,T,T)`, T=int)
//   (4) `float ratio = 120.0f / (f32)fps; int round = Round(ratio);`
//   (5) `return (unsigned int)fps;` at the first tail only (two distinct conversion
//       pseudos -- still merged into one copy, still in the tail block)
//   (6) `mFPS = (unsigned int)fps;` (puts a conversion pseudo in the target's block)
// `decomp-synth` beam search over the 17 patterns its scan selected for this
// function (signed_unsigned, temp_elimination, declaration_movement,
// statement_reorder, variable_inline, slot_pad, ...) ran 5 rounds / ~52 variants a
// depth for 613 s: 0 improved, source left untouched.
// Earlier lanes, same shape: w13-a's `return mFPS` at both tails and
// Round(120.0f/mFPS) were inert, and a single tail `if (mCount >= mSwitch)
// mCount = 0; return fps;` drops to 91.0 by losing the conditional `bltlr`.
// Not worth re-deriving: perturbing anything that DOES reach the schedule breaks
// the 57 equal rows while the copy stays at 54.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: one `mr
r3,r4` placed at a different spot.


## `?Load@RndPostProc@@UAAXAAVBinStream@@@Z`

`RndPostProc::Load` -- moved verbatim from `src/system/rndobj/PostProc.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Inside the body (was lines 403-407):

```text
        // w16-a: the rev-0x10 block's Vector3 and float are ONE Sphere (center
        // at 0x80, radius at +0x10 = 0x90 in the image's 16-aligned region), and
        // the dead zero-store at 0x90 before the reads is Sphere()'s
        // `radius(0)`.  Closes the 3-row slot swap earlier lanes recorded here
        // (99.97 -> 100); the value is read and discarded either way.
```


## `?UpdateColorModulation@RndPostProc@@IAAXXZ`

`RndPostProc::UpdateColorModulation` -- moved verbatim from `src/system/rndobj/PostProc.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 702-711):

```text
        // SOLVED (w26-v, 97.94 -> 100.0).  The image re-loads mFlickerSeconds.x
        // at the merge point (`lfs f0, 0x11c(r31)` at 0x90, the `blt` target
        // from the inner test) and lets the inner `fsel` write f0
        // (`fsel f0, f13, f0, f1` at 0x88), which destroys the value.  Binding
        // the operand as a REFERENCE keeps the load a distinct value, so it is
        // no longer CSE'd with the `Max(mFlickerSeconds.x, ...)` load above:
        // the merge-point reload appears, f0 then holds x only to the fsel
        // (its last use frees it before the def), and the fsel takes f0.
        // This is c2rs-bridge 8.8 applied to a scalar operand: bind, do not
        // copy (a plain `float fv = ...` copy is folded away; measured).
```
