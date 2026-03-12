// Asset loading tests — verify that all game asset types can be loaded
// without crashes, stream desync, or assertion failures.
//
// Archive-backed assets require DC3_DATA pointing at extracted ark files.
// Standalone .milo_xbox assets use the pre-extracted library at MILO_LIB.
//
// Run the bulk loading sweep (tests every .milo_xbox in the library):
//   cd native/build && ctest -R BulkLoad --output-on-failure
//   MILO_BULK_CATEGORY=ui ctest -R BulkLoad --output-on-failure

#include "test_helpers.h"
#include "char/FileMerger.h"
#include "hamobj/HamCharacter.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Object.h"
#include "utl/FilePath.h"

#include <sys/stat.h>
#include <dirent.h>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <vector>

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

// Try to load an archive-backed milo path. Returns dir or nullptr.
static ObjectDir *TryLoadArchive(const char *path) {
    FilePath fp(path);
    return DirLoader::LoadObjects(fp, nullptr, nullptr);
}

// ============================================================================
// Fixture: full engine init
// ============================================================================

class AssetLoadingTest : public EngineTestFixture {};

// ============================================================================
// Archive-backed asset loading — these use paths resolved through TheArchive
// ============================================================================

// Core character resources — small, should always work
static const char *kCharResources[] = {
    "char/shared/main_resource.milo",
    "char/shared/viseme_resource.milo",
    "char/shared/skeleton_bones_resource.milo",
    nullptr
};

TEST_F(AssetLoadingTest, LoadCharResources) {
    int loaded = 0, skipped = 0;
    for (int i = 0; kCharResources[i]; i++) {
        ObjectDir *dir = TryLoadArchive(kCharResources[i]);
        if (dir) {
            printf("  OK: %s -> '%s' class='%s'\n",
                   kCharResources[i], dir->Name(), dir->ClassName().Str());
            EXPECT_NE(strlen(dir->Name()), 0u);
            loaded++;
        } else {
            skipped++;
        }
    }
    if (loaded == 0) {
        GTEST_SKIP() << "No archive assets available (set DC3_DATA)";
    }
    printf("CharResources: loaded=%d skipped=%d\n", loaded, skipped);
}

// UI panel dirs — complex hierarchies with many object types
static const char *kUIAssets[] = {
    "ui/gen/cheat.milo_xbox",
    "ui/gen/common.milo_xbox",
    "ui/gen/locale.milo_xbox",
    "ui/gen/panel_select.milo_xbox",
    "ui/resource/fonts/gen/default.milo_xbox",
    "ui/resource/lists/gen/default.milo_xbox",
    nullptr
};

TEST_F(AssetLoadingTest, LoadUIAssets) {
    int loaded = 0, skipped = 0;
    for (int i = 0; kUIAssets[i]; i++) {
        ObjectDir *dir = TryLoadArchive(kUIAssets[i]);
        if (dir) {
            // Count objects
            int count = 0;
            for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it)
                count++;
            printf("  OK: %s -> '%s' class='%s' objects=%d\n",
                   kUIAssets[i], dir->Name(), dir->ClassName().Str(), count);
            EXPECT_GT(count, 0) << "Dir should have objects: " << kUIAssets[i];
            loaded++;
        } else {
            skipped++;
        }
    }
    if (loaded == 0)
        GTEST_SKIP() << "No archive UI assets available";
    printf("UIAssets: loaded=%d skipped=%d\n", loaded, skipped);
}

// SFX dirs — ObjectDir containers for sound objects
static const char *kSFXAssets[] = {
    "sfx/gen/common_bank.milo_xbox",
    "sfx/gen/shell_fx.milo_xbox",
    "sfx/gen/ingame_bank.milo_xbox",
    nullptr
};

