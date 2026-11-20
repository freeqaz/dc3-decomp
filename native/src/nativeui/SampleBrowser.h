// SampleBrowser: a mock song browser built only from nativeui widgets and fake
// data. It is the worked example of "how to write a screen", the subject of
// the framework's end-to-end test, and the starting point for the real song
// browser.
//
// What it shows: a title, sort tabs and a search field across the top; a
// "recently played" shelf; a sectioned list of songs beside a detail card; a
// hint bar.
//
// What it does:
//  - Typing anywhere filters the list (title or artist contains the query,
//    ignoring case). Focus does not have to be on the search field: text and
//    edit events nobody consumed are forwarded to it.
//  - NextTab/PrevTab change the sort from anywhere; the list is re-sectioned
//    (by first letter, by artist, by skill level).
//  - Moving the selection in the list or the shelf updates the detail card.
//
// Nothing here is game code: songs are generated, images are bare handles.
#pragma once

#include "nativeui/Clock.h"
#include "nativeui/Image.h"
#include "nativeui/Label.h"
#include "nativeui/ListView.h"
#include "nativeui/Screen.h"
#include "nativeui/Shelf.h"
#include "nativeui/TabStrip.h"
#include "nativeui/TextField.h"

#include <memory>
#include <string>
#include <vector>

namespace nativeui {

    struct SampleSong {
        std::string title;
        std::string artist;
        int skill = 0; // 0..6, see SampleSkillName
        ImageHandle art; // null for the songs whose art "has not loaded"
    };

    // `count` made-up songs, the same ones every time. Some titles and artists
    // have accented letters; every third song has no art.
    std::vector<SampleSong> MakeSampleSongs(int count);
    const char *SampleSkillName(int skill);

    class SampleBrowser {
    public:
        enum Sort {
            kByTitle = 0,
            kByArtist = 1,
            kBySkill = 2
        };

        // One row of the song list: a section header or a song.
        struct Entry {
            bool isHeader = false;
            std::string heading; // headers only
            int song = -1; // index into Songs(); songs only
        };

        SampleBrowser(Size viewport, const TextMeasurer &text, const Clock &clock);
        SampleBrowser(const SampleBrowser &) = delete;
        SampleBrowser &operator=(const SampleBrowser &) = delete;

        bool HandleEvent(const Event &event) { return screen_.HandleEvent(event); }
        const DisplayList &BuildFrame() { return screen_.BuildFrame(); }
        Screen &GetScreen() { return screen_; }

        TextField &Search() { return *search_; }
        TabStrip &Tabs() { return *tabs_; }
        Shelf &Recent() { return *recent_; }
        ListView &SongList() { return *songList_; }

        const std::vector<SampleSong> &Songs() const { return songs_; }
        // The list's rows for the current sort and search.
        const std::vector<Entry> &Entries() const { return entries_; }
        // The song the detail card shows, or null when the list is empty.
        const SampleSong *DetailSong() const;

    private:
        std::unique_ptr<Widget> BuildTree();
        std::unique_ptr<Widget> BuildRow(int index) const;
        std::unique_ptr<Widget> BuildRecentCard(int index) const;
        void RebuildRows();
        void ShowDetail(int song);

        Screen screen_;
        std::vector<SampleSong> songs_;
        std::vector<int> recentSongs_; // indices into songs_
        std::vector<Entry> entries_;
        int detailSong_ = -1;

        // Owned by the tree in screen_.
        TextField *search_ = nullptr;
        TabStrip *tabs_ = nullptr;
        Shelf *recent_ = nullptr;
        ListView *songList_ = nullptr;
        Image *detailArt_ = nullptr;
        Label *detailTitle_ = nullptr;
        Label *detailArtist_ = nullptr;
        Label *detailSkill_ = nullptr;
    };

}
