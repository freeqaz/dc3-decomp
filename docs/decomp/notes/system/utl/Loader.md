# Lane notes: `src/system/utl/Loader.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?PollFrontLoader@LoadMgr@@AAAXXZ`

`LoadMgr::PollFrontLoader` -- moved verbatim from `src/system/utl/Loader.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 312-316):

```text
// w17-d: 99.981 -> 100 (and its unwind funclet fn_827D15BC 99.9 -> 100). The
// image's missing 16 bytes of frame were a MemHeapTracker at 0xc0: the
// function's second unwind funclet (__unwind$106779) calls
// ??1MemHeapTracker@@QAA@XZ on r31+0xc0. The push/pop is that RAII scope,
// not a bare MemPushHeap/MemPopHeap pair.
```
