# Lane notes: `src/system/char/CharCuff.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/char/CharCuff.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

File-level (was lines 152-160; no definition follows it):

```text
// w8-c: CharCuff.obj carries six `std::list<RndMesh *>` COMDATs in the image
// (CharCuff.s: allocate 0x82391300, deallocate 0x82391340, _M_create_node
// 0x82391480, insert 0x82391600, _List_base::clear 0x82391680, ~list
// 0x8239273C) that nothing in the unit calls -- every `bl` to them in the whole
// binary is in rndobj/Utl, rndobj/AmbientOcclusion or world/DefaultPhysicsManager.
// CharCuff.obj simply won the COMDAT fold, so retail's CharCuff.cpp odr-used a
// std::list<RndMesh *> whose use we have not recovered (mIgnore is an
// ObjPtrList, which is intrusive and shares nothing with std::list).  Without an
// odr-use MSVC emits nothing and all six rows read 0.0.
```
