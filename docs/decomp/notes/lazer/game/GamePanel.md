# Lane notes: `src/lazer/game/GamePanel.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?UpdateOverlay@LoopVizCallback@@UAAMPAVRndOverlay@@M@Z`

`LoopVizCallback::UpdateOverlay` -- moved verbatim from `src/lazer/game/GamePanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 118-154):

```text
// w7-by (93.475266 -> 99.99647, 2 residual rows): the w7-am "stack allocator
// decision" and w7-z's "extra callee-saved FPR" were both downstream of ONE
// thing -- MSVC's CSE of `loopStartNorm + loopRangeNorm * loopProgress`.
// The image never CSEs that sum: it emits `fmadds f1, f28, f27, f29` at every
// use site (8287BC0C, 8287BCCC, 8287BDB8), computes it TWICE inside one call
// (`fmadds f0` + `fsubs f2, f25, f0` at 8287BC30..8287BC34), and keeps only 12
// callee-saved FPRs (`__savefpr_20`).  With a named `loopRangeNorm` at every
// site the product was a common subexpression, MSVC hoisted product and sum
// into f27/f28 before the branch, contraction to fmadds was impossible, and
// the extra live FPR pushed the two 16-byte Color staging temps onto one
// slot (frame 0x190 vs 0x1a0).
//
// MSVC's CSE is ONE PASS over the tree: `(loopEndNorm - loopStartNorm)` is
// unified into the single `fsubs f28, f25, f29` at 8287BB70, but a product
// whose operand is that re-derived sub is NOT unified with a product of the
// named local.  So the standalone width at the second DrawBar and every
// loop-position sum spell the range as `(loopEndNorm - loopStartNorm)`; the
// three sites that pass the range itself (`fmr f2, f28` at 8287BBAC,
// `fmuls f2, f28, f21` at 8287BC28, `fmuls f2, f0, f28` at 8287BCA0) keep the
// named local.  Measured: swapping only the standalone product's operand
// order is byte-identical (MSVC canonicalises commutative operands before
// CSE), and removing the standalone product entirely (diagnostic) also
// stops the CSE -- the product is the trigger, not the sums.
//
// The loop-end label position is `loopStartNorm + (loopEndNorm -
// loopStartNorm) * 1.0f`, which MSVC folds to `fadds f1, f28, f29` at
// 8287BD70 -- w7-z's observation, reproducible once the FPR cascade is gone.
// The endColor sites read mLoopStartChangeTimer (`lfs f0, 0x54(r30)` at
// 8287BD2C and 8287BFDC), so the Start/Start/Start/End timer reads on the
// four label colours are the image's, not a transcription slip.
//
// The meter-2 DrawLine's white stores alpha (0x9c) before blue (0x98) at
// 8287BF4C/8287BF54.  Color(1,1,1) and Color(1,1,1,1) store r,g,b,a; the
// packed ctor Color(0xffffff) stores alpha first in the IL and matches
// (w25-ma).  The five `MakeString` rows are ICF folds (`MakeString<_D3DFORMAT>`
// / `MakeString<int, SaveLoadManager::State>` are the same code as our
// `MakeString<int>` / `MakeString<int,int>`).
```


## `?UpdateNowBar@GamePanel@@AAAXXZ`

`GamePanel::UpdateNowBar` -- moved verbatim from `src/lazer/game/GamePanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 1048-1052):

```text
        // BUG FIX (w18-e, 98.17 -> 100): with no song duration the image shows
        // 0.0%.  The `ble cr6` on `0 < songDuration` lands on the join, where
        // `stfs f0, 0x64(r1)` stores f0 -- still the 0.0f loaded for the
        // `durVal < 0` test -- into val.  We left val uninitialised on that
        // path and printed stack garbage.
```
