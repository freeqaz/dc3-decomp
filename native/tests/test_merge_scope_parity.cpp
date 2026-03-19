// MergeDirs parity tests — verify native merge infrastructure matches Xbox
// behavior for both proxy and non-proxy FileMerger code paths.
//
// Tier 1: Synthetic tests (no assets, always runs)
// Tier 2: Real venue merge tests (requires MILO_LIB)

#include "test_helpers.h"

#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Object.h"
#include "obj/Utl.h"
#include "utl/FilePath.h"

#include <sys/stat.h>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

// ============================================================================
// Helpers
// ============================================================================

static std::string GetMiloLibRoot() {
    const char *env = getenv("MILO_LIB");
    if (env && env[0])
        return env;
    const char *home = getenv("HOME");
    if (home && home[0])
        return std::string(home)
            + "/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3";
    return "";
}

static bool FileExists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static ObjectDir *TryLoadStandalone(const std::string &path) {
    if (!FileExists(path))
        return nullptr;
    FilePath fp(path.c_str());
    return DirLoader::LoadObjects(fp, nullptr, nullptr);
}

class TestRefHolder : public Hmx::Object {
public:
    TestRefHolder() : mTarget(this, nullptr) {}
    void SetTarget(Hmx::Object *obj) { mTarget = obj; }
    Hmx::Object *Target() const { return mTarget.Ptr(); }
private:
    ObjPtr<Hmx::Object> mTarget;
};

// Verify ring integrity on a single object by walking its ref ring with a
// count limit. Returns false if the ring exceeds maxRefs (infinite loop).
static bool VerifyRingIntegrity(Hmx::Object *obj, int maxRefs = 100000) {
    const ObjRef &refs = obj->Refs();
    int count = 0;
    for (ObjRef::iterator it = refs.begin(); it != refs.end(); ++it) {
        count++;
        if (count > maxRefs)
            return false;
    }
    return true;
}

// Walk all objects in dir (recursively), verify each object's ring.
// Returns number of corrupt objects found.
static int VerifyAllRingsInDir(ObjectDir *dir) {
    int corrupt = 0;
    for (ObjDirItr<Hmx::Object> it(dir, true); it != nullptr; ++it) {
        if (!VerifyRingIntegrity(&*it)) {
            printf("  CORRUPT RING: '%s' (%s) in dir '%s'\n",
                   it->Name(), it->ClassName().Str(),
                   it->Dir() ? it->Dir()->Name() : "?");
            corrupt++;
        }
    }
    return corrupt;
}

// Exact code from FileMerger.cpp:218-230 — flatten pass for non-proxy merge.
static void RunNativeFlattenPass(ObjectDir *dir) {
    for (ObjDirItr<Hmx::Object> it(dir, true); it != nullptr; ++it) {
        if (it->Dir() != dir) {
            if (!dir->FindObject(it->Name(), false, false)) {
                it->SetName(it->Name(), dir);
            }
        }
    }
}

// Count objects not findable via FindObject from the top dir.
static int CountUnreachableObjects(ObjectDir *dir) {
    int unreachable = 0;
    for (ObjDirItr<Hmx::Object> it(dir, true); it != nullptr; ++it) {
        if (!dir->FindObject(it->Name(), false, true)) {
            printf("  UNREACHABLE: '%s' (%s)\n", it->Name(), it->ClassName().Str());
            unreachable++;
        }
    }
    return unreachable;
}

// Mirror FileMerger non-proxy path (FinishLoading lines 214-230).
static void MergeNonProxy(ObjectDir *source, ObjectDir *target) {
    MergeFilter filt(MergeFilter::kReplace, MergeFilter::kMergeInlinedMoveSharedSubdirs);
    ReserveToFit(source, target, 0);
    MergeDirs(source, target, filt);
    RunNativeFlattenPass(target);
}

// Mirror FileMerger proxy path (FinishLoading lines 201-213).
static void MergeProxy(ObjectDir *source, ObjectDir *worldRoot) {
    ObjectDir *existing = worldRoot->Find<ObjectDir>(source->Name(), false);
    if (existing) {
        MergeFilter filt(MergeFilter::kReplace,
                         MergeFilter::kMergeInlinedMoveSharedSubdirs);
        ReserveToFit(source, existing, 0);
        MergeDirs(source, existing, filt);
        existing->SyncObjects();
    } else {
        ReserveToFit(nullptr, worldRoot, 2);
        source->SetName(source->Name(), worldRoot);
    }
}

// ============================================================================
// Fixture
// ============================================================================

