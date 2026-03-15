#include "meta_ham/AppMiniLeaderboardDisplay.h"
#include "flow/Flow.h"
#include "hamobj/HamList.h"
#include "hamobj/MiniLeaderboardDisplay.h"
#include "meta/SongMgr.h"
#include "meta_ham/HamProfile.h"
#include "meta_ham/ProfileMgr.h"
#include "meta_ham/SongStatusMgr.h"
#include "net/DingoSvr.h"
#include "net_ham/LeaderboardJobs.h"
#include "net_ham/RCJobDingo.h"
#include "net_ham/RockCentral.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os/Debug.h"
#include "os/PlatformMgr.h"
#include "rndobj/Dir.h"
#include "ui/UIComponent.h"
#include "ui/UILabel.h"
#include "ui/UIList.h"
#include "ui/UIListLabel.h"
#include "ui/UIListSlot.h"
#include "ui/UIListWidget.h"
#include "utl/Symbol.h"

AppMiniLeaderboardDisplay::AppMiniLeaderboardDisplay()
    : mState(0), mLeaderboardList(0), mSongID(0), mLoadTime(0) {}

AppMiniLeaderboardDisplay::~AppMiniLeaderboardDisplay() {
    TheRockCentral.CancelOutstandingCalls(this);
}

BEGIN_HANDLERS(AppMiniLeaderboardDisplay)
    HANDLE_EXPR(update_leaderboard, UpdateLeaderboard(_msg->Sym(2)))
    HANDLE_MESSAGE(RCJobCompleteMsg)
    HANDLE_MESSAGE(ServerStatusChangedMsg)
    HANDLE_SUPERCLASS(MiniLeaderboardDisplay)
END_HANDLERS

void AppMiniLeaderboardDisplay::Poll() {
    UIComponent::Poll();
    if (mSongID != 0) {
        if (mState == 0) {
            float uiSeconds = TheTaskMgr.UISeconds();
            if (mLoadTime > uiSeconds)
                mLoadTime = uiSeconds;
            if (1.0f <= uiSeconds - mLoadTime) {
                UpdateLeaderboardOnline(mSongID);
            }
        } else if (mState == 5 && ThePlatformMgr.IsConnected()) {
            Symbol name = TheSongMgr.GetShortNameFromSongID(mSongID);
            UpdateLeaderboard(name);
        }
    }
}

void AppMiniLeaderboardDisplay::Enter() {
    UIComponent::Enter();
    TheServer.AddSink(this);
    if (mState != 0) {
        mState = 0;
        Flow *f = mResourceDir->Find<Flow>("pending.flow");
        f->Activate();
    }
}

void AppMiniLeaderboardDisplay::Exit() {
    UIComponent::Exit();
    TheServer.RemoveSink(this);
    TheRockCentral.CancelOutstandingCalls(this);
    mSongID = 0;
    mLoadTime = 0;
}

void AppMiniLeaderboardDisplay::DrawShowing() {
    MILO_ASSERT(mResourceDir, 0x5C);
    mResourceDir->SetWorldXfm(WorldXfm());
    mResourceDir->Draw();
}

int AppMiniLeaderboardDisplay::NumData() const { return mLBRows.size(); }

UIListWidgetState
AppMiniLeaderboardDisplay::ElementStateOverride(int, int data, UIListWidgetState s) const {
    HamProfile *profile = TheProfileMgr.GetActiveProfile(true);
    if (profile) {
        profile->UpdateOnlineID();
        bool bHasOnlineID = profile->IsSignedIn();
        MILO_ASSERT(bHasOnlineID, 500);
        XUID xuid = profile->GetOnlineID()->GetXUID();
        if (mLBRows[data].mXUID == xuid) {
            return kUIListWidgetHighlight;
        }
    }
    return kUIListWidgetActive;
}

void AppMiniLeaderboardDisplay::UpdateData(GetMiniLeaderboardJob *job) {
    mLBRows.clear();
    job->GetRows(&mLBRows);
    UpdateSelfInRows();
    MILO_ASSERT(mLeaderboardList, 0x15f);
    mLeaderboardList->Refresh(false);
}

