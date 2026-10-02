#include "meta_ham\FitnessCalorieSortByCalorie.h"
#include "meta_ham\FitnessCalorieSortMgr.h"
#include "meta_ham\FitnessCalorieSortNode.h"
#include "meta_ham\NavListNode.h"
#include "world\CameraShot.h"
#include "utl\MakeString.h"
#include "utl\Symbol.h"

FitnessCalorieSortCmp::~FitnessCalorieSortCmp() {}
FitnessCalorieSortByCalorie::~FitnessCalorieSortByCalorie() {}

FitnessCalorieSortNode::FitnessCalorieSortNode(NavListItemSortCmp *cmp, int i)
    : NavListItemNode(cmp) {
    mCalories = i;
}

// Returns 0 for item nodes, 1 for all other node types
int FitnessCalorieSortCmp::Compare(
    NavListItemSortCmp const *cmp, NavListNodeType type
) const {
    int diff = type - kNodeItem;
    return diff ? 0 : -1;
}

NavListShortcutNode *
FitnessCalorieSortByCalorie::NewShortcutNode(NavListItemNode *node) const {
    CamShotFrame::BlendEaseMode calories =
        (CamShotFrame::BlendEaseMode)static_cast<FitnessCalorieSortNode *>(node)->GetCalories();
    Symbol token;
    {
        Symbol s(MakeString("calorie_shortcut_%i", calories));
        token = s;
    }
    FitnessCalorieSortCmp *cmp = new FitnessCalorieSortCmp();
    NavListShortcutNode *shortcut = new NavListShortcutNode(cmp, token, true);
    return shortcut;
}

NavListHeaderNode *
FitnessCalorieSortByCalorie::NewHeaderNode(NavListItemNode *node) const {
    CamShotFrame::BlendEaseMode calories =
        (CamShotFrame::BlendEaseMode)static_cast<FitnessCalorieSortNode *>(node)->GetCalories();
    Symbol token;
    {
        Symbol s(MakeString("calorie_header_%i", calories));
        token = s;
    }
    FitnessCalorieSortCmp *cmp = new FitnessCalorieSortCmp();
    FitnessCalorieHeaderNode *header = new FitnessCalorieHeaderNode(cmp, token, true);
    return header;
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

// w10-e: both New*Node closed 99.973 -> 100 by scoping `s` in its own block.
// The image's EH temp for `new NavList*Node` reuses `s`'s slot (stw r3, 0x54(r31))
// while `calories` keeps 0x50 -- i.e. `s` is dead (its block closed) and the
// calories slot is still in scope.  With `s` at function scope MSVC reused the
// calories slot instead.  `Symbol token;` costs nothing: its default store is
// dead and is dropped before `token = s`.
