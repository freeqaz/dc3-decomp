# Lane notes: `src/system/hamobj/HamCamShot.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?UpdateTargetsFlipped@HamCamShot@@IAAXXZ`

`HamCamShot::UpdateTargetsFlipped` -- moved verbatim from `src/system/hamobj/HamCamShot.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 181-200):

```text
                    // CLOSED (w7-bp, 98.25641 -> 98.62937 canonical): this order is
                    // load-bearing.  Binding `target` BEFORE the call and passing
                    // `target.mTarget` reproduces the image exactly --
                    //     0x4ff0 lwz  r3, 0x8(r29)      (the Symbol, off the NODE)
                    //     0x4ff4 addi r28, r29, 0x8     (&target, kept in r28 only)
                    // Note MSVC still spells the Symbol load off the node pointer,
                    // not off r28, so `target.mTarget` costs nothing.
                    // With the call FIRST (`CharacterNameToCharacter(it->mTarget)`)
                    // and `Target &target = *it;` after it, MSVC emitted the `addi`
                    // ahead of the load AND homed it -- an extra dead
                    // `stw r28, 0x58(r31)` into what later becomes the `charName`
                    // MakeString slot (overwritten at 0x5120 on every path, so it
                    // really is dead).  That was 1 insert + 1 replace, 98.25641, and
                    // it made the loop 431 instructions against the image's 430.
                    // Second measured negative: deleting the reference entirely and
                    // spelling all four uses `it->mAnimGroup` does NOT remove the
                    // dead store -- MSVC forms and homes &*it either way -- and it
                    // costs a register cascade on top: 98.25408 canonical, 69
                    // diff_arg rows against 33.  The reference is not the problem;
                    // its position relative to the call is.
```

Inside the body (was lines 267-275):

```text
                // w16-b: 98.63 -> 100 canonical (modulo r9/r10 permutation and
                // the lbl_ name of two function-local statics). The image's
                // second induction variable (`addi r25, r22, 0x88`, bumped by 0x118
                // with the frame pointer, `subi r26, r25, 0x4` only AFTER the read
                // loop) is what MSVC makes when the read loop spells
                // `frame.mTargets` directly and the list reference is bound only
                // for the pop/push tail. Binding it at the top (w7-at/w7-bp) kept
                // r26 live across the read loop instead. The name copy is the
                // /Oi strcpy intrinsic (fixes the stbx operand order).
```


## `?FlipTargetAnimGroups@HamCamShot@@IAAXXZ`

`HamCamShot::FlipTargetAnimGroups` -- moved verbatim from `src/system/hamobj/HamCamShot.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 935-959):

```text
// RESIDUAL (w7-az, 93.46, 19 rows).  Everything through the two static-Symbol
// guards matches; the whole residual is that MSVC ROTATES our first search loop
// and not the image's.  The image enters loop 1 with `b .L_824A3D40` at
// 0x824A3D2C -- test at the top, one induction register (r10) -- and enters
// loop 2 the same way at 0x824A3D50.  We emit an extra `mr r9, r11` plus a
// peeled copy of the head test before the body and put the surviving test at
// the bottom, so loop 1 costs two extra instructions and permutes r9/r10/r11
// through the rest of the function.  Loop 2 already matches shape.
// NEGATIVES, both byte-inert or worse: rewriting both loops as
// `iterator p = mTargets.begin(); while (p != mTargets.end()) {...}` is
// byte-identical to this `for` (0 rows moved); hoisting `mTargets.end()` into a
// named `end` iterator used by both loops and both tail tests is WORSE (raw
// 90.7 -> 90.2) -- it pins &mTargets in a callee-saved register and permutes
// r28/r29 back through the Symbol constructors.
// w14-b (still 93.46): the image re-loads begin() for the second loop
// (`lwz r9, 0x0(r11)`) and rotates both loops as jump-to-test; we CSE begin()
// across the loops and guard the first. Inert: `p != end && p->mTarget != s`
// for-conditions, decl-initialised iterators, while loops, a Symbol copy of
// player0; a Target* result for the second search is 83.3.
// w21-g: 100 canonical (99.2 raw = the unnamed target-side static guard
// labels only).  The tell was the image's loop-2 begin: `lwz r9, 0x0(r11)`
// off the &mTargets register, where loop 1 loads `lwz r10, 0x284(r28)` off
// `this` -- two different address expressions, so MSVC does not CSE begin()
// across the loops and needs neither our `mr r9, r11` copy nor the peeled
// guard.  Loop 2 walks the list through a local `ObjList<Target> &`.
```
