#include "meta_ham\ChallengeResultPanel.h"
#include "HamPanel.h"
#include "flow\Flow.h"
#include "flow\PropertyEventProvider.h"
#include "hamobj\HamGameData.h"
#include "hamobj\HamNavList.h"
#include "hamobj\HamPlayerData.h"
#include "meta_ham\AppLabel.h"
#include "meta_ham\Challenges.h"
#include "obj\Object.h"
#include "os\Debug.h"
#include "ui\UIComponent.h"
#include "ui\UIList.h"
#include "ui\UIListLabel.h"
#include "ui\UIPanel.h"
#include "utl\Locale.h"
#include "utl\Symbol.h"

ChallengeResultPanel::ChallengeResultPanel()
    : mChallengeList(0), mPhase(0), mPlayerScore(0), mRivalIndex(0), mHalfDisplayCount(0), mPlayerIndex(0) {}

ChallengeResultPanel::~ChallengeResultPanel() {}

BEGIN_HANDLERS(ChallengeResultPanel)
    HANDLE_ACTION(update_list, UpdateList(_msg->Int(2)))
    HANDLE_MESSAGE(UIComponentScrollMsg)
    HANDLE_SUPERCLASS(HamPanel)
END_HANDLERS

BEGIN_PROPSYNCS(ChallengeResultPanel)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void ChallengeResultPanel::Text(int, int data, UIListLabel *slot, UILabel *label) const {
    MILO_ASSERT_RANGE(data, 0, mItems.size(), 0x11a);
    static Symbol best_score("best_score");
    AppLabel *app_label = dynamic_cast<AppLabel *>(label);
    MILO_ASSERT(app_label, 0x11E);
    if (mItems[data].mGamertag == gNullStr) {
        label->SetTextToken(gNullStr);
        return;
    } else {
        String curGamerTag = mItems[data].mGamertag;
        if (slot->Matches("white_small_gamertag")) {
            if (mPlayerScore <= mItems[data].mScore && data != mRivalIndex && data != mPlayerIndex) {
                label->SetPrelocalizedString(curGamerTag);
            } else {
                label->SetTextToken(gNullStr);
            }
        } else if (slot->Matches("grey_small_gamertag")) {
            if (mPlayerScore > mItems[data].mScore && data != mRivalIndex) {
                label->SetPrelocalizedString(curGamerTag);
            } else {
                label->SetTextToken(gNullStr);
            }
        } else if (slot->Matches("white_large_gamertag")) {
            if (mPlayerScore <= mItems[data].mScore && data == mRivalIndex) {
                label->SetPrelocalizedString(curGamerTag);
            } else {
                label->SetTextToken(gNullStr);
            }
        } else if (slot->Matches("grey_large_gamertag")) {
            if (mPlayerScore > mItems[data].mScore && data == mRivalIndex) {
                label->SetPrelocalizedString(curGamerTag);
            } else {
                label->SetTextToken(gNullStr);
            }
        } else if (slot->Matches("gold_large_gamertag")) {
            if (mPlayerScore == mItems[data].mScore && data == mPlayerIndex) {
                label->SetPrelocalizedString(curGamerTag);
            } else {
                label->SetTextToken(gNullStr);
            }
        } else if (slot->Matches("white_small_score")) {
            if (mPlayerScore <= mItems[data].mScore && data != mRivalIndex && data != mPlayerIndex) {
                app_label->SetTokenFmt(
                    best_score, LocalizeSeparatedInt(mItems[data].mScore, TheLocale)
                );
            } else {
                label->SetTextToken(gNullStr);
            }
        } else if (slot->Matches("grey_small_score")) {
            if (mPlayerScore > mItems[data].mScore && data != mRivalIndex) {
                app_label->SetTokenFmt(
                    best_score, LocalizeSeparatedInt(mItems[data].mScore, TheLocale)
                );
            } else {
                label->SetTextToken(gNullStr);
            }
        } else if (slot->Matches("white_large_score")) {
            if (mPlayerScore <= mItems[data].mScore && data == mRivalIndex) {
                app_label->SetTokenFmt(
                    best_score, LocalizeSeparatedInt(mItems[data].mScore, TheLocale)
                );
            } else {
                label->SetTextToken(gNullStr);
            }
        } else if (slot->Matches("grey_large_score")) {
            if (mPlayerScore > mItems[data].mScore && data == mRivalIndex) {
                app_label->SetTokenFmt(
                    best_score, LocalizeSeparatedInt(mItems[data].mScore, TheLocale)
                );
            } else {
                label->SetTextToken(gNullStr);
            }
        } else if (slot->Matches("gold_large_score")) {
            if (mPlayerScore == mItems[data].mScore && data == mPlayerIndex) {
                app_label->SetTokenFmt(
                    best_score, LocalizeSeparatedInt(mItems[data].mScore, TheLocale)
                );
            } else {
                label->SetTextToken(gNullStr);
            }
        }
    }
}

int ChallengeResultPanel::NumData() const { return mItems.size(); }

DataNode ChallengeResultPanel::OnMsg(const UIComponentScrollMsg &msg) {
    UIComponent *comp = msg.mData->Obj<UIComponent>(2);
    if (comp == mChallengeList
        && mRivalIndex - mChallengeList->FirstShowing() == mHalfDisplayCount) {
        mChallengeList->StopAutoScroll();
        mPhase = 2;
        DataDir()->Find<Flow>("rival_result.flow")->Activate();
    }
    return DataNode(1);
}