void AppMiniLeaderboardDisplay::ClearData() {
    mLBRows.clear();
    MILO_ASSERT(mLeaderboardList, 0xfa);
    mLeaderboardList->Refresh(false);
}

DataNode AppMiniLeaderboardDisplay::OnMsg(const ServerStatusChangedMsg &) {
    if (mSongID != 0) {
        Symbol name = TheSongMgr.GetShortNameFromSongID(mSongID);
        UpdateLeaderboard(name);
    }
    return DATA_UNHANDLED;
}

DataNode AppMiniLeaderboardDisplay::OnMsg(const RCJobCompleteMsg &msg) {
    if (msg.Success()) {
        GetMiniLeaderboardJob *job = dynamic_cast<GetMiniLeaderboardJob *>(msg.Job());
        if (job && job->SongID() == mSongID) {
            UpdateData(job);
        }
        if (mState != 1) {
            mState = 1;
            mResourceDir->Find<Flow>("ready.flow")->Activate();
        }
    } else if (mState != 3) {
        mState = 3;
        mResourceDir->Find<Flow>("connection_error.flow")->Activate();
    }
    return 1;
}

void AppMiniLeaderboardDisplay::Update() {
    MiniLeaderboardDisplay::Update();
    MILO_ASSERT(mResourceDir, 0x16a);
    static Symbol leaderboard("leaderboard");
    HamList *pLeaderboardList = mResourceDir->Find<HamList>("leaderboard.lst");
    mLeaderboardList = pLeaderboardList;
    mLeaderboardList->SetProvider(this);
}

void AppMiniLeaderboardDisplay::UpdateLeaderboardOnline(int i1) {
    HamProfile *profile = TheProfileMgr.GetActiveProfile(true);
    if (profile && profile->IsSignedIn() && ThePlatformMgr.IsConnected()) {
        TheRockCentral.ManageJob(new GetMiniLeaderboardJob(this, profile, i1));
        if (mState != 2) {
            mState = 2;
            mResourceDir->Find<Flow>("pending.flow")->Activate();
        }
    } else {
        int newState;
        if (!ThePlatformMgr.IsConnected()) {
            if (mState == 5) return;
            newState = 5;
        } else {
            if (mState == 4) return;
            newState = 4;
        }
        mState = newState;
        mResourceDir->Find<Flow>("no_profile.flow")->Activate();
    }
}

bool AppMiniLeaderboardDisplay::UpdateLeaderboard(Symbol s) { // has one small discrepancy
    if (!TheProfileMgr.HasActiveProfile(true)) {
        if (mState != 4) {
            mState = 4;
            Flow *f = mResourceDir->Find<Flow>("no_profile.flow", true);
            f->Activate();
            return true;
        }
    } else {
        HamProfile *profile = TheProfileMgr.GetActiveProfile(true);
        MILO_ASSERT(profile, 0xb1);
        profile->UpdateOnlineID();
        if (profile->IsSignedIn()) {
            if (!ThePlatformMgr.IsConnected()) { // mismatch right here?
                if (mState != 5) {
                    mState = 5;
                    Flow *f = mResourceDir->Find<Flow>("no_profile.flow", true);
                    f->Activate();
                    return true;
                }
            } else {
                mSongID = TheSongMgr.GetSongIDFromShortName(s, false);
                ClearData();
                TheRockCentral.CancelOutstandingCalls(this);
                if (mSongID == 0) {
                    return true;
                }
                if (mState != 0) {
                    mState = 0;
                    Flow *f = mResourceDir->Find<Flow>("pending.flow", true);
                    f->Activate();
                }
                mLoadTime = TheTaskMgr.UISeconds();
            }
        }
    }
    return true;
}

