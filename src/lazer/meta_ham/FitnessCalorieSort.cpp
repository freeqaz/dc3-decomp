#include "meta_ham/FitnessCalorieSort.h"
#include "FitnessCalorieSortMgr.h"
#include "NavListSort.h"
#include "meta_ham/AppLabel.h"
#include "meta_ham/ChallengeSort.h"
#include "meta_ham/NavListNode.h"
#include "os/Debug.h"
#include "rndobj/Mesh.h"
#include "ui/UILabel.h"
#include "ui/UIListLabel.h"
#include "utl/Symbol.h"

FitnessCalorieSort::FitnessCalorieSort() {}

void FitnessCalorieSort::Text(
    int, int idx, UIListLabel *uiListLabel, UILabel *uiLabel
) const {
    AppLabel *app_label = dynamic_cast<AppLabel *>(uiLabel);
    MILO_ASSERT(app_label, 0xa4);
    app_label->SetFromGeneralSelectNode(unk30[idx]);
}
void FitnessCalorieSort::DeleteItemList() {
    NavListSort::DeleteItemList();
    TheFitnessCalorieSortMgr->ClearHeaders();
}

void FitnessCalorieSort::SetHighlightedIx(int idx) {
    unk54 = unk50;
    if (0 <= idx) {
        if (mList.size() >= idx) {
            if (mList.size() == 0) {
                return;
            }
            unk50 = mList[idx];
            TheFitnessCalorieSortMgr->OnHighlightChanged();
            return;
        }
    }
    unk50 = 0;
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
    NavListSortNode *tempNode = unk50;
    unk50 = nullptr;
    unk54 = tempNode;
    if (node) {
        if (node->GetType() == 5 || node->GetType() == 4) {
            auto find = std::find_if(mList.begin(), mList.end(), SortNodeFind(node));
            if (find != mList.end()) {
                unk50 = *find;
                TheFitnessCalorieSortMgr->OnHighlightChanged();
            }
        }
    }
}

void FitnessCalorieSort::BuildItemList() {
    Symbol sym(gNullStr);
    auto sortNode = unk50;
    if (sortNode && sortNode->GetType() == 5) {
        sym = sortNode->GetToken();
    }
    DeleteItemList();
    FOREACH (it, unk3c) {
        (*it)->Renumber(mList);
    }
    FOREACH (it, unk30) {
        (*it)->Renumber(mList);
    }
    FOREACH (it, unk30) {
        (*it)->FinishBuildList(this);
    }
    if (!sym.Null()) {
        unk50 = GetNode(sym);
    }
    TheFitnessCalorieSortMgr->FinalizeHeaders();
}

void FitnessCalorieSort::BuildTree() {
    NavListSort::DeleteTree();
    Init();
    std::vector<NavListItemNode *> nodes;

    // Populate nodes from TheFitnessCalorieSortMgr calories
    std::vector<int> &calories = TheFitnessCalorieSortMgr->GetUnk78();
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
        unk30.push_back(shortcut);
        shortcut->InsertHeaderRange(&*pBegin, &*pNext, this);
        pBegin = pNext;
    }

    // Finalize shortcuts
    FOREACH (it, unk30) {
        (*it)->FinishSort(this);
    }
}
