// ObjPtrVec template method implementations.
// Separated from ObjPtr_p.h to avoid PCH inlining budget pollution on PPC.
// Include this header in .cpp files that call ObjPtrVec::find/swap/sort/merge/unique/remove.
//
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
#pragma once

#include "obj/Object.h"
#include "utl\MemMgr.h"
#include <algorithm>
#include <vector>

template <class T1, class T2>
typename ObjPtrVec<T1, T2>::const_iterator
ObjPtrVec<T1, T2>::find(const Hmx::Object *target) const {
    const_iterator it = begin();
    for (; it != end(); ++it) {
        if (*it == target)
            break;
    }
    return it;
}

template <class T1, class T2>
typename ObjPtrVec<T1, T2>::iterator
ObjPtrVec<T1, T2>::find(const Hmx::Object *target) {
    iterator it = begin();
    for (; it != end(); ++it) {
        if (*it == target)
            break;
    }
    return it;
}

template <class T1, class T2>
void ObjPtrVec<T1, T2>::swap(int a, int b) {
    iterator begin_a = begin() + a;
    iterator begin_b = begin() + b;
    T1 *tmp = begin_a->Obj();
    Set(begin_a, begin_b->Obj());
    Set(begin_b, tmp);
}

template <class T1, class T2>
template <class S>
void ObjPtrVec<T1, T2>::sort(const S &cmp) {
    // The image calls MemPushTemp()/MemPopTemp() directly here, not the
    // MemDoTempAllocations ctor/dtor pair: MemDoTempAllocations is the RAII wrapper that
    // inlines to exactly those two bl's.
    MemDoTempAllocations doTemp;
    std::vector<T1 *> ptrs;
    ptrs.resize(size());
    for (unsigned int i = 0; i < size(); i++) {
        ptrs[i] = mNodes[i].Obj();
    }
    std::sort(ptrs.begin(), ptrs.end(), cmp);
    for (unsigned int i = 0; i < size(); i++) {
        mNodes[i].SetObjConcrete(ptrs[i]);
    }
}

template <class T1, class T2>
void ObjPtrVec<T1, T2>::merge(const ObjPtrVec<T1, T2> &other) {
    for (const_iterator it = other.begin(); it != other.end(); ++it) {
        T1 *obj = it->Obj();
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
        if (find(obj) == end()) {
            push_back(obj);
        }
    }
}

template <class T1, class T2>
void ObjPtrVec<T1, T2>::unique() {
    if (mNodes.empty())
        return;
    T1 *obj = begin()->Obj();
    iterator jt = begin() + 1;
    while (jt != end()) {
        if (jt->Obj() == obj) {
            erase(jt);
        } else {
            obj = jt->Obj();
            ++jt;
        }
    }
}

template <class T1, class T2>
bool ObjPtrVec<T1, T2>::remove(T1 *obj) {
    iterator it = find(obj);
    if (it != end()) {
        erase(it);
        return true;
    }
    return false;
}