void AppMiniLeaderboardDisplay::Text(int, int data, UIListLabel *slot, UILabel *label) const {
    String selfName(gNullStr);
    if (data >= NumData()) {
        label->SetTextToken(gNullStr);
    } else {
        HamProfile *profile = TheProfileMgr.GetActiveProfile(true);
        if (profile) {
            selfName = profile->GetName();
        }
        if (slot->Matches("gamertag")) {
            static Symbol gamertag("gamertag");
            if (selfName == mLBRows[data].mName) {
                label->SetTextToken(gNullStr);
            } else {
                label->SetTokenFmt(gamertag, mLBRows[data].mName);
            }
        } else if (slot->Matches("score")) {
            label->SetInt(mLBRows[data].mScore, false);
        } else if (slot->Matches("no_flashcards")) {
            static Symbol no_flashcards_icon("no_flashcards_icon");
            if (mLBRows[data].mNoFlashcards) {
                label->SetTextToken(no_flashcards_icon);
            } else {
                label->SetTextToken(gNullStr);
            }
        } else if (slot->Matches("rank")) {
            static Symbol rank_fmt("rank_fmt");
            label->SetInt(mLBRows[data].mModeID, false);
        } else if (slot->Matches("difficulty")) {
            static Symbol beginner_short("beginner_short");
            static Symbol easy_short("easy_short");
            static Symbol medium_short("medium_short");
            static Symbol expert_short("expert_short");
            Difficulty d = mLBRows[data].mDiffID;
            switch (d) {
            case kDifficultyEasy:
                label->SetTextToken(easy_short);
                break;
            case kDifficultyMedium:
                label->SetTextToken(medium_short);
                break;
            case kDifficultyExpert:
                label->SetTextToken(expert_short);
                break;
            case kDifficultyBeginner:
                label->SetTextToken(beginner_short);
                break;
            default:
                MILO_NOTIFY(
                    "Bad difficulty %d retrieved from leaderboards for user                    %s at rank %d!",
                    d,
                    mLBRows[data].mName,
                    mLBRows[data].mRank
                );
                break;
            }
        } else if (slot->Matches("self")) {
            static Symbol gamertag2("gamertag");
            if (selfName == mLBRows[data].mName) {
                label->SetTokenFmt(gamertag2, mLBRows[data].mName);
            } else {
                label->SetTextToken(gNullStr);
            }
        }
    }
}

void AppMiniLeaderboardDisplay::UpdateSelfInRows() {
    HamProfile *profile = TheProfileMgr.GetActiveProfile(true);
    if (profile) {
        profile->UpdateOnlineID();
        bool bHasOnlineID = profile->IsSignedIn();
        MILO_ASSERT(bHasOnlineID, 0x107);
        XUID xuid = profile->GetOnlineID()->GetXUID();
        SongStatusMgr *pSongStatusMgr = profile->GetSongStatusMgr();
        MILO_ASSERT(pSongStatusMgr, 0x10b);
        bool noFlashcards = false;
        unsigned int score = pSongStatusMgr->GetScore(mSongID, noFlashcards);
        Difficulty diff = pSongStatusMgr->GetDifficulty(mSongID);
        if ((int)score > 0) {
            bool found = false;
            for (std::vector<LeaderboardRow>::iterator it = mLBRows.begin(); it != mLBRows.end(); ++it) {
                if (it->mXUID == xuid && score > (unsigned int)it->mScore) {
                    mLBRows.erase(it);
                    found = true;
                    break;
                }
            }
            if (found) {
                LeaderboardRow row;
                row.mScore = score;
                row.mIsPercentile = false;
                row.mRank = 0;
                row.mIsHardcore = true;
                row.mXUID = xuid;
                row.mName = profile->GetName();
                row.mNoFlashcards = noFlashcards;
                row.mDiffID = diff;
                bool inserted = false;
                for (std::vector<LeaderboardRow>::iterator it = mLBRows.begin(); it != mLBRows.end(); ++it) {
                    if (score >= (unsigned int)it->mScore) {
                        mLBRows.insert(it, 1, row);
                        inserted = true;
                        break;
                    }
                }
                if (!inserted) {
                    row.mModeID = 1;
                    mLBRows.push_back(row);
                }
                int rank = 1;
                for (std::vector<LeaderboardRow>::iterator it = mLBRows.begin(); it != mLBRows.end(); ++it) {
                    it->mModeID = rank;
                    rank++;
                }
            }
        }
    }
}
