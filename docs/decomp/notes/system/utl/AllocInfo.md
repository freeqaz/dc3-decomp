# Lane notes: `src/system/utl/AllocInfo.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `??0AllocInfo@@QAA@HHPBDPAXC_NE0HAAVString@@3@Z`

`AllocInfo::AllocInfo` -- moved verbatim from `src/system/utl/AllocInfo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 35-46):

```text
    // Body assignments, NOT a member-init list, and mFile HOISTED above the
    // three byte members: MSVC forces an init list into DECLARATION order, and
    // the image's store order is not declaration order -- it runs
    // 0x0, 0x4, 0x8, 0xc, 0x15, then the bytes 0x10/0x11/0x14, then 0x19.
    // Putting `mFile = file` ahead of the bytes here is worth 6.3pp
    // (88.66 -> 95.0); an init list, or body assignments in declaration
    // order, both read 88.66 and hoist mStrat/mPooled to the very front.
    // w15-b NEGATIVE (95.0): an exhaustive sweep of the six word stores
    // (mReqSize/mActSize/mType/mMem/mFile/mLine, the three byte stores kept
    // together just before mLine) -- ~700 of the 720 orders, each a per-TU
    // build -- found no order reaching 100.  The source order is not the lever:
    // MSVC re-sorts the first four stores (written 0,4,8,c it emits c,8,0,4).
```
