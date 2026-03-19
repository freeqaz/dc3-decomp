#pragma once
#include "Object.h"
#include "obj/Dir.h" /* IWYU pragma: keep */
#include "obj/Object.h"
#include "utl/BinStream.h" /* IWYU pragma: keep */
#include "os/Debug.h" /* IWYU pragma: keep */
#include "utl/PoolAlloc.h"
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
    if (mObject)
        mObject->AddRef(this);
}

template <class T1, class T2>
__forceinline ObjRefConcrete<T1, T2>::ObjRefConcrete(const ObjRefConcrete &o)
    : mObject(o.mObject) {
    if (mObject)
        mObject->AddRef(this);
}

template <class T1, class T2>
ObjRefConcrete<T1, T2>::~ObjRefConcrete() {
    if (mObject) {
#ifdef HX_NATIVE
        // During cascading ObjectDir destruction, ring nodes are freed in
        // unpredictable order. Calling Release writes to ring neighbors that
        // may already be freed. Skip all ring operations during the cascade.
        if (ObjectDir::InDeleteObjects()) {
            mObject = nullptr;
            return;
        }
#endif
        mObject->Release(this);
    }
}

template <class T1, class T2>
void ObjRefConcrete<T1, T2>::SetObjConcrete(T1 *obj) {
    if (mObject) {
#ifdef HX_NATIVE
        if (ObjectDir::InDeleteObjects())
            goto skip_release;
#endif
        mObject->Release(this);
    }
    mObject = obj;
    if (mObject) {
        mObject->AddRef(this);
    }
    return;
#ifdef HX_NATIVE
skip_release:
    mObject = obj;
#endif
}

