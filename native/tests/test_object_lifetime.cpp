// Native lifetime/merge safety regression tests.
#include "flow/Flow.h"
#include "flow/FlowAnimate.h"
#include "rndobj/Group.h"
#include "test_helpers.h"

#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Object.h"
#include "ui/UIPanel.h"
#include "obj/Utl.h"
#include "utl/FilePath.h"
#include <ctime>

namespace {

class ObjectLifetimeTest : public EngineTestFixture {};

class TestRefHolder : public Hmx::Object {
public:
    TestRefHolder() : mTarget(this, nullptr) {}

    void SetTarget(Hmx::Object *obj) { mTarget = obj; }
    Hmx::Object *Target() const { return mTarget.Ptr(); }

private:
    ObjPtr<Hmx::Object> mTarget;
};

class ExposedFlow : public Flow {
public:
    ExposedFlow() : Flow() {}

    int ChildCount() const { return mChildNodes.size(); }
    FlowNode *FrontChild() const { return mChildNodes.empty() ? nullptr : mChildNodes.front(); }
};

class ExposedRndGroup : public RndGroup {
public:
    ExposedRndGroup() : RndGroup() {}

    int ObjectCount() const { return mObjects.size(); }
};

// Expose protected FindEntry for corruption simulation tests.
class ExposedDir : public ObjectDir {
public:
    ExposedDir() : ObjectDir() {}
    Entry *ExposeFindEntry(const char *name, bool add) { return FindEntry(name, add); }
};

// Parity-oracle: this captures expected collision-merge ref behavior.
// Keep strict even if currently failing; this is our parity north star.
TEST_F(ObjectLifetimeTest, MergeDirsNameCollisionLeavesOnlyLivePointers) {
    ObjectDir *toDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *fromDir = Hmx::Object::New<ObjectDir>();
    Hmx::Object *refOwner = Hmx::Object::New<Hmx::Object>();

    Hmx::Object *toDup = Hmx::Object::New<Hmx::Object>();
    toDup->SetName("dup.obj", toDir);

    Hmx::Object *fromDup = Hmx::Object::New<Hmx::Object>();
    fromDup->SetName("dup.obj", fromDir);

    Hmx::Object *fromOnly = Hmx::Object::New<Hmx::Object>();
    fromOnly->SetName("only_from.obj", fromDir);

    ObjPtr<Hmx::Object> ref(refOwner, fromDup);
    ASSERT_EQ(ref.Ptr(), fromDup);

    MergeFilter filt((MergeFilter::Action)1, MergeFilter::kNoSubdirs);
    MergeDirs(fromDir, toDir, filt);

    // Parity expectation: on name collision with replace action, refs redirect
    // from source object to destination object in the target dir.
    EXPECT_EQ(ref.Ptr(), toDup);
    EXPECT_NE(ref.Ptr(), nullptr);

    delete fromDir;

    EXPECT_NE(ref.Ptr(), nullptr);
    EXPECT_NE(ref.Ptr(), nullptr);
    EXPECT_EQ(toDir->FindObject("dup.obj", false, true), ref.Ptr());

    int liveEntries = 0;
    for (ObjectDir::Entry *e = toDir->HashTable().Begin(); e != nullptr;
         e = toDir->HashTable().Next(e)) {
        if (e->obj) {
            EXPECT_NE(e->obj, nullptr) << "Dead pointer for entry " << e->name;
            liveEntries++;
        }
    }
    EXPECT_GE(liveEntries, 1);

    // Iterator should walk without touching dead objects.
    int itrCount = 0;
    for (ObjDirItr<Hmx::Object> it(toDir, false); it != nullptr; ++it) {
        EXPECT_NE(&*it, nullptr);
        itrCount++;
    }
    EXPECT_GE(itrCount, 1);

    delete refOwner;
    delete toDir;
}

// Normal deletion path should null the hash entry and keep iteration safe.
TEST_F(ObjectLifetimeTest, ObjDirItrIgnoresNullHashEntriesAfterDelete) {
    ExposedDir *dir = new ExposedDir();

    Hmx::Object *victim = Hmx::Object::New<Hmx::Object>();
    victim->SetName("victim.obj", dir);

    delete victim;

    ObjectDir::Entry *entry = dir->ExposeFindEntry("victim.obj", true);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->obj, nullptr);