void ChallengeResultPanel::FinishLoad() {
    UIPanel::FinishLoad();
    mChallengeList = DataDir()->Find<UIList>("challengee.lst");
    mRightHandNavList = DataDir()->Find<HamNavList>("right_hand.hnl");
    mResultEventProvider = DataDir()->Find<PropertyEventProvider>("result.ep");
}

void ChallengeResultPanel::Poll() {
    HamPanel::Poll();
    switch (mPhase) {
    case 0:
        if (!DataDir()->Find<Flow>("result_init.flow")->IsRunning()) {
            DataDir()->Find<Flow>("score.flow")->Activate();
            mPhase = 1;
            mChallengeList->AutoScroll();
        }
        break;
    case 2:
        if (!DataDir()->Find<Flow>("rival_result.flow")->IsRunning()) {
            mPhase = 3;
            mChallengeList->AutoScroll();
        }
        break;
    case 3:
        if (!mChallengeList->IsScrolling()) {
            mChallengeList->StopAutoScroll();
            mPhase = 4;
            DataDir()->Find<Flow>("final_result.flow")->Activate();
        }
        break;
    case 4:
        if (!DataDir()->Find<Flow>("final_result.flow")->IsRunning()) {
            mPhase = 5;
            mRightHandNavList->Enable();
            mRightHandNavList->SetShowing(true);
        }
        break;
    default:
        break;
    }
}

void ChallengeResultPanel::UpdateList(int player) {
    static Symbol score("score");
    static Symbol challenge_mission_index("challenge_mission_index");
    static Symbol side("side");
    static Symbol scroll_past_max_display("scroll_past_max_display");
    static Symbol max_display("max_display");
    static Symbol rival_beaten("rival_beaten");
    static Symbol grade("grade");
    static Symbol player_name("player_name");
    static Symbol challenge_mission_score("challenge_mission_score");
    static Symbol xp_before_mission("xp_before_mission");
    static Symbol xp_mission("xp_mission");
    static Symbol xp_total("xp_total");
    static Symbol is_challenging_self("is_challenging_self");
    static Symbol rival_is_self("rival_is_self");
    bool d16 = 0;
    String playerName;
    int numDisplay = mChallengeList->NumDisplay();
    int d15 = 0;
    int d14 = 0;
    int totalXP = TheChallenges->GetTotalXpEarned(player);
    HamPlayerData *playerData = TheGameData->Player(player);
    MILO_ASSERT(playerData, 0x7D);
    PropertyEventProvider *provider = playerData->Provider();
    MILO_ASSERT(provider, 0x7F);
    mPlayerScore = provider->Property(score)->Int();
    mRivalIndex = provider->Property(challenge_mission_index)->Int() + numDisplay;
    mSide = (SkeletonSide)provider->Property(side)->Int();
    playerName = provider->Property(player_name)->Str();
    int challengeScore = provider->Property(challenge_mission_score)->Int();
    bool challengeSelf = provider->Property(is_challenging_self)->Int();
    mHalfDisplayCount = (numDisplay / 2) + 1;
    if (mPlayerScore <= challengeScore) {
        mRivalIndex++;
    }
    mItems.clear();
    for (int i = 0; i < mChallengeList->NumDisplay(); i++) {
        mItems.push_back(ChallengeRow());
    }
    bool b3 = false;
    ChallengeRow row;
    row.mScore = mPlayerScore;
    row.mGamertag = playerName;
    row.mNotes = playerName;
    auto &challenges = TheChallenges->GetPlayerChallenges(player);
    int numPlayerChallenges = challenges.size();
    for (int i = 0; i < numPlayerChallenges; i++) {
        if (mPlayerScore <= challenges[i].mScore && !b3) {
            b3 = true;
            mPlayerIndex = mItems.size();
            mItems.push_back(row);
        }
        mItems.push_back(challenges[i]);
    }
    if (!b3) {
        mPlayerIndex = mItems.size();
        mItems.push_back(row);
    }
    int i8 = 0;
    int d20;
    for (int i = numDisplay; i < mItems.size(); i++) {
        if (mPlayerScore > mItems[i].mScore) {
            if (i < mRivalIndex) {
                d15 += TheChallenges->CalculateChallengeXp(
                    mItems[i].mScore, mItems[i].mDiff
                );
            } else if (i == mRivalIndex) {
                d14 = d15
                    + TheChallenges->CalculateChallengeXp(
                        mItems[i].mScore, mItems[i].mDiff
                    );
                d16 = 1;
            }
            i8++;
        }
    }
    if (i8 == 0) {
        d20 = 0;
    } else if (i8 == mItems.size() - numDisplay - 1) {
        d20 = 4;
    } else if (d16) {
        if (i8 > mRivalIndex + 1) {
            d20 = 3;
        } else {
            d20 = 2;
        }
    } else {
        d20 = 1;
    }
    mChallengeList->SetProperty(max_display, 0);
    mChallengeList->SetProperty(scroll_past_max_display, 1);
    mChallengeList->StopAutoScroll();
    mChallengeList->SetProvider(this);
    mRightHandNavList->Disable();
    mRightHandNavList->SetShowing(false);
    mResultEventProvider->SetProperty(rival_beaten, d16);
    mResultEventProvider->SetProperty(grade, d20);
    mResultEventProvider->SetProperty(side, mSide);
    mResultEventProvider->SetProperty(xp_before_mission, d15);
    mResultEventProvider->SetProperty(xp_mission, d14);
    mResultEventProvider->SetProperty(xp_total, totalXP);
    mResultEventProvider->SetProperty(rival_is_self, challengeSelf);
    mPhase = 0;
    DataDir()->Find<Flow>("result_init.flow")->Activate();
}
