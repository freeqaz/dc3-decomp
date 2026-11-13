# Lane notes: `src/system/hamobj/HamNavProvider.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetLabel@HamNavProvider@@QAAXHHVSymbol@@@Z`

`HamNavProvider::SetLabel` -- moved verbatim from `src/system/hamobj/HamNavProvider.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 241-246):

```text
        // w21-r (97.80 -> 100, 91/91 equal): the Size() test reads the member
        // directly and the Clone goes through a provider pointer declared
        // INSIDE the if -- that local is the image's `stw r11, 0x50(r31)`
        // (its home store sits at the Clone site, after the re-read of
        // Data()).  Earlier refutations (w11-a/w19-b) all declared the local
        // before the test or used it at the Size() site.
```
