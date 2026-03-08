#include "meta_ham/PlaylistSongProvider.h"
#include "Playlist.h"
#include "HamSongMgr.h"
#include "macros.h"
#include "meta_ham/AppLabel.h"
#include "meta_ham/HamStoreProvider.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "ui/UILabel.h"
#include "ui/UIListLabel.h"
#include "ui/UIListProvider.h"
#include "utl/Symbol.h"

PlaylistSongProvider::PlaylistSongProvider() : mPlaylist(0), unk34(false) {}

PlaylistSongProvider::~PlaylistSongProvider() {}

PackSongListProvider::~PackSongListProvider() {}

int PlaylistSongProvider::NumData() const {
    if (mPlaylist == nullptr) {
        return 0;
    }
    return mPlaylist->GetNumSongs();
}

Symbol PlaylistSongProvider::DataSymbol(int idx) const {
    MILO_ASSERT(mPlaylist, 0x6d);

    if (idx >= 0 && idx < NumData()) {
        if (mPlaylist != nullptr && mPlaylist->IsValidSong(idx)) {
            int songID = mPlaylist->GetSong(idx);
            auto songShortName = TheHamSongMgr.GetShortNameFromSongID(songID, true);
            return songShortName;
        }
    }

    return Symbol(nullptr);
}

void PlaylistSongProvider::Text(
    int i1, int data, UIListLabel *slot, UILabel *label
) const {
    MILO_ASSERT(data < NumData(), 0x22);
    Symbol dataSym = DataSymbol(data);
    if (slot->Matches("song")) {
        static Symbol playlist_addsong("playlist_addsong");
        if ((unsigned int)dataSym == playlist_addsong) {
            static Symbol songname_numbered("songname_numbered");
            label->SetTokenFmt(songname_numbered, data + 1, playlist_addsong);
            return;
        }
        AppLabel *pAppLabel = dynamic_cast<AppLabel *>(label);
        MILO_ASSERT(pAppLabel, 0x31);
        if (!(NumData() > 0x14 && data > (int)0x12)) {
            pAppLabel->SetSongName(dataSym, data + 1, false);
        } else {
            static Symbol ellipsis("ellipsis");
            label->SetTextToken(ellipsis);
        }
    } else if (slot->Matches("song_length")) {
        static Symbol playlist_addsong("playlist_addsong");
        if ((int)dataSym != playlist_addsong) {
            if ((NumData() <= 0x14 || data < 19)) {
                AppLabel *pAppLabel = dynamic_cast<AppLabel *>(label);
                MILO_ASSERT(pAppLabel, 0x4d);
                pAppLabel->SetSongDuration(dataSym);
            } else {
                static Symbol ellipsis("ellipsis");
                label->SetTextToken(ellipsis);
            }
        } else {
            static Symbol ellipsis("ellipsis");
            label->SetTextToken(ellipsis);
        }
    }
}

void PlaylistSongProvider::UpdateList(Playlist const *p, bool b) {
    unk34 = b;
    mPlaylist = p;
}

BEGIN_HANDLERS(PlaylistSongProvider)
    HANDLE_SUPERCLASS(UIListProvider)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
