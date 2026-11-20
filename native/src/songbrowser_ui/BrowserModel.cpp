#include "songbrowser_ui/BrowserModel.h"

namespace songbrowser_ui {

    using namespace songbrowser;

    void BrowserModel::SetCatalog(const SongCatalog &catalog, int maxRecents) {
        catalog_ = &catalog;
        search_.Rebuild(catalog);
        recents_ = catalog.Recents(maxRecents);

        // The data layer names tiers and sources when it sections by them; read
        // the names off those sections rather than keep a second list here.
        tierNames_.clear();
        const SongList byTier = Arrange(catalog, kByDifficulty);
        for (const Section &section : byTier.sections) {
            if (section.count > 0) {
                tierNames_[catalog.At(byTier.rows[section.firstRow]).difficultyTier] =
                    section.title;
            }
        }
        sourceNames_.clear();
        const SongList bySource = Arrange(catalog, kBySource);
        for (const Section &section : bySource.sections) {
            if (section.count > 0) {
                sourceNames_[catalog.At(bySource.rows[section.firstRow]).source] =
                    section.title;
            }
        }
        Rebuild();
    }

    void BrowserModel::SetArrangement(Arrangement arrangement) {
        arrangement_ = arrangement;
        Rebuild();
    }

    void BrowserModel::SetQuery(const std::string &query) {
        query_ = query;
        Rebuild();
    }

    void BrowserModel::Rebuild() {
        rows_.clear();
        rowOfSong_.clear();
        songRows_ = 0;
        found_ = SearchResult();
        if (!catalog_) {
            return;
        }
        rowOfSong_.assign(static_cast<std::size_t>(catalog_->Size()), -1);
        const auto addSong = [&](BrowserRow row) {
            if (rowOfSong_[static_cast<std::size_t>(row.song)] < 0) {
                rowOfSong_[static_cast<std::size_t>(row.song)] =
                    static_cast<int>(rows_.size());
            }
            rows_.push_back(row);
            ++songRows_;
        };

        found_ = search_.Search(query_);
        if (found_.searching) {
            if (!found_.rows.empty()) {
                BrowserRow heading;
                heading.heading = true;
                rows_.push_back(heading);
            }
            for (int i = 0; i < static_cast<int>(found_.rows.size()); ++i) {
                BrowserRow row;
                row.song = found_.rows[static_cast<std::size_t>(i)].song;
                row.result = i;
                addSong(row);
            }
            return;
        }

        ArrangeOptions options;
        options.minSectionSongs = kMinSectionSongs;
        list_ = Arrange(*catalog_, arrangement_, options);
        for (int s = 0; s < static_cast<int>(list_.sections.size()); ++s) {
            const Section &section = list_.sections[static_cast<std::size_t>(s)];
            BrowserRow heading;
            heading.heading = true;
            heading.section = s;
            rows_.push_back(heading);
            for (int i = 0; i < section.count; ++i) {
                BrowserRow row;
                row.song = list_.rows[static_cast<std::size_t>(section.firstRow + i)];
                row.section = s;
                addSong(row);
            }
        }
    }

    int BrowserModel::SongAt(int row) const {
        if (row < 0 || row >= static_cast<int>(rows_.size())) {
            return -1;
        }
        return rows_[static_cast<std::size_t>(row)].song;
    }

    int BrowserModel::RowOfSong(int song) const {
        if (song < 0 || song >= static_cast<int>(rowOfSong_.size())) {
            return -1;
        }
        return rowOfSong_[static_cast<std::size_t>(song)];
    }

    int BrowserModel::SectionJump(int row, int direction) const {
        if (row < 0 || row >= static_cast<int>(rows_.size())) {
            return -1;
        }
        const int section = rows_[static_cast<std::size_t>(row)].section;
        if (section < 0) {
            return -1;
        }
        const int wanted = section + (direction < 0 ? -1 : 1);
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
            const BrowserRow &candidate = rows_[static_cast<std::size_t>(i)];
            if (!candidate.heading && candidate.section == wanted) {
                return i;
            }
        }
        return -1;
    }

    std::string BrowserModel::HeadingTitle(int row) const {
        if (row < 0 || row >= static_cast<int>(rows_.size())) {
            return {};
        }
        const int section = rows_[static_cast<std::size_t>(row)].section;
        if (section >= 0) {
            return list_.sections[static_cast<std::size_t>(section)].title;
        }
        const std::size_t matches = found_.rows.size();
        return std::to_string(matches) + (matches == 1 ? " song matches" : " songs match");
    }

    std::string BrowserModel::HeadingCount(int row) const {
        if (row < 0 || row >= static_cast<int>(rows_.size())) {
            return {};
        }
        const int section = rows_[static_cast<std::size_t>(row)].section;
        if (section < 0) {
            return {};
        }
        return "(" + std::to_string(list_.sections[static_cast<std::size_t>(section)].count)
            + ")";
    }

    const SearchRow *BrowserModel::ResultAt(int row) const {
        if (row < 0 || row >= static_cast<int>(rows_.size())) {
            return nullptr;
        }
        const int result = rows_[static_cast<std::size_t>(row)].result;
        return result < 0 ? nullptr : &found_.rows[static_cast<std::size_t>(result)];
    }

    std::string BrowserModel::TierName(int tier) const {
        const auto found = tierNames_.find(tier);
        return found == tierNames_.end() ? std::string() : found->second;
    }

    std::string BrowserModel::SourceName(SongSource source) const {
        const auto found = sourceNames_.find(source);
        return found == sourceNames_.end() ? std::string() : found->second;
    }

}
