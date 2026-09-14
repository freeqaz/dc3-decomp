// ObjPtrVec template method implementations.
// Separated from ObjPtr_p.h to avoid PCH inlining budget pollution on PPC.
// Include this header in .cpp files that call ObjPtrVec::find/swap/sort/merge/unique/remove.
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
