# Lane notes: `src/lazer/meta_ham/SongSortNode.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Text@SongSortNode@@UBAXPAVUIListLabel@@PAVUILabel@@@Z`

`SongSortNode::Text` -- moved verbatim from `src/lazer/meta_ham/SongSortNode.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 293-299):

```text
// w20-f: normalized 100 / fuzzy 99.9165, 479/479 instructions equal; no
// operand or branch divergence.  The 8 charged rows are ham1/ham2/ham3
// (?BH@ vs our ?CP@) and song_select_song_prefix (?FF@ vs our ?HK@) scope
// indices -- but ham_xbox_r.map has NO local statics for this function, so
// those symbols.txt names were synthesised from an early build of ours
// (f821f972f) and are not evidence (fixable-scope-index.md, "The target side
// is ham_xbox_r.map").  Left alone.
```
