# Lane notes: `src/system/gesture/SkeletonClip.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?PrevSkeleton@SkeletonClip@@UBA_NABVSkeleton@@HAAVArchiveSkeleton@@AAH@Z`

`SkeletonClip::PrevSkeleton` -- moved verbatim from `src/system/gesture/SkeletonClip.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 276-283):

```text
            // The image does not dereference the pointer CurRecordedFrame
            // returned: it recomputes the element from the vector base and the
            // out-param index -- `lwz r8,0x50(r31)` / `mulli r10,r8,0x1c8` /
            // `lwz r11,0x0(r3)` / `add r11,r10,r11` / `lfs f10,0x1c4(r11)` at
            // 825??? (idx 21/36/44/49/52 of the listing).  Spelling it that way
            // makes our object exactly the target's size (320 == 320).  The
            // slot colouring that kept this at 71.0 (w7-am) was the second
            // call's out-params, below.
```


## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/gesture/SkeletonClip.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

File-level (was lines 377-387; nearest following definition: `SkeletonClip::Load`):

```text
// w21-bm: 99.991 -> 100.  INIT_REVS at FILE scope, right here.  The image's
// rev pair (lbl_82251B80: 0x0009,pad,0x0001,pad) is the FIRST object of this
// TU's .rdata (splits.txt starts SkeletonClip's .rdata at 0x82251C08, but
// 0x82251B80..0x82251C08 holds this pair plus DateTimeStr/IsFailClip unwind
// tables, i.e. SkeletonClip's own .rdata), so per the w15-r rule above
// INIT_REVS in obj/Object.h gRev is the lis/addi anchor and gAltRev is
// reached as anchor+4 (`addi r7, r27, 0x4`), scheduled before &d.altRev.
// Replaces w11-d's `static const unsigned short gRevs[4] = {9,0,1,0}` (2 rows:
// &d.altRev set up before &gRevs[2]); w16-c had measured an INIT_REVS spelling
// at 98.0 (placement not recorded).  Same values, same reads.  The rev checks
// stay hand-written: the image remaps rev 10 to 9/alt 1 before them.
```
