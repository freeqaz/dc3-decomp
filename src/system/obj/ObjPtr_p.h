#pragma once
#include "obj/Object.h"
#include "obj\Dir.h" /* IWYU pragma: keep */
#include "obj/Object.h"
#include "utl/BinStream.h" /* IWYU pragma: keep */
#include "os\Debug.h" /* IWYU pragma: keep */
#include "utl\PoolAlloc.h"
#include <algorithm> /* IWYU pragma: keep */
#include <cstddef> /* IWYU pragma: keep */

// DO NOT try to include this header directly!
// include obj/Object.h instead

#pragma region ObjRefConcrete
// ------------------------------------------------
// ObjRefConcrete
// ------------------------------------------------

template <class T1, class T2>
ObjRefConcrete<T1, T2>::ObjRefConcrete(T1 *obj) : mObject(obj) {
    if (mObject) {
        mObject->AddRef(this);
#ifdef HX_NATIVE
        RefAudit::Retarget(this, nullptr, static_cast<Hmx::Object *>(mObject));
#endif
    }
}

template <class T1, class T2>
__forceinline ObjRefConcrete<T1, T2>::ObjRefConcrete(const ObjRefConcrete &o)
    : mObject(o.mObject) {
    if (mObject) {
        mObject->AddRef(this);
#ifdef HX_NATIVE
        RefAudit::Retarget(this, nullptr, static_cast<Hmx::Object *>(mObject));
#endif
    }
}

template <class T1, class T2>
ObjRefConcrete<T1, T2>::~ObjRefConcrete() {
#ifdef HX_NATIVE
    RefAudit::Forget(this);
#endif
    if (mObject) {
#ifdef HX_NATIVE
        if (ObjectDir::InDeleteObjects() || Hmx::Object::sRingsDirty) {
            SafeReleaseFromRing(this);
            mObject = nullptr;
            return;
        }
#endif
        mObject->Release(this);
    }
}

template <class T1, class T2>
void ObjRefConcrete<T1, T2>::SetObjConcrete(T1 *obj) {
#ifdef HX_NATIVE
    RefAudit::Retarget(
        this,
        mObject ? static_cast<Hmx::Object *>(mObject) : nullptr,
        obj ? static_cast<Hmx::Object *>(obj) : nullptr
    );
#endif
    if (mObject) {
#ifdef HX_NATIVE
        if (ObjectDir::InDeleteObjects() || Hmx::Object::sRingsDirty) {
            SafeReleaseFromRing(this);
            goto skip_ring_ops;
        }
#endif
        mObject->Release(this);
    }
    mObject = obj;
    if (mObject) {
        mObject->AddRef(this);
    }
    return;
#ifdef HX_NATIVE
skip_ring_ops:
    mObject = obj;
    if (mObject) {
        mObject->AddRef(this);
    }
#endif
}

template <class T1, class T2>
void ObjRefConcrete<T1, T2>::CopyRef(const ObjRefConcrete &o) {
    if (&o == this) return;
#ifdef HX_NATIVE
    RefAudit::Retarget(
        this,
        mObject ? static_cast<Hmx::Object *>(mObject) : nullptr,
        o.mObject ? static_cast<Hmx::Object *>(o.mObject) : nullptr
    );
#endif
    if (mObject) this->ObjRef::Release(nullptr);
    mObject = o.mObject;
    if (!mObject) return;
    this->ObjRef::AddRef(const_cast<ObjRef *>(static_cast<const ObjRef *>(&o)));
}

template <class T1, class T2>
Hmx::Object *ObjRefConcrete<T1, T2>::SetObj(Hmx::Object *root_obj) {
    T1 *obj = root_obj ? dynamic_cast<T1 *>(root_obj) : nullptr;
    SetObjConcrete(obj);
    return mObject;
}

template <class T1>
BinStream &operator<<(BinStream &bs, const ObjRefConcrete<T1, class ObjectDir> &f) {
    MILO_ASSERT(f.RefOwner(), 0x4D1);
    T1 *obj = f;
    const char *objName = obj ? obj->Name() : "";
    bs << objName;
    return bs;
}

