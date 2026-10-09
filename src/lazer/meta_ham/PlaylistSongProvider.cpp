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
// 99.966% (normalized, full ninja).  Two charged rows, both the same fact: the
// Symbol temp that receives GetShortNameFromSongID's return lives at 0x54(r1) in
// the image (`addi r3, r1, 0x54` at 0x8298517C, `lwz r11, 0x54(r1)` at
// 0x82985184) and at 0x50(r1) in ours -- MSVC REUSES the MILO_ASSERT vararg slot
// at 0x50, which is dead after Debug::Fail, while the image allocates 0x54 beside
// it.  The fourteen r29/r30/r31 rows are a three-way rotation of the same
// callee-saved set and the canonical ruler forgives them; idx 18's MakeString
// instantiation is an ICF representative, not a per-call-site type.
//
// REFUTED, each a full ninja on this row:
//   99.966  this form
//   98.271  `Symbol shortName;` hoisted to function scope above the assert
//   95.593  the Symbol temp inlined into the return, songID kept
//   94.576  everything inlined into one return expression
// Keep the two named locals; the slot pairing is an allocator decision.
// w21-ar (99.966, same 16 rows: the 0x50/0x54 slot pair plus the r29/r30/r31
// rotation -- image ret=r31, this=r30, i=r29; ours ret=r29, this=r31, i=r30).
// Inert: MILO_ASSERT_EXPR (no do/while scope) for the 0x6d assert; dropping
// the `else` around `return gNullStr`.
// w21-bn: SLOT CLOSED, 99.966 -> 100 normalized.  The negated early return
// puts `shortName` in FUNCTION scope, where its lifetime overlaps the
// MILO_ASSERT's do/while temp, so MSVC stops pooling the two and gives it
// 0x54 as the image does (the image's block order -- valid path first,
// gNullStr ctor last -- is unchanged).  Same behaviour: every operand of the
// && chain is an int/pointer/bool, so the negation is exact.  The 14 rows
// left were the r29/r30/r31 rotation (image ret=r31 this=r30 i=r29).
// w25-gc: three early returns instead of one. Each `return gNullStr` adds
// references to the hidden sret pointer (colour priority 5 -> 9 -> 13), so it
// is popped before `this` (11) and `i` (8): sret r31, this r30, i r29 as the
// image. The three gNullStr tails cross-jump back into one block after
// colouring, so the block order is unchanged. Four returns (splitting `i >= 0`
// from `i < NumData()` as well) reorder the blocks.
Symbol PlaylistSongProvider::DataSymbol(int i) const {
    MILO_ASSERT(m_pPlaylist, 0x6d);
    if (!(i >= 0 && i < NumData())) {
        return gNullStr;
    }
    if (!m_pPlaylist) {
        return gNullStr;
    }
    if (!m_pPlaylist->IsValidSong(i)) {
        return gNullStr;
    }
    int songID = m_pPlaylist->GetSong(i);
    Symbol shortName = TheHamSongMgr.GetShortNameFromSongID(songID);
    return shortName;
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
