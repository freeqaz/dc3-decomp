# Lane notes: `src/lazer/meta_ham/FitnessCalorieSortByCalorie.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/lazer/meta_ham/FitnessCalorieSortByCalorie.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

File-level (was lines 64-69; no definition follows it):

```text
// w10-e: both New*Node closed 99.973 -> 100 by scoping `s` in its own block.
// The image's EH temp for `new NavList*Node` reuses `s`'s slot (stw r3, 0x54(r31))
// while `calories` keeps 0x50 -- i.e. `s` is dead (its block closed) and the
// calories slot is still in scope.  With `s` at function scope MSVC reused the
// calories slot instead.  `Symbol token;` costs nothing: its default store is
// dead and is dropped before `token = s`.
```
