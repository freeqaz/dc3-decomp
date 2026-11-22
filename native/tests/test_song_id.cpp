// The id of a song whose songs.dta gives a name (platform/SongId_Native.h).
//
// A community package writes `(song_id thefox)`.  The image's DataNode::Int
// logs a MILO_FAIL on it and returns half of the symbol's address, so the id
// changed whenever the file was read by a new process and the song's saved
// scores were lost with it.  A name is now a number that depends on the name
// alone, in a range no real song uses; an integer is passed through.
//
// In-process, no engine: the rule, the node reading and the collision answer.
// The whole game with a named package is the two-launch check in the commit
// that added this file.
//
// Sabotage-checked (see the commit): an integer no longer passed through, a
// changed hash, the range bit dropped, a constant id, the symbol handed to
// DataNode::Int, a second log line, a collision answer that is always "no",
// and a backup that is never read each fail a test here.

#include "test_helpers.h"
#include "platform/SongId_Native.h"
#include "obj/Data.h"
#include "obj/DataFile.h"
#include "utl/Symbol.h"

#include <climits>
#include <cstdio>
#include <set>
#include <string>

namespace {

// The largest id any real song in this checkout has (the DC2 DLC pack).
const int kLargestRealId = 10001040;

// The Fox, by hand: FNV-1a-32("thefox") = 0x57836D91; its top two bits (01)
// xor'd into the low 30 give 0x17836D90; plus 0x40000000.
const int kTheFoxId = 1468231056; // 0x57836D90

int CountOf(const std::string &text, const std::string &what) {
    int count = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1))
        count++;
    return count;
}

// The song entry of a one-song songs.dta; the file must be Release()d.
DataArray *SongOf(DataArray *file) { return file->Array(0); }

} // namespace

// Symbol and the DTA parser, no engine.
class NativeSongIdTest : public SymbolTestFixture {};

TEST_F(NativeSongIdTest, AnIntegerIsPassedThrough) {
    const int ids[] = { 7001, 7075, 6001, kLargestRealId, 1,       0,      -1,
                        -2,   NativeSongId::kCustomMin,   INT_MAX, INT_MIN };
    for (int id : ids) {
        EXPECT_EQ(NativeSongId::FromNode(DataNode(id), nullptr), id);
    }

    DataArray *file = DataReadString("(ymca (name \"Y.M.C.A.\") (song_id 7001))");
    ASSERT_TRUE(file);
    int songID = 0;
    EXPECT_TRUE(NativeSongId::Find(SongOf(file), Symbol("song_id"), songID));
    EXPECT_EQ(songID, 7001);
    file->Release();
}

TEST_F(NativeSongIdTest, TheFoxHasItsPinnedId) {
    EXPECT_EQ(NativeSongId::Fnv1a32("thefox"), 0x57836D91u);
    EXPECT_EQ(NativeSongId::FromName("thefox"), kTheFoxId);
    EXPECT_EQ(NativeSongId::FromName("thefox2"), 1726322584);

    // As the package has it, a symbol; and as a string.
    EXPECT_EQ(NativeSongId::FromNode(DataNode(Symbol("thefox")), nullptr), kTheFoxId);
    EXPECT_EQ(NativeSongId::FromNode(DataNode("thefox"), nullptr), kTheFoxId);

    DataArray *file = DataReadString(
        "(thefox (name \"The Fox (What Does the Fox Say?)\") (song_id thefox) (game_origin ham3_dlc))"
    );
    ASSERT_TRUE(file);
    int songID = 0;
    EXPECT_TRUE(NativeSongId::Find(SongOf(file), Symbol("song_id"), songID));
    EXPECT_EQ(songID, kTheFoxId);
    file->Release();
}

TEST_F(NativeSongIdTest, ANamedIdIsInTheCustomRange) {
    EXPECT_EQ(NativeSongId::kCustomMin, 0x40000000);
    EXPECT_EQ(NativeSongId::kCustomMax, INT_MAX);

    std::set<int> seen;
    char name[32];
    for (int i = 0; i < 20000; i++) {
        snprintf(name, sizeof(name), "custom_%d", i);
        int id = NativeSongId::FromName(name);
        ASSERT_GE(id, NativeSongId::kCustomMin) << name;
        ASSERT_LE(id, NativeSongId::kCustomMax) << name;
        ASSERT_GT(id, kLargestRealId) << name;
        ASSERT_TRUE(NativeSongId::IsCustom(id)) << name;
        seen.insert(id);
    }
    // 20000 names in 2^30 slots: a collision or two is chance, a pile is a
    // broken hash.
    EXPECT_GT((int)seen.size(), 19990);

    // No real id and no sentinel is in the range.
    const int real[] = { 6001, 7075, kLargestRealId, 0, -1, -2, INT_MIN };
    for (int id : real) {
        EXPECT_FALSE(NativeSongId::IsCustom(id)) << id;
    }
}

TEST_F(NativeSongIdTest, TwoNamesHaveTwoIds) {
    EXPECT_NE(NativeSongId::FromName("thefox"), NativeSongId::FromName("thefox2"));
    EXPECT_NE(NativeSongId::FromName("thefox"), NativeSongId::FromName("TheFox"));
    EXPECT_NE(NativeSongId::FromName("thefox"), NativeSongId::FromName("xofeht"));
    EXPECT_NE(NativeSongId::FromName("a"), NativeSongId::FromName("b"));
}

