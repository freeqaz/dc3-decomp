# Lane notes: `src/system/utl/NetCacheMgr.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?AddLoaderRef@NetCacheMgr@@IAAPAUNetLoaderRef@@PBDW4RefType@1@W4NetLoaderPos@@@Z`

`NetCacheMgr::AddLoaderRef` -- moved verbatim from `src/system/utl/NetCacheMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 396-406):

```text
        // w14-d: 97.17 -> 100. Three levers, all behaviour-neutral:
        // (1) Create/NetCacheLoader take `name` by IMPLICIT conversion -- that,
        //     not a named local, is what makes the image re-address the String
        //     temp (`addi r3, r31, 0x58` / `addi r5, r31, 0x68`) instead of
        //     consuming the ctor's return register;
        // (2) NetLoaderRef has an inline (String, refcount, net, cache) ctor and
        //     a zeroing default ctor: a named aggregate `tmp` made MSVC share
        //     the two arms' `s` slot (0x58) where the image gives each arm its
        //     own (0x60 / 0x70, frame 0x160 not 0x150);
        // (3) the "Found loader" MakeString takes `type` itself (its param home
        //     0x184 is the int& the image passes), not an `(int)type` temp.
```

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/utl/NetCacheMgr.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

File-level (was lines 43-63; nearest following definition: `NetLoaderRef::NetLoaderRef`):

```text
// w8-g 2026-09-15 -- BEHAVIOURAL LEAD, not closed.  The two NetCacheMgr EH
// funclets fn_827F1C80 and fn_827F1CF8 (40 B each, 99.80% normalized) each carry
// one charged row, and it names a destructor we never call:
//   image:  bl ??1?$pair@VString@@P6APAVLoader@@ABVFilePath@@W4LoaderPos@@@Z@stlpmtx_std@@QAA@XZ
//           i.e. ~pair<String, Loader *(*)(const FilePath &, LoaderPos)>,
//           at r31+0x80 (fn_827F1C80) and r31+0xa0 (fn_827F1CF8)
//   ours:   bl ??1NetLoaderRef@@QAA@XZ  at r31+0x90
// The parent frame also disagrees: 0x160 in the image, 0x150 in ours.  That is
// consistent with a container whose ELEMENT TYPE is wrong -- the image's is a
// std::pair of a String and a Loader-factory function pointer, ours is
// `std::list<NetLoaderRef> mNetLoaderRefs` (below).
//
// Both funclets are flagged UNVERIFIABLE_PAIRING (paired by byte signature, not
// by name), so the rows are not falsifiable in isolation, and the one question
// that IS falsifiable comes back NEGATIVE: our tree does emit that pair
// destructor -- `strings -a` finds it in build/373307D9/src/system/utl/Loader.obj
// (it is the Loader factory registry's map element), and the target has it in
// dozens of objects.  So this is most likely objdiff pairing our NetLoaderRef
// funclet against an unrelated Loader-registry funclet that happens to have the
// same masked bytes, NOT a missing symbol.  Left as a lead; do not spend a row
// budget on it without first matching the parent function.
```