TEST_F(AssetLoadingTest, LoadSFXAssets) {
    int loaded = 0, skipped = 0;
    for (int i = 0; kSFXAssets[i]; i++) {
        ObjectDir *dir = TryLoadArchive(kSFXAssets[i]);
        if (dir) {
            printf("  OK: %s -> '%s' class='%s'\n",
                   kSFXAssets[i], dir->Name(), dir->ClassName().Str());
            loaded++;
        } else {
            skipped++;
        }
    }
    if (loaded == 0)
        GTEST_SKIP() << "No archive SFX assets available";
    printf("SFXAssets: loaded=%d skipped=%d\n", loaded, skipped);
}

// Flow dirs — game logic containers
static const char *kFlowAssets[] = {
    "flow/gen/crowd_audio_proxy.milo_xbox",
    "flow/gen/nav_player.milo_xbox",
    "flow/gen/spawner.milo_xbox",
    nullptr
};

TEST_F(AssetLoadingTest, LoadFlowAssets) {
    int loaded = 0, skipped = 0;
    for (int i = 0; kFlowAssets[i]; i++) {
        ObjectDir *dir = TryLoadArchive(kFlowAssets[i]);
        if (dir) {
            printf("  OK: %s -> '%s' class='%s'\n",
                   kFlowAssets[i], dir->Name(), dir->ClassName().Str());
            loaded++;
        } else {
            skipped++;
        }
    }
    if (loaded == 0)
        GTEST_SKIP() << "No archive flow assets available";
    printf("FlowAssets: loaded=%d skipped=%d\n", loaded, skipped);
}

// World dirs — venues with meshes, lights, cameras
// NOTE: world/gen/world.milo_xbox crashes due to nested subdir type mismatch
// (iconmandir interpreted as RndDir with mRev 32 > INIT_REVS 10).
// Use specific venue files that are known to load correctly.
static const char *kWorldAssets[] = {
    "world/default/gen/default.milo_xbox",
    "world/shared/camshots/gen/angel.milo_xbox",
    nullptr
};

TEST_F(AssetLoadingTest, LoadWorldAssets) {
    int loaded = 0, skipped = 0;
    for (int i = 0; kWorldAssets[i]; i++) {
        ObjectDir *dir = TryLoadArchive(kWorldAssets[i]);
        if (dir) {
            int count = 0;
            for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it)
                count++;
            printf("  OK: %s -> '%s' class='%s' objects=%d\n",
                   kWorldAssets[i], dir->Name(), dir->ClassName().Str(), count);
            loaded++;
        } else {
            skipped++;
        }
    }
    if (loaded == 0)
        GTEST_SKIP() << "No archive world assets available";
    printf("WorldAssets: loaded=%d skipped=%d\n", loaded, skipped);
}

// ============================================================================
// Standalone .milo_xbox loading — uses pre-extracted library at MILO_LIB
// ============================================================================

struct StandaloneMiloEntry {
    const char *relPath;
    const char *category;
};

static const StandaloneMiloEntry kStandaloneMiloFiles[] = {
    // Flow
    {"flow/gen/crowd_audio_proxy.milo_xbox", "flow"},
    {"flow/gen/nav_player.milo_xbox", "flow"},
    // UI (small files)
    {"ui/resource/fonts/gen/default.milo_xbox", "ui-font"},
    {"ui/resource/lists/gen/default.milo_xbox", "ui-list"},
    // SFX
    {"sfx/gen/shell_fx.milo_xbox", "sfx"},
    // World (camshots are small, safe)
    {"world/shared/camshots/gen/angel.milo_xbox", "world-camshot"},
    {nullptr, nullptr}
};

TEST_F(AssetLoadingTest, LoadStandaloneMiloFiles) {
    std::string root = GetMiloLibRoot();
    if (root.empty()) {
        GTEST_SKIP() << "MILO_LIB not set and default path not found";
    }

    int loaded = 0, skipped = 0;
    for (int i = 0; kStandaloneMiloFiles[i].relPath; i++) {
        std::string full = root + "/" + kStandaloneMiloFiles[i].relPath;
        if (!FileExists(full)) {
            skipped++;
            continue;
        }

        FilePath fp(full.c_str());
        ObjectDir *dir = DirLoader::LoadObjects(fp, nullptr, nullptr);
        ASSERT_NE(dir, nullptr) << "Failed to load: " << full;

        int count = 0;
        for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it)
            count++;

        printf("  OK [%s]: '%s' class='%s' objects=%d\n",
               kStandaloneMiloFiles[i].category, dir->Name(),
               dir->ClassName().Str(), count);

        EXPECT_NE(strlen(dir->Name()), 0u);
        EXPECT_GT(count, 0) << "Dir has no objects: " << full;
        loaded++;
    }

    if (loaded == 0)
        GTEST_SKIP() << "No standalone .milo_xbox files found at " << root;
    printf("LoadStandaloneMiloFiles: loaded=%d skipped=%d\n", loaded, skipped);
}

