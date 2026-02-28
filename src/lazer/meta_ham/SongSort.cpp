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

void SongSort::BuildTree() {
    DeleteTree();
    Init();

    // Build a sorted list of song nodes, grouped by header type
    std::vector<NavListItemNode *> sortedNodes;
    FOREACH (songEntry, TheSongSortMgr->mSongRecordMap) {
        NavListItemNode *songNode = NewItemNode(&songEntry->second);
        auto insertPos =
            std::lower_bound(sortedNodes.begin(), sortedNodes.end(), songNode, CompareHeaders());
        sortedNodes.insert(insertPos, songNode);
    }

    // Group items into shortcuts with smart header merging.
    // For "by_song" and "by_artist" sorts, small groups are combined
    // to avoid too many tiny header sections.
    bool deferring = false;
    int cumulativeCount = 0;
    auto shortcutStart = sortedNodes.begin();
    auto groupStart = sortedNodes.begin();

    if (groupStart != sortedNodes.end()) {
        do {
            auto range = std::equal_range(
                sortedNodes.begin(), sortedNodes.end(), *groupStart, CompareHeaders()
            );
            auto rangeEnd = range.second;
            int groupSize = range.second - range.first;
            int remaining = sortedNodes.end() - groupStart;
            cumulativeCount += groupSize;
            int leftover = remaining - groupSize;

            static Symbol by_song("by_song");
            static Symbol by_artist("by_artist");
            if (leftover <= 0 || cumulativeCount >= 4
                || (mSortName != by_song
                    && (mSortName != by_artist || TheSongSortMgr->IsInHeaderMode())))
            {
                // Flush: determine the end of this shortcut's range
                auto endPtr = groupStart;
                if ((deferring && cumulativeCount <= 12) || !deferring) {
                    endPtr = rangeEnd;
                }

                NavListShortcutNode *shortcut = NewShortcutNode(*shortcutStart);
                mShortcutNodes.push_back(shortcut);
                shortcut->InsertHeaderRange(&*shortcutStart, &*endPtr, this);

                cumulativeCount = 0;
                deferring = false;
                shortcutStart = endPtr;
                groupStart = endPtr;
            } else {
                // Defer: accumulate this group with the next one
                deferring = true;
                groupStart = rangeEnd;
            }
        } while (shortcutStart != sortedNodes.end());
    }

    // Finalize each shortcut's internal structure
    FOREACH (it, mShortcutNodes) {
        (*it)->FinishSort(this);
    }
}

void SongSort::DeleteItemList() {
    NavListSort::DeleteItemList();
    TheSongSortMgr->ClearHeaders();
};

void SongSort::BuildItemList() {
    Symbol sym(gNullStr);
    if (mHighlightNode && mHighlightNode->GetType() == kNodeFunction) {
        sym = mHighlightNode->GetToken();
    }
    DeleteItemList();

    static Symbol perform("perform");
    static Symbol song_select_playlist("song_select_playlist");
    static Symbol random_song("random_song");
    static Symbol dance_battle("dance_battle");
    bool inDanceBattle = TheGameMode->InMode(dance_battle, true);
    static Symbol song_select_story("song_select_story");

    bool inPerform = TheGameMode->InMode(perform, true);
    static Symbol song_select_mode("song_select_mode");
    Symbol prop = TheGameMode->Property(song_select_mode, true)->Sym();

    if (TheSongSortMgr->HeadersSelectable() && (inPerform || inDanceBattle) && song_select_playlist != prop) {
        static Symbol finish_setlist("finish_setlist");
        SongFunctionNode *node = new SongFunctionNode(nullptr, finish_setlist, "ui/image/song_select_setlist_keep");
        node->SetShortcut(mShortcutNodes[0]);
        mAllNodes.insert(mAllNodes.end(), node);

        if (inPerform) {
            static Symbol playlists("playlists");
            SongFunctionNode *playlistNode = new SongFunctionNode(nullptr, playlists, "ui/image/song_select_setlist_keep");
            playlistNode->SetShortcut(mShortcutNodes[0]);
            mAllNodes.insert(mAllNodes.end(), playlistNode);
        }
    } else if (song_select_playlist == prop) {
        static Symbol finish_setlist("finish_setlist");
        SongFunctionNode *node = new SongFunctionNode(nullptr, finish_setlist, "ui/image/song_select_setlist_keep");
        node->SetShortcut(mShortcutNodes[0]);
        auto _tmp2 = mAllNodes.end();
        mAllNodes.insert(_tmp2, node);
    }

    FOREACH(it, mAllNodes) {
        (*it)->Renumber(mList);
    }

    FOREACH(it, mShortcutNodes) {
        (*it)->Renumber(mList);
    }

    if (song_select_playlist == prop) {
        FOREACH(it, mAllNodes) {
            (*it)->Renumber(mList);
        }
    }

    FOREACH(it, mShortcutNodes) {
        (*it)->FinishBuildList(this);
    }

    if (sym != gNullStr) {
        mHighlightNode = GetNode(sym);
    }

    TheSongSortMgr->FinalizeHeaders();
}

void SongSort::SetHighlightedIx(int i1) {
    mPrevHighlightNode = mHighlightNode;
    if (i1 >= 0 && i1 < mList.size()) {
        mHighlightNode = mList[i1];
        TheSongSortMgr->OnHighlightChanged();
        return;
    }
    mHighlightNode = 0;
};

void SongSort::SetHighlightItem(const NavListSortNode *node) {
    mPrevHighlightNode = mHighlightNode;
    mHighlightNode = nullptr;
    if (node) {
        if (node->GetType() == kNodeFunction || node->GetType() == kNodeItem) {
            auto findIf = std::find_if(mList.begin(), mList.end(), SortNodeFind(node));
            if (findIf != mList.end()) {
                mHighlightNode = *findIf;
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
    app_label->SetFromSongSelectNode(mShortcutNodes[i2]);
};

Symbol SongSort::DetermineHeaderSymbolFromSong(Symbol sym) {
    std::map<Symbol, SongRecord>::iterator it = TheSongSortMgr->mSongRecordMap.find(sym);
    if (it != TheSongSortMgr->mSongRecordMap.end()) {
        NavListItemNode *node = NewItemNode(&it->second);
        for (auto shortcutIt = mShortcutNodes.begin(); shortcutIt != mShortcutNodes.end();
             shortcutIt++) {
            NavListShortcutNode *shortcut = *shortcutIt;
            const std::list<NavListSortNode *> &children = shortcut->Children();
            MILO_ASSERT(children.size() == 1, 0xea);
            NavListHeaderNode *header =
                dynamic_cast<NavListHeaderNode *>(shortcut->FirstChild());
            MILO_ASSERT(header != NULL, 0xec);
            if (node->Compare(header, kNodeHeader) == 0) {
                delete node;
                return header->GetToken();
            }
        }
        delete node;
        Symbol nullSym(gNullStr);
        return nullSym;
    }
    return sym;
};
