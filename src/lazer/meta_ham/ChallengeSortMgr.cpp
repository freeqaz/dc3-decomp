#include "ChallengeSortMgr.h"

#include "ChallengeSortByScore.h"
#include "ChallengeSortNode.h"
#include "NavListSortMgr.h"
#include "hamobj/HamGameData.h"
#include "game/GameMode.h"
#include "macros.h"
#include "meta_ham/NavListNode.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "ui/UIPanel.h"
#include "utl/Symbol.h"

ChallengeRecord::ChallengeRecord(const ChallengeRecord &other)
    : mRow(other.mRow), mSongShortName(other.mSongShortName), mSongTitle(other.mSongTitle),
      mChallengerGamertag(other.mChallengerGamertag), mMissionInfo(other.mMissionInfo),
      mSongContentLockState(other.mSongContentLockState) {}

ChallengeRecord &ChallengeRecord::operator=(const ChallengeRecord &other) {
    this->mRow = other.mRow;
    this->mSongShortName = other.mSongShortName;
    this->mSongTitle = other.mSongTitle;
    this->mChallengerGamertag = other.mChallengerGamertag;
    this->mMissionInfo = other.mMissionInfo;
    this->mSongContentLockState = other.mSongContentLockState;
    return *this;
}

BEGIN_CUSTOM_HANDLERS(ChallengeSortMgr)
    HANDLE_EXPR(get_target_challenge_score, GetTargetChallengeScore(_msg->Int(2)))
    HANDLE_EXPR(get_total_earned_xp, GetTotalXpEarned(_msg->Int(2)))
    HANDLE_EXPR(get_challenger_name, GetChallengerName())
    HANDLE_EXPR(get_song_id, GetSongID(_msg->Int(2)))
    HANDLE_EXPR(get_song_shortname, GetSongShortName(_msg->Int(2)))
    HANDLE_EXPR(get_song_title, GetSongTitle(_msg->Int(2)))
    HANDLE_EXPR(get_challenger_xp, GetChallengerXp(_msg->Int(2)))
    HANDLE_EXPR(get_challenge_record_song_type, GetChallengeRecordSongType(_msg->Int(2)))
    HANDLE_SUPERCLASS(NavListSortMgr)
END_CUSTOM_HANDLERS

ChallengeSortMgr::ChallengeSortMgr(SongPreview &preview) : NavListSortMgr(preview) {
    SetName("challenge_provider", ObjectDir::Main());
    mSorts.push_back(new ChallengeSortByScore());
}

ChallengeSortMgr::~ChallengeSortMgr() {}

void ChallengeSortMgr::Init(SongPreview &preview) {
    MILO_ASSERT(!TheChallengeSortMgr, 0x18);
    TheChallengeSortMgr = new ChallengeSortMgr(preview);
    TheContentMgr.RegisterCallback(TheChallengeSortMgr, false);
}

void ChallengeSortMgr::Terminate() {
    TheContentMgr.UnregisterCallback(TheChallengeSortMgr, false);
    MILO_ASSERT(TheChallengeSortMgr, 0x22);
    if (!TheChallengeSortMgr) {
        TheChallengeSortMgr = nullptr;
        return;
    }
    RELEASE(TheChallengeSortMgr);
}

int ChallengeSortMgr::GetTotalXpEarned(int i1) {
    NavListNode *highlight = GetHighlightItem()->Parent();
    NavListNode *header = dynamic_cast<ChallengeHeaderNode *>(highlight);
    MILO_ASSERT(header, 0xcc);
    return static_cast<ChallengeHeaderNode *>(header)->GetTotalEarnedExp(i1);
}

int ChallengeSortMgr::GetPotentialChallengeExp(int i1) {
    if (IsIndexHeader(i1)) {
        return static_cast<ChallengeHeaderNode *>(mSorts[mCurrentSortIdx]->GetList()[i1])
            ->GetChallengeExp();
    } else {
        auto highlight = GetHighlightItem();
        NavListNode *header = dynamic_cast<ChallengeHeaderNode *>(highlight->Parent());
        MILO_ASSERT(header, 0xa5);
        return static_cast<ChallengeHeaderNode *>(header)->GetPotentialChallengeExp(
            highlight
        );
    }
}