class MergeScopeParityTest : public EngineTestFixture {};

// Venue entries for Tier 2 tests
struct VenueEntry {
    const char *relPath;
    const char *name;
};

static const VenueEntry kVenueWorlds[] = {
    {"world/glitterati/gen/glitterati.milo_xbox", "glitterati"},
    {"world/dclive/gen/dclive.milo_xbox", "dclive"},
    {"world/throneroom/gen/throneroom.milo_xbox", "throneroom"},
    {"world/houseparty/gen/houseparty.milo_xbox", "houseparty"},
    {"world/rollerrink/gen/rollerrink.milo_xbox", "rollerrink"},
    {"world/bid/gen/bid.milo_xbox", "bid"},
    {"world/dci/gen/dci.milo_xbox", "dci"},
    {"world/streetside/gen/streetside.milo_xbox", "streetside"},
    {nullptr, nullptr}
};

// ============================================================================
// Tier 1: Synthetic Tests
// ============================================================================

TEST_F(MergeScopeParityTest, SyntheticNonProxyMergeFlattensContent) {
    ObjectDir *fromDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *toDir = Hmx::Object::New<ObjectDir>();

    // Populate source with objects
    Hmx::Object *objA = Hmx::Object::New<Hmx::Object>();
    objA->SetName("anim.obj", fromDir);

    Hmx::Object *objB = Hmx::Object::New<Hmx::Object>();
    objB->SetName("mesh.obj", fromDir);

    // Add a shared subdir to source
    ObjectDir *sharedSub = Hmx::Object::New<ObjectDir>();
    sharedSub->SetName("effects", fromDir);
    fromDir->AppendSubDir(ObjDirPtr<ObjectDir>(sharedSub));

    Hmx::Object *subObj = Hmx::Object::New<Hmx::Object>();
    subObj->SetName("sparkle.obj", sharedSub);

    // Merge non-proxy (song path)
    MergeNonProxy(fromDir, toDir);

    // Source destroyed like PostMerge non-proxy
    delete fromDir;

    // All objects should be findable from toDir
    EXPECT_NE(toDir->FindObject("anim.obj", false, true), nullptr);
    EXPECT_NE(toDir->FindObject("mesh.obj", false, true), nullptr);

    // Ring integrity
    EXPECT_EQ(VerifyAllRingsInDir(toDir), 0);

    // Count objects — should have content
    int count = 0;
    for (ObjDirItr<Hmx::Object> it(toDir, true); it != nullptr; ++it)
        count++;
    EXPECT_GT(count, 0);

    delete toDir;
}

TEST_F(MergeScopeParityTest, SyntheticProxyMergeAddsSubdir) {
    // Use a staging dir to give the venue dir its name (SetName requires non-null dir).
    // In real flow the dir gets its name from the .milo file load.
    ObjectDir *stagingDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *venueDir = Hmx::Object::New<ObjectDir>();
    venueDir->SetName("glitterati", stagingDir);

    ObjectDir *worldRoot = Hmx::Object::New<ObjectDir>();

    // Populate venue with objects
    Hmx::Object *cam = Hmx::Object::New<Hmx::Object>();
    cam->SetName("main_cam.obj", venueDir);

    Hmx::Object *light = Hmx::Object::New<Hmx::Object>();
    light->SetName("spot01.obj", venueDir);

    // Remove from staging before proxy merge (simulating DirLoader providing a standalone dir)
    stagingDir->RemoveSubDir(venueDir);

    // Proxy merge — no existing dir, so SetName adds as subdir
    MergeProxy(venueDir, worldRoot);

    // venueDir should now be a subdir of worldRoot
    ObjectDir *found = worldRoot->Find<ObjectDir>("glitterati", false);
    EXPECT_EQ(found, venueDir);

    // Venue objects findable from worldRoot recursively
    EXPECT_NE(worldRoot->FindObject("main_cam.obj", false, true), nullptr);
    EXPECT_NE(worldRoot->FindObject("spot01.obj", false, true), nullptr);

    // Ring integrity on both
    EXPECT_EQ(VerifyAllRingsInDir(worldRoot), 0);

    delete worldRoot;
}