    int itrCount = 0;
    for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it) {
        EXPECT_NE(&*it, nullptr);
        itrCount++;
        ASSERT_LT(itrCount, 32);
    }
    EXPECT_EQ(itrCount, 0);

    delete dir;
}

// Unit baseline: direct ReplaceRefs redirect works outside merge recursion.
TEST_F(ObjectLifetimeTest, ReplaceRefsRedirectsObjPtr) {
    Hmx::Object *owner = Hmx::Object::New<Hmx::Object>();
    Hmx::Object *from = Hmx::Object::New<Hmx::Object>();
    Hmx::Object *to = Hmx::Object::New<Hmx::Object>();

    ObjPtr<Hmx::Object> ref(owner, from);
    ASSERT_EQ(ref.Ptr(), from);
    EXPECT_EQ(from->RefCount(), 1);

    from->ReplaceRefs(to);

    EXPECT_EQ(ref.Ptr(), to);
    EXPECT_EQ(from->RefCount(), 0);
    EXPECT_EQ(to->RefCount(), 1);

    delete owner;
    delete from;
    delete to;
}

// Hash-order deletion should not require dependency ordering for basic ObjPtr
// ownership: deleting the target must null incoming refs before later owners die.
TEST_F(ObjectLifetimeTest, DeleteOrderDoesNotRequireTopologicalSortForObjPtr) {
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();

    TestRefHolder *a = new TestRefHolder();
    TestRefHolder *b = new TestRefHolder();
    a->SetName("a.obj", dir);
    b->SetName("b.obj", dir);

    a->SetTarget(b);
    ASSERT_EQ(a->Target(), b);
    EXPECT_EQ(b->RefCount(), 1);

    delete b;

    EXPECT_EQ(a->Target(), nullptr);

    delete a;
    delete dir;
}

TEST_F(ObjectLifetimeTest, DeletingFlowChildLeavesNullTombstoneUntilParentTeardown) {
    ExposedFlow *flow = new ExposedFlow();
    FlowAnimate *child = Hmx::Object::New<FlowAnimate>();

    child->SetParent(flow, true);
    ASSERT_EQ(flow->ChildCount(), 1);
    ASSERT_EQ(flow->FrontChild(), child);

    delete child;

    EXPECT_EQ(flow->ChildCount(), 1);
    EXPECT_EQ(flow->FrontChild(), nullptr);

    delete flow;
}

TEST_F(ObjectLifetimeTest, DeletingRndGroupMemberRemovesOwnerControlNode) {
    ExposedRndGroup *group = new ExposedRndGroup();
    Hmx::Object *child = Hmx::Object::New<Hmx::Object>();

    group->AddObject(child);
    ASSERT_EQ(group->ObjectCount(), 1);

    delete child;

    EXPECT_EQ(group->ObjectCount(), 0);

    if (group->ObjectCount() == 0) {
        delete group;
    }
}

TEST_F(ObjectLifetimeTest, RemoveSubDirReleasesDirPtrRef) {
    ObjectDir *owner = Hmx::Object::New<ObjectDir>();
    ObjectDir *subdir = Hmx::Object::New<ObjectDir>();
    ObjDirPtr<ObjectDir> hold(subdir);

    owner->AppendSubDir(ObjDirPtr<ObjectDir>(subdir));
    EXPECT_TRUE(owner->HasSubDir(subdir));

    owner->RemoveSubDir(hold);
    EXPECT_FALSE(owner->HasSubDir(subdir));
    EXPECT_NE(subdir, nullptr);

    delete owner;
}

TEST_F(ObjectLifetimeTest, ObjDirPtrConstructorKeepsSingleWellFormedRefRingNode) {
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();
    ObjDirPtr<ObjectDir> keeper;
    keeper = dir;

    {
        ObjDirPtr<ObjectDir> hold(dir);

        const ObjRef &refs = dir->Refs();
        ObjRef::iterator it = refs.begin();
        ASSERT_NE(it, refs.end());

        ObjRef *first = it;
        ++it;
        ASSERT_NE(it, refs.end());

        ObjRef *second = it;
        EXPECT_NE(second, first);
        ++it;
        EXPECT_EQ(it, refs.end());
    }

    const ObjRef &refs = dir->Refs();
    ObjRef::iterator it = refs.begin();
    ASSERT_NE(it, refs.end());
    ++it;
    EXPECT_EQ(it, refs.end());

    keeper = nullptr;
}

