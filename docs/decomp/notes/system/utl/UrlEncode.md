# Lane notes: `src/system/utl/UrlEncode.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?URLEncode@@YAXPBDAAVString@@_N@Z`

`URLEncode` -- moved verbatim from `src/system/utl/UrlEncode.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 38-44):

```text
            // Nothing is assigned back to `c` here -- each arm APPENDS its
            // second character, and all three paths cross-jump into the one
            // `mr r3, r29 / bl String::operator+=(char)` tail at 0x827EF59C.
            // NEGATIVE RESULT (w7-al, 2026-09-14): collapsing the two arms into
            // `if (escapeUnsafe && ...) c = ' ';` plus one unconditional pair of
            // appends scores 93.4 -- MSVC then materialises `li r31, 0x20` where
            // the image has `lbz r4, 0x2(r30)`.
```
