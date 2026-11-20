// Song browser data - the browsable arrangements.  See SongSections.h.

#include "SongSections.h"

#include "SongText.h"

#include <algorithm>
#include <climits>
#include <cstdio>

namespace songbrowser {

namespace {

const char *const kArrangementNames[kNumArrangements] = { "title",  "artist", "difficulty",
                                                          "source", "decade", "bpm" };

// The game's English names for its tiers (locale song_tier_0..6), used when
// the catalog was not given localized ones.
const char *const kTierNames[] = { "Warmup", "Simple",   "Moderate",    "Tough",
                                   "Legit",  "Hardcore", "Off The Hook" };

const int kLast = INT_MAX; // the group of songs without the datum, at the end

struct Item {
    int song;
    int group;         // what section the row is in; sections go in this order
    double number;     // then by this (rank, year, BPM; 0 when not used)
    std::string first; // then by this key (the artist's in kByArtist, else the title's)
    std::string second; // then the other one
    std::string shortName; // and this makes the order total
    int letter;        // SortCharRank of `first`'s string
};

bool ItemLess(const Item &a, const Item &b) {
    if (a.group != b.group) return a.group < b.group;
    if (a.number != b.number) return a.number < b.number;
    int byFirst = CompareSortKeys(a.first, b.first);
    if (byFirst != 0) return byFirst < 0;
    int bySecond = CompareSortKeys(a.second, b.second);
    if (bySecond != 0) return bySecond < 0;
    return a.shortName < b.shortName;
}

Item MakeItem(const SongEntry &entry, int song, Arrangement arrangement) {
    Item item;
    item.song = song;
    item.group = 0;
    item.number = 0;
    item.shortName = entry.shortName;
    std::string titleKey = SortKey(entry.title, false);
    std::string artistKey = SortKey(entry.artist, true);
    if (arrangement == kByArtist) {
        item.first = artistKey;
        item.second = titleKey;
        item.letter = SortCharRank(entry.artist, true);
    } else {
        item.first = titleKey;
        item.second = artistKey;
        item.letter = SortCharRank(entry.title, false);
    }
    switch (arrangement) {
    case kByTitle:
    case kByArtist:
        // The sort character first, then the key: ArtistCmp's item order.
        item.group = item.letter;
        break;
    case kByDifficulty:
        // DifficultyCmp: tiers in order with "no tier" last; inside a tier
        // by rank with unranked last, then by title.
        item.group = entry.difficultyTier < 0 ? kLast : entry.difficultyTier;
        item.number = entry.rank == 0 ? 1e30 : entry.rank;
        break;
    case kBySource:
        // LocationCmp: ham1, ham2, ham3, everything else; then by title.
        item.group = entry.source;
        break;
    case kByDecade:
        item.group = entry.year > 0 ? entry.year / 10 * 10 : kLast;
        item.number = entry.year;
        break;
    case kByBpm:
        item.group = entry.bpm > 0 ? entry.bpm / 10 * 10 : kLast;
        item.number = entry.bpm;
        break;
    default:
        break;
    }
    return item;
}

void NameSection(const SongCatalog &catalog, Arrangement arrangement, int group, Section &section) {
    char token[32];
    char title[48];
    switch (arrangement) {
    case kByDifficulty:
        if (group == kLast) {
            section.token = "no_part"; // the game's token for a song without a tier
            section.title = catalog.Label(section.token, "Unrated");
            return;
        }
        snprintf(token, sizeof(token), "song_tier_%d", group);
        if (group >= 0 && group < (int)(sizeof(kTierNames) / sizeof(kTierNames[0]))) {
            snprintf(title, sizeof(title), "%s", kTierNames[group]);
        } else {
            snprintf(title, sizeof(title), "Tier %d", group + 1);
        }
        break;
    case kBySource: {
        static const char *const kSourceNames[] = { "Dance Central", "Dance Central 2",
                                                    "Dance Central 3", "DLC" };
        int source = group < 0 || group > 3 ? 3 : group;
        snprintf(token, sizeof(token), "%s", SourceToken((SongSource)source));
        snprintf(title, sizeof(title), "%s", kSourceNames[source]);
        break;
    }
    case kByDecade:
        if (group == kLast) {
            section.token = "decade_unknown";
            section.title = catalog.Label(section.token, "Unknown year");
            return;
        }
        snprintf(token, sizeof(token), "decade_%d", group);
        snprintf(title, sizeof(title), "%ds", group);
        break;
    case kByBpm:
        if (group == kLast) {
            section.token = "bpm_unknown";
            section.title = catalog.Label(section.token, "Unknown BPM");
            return;
        }
        snprintf(token, sizeof(token), "bpm_%d", group);
        snprintf(title, sizeof(title), "%d-%d BPM", group, group + 9);
        break;
    default:
        token[0] = title[0] = 0;
        break;
    }
    section.token = token;
    section.title = catalog.Label(section.token, title);
}

} // namespace

const char *ArrangementName(Arrangement arrangement) {
    if (arrangement < 0 || arrangement >= kNumArrangements) return "";
    return kArrangementNames[arrangement];
}

bool ArrangementFromName(const std::string &name, Arrangement &arrangement) {
    for (int i = 0; i < kNumArrangements; i++) {
        if (name == kArrangementNames[i]) {
            arrangement = (Arrangement)i;
            return true;
        }
    }
    return false;
}

SongList Arrange(
    const SongCatalog &catalog, Arrangement arrangement, const ArrangeOptions &options
) {
    SongList list;
    list.arrangement = arrangement;
    list.mRowOfSong.assign(catalog.Size(), -1);

    if (options.recentRows > 0) {
        std::vector<int> recents = catalog.Recents(options.recentRows);
        if (!recents.empty()) {
            Section section;
            section.token = "recently_played";
            section.title = catalog.Label(section.token, "Recently Played");
            section.firstRow = 0;
            section.count = (int)recents.size();
            section.recents = true;
            list.sections.push_back(section);
            list.rows = recents;
            list.rowLetter.assign(recents.size(), -1);
        }
    }

    std::vector<Item> items;
    for (int i = 0; i < catalog.Size(); i++) {
        items.push_back(MakeItem(catalog.At(i), i, arrangement));
    }
    std::sort(items.begin(), items.end(), ItemLess);

    int base = (int)list.rows.size();
    int count = (int)items.size();
    for (int i = 0; i < count; i++) {
        list.mRowOfSong[items[i].song] = base + i;
        list.rows.push_back(items[i].song);
        list.rowLetter.push_back(items[i].letter);
    }

    bool byLetter = arrangement == kByTitle || arrangement == kByArtist;
    int minSongs = byLetter && options.minSectionSongs > 1 ? options.minSectionSongs : 1;
    // Sections are whole groups, joined with the next until there are
    // minSongs of them; a short tail joins the section before it.  This is
    // SongSortByArtist::BuildTree's loop.
    std::vector<std::pair<int, int> > ranges;
    int begin = 0;
    while (begin < count) {
        int end = begin;
        while (end < count && end - begin < minSongs) {
            int group = items[end].group;
            while (end < count && items[end].group == group) {
                end++;
            }
        }
        if (end - begin < minSongs && !ranges.empty()) {
            ranges.back().second = end;
        } else {
            ranges.push_back(std::make_pair(begin, end));
        }
        begin = end;
    }
    for (size_t i = 0; i < ranges.size(); i++) {
        Section section;
        section.firstRow = base + ranges[i].first;
        section.count = ranges[i].second - ranges[i].first;
        int firstGroup = items[ranges[i].first].group;
        int lastGroup = items[ranges[i].second - 1].group;
        if (byLetter) {
            // "M" for one sort character, "A - C" for several.
            section.token = SortCharLabel(firstGroup);
            if (lastGroup != firstGroup) {
                section.token += " - ";
                section.token += SortCharLabel(lastGroup);
            }
            section.title = catalog.Label(section.token, section.token);
        } else {
            NameSection(catalog, arrangement, firstGroup, section);
        }
        list.sections.push_back(section);
    }
    return list;
}

int SongList::RowForSong(int catalogIndex) const {
    if (catalogIndex < 0 || catalogIndex >= (int)mRowOfSong.size()) return -1;
    return mRowOfSong[catalogIndex];
}

int SongList::RowForSong(const SongCatalog &catalog, const std::string &shortName) const {
    return RowForSong(catalog.IndexOf(shortName));
}

int SongList::SectionForRow(int row) const {
    for (size_t i = 0; i < sections.size(); i++) {
        if (row >= sections[i].firstRow && row < sections[i].firstRow + sections[i].count) {
            return (int)i;
        }
    }
    return -1;
}

int SongList::RowForLetter(char letter) const {
    int rank = 0;
    if (letter >= 'a' && letter <= 'z') rank = letter - 'a' + 1;
    if (letter >= 'A' && letter <= 'Z') rank = letter - 'A' + 1;
    for (size_t row = 0; row < rowLetter.size(); row++) {
        if (rowLetter[row] == rank) return (int)row;
    }
    return -1;
}

} // namespace songbrowser
