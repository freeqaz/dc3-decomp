# Lane notes: `src/system/hamobj/FilterQueue.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?GetResults@FilterQueue@@QAA_NAAMPAPAVDetectFrame@@M@Z`

`FilterQueue::GetResults` -- moved verbatim from `src/system/hamobj/FilterQueue.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 20-26):

```text
    // w21-at: 99.98 -> 100.  The two result slots are cleared by a loop (the
    // caller's DetectFrame *resultFrames[2], one per player); MSVC unrolls it
    // into the same two stores, but only the loop form gives the following
    // size() guard the image's _M_start-then-_M_finish load order.  Two
    // straight-line stores in either order, or a chained `=`, kept the swap
    // (w13-b had called it the stlport size() order family; an unsigned
    // index and w18-b's `size() > frameIdx` were inert / worse).
```


## `?Poll@FilterQueue@@QAAXABUSkeletonUpdateData@@@Z`

`FilterQueue::Poll` -- moved verbatim from `src/system/hamobj/FilterQueue.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 95-99):

```text
        // w16-b: 98.41 -> 100. The image holds only inFrame (r30) and
        // `&mErrorNodes[0]` (r25, taken right after the mFilterVersion load and
        // bumped by 4 per node); NumNodes()/NodeInput() re-read
        // inFrame->mFilterVersion. A named array pointer at the top does that;
        // a named `filterVer` local (w7-y) pinned the FilterVersion instead.
```
