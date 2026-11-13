# Lane notes: `src/system/meta/StorePreviewMgr.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?PlayCurrentPreview@StorePreviewMgr@@IAAXXZ`

`StorePreviewMgr::PlayCurrentPreview` -- moved verbatim from `src/system/meta/StorePreviewMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 83-87):

```text
// w16-d (99.883, 12 offset rows): the image keeps the IsLocalFile argument's
// home slot (stw r4, 0x50) apart from `str`, giving str/fp/fp 0x58/0x60/0x68
// where we pack str onto 0x50. Inert: the positive `!empty() && IsLocalFile()`
// form with the String block first. A named `const char *file` drops the 0x50
// store entirely (98.9).
```


## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/meta/StorePreviewMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

File-level (was lines 178-196; no definition follows it):

```text
// w8-j 2026-09-15 -- FLOOR claim for Handle/fn_82E1ED0C REFUTED by w18-d:
// the download_preview_file handler goes through the inline forwarder
// DownloadPreviewFile (og-dc3/rb3 name) -- one inline level more gives the
// image's separate String temp at r31+0x68. Handle and its funclet are 100.
//
//   ?PlayCurrentPreview@StorePreviewMgr@@IAAXXZ  99.883%, 412 B, 91 of 103
//     equal.  Every one of the 12 rows is a uniform -0x8: the image's first
//     EH-tracked object (`String str`) starts at 0x58 and ours at 0x50, so
//     `str`, both `FilePath fp` temps and the `int len` spill all sit 8 bytes
//     low.  The image reserves 0x50..0x57 for the MILO_ASSERT(mStreamPlayer,
//     0xd8) scratch -- the line number is passed to MakeString BY REFERENCE
//     (`ABH` in the mangled name), so it needs a real slot -- and then starts
//     objects at 0x58.  We reuse that same slot for `str`, which is legal
//     because the assert temp is dead by then, and MSVC took the reuse.
//     Instructions 0..32 are identical on both sides, including both stores to
//     0x50, so there is no missing local to add: the difference is purely
//     whether the allocator coalesced.
// No source lever found for any of the three; this is MSVC temp-area shaping.
// Measured percentages above are match_percent_normalized from a full ninja.
```
