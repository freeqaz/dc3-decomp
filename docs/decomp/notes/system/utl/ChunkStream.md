# Lane notes: `src/system/utl/ChunkStream.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Eof@ChunkStream@@UAA?AW4EofType@@XZ`

`ChunkStream::Eof` -- moved verbatim from `src/system/utl/ChunkStream.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 348-358):

```text
        // w21-ag: 97.97 -> 100 (all 300 rows equal).  mCurBufOffset is spelled
        // as a read through the freshly stored mCurChunk, not as
        // mChunkInfo.mMaxChunkSize: mCurChunk = mChunks - 1 points at
        // mMaxChunkSize (the int just before mChunks[0] in ChunkInfo), so it is
        // the same word.  MSVC folds the address back to the direct
        // `lwz r11, 0x30(r31)` the image has, but the dependence on the pointer
        // store keeps that load after the 0x8ac / 0x8b0 stores (827E0A10), and
        // the 0x888 store comes last -- the image's order.  The earlier
        // RESIDUAL note (w7-bp/w7-bs, "no member-store order reaches it",
        // "ordering dependence the literal does not carry") was this load.
        // Statement order is RB3's.
```


## `?DecompressChunk@ChunkStream@@CAXAAUDecompressTask@@@Z`

`ChunkStream::DecompressChunk` -- moved verbatim from `src/system/utl/ChunkStream.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 448-457):

```text
// w14-d (99.98, 4 rows): the image loads mDecompressedSize before mBuffer in
// the third arm and computes (size - dataMsk) + buffer.  All INERT: the operand
// order `(size - dataMsk) + buffer`, the expression inlined as the argument,
// `buffer + size - dataMsk`, `buffer - dataMsk + size`.
// w17-d (99.980): also inert -- `&((char *)mBuffer)[size - dataMsk]` and RB3's
// `dataOffset = &buf[size]; DecompressMem(dataOffset - dataMsk, ...)`.
// w21-bc: closed (99.98 -> 100) by binding mBuffer to a block-local `buffer`
// in the third arm only (lever: bind a member to a local once). A
// function-scope local instead hoists the load into a callee-saved register
// and breaks the first two arms (probed).
```
