#include "nativeui/SampleBrowser.h"

#include "nativeui/HintBar.h"
#include "nativeui/Layout.h"
#include "nativeui/Panel.h"
#include "nativeui/Utf8.h"

#include <algorithm>
#include <optional>

namespace nativeui {

    namespace {

        // This screen's own layout numbers (logical units). Everything else comes
        // from the theme.
        constexpr float kSearchWidth = 320.0f;
        constexpr float kDetailWidth = 340.0f;
        constexpr float kDetailArtHeight = 150.0f;
        constexpr int kSongCount = 120;
        constexpr int kRecentCount = 10;

        const char *const kSkillNames[] = { "Warmup", "Simple",   "Moderate",    "Tough",
                                            "Legit",  "Hardcore", "Off the hook" };
        constexpr int kSkillCount =
            static_cast<int>(sizeof(kSkillNames) / sizeof(kSkillNames[0]));

        // The heading a song files under for each sort.
        std::string SectionOf(const SampleSong &song, int sort) {
            switch (sort) {
            case SampleBrowser::kByArtist:
                return song.artist;
            case SampleBrowser::kBySkill:
                return SampleSkillName(song.skill);
            default: {
                const utf8::Decoded first = utf8::Decode(song.title, 0);
                std::string letter;
                utf8::Append(letter, utf8::ToUpper(first.codePoint));
                return letter;
            }
            }
        }

    }

    const char *SampleSkillName(int skill) {
        return kSkillNames[std::min(kSkillCount - 1, std::max(0, skill))];
    }

    std::vector<SampleSong> MakeSampleSongs(int count) {
        static const char *const kFirstWords[] = { "Midnight", "Electric",  "Golden",
                                                   "Neon",     "Crystal",   "Velvet",
                                                   "Stereo",   "Starlight", "Summer",
                                                   "Wild" };
        static const char *const kSecondWords[] = { "Heart",  "Dance",  "Fever",
                                                    "Lights", "Rhythm", "Dreams",
                                                    "Street", "Storm",  "Motion",
                                                    "Echo",   "Parade", "Signal" };
        static const char *const kArtists[] = {
            "The Night Owls",  "DJ Volta",     "C\xC3\xA9line Noir", "Los Ni\xC3\xB1os",
            "Stella Storm",    "Kid Cosmos",   "Aurora Vale",        "Mot\xC3\xB6rfunk",
            "Zed & The Zeros", "Pixel Parade", "Bj\xC3\xB6rk Lane",  "The Strangers"
        };
        // A few real-world shapes: accents, a long title that must be cut.
        static const char *const kSpecialTitles[] = {
            "D\xC3\xA9j\xC3\xA0 Vu",
            "Se\xC3\xB1orita",
            "\xC3\x87\x61 Plane Pour Moi",
            "Macarena (Bayside Boys Extended Club Remix Version)"
        };
        const int firstCount = 10;
        const int secondCount = 12;
        const int artistCount = 12;
        const int specialCount = 4;

        std::vector<SampleSong> songs;
        songs.reserve(static_cast<std::size_t>(std::max(0, count)));
        for (int i = 0; i < count; ++i) {
            SampleSong song;
            if (i < specialCount) {
                song.title = kSpecialTitles[i];
            } else {
                song.title = std::string(kFirstWords[i % firstCount]) + " "
                    + kSecondWords[(i / firstCount) % secondCount];
            }
            song.artist = kArtists[(i * 7) % artistCount];
            song.skill = (i * 5) % kSkillCount;
            song.art =
                ImageHandle { i % 3 == 0 ? 0u : 100u + static_cast<std::uint32_t>(i) };
            songs.push_back(std::move(song));
        }
        return songs;
    }

