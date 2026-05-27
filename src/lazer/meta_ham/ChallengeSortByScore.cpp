#include "ChallengeSortByScore.h"

#include "ChallengeRecord.h"
#include "ChallengeSortNode.h"
#include "Challenges.h"
#include "meta/Sorting.h"
#include "os/Debug.h"

int ChallengeScoreCmp::Compare(
    const NavListItemSortCmp *cmp, NavListNodeType type
) const {
    const ChallengeScoreCmp *mCmp;
    switch (type) {
    case kNodeShortcut:
        break;
    case kNodeHeader:
        mCmp = cmp->GetChallengeScoreCmp();
        if (mType == mCmp->mType) {
            return AlphaKeyStrCmp(mSongTitle, mCmp->mSongTitle, false);
        }
        if (mType < mCmp->mType) {
            return -1;
        }
        return 1;
        break;
    case kNodeItem:
        mCmp = cmp->GetChallengeScoreCmp();
        if (mScore != mCmp->mScore) {
            if (mScore <= mCmp->mScore) {
                return 1;
            }
            return -1;
        }
        break;
    default:
        MILO_FAIL("invalid type of node comparison.\n");
        return 0;
        break;
    }

    return 0;
}

NavListItemNode *ChallengeSortByScore::NewItemNode(void *p1) const {
    ChallengeRecord *record = static_cast<ChallengeRecord *>(p1);
    int score = record->GetChallengeRow().mScore;
    Symbol sym = record->GetSongTitle();
    int songID = record->GetChallengeRow().mSongID;

    int type = 2;
    if (songID == TheChallenges->GetGlobalChallengeSongID()) {
        type = 0;
    } else {
        songID = record->GetChallengeRow().mSongID;
        if (songID == TheChallenges->GetDlcChallengeSongID()) {
            type = 1;
        }
    }
    ChallengeScoreCmp *cmp = new ChallengeScoreCmp(type, score, sym.Str());
    return new ChallengeSortNode(cmp, record);
}

NavListShortcutNode *
ChallengeSortByScore::NewShortcutNode(NavListItemNode *item) const {
    static Symbol global_challenge("global_challenge");
    static Symbol dlc_challenge("dlc_challenge");

    int type = item->GetCmp()->GetChallengeScoreCmp()->mType;

    Symbol name;
    if (type == 0) {
        name = global_challenge;
    } else if (type == 1) {
        name = dlc_challenge;
    } else {
        name = item->GetToken();
    }

    ChallengeScoreCmp *newCmp =
        new ChallengeScoreCmp(type, 0, item->GetCmp()->GetChallengeScoreCmp()->mSongTitle);
    return new NavListShortcutNode(newCmp, name, true);
}

NavListHeaderNode *
ChallengeSortByScore::NewHeaderNode(NavListItemNode *item) const {
    static Symbol global_challenge("global_challenge");
    static Symbol dlc_challenge("dlc_challenge");

    int type = item->GetCmp()->GetChallengeScoreCmp()->mType;

    Symbol name;
    if (type == 0) {
        name = global_challenge;
    } else if (type == 1) {
        name = dlc_challenge;
    } else {
        auto _tmp0 = Symbol(static_cast<ChallengeSortNode *>(item)
                          ->GetChallengeRecord()
                          ->GetSongTitle()
                          .Str());
        name = _tmp0;
    }

    ChallengeScoreCmp *newCmp =
        new ChallengeScoreCmp(type, 0, item->GetCmp()->GetChallengeScoreCmp()->mSongTitle);
    return new ChallengeHeaderNode(newCmp, name, true);
}

NavListHeaderNode *
ChallengeSortByScore::NewHeaderNode(NavListItemNode *n1, NavListItemNode *n2) const {
    return NewHeaderNode(n1);
}