template <class T1, class T2>
Hmx::Object *ObjRefConcrete<T1, T2>::SetObj(Hmx::Object *root_obj) {
    T1 *obj = root_obj ? dynamic_cast<T1 *>(root_obj) : nullptr;
    SetObjConcrete(obj);
    return mObject ? mObject : nullptr;
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
    if (refOwner && dir) {
        SetObj(dir->FindObject(buf, false, true));
#ifdef HX_NATIVE
        // Native fallback: walk up the parent dir chain when not found locally.
        // On Xbox, FileMerger flattens all objects into the same scope.
        // On native, the merge pipeline is incomplete so objects may live in
        // a parent dir that isn't reachable with parentDirs=false.
        if (!mObject && buf[0] != '\0') {
            ObjectDir *searchDir = dir;
            while (!mObject && searchDir) {
                ObjectDir *nextDir = nullptr;
                if (searchDir->Dir() && searchDir->Dir() != searchDir) {
                    nextDir = searchDir->Dir();
                } else if (searchDir->Loader() && searchDir->Loader()->ParentDir()) {
                    nextDir = searchDir->Loader()->ParentDir();
                }
                if (!nextDir || nextDir == searchDir) break;
                SetObj(nextDir->FindObject(buf, false, true));
                searchDir = nextDir;
            }
            if (!mObject) {
                SetObj(ObjectDir::Main()->FindObject(buf, false, true));
            }
        }
#endif
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
#ifdef HX_NATIVE
    // RB3 reference: return mOwner directly. The original DC3 code
    // (mObject->RefOwner()) fails during Load() because mObject is null
    // before the target name is resolved, so the dir lookup can't find
    // the target object. Using mOwner->RefOwner() gives the owner's dir.
    return mOwner->RefOwner();
#else
    return mObject ? mObject->RefOwner() : nullptr;
#endif
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
            // corrupts the ring walk. Suppress the erase; the null entry is
            // cleaned up when the vector is destroyed or iterated.
            if (!gInReplaceList) {
                erase(iterator(mNodes.begin() + (n - mNodes.data())));
            } else {
                MILO_WARN("ReplaceNode: suppressed erase during ReplaceList (owner=%s)",
                    mOwner ? PathName(mOwner) : "<null>");
            }
#else
            erase(n);
#endif
        }
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
        mNodes.push_back(Node(this));
        Set(begin() + (mNodes.size() - 1), *it);
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
#ifdef HX_NATIVE
            // Native fallback: walk up the parent dir chain when not found locally.
            // On Xbox, FileMerger flattens all objects into the same scope.
            // On native, the merge pipeline is incomplete so objects may live in
            // a parent dir that isn't reachable with parentDirs=false.
            if (!casted && buf[0] != '\0') {
                ObjectDir *searchDir = dir;
                while (!casted && searchDir) {
                    ObjectDir *nextDir = nullptr;
                    if (searchDir->Dir() && searchDir->Dir() != searchDir) {
                        nextDir = searchDir->Dir();
                    } else if (searchDir->Loader() && searchDir->Loader()->ParentDir()) {
                        nextDir = searchDir->Loader()->ParentDir();
                    }
                    if (!nextDir || nextDir == searchDir) break;
                    casted = dynamic_cast<T1 *>(nextDir->FindObject(buf, false, true));
                    searchDir = nextDir;
                }
                if (!casted) {
                    casted = dynamic_cast<T1 *>(
                        ObjectDir::Main()->FindObject(buf, false, true));
                }
            }
#endif
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
BinStream &operator>>(BinStream &bs, ObjPtrVec<T1, ObjectDir> &vec) {
    vec.Load(bs, true, nullptr);
    return bs;
}

// ObjPtrVec::find, ::swap, ::sort bodies are in ObjPtrVec_impl.h
// to keep them out of the PCH (inlining budget pollution).
// Include ObjPtrVec_impl.h in .cpp files that call these methods.
#ifdef HX_NATIVE
#include "obj/ObjPtrVec_impl.h"
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
            erase(node);
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

template <class T1, class T2>
bool ObjPtrList<T1, T2>::remove(T1 *target) {
    for (iterator it = begin(); it != end(); ++it) {
        if (*it == target) {
            erase(it);
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
#ifdef HX_NATIVE
            if (!casted && buf[0] != '\0') {
                ObjectDir *searchDir = dir;
                while (!casted && searchDir) {
                    ObjectDir *nextDir = nullptr;
                    if (searchDir->Dir() && searchDir->Dir() != searchDir) {
                        nextDir = searchDir->Dir();
                    } else if (searchDir->Loader() && searchDir->Loader()->ParentDir()) {
                        nextDir = searchDir->Loader()->ParentDir();
                    }
                    if (!nextDir || nextDir == searchDir) break;
                    casted = dynamic_cast<T1 *>(nextDir->FindObject(buf, false, b4));
                    searchDir = nextDir;
                }
                if (!casted) {
                    casted = dynamic_cast<T1 *>(
                        ObjectDir::Main()->FindObject(buf, false, b4));
                }
            }
#endif
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
BinStream &operator>>(BinStream &bs, ObjPtrList<T1, ObjectDir> &list) {
    list.Load(bs, true, nullptr, true);
    return bs;
}

template <class T1, class T2>
template <class S>
void ObjPtrList<T1, T2>::sort(const S &cmp) {
    if (mNodes && mNodes->next) {
        Node *sentinel = mNodes;
#ifdef HX_NATIVE
        // Native Link() creates NULL-terminated forward links (last->next = nullptr).
        // PPC Link() creates circular links (last->next = sentinel).
        for (Node *outer = sentinel->next; outer != nullptr; outer = outer->next) {
#else
        for (Node *outer = sentinel->next; outer != sentinel; outer = outer->next) {
#endif
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
    }
}

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
        // If removing the tail, update head's tail pointer
        if (mNodes->prev == node) {
            mNodes->prev = prev;
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

template <class T1, class T2>
Hmx::Object *ObjPtrVec<T1, T2>::Node::RefOwner() const {
    return static_cast<ObjPtrVec<T1, T2>*>(mOwner)->Owner();
}

template <class T1, class T2>
typename ObjPtrVec<T1, T2>::iterator
ObjPtrVec<T1, T2>::erase(ObjPtrVec<T1, T2>::iterator it) {
    return mNodes.erase(it.it);
}

template <class T1, class T2>
typename ObjPtrVec<T1, T2>::iterator ObjPtrVec<T1, T2>::FindRef(ObjRef *ref) {
    for (iterator it = begin(); it != end(); ++it) {
        if (&(*it) == ref) return it;
    }
    return end();
}

// ObjPtrVec::merge, ::unique, ::remove bodies are in ObjPtrVec_impl.h
#include "obj/ObjPtrVec_impl.h"

// -- BinStream operator<< for ObjPtrList --

template <class T1>
BinStream &operator<<(BinStream &bs, const ObjPtrList<T1, ObjectDir> &list) {
    bs << list.size();
    for (typename ObjPtrList<T1, ObjectDir>::iterator it = list.begin(); it != list.end(); ++it) {
        Hmx::Object *obj = *it;
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

// -- BinStream operator<< for ObjPtrVec --

template <class T1>
BinStream &operator<<(BinStream &bs, const ObjPtrVec<T1, ObjectDir> &c) {
    bs << (int)c.size();
    MILO_ASSERT(c.Owner(), 0x525);
    for (int i = 0; i < (int)c.size(); i++) {
        const Hmx::Object *obj = c[i];
        const char *name = obj ? obj->Name() : "";
        bs << name;
    }
    return bs;
}

#endif
