# Lane notes: `src/system/ui/UILabel.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawShowing@UILabel@@UAAXXZ`

`UILabel::DrawShowing` -- moved verbatim from `src/system/ui/UILabel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 609-616):

```text
                // w14-f: 96.03 -> 100.  The image binds the element address
                // before Style() (`add r27, r28, r11`, callee-saved) and reads
                // mColorOverride AFTER the mFontColorOverride store, with a SIGNED
                // null test (`lwz r11, 0xc(r27)` / `cmpwi cr6, r11, 0x0`).  A
                // ternary reading the ObjPtr twice (CSE'd to one load) is what
                // keeps the element pointer live across the call; an `if (!c)`
                // after a single read held _M_start instead (w8-era note: 96.9
                // canonical but 29 rows).
```