// ============================================================================
// Known-failing loads — regression targets for decomp bugs
// ============================================================================
// These test assets that exercise code paths through incomplete decomp
// functions (ObjectDir::PreLoad 89.6%, ObjectDir::PostLoad 85.8%).
// They document known loading failures as targets for fixing.

// Try loading a standalone milo file. Returns dir or nullptr on failure.
// Catches MILO_FAIL crashes via the native port's longjmp handler.
static ObjectDir *TryLoadStandalone(const std::string &path) {
    if (!FileExists(path))
        return nullptr;
    FilePath fp(path.c_str());
    return DirLoader::LoadObjects(fp, nullptr, nullptr);
}

// Complex venues — these exercise deep subdir chains with many object types.
// The full loading chain goes:
//   DirLoader::LoadDir -> WorldDir::PreLoad -> PanelDir::PreLoad ->
//   RndDir::PreLoad -> ObjectDir::PreLoad (89.6%)
// then ObjectDir::PostLoad (85.8%) for inlined subdirs.
//
// KNOWN BUG: world/gen/world.milo_xbox contains inlined subdirs including
// 'director' (which itself inlines 'iconmandir'). Same root cause as
// World master file — contains inlined subdirs (director, iconmandir, etc.)
// Previously crashed with "String chars N > 512" due to missing PanelDir
// factory registration causing stream desync during inlined subdir loading.
TEST_F(AssetLoadingTest, LoadWorldMasterFile) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";
    std::string path = root + "/world/gen/world.milo_xbox";
    if (!FileExists(path))
        GTEST_SKIP() << "world.milo_xbox not found";

    ObjectDir *dir = TryLoadStandalone(path);
    ASSERT_NE(dir, nullptr) << "world.milo_xbox failed to load";
    EXPECT_STREQ(dir->ClassName().Str(), "WorldDir");
}

// Full venue worlds — large files with meshes, lights, cameras, animations.
// These exercise the complete loading pipeline including nested subdirs.
struct VenueEntry {
    const char *relPath;
    const char *name;
};

static const VenueEntry kVenueWorlds[] = {
    {"world/glitterati/gen/glitterati.milo_xbox", "glitterati"},
    {"world/dclive/gen/dclive.milo_xbox", "dclive"},
    {"world/houseparty/gen/houseparty.milo_xbox", "houseparty"},
    {"world/rollerrink/gen/rollerrink.milo_xbox", "rollerrink"},
    {"world/bid/gen/bid.milo_xbox", "bid"},
    {"world/dci/gen/dci.milo_xbox", "dci"},
    {"world/throneroom/gen/throneroom.milo_xbox", "throneroom"},
    {"world/streetside/gen/streetside.milo_xbox", "streetside"},
    {nullptr, nullptr}
};

