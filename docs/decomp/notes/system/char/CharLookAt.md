# Lane notes: `src/system/char/CharLookAt.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/char/CharLookAt.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

File-level (was lines 24-54; nearest following definition: `CharLookAt::CharLookAt`):

```text
// w21-an (?Multiply@@YAXABVMatrix3@Hmx@@0AAV12@@Z, emitted in this unit,
// body in math/Mtx.h -- not edited, another lane owned src/system/math/):
// stays 81.05. Standalone cl.exe probe of lever (n): all 18 trees of each
// arm written with explicit parens in the image's grouping (the fast and
// aliasing arms group differently, so the original was a flat sum MSVC
// re-sorted). The pair grouping then matches, but MSVC canonicalises the
// operand order inside each pair, so it still picks a different product for
// the fmuls (row 0 y: image fmuls a.x.z*b.z.y, probe a.x.y*b.y.y) and
// schedules differently; no better than the flat body. Not shipped.
// w24-c2: re-read at 81.05 (fuzzy 74.88; 128 diff_arg, 15 insert / 14
// delete, all FPR scheduling + the f24 vs f25 save count). The vy/vz shared
// stack slot is NOT a source-scope tell: our build shares a slot too (vx/vy
// at -0x60), so slot coloring follows the schedule. Every b element is
// loaded before the first out store in the image's alias arm, so all three
// Multiply calls precede the copies (an early `out.y = vy` would force a b.y
// reload, since b == out there). rb3-xenon mtx.cpp, rb3 Rot.cpp/Mtx.h and
// og-dc3 checked: no other shape. Floor stands; nothing built.
// w26-l (2026-11-02): re-triaged, no behaviour difference found. Every one of
// the 164 non-equal rows is a register permutation or a schedule slot: the
// two arms compute the same 18 products from the same 9 `a` and 9 `b`
// elements and store the same 9 outputs to the same offsets (the buffered
// arm's extra word per row is the Vector3 pad, identical on both sides), the
// only alias-sensitive store order difference is inside an arm the compiler
// chose, and the one difference that is neither a register nor a slot is the
// image's `lfs f7, 0x14(r4)` hoist above `cmplw cr6, r4, r5` (we hoist
// nothing), which is the b.y.y copy-order lever w13-o already swept in all
// six orders. NOT EDITED HERE: the
// body lives in math/Mtx.h, which is PCH-reached (confirmed with
// `ninja -t deps build/373307D9/pch/decomp_pch.obj`), so a spellings round
// would have whole-binary blast radius for a row already re-read by w13-o,
// w21-an, w21-q, w24-c2. Recorded, not attempted.
```


## `?Poll@CharLookAt@@UAAXXZ`

`CharLookAt::Poll` -- moved verbatim from `src/system/char/CharLookAt.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 281-313):

```text
                // Multiply(pivotXfm.m.y, rotMat, lookDir), spelled as
                // per-component accumulators.  The header's single
                // expression makes MSVC seed all three rows from the
                // z-term; the image seeds X from the y-term (it defers the
                // m.z.x load to last) and only Y and Z from the z-term.
                // A `+=` is a reassociation barrier, and MSVC emits the
                // first two of whatever order is written SWAPPED -- both
                // for the terms inside a row and for the rows themselves --
                // so the orders below are the image's orders pre-swapped.
                // See the note above Multiply(const Vector3 &, const
                // Hmx::Matrix3 &, Vector3 &) in math/Mtx.h.
                // w20-g: normalized 100.0, fuzzy 99.86. The 10 charged FP rows
                // (0x822f4..0x82338, 0x823ec/0x823f4, 0x826b8) are pure
                // multiplicand swaps of the same products (value-identical;
                // every operand mapped to its frame slot and checked). Writing
                // the seven mismatched terms below as pivotFwd.? * rotMat.?.?
                // was byte-for-byte INERT (measured): MSVC canonicalises the
                // order. (The 2 `_dw` static-name rows are fixed; see the
                // note at the second MILO_NOTIFY_ONCE below.)
                // Open (c2 §8.7 sort key, w25-hb): here the products are
                // var-vs-var. The pivotFwd components (sids ~1295..1298) have
                // keys above every rotMat component (sids 357..373), so pivot
                // sorts first in all 9. The image wants pivot first only for
                // rot.y.z and rot.z.z, which needs the two key ranges to
                // interleave (sid<<5 mod 0x10000).
                // The 3 rows in the TU-local Multiply (both call sites) are
                // var-vs-memory: the row base var's sid mod 8 sets the load key.
                //   line-96 site, bases x/y/z: 978/1088/991 (mod 8 = 2/0/7);
                //     needs {0,1} / >=2 / >=2.
                //   sourceFilter site: 1154/1158/1157 (2/6/5); needs !=0 /
                //     !=0 / ==0.
                // Adding one named reference renumbers these non-uniformly and
                // changes code.
```


## `?Multiply@@YAXABVMatrix3@Hmx@@0AAV12@@Z`

lane C / w-low-gesture-char (2026-11-13):

```text
// Behaviour audit (84.424 canonical / 77.865 fuzzy; body in PCH-reached
// math/Mtx.h, not edited): NOT A BUG.  Image branches on `&b == &out` only
// (cmplw r4, r5).  Non-alias arm: rows x, y, z in order, each out.r.c =
// a.r.x*b.x.c + a.r.y*b.y.c + a.r.z*b.z.c (checked term by term for row x).
// Alias arm: every a/b load precedes the first out store, vx -> -0x80,
// vy -> -0x70, vz -> -0x70 after vy is copied, then copied to out.x/y/z.
// So a == out and a == b == out are both safe on both sides.  Residual is FP
// scheduling/colouring, as w21-an / w24-c2 say.
```
