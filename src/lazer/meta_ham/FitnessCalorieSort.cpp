#include "meta_ham/FitnessCalorieSort.h"
#include "FitnessCalorieSortMgr.h"
#include "NavListSort.h"
#include "meta_ham/AppLabel.h"
#include "meta_ham/ChallengeSort.h"
#include "meta_ham/NavListNode.h"
#include "os/Debug.h"
#include "rndobj/Mesh.h"
#include "stl/_algo.h"
#include "stl/_vector.h"
#include "ui/UILabel.h"
#include "ui/UIListLabel.h"
#include "utl/Std.h"
#include "utl/Symbol.h"

FitnessCalorieSort::FitnessCalorieSort() {}

void FitnessCalorieSort::Text(
    int, int idx, UIListLabel *uiListLabel, UILabel *uiLabel
) const {
    AppLabel *app_label = dynamic_cast<AppLabel *>(uiLabel);
    MILO_ASSERT(app_label, 0xa4);
    app_label->SetFromGeneralSelectNode(mShortcutNodes[idx]);
}
void FitnessCalorieSort::DeleteItemList() {
    NavListSort::DeleteItemList();
    TheFitnessCalorieSortMgr->ClearHeaders();
}

void FitnessCalorieSort::SetHighlightedIx(int idx) {
    mPrevHighlightNode = mHighlightNode;
    if (0 <= idx) {
        if (mList.size() >= idx) {
            if (mList.size() == 0) {
                return;
            }
            mHighlightNode = mList[idx];
            TheFitnessCalorieSortMgr->OnHighlightChanged();
            return;
        }
    }
    mHighlightNode = 0;
}

void FitnessCalorieSort::UpdateHighlight() {
    if (mList.size() != 0) {
        NavListSort::UpdateHighlight();
        TheFitnessCalorieSortMgr->OnHighlightChanged();
    }
}

void FitnessCalorieSort::OnSelectShortcut(int i) {
    if (mList.size() != 0) {
        NavListSort::OnSelectShortcut(i);
        TheFitnessCalorieSortMgr->OnHighlightChanged();
    }
}

void FitnessCalorieSort::SetHighlightItem(NavListSortNode const *node) {
    NavListSortNode *tempNode = mHighlightNode;
    mHighlightNode = nullptr;
    mPrevHighlightNode = tempNode;
    if (node) {
        if (node->GetType() == 5 || node->GetType() == 4) {
            auto find = std::find_if(mList.begin(), mList.end(), SortNodeFind(node));
            if (find != mList.end()) {
                mHighlightNode = *find;
                TheFitnessCalorieSortMgr->OnHighlightChanged();
            }
        }
    }
}

void FitnessCalorieSort::BuildItemList() {
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
    TheFitnessCalorieSortMgr->FinalizeHeaders();
}

void FitnessCalorieSort::BuildTree() {
    NavListSort::DeleteTree();
    Init();
    std::vector<NavListItemNode *> nodes;

    // Populate nodes from TheFitnessCalorieSortMgr calories
    std::vector<int> &calories = TheFitnessCalorieSortMgr->GetCalorieValues();
    for (int i = 0; (unsigned int)i < (int)calories.size(); i++) {
        NavListItemNode *node = NewItemNode((void *)&calories[i]);
        nodes.push_back(node);
    }

    // Process shortcuts and insert headers
    int groupSize = TheFitnessCalorieSortMgr->GetGroupSize();
    std::vector<NavListItemNode *>::iterator pBegin = nodes.begin();
    std::vector<NavListItemNode *>::iterator pEnd = nodes.end();
    while (pBegin != pEnd) {
        std::vector<NavListItemNode *>::iterator pNext;
        int remaining = pEnd - pBegin;
        if (remaining <= groupSize) {
            pNext = pEnd;
        } else {
            pNext = pBegin + groupSize;
        }

        NavListShortcutNode *shortcut = NewShortcutNode(*pBegin);
        mShortcutNodes.push_back(shortcut);
        shortcut->InsertHeaderRange(&*pBegin, &*pNext, this);
        pBegin = pNext;
    }

    // Finalize shortcuts
    FOREACH (it, mShortcutNodes) {
        (*it)->FinishSort(this);
    }
}
