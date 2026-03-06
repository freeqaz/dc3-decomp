#include "meta_ham/FitnessGoalMgr.h"
#include "HamProfile.h"
#include "game/PartyModeMgr.h"
#include "macros.h"
#include "meta_ham/PlaylistSortMgr.h"
#include "meta_ham/ProfileMgr.h"
#include "net_ham/FitnessGoalJobs.h"
#include "net_ham/RCJobDingo.h"
#include "net_ham/RockCentral.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/PlatformMgr.h"
#include "ui/UI.h"
#include "utl/DataPointMgr.h"
#include "utl/Symbol.h"

FitnessGoalMgr::FitnessGoalMgr() {
    SetName("fitness_goal_mgr", ObjectDir::Main());
    mProfileName = gNullStr;
    mOnlineID = gNullStr;
    mIsProcessingCommand = false;
    mCurrentRCJob = nullptr;
    mCommandQueue.clear();
    mCurrentProfile = nullptr;
}

FitnessGoalMgr::~FitnessGoalMgr() {}

void FitnessGoalMgr::OnSmartGlassListen(int) {}

void FitnessGoalMgr::Init() {
    MILO_ASSERT(!TheFitnessGoalMgr, 0x17);
    TheFitnessGoalMgr = new FitnessGoalMgr();
}

void FitnessGoalMgr::StartCmdGetFitnessGoalFromRC() {
    mCurrentRCJob = new GetFitnessGoalJob(this, mOnlineID.c_str());
    TheRockCentral.ManageJob(mCurrentRCJob);
}

bool FitnessGoalMgr::HasValidProfile() {
    HamProfile *pProfile = TheProfileMgr.GetActiveProfile(true);
    if (pProfile) {
        pProfile->UpdateOnlineID();
        if (pProfile->IsSignedIn()) {
            int padNum = pProfile->GetPadNum();
            if (ThePlatformMgr.IsSignedIntoLive(padNum) && TheRockCentral.IsOnline()) {
                mProfileName = pProfile->GetName();
                QueueCmdChangeProfileOnlineID(pProfile->GetOnlineID()->ToString());
                return true;
            }
        }
    }
    mProfileName = gNullStr;
    QueueCmdChangeProfileOnlineID(gNullStr);
    return false;
}

void FitnessGoalMgr::UploadNextProfile() {
    std::list<HamProfile *>::iterator it;

    if (!mPendingProfiles.empty()) {
        mCurrentProfile = *mPendingProfiles.begin();
        it = mPendingProfiles.erase(mPendingProfiles.begin());
    }
    if (mCurrentProfile)
        QueueCmdUpdateFitnessGoalToRC(mCurrentProfile);
}

void FitnessGoalMgr::UpdateFitnessGoal(HamProfile *profile) {
    profile->UpdateOnlineID();
    if (profile->IsSignedIn()) {
        int padNum = profile->GetPadNum();
        if (ThePlatformMgr.IsSignedIntoLive(padNum) && profile->GetFitnessGoalNeedsUpload()) {
            if (!mCurrentProfile || mCurrentProfile == profile) {
                mCurrentProfile = profile;
                UploadNextProfile();
            } else {
                AddPendingProfile(profile);
            }
        }
    }
}

void FitnessGoalMgr::BroadcastSyncMsg(Symbol s) {
    Symbol sym = s;
    MILO_LOG("[FitnessGoalMgr::BroadcastSyncMsg] Broadcasting msg (%s).\n", sym);
    Message msg(sym);
    HandleType(msg);
    TheUI->Handle(msg, false);
}

void FitnessGoalMgr::StartCmdSendFitnessGoalToRC() {
    QueueableCommand *cmd = mCommandQueue.front();
    mCurrentRCJob = new SetFitnessGoalJob(this, cmd->mData.profile);
    TheRockCentral.ManageJob(mCurrentRCJob);
}

void FitnessGoalMgr::StartCmdUpdateFitnessGoalToRC() {
    QueueableCommand *cmd = mCommandQueue.front();
    mCurrentRCJob = new UpdateFitnessGoalJob(this, cmd->mData.profile);
    TheRockCentral.ManageJob(mCurrentRCJob);
}

void FitnessGoalMgr::StartCmdDeleteFitnessGoalFromRC() {
    QueueableCommand *cmd = mCommandQueue.front();
    mCurrentRCJob = new DeleteFitnessGoalJob(this, cmd->mData.profile);
    TheRockCentral.ManageJob(mCurrentRCJob);
}

