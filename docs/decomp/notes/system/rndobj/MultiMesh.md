# Lane notes: `src/system/rndobj/MultiMesh.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/rndobj/MultiMesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

File-level (was lines 12-36; nearest following definition: `RndMultiMesh::RndMultiMesh`):

```text
// w18-a: ??_GFixedSizeAlloc (99.545) and ??3FixedSizeAlloc (98.333) differ ONLY
// in the __FILE__ string MEM_OVERLOAD passes to MemFree: the image's literal is
// "e:\lazer_build_gmc1\system\src\utl/PoolAlloc.h" (forward slash -- the
// original TU first included it as "utl/PoolAlloc.h"), ours has the backslash
// spelling because this TU is compiled with decomp_pch.h, which reaches
// PoolAlloc.h through obj/ObjPtr_p.h's "utl\PoolAlloc.h".  utl/PoolAlloc.obj
// (also a PCH TU) needs the BACKSLASH spelling, so no single PCH include
// spelling serves both; closing these needs this TU built without the PCH
// (configure.py has no per-file opt-out today).
// w21-bj: CLOSED (both 100) -- this TU is now in config.pch_excluded_sources and
// MultiMesh.h includes "utl/PoolAlloc.h" (forward slash) before obj\Data.h, so
// it is the first opener.  Whole-binary A/B: UP 2, DOWN 0.
//
// w26-f2: the implicit `stlpmtx_std::list<RndMultiMesh::Instance>::operator=`
// emitted from this TU (`??4?$list@UInstance@RndMultiMesh@@...`) reads fuzzy
// 99.111115 at canonical 100.0, and it is NOT a spelling this TU controls.
// All six charged rows are the emit order of two INDEPENDENT iterator advances
// in the template body -- src/system/stlport/stl/_list.c:95, the
// `while (__first1 != __last1 && __first2 != __last2) *__first1++ = *__first2++;`
// loop: image `mr r11,r31; lwz r31,0(r31); mr r10,r30; lwz r30,0(r30)`, ours
// `mr r11,r30; lwz r30,0(r30); mr r10,r31; lwz r31,0(r31)` -- plus the
// `addi r3/r4` argument order derived from it.  The final register state is
// identical; only the schedule differs.  Moving that means editing a
// PCH-reached header that reaches 574 TUs, so it is out of scope for a lane
// that may not touch one.  No edit here can reach it.
```


## `?CollideList@RndMultiMesh@@UAAXABVSegment@@AAV?$list@UCollision@RndDrawable@@V?$StlNodeAlloc@UCollision@RndDrawable@@@stlpmtx_std@@@stlpmtx_std@@@Z`

`RndMultiMesh::CollideList` -- moved verbatim from `src/system/rndobj/MultiMesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 357-367):

```text
// RESIDUAL (w11-b, 99.22 canonical, 2 rows): `Refs().empty()` on the proxy's
// virtual-base Hmx::Object -- the image keeps the vbase adjust and the mRefs
// offset as two `addi rX,..,0x4`; we fold them into one `addi 0x8`. Same shape
// in CamShotCrowd::GetSelectedCrowd (98.34). Inert: begin()==end() (inline and
// in ObjRef::empty itself), a `const ObjRef &` local; a static_cast to
// Hmx::Object* adds a null check (94.6). Unit also blocked by the PCH-fixed
// utl\PoolAlloc.h __FILE__ spelling (see obj/Data.h).
// w17-a (still 99.22): `static_cast<Hmx::Object &>(*sit->first).Refs().empty()`
// DOES produce the image's two separate addi (vbptr displacement, then mRefs)
// but MSVC keeps a null test on the reference conversion (94.6); an inline
// proxy member `return Refs().empty();` folds back to one addi (99.22).
```

Inside the body (was lines 404-421):

```text
                                // RESIDUAL (99.2171, 2 rows): the image forms
                                // &mRefs as `addi r8, r9, 0x4` + `addi r8, r8,
                                // 0x4` off the vbase pointer; we fold the two
                                // offsets into one `addi r8, r9, 0x8`.  Five
                                // spellings of this call reproduce both the
                                // score and the same 2-row residual under the
                                // pinned name_check ruler (99.2171): this one,
                                // a named `const ObjRef &` local, a named
                                // `const ObjRef *` local, `empty() != false`,
                                // and a named proxy pointer.  `Next() ==
                                // &Refs()` and `begin() == end()` are WORSE
                                // (97.98), and a non-const `ObjRef &` local
                                // does not compile (Refs() returns a const
                                // reference).  So the fold is the constant
                                // folder's, not the source's: the address is
                                // one IL tree either way.  Nothing in the
                                // ObjRef/Owner headers reaches it (both are PCH
                                // headers and are not edited).  w26-p.
```

## `??4?$list@UInstance@RndMultiMesh@@V?$TransformListAlloc@UInstance@RndMultiMesh@@@stlpmtx_std@@@stlpmtx_std@@QAAAAV01@ABV01@@Z`

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.111, 6 non-equal row(s): NOT A BUG.
Every row is register-only; `scripts/analysis/regperm_equivalence.py` raised 10 lead(s),
each read by hand against the listing: pure register permutation, no operand swap. The
checker's leads are the dst/src node walk in r31/r30 with the two `mr`s emitted in the
opposite order; memcpy still gets (dst+8, src+8, 0x44).