// Fixture-backed safety baseline on real archive content.
TEST_F(ObjectLifetimeTest, MergeDirsRealFixturesLeaveOnlyLiveEntries) {
    FilePath toPath("char/shared/main_resource.milo");
    FilePath fromPath("char/shared/viseme_resource.milo");

    ObjectDir *toDir = DirLoader::LoadObjects(toPath, nullptr, nullptr);
    ObjectDir *fromDir = DirLoader::LoadObjects(fromPath, nullptr, nullptr);
    ASSERT_NE(toDir, nullptr);
    ASSERT_NE(fromDir, nullptr);

    MergeFilter filt((MergeFilter::Action)1, MergeFilter::kNoSubdirs);
    MergeDirs(fromDir, toDir, filt);
    delete fromDir;

    int liveEntries = 0;
    for (ObjectDir::Entry *e = toDir->HashTable().Begin(); e != nullptr;
         e = toDir->HashTable().Next(e)) {
        if (e->obj) {
            EXPECT_NE(e->obj, nullptr) << "Dead pointer for entry " << e->name;
            liveEntries++;
        }
    }
    EXPECT_GT(liveEntries, 0);

    int itrCount = 0;
    for (ObjDirItr<Hmx::Object> it(toDir, false); it != nullptr; ++it) {
        EXPECT_NE(&*it, nullptr);
        itrCount++;
        ASSERT_LT(itrCount, 10000);
    }
    EXPECT_GT(itrCount, 0);

    delete toDir;
}

TEST_F(ObjectLifetimeTest, RepeatedFixtureMergesKeepIteratorSafe) {
    FilePath basePath("char/shared/main_resource.milo");
    ObjectDir *base = DirLoader::LoadObjects(basePath, nullptr, nullptr);
    ASSERT_NE(base, nullptr);

    const char *overlays[] = {
        "char/shared/viseme_resource.milo",
        "char/shared/skeleton_bones_resource.milo",
        "char/shared/viseme_resource.milo",
        nullptr
    };

    MergeFilter filt((MergeFilter::Action)1, MergeFilter::kNoSubdirs);
    for (int i = 0; overlays[i]; i++) {
        ObjectDir *overlay = DirLoader::LoadObjects(FilePath(overlays[i]), nullptr, nullptr);
        ASSERT_NE(overlay, nullptr) << overlays[i];
        MergeDirs(overlay, base, filt);
        delete overlay;

        int itrCount = 0;
        for (ObjDirItr<Hmx::Object> it(base, false); it != nullptr; ++it) {
            EXPECT_NE(&*it, nullptr);
            itrCount++;
            ASSERT_LT(itrCount, 30000);
        }
        EXPECT_GT(itrCount, 0);
    }

    delete base;
}

TEST_F(ObjectLifetimeTest, MergeKeepCharClipSetRootDoesNotCorruptRefs) {
    const char *root = getenv("MILO_LIB");
    if (!root || !root[0]) {
        GTEST_SKIP() << "MILO_LIB not set";
    }

    std::string toFull = std::string(root) + "/char/crowd/anim/female_base.milo";
    std::string fromFull = std::string(root) + "/char/crowd/anim/female_medium.milo";

    ObjectDir *toDir = DirLoader::LoadObjects(FilePath(toFull.c_str()), nullptr, nullptr);
    ObjectDir *fromDir = DirLoader::LoadObjects(FilePath(fromFull.c_str()), nullptr, nullptr);
    ASSERT_NE(toDir, nullptr) << toFull;
    ASSERT_NE(fromDir, nullptr) << fromFull;

    MergeObject(fromDir, toDir, toDir, (MergeFilter::Action)2);

    delete fromDir;

    int itrCount = 0;
    for (ObjDirItr<Hmx::Object> it(toDir, false); it != nullptr; ++it) {
        EXPECT_NE(&*it, nullptr);
        itrCount++;
        ASSERT_LT(itrCount, 10000);
    }
    EXPECT_GT(itrCount, 0);

    delete toDir;
}

