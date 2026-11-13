# Lane notes: `src/system/char/CharLipSync.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/char/CharLipSync.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

File-level (was lines 420-424; no definition follows it):

```text
// w8-c: orphan COMDAT -- CharLipSync.obj carries
// `std::vector<String>::_M_allocate_and_copy<String *>` at 0x823BCFC8
// (CharLipSync.s) with no caller in the unit.  It is a MEMBER TEMPLATE, so
// `template class std::vector<String>;` would not reach it.
// _M_allocate_and_copy is an stlport/libstdc++ internal; libc++ (web) has none.
```
