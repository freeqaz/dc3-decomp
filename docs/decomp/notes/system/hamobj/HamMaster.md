# Lane notes: `src/system/hamobj/HamMaster.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/hamobj/HamMaster.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

File-level (was lines 83-87; no definition follows it):

```text
    // w13-b: 0x9c is a constructed 8-byte object, not two floats.  The ctor
    // stores its address to the in-construction EH slot right after the 0x9c
    // and 0xa0 stores (`addi r9, r30, 0x9c` ... `stw r9, 0x50(r31)`), as it does
    // for every other constructed member.  Vector3 is wrong (it moves 0xa8);
    // Vector2 + float is 100.
```
