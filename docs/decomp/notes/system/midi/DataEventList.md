# Lane notes: `src/system/midi/DataEventList.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?InsertEvent@DataEventList@@QAAXMMABVDataNode@@H@Z`

`DataEventList::InsertEvent` -- moved verbatim from `src/system/midi/DataEventList.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 152-157):

```text
// w21-bg: 99.912 -> 100.  The frame (0xb0, `str` at 0x58, `event`/`d` at
// 0x60) is decided by how `str` is printed into: `str << node` goes through
// the inline `operator<<(TextStream &, const DataNode &)` (obj/Data.h), which
// forwards `str` and so PINS it to its own slot (docs/decomp/patterns/
// stack-slot-sharing.md, m_fwd); a direct `node.Print(str, false, 0)` lets
// MSVC pack str onto event's slot (the old 0xa0 frame).  Same call either way.
```