template <class T1, class T2>
bool ObjRefConcrete<T1, T2>::Load(BinStream &bs, bool print, ObjectDir *dir) {
    char buf[128];
    bs.ReadString(buf, 128);
    Hmx::Object *refOwner = RefOwner();
    if (!dir && refOwner) {
        dir = refOwner->Dir();
    }
    // No native owner-less resolution and no parent-dir / Main() fallback
    // (removed 2026-09-30): the image (every ObjRefConcrete::Load
    // instantiation, 100% matched) resolves only with an owner, and only in
    // `dir` and its subdirs.  Instrumented over the perform and dance-battle
    // routes, the native fallbacks bound nothing at all; they could only bind
    // what the Xbox leaves null.  Pinned by NativeSuspectsTest.ObjPtr*.
    if (refOwner && dir) {
        SetObj(dir->FindObject(buf, false, true));
        if (!mObject && buf[0] != '\0') {
            if (print) {
                MILO_NOTIFY(
                    "%s couldn't find %s in %s", PathName(refOwner), buf, PathName(dir)
                );
            }
            return false;
        }
    } else {
        if (mObject) {
#ifdef HX_NATIVE
            RefAudit::Retarget(this, static_cast<Hmx::Object *>(mObject), nullptr);
#endif
            Release(this);
        }
        mObject = nullptr;
        if (buf[0] != '\0') {
            if (print)
                MILO_NOTIFY("No dir to find %s", buf);
        }
    }
    return true;
}

#pragma endregion
#pragma region ObjPtr
// ------------------------------------------------
// ObjPtr
// ------------------------------------------------

template <class T>
ObjPtr<T>::ObjPtr(Hmx::Object *owner, T *ptr = nullptr)
    : ObjRefConcrete<T>(ptr), mOwner(owner) {}

template <class T>
ObjPtr<T>::ObjPtr(const ObjPtr &p) : ObjRefConcrete(p), mOwner(p.mOwner) {}

template <class T>
ObjPtr<T>::~ObjPtr() {}

template <class T>
BinStream &operator>>(BinStream &bs, ObjPtr<T> &ptr) {
    ptr.Load(bs, true, nullptr);
    return bs;
}

#pragma endregion
#pragma region ObjOwnerPtr
// ------------------------------------------------
// ObjOwnerPtr
// ------------------------------------------------

template <class T>
ObjOwnerPtr<T>::ObjOwnerPtr(ObjRefOwner *owner, T *ptr)
    : ObjRefConcrete<T>(ptr), mOwner(owner) {
    MILO_ASSERT(owner, 0xC8);
}

template <class T>
ObjOwnerPtr<T>::ObjOwnerPtr(const ObjOwnerPtr &o)
    : ObjRefConcrete(o.mObject), mOwner(o.mOwner) {
    MILO_ASSERT(mOwner, 0xCE);
}

template <class T>
ObjOwnerPtr<T>::~ObjOwnerPtr() {}

template <class T>
Hmx::Object *ObjOwnerPtr<T>::RefOwner() const {
    return mOwner->RefOwner();
}

// template <class T1>
// BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<T1> &ptr);

template <class T1>
BinStream &operator>>(BinStream &bs, ObjOwnerPtr<T1> &ptr) {
    ptr.Load(bs, true, nullptr);
    return bs;
}

#pragma endregion
#pragma region ObjPtrVec
// ------------------------------------------------
// ObjPtrVec
// ------------------------------------------------

template <class T1, class T2>
ObjPtrVec<T1, T2>::ObjPtrVec(Hmx::Object *owner, EraseMode e, ObjListMode o)
    : mOwner(owner), mEraseMode(e), mListMode(o) {
    MILO_ASSERT(owner, 0x321);
}

template <class T1, class T2>
ObjPtrVec<T1, T2>::ObjPtrVec(const ObjPtrVec &other)
    : mOwner(other.mOwner), mEraseMode(other.mEraseMode), mListMode(other.mListMode) {
    *this = other;
}

template <class T1, class T2>
ObjPtrVec<T1, T2>::Node::Node(const Node &n)
    : ObjRefConcrete<T1, T2>(n), mOwner(n.mOwner) {}

template <class T1, class T2>
ObjPtrVec<T1, T2>::~ObjPtrVec() {
#ifdef HX_NATIVE
    ForgetVecCompact(this);
#endif
    mNodes.clear();
}

