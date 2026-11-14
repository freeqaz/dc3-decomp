# Lane notes: `src/system/net/DingoJob.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?CheckReqResult@DingoJob@@MAA_NXZ`

`DingoJob::CheckReqResult` -- moved verbatim from `src/system/net/DingoJob.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 112-116):

```text
// w14-e (96.2, 3 rows): the image schedules the `response = nullptr` store
// between `li r6, 0` and the two argument `addi`s; we put `addi r5` first.
// Declaring `response` above the converter reorders the frame (90.2).
// w18-e: still 96.2.  Inert: a separate `int *version = nullptr` local for
// the third argument.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.
