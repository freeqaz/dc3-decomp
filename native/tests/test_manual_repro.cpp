// Manual reproduction tests that require the full game engine.
// These are skipped in the automated test suite (milo-tests) because
// EngineTestFixture doesn't build the full UI/panel object graph.
// Run inside dc3-native with the appropriate env vars to exercise them.

#include "test_helpers.h"
#include "obj/Dir.h"
#include "ui/UIPanel.h"
#include <cstdlib>
#include <ctime>

namespace {

class ManualReproTest : public EngineTestFixture {};

TEST_F(ManualReproTest, AutosaveWarningPanelUnload) {
    UIPanel *panel = ObjectDir::Main()->Find<UIPanel>("autosave_warning_panel", false);
    if (!panel)
        GTEST_SKIP() << "Requires full UI object graph (run inside dc3-native)";

    panel->CheckLoad();
    ASSERT_TRUE(panel->CheckIsLoaded()) << "panel failed to load";

    std::clock_t start = std::clock();
    panel->CheckUnload();
    double seconds = double(std::clock() - start) / CLOCKS_PER_SEC;
    printf("AutosaveWarningPanelUnload: %.3fs state=%d loadRefs=%d\n",
           seconds, (int)panel->GetState(), panel->IsReferenced());

    EXPECT_EQ(panel->GetState(), UIPanel::kUnloaded);
}

} // namespace
