# Lane notes: `src/system/moviebink/BinkMovieImpl.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?New@MovieInternalBuffers@@SAPAU1@V?$vector@PAUBINK@@V?$StlNodeAlloc@PAUBINK@@@stlpmtx_std@@@stlpmtx_std@@@Z`

`MovieInternalBuffers::New` -- moved verbatim from `src/system/moviebink/BinkMovieImpl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 123-129):

```text
            // w16-e: 99.978 -> 100.  `curBuffers` is declared at function
            // scope, not inside the loop.  With the block-scoped declaration
            // MSVC loaded curBuffers.X before ret->mBuffers.X in the four
            // unsigned Max() calls (8 rows, which w7-m/w7-aa measured as a
            // scheduler tie); at function scope the image's acc-first load
            // order falls out.  Behaviour is unchanged: it is memset and
            // refilled by BinkGetFrameBuffersInfo on every iteration.
```


## `?DiscContentionPublish@BinkMovieImpl@@QAAXXZ`

`BinkMovieImpl::DiscContentionPublish` -- moved verbatim from `src/system/moviebink/BinkMovieImpl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 707-712):

```text
// RESIDUAL (w12-b, 99.0 canonical): register-only. The image keeps `this` in
// r30 (&mThreadId in r29) and later reuses r30 for the iterator (`mr r30, r11`
// after `lwz r11, 0xc4(r30)`); we put `this` in r29 and fold it into
// &unkbc. All six orders of {first, count, str} measured: (str, first, count)
// fixes first/count = r28/r29 but sinks their `li`s below String(); none
// moves `this` to r30.
```
