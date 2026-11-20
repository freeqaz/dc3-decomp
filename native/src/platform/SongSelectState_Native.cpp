// DC3 Native Port - what song select remembers across launches.
// See SongSelectState_Native.h and docs/native/SONG_SELECT.md.

#include "SongSelectState_Native.h"
#include "SaveStore_Web.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

// MemcardMgr_Stub.cpp: the directory the profile saves live in, created.
extern const char *NativeSaveDir();

namespace {

const char *const kMagic = "dc3-song-select";
const int kFormatVersion = 1;

// A song short name or a sort name: one word, no spaces.  Anything else in
// the file is somebody's edit gone wrong and is dropped rather than shown.
bool IsWord(const std::string &word) {
    if (word.empty() || word.size() > 128) return false;
    for (size_t i = 0; i < word.size(); i++) {
        unsigned char c = (unsigned char)word[i];
        if (c <= ' ' || c == 0x7f) return false;
    }
    return true;
}

} // namespace

NativeSongSelectState::NativeSongSelectState(const std::string &path) : mPath(path) {}

void NativeSongSelectState::Load() {
    mSort.clear();
    mHistory.clear();
    FILE *file = fopen(mPath.c_str(), "r");
    if (!file) return;
    char line[512];
    bool first = true;
    while (fgets(line, sizeof(line), file)) {
        char key[32] = {};
        char value[256] = {};
        int fields = sscanf(line, "%31s %255s", key, value);
        if (first) {
            first = false;
            // Not ours: leave it alone and start empty.
            if (fields < 1 || strcmp(key, kMagic) != 0) break;
            continue;
        }
        if (fields != 2 || !IsWord(value)) continue;
        if (strcmp(key, "sort") == 0) {
            mSort = value;
        } else if (strcmp(key, "played") == 0) {
            if ((int)mHistory.size() < kMaxHistory
                && std::find(mHistory.begin(), mHistory.end(), value) == mHistory.end()) {
                mHistory.push_back(value);
            }
        }
    }
    fclose(file);
}

bool NativeSongSelectState::Save() const {
    std::string tmpPath = mPath + ".tmp";
    FILE *file = fopen(tmpPath.c_str(), "w");
    if (!file) {
        fprintf(stderr, "DC3 Native: song select: cannot write %s\n", tmpPath.c_str());
        return false;
    }
    fprintf(file, "%s %d\n", kMagic, kFormatVersion);
    if (!mSort.empty()) fprintf(file, "sort %s\n", mSort.c_str());
    for (size_t i = 0; i < mHistory.size(); i++) {
        fprintf(file, "played %s\n", mHistory[i].c_str());
    }
    bool written = !ferror(file);
    if (fclose(file) != 0) written = false;
    if (!written || rename(tmpPath.c_str(), mPath.c_str()) != 0) {
        fprintf(stderr, "DC3 Native: song select: cannot write %s\n", mPath.c_str());
        remove(tmpPath.c_str());
        return false;
    }
    // Web: the file is in the save directory the page keeps in IndexedDB
    // (PLAN W-36); tell it now, as the profile and global-options writers do,
    // so the list survives a tab that is closed without a pagehide.
    Dc3SaveStoreChanged();
    return true;
}

void NativeSongSelectState::NotePlayed(const std::string &song) {
    if (!IsWord(song)) return;
    if (!mHistory.empty() && mHistory.front() == song) return;
    mHistory.erase(std::remove(mHistory.begin(), mHistory.end(), song), mHistory.end());
    mHistory.insert(mHistory.begin(), song);
    if ((int)mHistory.size() > kMaxHistory) mHistory.resize(kMaxHistory);
    Save();
}

void NativeSongSelectState::SetSort(const std::string &sort) {
    if (!IsWord(sort) || sort == mSort) return;
    mSort = sort;
    Save();
}

NativeSongSelectState &TheNativeSongSelectState() {
    static NativeSongSelectState *sState = nullptr;
    if (!sState) {
        sState = new NativeSongSelectState(std::string(NativeSaveDir()) + "/song_select.txt");
        sState->Load();
    }
    return *sState;
}
