# Lane notes: `src/system/hamobj/HamIKSkeleton.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetBone@HamIKSkeleton@@IAAXPAVRndTransformable@@0@Z`

`HamIKSkeleton::SetBone` -- moved verbatim from `src/system/hamobj/HamIKSkeleton.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 210-222):

```text
    // RESIDUAL (w13-b, 99.23): the image's guard for this NOTIFY_ONCE static
    // tests bit 0x2 (`rlwinm. r9, r11, 0, 30, 30` / `ori r11, r11, 0x2`), ours
    // bit 0x1: something in the original claimed the first bit of this
    // function's guard word (lbl_82F60FE0, referenced only from here) and was
    // optimised away.  Not reconstructed: no observable statement explains it.
    // w21-t: mechanism CONFIRMED, not shipped.  A dead static lexically
    // before _dw claims guard bit 0: `if (0) { MILO_NOTIFY_ONCE(...); }` as
    // the first statement reads 100 normalized, but under name_check the
    // atexit dtor becomes ??__F_dw@?N@ where the map has ?M@ (one scope too
    // many); the same block placed AFTER keeps ?M@ but bit 0x1.  So the
    // original had a dead static before _dw AND one scope fewer somewhere
    // before it.  Shipping invented dead code would also emit an extra
    // ??__F_dw dtor the map lacks, so the faithful code stays (99.23).
```
