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
    int, int i_iData, UIListLabel *uiListLabel, UILabel *uiLabel
) const {
    MILO_ASSERT(i_iData < NumData(), 0x22);
    Symbol dataSym = DataSymbol(i_iData);
    if (uiListLabel->Matches("song")) {
        static Symbol playlist_addsong("playlist_addsong");
        if (dataSym == playlist_addsong) {
            static Symbol songname_numbered("songname_numbered");
            uiLabel->SetTokenFmt(songname_numbered, i_iData + 1, playlist_addsong);
        } else {
            AppLabel *pAppLabel = dynamic_cast<AppLabel *>(uiLabel);
            MILO_ASSERT(pAppLabel, 0x31);
            if (NumData() <= 20 || (i_iData < 0x13)) {
                pAppLabel->SetSongName(dataSym, i_iData + 1, false);
                return;
            }
            static Symbol ellipsis("ellipsis");
            pAppLabel->SetTextToken(ellipsis);
        }
    } else if (uiListLabel->Matches("song_length")) {
        static Symbol playlist_addsong("playlist_addsong");
        if (dataSym != playlist_addsong) {
            if (NumData() <= 20 || i_iData < 19) {
                AppLabel *pAppLabel = dynamic_cast<AppLabel *>(uiLabel);
                MILO_ASSERT(pAppLabel, 0x4d);
                pAppLabel->SetSongDuration(dataSym);
                return;
            } else {
                static Symbol ellipsis("ellipsis");
                uiLabel->SetTextToken(gNullStr);
            }
        } else {
            uiLabel->SetTextToken(gNullStr);
        }
    } else {
        uiLabel->SetTextToken(gNullStr);
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
