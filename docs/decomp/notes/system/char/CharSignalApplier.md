# Lane notes: `src/system/char/CharSignalApplier.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `??6@YAAAVBinStream@@AAV0@ABUBoneOp@CharSignalApplier@@@Z`

`operator<<` -- moved verbatim from `src/system/char/CharSignalApplier.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Attribution: ambiguous: 2 overloads of `operator<<` fit equally; filed under the first, also possible: `??$?6UBoneOp@CharSignalApplier@@V?$StlNodeAlloc@UBoneOp@CharSignalApplier@@@stlpmtx_std@@@@YAAAVBinStream@@AAV0@ABV?$vector@UBoneOp@CharSignalApplier@@V?$StlNodeAlloc@UBoneOp@CharSignalApplier@@@stlpmtx_std@@@stlpmtx_std@@@Z`

Above the definition (was lines 26-48):

```text
// w28-b (2026-11-02): the unit's one non-100 row is
// vector<BoneOp>::_M_insert_overflow_aux (canonical 97.84946, fuzzy 97.52688;
// 5 rows of 93).  It is NOT a local-copy or call-site problem: every element
// operation in it is already byte-equal, including the `_Copy_Construct` that
// inlines BoneOp's copy ctor (`ObjPtr` build + `operator=`).  The whole
// residual is the three `__new_finish = <call>(...)` assignments inside the
// template's body.  The image writes __new_finish back to its home slot
// (0x54(r31)) after the FIRST `__uninitialized_copy` and again after the
// `__uninitialized_fill_n`; we write it once, at the join before
// `_M_clear_after_move`.  The image therefore carries one extra
// `stw r3, 0x54(r31)` and orders the two register moves the other way
// (`mr r8,r8` then `mr r29,r3` in the image; `mr r29,r3` then `mr r8,r8`
// here).  That store is dead -- 0x54 is overwritten before any read -- so
// this is a shape residual, not a behaviour difference.
// Why the fix cannot live in this TU: 233 of the 235 instantiated
// _M_insert_overflow_aux symbols in the binary are already 100.0, so the
// shared template text (src/system/stlport/stl/_vector.c, PCH-reached) is
// right and only this element type plus DancerFrame (HamDirector, 98.2301)
// diverge.  Two further source-level differences show up ONLY as ICF-masked
// relocation names and are therefore not scored: the image calls `_M_clear()`
// where we call `_M_clear_after_move()`, and it passes a `const BoneOp *`
// (`__uninitialized_copy<PBUBoneOp,...>`) where we pass a non-const one.  A
// header edit for either has a whole-binary blast radius across all 235.
```


## `?Poll@CharSignalApplier@@UAAXXZ`

`CharSignalApplier::Poll` -- moved verbatim from `src/system/char/CharSignalApplier.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 133-137):

```text
    // w13-c: 96.737 -> 100.  ClampEq (the in-place float specialisation,
    // Min(Max(min, value), max) -- the same expression as Clamp) is what loads
    // mSignalMin before mSignal, keeps the result in f0 and defers the
    // mDoSmoothing lbz past `stfs f0, 0x28`; with `mSignal = Clamp(...)` the
    // w7-al refutations below all held at 96.7 (13 rows).
```

Inside the body (was lines 139-144):

```text
    // NEGATIVE RESULT (w7-al, 2026-09-14): 0x823AB164 is `bne` over a `b`,
    // which looks like `if (mDoSmoothing) A else B`, but spelling it that way
    // costs more than it buys -- it flips the inner `fabs(...) < inc` branch
    // (0x823AB18C `bge`) the wrong way and drops the shared
    // `stfs f13, 0x3c(r28)` tail.  95.7 -> 94.6.  The `!mDoSmoothing` form is
    // kept; the branch-around-branch at 0x823AB164 is an MSVC peephole miss.
```


## `?Handle@CharSignalApplier@@UAA?AVDataNode@@PAVDataArray@@_N@Z`