void FitnessGoalMgr::HandleCmdChangeProfileOnlineID() {
    MILO_LOG("===== HandleCmdChangeProfileOnlineID\n");
    mOnlineID = mCommandQueue.front()->mData.onlineID;
    RELEASE(mCommandQueue.front());
    mCommandQueue.pop_front();
    ProcessNextCommand();
}

void FitnessGoalMgr::HandleCmdDeleteFitnessGoalFromRC() {
    mCurrentRCJob = nullptr;
    RELEASE(mCommandQueue.front());
    mCommandQueue.pop_front();
    ProcessNextCommand();
}

void FitnessGoalMgr::HandleCmdUpdateFitnessGoalToRC() {
    MILO_LOG("===== HandleCmdUpdateFitnessGoalToRC\n");
    mCurrentRCJob = nullptr;
    {
        // Scoped to control DataNode/DataArrayPtr lifetimes and Release order
        DataNode updated("updated");
        DataNode fitness("fitness");
        ThePlatformMgr.SmartGlassSend(0, DataArrayPtr(fitness, updated));
    }
    RELEASE(mCommandQueue.front());
    mCommandQueue.pop_front();
    if (mCurrentProfile) {
        mCurrentProfile->ClearFitnessGoalNeedUpload();
    }
    if (!mPendingProfiles.empty()) {
        UploadNextProfile();
    } else {
        mCurrentProfile = nullptr;
    }
    ProcessNextCommand();
}

void FitnessGoalMgr::QueueCmdGetFitnessGoalFromRC() {
    CmdGetFitnessGoalFromRC *cmd = new CmdGetFitnessGoalFromRC();
    mCommandQueue.push_back(cmd);
    if (!mIsProcessingCommand) {
        ProcessNextCommand();
    }
}

void FitnessGoalMgr::QueueCmdSendFitnessGoalToRC(HamProfile *profile) {
    CmdSendFitnessGoalToRC *cmd = new CmdSendFitnessGoalToRC(profile);
    mCommandQueue.push_back(cmd);
    if (!mIsProcessingCommand) {
        ProcessNextCommand();
    }
}

void FitnessGoalMgr::QueueCmdUpdateFitnessGoalToRC(HamProfile *profile) {
    CmdUpdateFitnessGoalToRC *cmd = new CmdUpdateFitnessGoalToRC(profile);
    mCommandQueue.push_back(cmd);
    if (!mIsProcessingCommand) {
        ProcessNextCommand();
    }
}

void FitnessGoalMgr::QueueCmdDeleteFitnessGoalFromRC(HamProfile *profile) {
    CmdDeleteFitnessGoalFromRC *cmd = new CmdDeleteFitnessGoalFromRC(profile);
    mCommandQueue.push_back(cmd);
    if (!mIsProcessingCommand) {
        ProcessNextCommand();
    }
}

void FitnessGoalMgr::QueueCmdChangeProfileOnlineID(String str) {
    CmdChangeProfileOnlineID *cmd = new CmdChangeProfileOnlineID(str);
    mCommandQueue.push_back(cmd);
    if (!mIsProcessingCommand) {
        ProcessNextCommand();
    }
}

void FitnessGoalMgr::OnSendFitnessGoalToRC(HamProfile *profile) {
    if (!profile) {
        return;
    }
    QueueCmdSendFitnessGoalToRC(profile);
}

void FitnessGoalMgr::DeleteFitnessGoalFromRC(HamProfile *profile) {
    if (!profile) {
        return;
    }
    QueueCmdDeleteFitnessGoalFromRC(profile);
}

DataNode FitnessGoalMgr::OnMsg(const SmartGlassMsg &msg) {
    MILO_LOG("SmartGlass: I should update fitness goal from RC\n");
    SendDataPoint("smartglass/fitness");
    QueueCmdGetFitnessGoalFromRC();
    return 1;
}

BEGIN_HANDLERS(FitnessGoalMgr)
    HANDLE_EXPR(has_valid_profile, HasValidProfile())
    HANDLE_EXPR(is_profile_changed, IsProfileChanged())
    HANDLE_ACTION(get_fitness_goal_from_rc, QueueCmdGetFitnessGoalFromRC())
    HANDLE_ACTION(smart_glass_listen, OnSmartGlassListen(_msg->Int(2)))
    HANDLE_MESSAGE(RCJobCompleteMsg)
    HANDLE_MESSAGE(SmartGlassMsg)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