template <class T1, class T2>
void ObjPtrVec<T1, T2>::ReplaceNode(Node *n, Hmx::Object *obj) {
    if (mListMode == kObjListOwnerControl) {
        mOwner->Replace(n, obj);
    } else {
        Hmx::Object *oldObj = n->SetObj(obj);
        if (!oldObj && mListMode == kObjListNoNull) {
#ifdef HX_NATIVE
            // During ReplaceList, erasing from the vector shifts subsequent
            // nodes via CopyRef, which modifies ring prev/next pointers and
            // corrupts the ring walk. In ReplaceRefs' snapshot walk the erase
            // is DEFERRED to the end of the outermost walk (see
            // DeferVecCompact); in a live ReplaceList walk it is suppressed.
            if (!gInReplaceList) {
                erase(iterator(mNodes.begin() + (n - mNodes.data())));
            } else if (gInRefSnapshot) {
                DeferVecCompact(this, &ObjPtrVec::CompactNulls);
            } else {
                MILO_WARN("ReplaceNode: suppressed erase during ReplaceList (owner=%s)",
                    mOwner ? PathName(mOwner) : "<null>");
            }
#else
            erase(n);
#endif
        }
#ifdef HX_NATIVE
        else if (obj && mListMode == kObjListNoNull) {
            // MergeObject redirects refs from o1→o2. If vec had refs to both,
            // both Nodes now point to o2 (duplicate). Erase the duplicate to
            // prevent stale-pointer dereference during later destruction.
            T1 *typed = dynamic_cast<T1 *>(obj);
            if (typed) {
                for (size_t i = 0; i < mNodes.size(); i++) {
                    if (&mNodes[i] != n && mNodes[i].Obj() == typed) {
                        n->SetObj(nullptr);
                        if (!gInReplaceList)
                            erase(iterator(mNodes.begin() + (n - mNodes.data())));
                        break;
                    }
                }
            }
        }
#endif
    }
}

template <class T1, class T2>
void ObjPtrVec<T1, T2>::Set(iterator it, T1 *obj) {
    if (!obj && mListMode == 0) {
        erase(it);
    } else
        it->SetObjConcrete(obj);
}

// see Draw.cpp for this
template <class T1, class T2>
void ObjPtrVec<T1, T2>::operator=(const ObjPtrVec &other) {
    if (this == &other) return;
    mNodes.clear();
    mNodes.reserve(other.mNodes.size());
    for (const_iterator it = other.begin(); it != other.end(); ++it) {
        // Node(this) must NOT go through ObjRefConcrete(T1 *), even with a
        // literal nullptr: see the protected default ctor in obj/Object.h.
        //
        // Why (w12-o, 2026-10-02): this compiler does intra-TU interprocedural
        // escape analysis. It knows push_back(const Node &) only READS its
        // argument, so as long as newNode never escapes, neither push_back nor
        // Set can modify it, the ctor's mObject = 0 reaches the destructor, and
        // ~ObjRefConcrete's `if (mObject) Release` folds away -- leaving only
        // the base-vptr reset, which is exactly the image. But the escape
        // analysis evidently runs before the ctor's dead `if (mObject) AddRef(this)`
        // branch is folded, and AddRef stores `this` into the referent's ring,
        // so ObjRefConcrete(nullptr) marks newNode escaped and the check (plus
        // the ring unlink, rows 69-77, 87.83784%) comes back. Scratch-TU proof
        // of the IPA: a callee that only reads `const S &` lets a later
        // `if (s.a)` fold; a callee that passes &s to an extern does not.
        //
        // Measured negatives under the old ctor (all 13 instantiations):
        //   `const Node newNode(this);`      byte-inert, 87.83784
        //   `mNodes.push_back(Node(this));`  85.09460
        //   explicit `mObject = 0` after Set 98.64865 (adds a store the image
        //       lacks; it was the clue that the image knew mObject was null)
        Node newNode(this);
        mNodes.push_back(newNode);
        Set(--end(), *it);
    }
}

template <class T1, class T2>
void ObjPtrVec<T1, T2>::push_back(T1 *obj) {
    insert(end(), obj);
}

template <class T1, class T2>
typename ObjPtrVec<T1, T2>::iterator
ObjPtrVec<T1, T2>::insert(typename ObjPtrVec<T1, T2>::const_iterator it, T1 *obj) {
    if (obj != 0 || mListMode != kObjListNoNull) {
#ifdef HX_NATIVE
        int idx = it.it - mNodes.begin();
#else
        // MSVC iterators can be null (zero-initialized); GCC iterators cannot.
        int idx = it.it ? (it.it - mNodes.begin()) : 0;
#endif
        Node newNode(this);
        typename std::vector<Node>::iterator pos = mNodes.begin() + idx;
        mNodes.insert(pos, 1, newNode);
        Set(begin() + idx, obj);
    }
#ifdef HX_NATIVE
    return iterator(mNodes.begin() + (it.it - mNodes.cbegin()));
#else
    return iterator(const_cast<typename std::vector<Node>::iterator>(it.it));
#endif
}

