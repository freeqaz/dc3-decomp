// ThreadTask::Replace erase-vs-remove regression test (Wave-4 Lane C).
//
// The original Xbox 360 binary, when a ThreadTask is *executing* and the ObjRef
// being replaced belongs to the task's mObjects list, ERASES that specific node
// from mObjects:
//
//     if (mExecuting && &mObjects == from->Parent() && from) {
//         mObjects.erase(<iterator wrapping `from`>);   // erase the dead node
//         return true;
//     }
//
// The decompiled source (and the og-dc3 copy of it) instead called
// `mObjects.remove(to)` — it searched the list for the *replacement* object `to`
// and unlinked that. On native this is wrong twice over:
//   * `to` is usually NOT in mObjects (it's the new object), so remove() is a
//     no-op and the stale `from` node is leaked / left dangling, and
//   * if `to` happened to be elsewhere in the list it would unlink the WRONG node.
//
// objdiff confirms the target calls ObjPtrList::erase (folded across template
// instantiations via ICF) on the `from` node, not ObjPtrList::remove on `to`.
//
// This test pins the BEHAVIOR (which node leaves the list), not the lowering.
// It drives the real ThreadTask::Replace against a controlled mObjects list and a
// real ObjRef node produced by push_back (so from->Parent() == &mObjects holds).
//
// Private/protected access via the standard test-only macro hack.

#include <gtest/gtest.h>

#include <cstring>
#include <new>

#define private public
#define protected public
#include "obj/Task.h"
#include "obj/Object.h"
#undef private
#undef protected

#include "test_helpers.h"

namespace {

// A trivial concrete Hmx::Object we can push into an ObjPtrList without standing
// up the factory/ObjectDir machinery. Plain Hmx::Object is concrete and its
// default ctor only zeroes a few ref pointers.
Hmx::Object *MakePlainObject() { return new Hmx::Object(); }

// Build a ThreadTask shell WITHOUT running its DataArray-heavy ctor (which needs
// global DataVariable state and a live script). Replace()'s erase path only reads
// `mExecuting` and `mObjects`, calls from->Parent(), and (on the fast path) calls
// ObjPtrList::erase — none of which touch the higher ThreadTask members.
//
// We give the shell a VALID Hmx::Object vtable at offset 0 by placement-newing a
// real Hmx::Object base into the storage, so the list owner's RefOwner() resolves
// (Hmx::Object::RefOwner() returns `this`) and the node ring stays well-formed.
// Then we construct the mObjects member (owned by the shell) and set mExecuting.
struct ThreadTaskShell {
    // Raw storage sized/aligned for a ThreadTask.
    alignas(ThreadTask) unsigned char storage[sizeof(ThreadTask)];

    ThreadTask *task() { return reinterpret_cast<ThreadTask *>(storage); }

    ThreadTaskShell() {
        std::memset(storage, 0, sizeof(storage));
        ThreadTask *t = task();
        // Valid Hmx::Object vtable/base at offset 0 (Task/ScriptTask/ThreadTask
        // share that base offset). Gives the list owner a working RefOwner().
        new (static_cast<Hmx::Object *>(t)) Hmx::Object();
        // Construct the mObjects member (an ObjPtrList<Hmx::Object>) owned by the
        // shell. kObjListOwnerControl mirrors the real ScriptTask ctor.
        new (&t->mObjects) ObjPtrList<Hmx::Object>(t, kObjListOwnerControl);
        t->mExecuting = true;
    }

    ~ThreadTaskShell() {
        // Destroy the list (clears/unlinks remaining nodes), then the Object base.
        task()->mObjects.~ObjPtrList();
        static_cast<Hmx::Object *>(task())->~Object();
    }
};

}  // namespace

using ThreadTaskReplaceTest = SymbolTestFixture;

// When the task is executing and `from` is a node of mObjects, Replace erases
// THAT node (mObjects shrinks by one, the other node survives) and returns true.
// It must NOT remove `to`.
TEST_F(ThreadTaskReplaceTest, ErasesFromNodeNotRemovesTo) {
    ThreadTaskShell shell;
    ThreadTask *task = shell.task();

    Hmx::Object *objA = MakePlainObject();  // becomes `from`'s referent
    Hmx::Object *objB = MakePlainObject();  // a second, untouched list member
    Hmx::Object *to = MakePlainObject();    // the replacement object (not in list)

    // Populate mObjects: [objA, objB]. Each push_back creates a Node whose
    // Parent() == &mObjects.
    task->mObjects.push_back(objA);
    task->mObjects.push_back(objB);
    ASSERT_EQ(task->mObjects.size(), 2);

    // `from` is the first node (the ObjRef holding objA). mNodes is the head.
    ObjRef *from = task->mObjects.begin().mNode;
    ASSERT_TRUE(from != nullptr);
    ASSERT_EQ(from->Parent(), &task->mObjects)
        << "the head node must belong to mObjects so the erase branch fires";

    bool ret = task->ThreadTask::Replace(from, to);

    EXPECT_TRUE(ret) << "Replace must report it handled the executing-task ref";
    // The crux: exactly the `from` node was erased; objB's node survives.
    EXPECT_EQ(task->mObjects.size(), 1)
        << "Replace must erase the `from` node from mObjects";
    EXPECT_EQ(task->mObjects.front(), objB)
        << "the surviving node must be objB (objA's node was the one erased)";

    delete objA;
    delete objB;
    delete to;
}

// If the buggy remove(to) path were taken instead, `to` (not in the list) would
// be searched for and nothing would be removed — the list would still hold both
// nodes. This test makes that failure mode explicit: even when `to` is ALSO a
// member of mObjects, the correct behavior erases the `from` node, leaving `to`.
TEST_F(ThreadTaskReplaceTest, ErasesFromEvenWhenToAlsoInList) {
    ThreadTaskShell shell;
    ThreadTask *task = shell.task();

    Hmx::Object *objA = MakePlainObject();  // `from`'s referent (should be erased)
    Hmx::Object *to = MakePlainObject();    // also a list member (must survive)

    task->mObjects.push_back(objA);
    task->mObjects.push_back(to);
    ASSERT_EQ(task->mObjects.size(), 2);

    ObjRef *from = task->mObjects.begin().mNode;  // head == objA's node
    ASSERT_EQ(from->Parent(), &task->mObjects);

    bool ret = task->ThreadTask::Replace(from, to);

    EXPECT_TRUE(ret);
    EXPECT_EQ(task->mObjects.size(), 1)
        << "the `from` node is erased (a remove(to) bug would erase `to` instead)";
    EXPECT_EQ(task->mObjects.front(), to)
        << "`to` must remain; only the `from` node leaves the list";

    delete objA;
    delete to;
}
