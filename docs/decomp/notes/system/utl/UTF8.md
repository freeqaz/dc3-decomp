# Lane notes: `src/system/utl/UTF8.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?WStrniCmp@@YAHPBG0H@Z`

`WStrniCmp` -- moved verbatim from `src/system/utl/UTF8.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 52-62):

```text
        // w8-g: CLOSED (89.66 -> 100.0).  The image loads *p2 into a scratch
        // (`lhzx r11, r8, r9`), saves char1 out of r3, and only then does
        // `mr r3, r11` -- 0x827E1564-0x827E156C -- where a plain `WToLower(*p2)`
        // loads straight into r3 and skips both moves.  w7-az was right that an
        // interposed `unsigned short raw2 = *p2;` is byte-inert (MSVC folds the
        // temp, re-measured here), but the POST-INCREMENT is not: writing the
        // advance into the argument makes MSVC evaluate the argument expression
        // before it has homed the previous call's result, which is exactly the
        // scratch-then-move sequence.  p1's `++` must stay a separate statement:
        // moving both inline switches the loop to two lhzu induction pointers and
        // loses the image's `subf r8, r3, r4` / `lhzx` delta addressing.
```
