# Lane notes: `src/system/os/HolmesClient.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?HolmesSetFileShare@@YAXPBD0@Z`

`HolmesSetFileShare` -- moved verbatim from `src/system/os/HolmesClient.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 594-598):

```text
    // w8-g: the target anchors ONE base register on gShareName here and reaches
    // gMachineName backwards from it (`subi r3, r31, 0x40` at 0x825c4bd4), the
    // mirror image of the HolmesClientInitOpcode case documented above.  Two
    // plain symbol names make MSVC materialize two independent addresses, so
    // the machine-name site has to be spelled relative to gShareName.
```


## `?HolmesClientOpen@@YA_NPBDHAAIAAH@Z`

`HolmesClientOpen` -- moved verbatim from `src/system/os/HolmesClient.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 659-667):

```text
        // All four flag bits are extracted up front and parked in callee-saved
        // registers -- the image's four consecutive `extrwi` at 0x825F1E4C..58
        // (bits 1, 8, 9 and 18, in that order) sit BEFORE the `bl Write` that
        // lowers `<< val`, so the declarations have to precede that statement
        // in the source.  The extraction is UNSIGNED -- `extrwi`, not the
        // `srawi`+mask an `int` shift produces.
        // FIXED (w12-d): the w7-ar r29/r30 residual came from chaining
        // `<< val << filename`; the image reloads gStreamBuffer for the
        // filename, i.e. two statements.
```


## `?HolmesClientCacheFile@@YA_NPADPBD@Z`

`HolmesClientCacheFile` -- moved verbatim from `src/system/os/HolmesClient.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 892-912):

```text
    // RESIDUAL (w7-ar, 94.83 canonical): 6 of 119 rows, pure scheduling. The image
    // emits `lis r11, gLastCachedResource@ha` at 0x825F2588 -- two slots earlier
    // than we do -- and the operator== argument setup (`addi r4, r11, 0x4` /
    // `addi r3, r31, 0x58`, 0x825F259C/0x825F25A0) three slots earlier, straddling
    // the writeTime `std` instead of following it. Refuted: declaring writeTime
    // before fileExists (inert, same 6 rows). Also refuted (w12-d): a typed
    // WIN32_FILE_ATTRIBUTE_DATA with `*(s64 *)&fileInfo.ftLastWriteTime` (inert).
    // w18-d: `attrResult != 0` (equal semantics) is byte-inert, same 6 rows.
    // w31-b (2026-11-03): MEASURED AS A PLACEMENT RESIDUAL, not a work item.
    // Both sides are 116 instructions and the opcode+immediate multiset is
    // IDENTICAL -- the only keys present on one side and not the other are the
    // six branch displacements, which move because the block itself moves.
    // Everything the body computes agrees with the image: `subic r8,r3,1` +
    // `subfe r28,r8,r9` is exactly `(attrResult - 1) != -1`, i.e.
    // attrResult != 0; writeTime is `ld` from fileInfo+0x14 into the s64 slot
    // and written with `li r5,8`; the four `stw r11,0/4/8/0xc(r29)` tail is the
    // dst[0..3] reorder; fileInfo is at r1+0x80 and the s64 at r1+0x60, the
    // same slots the image uses. What differs is only WHERE c2 schedules the
    // `lis gLastCachedResource` (1 slot) and the operator== argument setup
    // (3 slots) relative to the writeTime `std`: the image straddles the std,
    // we sink both below it. No source handle was found for a sched0 slot.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.
