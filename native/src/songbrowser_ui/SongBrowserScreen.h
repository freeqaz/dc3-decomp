// SongBrowserScreen: the natively drawn song browser, as one object.
// (docs/native/reference/SONG_BROWSER_UI.md has the layout and the pictures.)
//
// It is a nativeui screen fed by a songbrowser catalog. It knows nothing of
// the game: the host gives it input as nativeui events and a clock, a text
// measurer and album art through the seams below, and replays the display
// list it builds.
//
//     SongBrowserScreen browser(catalog, delegate, environment);   // or ..., &saved)
//     each frame:   for (event) browser.HandleEvent(event);
//                   nativeui::Replay(browser.BuildFrame(), renderer);
//     on leaving:   saved = browser.SaveState();
//
// What is on it, top to bottom, in a 1280x720 logical view drawn over the
// game's moving background (nothing here fills the screen opaquely):
//   top bar     SONGS, the sort tabs (title, artist, difficulty, source,
//               decade), the search field
//   left        Recently Played, a shelf of album-art cards, newest first
//               (only with a play history, never during a search, and folded
//               away while the list is being browsed); under it the list,
//               sectioned by the sort, or the ranked results of a search
//   right       the detail pane for the selected song
//   bottom      hints for what the controls do right now
//
// Input (every action is a nativeui::Action; the host maps buttons and keys):
//   Up / Down       move in the list; at the top of the list go up to the
//                   shelf (or the tabs with no history, or the search field
//                   during a search); from the shelf, Up to the tabs and Down
//                   to the list; from the tabs or the field, Down to the shelf
//                   or the list
//   Left / Right    in the list: jump to the previous / next section
//                   on the shelf: previous / next card
//                   on the tabs: previous / next sort; Right from the last
//                   tab goes to the search field, Left from the start of the
//                   field goes back to the tabs
//                   in the field: move the cursor
//   NextTab/PrevTab change the sort from anywhere, wrapping
//   PageUp/PageDown page the list (list focused)
//   Home / End      first / last row or card (cursor, in the field)
//   Accept          list or shelf: pick the song (onPick)
//                   search field: go down into the results
//   Back            a query is typed: clear it, and return to the song that
//                   was selected before the search; otherwise leave (onBack)
//   typed text      goes to the search field from anywhere and moves focus
//                   there; Backspace and Delete edit it from anywhere while a
//                   query is typed
//
// Focus on arrival: the saved State if one is given; else the first Recently
// Played card when there is history; else the first row of the list.
//
// Rules:
//  - The catalog is borrowed and must outlive the screen. After ANY change to
//    it (a song played, a new best score, a pack installed) call SetCatalog
//    before the next event or frame; the same object may be passed again.
//  - Callbacks run inside HandleEvent. None of them may destroy the screen.
//  - Everything that moves is driven by the clock, never by a frame count.
#pragma once

#include "nativeui/Clock.h"
#include "nativeui/DisplayList.h"
#include "nativeui/Input.h"
#include "nativeui/Text.h"
#include "nativeui/Theme.h"
#include "songbrowser/SongCatalog.h"
#include "songbrowser/SongSections.h"

#include <functional>
#include <memory>
#include <string>

namespace nativeui {
    class ListView;
    class Shelf;
}

namespace songbrowser_ui {

    // A moment the game's menu sounds belong to.
    enum class Cue {
        Move, // the selection or the focused region changed
        Select, // a song was picked
        Tab, // the sort changed
        Back, // a query was cleared, or the screen is being left
        Error // Accept with nothing to pick
    };

    // The entries of the classic list that are not songs.
    enum class Command {
        RandomSong, // let the game pick a song
        Playlists // go to the playlists
    };

    // The parts of the screen focus can be on.
    enum class Region {
        Search,
        Tabs,
        Recents,
        List
    };

    struct Delegate {
        // A song was picked: its short name ("ymca").
        std::function<void(const std::string &shortName)> onPick;
        // Back with no query typed: the player wants to leave.
        std::function<void()> onBack;
        // A command was chosen (Accept on it, or its own button).
        std::function<void(Command)> onCommand;
        std::function<void(Cue)> onCue;
        // The album art of a song, or the null handle while it is not loaded.
        // Asked every frame for every song whose art is on screen (a few
        // dozen calls), so it must be cheap; being asked is the host's cue to
        // start loading. Art that arrives later fades in.
        std::function<nativeui::ImageHandle(const songbrowser::SongEntry &)> artFor;
    };

    // What goes in the hint bar's pills. The host sets these to match the
    // device in use ("A" / "ENTER", "B" / "ESC", ...).
    struct HintGlyphs {
        std::string accept = "A";
        std::string back = "B";
        std::string sort = "LB RB";
        std::string section = "< >";
        std::string type = "ABC";
    };

    // The seams and settings a screen is built with. The measurer and the
    // clock are borrowed and must outlive the screen.
    struct Environment {
        const nativeui::TextMeasurer &text;
        const nativeui::Clock &clock;
        nativeui::Size viewport { 1280.0f, 720.0f };
        nativeui::Theme theme = nativeui::Dc3Theme();
        HintGlyphs glyphs;
        int maxRecents = 24; // cards on the Recently Played shelf
    };

    // Where the player was. Save it on the way out and hand it back to return
    // to the same place; `arrangement` alone is worth keeping between runs.
    struct State {
        songbrowser::Arrangement arrangement = songbrowser::kByTitle;
        Region focus = Region::List;
        std::string selectedSong; // short name of the list's selected song
        std::string recentSong; // short name of the shelf's selected card
        std::string query; // what is typed in the search field
        float scroll = 0.0f; // the list's scroll position
        // Where clearing the query returns to: the song and the scroll position.
        std::string songBeforeSearch;
        float scrollBeforeSearch = 0.0f;
    };

    class SongBrowserScreen {
    public:
        // `restore` null: start by the focus-on-arrival rule above.
        SongBrowserScreen(
            const songbrowser::SongCatalog &catalog,
            Delegate delegate,
            const Environment &environment,
            const State *restore = nullptr
        );
        ~SongBrowserScreen();
        SongBrowserScreen(const SongBrowserScreen &) = delete;
        SongBrowserScreen &operator=(const SongBrowserScreen &) = delete;

        // True if the event did something.
        bool HandleEvent(const nativeui::Event &event);
        // Advances to the clock's time, lays out and paints. The list is valid
        // until the next call.
        const nativeui::DisplayList &BuildFrame();

        // The catalog changed (or is another one). Keeps the sort, the query
        // and, where they still exist, the selected songs.
        void SetCatalog(const songbrowser::SongCatalog &catalog);

        State SaveState() const;
        // Songs and regions that no longer exist fall back by the arrival rule.
        void RestoreState(const State &state);

        // The device in use changed (pad to keyboard or back): what the hint
        // pills say from the next frame on.
        void SetHintGlyphs(const HintGlyphs &glyphs);

        // Whether the host should capture the keyboard for text. Always true:
        // typing anywhere on this screen searches.
        bool WantsTextInput() const { return true; }
        // True when no animation is running: the next frame would equal the
        // last unless an event arrives, the catalog changes, or art that was
        // asked for turns up (build a frame when a texture finishes loading).
        bool IsSettled() const;

        // For tests and tools.
        const nativeui::ListView &List() const;
        const nativeui::Shelf &Recents() const;
        // Short name of the song the detail pane shows; empty for none.
        const std::string &DetailSong() const;
        // The song the detail pane is fading away from; empty when no fade runs.
        const std::string &DetailFadingSong() const;
        // Whether the Recently Played shelf is (or is becoming) visible.
        bool RecentsShown() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

}