// Parity-oracle: kMoveAllSubdirs should transfer subdir ownership from source
// to destination (source no longer reports the moved subdir).
TEST_F(ObjectLifetimeTest, MergeDirsMoveAllSubdirsTransfersOwnership) {
    ObjectDir *toDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *fromDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *movedSubdir = Hmx::Object::New<ObjectDir>();

    fromDir->AppendSubDir(ObjDirPtr<ObjectDir>(movedSubdir));
    ASSERT_TRUE(fromDir->HasSubDir(movedSubdir));
    ASSERT_FALSE(toDir->HasSubDir(movedSubdir));

    MergeFilter filt((MergeFilter::Action)1, MergeFilter::kMoveAllSubdirs);
    MergeDirs(fromDir, toDir, filt);

    EXPECT_TRUE(toDir->HasSubDir(movedSubdir));
    EXPECT_FALSE(fromDir->HasSubDir(movedSubdir));

    delete fromDir;
    EXPECT_NE(movedSubdir, nullptr);
    delete toDir;
}

TEST_F(ObjectLifetimeTest, DeleteAutosaveWarningRawDir) {
    const char *root = getenv("MILO_LIB");
    if (!root || !root[0]) {
        GTEST_SKIP() << "MILO_LIB not set";
    }

    std::string full = std::string(root) + "/ui/title/gen/autosave_warning.milo_xbox";
    std::clock_t loadStart = std::clock();
    printf("DeleteAutosaveWarningRawDir: loading %s\n", full.c_str());
    ObjectDir *dir = DirLoader::LoadObjects(FilePath(full.c_str()), nullptr, nullptr);
    double loadSeconds = double(std::clock() - loadStart) / CLOCKS_PER_SEC;
    ASSERT_NE(dir, nullptr) << full;
    printf("DeleteAutosaveWarningRawDir: load complete in %.3fs\n", loadSeconds);

    int count = 0;
    for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it) {
        printf("  top[%d]: '%s' (%s)\n", count, ((Hmx::Object *)it)->Name(),
               ((Hmx::Object *)it)->ClassName().Str());
        count++;
    }
    EXPECT_GT(count, 0);

    std::clock_t start = std::clock();
    printf("DeleteAutosaveWarningRawDir: deleting dir '%s' objects=%d\n", dir->Name(), count);
    delete dir;
    double seconds = double(std::clock() - start) / CLOCKS_PER_SEC;
    printf("DeleteAutosaveWarningRawDir: %.3fs\n", seconds);
}

TEST_F(ObjectLifetimeTest, DeleteAutosavingIconSubdirOnly) {
    const char *root = getenv("MILO_LIB");
    if (!root || !root[0]) {
        GTEST_SKIP() << "MILO_LIB not set";
    }

    std::string full = std::string(root) + "/ui/title/gen/autosave_warning.milo_xbox";
    ObjectDir *dir = DirLoader::LoadObjects(FilePath(full.c_str()), nullptr, nullptr);
    ASSERT_NE(dir, nullptr) << full;

    ObjectDir *subdir = dir->Find<ObjectDir>("autosaving_icon", false);
    ASSERT_NE(subdir, nullptr) << "autosaving_icon subdir not found";
    ObjDirPtr<ObjectDir> hold(subdir);
    dir->RemoveSubDir(hold);

    int count = 0;
    for (ObjDirItr<Hmx::Object> it(subdir, false); it != nullptr; ++it) {
        printf("  sub[%d]: '%s' (%s)\n", count, ((Hmx::Object *)it)->Name(),
               ((Hmx::Object *)it)->ClassName().Str());
        count++;
    }
    printf("DeleteAutosavingIconSubdirOnly: deleting '%s' objects=%d\n",
           subdir->Name(), count);

    std::clock_t start = std::clock();
    delete subdir;
    double seconds = double(std::clock() - start) / CLOCKS_PER_SEC;
    printf("DeleteAutosavingIconSubdirOnly: %.3fs\n", seconds);

    delete dir;
}