TEST_F(AssetLoadingTest, LoadFullVenueWorlds) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";

    int loaded = 0, failed = 0, skipped = 0;
    for (int i = 0; kVenueWorlds[i].relPath; i++) {
        std::string path = root + "/" + kVenueWorlds[i].relPath;
        if (!FileExists(path)) {
            skipped++;
            continue;
        }

        printf("  Loading %s...\n", kVenueWorlds[i].name);
        fflush(stdout);
        ObjectDir *dir = TryLoadStandalone(path);
        if (!dir) {
            printf("  FAIL: %s returned nullptr\n", kVenueWorlds[i].name);
            failed++;
            ADD_FAILURE() << "Failed to load venue: " << kVenueWorlds[i].name
                << " (" << kVenueWorlds[i].relPath << ")";
            continue;
        }

        // Count objects both flat and recursive
        int flatCount = 0, recursiveCount = 0;
        for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it)
            flatCount++;
        for (ObjDirItr<Hmx::Object> it(dir, true); it != nullptr; ++it)
            recursiveCount++;
        int subdirCount = (int)dir->SubDirs().size();
        printf("  OK: %s -> '%s' class='%s' flat=%d recursive=%d subdirs=%d\n",
               kVenueWorlds[i].name, dir->Name(), dir->ClassName().Str(),
               flatCount, recursiveCount, subdirCount);
        // Complex venue worlds should have substantial content
        EXPECT_GT(recursiveCount, 10) << "Venue " << kVenueWorlds[i].name
            << " has suspiciously few objects — subdirs may not be loading";
        loaded++;
    }

    if (loaded == 0 && skipped > 0)
        GTEST_SKIP() << "No venue worlds found at " << root;
    printf("VenueWorlds: loaded=%d failed=%d skipped=%d\n", loaded, failed, skipped);
}

// Shared world subdirs — icon manager, director, phrase meter, etc.
// These contain types like HamCharacter that may not be registered.
static const StandaloneMiloEntry kSharedWorldSubdirs[] = {
    {"world/shared/gen/iconman.milo_xbox", "iconman"},
    {"world/shared/gen/peak_spiral.milo_xbox", "peak-spiral"},
    {"world/shared/gen/phrase_meter.milo_xbox", "phrase-meter"},
    {"world/shared/gen/move_feedback.milo_xbox", "move-feedback"},
    {"world/shared/gen/chars_base.milo_xbox", "chars-base"},
    {nullptr, nullptr}
};

// Director contains inlined subdirs (PanelDir 'hud', RndDir 'iconmandir',
// HamDirector, PracticeSection, PropAnims). Loading fails with stream desync:
// ObjectDir::PreLoad (88.9%) reads the wrong bytes for inlined subdir revisions.
// The 'iconmandir' inlined subdir gets mRev=32 (the outer file rev) instead
// of the actual RndDir rev, causing ASSERT_REVS WARNING then String overflow.
//
// Director subdir — contains inlined 'hud' (PanelDir) and 'iconmandir' subdirs.
// Previously crashed with "String chars N > 512" due to missing PanelDir
// factory registration causing NULL object and ReadDead stream desync.
TEST_F(AssetLoadingTest, LoadDirectorSubdir) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";
    std::string path = root + "/world/shared/gen/director.milo_xbox";
    if (!FileExists(path))
        GTEST_SKIP() << "director.milo_xbox not found";

    ObjectDir *dir = TryLoadStandalone(path);
    ASSERT_NE(dir, nullptr) << "director.milo_xbox failed to load";
    EXPECT_STREQ(dir->ClassName().Str(), "RndDir");
}

TEST_F(AssetLoadingTest, LoadSharedWorldSubdirs) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";

    int loaded = 0, failed = 0, skipped = 0;
    for (int i = 0; kSharedWorldSubdirs[i].relPath; i++) {
        std::string path = root + "/" + kSharedWorldSubdirs[i].relPath;
        if (!FileExists(path)) {
            skipped++;
            continue;
        }

        printf("  Loading %s...\n", kSharedWorldSubdirs[i].category);
        fflush(stdout);
        ObjectDir *dir = TryLoadStandalone(path);
        if (!dir) {
            printf("  FAIL: %s returned nullptr\n", kSharedWorldSubdirs[i].category);
            failed++;
            ADD_FAILURE() << "Failed to load shared subdir: "
                << kSharedWorldSubdirs[i].category
                << " (" << kSharedWorldSubdirs[i].relPath << ")";
            continue;
        }

        int count = 0;
        for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it)
            count++;
        printf("  OK [%s]: '%s' class='%s' objects=%d\n",
               kSharedWorldSubdirs[i].category, dir->Name(),
               dir->ClassName().Str(), count);
        EXPECT_GT(count, 0);
        loaded++;
    }

    if (loaded == 0 && skipped > 0)
        GTEST_SKIP() << "No shared world subdirs found";
    printf("SharedWorldSubdirs: loaded=%d failed=%d skipped=%d\n",
           loaded, failed, skipped);
}

