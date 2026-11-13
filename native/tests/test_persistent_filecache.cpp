// The persistent file cache (PLAN H-11): config/persistent_filecache.dta, the
// cache App::App builds at boot, between MetagameRank::Init and $extra_songs.
//
// Line 1 of the file is a byte budget (2 MiB); the rest are paths.  Among them
// are the 29 loading-music .mid that LoadingPanel::PlayLoadingMusic looks up
// with FileCache::GetFileAll -- in file caches ONLY, with MILO_ASSERT(f) in
// the image.  Native never built the cache until 2026-11-13, so every song
// load logged "loading music MIDI not found" and played no loading music.
//
// What these tests pin, all from the game data:
//   * DesktopHoldsEveryEntry: the cache built exactly as App::App builds it
//     (FileCache(budget, kLoadFront, false, true), StartSet(0), Add(path, 1,
//     ""), EndSet, PollUntilLoaded) through the real loader keeps EVERY entry
//     on desktop: the bytes desktop reads (the ark, the 360's own bytes) sum to
//     under the budget, so FileCache::DumpOverSize evicts nothing, and every
//     .mid in ui/global.dta's LOADINGMUSIC_ALL is served by GetFileAll.  A
//     negative control rebuilds it one byte under its own total and requires
//     an eviction, so the "nothing evicted" check is shown able to fail.
//   * WebBytesFitTheWebBudget: the web build reads the INFLATED extraction
//     (what native/web/server.py serves), 2-3x larger.  Over budget, the
//     cache evicts whole entries in an order that depends on the host's
//     std::sort of equal priorities.  The web budget (HX_WEB scale in App.cpp)
//     must hold the inflated set.
//
// The engine-level proof that the music then plays is the DTA flow test
// LoadingMusicFlowTest (test_dta_flow.cpp family).

#include "test_helpers.h"

#include "obj/Data.h"
#include "obj/DataFile.h"
#include "obj/DataUtl.h"
#include "obj/DirLoader.h"
#include "os/File.h"
#include "os/FileCache.h"
#include "os/System.h"
#include "rndobj/Utl.h"
#include "utl/Loader.h"
#include "utl/MakeString.h"

#include "platform/PersistentFileCache.h"

extern const char *NativeGetDataDir();

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

// Reads FileCache's protected size; everything else is the real class.
class ProbeCache : public FileCache {
public:
    ProbeCache(int size) : FileCache(size, kLoadFront, false, true) {}
    int Size() const { return CurSize(); }
    static void *Helper() { return sResourceCacheHelper; }
};

struct CacheConfig {
    int budget = 0;
    std::vector<std::string> paths;
};

CacheConfig ReadConfig() {
    CacheConfig c;
    DataArray *cfg = SystemConfig("persistent_filecache");
    if (!cfg)
        return c;
    c.budget = cfg->Int(1);
    for (int i = 2; i < cfg->Size(); i++)
        c.paths.push_back(cfg->Str(i));
    return c;
}

// App::App's sequence, on a cache of the given budget.
ProbeCache *Build(const CacheConfig &c, int budget) {
    ProbeCache *cache = new ProbeCache(budget);
    cache->StartSet(0);
    for (const std::string &p : c.paths)
        cache->Add(p.c_str(), 1, "");
    cache->EndSet();
    cache->PollUntilLoaded();
    return cache;
}

// The .mid each LOADINGMUSIC_ALL .mogg needs (PlayLoadingMusic's
// "sfx/samples/shell/%s.mid" of FileGetBase).
std::vector<std::string> LoadingMusicMids() {
    std::vector<std::string> mids;
    DataArray *global = DataReadFile("ui/global.dta", true);
    if (global)
        global->Release(); // the #define is what we want, not the array
    DataArray *all = DataGetMacro("LOADINGMUSIC_ALL");
    if (!all || all->Size() < 1 || all->Type(0) != kDataArray)
        return mids;
    DataArray *list = all->Array(0);
    for (int i = 0; i < list->Size(); i++)
        mids.push_back(MakeString("sfx/samples/shell/%s.mid", FileGetBase(list->Str(i))));
    return mids;
}

// The path FileCache::Add keys the entry by (and the file reads), as Add
// computes it: .milo through DirLoader::CachedPath, .png/.bmp through the
// registered ResourceFileCacheHelper (CacheResource).
std::string CachedName(const std::string &path) {
    const char *ext = FileGetExt(path.c_str());
    if (ext && !strcmp(ext, "milo"))
        return DirLoader::CachedPath(path.c_str(), false);
    if (ext && (!strcmp(ext, "png") || !strcmp(ext, "bmp")))
        return CacheResource(path.c_str(), nullptr); // ResourceFileCacheHelper
    return path; // no .wav in the list (WavFileCacheHelper)
}

