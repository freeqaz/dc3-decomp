#include "SongSortByDiff.h"

#include "HamSongMgr.h"
#include "meta/SongMgr.h"
#include "SongRecord.h"
#include "meta/Sorting.h"

int DifficultyCmp::Compare(const NavListItemSortCmp *cmp, NavListNodeType type) const {
    switch (type) {
    case kNodeShortcut:
        return 0;

    case kNodeHeader: {
        const DifficultyCmp *diffCmp = cmp->GetDifficultyCmp();
        if (mTier == diffCmp->mTier)
            return 0;
        if (diffCmp->mTier == -1)
            return -1;
        if (mTier == -1)
            return 1;
        if (mTier < diffCmp->mTier)
            return -1;
        else
            return 1;
    }
    case kNodeItem: {
        const DifficultyCmp *diffCmp = cmp->GetDifficultyCmp();
        float other = diffCmp->mRank;
        float mine = mRank;
        if (mine == other)
            return AlphaKeyStrCmp(mName, diffCmp->mName, false);
        if (other == 0)
            return -1;
        if (mine == 0)
            return 1;
        if (mine < other)
            return -1;
        else
            return 1;
    }
    default:
        MILO_FAIL("invalid type of node comparison.\n");
    }
    return 0;
}

DifficultyCmp::~DifficultyCmp() {}

NavListShortcutNode *SongSortByDiff::NewShortcutNode(NavListItemNode *node) const {
    auto cmp = node->GetCmp()->GetDifficultyCmp();
    auto newCmp = new DifficultyCmp(cmp->mTier, 0, "");
    static Symbol no_part("no_part");
    Symbol tierToken = cmp->mTier != -1 ? TheHamSongMgr.RankTierToken(cmp->mTier) : no_part;
    return new NavListShortcutNode(newCmp, tierToken, true);
}

NavListHeaderNode *SongSortByDiff::NewHeaderNode(NavListItemNode *node) const {
    int tier = node->GetCmp()->GetDifficultyCmp()->mTier;
    DifficultyCmp *newCmp = new DifficultyCmp(tier, 0, "");
    static Symbol no_part("no_part");
    Symbol tierToken = tier != -1 ? TheHamSongMgr.RankTierToken(tier) : no_part;
    return new SongHeaderNode(newCmp, tierToken, true);
}
