# Lane notes: `src/system/rndobj/MatAnim.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Copy@RndMatAnim@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z`

`RndMatAnim::Copy` -- moved verbatim from `src/system/rndobj/MatAnim.cpp` on 2026-11-13 (lane w-five-clean; line numbers are as of the commit before the move)

Above the definition (was lines 86-122):

```text
// w21-y (was 97.89, 2 rows: the `stw sOwner` sits one slot later in the
// image, after `subi r4, r31, 0x88` for mKeysOwner = this).  Behaviour checked
// against the image: ty==1 / ty==2 && owner!=m take the CopyRef branch, the deep
// path sets sOwner, mKeysOwner and the six key vectors -- identical; the
// remaining call-name rows are ICF folds.  MEASURED NEGATIVE: SetObjConcrete
// directly (inert), static_cast<Hmx::Object *>(this) (inert), swapping the two
// assignments (93.67, store after the call), `RndMatAnim *self = this;` for
// both = 98.87 (shipped): the store pair then matches, but MSVC CSEs the
// image's second `this` (`subi r4, r31, 0x88`) into the conversion's register,
// so 5 rows remain (the conversion reads via r4 instead of r31).
// w26-s: the residual is a c1 CSE, and 14 spellings were MEASURED INERT at
// 98.87: both stores through `this` (97.89), self/this mixed either way, two
// distinct named copies, `*const self`, `RndMatAnim &self = *this`, `&*self`,
// a cast at either use, a named `Hmx::Object *obj = self`, and a nested scope
// around the second store.  Shape read off the image: the conversion base is
// `r11` and `subi r4, r31, 0x88` is re-materialized for the ObjOwnerPtr call,
// i.e. the two RHSs are not CSE'd there at all; ours keeps ONE value in r4 and
// copy-prefers it into the call.  Hypothesis (not measured): the image's r11 is
// regasg first-fit, so the split follows from the conversion not being
// copy-preferred to r4 -- the open question is which source makes the second
// RHS a distinct value.
// w-this-adjust (2026-11-13, objdiff 4.2.9 name_check): POINTER AUDIT, NOT A
// BUG.  The `subi r11,r11,0x84` vs `addi r11,r11,0x4` row is the same
// RndMatAnim* -> Hmx::Object* virtual-base conversion: r31 is this+0x88, the
// image computes (r31 + vbtbl[1]) - 0x84, ours (r31-0x88) + vbtbl[1] + 4 --
// equal.  mKeysOwner gets r31-0x88 on both sides.  Re-measured: shipped `self`
// = 98.9 canonical, equal 90 / diff_arg 3 / diff_op 1 / del 1, 376 B (4 short);
// the RB3/rb3-xenon spelling `sOwner = this; mKeysOwner = this;` = 97.9, equal
// 94 / ins 1 / del 1, 380 B = target size, the ONLY residual being the
// `stw sOwner` emitted one slot before `subi r4, r31, 0x88` instead of after.
// That form reproduces more of the image and scores lower (insert/delete
// costs more than diff_arg); kept `self` for the score.  Also inert at 97.9:
// `mKeysOwner = (sOwner = this, this);`.
// w-five-clean (2026-11-13): READABILITY OVER SCORE -- the `self` local is
// gone; `sOwner = this; mKeysOwner = this;` is the RB3/rb3-xenon spelling, is
// exactly the image's 380 B and reproduces more of it (equal 94 vs 90).  The
// 98.87 `self` form is a score trick that banks nothing below 100.
```

Added after the move (lane w-five-clean):

```text
// w-five-clean (2026-11-13): BEHAVIOUR VERIFIED, value level, not just
// pointers.  Listing: ty==1 -> SetObjConcrete(&mKeysOwner, m->mKeysOwner);
// ty==2 && m->mKeysOwner (0x30(r30)) != m -> same; else sOwner = this,
// SetObjConcrete(&mKeysOwner, this), then six vector assignments, each
// RE-READING m->mKeysOwner (`lwz r11, 0x30(r30)`) after mKeysOwner = this, from
// 0x38/0x44/0x50/0x5c/0x68/0x74 into the same offsets of this -- the source
// order exactly.  Unicorn h4 (signal 3), fresh: DIVERGENT data_layout,
// stable_divergent -- first mismatch is __RTDynamicCast's RTTI arguments
// (per-side addresses); the comparator stops there, so it is silent on the
// rest.  Native: CopyPollSemanticsTest.MatAnim* (3 tests; 3 of 3 sabotages --
// copy from m's own vectors, invert the FromMax owner test, test kCopyDeep for
// the shallow branch -- fail them).  Readability change in the same lane:
// `self` dropped, 98.874 -> 97.895 canonical, equal 90 -> 94, 380 B.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.

## `??0TexPtr@RndMatAnim@@QAA@PAVRndTex@@@Z`

`RndMatAnim::TexPtr::TexPtr` -- moved verbatim from `src/system/rndobj/MatAnim.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 12-45):

