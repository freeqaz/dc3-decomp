#include "meta/Jukebox.h"
#include "math/Rand.h"
#include "os/Debug.h"
#include <algorithm>

Jukebox::Jukebox(int numItems) : mPlayCounter(0) { mJukeboxItems.reserve(numItems); }

void Jukebox::AddItem(int i1, int i2) {
    JukeboxItem item(i1, i2);
    if (mJukeboxItems.size() == mJukeboxItems.capacity()) {
        MILO_NOTIFY("Growing jukebox capacity");
    }
    mJukeboxItems.push_back(item);
}

int Jukebox::Pick(const std::vector<int> &valid_names) {
    MILO_ASSERT(!valid_names.empty(), 0x26);
    std::vector<JukeboxItem> items;
    for (std::vector<int>::const_iterator it = valid_names.begin();
         it != valid_names.end();
         ++it) {
        std::vector<JukeboxItem>::iterator jit =
            std::find(mJukeboxItems.begin(), mJukeboxItems.end(), *it);
        if (jit == mJukeboxItems.end()) {
            AddItem(*it, -1);
            jit = &mJukeboxItems.back();
        }
        items.push_back(*jit);
    }
    std::random_shuffle(items.begin(), items.end());
    return items[RandomInt(0, items.size())].mName;
}

void Jukebox::Play(int x) {
    std::vector<JukeboxItem>::iterator jit =
        std::find(mJukeboxItems.begin(), mJukeboxItems.end(), x);
    if (jit == mJukeboxItems.end()) {
        AddItem(x, mPlayCounter);
    } else {
        jit->mLastPlayed = mPlayCounter;
    }
    mPlayCounter++;
}
