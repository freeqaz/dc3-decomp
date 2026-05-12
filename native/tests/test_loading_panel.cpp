#include "test_helpers.h"

#include "meta_ham/LoadingPanel.h"
#include "obj/Data.h"

namespace {

class TestLoadingPanel : public LoadingPanel {
public:
    void ForceState(UIPanel::State state) { mState = state; }
    bool CallIsLoaded() const { return LoadingPanel::IsLoaded(); }
    void CallPlayLoadingMusic() { PlayLoadingMusic(); }
};

class LoadingPanelTest : public EngineTestFixture {};

TEST_F(LoadingPanelTest, MissingLoadingMusicDoesNotBlockNativeReadyState) {
    TestLoadingPanel panel;
    ASSERT_NE(LoadingPanel::TestGetSongDB(), nullptr);

    LoadingPanel::TestSetLoadingMaster(
        new HamMaster(LoadingPanel::TestGetSongDB()->SongData(), nullptr));
    ASSERT_NE(LoadingPanel::TestGetLoadingMaster(), nullptr);

    DataNode oldLoadingMusic = DataVariable("loading_music_mogg");
    DataVariable("loading_music_mogg") = "definitely_missing_loading_music.mogg";

    panel.ForceState(UIPanel::kDown);
    panel.CallPlayLoadingMusic();

    EXPECT_TRUE(panel.CallIsLoaded())
        << "native LoadingPanel should not wait forever when loading music is unavailable";

    DataVariable("loading_music_mogg") = oldLoadingMusic;
}

} // namespace
