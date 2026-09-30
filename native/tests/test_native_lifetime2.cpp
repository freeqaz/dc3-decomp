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
#include "rndobj/Lit.h"
#include "rndobj/Mesh.h"
#include "char/CharBonesMeshes.h"
#include "obj/Task.h"
#include "world/DefaultPhysicsManager.h"
#include "world/LightPreset.h"

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


// ===========================================================================
// Every owner-control holder: the owner hears its target die in the cascade
// ===========================================================================
//
// native-partyplay ran the image's owner step for TypeProps, RndEnviron and
// (backed out) RndGroup only.  Every other owner-control holder kept the old
// bypass: its ref was nulled and the owner's own state was left behind.  The
// four owners named as open leads, each against its image Replace:
//   * CharBonesMeshes::Replace substitutes sDummyMesh (PoseMeshes dereferences
//     every entry of mMeshes);
//   * DefaultPhysicsManager::Replace erases the node AND RemoveCollidable()s
//     the object from the raw RndMesh* lists and the dir map, which Poll and
//     the ray casts walk -- skipped, those kept the freed mesh;
//   * LightPreset::Replace removes the light from mLights and its column from
//     every keyframe;
//   * ScriptTask::Replace / MessageTask::Replace `delete this` -- skipped, the
//     task outlived an object its script names (ScriptTask) or its target
//     (MessageTask).
// Each is paired with a plain-delete control (the ReplaceRefs path).

namespace {
    struct BonesMeshesProbe : public CharBonesMeshes {
        ObjPtrVec<RndTransformable> &Meshes() { return mMeshes; }
        static RndTransformable *Dummy() { return sDummyMesh; }
    };

    struct PhysicsProbe : public DefaultPhysicsManager {
        PhysicsProbe() : DefaultPhysicsManager(nullptr) {}
        void Add(Hmx::Object *o) { AddCollidable(o, nullptr, true); }
        int Collidables() const { return mCollidables.size(); }
        int Active() const { return mActiveCollidables.size(); }
        int Mapped() const { return mCollidableDirs.size(); }
    };

    struct PresetProbe : public LightPreset {
        using LightPreset::AddLight;
        int Lights() const { return mLights.size(); }
        bool HasNullLight() const {
            for (int i = 0; i < mLights.size(); i++)
                if (!mLights[i])
                    return true;
            return false;
        }
    };

    ObjectDir *DyingDir(const char *name) {
        ObjectDir *dir = Hmx::Object::New<ObjectDir>();
        dir->SetName(name, ObjectDir::Main());
        return dir;
    }
}

static void CheckBonesMeshes(bool cascade) {
    if (!BonesMeshesProbe::Dummy())
        CharBonesMeshes::Init();
    BonesMeshesProbe *servo = new BonesMeshesProbe();
    ObjectDir *dir = cascade ? DyingDir("lt2_bones_dir") : nullptr;
    RndTransformable *bone = Hmx::Object::New<RndTransformable>();
    if (dir)
        bone->SetName("bone_pelvis.mesh", dir);
    servo->Meshes().push_back(bone);
    ASSERT_EQ(servo->Meshes().size(), 1);
    if (dir)
        delete dir;
    else
        delete bone;
    ASSERT_EQ(servo->Meshes().size(), 1);
    EXPECT_EQ((RndTransformable *)servo->Meshes()[0], BonesMeshesProbe::Dummy())
        << "the deleted bone was not replaced by sDummyMesh "
           "(CharBonesMeshes::Replace never ran); PoseMeshes would write through NULL";
    delete servo;
}

TEST_F(NativeLifetime2Test, BonesMeshesSubstituteTheDummyOnPlainDelete) { CheckBonesMeshes(false); }
TEST_F(NativeLifetime2Test, BonesMeshesSubstituteTheDummyInTheDirCascade) { CheckBonesMeshes(true); }

static void CheckPhysics(bool cascade) {
    PhysicsProbe *physics = new PhysicsProbe();
    ObjectDir *dir = cascade ? DyingDir("lt2_physics_dir") : nullptr;
    RndMesh *mesh = Hmx::Object::New<RndMesh>();
    if (dir)
        mesh->SetName("collide.mesh", dir);
    physics->Add(mesh);
    ASSERT_EQ(physics->Collidables(), 1);
    ASSERT_EQ(physics->Active(), 1);
    if (dir)
        delete dir;
    else
        delete mesh;
    EXPECT_EQ(physics->Collidables(), 0) << "the collidable's node was kept";
    EXPECT_EQ(physics->Active(), 0)
        << "the active-collidable list still holds the freed mesh "
           "(DefaultPhysicsManager::Replace -> RemoveCollidable never ran)";
    EXPECT_EQ(physics->Mapped(), 0) << "mCollidableDirs still maps the freed mesh";
    delete physics;
}

TEST_F(NativeLifetime2Test, PhysicsDropsADeletedCollidableOnPlainDelete) { CheckPhysics(false); }
TEST_F(NativeLifetime2Test, PhysicsDropsADeletedCollidableInTheDirCascade) { CheckPhysics(true); }