`CharSignalApplier::Handle` -- moved verbatim from `src/system/char/CharSignalApplier.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Attribution: definition is macro-generated (BEGIN_HANDLERS); symbol matched by the name the macro expands to

Above the definition (was lines 224-235):

```text
// w8-k: the macro form, not a bare `return Hmx::Object::Handle(d, b);`.  The
// target body (CharSignalApplier.s:1155-1275, 0x823AAA20, 436 B) is the full
// BEGIN_HANDLERS dispatch: ?Sym@DataArray@@ at 0x823AAA58 (that is _msg->Sym(1)),
// the MessageTimer::Active() guard + ??0Timer@@/Timer::Restart at 0x823AAA94/A4,
// then TWO HANDLE_FORWARD blocks -- ?Handle@CharWeightable@@ at 0x823AAAB8 and
// ?Handle@Object@Hmx@@ at 0x823AAB10, each followed by the same
// `cmpwi cr6, r11, 6` / DataNode copy-ctor / DataArray::Release sequence
// _HANDLE_CHECKED expands to -- and then the END_HANDLERS tail: PathName,
// MakeString<const char *, Symbol> on "%s unhandled msg: %s", Debug::Notify,
// Timer::SplitMs and MessageTimer::AddTime.  Plain HANDLE_SUPERCLASS, not
// HANDLE_VIRTUAL_SUPERCLASS: there is no ClassName()/StaticClassName() compare
// anywhere in the body.
```


## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/char/CharSignalApplier.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

File-level (was lines 258-263; no definition follows it):

```text
// w15-i1: vector<BoneOp>::_M_insert_overflow_aux sits at 97.85 canonical.  The
// image stores __new_finish to its EH home (`stw r3, 0x54(r31)`) right after the
// first __uninitialized_copy, before the fill_len==1 _Copy_Construct; we only
// store it at the join, i.e. our build treats that window as unable to throw.
// Marking BoneOp::operator= and this __uninitialized_fill_n `inline` (both are
// `f i` in the map; COMDAT selection verified flipped to ANY) is inert.
```

File-level (was lines 300-359; no definition follows it):

```text
// w8-k 2026-09-15: CLOSED, 0.0000 -> 11.34 -> 100.0000.  The w8-c note below is
// kept because its diagnosis of the ARTIFACT is correct and worth preserving, but
// its conclusion has been overtaken twice, so read it with these two corrections:
//
//   1. "the row can only be closed by renaming the address in symbols.txt, which
//      is a config change outside this lane" -- right about the mechanism, and
//      that rename is now DONE.  config/373307D9/symbols.txt binds
//      ?Handle@CharSignalApplier@@UAA?AVDataNode@@PAVDataArray@@_N@Z at
//      0x823AAA20.  Which of the two fold members to name is settled without
//      appealing to symbols.txt by the address-range test: splits.txt gives
//      CharSignalApplier .text [0x823AA888, 0x823ACE28), which contains the
//      address, and CharBoneTwist .text [0x82360180, 0x823616B8), which does not.
//      Nothing is taken from CharBoneTwist, which names no Handle body at all.
//   2. "-- i.e. exactly what our CharSignalApplier::Handle already compiles to."
//      This was WRONG, and it is the clause that made the row look like pure
//      pairing noise with no work behind it.  Our body was
//      `return Hmx::Object::Handle(d, b);`, a bare passthrough; the moment the
//      rename made the row scoreable it read 11.34% with 93 of 109 instructions
//      DELETED.  The target is the BEGIN_HANDLERS macro expansion -- see the note
//      on the definition above.  It now reads 100.0000, 109 of 109 equal.
//
// The general lesson, since this row cost two lanes: an unscoreable row hides
// whether there is source work behind it.  "We already emit the real body" was
// established from `strings` on the object, which proves a symbol of that NAME
// exists -- not that its CONTENTS are right.  Do not promote that to "no work
// here" without a ruler that can actually charge the body.
//
// --- w8-c, as written (diagnosis good, conclusion superseded by the above) ---
// w8-c: `merged_823AAA20` (436 B) holds at 0.0000 and is NOT work.
// It is an unscoreable pairing artifact, not a missing body.
//   - config/373307D9/symbols.txt:121732 binds the address with a synthetic
//     placeholder: `merged_823AAA20 = .text:0x823AAA20; // type:function
//     size:0x1B4 scope:global`. dtk therefore carves the target-side COMDAT
//     under that name, and no mangled C++ name on our side can ever pair
//     with it -- objdiff has nothing to match against.
//   - build/373307D9/icf_aliases.map:11012-11015 names the fold group:
//     0x823AAA20 = `CharBoneTwist::Handle` + `CharSignalApplier::Handle`.
//     /Gy COMDAT folding collapsed the two identical Handle bodies into one
//     address, and the linker map records both members.
//   - We DO emit the real body. `strings build/373307D9/src/system/char/
//     CharSignalApplier.obj` shows
//     `?Handle@CharSignalApplier@@UAA?AVDataNode@@PAVDataArray@@_N@Z`, and
//     the vtable thunk `?Handle@CharSignalApplier@@$4PPPPPPPM@A@AA?AVDataNode
//     @@PAVDataArray@@_N@Z` scores 100.0000 (12 B) in report.json.
//   - Target body: CharSignalApplier.s:1155-1275. Referenced as data at
//     CharSignalApplier.s:670 (`.4byte merged_823AAA20`, the vtable slot) and
//     tail-called at CharSignalApplier.s:3015 (`b merged_823AAA20`). Its
//     shape is the ordinary Handle dispatch: `?Sym@DataArray@@QBA?AVSymbol@@H@Z`
//     at 0x823AAA58, `??0Timer@@QAA@XZ` 0x823AAA94, `?Restart@Timer@@QAAXXZ`
//     0x823AAAA4, `?Handle@CharWeightable@@UAA...` 0x823AAAB8,
//     `?Handle@Object@Hmx@@UAA...` 0x823AAB10, the
//     `"%s unhandled msg: %s"` MakeString at 0x823AAB8C, and
//     `?AddTime@MessageTimer@@KAX...` 0x823AABC4 -- i.e. exactly what our
//     CharSignalApplier::Handle already compiles to.
//   - REFUTED: "CharBoneTwist is the fold winner, so name it there instead."
//     `grep -n "Handle@CharBoneTwist" build/373307D9/asm/system/char/
//     CharBoneTwist.s` returns nothing -- CharBoneTwist's Handle is not named
//     in the listings either. Both members of the group are anonymous; the
//     row can only be closed by renaming the address in symbols.txt, which is
//     a config change outside this lane, not a source fix.
```

## `?_M_insert_overflow_aux@?$vector@UBoneOp@CharSignalApplier@@V?$StlNodeAlloc@UBoneOp@CharSignalApplier@@@stlpmtx_std@@@stlpmtx_std@@IAAXPAUBoneOp@CharSignalApplier@@ABU34@ABU__false_type@2@I_N@Z`

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim; regalloc only.
