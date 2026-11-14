# Lane notes: `src/system/dsp/FlangerEffect.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Process@FlangerEffect@@QAAXPAMHH@Z`

`FlangerEffect::Process` -- moved verbatim from `src/system/dsp/FlangerEffect.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 146-150):

```text
    // w15-b (90.78 -> 91.7): a plain for loop, not the hand-rotated
    // `if (numSamples > 0) do {...} while` -- same trip count.  Left: the
    // f21/f22 naming of the two step quotients and the scheduling of the two
    // `% 9600` read indices (idx 137-183); swapping the step statements is
    // worse (84.6 instr-level).
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.
