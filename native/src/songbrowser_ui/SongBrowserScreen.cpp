#include "songbrowser_ui/SongBrowserScreen.h"

#include "nativeui/Backdrop.h"
#include "nativeui/HintBar.h"
#include "nativeui/Icon.h"
#include "nativeui/Label.h"
#include "nativeui/Layout.h"
#include "nativeui/ListView.h"
#include "nativeui/Panel.h"
#include "nativeui/Screen.h"
#include "nativeui/Shelf.h"
#include "nativeui/TabStrip.h"
#include "nativeui/TextField.h"
#include "songbrowser_ui/BrowserModel.h"
#include "songbrowser_ui/Containers.h"
#include "songbrowser_ui/DetailPane.h"
#include "songbrowser_ui/SongWidgets.h"

#include <algorithm>
#include <optional>

namespace songbrowser_ui {

    using namespace nativeui;
    using namespace songbrowser;

    namespace {

        // The sorts offered, in tab order. Decade earns a tab: a dance catalog
        // runs from the 1970s to now and "something from the 80s" is how people
        // ask for a song. BPM does not: ten-BPM bands say little about how a
        // song feels, difficulty already orders by effort, the BPM is on the
        // detail pane, and a sixth tab would crowd the search field at 1280.
        struct Tab {
            Arrangement arrangement;
            const char *label;
        };
        constexpr Tab kTabs[] = { { kByTitle, "Title" },
                                  { kByArtist, "Artist" },
                                  { kByDifficulty, "Difficulty" },
                                  { kBySource, "Source" },
                                  { kByDecade, "Decade" } };
        constexpr int kTabCount = static_cast<int>(sizeof(kTabs) / sizeof(kTabs[0]));

        // The tab showing `arrangement`; the first tab for one that has no tab.
        int TabOf(Arrangement arrangement) {
            for (int i = 0; i < kTabCount; ++i) {
                if (kTabs[i].arrangement == arrangement) {
                    return i;
                }
            }
            return 0;
        }

        // "3:42"; empty for an unknown length.
        std::string FormatLength(int milliseconds) {
            if (milliseconds <= 0) {
                return {};
            }
            const int seconds = (milliseconds + 500) / 1000;
            const std::string rest = std::to_string(seconds % 60);
            return std::to_string(seconds / 60) + ":" + (rest.size() < 2 ? "0" : "") + rest;
        }

        // "1,234,567".
        std::string FormatScore(int score) {
            std::string digits = std::to_string(std::max(0, score));
            for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) {
                digits.insert(static_cast<std::size_t>(at), ",");
            }
            return digits;
        }

        std::vector<ByteSpan> SpansOf(const SearchRow &row, MatchField field) {
            std::vector<ByteSpan> spans;
            for (const MatchRange &range : row.ranges) {
                if (range.field == field) {
                    spans.push_back({ range.begin, range.length });
                }
            }
            return spans;
        }

        // The rows that open the list and are not songs, in the order shown.
        struct CommandInfo {
            Command command;
            Icon icon;
            const char *key; // what the detail pane knows it by; never a song's name
            const char *title;
            const char *tagline; // on the row
            const char *description; // on the detail pane
        };
        // A song's short name is a word; a key that starts with '#' cannot be one.
        constexpr char kCommandKeyMark = '#';
        constexpr CommandInfo kCommands[] = {
            { Command::RandomSong,
              Icon::Shuffle,
              "#random",
              "Random song",
              "Let the game choose",
              "Not sure what to dance? Let the game pick a song from your whole library." },
            { Command::Playlists,
              Icon::Playlist,
              "#playlists",
              "Playlists",
              "Songs back to back",
              "Dance several songs in a row: pick one of the game's playlists or one of your own." }
        };
        constexpr int kCommandCount =
            static_cast<int>(sizeof(kCommands) / sizeof(kCommands[0]));

