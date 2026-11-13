# Lane notes: `src/system/gesture/FreestyleMotionFilter.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?UpdateFilters@FreestyleMotionFilter@@QAAXABUSkeletonUpdateData@@@Z`

`FreestyleMotionFilter::UpdateFilters` -- moved verbatim from `src/system/gesture/FreestyleMotionFilter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 56-69):

```text
                    // STILL OPEN (w26-f8, fuzzy 99.78494, canonical 100.0): the
                    // two residual rows are the ADD TREE, not the registers.
                    // Image: f0 = f13 + f0 then + f12, i.e. (y2 + x2) + z2.
                    // Ours: f13 = f13 + f12 then + f0, i.e. (y2 + z2) + x2.
                    // Source-ORDER spelling is inert (x2+y2+z2, y2+x2+z2,
                    // z2+y2+x2, a named `total`, a named first sum -- all five
                    // exactly 99.78494), so c2 picks the pairing, not the
                    // source.  What DOES move it is explicit parentheses, which
                    // change c1's folding before contraction: `(x2 + y2) + z2`
                    // and `(y2 + x2) + z2` reach 99.89247 (one row left), while
                    // `(x2 + z2) + y2` -- the tree the source already parses to
                    // -- collapses to 96.18279.  Not landed: 99.89247 still
                    // emits a different association from the image's, so it
                    // changes float rounding without reaching the image's.
```
