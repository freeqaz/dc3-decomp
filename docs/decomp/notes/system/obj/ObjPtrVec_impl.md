# Lane notes: `src/system/obj/ObjPtrVec_impl.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/obj/ObjPtrVec_impl.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

File-level (was lines 5-39; nearest following definition: `ObjPtrVec<T1,T2>::find`):

```text
// ============================================================================
// w48-a (2026-11-06): THIS HEADER IS **NOT** IN THE PCH CLOSURE.  It is safe to
// edit, and a normal `ninja` rebuilds every TU that uses it.  This is measured,
// not inferred, because the lane template's do-not-edit list says otherwise:
//   * `src/system/decomp_pch.h` is exactly `obj/Object.h` + `os\Debug.h`.
//   * `ninja -t deps build/373307D9/pch/decomp_pch.obj` lists 180 header deps
//     and ZERO occurrences of `ObjPtrVec_impl` (it does list `ObjPtr_p.h` and
//     `Object.h`).
//   * `ObjPtr_p.h` mentions this file twice and BOTH are `HX_NATIVE`-only:
//     line 429 (inside the `#ifdef HX_NATIVE` opened at 428) and line 975
//     (inside the block opened at 818 and closed at 977).  The PPC build sees
//     neither, which is why the dep list is empty of it.
//   * On PPC it arrives only through explicit includes in eight files:
//     char/Character.cpp:3, char/CharClipGroup.cpp:4, char/CharClipSet.cpp:5,
//     char/ClipCollide.cpp:3, flow/FlowManager.cpp:3,
//     flow/FlowMultiSetProperty.cpp:3, flow/FlowSlider.cpp:8,
//     world/LightPreset.cpp.
// ============================================================================
// The `ObjPtrVec<RndDrawable, ObjectDir>::merge` row (unit
// default/system/char/Character) reads 83.74138 canonical / 81.41379 fuzzy.
// EQUIVALENCE CERTIFICATE -- no behaviour defect: every load and store in the
// image has a counterpart at the same field; the one target-only load re-reads a
// value we keep live in a register.
// MECHANISM, so the next lane does not re-derive it: our body is ONE
// INSTRUCTION SHORT (228 B / 57 instructions vs the image's 232 B / 58).  The
// image RELOADS `_M_start` in begin()'s non-empty arm (`lwz r10,0x4(r29)` after
// the `beq`); we reuse the load that already fed size()/empty()
// (`clrrwi r31,r10,0`), so begin() AND size() stay live across `bl find`, which
// costs a callee-saved GPR and a whole frame class: `stwu 1,-144(1)` in the
// image vs `stwu 1,-160(1)` ours.  The rest is MSVC strength-reducing the loop
// induction variable onto the node's object field (`addi r27,r27,20` on both,
// plus one `slwi` we emit and the image does not).
// Two spellings are ALREADY MEASURED and must not be re-tried: hoisting the
// iterator as `iterator e = end();` materialises it on the stack and costs
// 83.7 -> 60.7, and `find(obj) == begin() + size()` is byte-identical.
```


## `?merge@?$ObjPtrVec@VRndDrawable@@VObjectDir@@@@QAAXABV1@@Z`

`ObjPtrVec<T1,T2>::merge` -- moved verbatim from `src/system/obj/ObjPtrVec_impl.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Inside the body (was lines 100-123):

```text
        // No `obj != NULL &&` guard here: the image calls find() unconditionally
        // (0x82358B9C falls into `bl find` from BOTH sides of the null test, which
        // is only the vbase adjustment MSVC emits for T1* -> Hmx::Object*). A null
        // entry is harmless because insert() drops a null in kObjListNoNull mode.
        // NEGATIVE RESULT (w7-al, 2026-09-14): 83.7 canonical, and 20 of the 34
        // rows are one callee-saved renumbering caused by ONE extra register.
        // The image holds the whole `end()` in r31 across the `bl find`
        // (0x...  `mulli r11, r11, 0x14` / `add r31, r11, r10` both BEFORE the
        // call, six callee-saved regs, `bl __savegprlr_26`); we keep size() in
        // r30 and begin() in r31 and sink the `mulli`/`add` past the call, which
        // costs a seventh register (`__savegprlr_25`) and 0x10 more frame.  That
        // is a scheduler choice, not a source one: hoisting it into a named
        // `iterator e = end();` materialises the iterator on the stack and costs
        // 83.7 -> 60.7, and writing the test as `end() == find(obj)` is
        // byte-inert.  The remaining two rows are the image biasing the loop
        // induction variable to the node's object field (`addi r27, r11, 0xc`,
        // un-biased again with `subi r10, r27, 0xc` for the end compare), which
        // is MSVC strength reduction with no source lever.
        // w21-h (83.7, unchanged): `find(obj) == begin() + size()` spelled out is
        // byte-identical.  The extra register is a CSE difference, not a
        // scheduling one: in the image the non-empty arm of begin() RELOADS
        // _M_start (`lwz r10, 0x4(r29)` after the `beq`), while we reuse the load
        // that fed size()/empty() (`clrrwi r31, r10, 0`), so begin and size both
        // stay live across `bl find`.  Same family as CharClipGroup::HasClip.
```

w-low-world (lane B, 2026-11-13): re-audited against the listing at 0x82358B0C:
NOT A BUG (same loop, unconditional find(), push_back on a miss).  Re-measured,
not new: `end() == find(obj)` is byte-inert, 83.74 unchanged -- do not retry.
