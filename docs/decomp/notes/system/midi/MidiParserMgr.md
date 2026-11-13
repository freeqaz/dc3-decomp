# Lane notes: `src/system/midi/MidiParserMgr.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ParseText@MidiParserMgr@@AAAPAVDataArray@@PBDH@Z`

`MidiParserMgr::ParseText` -- moved verbatim from `src/system/midi/MidiParserMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 157-162):

```text
// w17-d: 99.983604 -> 100. The image's catch funclet copies mFilename into a
// frame temp (lwz r11,0x54(r11); stw r11,0x50(r31); addi r4,r31,0x50) before
// binding it to MakeString's `const char *const &` -- the argument was an
// RVALUE (an accessor call), not the member lvalue. That temp is what pooled
// with the assert-line int at 0x50 and pushed `parsed` to 0x54. A same-type
// `(const char *)` cast is folded back to the lvalue and is inert.
```