// Character loading — main character has complex bone hierarchy + animations
TEST_F(AssetLoadingTest, LoadMainCharacter) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";

    std::string path = root + "/char/main/gen/main.milo_xbox";
    if (!FileExists(path))
        GTEST_SKIP() << "char/main not found";

    ObjectDir *dir = TryLoadStandalone(path);
    ASSERT_NE(dir, nullptr) << "Failed to load main character";

    int count = 0;
    for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it)
        count++;
    printf("  main.milo_xbox: '%s' class='%s' objects=%d\n",
           dir->Name(), dir->ClassName().Str(), count);
    EXPECT_GT(count, 0);
}

TEST_F(AssetLoadingTest, MainCharacterFileMergerConfiguresOutfitAndVisemeByDefault) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";

    std::string path = root + "/char/main/gen/main.milo_xbox";
    if (!FileExists(path))
        GTEST_SKIP() << "char/main not found";

    ObjectDir *dir = TryLoadStandalone(path);
    ASSERT_NE(dir, nullptr) << "Failed to load main character";

    HamCharacter *character = dynamic_cast<HamCharacter *>(dir);
    if (!character) {
        ObjDirItr<HamCharacter> it(dir, true);
        if (it)
            character = it;
    }
    ASSERT_NE(character, nullptr) << "No HamCharacter found in main.milo_xbox";

    FileMerger *fm = character->Find<FileMerger>("char.fm", false);
    ASSERT_NE(fm, nullptr) << "main.milo_xbox missing char.fm";

    character->SetOutfit("mo01");
    character->SetOutfitDir("char/main/dancer");
    character->StartLoad(false);

    FileMerger::Merger *outfitMerger = nullptr;
    FileMerger::Merger *visemeMerger = nullptr;
    ObjVector<FileMerger::Merger> &mergers = fm->Mergers();
    for (int i = 0; i < mergers.size(); i++) {
        if (mergers[i].mName == "outfit")
            outfitMerger = &mergers[i];
        else if (mergers[i].mName == "viseme")
            visemeMerger = &mergers[i];
    }

    ASSERT_NE(outfitMerger, nullptr);
    ASSERT_NE(visemeMerger, nullptr);
    EXPECT_FALSE(outfitMerger->mSelected.empty());
    EXPECT_FALSE(visemeMerger->mSelected.empty());
    EXPECT_NE(character->Find<ObjectDir>("viseme", false), nullptr);
}

// ============================================================================
// Verify object iteration doesn't loop infinitely or crash
// ============================================================================

TEST_F(AssetLoadingTest, ObjectIterationSafety) {
    ObjectDir *dir = TryLoadArchive("char/shared/main_resource.milo");
    if (!dir) {
        GTEST_SKIP() << "No archive assets available";
    }

    // Iterate with a safety limit
    const int kMaxObjects = 100000;
    int count = 0;
    for (ObjDirItr<Hmx::Object> it(dir, true); it != nullptr; ++it) {
        ASSERT_LT(count, kMaxObjects) << "Object iteration exceeded safety limit";
        EXPECT_NE(it->Name(), nullptr);
        EXPECT_NE(it->ClassName().Str(), nullptr);
        count++;
    }

    printf("ObjectIterationSafety: %d objects iterated without issue\n", count);
    EXPECT_GT(count, 0);
}

// ============================================================================
// Repeated load/delete cycle — check for memory leaks and dangling refs
// ============================================================================

TEST_F(AssetLoadingTest, RepeatedLoadCycle) {
    const char *testPath = "char/shared/main_resource.milo";
    ObjectDir *probe = TryLoadArchive(testPath);
    if (!probe) {
        GTEST_SKIP() << "No archive assets available";
    }
    delete probe;

    // Load and delete 5 times
    for (int cycle = 0; cycle < 5; cycle++) {
        ObjectDir *dir = TryLoadArchive(testPath);
        ASSERT_NE(dir, nullptr) << "Load failed on cycle " << cycle;

        int count = 0;
        for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it)
            count++;

        printf("  Cycle %d: loaded %d objects\n", cycle, count);
        EXPECT_GT(count, 0);
        delete dir;
    }
}