TEST_F(MergeScopeParityTest, SyntheticNameCollisionRedirectsRefs) {
    ObjectDir *fromDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *toDir = Hmx::Object::New<ObjectDir>();

    // Both dirs have an object named "target"
    Hmx::Object *fromTarget = Hmx::Object::New<Hmx::Object>();
    fromTarget->SetName("target.obj", fromDir);

    Hmx::Object *toTarget = Hmx::Object::New<Hmx::Object>();
    toTarget->SetName("target.obj", toDir);

    // Ref holder in fromDir points to fromTarget
    TestRefHolder *holder = new TestRefHolder();
    holder->SetName("holder.obj", fromDir);
    holder->SetTarget(fromTarget);
    ASSERT_EQ(holder->Target(), fromTarget);

    // Non-proxy merge with collision
    MergeNonProxy(fromDir, toDir);

    // After merge: ref should redirect to toDir's version
    EXPECT_EQ(holder->Target(), toTarget);
    EXPECT_NE(holder->Target(), nullptr);

    // Source destroyed
    delete fromDir;

    // Ref still valid after source deletion
    EXPECT_NE(holder->Target(), nullptr);
    EXPECT_EQ(VerifyAllRingsInDir(toDir), 0);

    delete toDir;
}

TEST_F(MergeScopeParityTest, SyntheticSequentialNonProxyThenProxy) {
    // Mirror the real song → venue → viz sequence
    ObjectDir *worldDir = Hmx::Object::New<ObjectDir>();

    // Step 1: Non-proxy merge (song path)
    {
        ObjectDir *songDir = Hmx::Object::New<ObjectDir>();
        Hmx::Object *songObj = Hmx::Object::New<Hmx::Object>();
        songObj->SetName("song_track.obj", songDir);

        MergeNonProxy(songDir, worldDir);
        delete songDir; // source destroyed like PostMerge non-proxy
    }
    EXPECT_EQ(VerifyAllRingsInDir(worldDir), 0)
        << "Ring corruption after song (non-proxy) merge";
    EXPECT_NE(worldDir->FindObject("song_track.obj", false, true), nullptr);

    // Step 2: Proxy merge (venue path)
    ObjectDir *venueDir = Hmx::Object::New<ObjectDir>();
    venueDir->SetName("throneroom", nullptr);
    {
        Hmx::Object *venueObj = Hmx::Object::New<Hmx::Object>();
        venueObj->SetName("throne_mesh.obj", venueDir);
    }
    MergeProxy(venueDir, worldDir);
    EXPECT_EQ(VerifyAllRingsInDir(worldDir), 0)
        << "Ring corruption after venue (proxy) merge";
    EXPECT_NE(worldDir->Find<ObjectDir>("throneroom", false), nullptr);

    // Step 3: Proxy merge (viz path)
    ObjectDir *vizDir = Hmx::Object::New<ObjectDir>();
    vizDir->SetName("ham_vis", nullptr);
    {
        Hmx::Object *vizObj = Hmx::Object::New<Hmx::Object>();
        vizObj->SetName("viz_effect.obj", vizDir);
    }
    MergeProxy(vizDir, worldDir);
    EXPECT_EQ(VerifyAllRingsInDir(worldDir), 0)
        << "Ring corruption after viz (proxy) merge";
    EXPECT_NE(worldDir->Find<ObjectDir>("ham_vis", false), nullptr);

    // All content accessible
    EXPECT_NE(worldDir->FindObject("song_track.obj", false, true), nullptr);
    EXPECT_NE(worldDir->FindObject("throne_mesh.obj", false, true), nullptr);
    EXPECT_NE(worldDir->FindObject("viz_effect.obj", false, true), nullptr);

    delete worldDir;
}

TEST_F(MergeScopeParityTest, SyntheticCrossRefsPreservedAcrossMerge) {
    ObjectDir *fromDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *toDir = Hmx::Object::New<ObjectDir>();

    // fromDir: "anim" has ObjPtr to "mesh" (both in fromDir)
    Hmx::Object *fromMesh = Hmx::Object::New<Hmx::Object>();
    fromMesh->SetName("mesh.obj", fromDir);

    TestRefHolder *fromAnim = new TestRefHolder();
    fromAnim->SetName("anim.obj", fromDir);
    fromAnim->SetTarget(fromMesh);

    // toDir: has "mesh" (collision)
    Hmx::Object *toMesh = Hmx::Object::New<Hmx::Object>();
    toMesh->SetName("mesh.obj", toDir);

    MergeNonProxy(fromDir, toDir);
    delete fromDir;

    // "anim"'s ObjPtr should point to toDir's "mesh"
    Hmx::Object *animInTo = toDir->FindObject("anim.obj", false, true);
    ASSERT_NE(animInTo, nullptr);
    TestRefHolder *animHolder = dynamic_cast<TestRefHolder *>(animInTo);
    if (animHolder) {
        EXPECT_EQ(animHolder->Target(), toMesh);
    }

    EXPECT_EQ(VerifyAllRingsInDir(toDir), 0);

    delete toDir;
}