        // The index of `command` in kCommands; -1 for none.
        int IndexOfCommand(const std::optional<Command> &command) {
            for (int i = 0; command && i < kCommandCount; ++i) {
                if (kCommands[i].command == *command) {
                    return i;
                }
            }
            return -1;
        }

        const std::string kNoSong;

        // The dark glass the list and the shelf sit on: it takes the backdrop's
        // light down so that row text reads the same wherever a band passes.
        PanelStyle WellStyle() {
            PanelStyle style;
            style.fillTop = &Palette::well;
            style.fillBottom = &Palette::well;
            style.radius = &Radii::large;
            return style;
        }

    }

    class SongBrowserScreen::Impl {
    public:
        Impl(
            const SongCatalog &catalog,
            Delegate delegate,
            const Environment &environment,
            const State *restore
        )
            : delegate_(std::move(delegate)), glyphs_(environment.glyphs),
              maxRecents_(environment.maxRecents),
              screen_(
                  environment.viewport, environment.theme, environment.text, environment.clock
              ) {
            screen_.SetRoot(BuildTree());
            Wire();
            catalog_ = &catalog;
            model_.SetCommandCount(kCommandCount);
            model_.SetCatalog(catalog, maxRecents_);
            if (restore) {
                Restore(*restore);
            } else {
                ReloadShelf({});
                ReloadList(-1);
                Focus(ArrivalRegion(), true);
            }
            screen_.PlayEnter();
        }

        bool HandleEvent(const Event &event) { return screen_.HandleEvent(event); }
        const DisplayList &BuildFrame() { return screen_.BuildFrame(); }
        bool IsSettled() const { return screen_.AtRest(); }

        void SetCatalog(const SongCatalog &catalog) {
            // The names were noted when the selections were made: the old
            // catalog may already be gone, or changed under the same address.
            const std::string listSong = listSongName_;
            const std::string recentSong = recentSongName_;
            const int command = listCommand_;
            const Region region = FocusedRegion();
            catalog_ = &catalog;
            model_.SetCatalog(catalog, maxRecents_);
            ReloadShelf(recentSong);
            ReloadList(catalog.IndexOf(listSong), command);
            Focus(region == Region::Recents && !ShelfAvailable() ? Region::List : region, true);
        }

        State Save() const {
            State state;
            state.arrangement = model_.GetArrangement();
            state.focus = FocusedRegion();
            state.selectedSong = listSongName_;
            if (listCommand_ >= 0) {
                state.selectedCommand = kCommands[listCommand_].command;
            }
            state.recentSong = recentSongName_;
            state.query = search_->Text();
            state.songBeforeSearch = songBeforeSearch_;
            state.scrollBeforeSearch = scrollBeforeSearch_;
            state.scroll = list_->ScrollTarget();
            return state;
        }

        void Restore(const State &state) {
            ++quiet_;
            const int tab = TabOf(state.arrangement);
            tabs_->Select(tab);
            model_.SetArrangement(kTabs[tab].arrangement);
            search_->SetText(state.query);
            model_.SetQuery(state.query);
            --quiet_;
            songBeforeSearch_ = state.songBeforeSearch;
            scrollBeforeSearch_ = state.scrollBeforeSearch;
            regionBeforeSearch_ = Region::List;
            commandBeforeSearch_ = -1;
            ReloadShelf(state.recentSong);
            ReloadList(
                catalog_->IndexOf(state.selectedSong), IndexOfCommand(state.selectedCommand)
            );
            list_->SetScrollOffset(state.scroll);
            const bool usable = state.focus != Region::Recents || ShelfAvailable();
            Focus(usable ? state.focus : ArrivalRegion(), true);
        }

        void SetGlyphs(const HintGlyphs &glyphs) {
            glyphs_ = glyphs;
            UpdateHints();
        }

        std::optional<Command> SelectedCommand() const {
            return listCommand_ >= 0 ? std::optional<Command>(kCommands[listCommand_].command)
                                     : std::nullopt;
        }
        std::optional<Command> DetailCommand() const {
            for (const CommandInfo &info : kCommands) {
                if (detail_->ShownKey() == info.key) {
                    return info.command;
                }
            }
            return std::nullopt;
        }

        const ListView &List() const { return *list_; }
        const Shelf &Recents() const { return *shelf_; }
        const DetailPane &Detail() const { return *detail_; }
        bool RecentsShown() const { return reveal_->Shown(); }

    private:
        // -----------------------------------------------------------------------
        // The tree
        // -----------------------------------------------------------------------

        // The page is hung on the game's frame: the two hairlines its menus draw
        // across the screen (Metrics::frameTop / frameBottom). The title bar sits
        // above the upper one, the hints below the lower one, and the body
        // between them, clear of both by a padding.
        std::unique_ptr<Widget> BuildTree() {
            const Theme &theme = screen_.GetTheme();
            const Spacing &space = theme.space;
            const Metrics &metric = theme.metric;

            // Top bar: title, sort tabs, search field.
            auto topBar = std::make_unique<Row>(space.lg, CrossAlign::Center);
            topBar->Add(std::make_unique<Label>("Songs", &Typography::display));
            std::vector<std::string> labels;
            for (const Tab &tab : kTabs) {
                labels.push_back(tab.label);
            }
            tabs_ = &topBar->Add(std::make_unique<TabStrip>(std::move(labels)), 1.0f);
            auto search = std::make_unique<TextField>("Type to search");
            search->SetLeadingIcon(Icon::Search);
            search_ = search.get();
            topBar->Add(
                std::make_unique<SizedBox>(
                    metric.searchFieldWidth, std::nullopt, std::move(search)
                )
            );

            // Left column: Recently Played (folds away), then the list.
            auto recents = std::make_unique<Column>(space.sm, CrossAlign::Stretch);
            recents->Add(
                std::make_unique<Label>(
                    "Recently played", &Typography::label, &Palette::textSecondary
                )
            );
            shelf_ = &recents->Add(
                std::make_unique<Shelf>([this](int card) { return BuildCard(card); })
            );
            auto reveal = std::make_unique<Reveal>(
                std::make_unique<Padding>(
                    Insets { 0.0f, 0.0f, 0.0f, space.md }, std::move(recents)
                )
            );
            reveal_ = reveal.get();

            auto listArea = std::make_unique<Stack>();
            list_ = &listArea->Add(
                std::make_unique<ListView>(
                    [this](int row) { return DescribeRow(row); },
                    [this](int row) { return BuildRow(row); }
                )
            );
            notice_ = &listArea->Add(std::make_unique<Notice>());

            auto left = std::make_unique<Column>(0.0f, CrossAlign::Stretch);
            left->Add(std::move(reveal));
            left->Add(std::move(listArea), 1.0f);
            auto well = std::make_unique<Stack>();
            well->Add(std::make_unique<Panel>(WellStyle()));
            well->Add(std::make_unique<Padding>(Insets::All(space.md), std::move(left)));

            auto body = std::make_unique<Row>(space.lg, CrossAlign::Stretch);
            body->Add(std::move(well), 1.0f);
            detail_ = &body->Add(std::make_unique<DetailPane>());

            auto hints = std::make_unique<HintBar>(std::vector<Hint> {});
            hints_ = hints.get();

            auto page = std::make_unique<Column>(0.0f, CrossAlign::Stretch);
            page->Add(std::make_unique<SizedBox>(std::nullopt, metric.frameTop, std::move(topBar)));
            page->Add(
                std::make_unique<Padding>(Insets::Symmetric(0.0f, space.md), std::move(body)),
                1.0f
            );
            page->Add(
                std::make_unique<SizedBox>(
                    std::nullopt,
                    screen_.Viewport().height - metric.frameBottom,
                    std::make_unique<Align>(Alignment { 0.0f, 0.5f }, std::move(hints))
                )
            );

            // The screen's own backdrop goes over whatever the game draws behind
            // it and under everything else.
            auto root = std::make_unique<Stack>();
            root->Add(std::make_unique<Backdrop>());
            root->Add(
                std::make_unique<Padding>(Insets::Symmetric(space.xl, 0.0f), std::move(page))
            );
            auto scope = std::make_unique<EventScope>(std::move(root));
            scope->onEvent = [this](const Event &event) { return OnUnconsumed(event); };
            return scope;
        }

        void Wire() {
            search_->onChanged = [this](const std::string &text) { OnQueryEdited(text); };
            tabs_->onChanged = [this](int tab) {
                if (quiet_ == 0) {
                    OnSortChanged(tab);
                }
            };
            list_->onSelectionChanged = [this](int) { OnSelectionMoved(); };
            shelf_->onSelectionChanged = [this](int) { OnSelectionMoved(); };
            list_->onActivated = [this](int row) { Activate(row); };
            shelf_->onActivated = [this](int card) { Pick(RecentAt(card)); };
        }

        ListRowInfo DescribeRow(int row) const {
            const Metrics &metric = screen_.GetTheme().metric;
            const bool heading = model_.Rows()[static_cast<std::size_t>(row)].heading;
            return { heading ? metric.listHeaderHeight : metric.listRowHeight, !heading };
        }

        std::unique_ptr<Widget> BuildRow(int row) {
            if (model_.Rows()[static_cast<std::size_t>(row)].heading) {
                return std::make_unique<SectionHeading>(
                    model_.HeadingTitle(row), model_.HeadingCount(row)
                );
            }
            const auto lit = [this, row] {
                return list_->HasFocus() && list_->SelectedIndex() == row;
            };
            const int command = model_.CommandAt(row);
            if (command >= 0) {
                const CommandInfo &info = kCommands[command];
                return std::make_unique<CommandRow>(info.icon, info.title, info.tagline, lit);
            }
            const int song = model_.SongAt(row);
            const SongEntry *entry = EntryAt(song);
            if (!entry) {
                return nullptr;
            }
            SongRowModel model;
            model.title = entry->title;
            model.secondary = entry->artist;
            model.difficultyTier = entry->difficultyTier;
            model.stars = entry->bestStars;
            model.played = entry->played;
            if (const SearchRow *found = model_.ResultAt(row)) {
                model.titleMatch = SpansOf(*found, kFieldTitle);
                model.secondaryMatch = SpansOf(*found, kFieldArtist);
                // Matched on the album alone: show the album, so the row says why
                // it is here.
                const std::vector<ByteSpan> album = SpansOf(*found, kFieldAlbum);
                if (model.titleMatch.empty() && model.secondaryMatch.empty()
                    && !album.empty()) {
                    model.secondary = entry->album;
                    model.secondaryMatch = album;
                }
            }
            return std::make_unique<SongRow>(
                std::move(model), [this, song] { return ArtFor(song); }, lit
            );
        }

        std::unique_ptr<Widget> BuildCard(int card) {
            const int song = RecentAt(card);
            const SongEntry *entry = EntryAt(song);
            if (!entry) {
                return nullptr;
            }
            return std::make_unique<RecentCard>(entry->title, entry->artist, [this, song] {
                return ArtFor(song);
            });
        }

        // -----------------------------------------------------------------------
        // Songs
        // -----------------------------------------------------------------------

        const SongEntry *EntryAt(int song) const {
            return song >= 0 && song < catalog_->Size() ? &catalog_->At(song) : nullptr;
        }

        ImageHandle ArtFor(int song) const {
            const SongEntry *entry = EntryAt(song);
            return entry && delegate_.artFor ? delegate_.artFor(*entry) : ImageHandle {};
        }

        int RecentAt(int card) const {
            const std::vector<int> &recents = model_.Recents();
            return card >= 0 && card < static_cast<int>(recents.size())
                ? recents[static_cast<std::size_t>(card)]
                : -1;
        }

        std::string NameOf(int song) const {
            const SongEntry *entry = EntryAt(song);
            return entry ? entry->shortName : std::string();
        }

        // The song the detail pane should show: the focused region's selection.
        int CurrentSong() const {
            if (FocusedRegion() == Region::Recents) {
                return RecentAt(shelf_->SelectedIndex());
            }
            return model_.SongAt(list_->SelectedIndex());
        }

        DetailModel DetailFor(int song) const {
            const SongEntry &entry = *EntryAt(song);
            DetailModel model;
            model.key = entry.shortName;
            model.title = entry.title;
            model.artist = entry.artist;
            model.album = entry.album;
            if (entry.year > 0) {
                const std::string year = std::to_string(entry.year);
                model.album = entry.album.empty() ? year : entry.album + " (" + year + ")";
            }
            model.length = FormatLength(entry.lengthMs);
            model.bpm = entry.bpm > 0 ? std::to_string(entry.bpm) + " BPM" : std::string();
            model.difficulty = model_.TierName(entry.difficultyTier);
            model.difficultyTier = entry.difficultyTier;
            model.played = entry.played;
            model.stars = entry.bestStars;
            model.score = !entry.played
                ? std::string("Not played yet")
                : (entry.bestScore > 0 ? FormatScore(entry.bestScore) : std::string());
            model.source = model_.SourceName(entry.source);
            model.art = [this, song] { return ArtFor(song); };
            return model;
        }

        DetailModel DetailFor(const CommandInfo &info) const {
            DetailModel model;
            model.key = info.key;
            model.title = info.title;
            model.icon = info.icon;
            model.description = info.description;
            return model;
        }

        // Accept on a row of the list: a command is run, a song is picked.
        void Activate(int row) {
            const int command = model_.CommandAt(row);
            if (command < 0) {
                Pick(model_.SongAt(row));
                return;
            }
            Play(Cue::Select);
            if (delegate_.onCommand) {
                delegate_.onCommand(kCommands[command].command);
            }
        }

        void Pick(int song) {
            const SongEntry *entry = EntryAt(song);
            if (!entry) {
                Play(Cue::Error);
                return;
            }
            Play(Cue::Select);
            if (delegate_.onPick) {
                delegate_.onPick(entry->shortName);
            }
        }

        void Play(Cue cue) const {
            if (quiet_ == 0 && delegate_.onCue) {
                delegate_.onCue(cue);
            }
        }

        // -----------------------------------------------------------------------
        // Regions and focus
        // -----------------------------------------------------------------------

        Region FocusedRegion() const {
            const Widget *focused = screen_.Focused();
            if (focused == search_) {
                return Region::Search;
            }
            if (focused == tabs_) {
                return Region::Tabs;
            }
            return focused == shelf_ ? Region::Recents : Region::List;
        }

        Widget *WidgetOf(Region region) const {
            switch (region) {
            case Region::Search:
                return search_;
            case Region::Tabs:
                return tabs_;
            case Region::Recents:
                return shelf_;
            case Region::List:
                break;
            }
            return list_;
        }

        // The shelf can be shown: there is history, and no search is running.
        bool ShelfAvailable() const {
            return !model_.Recents().empty() && !model_.Searching();
        }

        Region ArrivalRegion() const {
            return ShelfAvailable() ? Region::Recents : Region::List;
        }

        // Moves focus; returns whether it moved. `refresh` updates what depends
        // on the focus even if it did not.
        bool Focus(Region region, bool refresh = false) {
            Widget *widget = WidgetOf(region);
            const bool moved = screen_.Focused() != widget;
            if (moved) {
                screen_.SetFocus(widget);
            }
            if (moved || refresh) {
                UpdateShelf();
                UpdateDetail();
                UpdateHints();
            }
            return moved;
        }

        void MoveTo(Region region) {
            if (Focus(region)) {
                Play(Cue::Move);
            }
        }

        void MoveVertically(int direction) {
            const Region below = ShelfAvailable() ? Region::Recents : Region::List;
            switch (FocusedRegion()) {
            case Region::List:
                if (direction < 0) {
                    MoveTo(
                        model_.Searching() ? Region::Search
                                           : (ShelfAvailable() ? Region::Recents : Region::Tabs)
                    );
                }
                break;
            case Region::Recents:
                MoveTo(direction < 0 ? Region::Tabs : Region::List);
                break;
            case Region::Tabs:
            case Region::Search:
                // Nothing to go down into: stay (an empty result list).
                if (direction > 0
                    && (below == Region::Recents || list_->SelectedIndex() >= 0)) {
                    MoveTo(below);
                }
                break;
            }
        }

        void MoveSideways(int direction) {
            switch (FocusedRegion()) {
            case Region::List: {
                const int row = model_.SectionJump(list_->SelectedIndex(), direction);
                if (row >= 0) {
                    list_->Select(row);
                }
                break;
            }
            case Region::Tabs:
                if (direction > 0) {
                    MoveTo(Region::Search);
                }
                break;
            case Region::Search:
                if (direction < 0) {
                    MoveTo(Region::Tabs);
                }
                break;
            case Region::Recents:
                break;
            }
        }

        // -----------------------------------------------------------------------
        // Events nothing focused consumed
        // -----------------------------------------------------------------------

        bool OnUnconsumed(const Event &event) {
            if (const TextEvent *typed = std::get_if<TextEvent>(&event)) {
                // Typing anywhere searches. A space cannot start a query.
                const bool leadingSpace = typed->codePoint == static_cast<char32_t>(' ') && search_->Text().empty();
                if (FocusedRegion() == Region::Search || leadingSpace
                    || !search_->HandleEvent(event)) {
                    return false;
                }
                Focus(Region::Search);
                return true;
            }
            if (std::holds_alternative<EditEvent>(event)) {
                if (FocusedRegion() == Region::Search || search_->Text().empty()) {
                    return false;
                }
                search_->HandleEvent(event);
                Focus(Region::Search);
                return true;
            }
            const ActionEvent *action = std::get_if<ActionEvent>(&event);
            if (!action) {
                return false;
            }
            switch (action->action) {
            case Action::Up:
                MoveVertically(-1);
                return true;
            case Action::Down:
                MoveVertically(1);
                return true;
            case Action::Left:
                MoveSideways(-1);
                return true;
            case Action::Right:
                MoveSideways(1);
                return true;
            case Action::NextTab:
            case Action::PrevTab:
                return tabs_->HandleEvent(event);
            case Action::Accept:
                if (FocusedRegion() == Region::List) {
                    Play(Cue::Error); // a list with nothing to pick
                } else {
                    MoveVertically(1);
                }
                return true;
            case Action::Back:
                Play(Cue::Back);
                if (!search_->Text().empty()) {
                    ClearSearch();
                } else if (delegate_.onBack) {
                    delegate_.onBack();
                }
                return true;
            default:
                return false;
            }
        }

        // -----------------------------------------------------------------------
        // Search and sort
        // -----------------------------------------------------------------------

        void OnQueryEdited(const std::string &text) {
            if (model_.Query().empty() && !text.empty()) {
                // A search is starting: remember where to come back to.
                songBeforeSearch_ = listSongName_;
                commandBeforeSearch_ = listCommand_;
                scrollBeforeSearch_ = list_->ScrollTarget();
                regionBeforeSearch_ = FocusedRegion();
            }
            const bool wasSearching = model_.Searching();
            model_.SetQuery(text);
            if (model_.Searching()) {
                ReloadList(-1); // the best match is selected
            } else if (wasSearching) {
                // The list comes back where it was, not merely with the song in view.
                ReloadList(catalog_->IndexOf(songBeforeSearch_), commandBeforeSearch_);
                list_->SetScrollOffset(scrollBeforeSearch_);
            }
            UpdateShelf();
            UpdateHints();
        }

        void ClearSearch() {
            search_->SetText({});
            OnQueryEdited({});
            const bool usable = regionBeforeSearch_ != Region::Recents || ShelfAvailable();
            Focus(usable ? regionBeforeSearch_ : Region::List, true);
        }

        void OnSortChanged(int tab) {
            const int song = model_.Searching() ? -1 : model_.SongAt(list_->SelectedIndex());
            const int command = listCommand_;
            model_.SetArrangement(kTabs[tab].arrangement);
            if (!model_.Searching()) {
                // The same song (or command) stays selected, and in view.
                ReloadList(song, command);
            }
            Play(Cue::Tab);
        }

        // -----------------------------------------------------------------------
        // Keeping the widgets in step with the model
        // -----------------------------------------------------------------------

        // Selects `selectCommand` if the list shows it, else `selectSong`, else
        // the first song: a reload never lands on a command nobody chose, so
        // arriving on the screen puts the selection on a song.
        void ReloadList(int selectSong, int selectCommand = -1) {
            ++quiet_;
            list_->SetRowCount(static_cast<int>(model_.Rows().size()));
            int row = model_.RowOfCommand(selectCommand);
            if (row < 0) {
                row = model_.RowOfSong(selectSong);
            }
            if (row < 0) {
                row = model_.FirstSongRow();
            }
            if (row >= 0) {
                list_->Select(row);
            } else {
                list_->SelectFirst();
            }
            --quiet_;
            if (model_.SongCount() > 0) {
                notice_->Hide();
            } else if (model_.Searching()) {
                notice_->Show("No songs match", "Nothing found for " + model_.Query());
            } else {
                notice_->Show("No songs", "No songs are installed");
            }
            UpdateDetail();
            UpdateHints();
        }

        // `selectName`: the card to select by short name; the first card otherwise.
        void ReloadShelf(const std::string &selectName) {
            const std::vector<int> &recents = model_.Recents();
            int card = 0;
            for (int i = 0; i < static_cast<int>(recents.size()); ++i) {
                if (NameOf(recents[static_cast<std::size_t>(i)]) == selectName) {
                    card = i;
                }
            }
            ++quiet_;
            shelf_->SetCardCount(static_cast<int>(recents.size()));
            shelf_->Select(card);
            --quiet_;
            UpdateShelf();
        }

        void OnSelectionMoved() {
            UpdateDetail();
            Play(Cue::Move);
        }

        void UpdateShelf() {
            reveal_->SetShown(ShelfAvailable() && FocusedRegion() != Region::List);
        }

        void UpdateDetail() {
            listSongName_ = NameOf(model_.SongAt(list_->SelectedIndex()));
            listCommand_ = model_.CommandAt(list_->SelectedIndex());
            recentSongName_ = NameOf(RecentAt(shelf_->SelectedIndex()));
            const int song = CurrentSong();
            if (EntryAt(song)) {
                detail_->Show(DetailFor(song));
            } else if (listCommand_ >= 0 && FocusedRegion() != Region::Recents) {
                detail_->Show(DetailFor(kCommands[listCommand_]));
            } else {
                detail_->Clear();
            }
        }

        void UpdateHints() {
            const Region region = FocusedRegion();
            const bool typed = !search_->Text().empty();
            // A pad's accept and back buttons have the colours the game gives them.
            const auto accept = [this](const char *text) {
                return Hint { glyphs_.accept, text, &Palette::buttonAccept };
            };
            const Hint back { glyphs_.back, typed ? "Clear search" : "Back", &Palette::buttonBack };
            std::vector<Hint> hints;
            switch (region) {
            case Region::List:
                if (list_->SelectedIndex() >= 0) {
                    hints.push_back(accept("Select"));
                }
                hints.push_back(back);
                if (!model_.Searching()) {
                    hints.push_back({ glyphs_.section, "Section" });
                    hints.push_back({ glyphs_.sort, "Sort" });
                    hints.push_back({ glyphs_.type, "Search" });
                }
                break;
            case Region::Recents:
                hints.push_back(accept("Select"));
                hints.push_back(back);
                hints.push_back({ glyphs_.sort, "Sort" });
                hints.push_back({ glyphs_.type, "Search" });
                break;
            case Region::Tabs:
                hints.push_back({ glyphs_.section, "Sort" });
                hints.push_back(back);
                hints.push_back({ glyphs_.type, "Search" });
                break;
            case Region::Search:
                if (typed) {
                    if (list_->SelectedIndex() >= 0) { // nothing to go to when nothing matched
                        hints.push_back(accept("Results"));
                    }
                } else {
                    hints.push_back({ glyphs_.type, "Type to search" });
                }
                hints.push_back(back);
                break;
            }
            hints_->SetHints(std::move(hints));
        }

        Delegate delegate_;
        HintGlyphs glyphs_;
        int maxRecents_;
        Screen screen_;
        const SongCatalog *catalog_ = nullptr;
        BrowserModel model_;
        int quiet_ = 0; // above zero while the screen itself moves selections: no cues

        // What is selected, by short name, noted whenever it changes. A list
        // whose selection is a command has no song name and a command index.
        std::string listSongName_;
        std::string recentSongName_;
        int listCommand_ = -1;
        // Where clearing the query returns to.
        std::string songBeforeSearch_;
        int commandBeforeSearch_ = -1;
        float scrollBeforeSearch_ = 0.0f;
        Region regionBeforeSearch_ = Region::List;

        // Owned by the tree in screen_.
        TextField *search_ = nullptr;
        TabStrip *tabs_ = nullptr;
        Shelf *shelf_ = nullptr;
        Reveal *reveal_ = nullptr;
        ListView *list_ = nullptr;
        Notice *notice_ = nullptr;
        DetailPane *detail_ = nullptr;
        HintBar *hints_ = nullptr;
    };

    SongBrowserScreen::SongBrowserScreen(
        const SongCatalog &catalog,
        Delegate delegate,
        const Environment &environment,
        const State *restore
    )
        : impl_(std::make_unique<Impl>(catalog, std::move(delegate), environment, restore)) {}

    SongBrowserScreen::~SongBrowserScreen() = default;

    bool SongBrowserScreen::HandleEvent(const Event &event) {
        return impl_->HandleEvent(event);
    }

    const DisplayList &SongBrowserScreen::BuildFrame() { return impl_->BuildFrame(); }

    void SongBrowserScreen::SetCatalog(const SongCatalog &catalog) {
        impl_->SetCatalog(catalog);
    }

    State SongBrowserScreen::SaveState() const { return impl_->Save(); }

    void SongBrowserScreen::RestoreState(const State &state) { impl_->Restore(state); }

    void SongBrowserScreen::SetHintGlyphs(const HintGlyphs &glyphs) {
        impl_->SetGlyphs(glyphs);
    }

    bool SongBrowserScreen::IsSettled() const { return impl_->IsSettled(); }

    const ListView &SongBrowserScreen::List() const { return impl_->List(); }

    const Shelf &SongBrowserScreen::Recents() const { return impl_->Recents(); }

    namespace {

        // The detail pane's key as a song's short name: none for a command.
        const std::string &SongOfKey(const std::string &key) {
            return !key.empty() && key.front() == kCommandKeyMark ? kNoSong : key;
        }

    }

    const std::string &SongBrowserScreen::DetailSong() const {
        return SongOfKey(impl_->Detail().ShownKey());
    }

    const std::string &SongBrowserScreen::DetailFadingSong() const {
        return SongOfKey(impl_->Detail().FadingKey());
    }

    std::optional<Command> SongBrowserScreen::SelectedCommand() const {
        return impl_->SelectedCommand();
    }

    std::optional<Command> SongBrowserScreen::DetailCommand() const {
        return impl_->DetailCommand();
    }

    bool SongBrowserScreen::RecentsShown() const { return impl_->RecentsShown(); }

}
