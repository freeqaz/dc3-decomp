// Song browser data - the catalog.  See SongCatalog.h.

#include "SongCatalog.h"

namespace songbrowser {

SongSource SourceFromOrigin(const std::string &origin) {
    if (origin == "ham1") return kSourceDC1;
    if (origin == "ham2") return kSourceDC2;
    if (origin == "ham3") return kSourceDC3;
    return kSourceDownload;
}

const char *SourceToken(SongSource source) {
    switch (source) {
    case kSourceDC1:
        return "ham1";
    case kSourceDC2:
        return "ham2";
    case kSourceDC3:
        return "ham3";
    default:
        return "DLC";
    }
}

void SongCatalog::Clear() {
    mEntries.clear();
    mByShortName.clear();
    mById.clear();
    mHistory.clear();
    mLabels.clear();
}

int SongCatalog::Add(const SongEntry &entry) {
    std::map<std::string, int>::iterator existing = mByShortName.find(entry.shortName);
    int index;
    if (existing != mByShortName.end()) {
        index = existing->second;
        mById.erase(mEntries[index].id);
        mEntries[index] = entry;
    } else {
        index = (int)mEntries.size();
        mEntries.push_back(entry);
        mByShortName[entry.shortName] = index;
    }
    mById[entry.id] = index;
    return index;
}

int SongCatalog::IndexOf(const std::string &shortName) const {
    std::map<std::string, int>::const_iterator it = mByShortName.find(shortName);
    return it == mByShortName.end() ? -1 : it->second;
}

int SongCatalog::IndexOfId(int id) const {
    std::map<int, int>::const_iterator it = mById.find(id);
    return it == mById.end() ? -1 : it->second;
}

const SongEntry *SongCatalog::Find(const std::string &shortName) const {
    int index = IndexOf(shortName);
    return index < 0 ? nullptr : &mEntries[index];
}

const SongEntry *SongCatalog::FindById(int id) const {
    int index = IndexOfId(id);
    return index < 0 ? nullptr : &mEntries[index];
}

void SongCatalog::SetRecents(const std::vector<std::string> &newestFirst) {
    // A name listed twice counts once, at its newest place.
    mHistory.clear();
    for (size_t i = 0; i < newestFirst.size(); i++) {
        bool seen = false;
        for (size_t j = 0; j < mHistory.size() && !seen; j++) {
            seen = mHistory[j] == newestFirst[i];
        }
        if (!seen) mHistory.push_back(newestFirst[i]);
    }
    for (size_t i = 0; i < mEntries.size(); i++) {
        mEntries[i].recentRank = -1;
    }
    for (size_t i = 0; i < mHistory.size(); i++) {
        int index = IndexOf(mHistory[i]);
        if (index < 0) continue;
        mEntries[index].recentRank = (int)i;
        mEntries[index].played = true;
    }
}

std::vector<int> SongCatalog::Recents(int maxRows) const {
    std::vector<int> recents;
    for (size_t i = 0; i < mHistory.size(); i++) {
        if (maxRows > 0 && (int)recents.size() >= maxRows) break;
        int index = IndexOf(mHistory[i]);
        // Not in this catalog (a pack that is no longer installed), or not
        // selectable: song select's Recently Played skips both.
        if (index < 0 || mEntries[index].locked) continue;
        recents.push_back(index);
    }
    return recents;
}

void SongCatalog::SetLabel(const std::string &token, const std::string &text) {
    mLabels[token] = text;
}

std::string SongCatalog::Label(const std::string &token, const std::string &fallback) const {
    std::map<std::string, std::string>::const_iterator it = mLabels.find(token);
    return it == mLabels.end() ? fallback : it->second;
}

} // namespace songbrowser
