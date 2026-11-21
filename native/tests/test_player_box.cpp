// The cursor panel's two player boxes when there is no camera (PLAN H-147,
// native/src/platform/PlayerBox_Native.h).
//
// With no camera stream the boxes are not drawn: the present player's box
// would be an empty frame and the other side's would ask for a player to step
// in front of a sensor that is not there.  These are the in-process tests:
//
//   PlayerBoxRuleTest   the mode words, DC3_PLAYER_BOX's reading and the rule's
//                       whole truth table -- no engine.
//   PlayerBoxScopeTest  NativePlayerBox::DrawScope on a dir holding two groups
//                       with the cursor panel's names: what it hides while it
//                       lives, and that the groups' own `showing` is back
//                       afterwards -- the property the game's scripts read and
//                       write (movie_overlay.dta, rhythm_battle.dta, the party
//                       mode screens).
//
// The whole game, with and without a camera, is test_native_camera_view.cpp.
//
// Sabotage-checked (see the commit): the rule inverted, the restore dropped,
// a wrong group name, and a scope that also shows what a script had hidden
// each fail a test here.

#include "test_helpers.h"
#include "platform/CameraStream_Native.h"
#include "platform/PlayerBox_Native.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "rndobj/Group.h"

#include <string>
#include <utility>

using namespace NativePlayerBox;

namespace {

void PublishOneFrame() {
    NativeCamera::Image img;
    img.width = 4;
    img.height = 4;
    img.rgb.assign(4 * 4 * 3, 0);
    img.mask.assign(4 * 4, 0);
    NativeCamera::Publish(std::move(img));
}

// `"key":<digits>` after the playerBox member's start, or -1.
long Counter(const std::string &json, const char *key) {
    std::string k = std::string("\"") + key + "\":";
    size_t at = json.find(k);
    return at == std::string::npos ? -1 : strtol(json.c_str() + at + k.size(), nullptr, 10);
}

} // namespace

// ---------------------------------------------------------------------------
// The words and the rule
// ---------------------------------------------------------------------------

TEST(PlayerBoxRuleTest, ModeWords) {
    struct Row {
        const char *text;
        Mode mode;
    };
    const Row rows[] = {
        { "auto", kAuto },  { "AUTO", kAuto }, { "on", kOn },     { "1", kOn },
        { "true", kOn },    { "Yes", kOn },    { "always", kOn }, { "off", kOff },
        { "0", kOff },      { "FALSE", kOff }, { "no", kOff },    { "never", kOff },
    };
    for (const Row &row : rows) {
        // start from a mode the word does not name, so a parse that returns
        // true without writing is caught
        Mode mode = row.mode == kAuto ? kOn : kAuto;
        EXPECT_TRUE(ParseMode(row.text, &mode)) << row.text;
        EXPECT_EQ(mode, row.mode) << row.text;
        // the name of a mode parses back to it
        Mode again = row.mode == kAuto ? kOn : kAuto;
        EXPECT_TRUE(ParseMode(ModeName(row.mode), &again)) << ModeName(row.mode);
        EXPECT_EQ(again, row.mode) << ModeName(row.mode);
    }
}

TEST(PlayerBoxRuleTest, NotAModeWord) {
    const char *const bad[] = { nullptr, "", " ", "2", "onn", "auto ", "camera" };
    for (const char *text : bad) {
        Mode mode = kOff;
        EXPECT_FALSE(ParseMode(text, &mode)) << (text ? text : "(null)");
        EXPECT_EQ(mode, kOff) << "a refused word changed the mode";
    }
}

