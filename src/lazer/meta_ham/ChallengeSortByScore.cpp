#include "ChallengeSortByScore.h"

#include "ChallengeSortNode.h"

ChallengeSortNode::ChallengeSortNode(NavListItemSortCmp *cmp, ChallengeRecord *record) : NavListItemNode(cmp), mChallengeRecord(record){  }

NavListHeaderNode *
ChallengeSortByScore::NewHeaderNode(NavListItemNode *n1, NavListItemNode *n2) const {
    return NewHeaderNode(n1);
}