template <class T1, class T2>
bool ObjPtrVec<T1, T2>::Load(BinStream &bs, bool print, ObjectDir *dir) {
    bool ret = true;
    mNodes.clear();
    int count;
    bs >> count;
    mNodes.reserve(count);
    if (!dir && mOwner) {
        dir = mOwner->Dir();
    }
    if (print) {
        MILO_ASSERT(dir, 0x488);
    }
    while (count != 0U) {
        char buf[0x80];
        bs.ReadString(buf, 0x80);
        if (dir) {
            T1 *casted = dynamic_cast<T1 *>(dir->FindObject(buf, false, true));
            if (!casted && buf[0] != '\0') {
                if (print)
                    MILO_NOTIFY(
                        "%s couldn't find %s in %s", PathName(mOwner), buf, PathName(dir)
                    );
                ret = false;
            } else if (casted || mListMode != kObjListNoNull) {
                push_back(casted);
            }
        }
        count--;
    }
    return ret;
}

template <class T1>
BinStream &operator<<(BinStream &bs, const ObjPtrVec<T1, ObjectDir> &c) {
    bs << c.size();
    MILO_ASSERT(c.Owner(), 0x525);
    // Read the object pointer off the Node rather than letting `*it` convert:
    // the conversion makes MSVC strength-reduce the induction variable to
    // &node->mObject and then re-derive the node with a subi to compare against
    // end().  The image keeps the Node itself in the induction register and
    // loads 0xc(node), and tests it unsigned (cmplwi, i.e. a pointer) rather
    // than signed.
    for (typename ObjPtrVec<T1, ObjectDir>::const_iterator it = c.begin();
         it != c.end();
         ++it) {
        const T1 *obj = it->Obj();
        if (obj) {
            bs << obj->Name();
        } else {
            bs << "";
        }
    }
    return bs;
}

template <class T1>
BinStream &operator>>(BinStream &bs, ObjPtrVec<T1, ObjectDir> &vec) {
    vec.Load(bs, true, nullptr);
    return bs;
}

// ObjPtrVec::find, ::swap, ::sort bodies are in ObjPtrVec_impl.h
// to keep them out of the PCH (inlining budget pollution).
// Include ObjPtrVec_impl.h in .cpp files that call these methods.
#ifdef HX_NATIVE
#include "obj\ObjPtrVec_impl.h"
#endif

#pragma endregion
#pragma region ObjPtrList
// ------------------------------------------------
// ObjPtrList
// ------------------------------------------------

template <class T1, class T2>
ObjPtrList<T1, T2>::ObjPtrList(ObjRefOwner *owner, ObjListMode mode)
    : mSize(0), mNodes(nullptr), mOwner(owner), mListMode(mode) {
    if (mode == kObjListOwnerControl) {
        MILO_ASSERT(owner, 0x103);
    }
}

template <class T1, class T2>
ObjPtrList<T1, T2>::ObjPtrList(const ObjPtrList &other)
    : mSize(0), mNodes(nullptr), mOwner(other.mOwner), mListMode(other.mListMode) {
    for (iterator it = other.begin(); it != other.end(); ++it) {
        push_back(*it);
    }
}

