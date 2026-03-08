#include "MQSongSort.h"

#include "AppLabel.h"
#include "ChallengeSort.h"
#include "MQSongSortMgr.h"

MQSongSort::MQSongSort() {};

void MQSongSort::DeleteItemList() {
    NavListSort::DeleteItemList();
    TheMQSongSortMgr->ClearHeaders();
}

void MQSongSort::SetHighlightedIx(int i1) {
    mPrevHighlightNode = mHighlightNode;
    if (i1 >= 0) {
        if (mList.size() >= i1) {
            if (mList.size() == 0) {
                return;
            }
            mHighlightNode = mList[i1];
            TheMQSongSortMgr->OnHighlightChanged();
            return;
        }
    }
    mHighlightNode = 0;
}

void MQSongSort::UpdateHighlight() {
    if (mList.size() != 0) {
        NavListSort::UpdateHighlight();
        TheMQSongSortMgr->OnHighlightChanged();
    }
}

void MQSongSort::OnSelectShortcut(int i1) {
    if (mList.size() != 0) {
        NavListSort::OnSelectShortcut(i1);
        TheMQSongSortMgr->OnHighlightChanged();
    }
}

void MQSongSort::Text(int i1, int i2, UIListLabel *listlabel, UILabel *label) const {
    AppLabel *app_label = dynamic_cast<AppLabel *>(label);
    MILO_ASSERT(app_label, 0xd0);

    app_label->SetFromGeneralSelectNode(mShortcutNodes[i2]);
}

void MQSongSort::SetHighlightItem(const NavListSortNode *node) {
    NavListSortNode *tempNode = mHighlightNode;
    mHighlightNode = nullptr;
    mPrevHighlightNode = tempNode;
    if (node) {
        if (node->GetType() == 5 || node->GetType() == 4) {
            auto find = std::find_if(mList.begin(), mList.end(), SortNodeFind(node));
            if (find != mList.end()) {
                mHighlightNode = *find;
                TheMQSongSortMgr->OnHighlightChanged();
            }
        }
    }
}

void MQSongSort::BuildItemList() {
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
    TheMQSongSortMgr->FinalizeHeaders();
}