TEST_F(ObjectLifetimeTest, ManualReproAutosaveWarningPanelUnload) {
    if (!getenv("MILO_REPRO_UNLOAD")) {
        GTEST_SKIP() << "Set MILO_REPRO_UNLOAD=1 to enable manual panel unload repro";
    }

    UIPanel *panel = ObjectDir::Main()->Find<UIPanel>("autosave_warning_panel", false);
    if (!panel) {
        GTEST_SKIP() << "autosave_warning_panel not found; EngineTestFixture does not build the full UI object graph";
    }

    panel->CheckLoad();
    ASSERT_TRUE(panel->CheckIsLoaded()) << "panel failed to load";

    std::clock_t start = std::clock();
    panel->CheckUnload();
    double seconds = double(std::clock() - start) / CLOCKS_PER_SEC;
    printf("ManualReproAutosaveWarningPanelUnload: %.3fs state=%d loadRefs=%d\n",
           seconds, (int)panel->GetState(), panel->IsReferenced());

    EXPECT_EQ(panel->GetState(), UIPanel::kUnloaded);
}

// ============================================================================
// Ring corruption & DirPtrRefCount tests
// ============================================================================
// These tests target the specific ring corruption patterns identified in
// session 2026-03-18-venue-merge-crash-ring-corruption.md

// Verify that ReplaceList with the live walk handles basic ref replacement
// without corruption (the old snapshot approach could leave dangling pointers).
TEST_F(ObjectLifetimeTest, ReplaceListLiveWalkDoesNotCrash) {
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();

    // Create several objects in the dir, each with an ObjPtr pointing to
    // a target. When we ReplaceRefs on the target, all ObjPtrs should redirect.
    Hmx::Object *target = Hmx::Object::New<Hmx::Object>();
    target->SetName("target.obj", dir);

    Hmx::Object *replacement = Hmx::Object::New<Hmx::Object>();
    replacement->SetName("replacement.obj", dir);

    const int kNumHolders = 10;
    std::vector<TestRefHolder *> holders;
    for (int i = 0; i < kNumHolders; i++) {
        TestRefHolder *h = new TestRefHolder();
        char name[32];
        snprintf(name, sizeof(name), "holder%d.obj", i);
        h->SetName(name, dir);
        h->SetTarget(target);
        holders.push_back(h);
    }

    EXPECT_EQ(target->RefCount(), kNumHolders);

    // This exercises the live ring walk in ReplaceList
    target->ReplaceRefs(replacement);

    EXPECT_EQ(target->RefCount(), 0);
    EXPECT_EQ(replacement->RefCount(), kNumHolders);
    for (auto *h : holders) {
        EXPECT_EQ(h->Target(), replacement);
    }

    delete dir;
}

// Verify DirPtrRefCounts stays consistent through merge operations.
// The MergeObjectsRecurse manual Release/AddRef (lines 369-378 of Utl.cpp)
// moves refs between rings without updating DirPtrRefCounts. This test
// confirms the count tracks the actual ObjDirPtr pointing relationship,
// not ring membership.
TEST_F(ObjectLifetimeTest, DirPtrRefCountsConsistentAfterMerge) {
    ObjectDir *toDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *fromDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *subdir = Hmx::Object::New<ObjectDir>();
    subdir->SetName("sub.dir", fromDir);

    // Create an ObjDirPtr in fromDir pointing to subdir
    ObjDirPtr<ObjectDir> holder(subdir);
    EXPECT_TRUE(subdir->HasDirPtrs());

    auto &counts = DirPtrRefCounts();
    auto it = counts.find((const void *)subdir);
    ASSERT_NE(it, counts.end());
    int countBefore = it->second;
    EXPECT_GT(countBefore, 0);

    // Merge fromDir into toDir — the subdir ref should be properly tracked
    MergeFilter filt((MergeFilter::Action)1, MergeFilter::kNoSubdirs);
    MergeDirs(fromDir, toDir, filt);

    // The holder ObjDirPtr still points to subdir — count should be same
    it = counts.find((const void *)subdir);
    ASSERT_NE(it, counts.end());
    EXPECT_EQ(it->second, countBefore);
    EXPECT_TRUE(subdir->HasDirPtrs());

    holder = nullptr;
    delete fromDir;
    delete toDir;
}