    SampleBrowser::SampleBrowser(
        Size viewport, const TextMeasurer &text, const Clock &clock
    )
        : screen_(viewport, Dc3Theme(), text, clock),
          songs_(MakeSampleSongs(kSongCount)) {
        for (int i = 0; i < kRecentCount; ++i) {
            recentSongs_.push_back((i * 13 + 5) % kSongCount);
        }

        screen_.SetRoot(BuildTree());
        recent_->SetCardCount(static_cast<int>(recentSongs_.size()));
        RebuildRows();
        screen_.SetFocus(songList_);

        search_->onChanged = [this](const std::string &) { RebuildRows(); };
        tabs_->onChanged = [this](int) { RebuildRows(); };
        songList_->onSelectionChanged = [this](int row) {
            if (row >= 0 && row < static_cast<int>(entries_.size())) {
                ShowDetail(entries_[row].song);
            }
        };
        recent_->onSelectionChanged = [this](int card) {
            ShowDetail(recentSongs_[card]);
        };

        // Events no widget wanted: typing searches, the shoulder buttons sort.
        screen_.onUnhandledEvent = [this](const Event &event) {
            if (std::holds_alternative<TextEvent>(event)
                || std::holds_alternative<EditEvent>(event)) {
                return search_->HandleEvent(event);
            }
            if (const ActionEvent *action = std::get_if<ActionEvent>(&event)) {
                if (action->action == Action::NextTab
                    || action->action == Action::PrevTab) {
                    return tabs_->HandleEvent(event);
                }
            }
            return false;
        };
    }

    const SampleSong *SampleBrowser::DetailSong() const {
        return detailSong_ >= 0 ? &songs_[detailSong_] : nullptr;
    }

    std::unique_ptr<Widget> SampleBrowser::BuildTree() {
        const Theme &theme = screen_.GetTheme();
        const Spacing &space = theme.space;

        // Top bar: title, sort tabs, search field.
        auto topBar = std::make_unique<Row>(space.lg, CrossAlign::Center);
        topBar->Add(std::make_unique<Label>("Songs", &Typography::display));
        tabs_ = &topBar->Add(
            std::make_unique<TabStrip>(std::vector<std::string> {
                "Title", "Artist", "Skill level" }),
            1.0f
        );
        auto search = std::make_unique<TextField>("Search");
        search_ = search.get();
        topBar->Add(
            std::make_unique<SizedBox>(kSearchWidth, std::nullopt, std::move(search))
        );

        // Detail card.
        auto detail = std::make_unique<Column>(space.sm, CrossAlign::Stretch);
        auto art = std::make_unique<Image>(
            ImageHandle {}, &Palette::imagePlaceholder, &Radii::medium
        );
        detailArt_ = art.get();
        detail->Add(
            std::make_unique<SizedBox>(std::nullopt, kDetailArtHeight, std::move(art))
        );
        detailTitle_ = &detail->Add(std::make_unique<Label>("", &Typography::title));
        detailArtist_ = &detail->Add(
            std::make_unique<Label>("", &Typography::body, &Palette::textSecondary)
        );
        detailSkill_ = &detail->Add(
            std::make_unique<Label>("", &Typography::caption, &Palette::accent)
        );
        PanelStyle cardStyle;
        cardStyle.border = &Palette::panelBorder;
        cardStyle.halo = PanelStyle::Halo::Shadow;
        auto detailCard = std::make_unique<Panel>(
            cardStyle, std::make_unique<Padding>(Insets::All(space.md), std::move(detail))
        );

        // Song list beside the detail card.
        auto songList = std::make_unique<ListView>(
            [this](int index) {
                const Metrics &metric = screen_.GetTheme().metric;
                const bool header = entries_[index].isHeader;
                return ListRowInfo { header ? metric.listHeaderHeight
                                            : metric.listRowHeight,
                                     !header };
            },
            [this](int index) { return BuildRow(index); }
        );
        songList_ = songList.get();
        auto browse = std::make_unique<Row>(space.lg, CrossAlign::Stretch);
        browse->Add(std::move(songList), 1.0f);
        browse->Add(
            std::make_unique<SizedBox>(kDetailWidth, std::nullopt, std::move(detailCard))
        );

        auto recent =
            std::make_unique<Shelf>([this](int index) { return BuildRecentCard(index); });
        recent_ = recent.get();

        auto page = std::make_unique<Column>(space.md, CrossAlign::Stretch);
        page->Add(std::move(topBar));
        page->Add(
            std::make_unique<Label>(
                "Recently played", &Typography::caption, &Palette::textSecondary
            )
        );
        page->Add(std::move(recent));
        page->Add(std::move(browse), 1.0f);
        page->Add(
            std::make_unique<HintBar>(std::vector<Hint> {
                { "A", "Select" }, { "B", "Back" }, { "LB", "Sort" }, { "RB", "Sort" } })
        );

        PanelStyle backdrop;
        backdrop.fillTop = &Palette::background;
        backdrop.fillBottom = &Palette::background;
        backdrop.radius = &Radii::none;

        auto root = std::make_unique<Stack>();
        root->Add(std::make_unique<Panel>(backdrop));
        root->Add(
            std::make_unique<Padding>(
                Insets::Symmetric(space.xl, space.lg), std::move(page)
            )
        );
        return root;
    }

