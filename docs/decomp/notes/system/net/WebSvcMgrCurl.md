# Lane notes: `src/system/net/WebSvcMgrCurl.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@WebSvcMgrCurl@@UAAXXZ`

`WebSvcMgrCurl::Poll` -- moved verbatim from `src/system/net/WebSvcMgrCurl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 25-30):

```text
// w8-l: 99.978490 normalized, the only function short of 100% in this unit.
// Two rows, [16] and [19]: the FIRST MILO_ASSERT's line-number temp homes at
// 0x50(r1) in the image and 0x54(r1) in ours.  Every NAMED local agrees exactly
// (httpStatus 0x50, msgs_in_queue 0x54, running_handles 0x58, assert-2 temp
// 0x5c) -- only which free slot the first assert temp pools into differs.
// Same family as PlaylistSongProvider::DataSymbol; no source lever found.
```