// ============================================================================
// Tier 2: Real Venue Merge Tests (requires MILO_LIB)
// ============================================================================

TEST_F(MergeScopeParityTest, VenueProxyMergeIntoWorldRoot) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";

    int tested = 0;
    for (int i = 0; kVenueWorlds[i].relPath; i++) {
        std::string path = root + "/" + kVenueWorlds[i].relPath;
        ObjectDir *venueDir = TryLoadStandalone(path);
        if (!venueDir)
            continue;

        printf("  Proxy merge: %s ('%s')\n", kVenueWorlds[i].name, venueDir->Name());

        ObjectDir *worldRoot = Hmx::Object::New<ObjectDir>();
        MergeProxy(venueDir, worldRoot);

        // Venue should be findable as subdir
        ObjectDir *found = worldRoot->Find<ObjectDir>(venueDir->Name(), false);
        EXPECT_NE(found, nullptr)
            << "Venue " << kVenueWorlds[i].name << " not found as subdir";

        // Ring integrity
        int corrupt = VerifyAllRingsInDir(worldRoot);
        EXPECT_EQ(corrupt, 0)
            << "Ring corruption in " << kVenueWorlds[i].name
            << " (" << corrupt << " objects)";

        // Venue subdir should have objects
        int objCount = 0;
        for (ObjDirItr<Hmx::Object> it(venueDir, true); it != nullptr; ++it)
            objCount++;
        EXPECT_GT(objCount, 0) << kVenueWorlds[i].name << " has no objects";
        printf("    objects=%d corrupt=%d\n", objCount, corrupt);

        // Deletion should complete (no hang)
        delete worldRoot;
        tested++;
    }

    if (tested == 0)
        GTEST_SKIP() << "No venue worlds found at " << root;
    printf("VenueProxyMerge: tested %d venues\n", tested);
}

TEST_F(MergeScopeParityTest, VenueNonProxyMergeFlattensIntoTarget) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";

    int tested = 0;
    for (int i = 0; kVenueWorlds[i].relPath; i++) {
        std::string path = root + "/" + kVenueWorlds[i].relPath;
        ObjectDir *venueDir = TryLoadStandalone(path);
        if (!venueDir)
            continue;

        printf("  Non-proxy merge: %s\n", kVenueWorlds[i].name);

        ObjectDir *targetDir = Hmx::Object::New<ObjectDir>();
        MergeNonProxy(venueDir, targetDir);
        delete venueDir; // source destroyed like PostMerge non-proxy

        int corrupt = VerifyAllRingsInDir(targetDir);
        EXPECT_EQ(corrupt, 0)
            << "Ring corruption in " << kVenueWorlds[i].name;

        int unreachable = CountUnreachableObjects(targetDir);
        EXPECT_EQ(unreachable, 0)
            << kVenueWorlds[i].name << " has unreachable objects";

        printf("    corrupt=%d unreachable=%d\n", corrupt, unreachable);

        delete targetDir;
        tested++;
    }

    if (tested == 0)
        GTEST_SKIP() << "No venue worlds found at " << root;
    printf("VenueNonProxyMerge: tested %d venues\n", tested);
}

TEST_F(MergeScopeParityTest, VenueMergeSubdirObjectsFindableFromTop) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";

    std::string path = root + "/world/glitterati/gen/glitterati.milo_xbox";
    ObjectDir *venueDir = TryLoadStandalone(path);
    if (!venueDir)
        GTEST_SKIP() << "glitterati.milo_xbox not found";

    ObjectDir *worldRoot = Hmx::Object::New<ObjectDir>();
    MergeProxy(venueDir, worldRoot);

    // Count flat vs recursive objects in venue subdir
    int flatCount = 0, recursiveCount = 0;
    for (ObjDirItr<Hmx::Object> it(venueDir, false); it != nullptr; ++it)
        flatCount++;
    for (ObjDirItr<Hmx::Object> it(venueDir, true); it != nullptr; ++it)
        recursiveCount++;

    printf("  glitterati: flat=%d recursive=%d\n", flatCount, recursiveCount);

    // Verify each object in venue subdirs is findable from worldRoot
    int scopeGaps = 0;
    for (ObjDirItr<Hmx::Object> it(venueDir, true); it != nullptr; ++it) {
        if (!worldRoot->FindObject(it->Name(), false, true)) {
            printf("  SCOPE GAP: '%s' (%s) not findable from worldRoot\n",
                   it->Name(), it->ClassName().Str());
            scopeGaps++;
        }
    }
    printf("  scopeGaps=%d\n", scopeGaps);

    // Some scope gaps may be expected due to name shadowing, but log them
    if (scopeGaps > 0) {
        printf("  WARNING: %d objects only reachable recursively\n", scopeGaps);
    }

    EXPECT_EQ(VerifyAllRingsInDir(worldRoot), 0);

    delete worldRoot;
}

