# Lane notes: `src/system/utl/PoolAlloc.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/utl/PoolAlloc.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

File-level (was lines 12-17; nearest following definition: `PoolAllocInit`):

```text
// w14-l: FILE STATICS.  Neither is in the linker map (which names every public
// global), and RawAlloc reaches gSmallHunk as +0x4 off the one base it
// materializes for gBigHunk (827CF164 `addi r31, r11, gBigHunk@l`, then
// 0x0(r31) / 0x4(r31)) -- MSVC only co-addresses two globals when both have
// internal linkage.  Their .data is the image's 0x82F189D0 / 0x82F189D4, which
// splits.txt used to file under MemMgr's range.
```


## `?RawAlloc@FixedSizeAlloc@@MAAPAHH@Z`

`FixedSizeAlloc::RawAlloc` -- moved verbatim from `src/system/utl/PoolAlloc.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 106-117):

```text
// w21-bc: 98.214 -> 100 with RB3's ChunkAllocator::RawPoolAlloc shape: no
// `buf` local held across the refill -- the refill bumps the GLOBAL
// (`sPoolBuf += 0x10`) and the tail reads it back (`ret = sPoolBuf; sPoolBuf +=
// words`). MSVC then coalesces the phi onto r3 (image 0x827CF13C `lwz r3,
// sPoolBuf` / 0x827CF1D4 `addi r3, r11, 0x40`) and the extra `mr r3, r11` is
// gone. Same values stored and returned as the old `buf` spelling.
//
// The pool is walked in INT UNITS: the image's `srawi r11, r4, 2` / `slwi r28,
// r11, 2` pair (which MSVC would fold to one clrrwi for the byte-wise
// `(size >> 2) << 2`) is the source's `>> 2` plus the compiler's sizeof(int)
// scaling of `int *` arithmetic. Same at sPoolEnd. gBigHunk is read again after
// the calls (the image loads it twice).
```