    std::unique_ptr<Widget> SampleBrowser::BuildRow(int index) const {
        const Spacing &space = screen_.GetTheme().space;
        const Entry &row = entries_[index];
        if (row.isHeader) {
            return std::make_unique<Padding>(
                Insets::Symmetric(space.md, 0.0f),
                std::make_unique<Label>(
                    row.heading, &Typography::caption, &Palette::textSecondary
                )
            );
        }
        const SampleSong &song = songs_[row.song];
        const float thumbnail = screen_.GetTheme().metric.listRowHeight - 2.0f * space.xs;
        auto line = std::make_unique<Row>(space.md, CrossAlign::Center);
        line->Add(
            std::make_unique<SizedBox>(
                thumbnail, thumbnail, std::make_unique<Image>(song.art)
            )
        );
        line->Add(std::make_unique<Label>(song.title), 1.0f);
        line->Add(
            std::make_unique<Label>(
                song.artist, &Typography::caption, &Palette::textSecondary
            )
        );
        return std::make_unique<Padding>(
            Insets { space.sm, space.xs, space.md, space.xs }, std::move(line)
        );
    }

    std::unique_ptr<Widget> SampleBrowser::BuildRecentCard(int index) const {
        const Spacing &space = screen_.GetTheme().space;
        const SampleSong &song = songs_[recentSongs_[index]];
        auto card = std::make_unique<Stack>();
        card->Add(
            std::make_unique<Image>(song.art, &Palette::imagePlaceholder, &Radii::medium)
        );
        card->Add(
            std::make_unique<Align>(
                Alignment { 0.0f, 1.0f },
                std::make_unique<Padding>(
                    Insets::All(space.sm),
                    std::make_unique<Label>(song.title, &Typography::caption)
                )
            )
        );
        return card;
    }

    void SampleBrowser::RebuildRows() {
        const int sort = tabs_->SelectedIndex();
        const std::string query = utf8::ToUpper(search_->Text());

        std::vector<int> matches;
        for (int i = 0; i < static_cast<int>(songs_.size()); ++i) {
            const SampleSong &song = songs_[i];
            if (query.empty()
                || utf8::ToUpper(song.title).find(query) != std::string::npos
                || utf8::ToUpper(song.artist).find(query) != std::string::npos) {
                matches.push_back(i);
            }
        }
        std::stable_sort(matches.begin(), matches.end(), [&](int a, int b) {
            const SampleSong &x = songs_[a];
            const SampleSong &y = songs_[b];
            if (sort == kBySkill && x.skill != y.skill) {
                return x.skill < y.skill;
            }
            if (sort == kByArtist && x.artist != y.artist) {
                return utf8::ToUpper(x.artist) < utf8::ToUpper(y.artist);
            }
            return utf8::ToUpper(x.title) < utf8::ToUpper(y.title);
        });

        entries_.clear();
        std::string section;
        for (const int song : matches) {
            const std::string heading = SectionOf(songs_[song], sort);
            if (entries_.empty() || heading != section) {
                section = heading;
                Entry header;
                header.isHeader = true;
                header.heading = heading;
                entries_.push_back(std::move(header));
            }
            Entry row;
            row.song = song;
            entries_.push_back(std::move(row));
        }

        songList_->SetRowCount(static_cast<int>(entries_.size()));
        songList_->SelectFirst();
        const int selected = songList_->SelectedIndex();
        ShowDetail(selected >= 0 ? entries_[selected].song : -1);
    }

    void SampleBrowser::ShowDetail(int song) {
        detailSong_ = song;
        if (song < 0) {
            detailArt_->SetImage(ImageHandle {});
            detailTitle_->SetText("No songs found");
            detailArtist_->SetText("");
            detailSkill_->SetText("");
            return;
        }
        const SampleSong &shown = songs_[song];
        detailArt_->SetImage(shown.art);
        detailTitle_->SetText(shown.title);
        detailArtist_->SetText(shown.artist);
        detailSkill_->SetText(SampleSkillName(shown.skill));
    }

}
