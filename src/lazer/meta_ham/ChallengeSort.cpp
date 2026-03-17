#include "ChallengeSort.h"
#include "AppLabel.h"
#include "Challenges.h"
#include "ChallengeSortByScore.h"
#include "ChallengeSortMgr.h"
#include "ChallengeSortNode.h"
#include "meta_ham/MQSongSort.h"
#include "meta_ham/NavListSort.h"
#include "obj/Object.h"

struct NodeFind {
    NodeFind(Symbol t) : token(t) {}
    bool operator()(const NavListNode *n) const { return n->GetToken() == token; }
    Symbol token;
};

SortNodeFind::SortNodeFind(const NavListSortNode *node)
    : mToken(node->GetToken()), mType(node->GetType()) {}

bool SortNodeFind::operator()(const NavListSortNode *node) const {
    return node->GetToken() == mToken && node->GetType() == mType;
}

#pragma region ChallengeSort

ChallengeSort::ChallengeSort() {}

BEGIN_HANDLERS(ChallengeSort)
    HANDLE_SUPERCLASS(NavListSort)
END_HANDLERS

void ChallengeSort::SetHighlightedIx(int idx) {
    mPrevHighlightNode = mHighlightNode;
    if (idx >= 0) {
        if (mList.size() >= idx) {
            if (mList.size() == 0)
                return;
            mHighlightNode = mList[idx];
            TheChallengeSortMgr->OnHighlightChanged();
            return;
        }
    }
    mHighlightNode = nullptr;
}

void ChallengeSort::DeleteItemList() {
    NavListSort::DeleteItemList();
    TheChallengeSortMgr->ClearHeaders();
}

void ChallengeSort::UpdateHighlight() {
    if (mList.size() != 0) {
        NavListSort::UpdateHighlight();
        TheChallengeSortMgr->OnHighlightChanged();
    }
}

void ChallengeSort::OnSelectShortcut(int idx) {
    if (mList.size() != 0) {
        NavListSort::OnSelectShortcut(idx);
        TheChallengeSortMgr->OnHighlightChanged();
    }
}

void ChallengeSort::Text(int i1, int i2, UIListLabel *listlabel, UILabel *label) const {
    AppLabel *app_label = dynamic_cast<AppLabel *>(label);
    MILO_ASSERT(app_label, 0xe1);
    app_label->SetFromGeneralSelectNode(mShortcutNodes[i2]);
}

void ChallengeSort::SetHighlightItem(const NavListSortNode *node) {
    mPrevHighlightNode = mHighlightNode;
    mHighlightNode = nullptr;
    if (node) {
        if (node->GetType() == 5 || node->GetType() == 4) {
            auto findNode = std::find_if(mList.begin(), mList.end(), SortNodeFind(node));
            if (findNode != mList.end()) {
                mHighlightNode = *findNode;
                TheChallengeSortMgr->OnHighlightChanged();
            }
        }
    }
}

void ChallengeSort::BuildTree() {
    DeleteTree();
    Init();
    std::vector<NavListItemNode *> sortedItems;
    unsigned int i = 0;
    std::vector<ChallengeRecord> &records = TheChallengeSortMgr->mChallengeRecords;
    if (records.size() != 0) {
        do {
            if (records[i].GetSongContentLockState() != 1) {
                NavListItemNode *item = NewItemNode(&records[i]);
                auto pos =
                    std::lower_bound(sortedItems.begin(), sortedItems.end(), item, CompareHeaders());
                sortedItems.insert(pos, 1, item);
            }
            i++;
        } while (i < records.size());
    }

    static Symbol global_challenge("global_challenge");
    static Symbol dlc_challenge("dlc_challenge");

    String globalSongName = TheChallenges->GetGlobalChallengeSongName();
    String dlcSongName = TheChallenges->GetDlcChallengeSongName();

    NavListShortcutNode *globalShortcut =
        new NavListShortcutNode(new ChallengeScoreCmp(0, 0, globalSongName.c_str()), global_challenge, true);
    NavListShortcutNode *dlcShortcut =
        new NavListShortcutNode(new ChallengeScoreCmp(1, 0, dlcSongName.c_str()), dlc_challenge, true);

    ChallengeHeaderNode *globalHeader =
        new ChallengeHeaderNode(new ChallengeScoreCmp(0, 0, globalSongName.c_str()), global_challenge, true);
    ChallengeHeaderNode *dlcHeader =
        new ChallengeHeaderNode(new ChallengeScoreCmp(1, 0, dlcSongName.c_str()), dlc_challenge, true);

    globalShortcut->mChildren.insert(globalShortcut->mChildren.end(), (NavListSortNode *)globalHeader);
    dlcShortcut->mChildren.insert(dlcShortcut->mChildren.end(), (NavListSortNode *)dlcHeader);

    mShortcutNodes.push_back(globalShortcut);
    mShortcutNodes.push_back(dlcShortcut);

    for (auto it = sortedItems.begin(); it != sortedItems.end(); ++it) {
        NavListShortcutNode *shortcut = NewShortcutNode(*it);
        Symbol token = shortcut->GetToken();
        auto found =
            std::find_if(mShortcutNodes.begin(), mShortcutNodes.end(), NodeFind(token));
        if (found == mShortcutNodes.end()) {
            mShortcutNodes.push_back(shortcut);
        } else {
            delete shortcut;
            shortcut = *found;
        }
        shortcut->Insert(*it, this);
    }

    for (auto it = mShortcutNodes.begin(); it != mShortcutNodes.end(); ++it) {
        (*it)->FinishSort(this);
    }
}

void ChallengeSort::BuildItemList() {
    Symbol sym(gNullStr);
    auto sortNode = mHighlightNode;
    if (sortNode && sortNode->GetType() == 5) {
        sym = sortNode->GetToken();
    }
    DeleteItemList();
    FOREACH (it, mAllNodes) {
        (*it)->Renumber(mList);
    }
    FOREACH (it, mShortcutNodes) {
        (*it)->Renumber(mList);
    }
    FOREACH (it, mShortcutNodes) {
        (*it)->FinishBuildList(this);
    }
    if (!sym.Null()) {
        mHighlightNode = GetNode(sym);
    }
    TheChallengeSortMgr->FinalizeHeaders();
}

#pragma endregion

BEGIN_HANDLERS(MQSongSort)
    HANDLE_SUPERCLASS(NavListSort)
END_HANDLERS