int ChallengeSortMgr::GetOwnerChallengeScore(int songID) {
    for (int i = 0; i < mChallengeRecords.size(); i++) {
        if (songID == mChallengeRecords[i].GetChallengeRow().mSongID
            && mChallengeRecords[i].GetChallengerGamertag() == mChallengeRecords[i].GetMissionInfo()) {
            return mChallengeRecords[i].GetChallengeRow().mScore;
        }
    }
    return 0;
}

int ChallengeSortMgr::GetChallengeExp(int i1) {
    if (IsIndexHeader(i1)) {
        return static_cast<ChallengeHeaderNode *>(mSorts[mCurrentSortIdx]->GetList()[i1])
            ->GetChallengeExp();
    } else
        return static_cast<ChallengeSortNode *>(mSorts[mCurrentSortIdx]->GetList()[i1])
            ->GetChallengeExp();
}

int ChallengeSortMgr::GetSongID(int i1) {
    if (IsIndexHeader(i1)) {
        return static_cast<ChallengeHeaderNode *>(mSorts[mCurrentSortIdx]->GetList()[i1])
            ->GetSongID();
    } else
        return static_cast<ChallengeSortNode *>(mSorts[mCurrentSortIdx]->GetList()[i1])
            ->GetSongID();
}

Symbol ChallengeSortMgr::GetSongShortName(int songID) {
    if (IsIndexHeader(songID)) {
        return static_cast<ChallengeHeaderNode *>(
                   mSorts[mCurrentSortIdx]->GetList()[songID]
        )
            ->GetSongShortName();
    } else {
        return mSorts[mCurrentSortIdx]->GetList()[songID]->GetToken();
    }
}

int ChallengeSortMgr::GetOwnerChallengeTimeStamp(int i1) {
    for (int i = 0; i < mChallengeRecords.size(); i++) {
        if (i1 == mChallengeRecords[i].GetChallengeRow().mSongID
            && mChallengeRecords[i].GetChallengerGamertag() == mChallengeRecords[i].GetMissionInfo()) {
            return mChallengeRecords[i].GetChallengeRow().mTimeStamp;
        }
    }
    return 0;
}

int ChallengeSortMgr::GetChallengeScore(int i1) {
    if (IsIndexHeader(i1)) {
        return GetBestChallengeScore(GetSongID(i1));
    } else {
        return static_cast<ChallengeSortNode *>(mSorts[mCurrentSortIdx]->GetList()[i1])
            ->GetChallengeScore();
    }
}

Symbol ChallengeSortMgr::GetChallengerName() {
    auto node = dynamic_cast<ChallengeSortNode *>(GetHighlightItem());
    MILO_ASSERT(node, 0xdb);
    return node->GetChallengeRecord()->GetMissionInfo();
}

int ChallengeSortMgr::GetBestChallengeScore(int songID) {
    int currentHighest = 0;
    for (int i = 0; i < mChallengeRecords.size(); i++) {
        int score = mChallengeRecords[i].GetChallengeRow().mScore;
        if (songID == mChallengeRecords[i].GetChallengeRow().mSongID
            && currentHighest < score) {
            currentHighest = score;
        }
    }
    return currentHighest;
}

String ChallengeSortMgr::GetSongTitle(int songID) {
    if (IsIndexHeader(songID)) {
        return static_cast<ChallengeHeaderNode *>(
                   mSorts[mCurrentSortIdx]->GetList()[songID]
        )
            ->GetSongShortTitle();
        ;
    } else {
        return static_cast<ChallengeSortNode *>(mSorts[mCurrentSortIdx]->GetList()[songID])
            ->GetChallengeRecord()
            ->GetSongTitle(); // FIXME
    }
}

