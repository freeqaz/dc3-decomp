#include "meta_ham\FitnessCalorieSort.h"
#include "FitnessCalorieSortMgr.h"
#include "NavListSort.h"
#include "meta_ham\AppLabel.h"
#include "meta_ham\ChallengeSort.h"
#include "meta_ham\NavListNode.h"
#include "os\Debug.h"
#include "rndobj\Mesh.h"
#include "stl\_algo.h"
#include "stl\_vector.h"
#include "ui\UILabel.h"
#include "ui\UIListLabel.h"
#include "utl\Std.h"
#include "utl\Symbol.h"

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

    std::vector<int> &values = TheFitnessCalorieSortMgr->GetCalorieValues();
    for (int i = 0; i < values.size(); i++) {
        nodes.push_back(NewItemNode(&values[i]));
    }

    int groupSize = TheFitnessCalorieSortMgr->GetGroupSize();
    auto begin = nodes.begin();
    auto end = nodes.end();
    while (begin != end) {
        std::vector<NavListItemNode *>::iterator it;
        // if not enough nodes to make a set of size "groupsize", go to end
        if (end - begin <= groupSize) {
            it = end;
            // else move forward of size "groupsize" nodes
        } else {
            it = begin + groupSize;
        }
        NavListShortcutNode *shortcutNode = NewShortcutNode(*begin);
        mShortcutNodes.push_back(shortcutNode);
        shortcutNode->InsertHeaderRange(begin, it, this);
        begin = it;
    }

    FOREACH (it, mShortcutNodes) {
        (*it)->FinishSort(this);
    }
}
