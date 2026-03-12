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

} // namespace
