# Lane notes: `src/system/utl/BinStream.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?WriteEndian@BinStream@@QAAXPBXH@Z`

`BinStream::WriteEndian` -- moved verbatim from `src/system/utl/BinStream.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 275-291):

```text
// w21-af: 94.74 -> 100. The vector is built from a NAMED allocator local.
// With `new std::vector<ObjVersion>()` the allocator comes from STLport's
// default argument (`const allocator_type& __a = allocator_type()`), a
// temporary created inside the new-expression's conditional construct arm;
// StlNodeAlloc has a user-declared destructor, and MSVC zero-inits a 4-byte
// temp ahead of the `if` (the dead `stw r30, 0x50(r31)` w7-az found, which also
// pulled `li r30, 0x0` up) -- most likely the conditional-destruction flag for
// that temporary. A named local is constructed unconditionally and needs none.
// Still 94.74: the temp spelled at the call site,
// `new std::vector<ObjVersion>(std::vector<ObjVersion>::allocator_type())`.
// Refuted on the way (one full ninja, whole binary): deleting
// `~StlNodeAlloc() {}` from utl/StlAlloc.h also closes this function but costs
// 65 others (DOWN 65 / UP 1), so the destructor is real. Earlier inert
// spellings (w7-az, w9-b): `new std::vector<ObjVersion>` without `()`, a named
// vector-pointer local, a named ObjVersion temp, deleting `~ObjVersion() {}`.
// (w9-b's split-_VECTOR_IMPL-ctor experiment in stl/_vector.h was never
// measured and is now unnecessary.)
```
