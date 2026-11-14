# Lane notes: `src/system/hamobj/HamRegulate.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Regulate@HamRegulate@@IAAXAAVVector3@@AAM@Z`

`HamRegulate::Regulate` -- moved verbatim from `src/system/hamobj/HamRegulate.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 113-119):

```text
    // w21-b: CLOSED (94.23 -> 100, 150/150 equal).  The w7-bv residual (the
    // else arm's load/store interleave after Multiply) was the rotDelta
    // statement reading mWaypoint->LocalXfm() twice inline; binding it ONCE to
    // a `const Transform &` (one mWaypoint load, as the image has at
    // 824C5338) gives the image's schedule.  Subtract() below still goes
    // through mWaypoint again, which is the image's reload after the store
    // through rotDelta.
```


## `?Poll@HamRegulate@@UAAXXZ`

`HamRegulate::Poll` -- moved verbatim from `src/system/hamobj/HamRegulate.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 161-229):

```text
    // REFUTED (w7-ag): hoisting moveX/moveY above the DeltaSeconds() call
    // (declaration order moveX, moveY, moveZ, dt) schedules both lfs BEFORE the
    // call instead of after it -- 98.8 -> 97.6.  The two remaining charged rows
    // are our extra `fmr f28, f0` / `fmr f29, f29` pair: the image loads
    // posDelta.x/.y straight into the callee-saved f29/f30
    // (build/373307D9/asm/system/hamobj/HamRegulate.s, `lfs f29, 0x60(r1)` /
    // `lfs f30, 0x64(r1)`) where we land them in scratch and copy.
    // w19-b (98.81, same 2 inserted fmr + the f28/f29/f30 renumbering they
    // cause): the copy is MSVC propagating the loaded temp into the sqrt and
    // the scale products, so temp and moveX interfere.  Inert: the sqrt over
    // posDelta.x/.y instead of moveX/moveY (identical bytes).  Worse: moveZ
    // declared after moveY (97.6, moves the z=0 fmr below the call).
    // w21-o (98.81, unchanged): behaviour re-read against the image (scale,
    // foot-state mask, accum y/z/x sum, EditMode/teleported/absDt gate all
    // match).  Reproduced the two `fmr f28/f29` copies in a standalone cl.exe
    // probe and tried 11 more shapes there -- all keep the load temp + copy:
    // sqrt over posDelta.x/.y, Teleported() test flipped, a Vector3 `move`
    // aggregate (with and without z zeroed first), declarations split from
    // the loads, DeltaSeconds() folded into Max, `scale * moveX` order,
    // scale products re-reading posDelta, moveRot defaulted before the
    // clamp.  The image loads straight into f29/f30; no spelling found.
    // w21-au: CLOSED (98.81 -> 100).  moveX/moveY are NOT copies of
    // posDelta.x/.y made up front: they are assigned in each arm (scaled,
    // unscaled, teleported) and the sqrt reads posDelta directly.  With the
    // up-front copy MSVC propagated the load temp into the sqrt and the scale
    // products, so temp and moveX interfered (the two fmr); assigned per arm,
    // the temp dies at the copy and coalesces into f29/f30.  Same values on
    // every path (x * scale order kept).  100 MODULO REGISTER PERMUTATION:
    // fuzzy 98.04 after the teleported arm's y-before-x order put x/y in the
    // image's f29/f30.  Left: moveZ/moveRot in f27/f28 (image f28/f27) and
    // footState in r11 (image r10), so the xor/and operands read swapped.
    // Inert for those (measured): all 67 orders of the declarations after
    // DeltaSeconds(), the 36 assignment orders of the two unscaled arms,
    // `moveZ * scale`; worse: `float moveRot = rotDelta` + `scale * moveRot`
    // (96.4), moveZ declared after dt (98.8).
    // w25-gk (fuzzy, 121 -> 153 of 168 equal in a c2rs-tap listing compare):
    // the accumulation is Add() into posDelta and the threshold test reads
    // posDelta back (same y, z, x sum order, so native rounding is unchanged).
    // With three named accum floats the image's 16.0f constant lost its f11
    // (the accums were demoted to regasg temps) and footState took r11; with
    // Add() both land as in the image.  Left: moveZ (pri 14) colours after
    // moveRot (pri 21), so they read f27/f28 swapped; the scale products and
    // the final `xfm.v +=` adds read operand-swapped (C2RS-BRIDGE 8.7 key:
    // the image orders the move value first).  Inert or worse (measured): a
    // Vector3 `move` aggregate (153, same rows), `scale * posDelta.x`, and
    // per-component `xfm.v.x = move.x + xfm.v.x` (105).
    // w26-f2 (fuzzy 99.434525, canonical 100.0) -- two residuals, both keyed
    // and both left in place:
    //  (1) the FPR half above is exactly as w25-gk left it: image moveZ=f28 /
    //      moveRot=f27, ours the swap.  moveZ (pri 14) still colours after
    //      moveRot (pri 21).
    //  (2) three commutative sites where the image puts the MEMORY operand
    //      first -- `fmuls f29,f29,f0`, `fadds f13,f13,f30`,
    //      `fadds f12,f12,f28`.  Index 149 (`fadds f0,f0,f29`, the x
    //      component) ALREADY agrees, so the C2RS-BRIDGE 8.7 memory-leaf key
    //      falls between moveX's and moveY/moveZ's -- it is not one uniform
    //      "move value first" rule, which is why writing the adds that way
    //      uniformly does not reproduce the image.  Inert or worse (measured
    //      this pass): dropping the named `dt` (97.14).
    // w33-b (fuzzy 99.434525 -> 99.64286, canonical 100.0, 168 instructions /
    // 672 B unchanged): binding posDelta to a `const Vector3 &pd` BEFORE the
    // sqrt, so the sqrt, both scale products and the else arm read through
    // it, moved the 8.7 commutative key enough that the two `fmuls` scale
    // products (51/52) and the index 151 add now take the image's operand
    // order -- three rows the w26-f2 note had left.  Binding only the scale products,
    // after the sqrt, reaches 99.52381; the source operand order is itself
    // inert (`scale * pd.x` is byte-identical), so the key, not the spelling,
    // decides.  Left: 12 rows, all pure moveZ/moveRot colour (image f28/f27,
    // ours f27/f28) -- the pri-14/pri-21 tie above, untouched by this lever.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.643, 12 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.