```text
// RESIDUAL 89.500 canonical / 88.750 fuzzy (w8-h).  The 13 instructions of
// the inlined
// ObjPtr<RndTex>(DeferOwner(), tex) are identical; the residual is a 2-instruction
// permutation of the join block with the SAME multiset on both sides.  The image
// anchors sOwner first and starts its load BEFORE the compiler-generated vtable
// store (`lis r11, sOwner@h` / `lis r10, ??_7TexPtr@h` / addi / lwz / stw r10,
// 0(r3) / stw r11, 0x10(r3)); we anchor the vtable first and sink the load after
// the store.  The r10<->r11 swap is downstream -- whichever anchor is emitted
// first takes r11 on both sides.  Same values, same offsets, no behavioural
// divergence.  MEASURED NEGATIVE (w8-h): a named temp for the global,
// `Hmx::Object *owner = sOwner; mOwner = owner;`, is byte-inert (89.500,
// identical instruction table) -- the same folding w7-ac recorded for this TU.
// w21-y (stopped at 89.50, same 4 rows idx 15-18): behaviour re-checked -- both
// sides store the same 5 words (ObjRefConcrete vptr, mObject, ring links, TexPtr
// vptr, mOwner=sOwner).  MEASURED NEGATIVE: (1) the RB3/rb3-xenon spelling
// `ObjPtr<RndTex>(sOwner, tex)` = 74.0 (sOwner loaded at function entry, before
// the AddRef block; the image loads it in the join); (2) a DeferOwner overload
// taking the owner by `Hmx::Object *const &` (load deferred into the base
// mem-init) = 85.0 -- the load then sits between the ObjPtr and TexPtr vptr
// stores, so the dead ObjPtr vptr store survives (image has none).  The image
// needs the load AFTER the AddRef block yet with sOwner's address taking r11
// before the TexPtr vptr; no tried spelling gives both.
// w34-a (2026-11-04, behaviour verdict): AGREES. Both sides store the same five
// words at the same offsets with the same values -- ObjRefConcrete vptr 0x0,
// mObject 0xc, ring links 0x4/0x8, TexPtr vptr 0x0, mOwner 0x10 = sOwner -- and
// the four charged rows are the two address halves exchanging r10/r11 around a
// load that moves by one slot, not a value or offset difference, so there is no
// behavioural divergence here.  MEASURED NEGATIVE this lane: binding the global
// to a pointer reference (`Hmx::Object *&owner = sOwner; mOwner = owner;`) is
// byte-inert at 89.50000 / 88.75000.  (Initialising it in the mem-init list is
// not available: mOwner is ObjPtr's member, not TexPtr's, which is why the
// DeferOwner() form exists.)  Mechanism hypothesis: c2's scheduler places an
// independent leaf load across the compiler-inserted derived-vptr store, and no
// source operand participates in that decision.
```


## `?Load@RndMatAnim@@UAAXAAVBinStream@@@Z`

`RndMatAnim::Load` -- moved verbatim from `src/system/rndobj/MatAnim.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Inside the body (was lines 148-168):

```text
        // The cast to the Keys base is load-bearing: without it, BinStreamRev's member
        // template operator>>(T&) is an exact match for TexKeys and wins over the free
        // operator>>(BinStreamRev&, Keys<T1,T2>&) (which needs a derived-to-base
        // binding), routing the read through the BinStream vector reader instead. That
        // picks the generic operator>>(BinStream&, Key<TexPtr>&) rather than the
        // BinStreamRev specialization below, which is the one that does the deferred
        // TexPtr::Load(s, true, nullptr). Same spelling as LoadStage().
        // w16-a (closes the 6-row residual w7-ac/earlier lanes recorded here,
        // 97.90 -> 100): the image calls the std::vector BinStreamRev reader
        // DIRECTLY for every operand, and a nested free-function call chain
        // evaluates its arguments right to left -- hence all four member
        // addresses formed before the first call (0x826EC53C-0x826EC544:
        // `subi r29, r30, 0x14` / `subi r28, r30, 0x20` / `subi r30, r30,
        // 0x2c`).  Going through math/Key.h's inline Keys wrapper sequences
        // each call before the next operand is formed; the named-reference and
        // named-pointer operand spellings earlier lanes refuted were inert
        // because they still went through that wrapper.  Binding the vector
        // base is exactly what the wrapper does, so behaviour is unchanged (the
        // free vector reader is more specialized than the member template, and
        // its per-element `bs >> *it` still picks the BinStreamRev Key<TexPtr>
        // reader below).
```


## `?LoadStage@RndMatAnim@@IAAXAAVBinStreamRev@@@Z`

`RndMatAnim::LoadStage` -- moved verbatim from `src/system/rndobj/MatAnim.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 315-333):

```text
        // BEHAVIOURAL FIX (w7-av): these three read into THIS object's keys, not
        // the keys OWNER's.  The image computes all three operand addresses off
        // `this` in r30 -- `addi r4, r30, 0x50` (0x826EBFD8), `addi r29, r30,
        // 0x68` (0x826EBFE0), `addi r28, r30, 0x5c` (0x826EBFE4) -- and never
        // loads the mKeysOwner pointer
        // at 0x30(this), which the TransKeys()/ScaleKeys()/RotKeys() accessors
        // this used to call would have needed.  It cannot: RndMatAnim::Load runs
        // LoadStages(d) BEFORE `d >> mKeysOwner`, so mKeysOwner is still whatever
        // the freshly-constructed object holds when LoadStage runs.
        //
        // Residual, 73.30 canonical (was 92.79 with the accessor bug): the image
        // computes this+0x5c and this+0x68 BEFORE the first call and parks them
        // in r28/r29, so its prologue is __savegprlr_28; we rematerialise
        // `addi r4, r30, N` at each call and save only r30/r31.  Three spellings
        // are byte-for-byte inert against that -- named `Keys<Vector3,Vector3>&`
        // references, named pointers, and a mix -- which is the same refutation
        // already recorded above for RndMatAnim::Load's four-operand chain.
        // w16-a: same cause and fix as RndMatAnim::Load -- bind the vector base
        // so the image's direct std::vector reader calls evaluate right to left.
```