TEST(PlayerBoxRuleTest, TheVariable) {
    bool unknown = true;
    EXPECT_EQ(ModeFromEnv(nullptr, &unknown), kAuto);
    EXPECT_FALSE(unknown) << "unset is the default, not a mistake";
    unknown = true;
    EXPECT_EQ(ModeFromEnv("", &unknown), kAuto);
    EXPECT_FALSE(unknown) << "empty counts as unset";
    unknown = true;
    EXPECT_EQ(ModeFromEnv("on", &unknown), kOn);
    EXPECT_FALSE(unknown);
    unknown = true;
    EXPECT_EQ(ModeFromEnv("0", &unknown), kOff);
    EXPECT_FALSE(unknown);
    unknown = false;
    EXPECT_EQ(ModeFromEnv("sometimes", &unknown), kAuto);
    EXPECT_TRUE(unknown) << "a value that is not a mode word must be reported";
    EXPECT_EQ(ModeFromEnv("sometimes", nullptr), kAuto);
}

TEST(PlayerBoxRuleTest, TruthTable) {
    EXPECT_FALSE(ShouldDraw(kAuto, false)) << "no camera: nothing to show in the boxes";
    EXPECT_TRUE(ShouldDraw(kAuto, true)) << "a live camera: the boxes show the player";
    EXPECT_TRUE(ShouldDraw(kOn, false)) << "on is the switch back: always drawn";
    EXPECT_TRUE(ShouldDraw(kOn, true));
    EXPECT_FALSE(ShouldDraw(kOff, false));
    EXPECT_FALSE(ShouldDraw(kOff, true)) << "off hides them from a camera too";
}

// ---------------------------------------------------------------------------
// The draw scope
// ---------------------------------------------------------------------------

class PlayerBoxScopeTest : public EngineTestFixture {
protected:
    void SetUp() override {
        NativeCamera::ResetForTest();
        ResetForTest();
        SetMode(kAuto); // not the variable: the gate may run with it set
        ASSERT_FALSE(NativeCamera::Active());
        mDir = Hmx::Object::New<ObjectDir>();
        mDir->SetName("player_box_scope_test_dir", ObjectDir::Main());
        for (int i = 0; i < 2; i++) {
            mGroup[i] = Hmx::Object::New<RndGroup>();
            mGroup[i]->SetName(kGroupNames[i], mDir);
            ASSERT_TRUE(mGroup[i]->Showing());
        }
        // a third drawable the scope has no business with
        mOther = Hmx::Object::New<RndGroup>();
        mOther->SetName("all.grp", mDir);
    }
    void TearDown() override {
        delete mDir; // and its objects
        NativeCamera::ResetForTest();
        ResetForTest();
    }
    ObjectDir *mDir = nullptr;
    RndGroup *mGroup[2] = { nullptr, nullptr };
    RndGroup *mOther = nullptr;
};

TEST_F(PlayerBoxScopeTest, TheNamesAreTheCursorPanelsGroups) {
    // What ui/gen/cursor.milo calls them, and what the game's scripts find
    // ({cursor_panel find depth_buffer_left.grp} in ui/movie_overlay.dta).
    EXPECT_STREQ(kGroupNames[0], "depth_buffer_left.grp");
    EXPECT_STREQ(kGroupNames[1], "depth_buffer_right.grp");
}

TEST_F(PlayerBoxScopeTest, NoCameraHidesBothForTheDrawOnly) {
    {
        DrawScope scope(mDir);
        EXPECT_EQ(scope.Hidden(), 2);
        EXPECT_FALSE(mGroup[0]->Showing()) << "the left box would have been drawn";
        EXPECT_FALSE(mGroup[1]->Showing()) << "the right box would have been drawn";
        EXPECT_TRUE(mOther->Showing()) << "the scope hid something that is not a box";
    }
    EXPECT_TRUE(mGroup[0]->Showing()) << "the group's own showing was not put back";
    EXPECT_TRUE(mGroup[1]->Showing()) << "the group's own showing was not put back";
}

