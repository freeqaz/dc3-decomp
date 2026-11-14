# Lane notes: `src/system/char/CharClipSet.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetFrame@CharClipSet@@UAAXMM@Z`

`CharClipSet::SetFrame` -- moved verbatim from `src/system/char/CharClipSet.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 249-261):

```text
                // Measured (w26-f15): the ONE row objdiff charges in this
                // function is here, not in the loop copy below -- index 243,
                // image `fadds f0,f13,f0` (pelvisXfm.v.x first) vs ours
                // `fadds f0,f0,f13`. The y and z adds of the same expansion
                // already match with posPtr first, so the two operand keys
                // differ by little and the +4/+8 component displacements
                // straddle them. 13 more spellings inert or worse this round:
                // `Vector3 &`/`const Vector3 &` bindings for the whole vector
                // cost 2 instructions (327 insns, 98.785, and 3 instructions
                // for the pointer-plus-reference form), explicit per-component
                // adds are byte-identical to `operator+=`, and binding the x
                // components by reference (`float &vx`, `const float &px`)
                // costs 3 instructions (328 insns, 97.929).
```

Inside the body (was lines 281-287):

```text
                        // w25-gv (c2 §8.7) predicted the loop copy was the open
                        // one and the pelvis copy matched.  w26-f15 measured the
                        // opposite with objdiff's own instruction table: the
                        // only charged row is the pelvis copy (index 243), and
                        // this loop copy scores equal.  The sid/base analysis
                        // above is therefore applied to the wrong site; keep it
                        // as the mechanism, not as the attribution.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.969, 1 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.
