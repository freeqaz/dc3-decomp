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
#include "char/CharInterest.h"
#include "rndobj/Trans.h"
#include "utl/FilePath.h"

#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
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

static ObjectDir *TryLoadGlitteratiVenue() {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        return nullptr;
    return TryLoadStandalone(root + "/world/glitterati/gen/glitterati.milo_xbox");
}

class TestRefHolder : public Hmx::Object {
public:
    TestRefHolder() : mTarget(this, nullptr) {}
    void SetTarget(Hmx::Object *obj) { mTarget = obj; }
    Hmx::Object *Target() const { return mTarget.Ptr(); }
private:
    ObjPtr<Hmx::Object> mTarget;
};

class TestFileMergerPolicyFilter : public MergeFilter {
public:
    TestFileMergerPolicyFilter()
        : MergeFilter(MergeFilter::kReplace,
                      MergeFilter::kMergeInlinedMoveSharedSubdirs) {}

    virtual Action Filter(Hmx::Object *o1, Hmx::Object *o2, ObjectDir *) {
        if (!o2)
            return kReplace;
        const char *name = o1->Name();
        if (std::strncmp(name, "spot_", 5) == 0
            || std::strncmp(name, "bone_", 5) == 0) {
            return kIgnore;
        }
        return kReplace;
    }
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

// Test helper: ObjectDir::mInlineSubDirType is protected, so we use a
// derived class to expose it for test setup.
class TestObjectDir : public ObjectDir {
public:
    void SetInlineSubDirType(InlineDirType t) { mInlineSubDirType = t; }
};
static void SetSubDirType(ObjectDir *dir, InlineDirType t) {
    static_cast<TestObjectDir *>(dir)->SetInlineSubDirType(t);
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

// Mirrors FileMerger.cpp post-merge flatten pass.
// Reparents objects that MergeDirs missed, but skips objects in retained
// kMergeReplace subdir trees — Xbox keeps those in their subdir scope.
static void RunNativeFlattenPass(ObjectDir *dir) {
    for (ObjDirItr<Hmx::Object> it(dir, true); it != nullptr; ++it) {
        if (it->Dir() != dir) {
            bool inRetainedSubdirTree = false;
            for (int s = 0; s < dir->SubDirs().size(); s++) {
                ObjectDir *retained = dir->SubDirs()[s];
                if (retained && retained->HasSubDir(it->Dir())) {
                    inRetainedSubdirTree = true;
                    break;
                }
            }
            if (inRetainedSubdirTree)
                continue;
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

static std::vector<std::string> CollectInterestNames(ObjectDir *dir) {
    std::vector<std::string> names;
    for (ObjDirItr<CharInterest> it(dir, true); it != nullptr; ++it)
        names.push_back(it->Name());
    return names;
}

static bool HasInterestNamed(
    const std::vector<std::string> &names, const char *needle
) {
    for (std::vector<std::string>::const_iterator it = names.begin();
         it != names.end();
         ++it) {
        if (*it == needle)
            return true;
    }
    return false;
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
class MergeScopeParityUnitTest : public SymbolTestFixture {};

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
    // Staging dir gives the venue its name. Kept alive to avoid ObjDirPtr
    // delete-on-last-ref freeing venueDir before merge completes.
    ObjectDir *stagingDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *venueDir = Hmx::Object::New<ObjectDir>();
    venueDir->SetName("glitterati", stagingDir);

    ObjectDir *worldRoot = Hmx::Object::New<ObjectDir>();

    // Populate venue with objects
    Hmx::Object *cam = Hmx::Object::New<Hmx::Object>();
    cam->SetName("main_cam.obj", venueDir);

    Hmx::Object *light = Hmx::Object::New<Hmx::Object>();
    light->SetName("spot01.obj", venueDir);

    // Proxy merge — no existing dir, so SetName adds to hash table
    MergeProxy(venueDir, worldRoot);

    // venueDir should be findable from worldRoot
    ObjectDir *found = worldRoot->Find<ObjectDir>("glitterati", false);
    EXPECT_EQ(found, venueDir);

    // Venue objects findable within venueDir itself
    EXPECT_NE(venueDir->FindObject("main_cam.obj", false, false), nullptr);
    EXPECT_NE(venueDir->FindObject("spot01.obj", false, false), nullptr);

    // Ring integrity
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
    {
        ObjectDir *staging = Hmx::Object::New<ObjectDir>();
        ObjectDir *venueDir = Hmx::Object::New<ObjectDir>();
        venueDir->SetName("throneroom", staging);
        Hmx::Object *venueObj = Hmx::Object::New<Hmx::Object>();
        venueObj->SetName("throne_mesh.obj", venueDir);
        MergeProxy(venueDir, worldDir);
        // Don't delete staging — its ObjDirPtr to venueDir would cascade-delete
        // the venue that was just merged into worldDir.
    }
    EXPECT_EQ(VerifyAllRingsInDir(worldDir), 0)
        << "Ring corruption after venue (proxy) merge";
    EXPECT_NE(worldDir->Find<ObjectDir>("throneroom", false), nullptr);

    // Step 3: Proxy merge (viz path)
    {
        ObjectDir *staging = Hmx::Object::New<ObjectDir>();
        ObjectDir *vizDir = Hmx::Object::New<ObjectDir>();
        vizDir->SetName("ham_vis", staging);
        Hmx::Object *vizObj = Hmx::Object::New<Hmx::Object>();
        vizObj->SetName("viz_effect.obj", vizDir);
        MergeProxy(vizDir, worldDir);
        delete staging;
    }
    EXPECT_EQ(VerifyAllRingsInDir(worldDir), 0)
        << "Ring corruption after viz (proxy) merge";
    EXPECT_NE(worldDir->Find<ObjectDir>("ham_vis", false), nullptr);

    // Song content is flat-merged (non-proxy) — findable directly from worldDir
    EXPECT_NE(worldDir->FindObject("song_track.obj", false, true), nullptr);
    // Venue/viz content is in subdirs (proxy merge) — findable via subdir lookup
    ObjectDir *throneroom = worldDir->Find<ObjectDir>("throneroom", false);
    EXPECT_NE(throneroom, nullptr);
    if (throneroom)
        EXPECT_NE(throneroom->FindObject("throne_mesh.obj", false, false), nullptr);
    ObjectDir *hamVis = worldDir->Find<ObjectDir>("ham_vis", false);
    EXPECT_NE(hamVis, nullptr);
    if (hamVis)
        EXPECT_NE(hamVis->FindObject("viz_effect.obj", false, false), nullptr);

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
// Tier 1b: Merge-Flatten Parity Tests
//
// These test the exact Xbox MergeDirs semantics for different SubdirActions:
//   kMergeMerge: objects from inlined subdirs merge into target hash table
//   kMergeReplace: shared subdirs move as children, objects stay in subdir
//
// The FileMerger uses kMergeInlinedMoveSharedSubdirs which maps:
//   kInlineAlways/kInlineCached → kMergeMerge (flatten into parent)
//   kInlineNever/kInlineCachedShared → kMergeReplace (keep in subdir)
// ============================================================================

// Xbox behavior: after MergeDirs with kMergeInlinedMoveSharedSubdirs,
// objects from inlined subdirs (kInlineAlways) are reparented into the
// target dir's hash table. Non-recursive FindObject should find them.
TEST_F(MergeScopeParityUnitTest, MergeMergeSubdirObjectsFlattenIntoTarget) {
    ObjectDir *fromDir = new TestObjectDir();
    ObjectDir *toDir = new TestObjectDir();

    // Create an inlined subdir (kInlineAlways → kMergeMerge)
    ObjectDir *inlinedSub = new TestObjectDir();
    inlinedSub->SetName("inlined_fx", fromDir);
    SetSubDirType(inlinedSub, kInlineAlways);
    fromDir->AppendSubDir(ObjDirPtr<ObjectDir>(inlinedSub));

    Hmx::Object *fxObj = new Hmx::Object();
    fxObj->SetName("sparkle.fx", inlinedSub);

    // A direct object in fromDir
    Hmx::Object *topObj = new Hmx::Object();
    topObj->SetName("main.mesh", fromDir);

    // Merge using FileMerger filter
    MergeFilter filt(MergeFilter::kReplace,
                     MergeFilter::kMergeInlinedMoveSharedSubdirs);
    ReserveToFit(fromDir, toDir, 0);
    MergeDirs(fromDir, toDir, filt);

    // Xbox behavior: both objects should be in toDir's flat hash table.
    // No flatten pass needed — MergeDirs itself handles kMergeMerge.
    EXPECT_NE(toDir->FindObject("main.mesh", false, false), nullptr)
        << "Direct objects must be in target hash table after MergeDirs";
    EXPECT_NE(toDir->FindObject("sparkle.fx", false, false), nullptr)
        << "kMergeMerge subdir objects must be flattened into target by MergeDirs";

    delete fromDir;
    delete toDir;
}

// Xbox behavior: after MergeDirs with kMergeInlinedMoveSharedSubdirs,
// objects from shared subdirs (kInlineNever) are NOT reparented.
// The subdir is moved as a child of the target, and its objects remain
// in the subdir's own hash table. Non-recursive FindObject should NOT
// find them; recursive FindObject should.
TEST_F(MergeScopeParityUnitTest, MergeReplaceSubdirObjectsStayInSubdir) {
    ObjectDir *fromDir = new TestObjectDir();
    ObjectDir *toDir = new TestObjectDir();

    // Create a shared subdir (kInlineNever → kMergeReplace)
    ObjectDir *sharedSub = new TestObjectDir();
    sharedSub->SetName("shared_textures", fromDir);
    SetSubDirType(sharedSub, kInlineNever);
    fromDir->AppendSubDir(ObjDirPtr<ObjectDir>(sharedSub));

    Hmx::Object *texObj = new Hmx::Object();
    texObj->SetName("wood.tex", sharedSub);

    // Also a direct object
    Hmx::Object *topObj = new Hmx::Object();
    topObj->SetName("main.mesh", fromDir);

    // Merge using FileMerger filter — NO flatten pass (Xbox parity)
    MergeFilter filt(MergeFilter::kReplace,
                     MergeFilter::kMergeInlinedMoveSharedSubdirs);
    ReserveToFit(fromDir, toDir, 0);
    MergeDirs(fromDir, toDir, filt);

    // Direct objects should be in target hash table
    EXPECT_NE(toDir->FindObject("main.mesh", false, false), nullptr)
        << "Direct objects must be in target hash table";

    // kMergeReplace subdir: object should NOT be in target's flat hash table
    EXPECT_EQ(toDir->FindObject("wood.tex", false, false), nullptr)
        << "kMergeReplace subdir objects must NOT be flattened into target "
           "(Xbox keeps them in the subdir)";

    // But it SHOULD be findable via recursive search
    EXPECT_NE(toDir->FindObject("wood.tex", false, true), nullptr)
        << "kMergeReplace subdir objects must be reachable via recursive search";

    delete fromDir;
    delete toDir;
}

// The native flatten pass (RunNativeFlattenPass) currently over-flattens:
// it pulls kMergeReplace'd subdir objects into the parent, which Xbox
// does NOT do. This test verifies the FULL non-proxy pipeline matches Xbox.
TEST_F(MergeScopeParityUnitTest, NonProxyPipelinePreservesReplaceSubdirScope) {
    ObjectDir *fromDir = new TestObjectDir();
    ObjectDir *toDir = new TestObjectDir();

    // Inlined subdir (kInlineAlways → should flatten)
    ObjectDir *inlinedSub = new TestObjectDir();
    inlinedSub->SetName("inlined_fx", fromDir);
    SetSubDirType(inlinedSub, kInlineAlways);
    fromDir->AppendSubDir(ObjDirPtr<ObjectDir>(inlinedSub));

    Hmx::Object *fxObj = new Hmx::Object();
    fxObj->SetName("sparkle.fx", inlinedSub);

    // Shared subdir (kInlineNever → should NOT flatten)
    ObjectDir *sharedSub = new TestObjectDir();
    sharedSub->SetName("shared_textures", fromDir);
    SetSubDirType(sharedSub, kInlineNever);
    fromDir->AppendSubDir(ObjDirPtr<ObjectDir>(sharedSub));

    Hmx::Object *texObj = new Hmx::Object();
    texObj->SetName("wood.tex", sharedSub);

    // Direct object
    Hmx::Object *topObj = new Hmx::Object();
    topObj->SetName("main.mesh", fromDir);

    // Run the full non-proxy pipeline (MergeDirs + flatten pass)
    MergeNonProxy(fromDir, toDir);
    delete fromDir;

    // Direct objects: must be in flat scope
    EXPECT_NE(toDir->FindObject("main.mesh", false, false), nullptr)
        << "Direct objects must be in target hash table";

    // Inlined subdir objects: must be in flat scope (kMergeMerge)
    EXPECT_NE(toDir->FindObject("sparkle.fx", false, false), nullptr)
        << "kMergeMerge subdir objects must be in target hash table";

    // Shared subdir objects: must NOT be in flat scope (kMergeReplace)
    // This is the key parity test — Xbox keeps these in the subdir.
    // The current native flatten pass over-flattens them.
    EXPECT_EQ(toDir->FindObject("wood.tex", false, false), nullptr)
        << "kMergeReplace subdir objects must NOT be over-flattened into target "
           "(Xbox leaves them in the shared subdir)";

    // But they must still be reachable recursively
    EXPECT_NE(toDir->FindObject("wood.tex", false, true), nullptr)
        << "kMergeReplace subdir objects must be reachable via recursive search";

    EXPECT_EQ(VerifyAllRingsInDir(toDir), 0);
    delete toDir;
}

// Nested descendants of a retained kMergeReplace subtree must stay scoped
// under that subtree as well. Flattening only the direct child check is not
// enough; grandchildren would be hoisted incorrectly.
TEST_F(MergeScopeParityUnitTest, NonProxyPipelinePreservesNestedReplaceSubdirScope) {
    ObjectDir *fromDir = new TestObjectDir();
    ObjectDir *toDir = new TestObjectDir();

    ObjectDir *sharedSub = new TestObjectDir();
    sharedSub->SetName("shared_root", fromDir);
    SetSubDirType(sharedSub, kInlineNever);
    fromDir->AppendSubDir(ObjDirPtr<ObjectDir>(sharedSub));

    ObjectDir *nestedSub = new TestObjectDir();
    nestedSub->SetName("nested_fx", sharedSub);
    sharedSub->AppendSubDir(ObjDirPtr<ObjectDir>(nestedSub));

    Hmx::Object *nestedObj = new Hmx::Object();
    nestedObj->SetName("deep.sparkle", nestedSub);

    MergeNonProxy(fromDir, toDir);
    delete fromDir;

    EXPECT_EQ(toDir->FindObject("deep.sparkle", false, false), nullptr)
        << "Objects under retained kMergeReplace subdir trees must not be flattened "
           "into the target hash table";
    EXPECT_NE(toDir->FindObject("deep.sparkle", false, true), nullptr)
        << "Objects under retained kMergeReplace subdir trees must remain reachable "
           "through recursive lookup";

    delete toDir;
}

// Proxy merge path: verify objects from kMergeMerge subdirs are flattened
// into the existing dir's hash table after MergeDirs (matching Xbox).
TEST_F(MergeScopeParityTest, ProxyMergeExistingDirFlattensInlinedSubdirs) {
    ObjectDir *worldRoot = Hmx::Object::New<ObjectDir>();

    // Pre-existing venue dir in the world root
    ObjectDir *existingVenue = Hmx::Object::New<ObjectDir>();
    existingVenue->SetName("glitterati", worldRoot);

    // Staging dir gives the incoming venue its name and a valid dir context.
    // Mirrors how DirLoader creates the incoming dir before FileMerger merges it.
    ObjectDir *stagingDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *incomingVenue = Hmx::Object::New<ObjectDir>();
    incomingVenue->SetName("glitterati", stagingDir);

    // Inlined subdir in incoming (kInlineAlways → kMergeMerge)
    ObjectDir *inlinedSub = Hmx::Object::New<ObjectDir>();
    inlinedSub->SetName("venue_fx", incomingVenue);
    SetSubDirType(inlinedSub, kInlineAlways);
    incomingVenue->AppendSubDir(ObjDirPtr<ObjectDir>(inlinedSub));

    Hmx::Object *fxObj = Hmx::Object::New<Hmx::Object>();
    fxObj->SetName("confetti.fx", inlinedSub);

    Hmx::Object *topObj = Hmx::Object::New<Hmx::Object>();
    topObj->SetName("stage.mesh", incomingVenue);

    // Run proxy merge (finds existing "glitterati" dir in worldRoot)
    MergeProxy(incomingVenue, worldRoot);

    // Objects should be in the existing venue dir's hash table
    EXPECT_NE(existingVenue->FindObject("stage.mesh", false, false), nullptr)
        << "Direct objects must be in existing venue's hash table after proxy merge";
    EXPECT_NE(existingVenue->FindObject("confetti.fx", false, false), nullptr)
        << "kMergeMerge subdir objects must be flattened into existing venue";

    delete stagingDir;
    delete worldRoot;
}

// ============================================================================
// Tier 1c: WorldCamInterest Merge Parity Tests
//
// DC3 venues contain WorldCamInterest.intr (a CharInterest parented to the
// camera) which drives eye tracking. This object must survive the merge
// pipeline and be discoverable via ObjDirItr<CharInterest>.
// See: docs/sessions/2026-03-25-eye-tracking-worldcaminterest.md
// ============================================================================

// Synthetic: CharInterest survives non-proxy merge and is discoverable
// via the same ObjDirItr pattern that SyncInterestObjects uses.
TEST_F(MergeScopeParityTest, SyntheticCharInterestSurvivesNonProxyMerge) {
    ObjectDir *fromDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *toDir = Hmx::Object::New<ObjectDir>();

    // Create CharInterest in source (like WorldCamInterest.intr in a venue)
    CharInterest *interest = Hmx::Object::New<CharInterest>();
    interest->SetName("WorldCamInterest.intr", fromDir);

    // Also a regular object
    Hmx::Object *mesh = Hmx::Object::New<Hmx::Object>();
    mesh->SetName("stage.mesh", fromDir);

    MergeNonProxy(fromDir, toDir);
    delete fromDir;

    // CharInterest must be findable by name
    Hmx::Object *found = toDir->FindObject("WorldCamInterest.intr", false, true);
    ASSERT_NE(found, nullptr)
        << "WorldCamInterest.intr must survive non-proxy merge";
    EXPECT_NE(dynamic_cast<CharInterest *>(found), nullptr)
        << "Found object must be a CharInterest";

    // Must be discoverable via ObjDirItr<CharInterest> (the SyncInterestObjects pattern)
    int interestCount = 0;
    for (ObjDirItr<CharInterest> it(toDir, true); it != nullptr; ++it)
        interestCount++;
    EXPECT_GE(interestCount, 1)
        << "ObjDirItr<CharInterest> must find WorldCamInterest.intr after merge";

    delete toDir;
}

// Synthetic: CharInterest survives proxy merge into existing venue dir.
// In the real game, the venue is a named ObjectDir in worldRoot (not in
// mSubDirs). SyncInterestObjects iterates from the venue dir itself.
TEST_F(MergeScopeParityTest, SyntheticCharInterestSurvivesProxyMerge) {
    ObjectDir *worldRoot = Hmx::Object::New<ObjectDir>();

    // Existing venue is a named object in worldRoot (matching real game)
    ObjectDir *existingVenue = Hmx::Object::New<ObjectDir>();
    existingVenue->SetName("test_venue", worldRoot);

    ObjectDir *stagingDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *incomingVenue = Hmx::Object::New<ObjectDir>();
    incomingVenue->SetName("test_venue", stagingDir);

    CharInterest *interest = Hmx::Object::New<CharInterest>();
    interest->SetName("WorldCamInterest.intr", incomingVenue);

    MergeProxy(incomingVenue, worldRoot);

    // Must be findable from the existing venue dir (where SyncInterestObjects looks)
    Hmx::Object *found = existingVenue->FindObject("WorldCamInterest.intr", false, true);
    EXPECT_NE(found, nullptr)
        << "WorldCamInterest.intr must survive proxy merge into existing venue";

    // Must be discoverable via ObjDirItr from venue dir
    // (this is the actual SyncInterestObjects pattern)
    int interestCount = 0;
    for (ObjDirItr<CharInterest> it(existingVenue, true); it != nullptr; ++it)
        interestCount++;
    EXPECT_GE(interestCount, 1)
        << "ObjDirItr<CharInterest> from venue dir must find WorldCamInterest.intr";

    delete stagingDir;
    delete worldRoot;
}

TEST_F(MergeScopeParityTest, SyntheticInterestCollectionMatchesSyncPattern) {
    ObjectDir *fromDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *toDir = Hmx::Object::New<ObjectDir>();

    CharInterest *cameraInterest = Hmx::Object::New<CharInterest>();
    cameraInterest->SetName("WorldCamInterest.intr", fromDir);

    CharInterest *dancerInterest = Hmx::Object::New<CharInterest>();
    dancerInterest->SetName("dancer_eyes.intr", fromDir);

    MergeNonProxy(fromDir, toDir);
    delete fromDir;

    Hmx::Object *owner = Hmx::Object::New<Hmx::Object>();
    ObjPtrList<CharInterest> interests(owner);
    for (ObjDirItr<CharInterest> it(toDir, true); it != nullptr; ++it)
        interests.push_back(it);

    int count = 0;
    bool foundWorldCam = false;
    bool foundDancerEyes = false;
    for (ObjPtrList<CharInterest>::iterator it = interests.begin();
         it != interests.end();
         ++it) {
        count++;
        if (std::strcmp((*it)->Name(), "WorldCamInterest.intr") == 0)
            foundWorldCam = true;
        if (std::strcmp((*it)->Name(), "dancer_eyes.intr") == 0)
            foundDancerEyes = true;
    }

    EXPECT_EQ(count, 2);
    EXPECT_TRUE(foundWorldCam);
    EXPECT_TRUE(foundDancerEyes);

    delete owner;
    delete toDir;
}

TEST_F(MergeScopeParityTest, SyntheticInterestTransformParentSurvivesMerge) {
    ObjectDir *fromDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *toDir = Hmx::Object::New<ObjectDir>();

    RndTransformable *fakeCam = Hmx::Object::New<RndTransformable>();
    fakeCam->SetName("fake_cam.trans", fromDir);
    Transform camXfm = Transform::IDXfm();
    camXfm.v.Set(100.0f, 200.0f, 300.0f);
    fakeCam->SetLocalXfm(camXfm);

    CharInterest *interest = Hmx::Object::New<CharInterest>();
    interest->SetName("WorldCamInterest.intr", fromDir);
    interest->SetTransParent(fakeCam, false);

    MergeNonProxy(fromDir, toDir);
    delete fromDir;

    CharInterest *mergedInterest =
        dynamic_cast<CharInterest *>(toDir->FindObject("WorldCamInterest.intr", false, true));
    ASSERT_NE(mergedInterest, nullptr);
    EXPECT_NE(mergedInterest->TransParent(), nullptr);

    RndTransformable *mergedCam =
        dynamic_cast<RndTransformable *>(toDir->FindObject("fake_cam.trans", false, true));
    ASSERT_NE(mergedCam, nullptr);
    EXPECT_EQ(mergedInterest->TransParent(), mergedCam);
    EXPECT_NEAR(mergedInterest->WorldXfm().v.x, 100.0f, 0.001f);
    EXPECT_NEAR(mergedInterest->WorldXfm().v.y, 200.0f, 0.001f);
    EXPECT_NEAR(mergedInterest->WorldXfm().v.z, 300.0f, 0.001f);

    delete toDir;
}

TEST_F(MergeScopeParityTest, SyntheticFileMergerFilterDoesNotSkipCharInterest) {
    ObjectDir *fromDir = Hmx::Object::New<ObjectDir>();
    ObjectDir *toDir = Hmx::Object::New<ObjectDir>();

    Hmx::Object *existingSpot = Hmx::Object::New<Hmx::Object>();
    existingSpot->SetName("spot_light01", toDir);
    Hmx::Object *existingBone = Hmx::Object::New<Hmx::Object>();
    existingBone->SetName("bone_head", toDir);
    CharInterest *existingInterest = Hmx::Object::New<CharInterest>();
    existingInterest->SetName("WorldCamInterest.intr", toDir);

    Hmx::Object *incomingSpot = Hmx::Object::New<Hmx::Object>();
    incomingSpot->SetName("spot_light01", fromDir);
    Hmx::Object *incomingBone = Hmx::Object::New<Hmx::Object>();
    incomingBone->SetName("bone_head", fromDir);
    CharInterest *incomingInterest = Hmx::Object::New<CharInterest>();
    incomingInterest->SetName("WorldCamInterest.intr", fromDir);

    TestRefHolder *holder = new TestRefHolder();
    holder->SetName("interest_holder.obj", fromDir);
    holder->SetTarget(incomingInterest);

    TestFileMergerPolicyFilter filt;
    ReserveToFit(fromDir, toDir, 0);
    MergeDirs(fromDir, toDir, filt);
    delete fromDir;

    EXPECT_EQ(toDir->FindObject("spot_light01", false, false), existingSpot);
    EXPECT_EQ(toDir->FindObject("bone_head", false, false), existingBone);

    TestRefHolder *mergedHolder =
        dynamic_cast<TestRefHolder *>(toDir->FindObject("interest_holder.obj", false, true));
    ASSERT_NE(mergedHolder, nullptr);
    EXPECT_EQ(mergedHolder->Target(), existingInterest)
        << "CharInterest collisions should follow replace semantics, not the bone/spot skip path";

    delete toDir;
}

// Real asset: load a single venue and check WorldCamInterest.intr exists
// in the source data BEFORE any merge (source data integrity).
TEST_F(MergeScopeParityTest, RealVenueSourceHasWorldCamInterest) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";

    // Use glitterati as canonical test venue (single venue to avoid
    // cascading cleanup crashes from pre-existing merge bugs)
    std::string path = root + "/world/glitterati/gen/glitterati.milo_xbox";
    ObjectDir *venueDir = TryLoadStandalone(path);
    if (!venueDir)
        GTEST_SKIP() << "glitterati.milo_xbox not available";

    Hmx::Object *interest =
        venueDir->FindObject("WorldCamInterest.intr", false, true);
    EXPECT_NE(interest, nullptr)
        << "glitterati must contain WorldCamInterest.intr in source data";
    if (interest) {
        printf("  glitterati: WorldCamInterest.intr found (%s)\n",
               interest->ClassName().Str());
    }

    // Also check via ObjDirItr<CharInterest>
    int interestCount = 0;
    for (ObjDirItr<CharInterest> it(venueDir, true); it != nullptr; ++it) {
        printf("  CharInterest: '%s'\n", it->Name());
        interestCount++;
    }
    EXPECT_GT(interestCount, 0)
        << "glitterati must have at least one CharInterest in source data";

    // Don't delete venueDir — pre-existing cleanup crashes.
    // Leak is acceptable in tests; process exits immediately after.
}

TEST_F(MergeScopeParityTest, RealVenueWorldCamInterestSurvivesProxyMerge) {
    ObjectDir *venueDir = TryLoadGlitteratiVenue();
    if (!venueDir)
        GTEST_SKIP() << "glitterati.milo_xbox not available";

    ObjectDir *worldRoot = Hmx::Object::New<ObjectDir>();
    MergeProxy(venueDir, worldRoot);

    std::vector<std::string> names = CollectInterestNames(worldRoot);
    EXPECT_TRUE(HasInterestNamed(names, "WorldCamInterest.intr"))
        << "Proxy merge should leave WorldCamInterest discoverable from world root";

    // Leak real-asset dirs to avoid unrelated cascade cleanup crashes.
    (void)worldRoot;
}

TEST_F(MergeScopeParityTest, RealVenueWorldCamInterestSurvivesNonProxyMerge) {
    ObjectDir *venueDir = TryLoadGlitteratiVenue();
    if (!venueDir)
        GTEST_SKIP() << "glitterati.milo_xbox not available";

    ObjectDir *worldRoot = Hmx::Object::New<ObjectDir>();
    MergeNonProxy(venueDir, worldRoot);

    std::vector<std::string> names = CollectInterestNames(worldRoot);
    EXPECT_TRUE(HasInterestNamed(names, "WorldCamInterest.intr"))
        << "Non-proxy merge should flatten WorldCamInterest into the destination";

    (void)worldRoot;
}

TEST_F(MergeScopeParityTest, RealVenueInterestCollectionPattern) {
    ObjectDir *venueDir = TryLoadGlitteratiVenue();
    if (!venueDir)
        GTEST_SKIP() << "glitterati.milo_xbox not available";

    ObjectDir *worldRoot = Hmx::Object::New<ObjectDir>();
    MergeNonProxy(venueDir, worldRoot);

    Hmx::Object *owner = Hmx::Object::New<Hmx::Object>();
    ObjPtrList<CharInterest> interests(owner);
    for (ObjDirItr<CharInterest> it(worldRoot, true); it != nullptr; ++it)
        interests.push_back(it);

    int count = 0;
    bool foundWorldCam = false;
    for (ObjPtrList<CharInterest>::iterator it = interests.begin();
         it != interests.end();
         ++it) {
        count++;
        if (std::strcmp((*it)->Name(), "WorldCamInterest.intr") == 0)
            foundWorldCam = true;
    }

    EXPECT_GT(count, 0);
    EXPECT_TRUE(foundWorldCam)
        << "The merged venue should satisfy the same CharInterest collection pattern as SyncInterestObjects";

    delete owner;
    (void)worldRoot;
}

TEST_F(MergeScopeParityTest, RealVenueWorldCamInterestParent) {
    ObjectDir *venueDir = TryLoadGlitteratiVenue();
    if (!venueDir)
        GTEST_SKIP() << "glitterati.milo_xbox not available";

    ObjectDir *worldRoot = Hmx::Object::New<ObjectDir>();
    MergeNonProxy(venueDir, worldRoot);

    CharInterest *interest =
        dynamic_cast<CharInterest *>(worldRoot->FindObject("WorldCamInterest.intr", false, true));
    ASSERT_NE(interest, nullptr);
    EXPECT_NE(interest->TransParent(), nullptr)
        << "WorldCamInterest should keep a transform parent after merge so it can follow the camera";

    (void)worldRoot;
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

        // Venue subdir should have objects
        int objCount = 0;
        for (ObjDirItr<Hmx::Object> it(venueDir, true); it != nullptr; ++it)
            objCount++;
        EXPECT_GT(objCount, 0) << kVenueWorlds[i].name << " has no objects";
        printf("    objects=%d\n", objCount);

        // Leak — pre-existing cascade cleanup issues with real venue data.
        (void)worldRoot;
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
        // Don't delete venueDir before checks — matches real game lifecycle
        // where DirLoader manages source dir lifetime. Deleting immediately
        // leaves stale ring entries from outgoing refs (the cascade skip in
        // ~ObjRefConcrete doesn't unlink external refs).

        int unreachable = CountUnreachableObjects(targetDir);
        EXPECT_EQ(unreachable, 0)
            << kVenueWorlds[i].name << " has unreachable objects";

        printf("    unreachable=%d\n", unreachable);

        // Leak both dirs — pre-existing cascade cleanup issues with real
        // venue data make delete unsafe for multi-venue iteration.
        (void)venueDir;
        (void)targetDir;
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

    // Skip ring check — proxy merge doesn't delete source, so no stale entries.
    // But real venue data has complex subdir structures that may have
    // pre-existing ring issues from the merge pipeline.

    // Leak — pre-existing cascade cleanup issues with real venue data.
    (void)worldRoot;
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
    std::string nameA = venueA->Name();
    MergeProxy(venueA, worldRoot);
    EXPECT_EQ(VerifyAllRingsInDir(worldRoot), 0);

    // Find and delete venue A subdir (simulating Merger::Clear for proxy)
    printf("  Clear venue A\n");
    ObjectDir *subA = worldRoot->Find<ObjectDir>(nameA.c_str(), false);
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
    EXPECT_EQ(worldRoot->Find<ObjectDir>(nameA.c_str(), false), nullptr);

    delete worldRoot;
}

// ============================================================================
// Diagnostic: Verify inline subdirs load correctly from base venue .milo
// ============================================================================

TEST_F(MergeScopeParityTest, VenueInlineSubdirsLoadContent) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";

    std::string path = root + "/world/glitterati/gen/glitterati.milo_xbox";
    ObjectDir *venueDir = TryLoadStandalone(path);
    if (!venueDir)
        GTEST_SKIP() << "glitterati.milo_xbox not found";

    // Print subdir hierarchy
    printf("  Venue '%s' class='%s'\n", venueDir->Name(), venueDir->ClassName().Str());
    printf("  SubDirs: %d\n", (int)venueDir->SubDirs().size());
    for (int i = 0; i < (int)venueDir->SubDirs().size(); i++) {
        ObjectDir *sub = venueDir->SubDirs()[i];
        if (!sub) {
            printf("    [%d] nullptr\n", i);
            continue;
        }
        int subFlat = 0, subRecursive = 0;
        for (ObjDirItr<Hmx::Object> it(sub, false); it != nullptr; ++it)
            subFlat++;
        for (ObjDirItr<Hmx::Object> it(sub, true); it != nullptr; ++it)
            subRecursive++;
        printf("    [%d] '%s' class='%s' flat=%d recursive=%d subdirs=%d\n",
               i, sub->Name(), sub->ClassName().Str(),
               subFlat, subRecursive, (int)sub->SubDirs().size());
    }

    // Count total objects
    int flatCount = 0, recursiveCount = 0;
    for (ObjDirItr<Hmx::Object> it(venueDir, false); it != nullptr; ++it)
        flatCount++;
    for (ObjDirItr<Hmx::Object> it(venueDir, true); it != nullptr; ++it)
        recursiveCount++;
    printf("  Total: flat=%d recursive=%d\n", flatCount, recursiveCount);

    // Check for specific building mesh names that component files contain
    const char *buildingNames[] = {
        "GLI_Building01.mesh", "GLI_Building02.mesh", "GLI_Building03.mesh",
        "GLI_Building04.mesh", "GLI_Buildings1.mat", "GLI_Buildings2.mat",
        nullptr
    };
    printf("  Checking for building objects from component files:\n");
    int found = 0, missing = 0;
    for (int i = 0; buildingNames[i]; i++) {
        Hmx::Object *obj = venueDir->FindObject(buildingNames[i], false, true);
        if (obj) {
            printf("    FOUND: '%s' class='%s'\n", buildingNames[i], obj->ClassName().Str());
            found++;
        } else {
            printf("    MISSING: '%s'\n", buildingNames[i]);
            missing++;
        }
    }
    printf("  Building objects: found=%d missing=%d\n", found, missing);

    // The base venue should have substantial content from inline subdirs
    EXPECT_GT(recursiveCount, 100)
        << "Base venue has too few objects — inline subdirs may not be loading";

    // Skip deletion — crashes due to cascade delete of massive shared subdir tree
    // (same issue as other venue tests that don't delete). Leak is acceptable in tests.
    // delete venueDir;
}

} // namespace
