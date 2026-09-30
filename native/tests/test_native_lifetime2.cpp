// Native object-lifetime lane 2 (branch native-lifetime2, 2026-09-30).
//
// The image frees a dir's objects with `delete` (ObjectDir::DeleteObjects:
// every object in the hash table but the dir itself goes through vtable slot
// 0, the scalar deleting dtor -- no survivor test, no DirPtr test), and
// ~Object runs ReplaceRefs(nullptr): every ref to the dying object gets
// ObjRef::Replace(nullptr).  Native DeleteObjects nullifies refs with
// Hmx::Object::NullifyAllRefs instead, which fires no Replace; for the
// OWNER-CONTROL holders (ObjOwnerPtr, and ObjPtrList/ObjPtrVec nodes in
// kObjListOwnerControl) it runs the image's step owner by owner.

#include "test_helpers.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "rndobj/Group.h"

class NativeLifetime2Test : public EngineTestFixture {};

// ===========================================================================
// RndGroup: a deleted child leaves the group (RndGroup::Replace erases its node)
// ===========================================================================
//
// Skipped in the cascade, a group kept a NULL child: on the party route a
// sound_group's get_group_children handed ui_objects.dta's `shuffle` a null
// $elem (`$elem = <null> not function or object`, 8x per party song).
// native-partyplay ran the group's Replace inside the NullifyAllRefs walk and
// backed it out: it double-freed a list node in
// MergeScopeParityTest.RepeatedVenueMergeAfterClear.

// Control: outside a cascade the child's node is erased (ReplaceRefs).
TEST_F(NativeLifetime2Test, GroupDropsADeletedChildOnPlainDelete) {
    RndGroup *group = Hmx::Object::New<RndGroup>();
    Hmx::Object *child = Hmx::Object::New<Hmx::Object>();
    group->AddObject(child);
    ASSERT_EQ(group->Objects().size(), 1);
    delete child;
    EXPECT_EQ(group->Objects().size(), 0);
    delete group;
}

TEST_F(NativeLifetime2Test, GroupDropsADeletedChildInTheDirCascade) {
    RndGroup *group = Hmx::Object::New<RndGroup>();
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();
    dir->SetName("lt2_group_cascade_dir", ObjectDir::Main());
    Hmx::Object *child = Hmx::Object::New<Hmx::Object>();
    child->SetName("child.snd", dir);
    group->AddObject(child);
    ASSERT_EQ(group->Objects().size(), 1);
    delete dir;
    EXPECT_EQ(group->Objects().size(), 0)
        << "the group kept a NULL child node (RndGroup::Replace never ran)";
    for (ObjPtrList<Hmx::Object>::iterator it = group->Objects().begin();
         it != group->Objects().end(); ++it) {
        EXPECT_NE(*it, nullptr) << "null child left in the group";
    }
    delete group;
}

// The double free.  NullifyAllRefs walks the dying object's ring front to
// back and self-loops each ref it nulls WITHOUT touching its neighbours, so
// the next ref's `prev` still names the ref just processed.  An owner step
// that unlinks its ref (RndGroup::Replace erases the node; ~ObjRefConcrete in
// a cascade does SafeReleaseFromRing) then writes through that stale `prev`:
// into an earlier holder that is already self-looped -- or, when that holder
// was a kObjListNoNull list node, which NullifyObj `delete`s, into a freed
// block.  That write is what corrupted glibc's bins in the merge tests.
//
// Observable without a sanitizer: the earlier holder must end self-looped
// (empty ring), as NullifyObj left it.  With the stale unlink its `next`
// points at the dying object's own ring head, i.e. into a freed block.
TEST_F(NativeLifetime2Test, GroupStepLeavesEarlierRefsSelfLooped) {
    Hmx::Object *holderOwner = Hmx::Object::New<Hmx::Object>();
    RndGroup *group = Hmx::Object::New<RndGroup>();
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();
    dir->SetName("lt2_group_ring_dir", ObjectDir::Main());
    Hmx::Object *child = Hmx::Object::New<Hmx::Object>();
    child->SetName("child_ring.snd", dir);
    // Ring order is AddRef order: the holder's ref first, the group's node
    // second, so the walk nulls the holder and then runs the group's step.
    ObjPtr<Hmx::Object> holder(holderOwner, child);
    group->AddObject(child);
    delete dir;
    EXPECT_EQ((Hmx::Object *)holder, nullptr);
    EXPECT_TRUE(holder.empty())
        << "the group's unlink wrote through the stale prev of its node into "
           "the holder, which now links into the freed object's ring";
    EXPECT_EQ(group->Objects().size(), 0);
    delete group;
    delete holderOwner;
}

// Same, with the earlier holder a kObjListNoNull list node -- the shape that
// freed a node and then wrote into it.  The list must end empty, and the
// group too; a second cascade afterwards must not trip over the corruption.
TEST_F(NativeLifetime2Test, GroupStepAfterANoNullListNode) {
    Hmx::Object *holderOwner = Hmx::Object::New<Hmx::Object>();
    RndGroup *group = Hmx::Object::New<RndGroup>();
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();
    dir->SetName("lt2_group_nonull_dir", ObjectDir::Main());
    Hmx::Object *child = Hmx::Object::New<Hmx::Object>();
    child->SetName("child_nonull.snd", dir);
    ObjPtrList<Hmx::Object> *holder =
        new ObjPtrList<Hmx::Object>(holderOwner, kObjListNoNull);
    holder->push_back(child);
    group->AddObject(child);
    delete dir;
    EXPECT_EQ(holder->size(), 0);
    EXPECT_EQ(group->Objects().size(), 0);
    delete holder;
    delete group;
    delete holderOwner;
}
