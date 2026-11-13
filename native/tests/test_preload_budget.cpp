// Song preload budget: every song's preload set must fit the budget the game
// computes, as each native platform reads the files.
//
// loading.dta's PRELOAD_PANEL_COMMON gives the song preload cache
// (PreloadPanel::sCache, a FileCache) `max_cache_size` = 16 MiB for three
// files: barks.milo, <song>.milo and <song>.mid.  That is a budget in ON-DISC
// bytes: the 360 reads the compressed .milo_xbox from the ark.  Over budget,
// FileCache::DumpOverSize evicts an entry, PreloadPanel::CheckFileCached fails,
// on_preload_failed -> data_error -> the game goes back to the title screen
// with no message.  185b6f50e scales the budget x4 under HX_NATIVE.
//
// The two native platforms read DIFFERENT bytes for the same path:
//   * desktop (dc3-native, and this test process) reads the ark
//     (gen/main_xbox.hdr), where song milos are the compressed .milo_xbox --
//     the 360's own bytes; the largest set is ~13 MiB, under even the unscaled
//     16 MiB, so desktop never hit the bug (measured 2026-11-13: omg with the
//     x4 removed plays to gameplay on desktop);
//   * web (dc3-web) fetches /api/file/<path>, which native/web/server.py
//     serves from the INFLATED extraction ($DC3_DATA/extracted, extract_ark.sh
//     --inflate), 2.2-2.6x larger: 34 of the 57 song sets exceed 16 MiB (omg
//     25.20 MiB).  That is where the bug was seen.
//
// Both sides of each comparison come from the game data, so a change to
// either is caught:
//   * the budget: a PreloadPanel is given the real `{new PreloadPanel
//     preload_panel ...}` array from ui/loading/loading.dta through SetTypeDef,
//     exactly as DataNew does at boot, and mMaxCacheSize is read back (the
//     HX_NATIVE scale included);
//   * the bytes: for every song in songs/songs.dta the three paths are built
//     the way HamSongMgr builds them (barks_file / song_file_path ".milo" /
//     midi_file).  Desktop: handed to the real FileCache (PreloadPanel::sCache)
//     the way PreloadPanel::StartCache does -- Clear, SetSize(budget),
//     StartSet, Add x3, EndSet -- and polled as PollForLoading polls until
//     DoneCaching; each file must still be FileCached (real loader, real
//     FileCache::Poll -> DumpOverSize).  Web: the files server.py would serve,
//     summed, as DumpOverSize compares them (CurSize > mMaxSize evicts).
// A negative control drives the real cache one byte under the largest set and
// requires an eviction, so the cache assertion is shown able to fail.
//
// Sabotage (2026-11-13): with the `mMaxCacheSize *= 4` line removed,
// WebReadsInflatedFiles fails for 34 songs (omg 25.20 of 16.00 MiB) and
// DesktopReadsTheArk still passes -- as the game does on each platform.

#include "test_helpers.h"

#include "meta/PreloadPanel.h"
#include "obj/Data.h"
#include "obj/DataFile.h"
#include "obj/DirLoader.h"
#include "os/File.h"
#include "os/FileCache.h"
#include "os/System.h"
#include "utl/FilePath.h"
#include "utl/Loader.h"
#include "utl/MakeString.h"

extern const char *NativeGetDataDir();

#include <chrono>
#include <sys/stat.h>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Reads PreloadPanel's protected budget; everything else is the real class.
class BudgetProbePanel : public PreloadPanel {
public:
    int Budget() const { return mMaxCacheSize; }
};

// The `{new PreloadPanel <name> ...}` command in a parsed loading.dta, or null.
DataArray *FindPreloadPanelNew(DataArray *file, const char *panelName) {
    for (int i = 0; i < file->Size(); i++) {
        if (file->Type(i) != kDataCommand)
            continue;
        DataArray *cmd = file->Command(i);
        if (cmd->Size() < 3 || cmd->Type(0) != kDataSymbol || cmd->Type(1) != kDataSymbol)
            continue;
        if (cmd->Sym(0) != "new" || cmd->Sym(1) != "PreloadPanel")
            continue;
        if (cmd->Type(2) == kDataSymbol && cmd->Sym(2) == panelName)
            return cmd;
    }
    return nullptr;
}

struct SongPreload {
    std::string song;
    std::string files[3]; // barks, milo, mid -- as preload_files lists them
    long long bytes[3] = {-1, -1, -1};
    long long Total() const {
        long long t = 0;
        for (long long b : bytes)
            t += b > 0 ? b : 0;
        return t;
    }
};

