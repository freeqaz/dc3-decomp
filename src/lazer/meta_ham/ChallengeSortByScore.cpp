#include "ChallengeSortByScore.h"

#include "ChallengeRecord.h"
#include "ChallengeSortNode.h"
#include "Challenges.h"
#include "meta/Sorting.h"
#include "os/Debug.h"

int ChallengeScoreCmp::Compare(
    const NavListItemSortCmp *other, NavListNodeType nodeType
) const {
    switch (nodeType) {
    case kNodeShortcut:
        break;
    case kNodeHeader: {
        const ChallengeScoreCmp *otherCmp = other->GetChallengeScoreCmp();
        if (mType == otherCmp->mType) {
            return AlphaKeyStrCmp(
                mSongTitle, otherCmp->mSongTitle, false
            );
        }
        if (otherCmp->mType <= mType) {
            return 1;
        }
        return -1;
    }
    case kNodeItem: {
        const ChallengeScoreCmp *otherCmp = other->GetChallengeScoreCmp();
        if (mScore != otherCmp->mScore) {
            if (mScore <= otherCmp->mScore) {
                return 1;
            }
            return -1;
        }
        break;
    }
    default:
        MILO_FAIL("invalid type of node comparison.\n");
        break;
    }
    return 0;
}

NavListItemNode *ChallengeSortByScore::NewItemNode(void *data) const {
    ChallengeRecord *record = (ChallengeRecord *)data;
    int songID = record->GetChallengeRow().mSongID;
    int score = record->GetChallengeRow().mScore;
    Symbol songTitle = record->GetSongTitle();

    int type = 2;
    if (songID == TheChallenges->GetGlobalChallengeSongID()) {
        type = 0;
    } else if (songID == TheChallenges->GetDlcChallengeSongID()) {
        type = 1;
    }

    ChallengeScoreCmp *cmp = new ChallengeScoreCmp(type, score, songTitle.Str());
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
        name = Symbol(static_cast<ChallengeSortNode *>(item)
                          ->GetChallengeRecord()
                          ->GetSongTitle()
                          .Str());
    }

    ChallengeScoreCmp *newCmp =
        new ChallengeScoreCmp(type, 0, item->GetCmp()->GetChallengeScoreCmp()->mSongTitle);
    return new ChallengeHeaderNode(newCmp, name, true);
}

NavListHeaderNode *
ChallengeSortByScore::NewHeaderNode(NavListItemNode *n1, NavListItemNode *n2) const {
    return NewHeaderNode(n1);
}