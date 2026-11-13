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