// HamSongMgr::BarksFile / SongMgr::SongFilePath(song, ".milo", 0) /
// FakeSongMgr::MidiFile for an on-disc song.  BarksFile asks SongPath for
// version 10 (11 in Norwegian), which redirects to songs/updates/ when the
// song's metadata version is older; every on-disc song is version 11, but the
// redirect is mirrored so a song that needs it is not silently mis-sized.
SongPreload PreloadFor(DataArray *entry) {
    SongPreload p;
    p.song = entry->Sym(0).Str();
    DataArray *songArr = entry->FindArray("song", false);
    DataArray *nameArr = songArr ? songArr->FindArray("name", false) : nullptr;
    std::string base = nameArr ? nameArr->Str(1) : "";
    int version = 0;
    if (DataArray *v = entry->FindArray("version", false))
        version = v->Int(1);
    Symbol lang = SystemLanguage();
    int barksVer = lang == "nor" ? 11 : 10;
    std::string barksBase = base;
    if (version < barksVer) {
        const char *b = FileGetBase(base.c_str());
        barksBase = MakeString("songs/updates/%s/%s", b, b);
    }
    p.files[0] = MakeString("%s/loc/%s/barks.milo", FileGetPath(barksBase.c_str()), lang.Str());
    p.files[1] = base + ".milo";
    p.files[2] = base + ".mid";
    return p;
}

// Size of a file as the native File layer opens it (what FileLoader reads into
// the cache entry): .milo through DirLoader::CachedPath, as FileCache::Add does.
long long NativeFileSize(const std::string &path) {
    std::string real = path;
    const char *ext = FileGetExt(path.c_str());
    if (ext && !strcmp(ext, "milo"))
        real = DirLoader::CachedPath(path.c_str(), false);
    if (!FileExists(real.c_str(), 0, nullptr))
        return -1;
    File *f = NewFile(real.c_str(), FILE_OPEN_READ);
    if (!f)
        return -1;
    long long size = f->Size();
    delete f;
    return size;
}

// Where server.py finds a file the web build asks for (resolve_asset_path):
// $DC3_DATA/extracted/<cached path>, .milo through DirLoader::CachedPath as
// FileCache::Add does.  -1 if absent.
long long WebFileSize(const std::string &path) {
    const char *dataDir = NativeGetDataDir();
    if (!dataDir || !*dataDir)
        return -1;
    std::string rel = path;
    const char *ext = FileGetExt(path.c_str());
    if (ext && !strcmp(ext, "milo"))
        rel = DirLoader::CachedPath(path.c_str(), false);
    struct stat st;
    std::string full = std::string(dataDir) + "/extracted/" + rel;
    if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return -1;
    return (long long)st.st_size;
}

std::string Mib(long long b) { return MakeString("%.2f MiB", b / 1048576.0); }

class PreloadBudgetTest : public ::testing::Test {
protected:
    // The budget the game computes, from the real loading.dta; 0 on failure
    // (with a gtest failure recorded).
    static int GameBudget() {
        DataArray *loading = DataReadFile("ui/loading/loading.dta", true);
        if (!loading) {
            ADD_FAILURE() << "ui/loading/loading.dta did not parse";
            return 0;
        }
        int budgets[2] = {0, 0};
        const char *panels[2] = {"preload_panel", "practice_preload_panel"};
        for (int i = 0; i < 2; i++) {
            DataArray *cmd = FindPreloadPanelNew(loading, panels[i]);
            if (!cmd) {
                ADD_FAILURE() << "no {new PreloadPanel " << panels[i]
                              << " ...} in ui/loading/loading.dta";
                loading->Release();
                return 0;
            }
            BudgetProbePanel panel;
            panel.SetTypeDef(cmd); // what DataNew does with this array
            budgets[i] = panel.Budget();
        }
        loading->Release();
        EXPECT_EQ(budgets[0], budgets[1])
            << "preload_panel and practice_preload_panel share PRELOAD_PANEL_COMMON";
        printf("[preload-budget] budget the game computes: %d bytes (%s)\n", budgets[0],
               Mib(budgets[0]).c_str());
        return budgets[0];
    }

    static std::vector<SongPreload> AllSongs() {
        std::vector<SongPreload> all;
        DataArray *songs = DataReadFile("songs/songs.dta", true);
        if (!songs) {
            ADD_FAILURE() << "songs/songs.dta did not parse";
            return all;
        }
        for (int i = 0; i < songs->Size(); i++) {
            if (songs->Type(i) != kDataArray)
                continue;
            DataArray *entry = songs->Array(i);
            if (entry->Size() < 2 || entry->Type(0) != kDataSymbol)
                continue;
            all.push_back(PreloadFor(entry));
        }
        songs->Release();
        return all;
    }

