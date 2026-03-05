// Native lifetime/merge safety regression tests.
#include "test_helpers.h"

#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Object.h"
#include "obj/Utl.h"

namespace {

class ObjectLifetimeTest : public EngineTestFixture {};

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
    EXPECT_TRUE(HmxObjectIsLive(ref.Ptr()));

    delete fromDir;

    EXPECT_NE(ref.Ptr(), nullptr);
    EXPECT_TRUE(HmxObjectIsLive(ref.Ptr()));
    EXPECT_EQ(toDir->FindObject("dup.obj", false, true), ref.Ptr());

    int liveEntries = 0;
    for (ObjectDir::Entry *e = toDir->HashTable().Begin(); e != nullptr;
         e = toDir->HashTable().Next(e)) {
        if (e->obj) {
            EXPECT_TRUE(HmxObjectIsLive(e->obj)) << "Dead pointer for entry " << e->name;
            liveEntries++;
        }
    }
    EXPECT_GE(liveEntries, 1);

    // Iterator should walk without touching dead objects.
    int itrCount = 0;
    for (ObjDirItr<Hmx::Object> it(toDir, false); it != nullptr; ++it) {
        EXPECT_TRUE(HmxObjectIsLive(&*it));
        itrCount++;
    }
    EXPECT_GE(itrCount, 1);

    delete refOwner;
    delete toDir;
}

// Safety regression: iterator should never return dead entries.
TEST_F(ObjectLifetimeTest, ObjDirItrSkipsDeadHashEntries) {
    ExposedDir *dir = new ExposedDir();

    Hmx::Object *victim = Hmx::Object::New<Hmx::Object>();
    victim->SetName("victim.obj", dir);

    // Keep a stale pointer, then force it back into hash table to simulate corruption.
    Hmx::Object *stale = victim;
    delete victim;
    ASSERT_FALSE(HmxObjectIsLive(stale));

    ObjectDir::Entry *entry = dir->ExposeFindEntry("victim.obj", true);
    ASSERT_NE(entry, nullptr);
    entry->obj = stale;

    int itrCount = 0;
    for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it) {
        // If we ever visit stale, live check will fail.
        EXPECT_TRUE(HmxObjectIsLive(&*it));
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
            EXPECT_TRUE(HmxObjectIsLive(e->obj)) << "Dead pointer for entry " << e->name;
            liveEntries++;
        }
    }
    EXPECT_GT(liveEntries, 0);

    int itrCount = 0;
    for (ObjDirItr<Hmx::Object> it(toDir, false); it != nullptr; ++it) {
        EXPECT_TRUE(HmxObjectIsLive(&*it));
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
            EXPECT_TRUE(HmxObjectIsLive(&*it));
            itrCount++;
            ASSERT_LT(itrCount, 30000);
        }
        EXPECT_GT(itrCount, 0);
    }

    delete base;
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
    delete toDir;
}

} // namespace
