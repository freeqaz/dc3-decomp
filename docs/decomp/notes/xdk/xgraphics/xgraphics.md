# Lane notes: `src/xdk/xgraphics/xgraphics.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/xdk/xgraphics/xgraphics.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

File-level (was lines 138-144; no definition follows it):

```text
// Same as XGSetTextureHeader but with an extra Pitch argument between
// MipOffset and pTexture -- DxTex::SyncBitmap passes 13 arguments (8 in
// r3-r10, five in the parameter save area at r1+0x54..0x74).
// BaseOffset/MipOffset are UINT as in the XDK (XGHEADER_CONTIGUOUS_MIP_OFFSET
// is (UINT)-1): with INT here the -1 SyncBitmap passes is a different
// constant from params.HierarchicalZBase = -1 and the image's shared
// prologue li r28,-1 is not reproduced (w7-bm, 94.7 -> 95.0).
```
