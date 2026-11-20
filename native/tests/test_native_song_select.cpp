// Song select on native: Recently Played, sort by artist, section jumps, the
// remembered sort and the classic switch (docs/native/SONG_SELECT.md).
//
// Two kinds of test:
//
//   * NativeSongSelectStateTest -- the file song select keeps beside the
//     saves ($DC3_SAVE_DIR/song_select.txt), driven against a scratch path.
//     No engine, runs everywhere.
//   * SongSelect*FlowTest (further down) -- the real screens in a child
//     dc3-native, driven over the HTTP debug server.  Gated by
//     DC3_DTA_FLOW_TESTS=1 like every other flow test (needs the game assets).

#include <gtest/gtest.h>

#include "platform/SongSelectState_Native.h"
#include "test_tmp.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string ReadFile(const std::string &path) {
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

void WriteFile(const std::string &path, const std::string &text) {
    std::ofstream out(path);
    out << text;
}

std::vector<std::string> Songs(std::initializer_list<const char *> names) {
    return std::vector<std::string>(names.begin(), names.end());
}

} // namespace

// The newest song is first, and a song played again moves up rather than
// appearing twice.
TEST(NativeSongSelectStateTest, HistoryIsNewestFirstWithoutDuplicates) {
    ScopedTestDir dir("dc3-songselect");
    ASSERT_TRUE(dir.ok());
    NativeSongSelectState state(dir.path() + "/song_select.txt");
    state.NotePlayed("ymca");
    state.NotePlayed("starships");
    state.NotePlayed("macarena");
    EXPECT_EQ(state.History(), Songs({ "macarena", "starships", "ymca" }));
    state.NotePlayed("ymca");
    EXPECT_EQ(state.History(), Songs({ "ymca", "macarena", "starships" }));
}

// What one launch wrote is what the next one reads: this is "the history
// survives a relaunch" at the level of the file.
TEST(NativeSongSelectStateTest, HistoryAndSortSurviveAReload) {
    ScopedTestDir dir("dc3-songselect");
    ASSERT_TRUE(dir.ok());
    std::string path = dir.path() + "/song_select.txt";
    {
        NativeSongSelectState state(path);
        state.NotePlayed("ymca");
        state.NotePlayed("starships");
        state.SetSort("by_artist");
    }
    NativeSongSelectState reloaded(path);
    reloaded.Load();
    EXPECT_EQ(reloaded.History(), Songs({ "starships", "ymca" }));
    EXPECT_EQ(reloaded.Sort(), "by_artist");
    EXPECT_EQ(ReadFile(path),
              "dc3-song-select 1\nsort by_artist\nplayed starships\nplayed ymca\n");
}

TEST(NativeSongSelectStateTest, HistoryIsCapped) {
    ScopedTestDir dir("dc3-songselect");
    ASSERT_TRUE(dir.ok());
    std::string path = dir.path() + "/song_select.txt";
    NativeSongSelectState state(path);
    for (int i = 0; i < NativeSongSelectState::kMaxHistory + 10; i++) {
        state.NotePlayed("song" + std::to_string(i));
    }
    ASSERT_EQ((int)state.History().size(), (int)NativeSongSelectState::kMaxHistory);
    EXPECT_EQ(state.History().front(),
              "song" + std::to_string(NativeSongSelectState::kMaxHistory + 9));
    NativeSongSelectState reloaded(path);
    reloaded.Load();
    EXPECT_EQ(reloaded.History(), state.History());
}

// No file, a file that is not ours, and lines nobody wrote: all an empty or
// partial state, never a crash and never a row with a made-up name.
TEST(NativeSongSelectStateTest, MissingAndForeignFilesAreAnEmptyState) {
    ScopedTestDir dir("dc3-songselect");
    ASSERT_TRUE(dir.ok());
    std::string path = dir.path() + "/song_select.txt";

    NativeSongSelectState missing(path);
    missing.Load();
    EXPECT_TRUE(missing.History().empty());
    EXPECT_TRUE(missing.Sort().empty());

    WriteFile(path, "something else\nplayed ymca\n");
    NativeSongSelectState foreign(path);
    foreign.Load();
    EXPECT_TRUE(foreign.History().empty()) << "a file without our first line is not read";

    WriteFile(path,
              "dc3-song-select 1\n"
              "played ymca\n"
              "volume 11\n"            // a key a newer build might write
              "played\n"               // no name
              "played ymca\n"          // twice
              "\n"
              "played starships\n"
              "sort by_song\n");
    NativeSongSelectState mixed(path);
    mixed.Load();
    EXPECT_EQ(mixed.History(), Songs({ "ymca", "starships" }));
    EXPECT_EQ(mixed.Sort(), "by_song");
}

// A save dir that cannot be written loses the history on exit, not the
// session: the state in memory still answers.
TEST(NativeSongSelectStateTest, UnwritablePathKeepsTheStateInMemory) {
    NativeSongSelectState state("/nonexistent-dc3-dir/song_select.txt");
    state.NotePlayed("ymca");
    state.SetSort("by_artist");
    EXPECT_EQ(state.History(), Songs({ "ymca" }));
    EXPECT_EQ(state.Sort(), "by_artist");
    EXPECT_FALSE(state.Save());
}