// ============================================================================
// Bulk loading — try every .milo_xbox file in the library
// ============================================================================
// Walks the MILO_LIB directory tree and attempts to load every .milo_xbox.
// Reports per-category pass/fail counts. Failures are non-fatal (EXPECT, not
// ASSERT) so one bad file doesn't abort the entire sweep.
//
// Filter by category with env var:
//   MILO_BULK_CATEGORY=ui    (ui, world, char, sfx, flow, songs, all)
//   MILO_BULK_LIMIT=50       (max files per category, 0=unlimited)
//
// Run:
//   cd native/build && ctest -R BulkLoad --output-on-failure

static void CollectMiloFiles(const std::string &dir, std::vector<std::string> &out) {
    DIR *dp = opendir(dir.c_str());
    if (!dp) return;
    struct dirent *ent;
    while ((ent = readdir(dp)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string full = dir + "/" + ent->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            CollectMiloFiles(full, out);
        } else if (S_ISREG(st.st_mode)) {
            std::string name(ent->d_name);
            if (name.size() > 10 && name.substr(name.size() - 10) == ".milo_xbox") {
                out.push_back(full);
            }
        }
    }
    closedir(dp);
}

static std::string CategoryFromPath(const std::string &path, const std::string &root) {
    std::string rel = path.substr(root.size() + 1);
    size_t slash = rel.find('/');
    return (slash != std::string::npos) ? rel.substr(0, slash) : "other";
}

TEST_F(AssetLoadingTest, BulkLoadAllFiles) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";

    // Make MILO_FAIL non-fatal so one bad file doesn't abort the sweep
    setenv("MILO_FATAL_FAILS", "0", 1);

    const char *catFilter = getenv("MILO_BULK_CATEGORY");
    std::string category = catFilter ? catFilter : "all";

    const char *limitStr = getenv("MILO_BULK_LIMIT");
    const char *bulkAll = getenv("MILO_BULK_ALL");
    int limit = limitStr ? atoi(limitStr) : (bulkAll && std::string(bulkAll) == "1") ? 0 : 20;

    // Collect all .milo_xbox files
    std::vector<std::string> allFiles;
    CollectMiloFiles(root, allFiles);
    std::sort(allFiles.begin(), allFiles.end());

    ASSERT_GT((int)allFiles.size(), 0) << "No .milo_xbox files found in " << root;
    printf("Found %d .milo_xbox files in library\n", (int)allFiles.size());

    // Filter by category
    std::vector<std::string> files;
    for (auto &f : allFiles) {
        if (category != "all") {
            std::string cat = CategoryFromPath(f, root);
            if (cat != category) continue;
        }
        files.push_back(f);
        if (limit > 0 && (int)files.size() >= limit) break;
    }

    printf("Testing %d files (category=%s, limit=%d)\n",
           (int)files.size(), category.c_str(), limit);

    int passed = 0, failed = 0, skipped = 0;
    std::vector<std::string> failures;

    for (auto &path : files) {
        std::string rel = path.substr(root.size() + 1);
        ObjectDir *dir = TryLoadStandalone(path);
        if (dir) {
            passed++;
            // Don't delete dir — ObjDirPtr destructor cascade is O(n^2) for
            // world files with many shared subdirs (HasDirPtrs walks the ref
            // ring per subdir). One venue file takes 30+ seconds to destroy.
            // Loading is what we're testing, not destruction.
        } else {
            failures.push_back(rel);
            failed++;
        }
    }

    printf("\n=== Bulk Load Results ===\n");
    printf("Passed: %d  Failed: %d  Skipped: %d  Total: %d\n",
           passed, failed, skipped, (int)files.size());

    if (!failures.empty()) {
        printf("\nFailed files (%d):\n", (int)failures.size());
        for (auto &f : failures)
            printf("  FAIL: %s\n", f.c_str());
    }

    // We expect at least 90% pass rate
    if (files.size() > 10) {
        float passRate = (float)passed / (float)files.size();
        EXPECT_GE(passRate, 0.9f)
            << "Pass rate " << (passRate * 100) << "% is below 90% threshold";
    }
}