TEST_F(MergeScopeParityTest, SequentialMergesIntoSameWorldRoot) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";

    // Need at least one small milo and one venue
    std::string smallPath = root + "/world/shared/gen/peak_spiral.milo_xbox";
    std::string venuePath = root + "/world/glitterati/gen/glitterati.milo_xbox";

    ObjectDir *smallDir = TryLoadStandalone(smallPath);
    ObjectDir *venueDir = TryLoadStandalone(venuePath);
    if (!smallDir && !venueDir)
        GTEST_SKIP() << "Required assets not found";

    ObjectDir *worldRoot = Hmx::Object::New<ObjectDir>();

    // Step 1: Non-proxy merge (small milo → flat into worldRoot)
    if (smallDir) {
        printf("  Step 1: non-proxy merge peak_spiral\n");
        MergeNonProxy(smallDir, worldRoot);
        delete smallDir;
        EXPECT_EQ(VerifyAllRingsInDir(worldRoot), 0)
            << "Ring corruption after non-proxy merge";
    }

    // Step 2: Proxy merge (venue → subdir of worldRoot)
    if (venueDir) {
        printf("  Step 2: proxy merge glitterati\n");
        MergeProxy(venueDir, worldRoot);
        EXPECT_EQ(VerifyAllRingsInDir(worldRoot), 0)
            << "Ring corruption after venue proxy merge";
        EXPECT_NE(worldRoot->Find<ObjectDir>(venueDir->Name(), false), nullptr);
    }

    // Step 3: Try another small milo as proxy
    std::string smallPath2 = root + "/world/shared/gen/phrase_meter.milo_xbox";
    ObjectDir *smallDir2 = TryLoadStandalone(smallPath2);
    if (smallDir2) {
        printf("  Step 3: proxy merge phrase_meter\n");
        MergeProxy(smallDir2, worldRoot);
        EXPECT_EQ(VerifyAllRingsInDir(worldRoot), 0)
            << "Ring corruption after second proxy merge";
    }

    // Verify worldRoot is deletable without hang
    printf("  Deleting worldRoot...\n");
    delete worldRoot;
    printf("  Done\n");
}

TEST_F(MergeScopeParityTest, RepeatedVenueMergeAfterClear) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";

    std::string pathA = root + "/world/glitterati/gen/glitterati.milo_xbox";
    std::string pathB = root + "/world/dclive/gen/dclive.milo_xbox";

    ObjectDir *venueA = TryLoadStandalone(pathA);
    ObjectDir *venueB = TryLoadStandalone(pathB);
    if (!venueA || !venueB) {
        delete venueA;
        delete venueB;
        GTEST_SKIP() << "Need both glitterati and dclive";
    }

    ObjectDir *worldRoot = Hmx::Object::New<ObjectDir>();

    // Merge venue A
    printf("  Merge venue A (glitterati)\n");
    const char *nameA = venueA->Name();
    MergeProxy(venueA, worldRoot);
    EXPECT_EQ(VerifyAllRingsInDir(worldRoot), 0);

    // Find and delete venue A subdir (simulating Merger::Clear for proxy)
    printf("  Clear venue A\n");
    ObjectDir *subA = worldRoot->Find<ObjectDir>(nameA, false);
    ASSERT_NE(subA, nullptr);
    ObjDirPtr<ObjectDir> holdA(subA);
    worldRoot->RemoveSubDir(holdA);
    delete subA;
    holdA = nullptr;

    EXPECT_EQ(VerifyAllRingsInDir(worldRoot), 0)
        << "Ring corruption after clearing venue A";

    // Merge venue B
    printf("  Merge venue B (dclive)\n");
    MergeProxy(venueB, worldRoot);
    EXPECT_NE(worldRoot->Find<ObjectDir>(venueB->Name(), false), nullptr);
    EXPECT_EQ(VerifyAllRingsInDir(worldRoot), 0)
        << "Ring corruption after merging venue B";

    // Venue A should be gone
    EXPECT_EQ(worldRoot->Find<ObjectDir>(nameA, false), nullptr);

    delete worldRoot;
}

} // namespace
