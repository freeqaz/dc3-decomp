#pragma once
// DC3 Native Port - what song select remembers across launches
// (docs/native/SONG_SELECT.md).
//
// Two things the image has no place for: the songs most recently played, in
// any mode, and the sort last chosen on song select.  Neither fits the
// profile save -- that is a fixed-size image (SongStatusMgr::SaveSize), and
// its last-played time is only stamped when a Perform song is finished -- so
// they live in one small text file beside it:
//
//     $DC3_SAVE_DIR/song_select.txt
//
//     dc3-song-select 1
//     sort by_artist
//     played ymca            <- newest first
//     played starships
//
// The file belongs to the machine, not to a profile: whoever is signed in
// sees the same list.  A missing or unreadable file is an empty history and
// no remembered sort; unknown lines are skipped, so a newer build's file
// still loads.
//
// No engine types here on purpose: the unit tests drive this class against a
// scratch file (native/tests/test_native_song_select.cpp).

#include <string>
#include <vector>

class NativeSongSelectState {
public:
    // More than the six rows song select shows, so a song that is no longer
    // installed (a removed DLC pack) does not shorten the visible list.
    enum { kMaxHistory = 24 };

    explicit NativeSongSelectState(const std::string &path);

    // Forget what is in memory and read the file again.
    void Load();
    // Write the file (to a temporary beside it, then rename).  False when it
    // could not be written; the state in memory is kept either way.
    bool Save() const;

    // `song` was played now: it moves to the front, once.  Saves.
    void NotePlayed(const std::string &song);
    // Newest first, no duplicates, at most kMaxHistory.
    const std::vector<std::string> &History() const { return mHistory; }

    // The sort last chosen ("" when none was).  SetSort saves when it changes.
    const std::string &Sort() const { return mSort; }
    void SetSort(const std::string &sort);

    const std::string &Path() const { return mPath; }

private:
    std::string mPath;
    std::string mSort;
    std::vector<std::string> mHistory;
};

// The state of this install: $DC3_SAVE_DIR/song_select.txt, read on first use.
NativeSongSelectState &TheNativeSongSelectState();
