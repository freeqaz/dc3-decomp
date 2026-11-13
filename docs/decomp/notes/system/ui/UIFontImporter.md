# Lane notes: `src/system/ui/UIFontImporter.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Load@UIFontImporter@@UAAXAAVBinStream@@@Z`

`UIFontImporter::Load` -- moved verbatim from `src/system/ui/UIFontImporter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Inside the body (was lines 220-226):

```text
    // w20-f BEHAVIOUR FIX: the DC3-only fields are gated on the ALT rev, not
    // rev.  Image: rev lives in r26 (clrlwi r26,r11,16), altRev in r25
    // (srwi r25,r11,16), and these four tests read r25 -- 0x827ACA6C
    // `cmpwi cr6, r25, 0x0`, 0x827ACAD0 `cmpwi cr6, r25, 0x4`, 0x827ACB7C
    // `cmpwi cr6, r25, 0x1`, 0x827ACB94 `cmpwi cr6, r25, 0x2`.  RB3's importer
    // has none of these fields and gates mFontSupersample on rev > 1, which
    // stays on rev here.
```