long long WebFileSize(const std::string &path) {
    const char *dataDir = NativeGetDataDir();
    if (!dataDir || !*dataDir)
        return -1;
    struct stat st;
    std::string full = std::string(dataDir) + "/extracted/" + CachedName(path);
    if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return -1;
    return (long long)st.st_size;
}

std::string Mib(long long b) { return MakeString("%.3f MiB", b / 1048576.0); }

class PersistentFileCacheTest : public EngineTestFixture {};

TEST_F(PersistentFileCacheTest, DesktopHoldsEveryEntry) {
    if (!EnsureEngineInit())
        return;
    CacheConfig cfg = ReadConfig();
    ASSERT_GT(cfg.budget, 0) << "ham_keep.dta (persistent_filecache ...) did not parse";
    ASSERT_GE(cfg.paths.size(), 100u) << "the shipped list has 105 paths";
    std::vector<std::string> mids = LoadingMusicMids();
    ASSERT_EQ(mids.size(), 29u) << "ui/global.dta LOADINGMUSIC_ALL";

    const int budget = NativePersistentFileCacheBudget(cfg.budget, false);
    EXPECT_EQ(budget, cfg.budget) << "desktop reads the 360's bytes: no scaling";
    ProbeCache *cache = Build(cfg, budget);
    const int total = cache->Size();
    printf("[persistent-cache] desktop: %zu paths, %d bytes (%s) of budget %d (%s)\n",
           cfg.paths.size(), total, Mib(total).c_str(), budget, Mib(budget).c_str());

    // Known native gap: no native renderer calls Rnd::Init, so RndUtlInit never
    // registers ResourceFileCacheHelper, FileCache::Add keys the one .png raw
    // (ui/image/dc_logo_keep.png, not gen/..._xbox), its read fails and the
    // entry holds nothing (size 0).  Harmless -- MakeFile returns null and
    // NewFile falls through to the normal read -- but it is not the image's
    // cache.  Checked both ways so this list goes stale loudly.
    const bool helperMissing = ProbeCache::Helper() == nullptr;
    int cached = 0, expected = 0;
    for (const std::string &p : cfg.paths) {
        const char *ext = FileGetExt(p.c_str());
        const bool resource = ext && (!strcmp(ext, "png") || !strcmp(ext, "bmp"));
        File *f = FileCache::GetFileAll(CachedName(p).c_str());
        if (resource && helperMissing) {
            EXPECT_EQ(f, nullptr) << p << " is cached although ResourceFileCacheHelper is "
                                     "unregistered: update this test";
            delete f;
            continue;
        }
        expected++;
        if (f) {
            cached++;
            delete f;
        } else {
            ADD_FAILURE() << "desktop: persistent cache does not hold " << p << " ("
                          << CachedName(p) << ")";
        }
    }
    printf("[persistent-cache] desktop: %d of %d entries served%s\n", cached, expected,
           helperMissing ? " (the .png skipped: ResourceFileCacheHelper unregistered)" : "");
    EXPECT_EQ(cached, expected);
    for (const std::string &m : mids) {
        File *f = FileCache::GetFileAll(m.c_str());
        EXPECT_NE(f, nullptr) << "LoadingPanel::PlayLoadingMusic would not find " << m;
        delete f;
    }
    delete cache;

    // Negative control: one byte under its own total must evict something.
    ProbeCache *tight = Build(cfg, total - 1);
    int kept = 0;
    for (const std::string &p : cfg.paths) {
        File *f = FileCache::GetFileAll(CachedName(p).c_str());
        if (f) {
            kept++;
            delete f;
        }
    }
    EXPECT_LT(kept, expected)
        << "control: a budget one byte under the set evicted nothing -- the check is vacuous";
    delete tight;
}

TEST_F(PersistentFileCacheTest, WebBytesFitTheWebBudget) {
    if (!EnsureEngineInit())
        return;
    CacheConfig cfg = ReadConfig();
    ASSERT_GT(cfg.budget, 0);
    long long total = 0;
    int present = 0;
    for (const std::string &p : cfg.paths) {
        long long b = WebFileSize(p);
        EXPECT_GE(b, 0) << "web: server.py would 404 " << CachedName(p);
        if (b >= 0) {
            total += b;
            present++;
        }
    }
    ASSERT_EQ(present, (int)cfg.paths.size())
        << "expected every path under " << NativeGetDataDir() << "/extracted";
    const int budget = NativePersistentFileCacheBudget(cfg.budget, true);
    printf("[persistent-cache] web: %d paths, %lld bytes (%s); image budget %s, web budget %s\n",
           present, total, Mib(total).c_str(), Mib(cfg.budget).c_str(), Mib(budget).c_str());
    EXPECT_GT(total, cfg.budget)
        << "control: the inflated set should exceed the image's budget (else the scale is moot)";
    EXPECT_LE(total, budget) << "web: FileCache::DumpOverSize would evict entries (in host "
                                "std::sort order), possibly the loading-music .mid";
}

} // namespace