template <class T1, class T2>
#ifdef HX_NATIVE
void *ObjPtrList<T1, T2>::Node::operator new(size_t s) {
#else
void *ObjPtrList<T1, T2>::Node::operator new(unsigned int s) {
#endif
    return PoolAlloc(s, s, __FILE__, 0x122, "ObjPtrList_node");
}

template <class T1, class T2>
void ObjPtrList<T1, T2>::Node::operator delete(void *v) {
    PoolFree(sizeof(Node), v, __FILE__, 0x122, "ObjPtrList_node");
}

template <class T1, class T2>
void ObjPtrList<T1, T2>::ReplaceNode(struct ObjPtrList::Node *node, Hmx::Object *obj) {
    if (mListMode == kObjListOwnerControl) {
        mOwner->Replace(static_cast<ObjRef *>(static_cast<ObjRefConcrete<T1, T2> *>(node)), obj);
    } else {
        Hmx::Object *old = static_cast<ObjRefConcrete<T1, T2> *>(node)->SetObj(obj);
        if (!old && mListMode == kObjListNoNull) {
#ifdef HX_NATIVE
            // During ReplaceList, erasing frees the node while other ObjRefs in
            // the ring still hold prev/next pointers to it. Subsequent ring
            // operations write through dangling pointers and corrupt glibc's
            // heap metadata ("corrupted double-linked list"). Suppress the erase;
            // the null entry persists until the list is destroyed or cleaned up.
            // Matches the guard in ObjPtrVec::ReplaceNode.
            //
            // EXCEPT in ReplaceRefs' snapshot walk (gInRefSnapshot), where the
            // hazard above does not exist -- and where suppressing is itself
            // a crash: a kObjListNoNull list is left holding a NULL its owner
            // was promised it can never hold.  Measured 2026-09-30 by the
            // native harvest: leaving a song for song select,
            // ~FaderGroup popped exactly such a NULL Fader out of mFaders and
            // called RemoveClient on it -> SIGSEGV at 0xf0 (DirUnloader ->
            // ~MoggClip -> ~StandardStream -> ~FaderGroup), immediately after
            // this warning fired with owner=<null>.  The image erases
            // unconditionally.
            if (!gInReplaceList || gInRefSnapshot) {
                erase(node);
            } else {
                MILO_WARN("ObjPtrList::ReplaceNode: suppressed erase during ReplaceList (owner=%s)",
                    mOwner ? PathName(dynamic_cast<Hmx::Object*>(mOwner)) : "<null>");
            }
#else
            erase(node);
#endif
        }
    }
}

template <class T1, class T2>
void ObjPtrList<T1, T2>::operator=(const ObjPtrList &other) {
    if (this == &other)
        return;
    while (mSize > other.mSize)
        pop_back();
    Node *otherNodes = other.mNodes;
    for (Node *n = mNodes; n != nullptr; n = n->next, otherNodes = otherNodes->next) {
        *n = *otherNodes;
    }
    for (; otherNodes != nullptr; otherNodes = otherNodes->next) {
        push_back(otherNodes->Obj());
    }
}

#ifndef HX_NATIVE
template <class T1, class T2>
T1 *ObjPtrList<T1, T2>::front() const {
    MILO_ASSERT(mNodes != NULL, 0x189);
    return mNodes->Obj();
}

template <class T1, class T2>
T1 *ObjPtrList<T1, T2>::back() const {
    MILO_ASSERT(mNodes != NULL, 0x18A);
    return mNodes->prev->Obj();
}
#endif

template <class T1, class T2>
void ObjPtrList<T1, T2>::pop_back() {
    MILO_ASSERT(mNodes != NULL, 0x18B);
    erase(mNodes->prev);
}

template <class T1, class T2>
void ObjPtrList<T1, T2>::pop_front() {
    MILO_ASSERT(mNodes != NULL, 0x18C);
    erase(mNodes);
}

template <class T1, class T2>
void ObjPtrList<T1, T2>::push_back(T1 *obj) {
    insert(end(), obj);
}

template <class T1, class T2>
typename ObjPtrList<T1, T2>::iterator
ObjPtrList<T1, T2>::insert(typename ObjPtrList<T1, T2>::iterator it, T1 *obj) {
    if (mListMode == kObjListNoNull) {
#ifdef HX_NATIVE
        // NullifyAllRefs can nullify ObjPtrList entries during async unload
        // or cascade Phase 0. Skip inserting null into no-null lists.
        if (!obj)
            return end();
#endif
        MILO_ASSERT(obj, 0x177);
    }
    Node *node = new Node();
    node->SetObjConcrete(obj);
    Link(it, node);
    return node;
}

template <class T1, class T2>
void ObjPtrList<T1, T2>::Set(iterator it, T1 *obj) {
    it.mNode->SetObjConcrete(obj);
}

template <class T1, class T2>
inline typename ObjPtrList<T1, T2>::iterator
ObjPtrList<T1, T2>::find(const Hmx::Object *target) const {
    for (iterator it = begin(); it != end(); ++it) {
        if (*it == target)
            return it;
    }
    return end();
}

// TODO: not 100%, work on this
// addr: 0x825C6868
template <class T1, class T2>
bool ObjPtrList<T1, T2>::remove(T1 *target) {
    for (iterator it = begin(); it != end();) {
        auto old = it++;
        if (*old == target) {
            erase(old);
            return true;
        }
    }
    return false;
}

// remove a particular item inside iterator otherIt, from list otherList,
// and insert it into this list at the position indicated by thisIt
template <class T1, class T2>
void ObjPtrList<T1, T2>::MoveItem(
    iterator thisIt, ObjPtrList<T1, T2> &otherList, iterator otherIt
) {
    if (otherIt != thisIt) {
        otherList.Unlink(otherIt.mNode);
        Link(thisIt, otherIt.mNode);
    }
}

template <class T1, class T2>
bool ObjPtrList<T1, T2>::Load(BinStream &bs, bool print, ObjectDir *dir, bool b4) {
    bool ret = true;
    clear();
    int count;
    bs >> count;
    Hmx::Object *refOwner = mOwner ? mOwner->RefOwner() : nullptr;
    if (!dir && refOwner)
        dir = refOwner->Dir();
    if (print) {
        MILO_ASSERT(dir, 0x210);
    }
    while (count != 0U) {
        char buf[0x80];
        bs.ReadString(buf, 0x80);
        if (dir) {
            T1 *casted = dynamic_cast<T1 *>(dir->FindObject(buf, false, b4));
            if (!casted && buf[0] != '\0') {
                if (print)
                    MILO_NOTIFY(
                        "%s couldn't find %s in %s", PathName(refOwner), buf, PathName(dir)
                    );
                ret = false;
            } else if (casted) {
                push_back(casted);
            }
        }
        count--;
    }
    return ret;
}

template <class T1>
BinStream &operator<<(BinStream &bs, const ObjPtrList<T1, ObjectDir> &c) {
    bs << c.size();
    MILO_ASSERT(c.Owner(), 0x4E1);
    FOREACH (it, c) {
        if (*it) {
            bs << (*it)->Name();
        } else {
            bs << "";
        }
    }
    return bs;
}

template <class T1>
BinStream &operator>>(BinStream &bs, ObjPtrList<T1, ObjectDir> &list) {
    list.Load(bs, true, nullptr, true);
    return bs;
}

template <class T1, class T2>
template <class S>
void ObjPtrList<T1, T2>::sort(const S &cmp) {
    if (mNodes && mNodes->next) {
#ifdef HX_NATIVE
        // Native Link() creates NULL-terminated forward links (last->next = nullptr).
        // PPC Link() creates circular links (last->next = sentinel).
        Node *sentinel = mNodes;
        for (Node *outer = sentinel->next; outer != nullptr; outer = outer->next) {
            for (Node *inner = outer; inner != sentinel; inner = inner->prev) {
                Node *prev = inner->prev;
                if (cmp(inner->Obj(), prev->Obj())) {
                    T1 *tmp = inner->Obj();
                    inner->SetObjConcrete(prev->Obj());
                    prev->SetObjConcrete(tmp);
                } else {
                    break;
                }
            }
        }
#else
        Node *last = mNodes->prev;
        for (Node *n = last->prev; n != last; n = n->prev) {
            for (Node *x = n; x != last; x = x->next) {
                Node *nextX = x->next;
                if (cmp(nextX->Obj(), x->Obj())) {
                    T1 *tmp = x->Obj();
                    x->SetObjConcrete(nextX->Obj());
                    nextX->SetObjConcrete(tmp);
                } else {
                    break;
                }
            }
        }
#endif
    }
}

#ifndef HX_NATIVE
// Xbox template implementations.
// These were originally explicit specializations in link_glue.cpp for each type.
// Providing generic template versions so the compiler can instantiate them.

template <class T1, class T2>
void ObjPtrList<T1, T2>::Link(iterator it, Node *node) {
    node->mOwner = this;
    node->next = it.mNode;
    if (it.mNode == mNodes) {
        if (mNodes) {
            node->prev = mNodes->prev;
            mNodes->prev = node;
        } else {
            node->prev = node;
        }
        mNodes = node;
    } else if (it.mNode == nullptr) {
        // Insert at end
        node->prev = mNodes->prev;
        mNodes->prev->next = node;
        mNodes->prev = node;
    } else {
        // Insert before a middle node
        node->prev = it.mNode->prev;
        it.mNode->prev->next = node;
        it.mNode->prev = node;
    }
    mSize++;
}

template <class T1, class T2>
typename ObjPtrList<T1, T2>::Node *ObjPtrList<T1, T2>::Unlink(Node *n) {
    MILO_ASSERT(n != NULL && mNodes != NULL, 0x26B);
    Node *next;
    if (n == mNodes) {
        // Removing head: the successor (or NULL) becomes the new head, and is
        // also what we return. The target keeps a single `mSize--` and a single
        // return for all three arms, so do not early-return from any of them.
        if (mNodes->next != nullptr) {
            mNodes->next->prev = mNodes->prev;
            mNodes = mNodes->next;
        } else {
            mNodes = nullptr;
        }
        next = mNodes;
    } else if (n == mNodes->prev) {
        // Removing tail
        mNodes->prev = mNodes->prev->prev;
        mNodes->prev->next = nullptr;
        next = mNodes->prev;
    } else {
        // Middle node. The parameter is `n`: the retail assert string is
        // "n != NULL && mNodes != NULL" (??_C@_0BM@NPEFHGCK, UIList.obj).
        n->prev->next = n->next;
        n->next->prev = n->prev;
        next = n->next;
    }
    mSize--;
    return next;
}

template <class T1, class T2>
typename ObjPtrList<T1, T2>::iterator ObjPtrList<T1, T2>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

// ObjPtrList<T>::RefOwner / ::Replace are NOT defined here on Xbox: retail
// folds them into ObjPtrVec's "should never be called" stub (see Object.h).

template <class T1, class T2>
Hmx::Object *ObjPtrList<T1, T2>::Node::RefOwner() const {
    ObjPtrList<T1, T2> *list = static_cast<ObjPtrList<T1, T2> *>(mOwner);
    return list->Owner();
}

// -- ObjPtrVec Xbox template implementations --
// w24-pch (B4, open): whether erase<T> inlines Set<T> follows COMDAT compile
// order per TU (w21-d rule; image inlines for Font RndMat, LightPreset
// Spotlight, CharClipGroup; calls for FlowManager FlowNode and LightPreset
// E/L/SD).  Defining erase BEFORE Set in this header is inert on all of them
// (per-object probe: erase<Spotlight> 83.12, erase<FlowNode> 80.58, control
// erase<RndMat> 100, unchanged).  Definition order is not the input; the open
// question is what reference makes the front end queue Set<T> ahead of erase<T>.

template <class T1, class T2>
typename ObjPtrVec<T1, T2>::iterator
ObjPtrVec<T1, T2>::erase(typename ObjPtrVec<T1, T2>::iterator it) {
    int idx = it.it ? (it.it - mNodes.begin()) : 0;
    if (mEraseMode == kEraseSwapLast) {
        unsigned int last = mNodes.size() - 1;
        if ((unsigned int)idx != last) {
            T1 *lastObj = mNodes.back().Obj();
            mNodes.pop_back();
            Set(begin() + idx, lastObj);
            return it;
        }
    }
    mNodes.erase(mNodes.begin() + idx);
    return it;
}

template <class T1, class T2>
typename ObjPtrVec<T1, T2>::iterator ObjPtrVec<T1, T2>::FindRef(ObjRef *ref) {
    // A Node's Parent() *is* the ObjPtrVec that owns it, so the linear scan the
    // decomp used to do is unnecessary: ask the ref who owns it.
    if (ref->Parent() == this) {
        return iterator(static_cast<Node *>(ref));
    }
    return end();
}

#endif // !HX_NATIVE

#ifdef HX_NATIVE
// Generic template implementations for native port.
// On Xbox, these were explicit specializations in link_glue.cpp for each type.
// On native, we provide generic versions so all ObjPtrList types work.

// ObjPtrList uses a semi-circular doubly-linked list:
//   - mNodes->prev always points to the TAIL node (O(1) pop_back)
//   - Forward links (next) are NULL-terminated (last->next = nullptr)
//   - Non-head nodes have regular prev links to their predecessor
//   - Single node: prev = self (it IS the tail)

template <class T1, class T2>
void ObjPtrList<T1, T2>::Link(iterator it, Node *node) {
    node->mOwner = this;
    Node *pos = it.mNode;
    if (mNodes == nullptr) {
        // First node: self-referencing prev (head->prev = tail = self)
        node->next = nullptr;
        node->prev = node;
        mNodes = node;
    } else if (pos == nullptr) {
        // Insert at end (push_back): append after current tail
        Node *tail = mNodes->prev;
        tail->next = node;
        node->prev = tail;
        node->next = nullptr;
        mNodes->prev = node; // update head's tail pointer
    } else if (pos == mNodes) {
        // Insert before head: new node becomes head
        node->next = mNodes;
        node->prev = mNodes->prev; // inherit tail pointer
        mNodes->prev = node; // old head's prev = new predecessor
        mNodes = node;
    } else {
        // Insert before a middle/tail node
        node->next = pos;
        node->prev = pos->prev;
        pos->prev->next = node;
        pos->prev = node;
    }
    mSize++;
}

template <class T1, class T2>
typename ObjPtrList<T1, T2>::Node *ObjPtrList<T1, T2>::Unlink(Node *node) {
    Node *next = node->next;
    Node *prev = node->prev;
    if (mNodes == node) {
        // Removing head
        mNodes = next;
        if (mNodes) {
            // New head inherits the tail pointer
            // (prev = old head's prev = tail, which is still valid)
            mNodes->prev = prev;
        }
    } else {
        // Removing non-head node
        prev->next = next;
        if (next) {
            next->prev = prev;
        }
        // If removing the tail, update head's tail pointer -- and return the
        // NEW TAIL, not null: the image's Unlink returns mNodes->prev on this
        // arm (UIList.s ?Unlink@?$ObjPtrList@VEventTrigger 8278B7EC
        // lwz r3,0x18(r11)), so erase(tail) lands on the predecessor.
        if (mNodes->prev == node) {
            mNodes->prev = prev;
            next = prev;
        }
    }
    mSize--;
    return next;
}

template <class T1, class T2>
typename ObjPtrList<T1, T2>::iterator ObjPtrList<T1, T2>::erase(iterator it) {
    Node *node = it.mNode;
    Node *next = Unlink(node);
    delete node;
    return iterator(next);
}

template <class T1, class T2>
Hmx::Object *ObjPtrList<T1, T2>::RefOwner() const {
    return mOwner ? mOwner->RefOwner() : 0;
}

template <class T1, class T2>
bool ObjPtrList<T1, T2>::Replace(ObjRef *ref, Hmx::Object *obj) {
    for (iterator it = begin(); it != end(); ++it) {
        if (it.mNode == ref) {
            ReplaceNode(it.mNode, obj);
            return true;
        }
    }
    return false;
}

template <class T1, class T2>
Hmx::Object *ObjPtrList<T1, T2>::Node::RefOwner() const {
    ObjPtrList<T1, T2> *list = static_cast<ObjPtrList<T1, T2> *>(mOwner);
    return list->Owner();
}

template <class T1, class T2>
T1 *ObjPtrList<T1, T2>::front() const {
    return mNodes ? mNodes->Obj() : 0;
}

template <class T1, class T2>
T1 *ObjPtrList<T1, T2>::back() const {
    return mNodes ? mNodes->prev->Obj() : 0;
}

// -- ObjPtrVec generic template implementations --
// NOTE: Node::RefOwner is inline-defined in Object.h (line 318) for both
// Xbox and native; the BinStream operator<<'s for ObjPtrList/ObjPtrVec are
// defined unconditionally earlier in this file. Do NOT redefine them here
// or native builds will fail with redefinition errors.

template <class T1, class T2>
typename ObjPtrVec<T1, T2>::iterator
ObjPtrVec<T1, T2>::erase(ObjPtrVec<T1, T2>::iterator it) {
    // Same semantics as the Xbox erase above. This used to be a bare
    // `mNodes.erase(it.it)` that ignored mEraseMode, so on native every
    // ObjPtrVec shifted on erase -- but the image, for a vector built with
    // kEraseSwapLast, moves the LAST element into the hole and pops instead
    // (FlowManager.s, ObjPtrVec<FlowNode>::erase: `lwz r10,0x14(r30);
    // cmpwi cr6,r10,1`, then pop_back and Set(begin()+idx, last)). The four
    // swap-last owners -- CharClipGroup::mClips, HamWardrobe::mMainCharacters,
    // FlowMultiSetProperty::mTargets, Waypoint::mConnections -- therefore
    // ended up in a different ORDER than the shipped game after any erase in
    // the middle, including the one an object deletion triggers.
    int idx = it.it - mNodes.begin();
    if (mEraseMode == kEraseSwapLast) {
        unsigned int last = mNodes.size() - 1;
        if ((unsigned int)idx != last) {
            T1 *lastObj = mNodes.back().Obj();
            mNodes.pop_back();
            Set(begin() + idx, lastObj);
            return begin() + idx;
        }
    }
    return iterator(mNodes.erase(it.it));
}

template <class T1, class T2>
typename ObjPtrVec<T1, T2>::iterator ObjPtrVec<T1, T2>::FindRef(ObjRef *ref) {
    for (iterator it = begin(); it != end(); ++it) {
        if (&(*it) == ref) return it;
    }
    return end();
}

// ObjPtrVec::merge, ::unique, ::remove bodies are in ObjPtrVec_impl.h
#include "obj\ObjPtrVec_impl.h"

#endif
