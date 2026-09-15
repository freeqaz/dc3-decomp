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
    Symbol s(MakeString("calorie_shortcut_%i", calories));
    Symbol token = s;
    FitnessCalorieSortCmp *cmp = new FitnessCalorieSortCmp();
    NavListShortcutNode *shortcut = new NavListShortcutNode(cmp, token, true);
    return shortcut;
}

NavListHeaderNode *
FitnessCalorieSortByCalorie::NewHeaderNode(NavListItemNode *node) const {
    CamShotFrame::BlendEaseMode calories =
        (CamShotFrame::BlendEaseMode)static_cast<FitnessCalorieSortNode *>(node)->GetCalories();
    Symbol s(MakeString("calorie_header_%i", calories));
    Symbol token = s;
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

// w8-j 2026-09-15 -- FLOOR at 99.973% for BOTH
// ?NewShortcutNode@FitnessCalorieSortByCalorie@@UBAPAVNavListShortcutNode@@...
// and ?NewHeaderNode@...@@UBAPAVNavListHeaderNode@@... (148 B each, 36 of 37
// instructions equal).  One row each, and it is the same row:
//     target   stw r3, 0x54(r31)        ours   stw r3, 0x50(r31)
// run_diff_inspect mode=stack-layout shows the frame sizes match exactly
// (0x80 both sides, 3 callee-saved GPRs both sides) and that the two 4-byte
// slots simply hold SWAPPED variables: the image keeps an address at 0x50 and
// the int at 0x54, we keep `calories` (int) at 0x50 and `s` (Symbol) at 0x54.
// No instruction is inserted or deleted, so nothing is missing from the source.
// The declaration order that would swap them is not reachable: `s` is
// initialised from MakeString(..., calories), so `calories` must be declared
// first.  These two rows already carry 10 recorded attempts apiece; recording
// the slot-swap diagnosis rather than spending an eleventh.
