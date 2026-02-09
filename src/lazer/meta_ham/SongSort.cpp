#include "SongSort.h"

#include "AppLabel.h"
#include "ChallengeSort.h"
#include "SongSortMgr.h"
#include "SongSortNode.h"
#include "game/GameMode.h"
#include "meta_ham/NavListNode.h"
#include "meta_ham/SongSortMgr.h"
#include "os/Debug.h"
#include "ui/UILabel.h"
#include "ui/UIListLabel.h"
#include "utl/Std.h"

SongSort::SongSort() {};

void SongSort::BuildTree() {};

void SongSort::DeleteItemList() {
    NavListSort::DeleteItemList();
    TheSongSortMgr->ClearHeaders();
};

void SongSort::BuildItemList() {
    Symbol sym(gNullStr);
    if (unk50 && unk50->GetType() == kNodeFunction) {
        sym = unk50->GetToken();
    }
    DeleteItemList();

    static Symbol song_select_mode("song_select_mode");
    static Symbol song_select_story("song_select_story");
    static Symbol song_select_playlist("song_select_playlist");
    static Symbol random_song("random_song");
    static Symbol perform("perform");
    static Symbol dance_battle("dance_battle");

    bool inPerform = TheGameMode->InMode(perform, true);
    bool inDanceBattle = TheGameMode->InMode(dance_battle, true);
    Symbol prop = TheGameMode->Property(song_select_mode, true)->Sym();

    if (TheSongSortMgr->HeadersSelectable() && (inPerform || inDanceBattle) && song_select_playlist != prop) {
        static Symbol finish_setlist("finish_setlist");
        SongFunctionNode *node = new SongFunctionNode(nullptr, finish_setlist, "ui/image/song_select_setlist_keep");
        node->SetShortcut(unk30[0]);
        unk3c.insert(unk3c.end(), node);

        if (inPerform) {
            static Symbol playlists("playlists");
            SongFunctionNode *playlistNode = new SongFunctionNode(nullptr, playlists, "ui/image/song_select_setlist_keep");
            playlistNode->SetShortcut(unk30[0]);
            unk3c.insert(unk3c.end(), playlistNode);
        }
    } else if (song_select_playlist == prop) {
        static Symbol finish_setlist("finish_setlist");
        SongFunctionNode *node = new SongFunctionNode(nullptr, finish_setlist, "ui/image/song_select_setlist_keep");
        node->SetShortcut(unk30[0]);
        unk3c.insert(unk3c.end(), node);
    }

    FOREACH(it, unk3c) {
        (*it)->Renumber(mList);
    }

    FOREACH(it, unk30) {
        (*it)->Renumber(mList);
    }

    if (song_select_playlist == prop) {
        FOREACH(it, unk3c) {
            (*it)->Renumber(mList);
        }
    }

    FOREACH(it, unk30) {
        (*it)->FinishBuildList(this);
    }

    if (sym != gNullStr) {
        unk50 = GetNode(sym);
    }

    TheSongSortMgr->FinalizeHeaders();
}

void SongSort::SetHighlightedIx(int i1) {
    unk54 = unk50;
    if (i1 >= 0 && i1 < mList.size()) {
        unk50 = mList[i1];
        TheSongSortMgr->OnHighlightChanged();
        return;
    }
    unk50 = 0;
};

void SongSort::SetHighlightItem(const NavListSortNode *node) {
    unk54 = unk50;
    unk50 = nullptr;
    if (node) {
        if (node->GetType() == kNodeFunction || node->GetType() == kNodeItem) {
            auto findIf = std::find_if(mList.begin(), mList.end(), SortNodeFind(node));
            if (findIf != mList.end()) {
                unk50 = *findIf;
                TheSongSortMgr->OnHighlightChanged();
            }
        }
    }
};

void SongSort::UpdateHighlight() {
    NavListSort::UpdateHighlight();
    TheSongSortMgr->OnHighlightChanged();
};

void SongSort::OnSelectShortcut(int i1) {
    NavListSort::OnSelectShortcut(i1);
    TheSongSortMgr->OnHighlightChanged();
};

void SongSort::Text(int i1, int i2, UIListLabel *listlabel, UILabel *uilabel) const {
    AppLabel *app_label = dynamic_cast<AppLabel *>(uilabel);
    MILO_ASSERT(app_label, 0x100);
    app_label->SetFromSongSelectNode(unk30[i2]);
};

Symbol SongSort::DetermineHeaderSymbolFromSong(Symbol sym) { return sym; };
