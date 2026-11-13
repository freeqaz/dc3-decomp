// LoadingPanel's loading music (PLAN H-11).
//
// The image's PlayLoadingMusic finds the era's .mid ONLY in a file cache
// (FileCache::GetFileAll, MILO_ASSERT(f)); App::App puts all 29 there at boot
// (config/persistent_filecache.dta).  Native left that cache out, and this
// file used to pin the HX_NATIVE bypass that hid it ("missing loading music
// does not block the ready state"): 45 of 45 songs loaded with no music.
//
// Now: with the persistent cache built as App::App builds it, PlayLoadingMusic
// finds the .mid and HamMaster loads it; without the cache the lookup misses.
// The stream playing is checked end to end by LoadingMusicFlowTest.

#include "test_helpers.h"

#include "hamobj/HamAudio.h"
#include "meta_ham/LoadingPanel.h"
#include "obj/Data.h"
#include "os/FileCache.h"
#include "os/System.h"
#include "utl/Loader.h"

#include <chrono>
#include <memory>
#include <string>

namespace {

class TestLoadingPanel : public LoadingPanel {
public:
    void ForceState(UIPanel::State state) { mState = state; }
    void CallPlayLoadingMusic() { PlayLoadingMusic(); }
};

// App::App's persistent cache (the same calls, the same config).
FileCache *BuildPersistentCache() {
    DataArray *cfg = SystemConfig("persistent_filecache");
    if (!cfg)
        return nullptr;
    FileCache *cache = new FileCache(cfg->Int(1), kLoadFront, false, true);
    cache->StartSet(0);
    for (int i = 2; i < cfg->Size(); i++)
        cache->Add(cfg->Str(i), 1, "");
    cache->EndSet();
    cache->PollUntilLoaded();
    return cache;
}

class LoadingPanelTest : public EngineTestFixture {
protected:
    void SetUp() override { oldMusic = DataVariable("loading_music_mogg"); }
    void TearDown() override { DataVariable("loading_music_mogg") = oldMusic; }
    DataNode oldMusic;
};

TEST_F(LoadingPanelTest, LoadingMusicLoadsFromThePersistentCache) {
    std::unique_ptr<FileCache> cache(BuildPersistentCache());
    ASSERT_NE(cache, nullptr) << "no (persistent_filecache ...) in the system config";

    // ~LoadingPanel releases the master and the SongDB the ctor made.
    TestLoadingPanel panel;
    LoadingPanel::TestSetLoadingMaster(
        new HamMaster(LoadingPanel::TestGetSongDB()->SongData(), nullptr));
    HamMaster *master = LoadingPanel::TestGetLoadingMaster();
    ASSERT_NE(master, nullptr);
    const char *mogg = "sfx/samples/shell/loadingmusic_80s_02.mogg";
    DataVariable("loading_music_mogg") = mogg;

    panel.ForceState(UIPanel::kDown);
    panel.CallPlayLoadingMusic();
    ASSERT_NE(panel.mSongInfo, nullptr)
        << "PlayLoadingMusic returned before loading: the .mid was not in a file cache";

    // HamMaster parses the .mid (served from the cache) and then starts the
    // .mogg; what the frame loop does while loading_screen waits.
    auto t0 = std::chrono::steady_clock::now();
    while (!master->IsLoaded()
           && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(20)) {
        TheLoadMgr.Poll();
    }
    ASSERT_TRUE(master->IsLoaded()) << "the loading music's .mid never finished loading";
    HamAudio *audio = master->GetAudio();
    ASSERT_NE(audio, nullptr);
    EXPECT_FALSE(audio->Fail()) << mogg;
    // That the stream then plays is an engine-level fact (this harness skips
    // SynthInit): LoadingMusicFlowTest reads LoadingPanel::Unload's
    // "loading music ... streamed N ms" trace from a real dc3-native run.
}

TEST_F(LoadingPanelTest, WithoutTheCacheTheMidIsNotFound) {
    // The control: the same lookup with no persistent cache must miss, or the
    // test above would pass whether App::App builds the cache or not.
    const char *mid = "sfx/samples/shell/loadingmusic_80s_02.mid";
    File *f = FileCache::GetFileAll(mid);
    EXPECT_EQ(f, nullptr) << "a file cache already holds " << mid
                          << " before the persistent cache is built";
    delete f;
    std::unique_ptr<FileCache> cache(BuildPersistentCache());
    ASSERT_NE(cache, nullptr);
    f = FileCache::GetFileAll(mid);
    EXPECT_NE(f, nullptr) << "the persistent cache does not serve " << mid;
    delete f;
}

} // namespace