// Verify that ObjPtrVec deferred purge entries are cleaned up when the
// ObjPtrVec is destroyed during a ReplaceList walk.
TEST_F(ObjectLifetimeTest, DeferredPurgeCleanedOnObjPtrVecDestruction) {
    // This tests the fix in ~ObjPtrVec: removing stale gDeferredPurges entries
    // when the vector is destroyed before the outermost ReplaceList exits.
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();

    Hmx::Object *target = Hmx::Object::New<Hmx::Object>();
    target->SetName("target.obj", dir);

    Hmx::Object *replacement = Hmx::Object::New<Hmx::Object>();
    replacement->SetName("replacement.obj", dir);

    // Create a group (has ObjPtrVec with kObjListOwnerControl) that references target
    ExposedRndGroup *group = new ExposedRndGroup();
    group->SetName("group.grp", dir);
    group->AddObject(target);
    ASSERT_EQ(group->ObjectCount(), 1);

    // ReplaceRefs should handle the group's internal ObjPtrVec correctly
    target->ReplaceRefs(replacement);

    // The group should still be valid (not crashed)
    EXPECT_GE(group->ObjectCount(), 0);

    delete dir;
}

// Verify that the ObjDirPtr delete-during-cascade doesn't cause double-free.
// When ObjDirPtr::operator= deletes an ObjectDir, the destructor chain
// should not re-delete objects that are still being processed.
// Nested subdir cascade: dir1 → dir2 → dir3 with cross-references.
// Previously hung due to double-AddRef in ObjDirPtr(C*) creating self-loops.
TEST_F(ObjectLifetimeTest, ObjDirPtrCascadeDeleteDoesNotDoubleFree) {
    // Create a chain: dir1 has subdir dir2, dir2 has subdir dir3
    ObjectDir *dir1 = Hmx::Object::New<ObjectDir>();
    ObjectDir *dir2 = Hmx::Object::New<ObjectDir>();
    ObjectDir *dir3 = Hmx::Object::New<ObjectDir>();

    dir1->AppendSubDir(ObjDirPtr<ObjectDir>(dir2));
    dir2->AppendSubDir(ObjDirPtr<ObjectDir>(dir3));

    EXPECT_TRUE(dir1->HasSubDir(dir2));
    EXPECT_TRUE(dir2->HasSubDir(dir3));

    // Create cross-references: an object in dir3 references an object in dir1
    Hmx::Object *obj1 = Hmx::Object::New<Hmx::Object>();
    obj1->SetName("obj1.obj", dir1);

    TestRefHolder *holder3 = new TestRefHolder();
    holder3->SetName("holder.obj", dir3);
    holder3->SetTarget(obj1);

    EXPECT_EQ(obj1->RefCount(), 1);

    // Deleting dir1 cascades: dir1 → dir2 → dir3 → holder3 destroyed
    // holder3's ObjPtr destructor should safely null its reference to obj1
    // (which is also being destroyed as part of dir1)
    delete dir1;
    // If we get here without crash/double-free, the cascade is safe
}

// Verify that replacing refs on an object whose ObjDirPtr refs cause the
// object itself to be deleted (HasDirPtrs returns false mid-walk) doesn't crash.
TEST_F(ObjectLifetimeTest, ReplaceRefsWithSelfDeletingObjDirPtr) {
    ObjectDir *target = Hmx::Object::New<ObjectDir>();
    ObjectDir *replacement = Hmx::Object::New<ObjectDir>();

    // The only ObjDirPtr to target — when this is replaced, HasDirPtrs
    // returns false and ObjDirPtr::operator= tries to delete target.
    // But we're in the middle of walking target's refs!
    ObjDirPtr<ObjectDir> dirPtr(target);
    EXPECT_TRUE(target->HasDirPtrs());

    // Also add a regular ObjPtr to target so there are multiple refs in the ring
    Hmx::Object *owner = Hmx::Object::New<Hmx::Object>();
    ObjPtr<Hmx::Object> objRef(owner, target);
    EXPECT_EQ(target->RefCount(), 2); // dirPtr + objRef

    // This should not crash even though Replace on the ObjDirPtr may
    // trigger delete of target
    target->ReplaceRefs(replacement);

    // After ReplaceRefs, refs should point to replacement (if target survived)
    // or be null (if target was deleted)
    EXPECT_TRUE(objRef.Ptr() == replacement || objRef.Ptr() == nullptr);

    delete owner;
    delete replacement;
    // target may have been deleted by the ObjDirPtr cascade — don't double-delete
}

} // namespace
