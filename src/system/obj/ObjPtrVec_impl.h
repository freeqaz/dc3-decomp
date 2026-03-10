// ObjPtrVec template method implementations.
// Separated from ObjPtr_p.h to avoid PCH inlining budget pollution on PPC.
// Include this header in .cpp files that call ObjPtrVec::find/swap/sort/merge/unique/remove.
#pragma once

#include "obj/Object.h"
#include "utl/MemMgr.h"
#include <algorithm>
#include <vector>

template <class T1, class T2>
typename ObjPtrVec<T1, T2>::const_iterator
ObjPtrVec<T1, T2>::find(const Hmx::Object *target) const {
    for (const_iterator it = begin(); it != end(); ++it) {
        if (*it == target)
            return it;
    }
    return end();
}

template <class T1, class T2>
typename ObjPtrVec<T1, T2>::iterator
ObjPtrVec<T1, T2>::find(const Hmx::Object *target) {
    for (iterator it = begin(); it != end(); ++it) {
        if (*it == target)
            return it;
    }
    return end();
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
    MemPushTemp();
    {
        std::vector<T1 *> ptrs;
        ptrs.insert(ptrs.begin(), size(), (T1 *)0);
        for (unsigned int i = 0; i < size(); i++) {
            ptrs[i] = mNodes[i].Obj();
        }
        std::sort(ptrs.begin(), ptrs.end(), cmp);
        for (unsigned int i = 0; i < size(); i++) {
            mNodes[i].SetObjConcrete(ptrs[i]);
        }
    }
    MemPopTemp();
}

template <class T1, class T2>
void ObjPtrVec<T1, T2>::merge(const ObjPtrVec<T1, T2> &other) {
    for (const_iterator it = other.begin(); it != other.end(); ++it) {
        T1 *obj = it->Obj();
        if (obj && find(obj) == end()) {
            push_back(obj);
        }
    }
}

template <class T1, class T2>
void ObjPtrVec<T1, T2>::unique() {
    for (int i = 0; i < (int)mNodes.size(); i++) {
        for (int j = i + 1; j < (int)mNodes.size(); j++) {
            if (mNodes[i].Obj() == mNodes[j].Obj()) {
                erase(iterator(mNodes.begin() + j));
                j--;
            }
        }
    }
}

template <class T1, class T2>
bool ObjPtrVec<T1, T2>::remove(T1 *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it->Obj() == obj) {
            erase(it);
            return true;
        }
    }
    return false;
}