static void CheckLightPreset(bool cascade) {
    PresetProbe *preset = new PresetProbe();
    ObjectDir *dir = cascade ? DyingDir("lt2_preset_dir") : nullptr;
    RndLight *light = Hmx::Object::New<RndLight>();
    if (dir)
        light->SetName("key.lit", dir);
    preset->AddLight(light);
    ASSERT_EQ(preset->Lights(), 1);
    if (dir)
        delete dir;
    else
        delete light;
    EXPECT_EQ(preset->Lights(), 0)
        << "the preset kept the deleted light (LightPreset::Replace never ran)";
    EXPECT_FALSE(preset->HasNullLight());
    delete preset;
}

TEST_F(NativeLifetime2Test, LightPresetDropsADeletedLightOnPlainDelete) { CheckLightPreset(false); }
TEST_F(NativeLifetime2Test, LightPresetDropsADeletedLightInTheDirCascade) { CheckLightPreset(true); }

static void CheckScriptTask(bool cascade) {
    ObjectDir *dir = cascade ? DyingDir("lt2_task_dir") : nullptr;
    Hmx::Object *target = Hmx::Object::New<Hmx::Object>();
    if (dir)
        target->SetName("task_target", dir);
    // {<target> foo}: the script names the object, so ScriptTask's
    // UpdateVarsObjects puts it in mObjects (owner control).
    DataArray *script = new DataArray(1);
    DataArray *cmd = new DataArray(2);
    cmd->Node(0) = DataNode(target);
    cmd->Node(1) = DataNode(Symbol("foo"));
    script->Node(0) = DataNode(cmd, kDataCommand);
    cmd->Release();
    ScriptTask *task = new ScriptTask(script, true, nullptr);
    script->Release();
    Hmx::DeathWatch watch(task);
    if (dir)
        delete dir;
    else
        delete target;
    EXPECT_TRUE(watch.Dead())
        << "the task outlived an object its script names "
           "(ScriptTask::Replace never ran)";
    if (!watch.Dead())
        delete task;
}

TEST_F(NativeLifetime2Test, ScriptTaskDiesWithItsObjectOnPlainDelete) { CheckScriptTask(false); }
TEST_F(NativeLifetime2Test, ScriptTaskDiesWithItsObjectInTheDirCascade) { CheckScriptTask(true); }

static void CheckMessageTask(bool cascade) {
    ObjectDir *dir = cascade ? DyingDir("lt2_msgtask_dir") : nullptr;
    Hmx::Object *target = Hmx::Object::New<Hmx::Object>();
    if (dir)
        target->SetName("msg_target", dir);
    DataArray *msg = new DataArray(2);
    msg->Node(0) = DataNode(target);
    msg->Node(1) = DataNode(Symbol("foo"));
    MessageTask *task = new MessageTask(target, msg);
    msg->Release();
    Hmx::DeathWatch watch(task);
    if (dir)
        delete dir;
    else
        delete target;
    EXPECT_TRUE(watch.Dead())
        << "the task outlived its target (MessageTask::Replace never ran)";
    if (!watch.Dead())
        delete task;
}

TEST_F(NativeLifetime2Test, MessageTaskDiesWithItsTargetOnPlainDelete) { CheckMessageTask(false); }
TEST_F(NativeLifetime2Test, MessageTaskDiesWithItsTargetInTheDirCascade) { CheckMessageTask(true); }

// ===========================================================================
// Make Your Move: player 1's frame scores (LP64 stride)
// ===========================================================================
//
// FreestyleMoveRecorder::GetScore finds a player's FreestyleFrameScores as
// `(char *)unke4 + (playerIdx << 4)` -- the Xbox's sizeof, 0x10.  Natively the
// struct is 0x20, so player 1 read player 0's vector capacity pointer as its
// score array and the low half of a heap pointer as its count.  The party
// route SIGSEGV'd there (GetScore <- BustAMovePanel::Poll, Make Your Move).
// A fresh recorder has no scores, so every player's score is exactly 0.  The
// misread count is positive or negative with the heap address, so one
// recorder can pass by luck: 32 of them cannot.

#include <unistd.h>
#include "hamobj/FreestyleMoveRecorder.h"

namespace {
void ScorePlayerOneOnFreshRecorders() {
    for (int i = 0; i < 32; i++) {
        FreestyleMoveRecorder *rec = new FreestyleMoveRecorder();
        rec->StartRecording();
        float score = rec->GetScore((const BaseSkeleton *)nullptr, 1, 0.0f, false);
        if (score != 0.0f)
            _exit(2);
        delete rec;
    }
    _exit(0);
}
} // namespace

class MakeYourMoveScoresTest : public SymbolTestFixture {};

TEST_F(MakeYourMoveScoresTest, PlayerOneReadsItsOwnFrameScores) {
    GTEST_FLAG_SET(death_test_style, "threadsafe"); // see test_object_lifetime.cpp
    ASSERT_EXIT(ScorePlayerOneOnFreshRecorders(), ::testing::ExitedWithCode(0), "")
        << "player 1's score read past player 0's FreestyleFrameScores "
           "(GetScore's 0x10 stride)";
}
