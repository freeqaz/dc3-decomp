#include "lazer/meta_ham/PlaylistSongProvider.h"
#include "Playlist.h"
#include "macros.h"
#include "meta_ham/AppLabel.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "ui/UILabel.h"
#include "ui/UIListLabel.h"
#include "ui/UIListProvider.h"
#include "utl/Symbol.h"

PlaylistSongProvider::PlaylistSongProvider() : unk30(0), unk34(false) {}

PlaylistSongProvider::~PlaylistSongProvider() {}

int PlaylistSongProvider::NumData() const {
    if (unk30 == nullptr) {
        return 0;
    }
    return unk30->GetNumSongs();
}

Symbol PlaylistSongProvider::DataSymbol(int m_pPlaylist) const {
    MILO_ASSERT(m_pPlaylist, 0x6d);
    return Symbol(0);
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
            uiLabel->SetTokenFmt(songname_numbered, i_iData + playlist_addsong);
            return;
        }
        AppLabel *pAppLabel = dynamic_cast<AppLabel *>(uiLabel);
        MILO_ASSERT(pAppLabel, 0x31);
        if (NumData() > 0x14 && i_iData >= 0x13) {
            static Symbol ellipsis("ellipsis");
            uiLabel->SetTextToken(ellipsis);
        } else {
            pAppLabel->SetSongName(dataSym, i_iData + 1, false);
        }
    } else if (uiListLabel->Matches("song_length")) {
        static Symbol playlist_addsong("playlist_addsong");
        if (dataSym == playlist_addsong) {
            static Symbol ellipsis("ellipsis");
            uiLabel->SetTextToken(ellipsis);
        } else if (NumData() > 0x14 && i_iData >= 0x13) {
            static Symbol ellipsis("ellipsis");
            uiLabel->SetTextToken(ellipsis);
        } else {
            AppLabel *pAppLabel = dynamic_cast<AppLabel *>(uiLabel);
            MILO_ASSERT(pAppLabel, 0x4d);
            pAppLabel->SetSongDuration(dataSym);
        }
    }
}

void PlaylistSongProvider::UpdateList(Playlist const *p, bool b) {
    unk34 = b;
    unk30 = p;
}

BEGIN_HANDLERS(PlaylistSongProvider)
    HANDLE_SUPERCLASS(UIListProvider)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