// Per-category tests — can run in parallel with ctest -j$(nproc)
// Default subset size for large categories (sfx, songs).
// Set MILO_BULK_ALL=1 to load every file instead of a subset.
static const int kDefaultSubsetSize = 5;

static void RunCategoryBulkLoad(const char *category) {
    std::string root = GetMiloLibRoot();
    if (root.empty()) {
        GTEST_SKIP() << "MILO_LIB not set";
        return;
    }
    setenv("MILO_FATAL_FAILS", "0", 1);

    std::vector<std::string> allFiles;
    CollectMiloFiles(root, allFiles);
    std::sort(allFiles.begin(), allFiles.end());

    std::vector<std::string> files;
    for (auto &f : allFiles) {
        if (CategoryFromPath(f, root) == category)
            files.push_back(f);
    }
    if (files.empty()) {
        GTEST_SKIP() << "No files for category " << category;
        return;
    }

    // For large categories, only test a spread-out subset by default.
    // MILO_BULK_ALL=1 runs the full set.
    const char *bulkAll = std::getenv("MILO_BULK_ALL");
    bool runAll = bulkAll && std::string(bulkAll) == "1";
    std::vector<std::string> subset;
    if (!runAll && (int)files.size() > kDefaultSubsetSize) {
        // Pick evenly spaced files for coverage across the category
        for (int i = 0; i < kDefaultSubsetSize; i++) {
            int idx = i * (int)files.size() / kDefaultSubsetSize;
            subset.push_back(files[idx]);
        }
        printf("Category '%s': %d files (testing subset of %d, set MILO_BULK_ALL=1 for all)\n",
               category, (int)files.size(), (int)subset.size());
    } else {
        subset = files;
        printf("Category '%s': %d files\n", category, (int)subset.size());
    }

    int passed = 0, failed = 0;
    std::vector<std::string> failures;

    for (auto &path : subset) {
        ObjectDir *dir = TryLoadStandalone(path);
        if (dir) {
            passed++;
        } else {
            failures.push_back(path.substr(root.size() + 1));
            failed++;
        }
    }

    printf("  Passed: %d  Failed: %d\n", passed, failed);
    for (auto &f : failures)
        printf("  FAIL: %s\n", f.c_str());

    EXPECT_EQ(failed, 0) << failed << " files failed to load";
}

TEST_F(AssetLoadingTest, BulkLoad_Flow)  { RunCategoryBulkLoad("flow"); }
TEST_F(AssetLoadingTest, BulkLoad_Char)  { RunCategoryBulkLoad("char"); }
TEST_F(AssetLoadingTest, BulkLoad_World) { RunCategoryBulkLoad("world"); }
TEST_F(AssetLoadingTest, BulkLoad_UI)    { RunCategoryBulkLoad("ui"); }
TEST_F(AssetLoadingTest, BulkLoad_SFX)   { RunCategoryBulkLoad("sfx"); }
TEST_F(AssetLoadingTest, BulkLoad_Songs) { RunCategoryBulkLoad("songs"); }

// ============================================================================
// Subdir loading validation — verify inlined subdirs are populated
// ============================================================================

#include "rndobj/PropAnim.h"
#include "rndobj/Dir.h"
#include "flow/Flow.h"