TEST_F(NativeSongIdTest, NoNameIsNoId) {
    EXPECT_EQ(NativeSongId::FromName(""), 0);
    EXPECT_EQ(NativeSongId::FromName(nullptr), 0);

    DataArray *file = DataReadString("(nosongid (name \"No Id\"))");
    ASSERT_TRUE(file);
    int songID = 1234;
    EXPECT_FALSE(NativeSongId::Find(SongOf(file), Symbol("song_id"), songID));
    EXPECT_EQ(songID, 1234) << "an absent song_id must leave the caller's value alone";
    file->Release();
}

// GetSongID (SongMgr.cpp): the song's own entry first, then the backup's.
TEST_F(NativeSongIdTest, TheBackupIsReadWhenTheSongHasNoId) {
    DataArray *named = DataReadString("(thefox (song_id thefox))");
    DataArray *integer = DataReadString("(ymca (song_id 7001))");
    DataArray *none = DataReadString("(nosongid (name \"No Id\"))");
    DataArray *zero = DataReadString("(zerosong (song_id 0))");
    ASSERT_TRUE(named && integer && none && zero);
    Symbol key("song_id");

    EXPECT_EQ(NativeSongId::FindWithBackup(SongOf(named), nullptr, key), kTheFoxId);
    EXPECT_EQ(NativeSongId::FindWithBackup(SongOf(named), SongOf(integer), key), kTheFoxId)
        << "the song's own id wins over the backup's";
    EXPECT_EQ(NativeSongId::FindWithBackup(SongOf(integer), SongOf(named), key), 7001);
    EXPECT_EQ(NativeSongId::FindWithBackup(SongOf(none), SongOf(named), key), kTheFoxId);
    EXPECT_EQ(NativeSongId::FindWithBackup(SongOf(zero), SongOf(integer), key), 7001);
    EXPECT_EQ(NativeSongId::FindWithBackup(SongOf(none), nullptr, key), 0);
    EXPECT_EQ(NativeSongId::FindWithBackup(SongOf(none), SongOf(none), key), 0);

    named->Release();
    integer->Release();
    none->Release();
    zero->Release();
}

// The deliberate case is quiet: no MILO_FAIL, and one line per name however
// often the song is read (the game reads it twice: the manager, then the
// metadata).
TEST_F(NativeSongIdTest, ANamedIdLogsOneLineAndNoFail) {
    DataArray *file = DataReadString("(onelinesong (song_id onelinesong))");
    ASSERT_TRUE(file);

    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    int first = 0, second = 0;
    NativeSongId::Find(SongOf(file), Symbol("song_id"), first);
    NativeSongId::Find(SongOf(file), Symbol("song_id"), second);
    int third = NativeSongId::FromNode(SongOf(file)->FindArray(Symbol("song_id"), false)->Node(1), nullptr);
    fflush(stdout);
    fflush(stderr);
    std::string out = testing::internal::GetCapturedStdout();
    std::string err = testing::internal::GetCapturedStderr();
    file->Release();

    EXPECT_EQ(first, NativeSongId::FromName("onelinesong"));
    EXPECT_EQ(second, first);
    EXPECT_EQ(third, first);
    EXPECT_EQ(CountOf(out + err, "DC3 SongId: song_id 'onelinesong'"), 1) << out << err;
    EXPECT_EQ(CountOf(out + err, "is not Int"), 0) << out << err;
    EXPECT_EQ(CountOf(out + err, "FAIL"), 0) << out << err;
}

TEST_F(NativeSongIdTest, ACollisionIsRefusedWithOneLine) {
    // Two names on one id, found by search: the hash has collisions, as any
    // 30-bit number must.
    const int shared = 1223388251;
    ASSERT_EQ(NativeSongId::FromName("song47674"), shared);
    ASSERT_EQ(NativeSongId::FromName("song212103"), shared);

    testing::internal::CaptureStdout();
    bool refused = NativeSongId::Collides(shared, "song47674", "song212103");
    fflush(stdout);
    std::string out = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(refused);
    EXPECT_EQ(CountOf(out, "\n"), 1) << out;
    EXPECT_EQ(CountOf(out, "DC3 SongId: COLLISION"), 1) << out;
    EXPECT_NE(out.find("'song212103' is NOT loaded"), std::string::npos) << out;
    EXPECT_NE(out.find("'song47674'"), std::string::npos) << out;
    EXPECT_NE(out.find("1223388251"), std::string::npos) << out;

    // A name landing on an id that another package wrote as an integer, in
    // either order of arrival.
    testing::internal::CaptureStdout();
    EXPECT_TRUE(NativeSongId::Collides(kTheFoxId, "integersong", "thefox"));
    EXPECT_TRUE(NativeSongId::Collides(kTheFoxId, "thefox", "integersong"));
    fflush(stdout);
    out = testing::internal::GetCapturedStdout();
    EXPECT_EQ(CountOf(out, "DC3 SongId: COLLISION"), 2) << out;

    // Not collisions, and silent: nothing has the id; the same song again
    // (the game's own "found twice"); an id outside the custom range (the
    // game's own duplicate handling, untouched).
    testing::internal::CaptureStdout();
    EXPECT_FALSE(NativeSongId::Collides(kTheFoxId, nullptr, "thefox"));
    EXPECT_FALSE(NativeSongId::Collides(kTheFoxId, "thefox", "thefox"));
    EXPECT_FALSE(NativeSongId::Collides(7001, "ymca", "othersong"));
    EXPECT_FALSE(NativeSongId::Collides(0, "ymca", "othersong"));
    fflush(stdout);
    out = testing::internal::GetCapturedStdout();
    EXPECT_EQ(out, "");
}
