# Lane notes: `src/system/ui/PanelDir.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?PanelNav@PanelDir@@AAA_NW4JoypadAction@@W4JoypadButton@@VSymbol@@@Z`

`PanelDir::PanelNav` -- moved verbatim from `src/system/ui/PanelDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 603-614):

```text
// w15-i2 (96.739, 4 rows): the image homes mFocusComponent to frame slot
// 0x50 (`stw r4, 0x50(r31)`, never read) before entering the loop, and
// shares the `li r3,0` epilogue with the loop exit.  REFUTED: RB3's
// `while (comp = ComponentNav(...))` spelling with the same statics (75.33).
// w21-p: 96.74 -> 100 (92 of 92 rows equal).  Two levers: (1) null-test the
// MEMBER and only then copy it into comp -- that is the image's dead
// `stw r4, 0x50(r31)`, MSVC homing the raw member receiver after its null
// check; a `comp = mFocusComponent; if (!comp)` local never gets it.  (2) A
// structured `if (mFocusComponent) { ... break; ... } return false;` instead
// of two `goto fail`s puts the single epilogue right after the `li r3, 0`
// exit, with the success path branching back to it, as in the image.  Same
// behaviour as the goto form: every non-success exit returns false.
```
