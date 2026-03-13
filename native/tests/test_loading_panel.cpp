#include "test_helpers.h"

#include "meta_ham/LoadingPanel.h"
#include "obj/Data.h"
#include "os/ContentMgr.h"

namespace {

class TestLoadingPanel : public LoadingPanel {
public:
    void ForceState(UIPanel::State state) { mState = state; }
    bool CallIsLoaded() const { return LoadingPanel::IsLoaded(); }
    void CallPlayLoadingMusic() { PlayLoadingMusic(); }
};

class LoadingPanelTest : public EngineTestFixture {};

TEST_F(LoadingPanelTest, MissingLoadingMusicDoesNotBlockNativeReadyState) {
    if (!TheContentMgr.RefreshDone()) {
        GTEST_SKIP() << "ContentMgr still refreshing; readiness gate is not stable yet";
    }

    TestLoadingPanel panel;
    ASSERT_NE(LoadingPanel::sSongDB, nullptr);

    LoadingPanel::sLoadingMaster =
        new HamMaster(LoadingPanel::sSongDB->SongData(), nullptr);
    ASSERT_NE(LoadingPanel::sLoadingMaster, nullptr);

    DataNode oldLoadingMusic = DataVariable("loading_music_mogg");
    DataVariable("loading_music_mogg") = "definitely_missing_loading_music.mogg";

    panel.ForceState(UIPanel::kDown);
    panel.CallPlayLoadingMusic();

    EXPECT_TRUE(panel.CallIsLoaded())
        << "native LoadingPanel should not wait forever when loading music is unavailable";

    DataVariable("loading_music_mogg") = oldLoadingMusic;
}

} // namespace
