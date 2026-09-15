#include "meta_ham\PlaylistSongProvider.h"
#include "Playlist.h"
#include "HamSongMgr.h"
#include "macros.h"
#include "meta_ham\AppLabel.h"
#include "meta_ham\HamStoreProvider.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "ui\UILabel.h"
#include "ui\UIListLabel.h"
#include "ui\UIListProvider.h"
#include "utl\Symbol.h"

PlaylistSongProvider::PlaylistSongProvider() : m_pPlaylist(0), unk34(false) {}

PackSongListProvider::~PackSongListProvider() {}

int PlaylistSongProvider::NumData() const {
    if (m_pPlaylist == nullptr) {
        return 0;
    }
    return m_pPlaylist->GetNumSongs();
}

// w8-l: 99.966100 normalized, and this is the ONLY function keeping the unit
// from 100% (12/13).  Only TWO rows are charged, both a stack-slot choice:
//   [47] addi r3, r1, 0x54  (target)  vs  0x50 (ours)
//   [49] lwz  r11, 0x54(r1) (target)  vs  0x50(r1) (ours)
// The image gives the MILO_ASSERT line-number temp (MakeString takes const int&,
// so 0x6d needs a home) its own slot at 0x50 and puts `shortName` at 0x54; we
// pool both into 0x50.  The other 14 rows are a callee-saved permutation the
// canonical ruler forgives (target r31=r3/r30=r4/r29=r5, ours r29/r31/r30).
// Refuted here: returning the call directly instead of naming `shortName`
// regressed to 94.57627; naming only the songID temp regressed to 95.59322.
// Hoisting `Symbol shortName;` above the MILO_ASSERT to force overlapping live
// ranges is not viable -- Symbol's default ctor is non-trivial (mStr(gNullStr))
// and would emit a store the image does not have.
// Same unsolved family as MidiParserMgr::ParseText, WebSvcMgrCurl::Poll and
// AccomplishmentProgress::AddAccomplishment: the image never pools the first
// 4-byte assert temp with a later temp, and no source lever found does that.
Symbol PlaylistSongProvider::DataSymbol(int i) const {
    MILO_ASSERT(m_pPlaylist, 0x6d);
    if (i >= 0 && i < NumData() && m_pPlaylist && m_pPlaylist->IsValidSong(i)) {
        int songID = m_pPlaylist->GetSong(i);
        Symbol shortName = TheHamSongMgr.GetShortNameFromSongID(songID);
        return shortName;
    } else {
        return gNullStr;
    }
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
    m_pPlaylist = p;
}

BEGIN_HANDLERS(PlaylistSongProvider)
    HANDLE_SUPERCLASS(UIListProvider)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
