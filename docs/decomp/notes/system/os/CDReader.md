# Lane notes: `src/system/os/CDReader.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?CDReadExternal@@YA_NAAPAXH_K@Z`

`CDReadExternal` -- moved verbatim from `src/system/os/CDReader.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 123-139):

```text
        // The local the high-dword pointer points at is 64 bits wide, not a
        // truncated LONG: 0x82602C9C is `std r29, 0x50(r1)`, a full 8-byte store
        // of the offset, and r5 (= r1+0x50) is what SetFilePointer gets as
        // lpDistanceToMoveHigh.  On big-endian that pointer lands on the HIGH
        // dword of the offset, which is what the API wants.  Written as
        // `LONG l = u;` we stored the truncated LOW 32 bits there instead and
        // handed the API the low half twice.  The `u64` parameter is also homed
        // (`std r5, 0xa0(r1)`) and the LONG argument reloaded out of it
        // (`lwz r4, 0xa4(r1)`) -- w8-g CLOSED (88.46 -> 100.0).  w7-az was right
        // that an explicit `(LONG)` cast is byte-inert: on PPC64 the low dword of
        // `u` is already the low half of its register, so MSVC truncates with a
        // no-op `clrrwi` and never homes the parameter.  Spelling the truncation
        // as the big-endian LOAD of that dword -- `((const LONG *)&u)[1]` -- takes
        // the parameter's address, which is what forces `std r5, 0xa0(r1)` and
        // makes the argument come back as `lwz r4, 0xa4(r1)`.  It also flips the
        // r29/r30 assignment order to the image's.  Same value on big-endian;
        // native gets the plain cast.
```