TEST_F(AssetLoadingTest, ChooseModeSubdirLoading) {
    std::string root = GetMiloLibRoot();
    if (root.empty())
        GTEST_SKIP() << "MILO_LIB not set";
    std::string path = root + "/ui/choose_mode/gen/choose_mode.milo_xbox";
    if (!FileExists(path))
        GTEST_SKIP() << "choose_mode.milo_xbox not found";

    FilePath fp(path.c_str());
    ObjectDir *dir = DirLoader::LoadObjects(fp, nullptr, nullptr);
    ASSERT_NE(dir, nullptr);

    printf("Dir: '%s' class='%s'\n", dir->Name(), dir->ClassName().Str());

    printf("SubDirs (%d):\n", (int)dir->SubDirs().size());
    for (int i = 0; i < (int)dir->SubDirs().size(); i++) {
        ObjectDir *sub = dir->SubDirs()[i];
        if (sub) {
            int count = 0;
            for (ObjDirItr<Hmx::Object> it(sub, false); it != nullptr; ++it)
                count++;
            printf("  [%d] '%s' class='%s' objects=%d subdirs=%d\n",
                   i, sub->Name(), sub->ClassName().Str(), count,
                   (int)sub->SubDirs().size());
        } else {
            printf("  [%d] nullptr\n", i);
        }
    }

    int flatCount = 0;
    for (ObjDirItr<Hmx::Object> it(dir, false); it != nullptr; ++it)
        flatCount++;
    int recCount = 0;
    for (ObjDirItr<Hmx::Object> it(dir, true); it != nullptr; ++it)
        recCount++;
    printf("Objects: flat=%d recursive=%d\n", flatCount, recCount);

    int paCount = 0;
    for (ObjDirItr<RndPropAnim> it(dir, true); it != nullptr; ++it) {
        printf("  PropAnim '%s' (dir='%s') end=%.1f\n",
               it->Name(), it->Dir() ? it->Dir()->Name() : "?", it->EndFrame());
        paCount++;
    }
    printf("PropAnims: %d\n", paCount);

    int flowCount = 0;
    for (ObjDirItr<Flow> it(dir, true); it != nullptr; ++it) {
        printf("  Flow '%s' (dir='%s')\n",
               it->Name(), it->Dir() ? it->Dir()->Name() : "?");
        flowCount++;
    }
    printf("Flows: %d\n", flowCount);

    // Check for RndDir objects (nested dirs that aren't formal subdirs)
    printf("\nRndDir objects in main dir:\n");
    int rndDirCount = 0;
    for (ObjDirItr<RndDir> it(dir, false); it != nullptr; ++it) {
        printf("  RndDir '%s' (dir='%s')\n", it->Name(), it->Dir() ? it->Dir()->Name() : "?");
        // Count objects inside this nested RndDir
        int innerCount = 0;
        for (ObjDirItr<Hmx::Object> inner((ObjectDir*)&*it, false); inner != nullptr; ++inner)
            innerCount++;
        int innerPA = 0;
        for (ObjDirItr<RndPropAnim> inner((ObjectDir*)&*it, false); inner != nullptr; ++inner) {
            printf("    PropAnim '%s' end=%.1f\n", inner->Name(), inner->EndFrame());
            innerPA++;
        }
        printf("    Objects=%d PropAnims=%d subdirs=%d\n", innerCount, innerPA, (int)it->SubDirs().size());
        rndDirCount++;
    }
    printf("RndDir count: %d\n\n", rndDirCount);

    // game_mode_icon RndDir should have icon_enter PropAnims
    // Note: ObjDirItr only traverses SubDirs(), not nested RndDir objects.
    // The flat iteration of the main dir finds game_mode_icon as an object;
    // its PropAnims must be queried by iterating inside that RndDir specifically.
    bool hasIconEnter = false;
    for (ObjDirItr<RndDir> rdit(dir, false); rdit != nullptr; ++rdit) {
        if (std::strcmp(rdit->Name(), "game_mode_icon") == 0) {
            for (ObjDirItr<RndPropAnim> pa((ObjectDir*)&*rdit, false); pa != nullptr; ++pa) {
                if (std::strstr(pa->Name(), "icon_enter") != nullptr)
                    hasIconEnter = true;
            }
        }
    }
    EXPECT_TRUE(hasIconEnter) << "game_mode_icon RndDir missing icon_enter PropAnims";
    EXPECT_GE(rndDirCount, 2) << "Expected at least game_mode_icon + self as RndDir objects";
}
