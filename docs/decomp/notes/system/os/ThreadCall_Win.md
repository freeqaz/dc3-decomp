# Lane notes: `src/system/os/ThreadCall_Win.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ThreadCallPoll@@YAXXZ`

`ThreadCallPoll` -- moved verbatim from `src/system/os/ThreadCall_Win.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 94-99):

```text
// w14-d (94.63, 6 rows): the image zero-extends the saved type before the
// switch (`clrrwi r10, r10, 0` at 825CEAF4) and schedules the gReadyForNext
// store / gData address differently.  INERT: `uint oldType`.  WORSE (93.88):
// declaring oldType inside the `if`; RB3's AdvanceIdx/ordering shape.
// w18-d (94.63): `switch ((unsigned int)oldType)` does not produce the image's
// `clrrwi r10, r10, 0` zero-extension either.
```