TEST_F(PlayerBoxScopeTest, AGroupAScriptHidStaysHidden) {
    mGroup[1]->SetShowing(false); // what movie_overlay.dta does on enter
    {
        DrawScope scope(mDir);
        EXPECT_EQ(scope.Hidden(), 1);
        EXPECT_FALSE(mGroup[0]->Showing());
        EXPECT_FALSE(mGroup[1]->Showing());
    }
    EXPECT_TRUE(mGroup[0]->Showing());
    EXPECT_FALSE(mGroup[1]->Showing()) << "the scope showed a box the game had hidden";
}

TEST_F(PlayerBoxScopeTest, ALiveCameraDrawsThem) {
    PublishOneFrame();
    ASSERT_TRUE(NativeCamera::Active());
    {
        DrawScope scope(mDir);
        EXPECT_EQ(scope.Hidden(), 0);
        EXPECT_TRUE(mGroup[0]->Showing());
        EXPECT_TRUE(mGroup[1]->Showing());
    }
    // ... and they go again when the stream stops
    NativeCamera::Stop();
    ASSERT_FALSE(NativeCamera::Active());
    {
        DrawScope scope(mDir);
        EXPECT_EQ(scope.Hidden(), 2);
    }
    EXPECT_TRUE(mGroup[0]->Showing());
}

TEST_F(PlayerBoxScopeTest, OnDrawsThemWithNoCamera) {
    SetMode(kOn);
    DrawScope scope(mDir);
    EXPECT_EQ(scope.Hidden(), 0);
    EXPECT_TRUE(mGroup[0]->Showing());
    EXPECT_TRUE(mGroup[1]->Showing());
}

TEST_F(PlayerBoxScopeTest, OffHidesThemFromACameraToo) {
    SetMode(kOff);
    PublishOneFrame();
    ASSERT_TRUE(NativeCamera::Active());
    {
        DrawScope scope(mDir);
        EXPECT_EQ(scope.Hidden(), 2);
        EXPECT_FALSE(mGroup[0]->Showing());
        EXPECT_FALSE(mGroup[1]->Showing());
    }
    EXPECT_TRUE(mGroup[0]->Showing());
    EXPECT_TRUE(mGroup[1]->Showing());
}

TEST_F(PlayerBoxScopeTest, NoDirAndNoGroupsAreNoOps) {
    {
        DrawScope scope(nullptr);
        EXPECT_EQ(scope.Hidden(), 0);
    }
    ObjectDir *empty = Hmx::Object::New<ObjectDir>();
    empty->SetName("player_box_scope_test_empty_dir", ObjectDir::Main());
    {
        DrawScope scope(empty);
        EXPECT_EQ(scope.Hidden(), 0);
    }
    delete empty;
}

TEST_F(PlayerBoxScopeTest, TheStateCountsWhatTheDrawsDid) {
    std::string before = StateJson();
    EXPECT_NE(before.find("\"mode\":\"auto\""), std::string::npos) << before;
    EXPECT_NE(before.find("\"drawn\":false"), std::string::npos) << before;
    EXPECT_EQ(Counter(before, "drawsShown"), 0) << before;
    EXPECT_EQ(Counter(before, "drawsHidden"), 0) << before;
    for (int i = 0; i < 3; i++)
        DrawScope scope(mDir);
    std::string hidden = StateJson();
    EXPECT_EQ(Counter(hidden, "drawsHidden"), 3) << hidden;
    EXPECT_EQ(Counter(hidden, "drawsShown"), 0) << hidden;
    EXPECT_EQ(Counter(hidden, "groupsHidden"), 2) << hidden;
    SetMode(kOn);
    for (int i = 0; i < 2; i++)
        DrawScope scope(mDir);
    std::string shown = StateJson();
    EXPECT_NE(shown.find("\"mode\":\"on\""), std::string::npos) << shown;
    EXPECT_NE(shown.find("\"drawn\":true"), std::string::npos) << shown;
    EXPECT_EQ(Counter(shown, "drawsHidden"), 3) << shown;
    EXPECT_EQ(Counter(shown, "drawsShown"), 2) << shown;
    EXPECT_EQ(Counter(shown, "groupsHidden"), 0) << shown;
}