int ChallengeSortMgr::GetChallengeRecordSongType(int i1) {
    if (IsIndexHeader(i1)) {
        return -1;
    } else {
        return static_cast<ChallengeSortNode *>(mSorts[mCurrentSortIdx]->GetList()[i1])
            ->GetChallengeRecord()
            ->GetSongContentLockState();
    }
}

Symbol ChallengeSortMgr::MoveOn() {
    static Symbol song_select_quickplay("song_select_quickplay");
    Symbol songSel = TheGameMode->Property("song_select_mode", true)->Sym();
    if (song_select_quickplay == songSel) {
        static Symbol move_on_quickplay("move_on_quickplay");
        UIPanel *challengeFeedPanel =
            ObjectDir::Main()->Find<UIPanel>("challenge_feed_panel");
        static Message move_on_quickplay_msg("move_on_quickplay");
        challengeFeedPanel->HandleType(move_on_quickplay_msg);
    }
    return gNullStr;
}

bool ChallengeSortMgr::SelectionIs(Symbol selection) {
    static Symbol challenge("challenge");
    static Symbol header("header");
    if (selection == challenge) {
        return dynamic_cast<ChallengeSortNode *>(GetHighlightItem()) != nullptr;
    } else if (selection == header) {
        return dynamic_cast<ChallengeHeaderNode *>(GetHighlightItem()) != nullptr;
    }
    return false;
}

int ChallengeSortMgr::GetTargetChallengeScore(int i) { return 1000; }

const char *ChallengeSortMgr::GetBestChallengeScoreGamertag(int songID) {
    int bestScore = -1;
    int bestIndex = -1;
    unsigned int recordCount = mChallengeRecords.size();
    unsigned int j = 0;
    if (recordCount != 0) {
        do {
            ChallengeRecord &record = mChallengeRecords[j];
            int score = record.GetChallengeRow().mScore;
            if (songID == record.GetChallengeRow().mSongID && bestScore < score) {
                bestScore = score;
                bestIndex = j;
            }
            j++;
        } while (j < recordCount);
        if (bestIndex != -1) {
            int type = mChallengeRecords[bestIndex].GetChallengeRow().mType;
            bool inRange = (type >= 0 && type <= 2);
            if (!inRange) {
                inRange = (type >= 3 && type <= 5);
                if (!inRange) {
                    return mChallengeRecords[bestIndex].GetChallengerGamertag().Str();
                }
            }
            return "HARMONIX";
        }
    }
    return gNullStr;
}

int ChallengeSortMgr::GetChallengerXp(int i) {
    if (IsIndexHeader(i)) {
        int songID = GetSongID(i);
        int highestScore = 0;
        unsigned int recordCount = mChallengeRecords.size();
        int result = 0;
        unsigned int j = 0;
        if (recordCount != 0) {
            do {
                ChallengeRecord &record = mChallengeRecords[j];
                int score = record.GetChallengeRow().mScore;
                if (songID == record.GetChallengeRow().mSongID && highestScore < score) {
                    result = record.GetChallengeRow().mChallengerXp;
                    highestScore = score;
                }
                j++;
            } while (j < recordCount);
        }
        return result;
    } else {
        ChallengeSortNode* node = static_cast<ChallengeSortNode*>(mSorts[mCurrentSortIdx]->GetList()[i]);
        return node->GetChallengerXp();
    }
}

const char *ChallengeSortMgr::GetChallengerGamertag(int i) {
    if (IsIndexHeader(i)) {
        int songID = GetSongID(i);
        return GetBestChallengeScoreGamertag(songID);
    } else {
        NavListNode* listNode = mSorts[mCurrentSortIdx]->GetList()[i];
        ChallengeSortNode* node = static_cast<ChallengeSortNode*>(listNode);
        return node->GetChallengerGamertag();
    }
}

void ChallengeSortMgr::OnEnter() {}
