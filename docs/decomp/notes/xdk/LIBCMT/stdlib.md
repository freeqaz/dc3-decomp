# Lane notes: `src/xdk/LIBCMT/stdlib.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/xdk/LIBCMT/stdlib.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

File-level (was lines 9-17; no definition follows it):

```text
// w15-b: the XDK CRT declares the allocators _CRTNOALIAS _CRTRESTRICT, i.e.
// __declspec(noalias) (the call touches no global state except through its
// pointer arguments) and __declspec(restrict) (the result aliases nothing).
// Without noalias MSVC must reload every cached file-static after a realloc;
// the image does not: yy_get_next_buffer keeps yy_current_buffer in r31 across
// `bl realloc` (0x825C8B44), and yy_create_buffer computes `size + 2` before
// storing yy_buf_size because the store is no longer pinned above malloc.
// Whole-binary A/B (full ninja): yy_get_next_buffer 93.56 -> 100,
// yy_create_buffer 95.35 -> 100, MemTrackInit 96.40 -> 100, 0 rows down.
```
