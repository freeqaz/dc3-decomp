# Lane notes: `src/system/rndobj/MeshAnim.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/rndobj/MeshAnim.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

File-level (was lines 5-28; nearest following definition: `MeshAnimAppendVertColorKey`):

```text
// w8-e 2026-09-15: ?push_back@?$vector@VColor@Hmx@@... (136 B, 0%) is a real
// same-TU gap -- ham_xbox_r.map contributes it from `rndobj:MeshAnim.obj` at
// 826d5788, so the image really does push_back into a std::vector<Hmx::Color>
// in this file.  Our MeshAnim.cpp reaches its colour keys with resize() /
// insert() / erase() instead, which emit a different COMDAT set, so push_back is
// never instantiated and the row reads 0.0%.  The lever is rewriting whichever
// colour-key append path the image uses as a literal `push_back`; note the
// sibling ??$?6VVector2@...BinStream at 826d4508 is also contributed by this
// object, which is consistent with the load/save path here being vector-shaped.
//
// w8-h 2026-09-15, ADJUDICATED: this is the discarded-function ORPHAN
// INSTANTIATION shape, not a missing append path.  MeshAnim.s defines
// push_back<Hmx::Color> at 826d5788 but contains NO `bl` to it anywhere; the
// only three call sites in the whole image are world/Crowd.s:8997, :10651 and
// world/ColorPalette.s:1504.  Its own callee
// _M_insert_overflow_aux<Hmx::Color> is NOT defined in this object either (the
// three _M_insert_overflow_aux COMDATs here are all for the OUTER
// vector<Key<...>>).  So MSVC instantiated push_back through an
// external-linkage function in this TU and /OPT:REF then discarded that
// function -- its name is not recoverable from the binary.  Per
// docs pattern "orphan instantiation from a discarded function", an
// unreferenced EXTERNAL-linkage stand-in reproduces the COMDAT exactly and a
// `static` one does nothing (MSVC drops a static before instantiating inside
// it).  The name below is a stand-in, not the shipped name.
```
