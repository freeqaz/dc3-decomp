# Lane notes: `src/system/obj/Utl.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `ObjDirItrObjectDirInstantiation()`

`ObjDirItrObjectDirInstantiation` -- moved verbatim from `src/system/obj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Attribution: no function named `ObjDirItrObjectDirInstantiation` in report.json (unit); filed under the source name

Above the definition (was lines 290-299):

```text
// w8-a: ORPHAN INSTANTIATION. The image's obj/Utl.obj owns the COMDATs for
// ObjDirItr<ObjectDir>::{ctor,Advance,operator++} (0x8259B6C0, 0x8259B220,
// 0x8259B798 in build/373307D9/asm/system/obj/Utl.s) but nothing *inside*
// obj/Utl.s calls them -- the only caller in the whole image is
// obj/TypeProps.s:1835. So the odr-use lived in an external-linkage function
// that /OPT:REF discarded; its name is not recoverable from the binary.
// obj/Utl.h still declares two such never-defined, never-shipped functions
// (InitObject, StringMatchesFilter), which is independent evidence that this
// TU carried discarded externals. A `static` stand-in does nothing -- MSVC
// drops an unreferenced static before instantiating through it.
```