    // PreloadPanel::StartCache + PollForLoading on the real cache.  Returns
    // false if the cache never finished.
    static bool RunCache(FileCache *cache, const SongPreload &p, int budget) {
        cache->Clear();
        cache->SetSize(budget);
        cache->StartSet(0);
        for (int f = 0; f < 3; f++)
            cache->Add(p.files[f].c_str(), 1, p.files[f].c_str());
        cache->EndSet();
        auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60)) {
            TheLoadMgr.Poll();
            if (cache->DoneCaching()) {
                FileCache::PollAll();
                return true;
            }
            FileCache::PollAll();
        }
        return false;
    }

    static int CachedCount(FileCache *cache, const SongPreload &p) {
        int n = 0;
        for (int f = 0; f < 3; f++)
            if (p.bytes[f] >= 0 && cache->FileCached(p.files[f].c_str()))
                n++;
        return n;
    }

    static int Present(const SongPreload &p) {
        int n = 0;
        for (long long b : p.bytes)
            n += b >= 0;
        return n;
    }

    void ExpectFits(const SongPreload &p, int budget, const char *platform) {
        EXPECT_LE(p.Total(), budget)
            << platform << " " << p.song << ": barks " << p.bytes[0] << " + milo "
            << p.bytes[1] << " + mid " << p.bytes[2] << " = " << p.Total() << " bytes ("
            << Mib(p.Total()) << ") > preload budget " << budget << " (" << Mib(budget)
            << "): FileCache evicts, CheckFileCached fails and the song bounces to the "
               "title screen";
    }
};

TEST_F(PreloadBudgetTest, DesktopReadsTheArk) {
    if (!EnsureEngineInit())
        return;
    const int budget = GameBudget();
    ASSERT_GT(budget, 0);
    std::vector<SongPreload> all = AllSongs();
    ASSERT_NE(PreloadPanel::sCache, nullptr) << "PreloadPanel's ctor creates sCache";
    FileCache *cache = PreloadPanel::sCache;

    int examined = 0;
    const SongPreload *largest = nullptr;
    for (SongPreload &p : all) {
        for (int f = 0; f < 3; f++)
            p.bytes[f] = NativeFileSize(p.files[f]);
        if (p.bytes[1] <= 0)
            continue; // no song milo (hb_camp_* stubs): nothing to preload
        examined++;
        if (!largest || p.Total() > largest->Total())
            largest = &p;
        ExpectFits(p, budget, "desktop");
        ASSERT_TRUE(RunCache(cache, p, budget)) << p.song << ": cache did not finish in 60 s";
        EXPECT_EQ(CachedCount(cache, p), Present(p))
            << "desktop " << p.song << ": the preload cache evicted a file (budget " << budget
            << ", set " << p.Total() << ")";
    }
    ASSERT_NE(largest, nullptr);
    printf("[preload-budget] desktop: %d songs, largest %s %s\n", examined,
           largest->song.c_str(), Mib(largest->Total()).c_str());
    EXPECT_GE(examined, 57) << "every song milo should resolve; did the paths break?";

    // Negative control: the same cache one byte short of the largest set must
    // evict, or the FileCached assertions above could not fail.
    ASSERT_TRUE(RunCache(cache, *largest, (int)largest->Total() - 1));
    EXPECT_LT(CachedCount(cache, *largest), Present(*largest))
        << "control: a budget one byte under " << largest->song
        << "'s set evicted nothing -- the cache check above is vacuous";
    cache->Clear();
}

TEST_F(PreloadBudgetTest, WebReadsInflatedFiles) {
    if (!EnsureEngineInit())
        return;
    const int budget = GameBudget();
    ASSERT_GT(budget, 0);
    std::vector<SongPreload> all = AllSongs();
    int examined = 0, over16 = 0;
    const SongPreload *largest = nullptr;
    for (SongPreload &p : all) {
        for (int f = 0; f < 3; f++)
            p.bytes[f] = WebFileSize(p.files[f]);
        if (p.bytes[1] <= 0)
            continue;
        examined++;
        if (p.Total() > 16 * 1024 * 1024)
            over16++;
        if (!largest || p.Total() > largest->Total())
            largest = &p;
        ExpectFits(p, budget, "web");
    }
    // The web build reads the extraction; with game data present it must be
    // there (server.py serves nothing else).  0 examined = paths broke.
    ASSERT_GE(examined, 57) << "expected every song milo under " << NativeGetDataDir()
                            << "/extracted (what native/web/server.py serves)";
    printf("[preload-budget] web: %d songs, largest %s %s, %d over loading.dta's "
           "unscaled 16 MiB\n",
           examined, largest->song.c_str(), Mib(largest->Total()).c_str(), over16);
}

} // namespace
