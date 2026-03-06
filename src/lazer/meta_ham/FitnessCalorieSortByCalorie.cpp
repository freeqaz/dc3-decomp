#include "meta_ham/FitnessCalorieSortByCalorie.h"
#include "meta_ham/FitnessCalorieSortMgr.h"
#include "meta_ham/FitnessCalorieSortNode.h"
#include "meta_ham/NavListNode.h"
#include "utl/MakeString.h"
#include "utl/Symbol.h"

FitnessCalorieSortCmp::~FitnessCalorieSortCmp() {}
FitnessCalorieSortByCalorie::~FitnessCalorieSortByCalorie() {}

// Returns 0 for item nodes, 1 for all other node types
int FitnessCalorieSortCmp::Compare(
    NavListItemSortCmp const *cmp, NavListNodeType type
) const {
    return !(type == kNodeItem);
}

FitnessCalorieSortByCalorie::FitnessCalorieSortByCalorie() {
    static Symbol by_calorie("by_calorie");
    mSortName = by_calorie;
}

NavListShortcutNode *
FitnessCalorieSortByCalorie::NewShortcutNode(NavListItemNode *node) const {
    Symbol s = MakeString(
        "calorie_shortcut_%i", static_cast<FitnessCalorieSortNode *>(node)->GetCalories()
    );
    FitnessCalorieSortCmp *cmp = new FitnessCalorieSortCmp();
    return new NavListShortcutNode(cmp, s, true);
}

NavListHeaderNode *
FitnessCalorieSortByCalorie::NewHeaderNode(NavListItemNode *node) const {
    Symbol s = MakeString(
        "calorie_header_%i", static_cast<FitnessCalorieSortNode *>(node)->GetCalories()
    );
    FitnessCalorieSortCmp *cmp = new FitnessCalorieSortCmp();
    return new FitnessCalorieHeaderNode(cmp, s, true);
}

NavListHeaderNode *
FitnessCalorieSortByCalorie::NewHeaderNode(NavListItemNode *n1, NavListItemNode *n2) const {
    return NewHeaderNode(n1);
}

NavListItemNode *FitnessCalorieSortByCalorie::NewItemNode(void *p1) const {
    int *i = static_cast<int *>(p1);
    FitnessCalorieSortCmp *cmp = new FitnessCalorieSortCmp();
    return new FitnessCalorieSortNode(cmp, *i);
}
