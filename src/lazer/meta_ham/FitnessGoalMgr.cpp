#include "meta_ham\FitnessGoalMgr.h"
#include "HamProfile.h"
#include "game\PartyModeMgr.h"
#include "hamobj\HamGameData.h"
#include "macros.h"
#include "meta_ham\PassiveMessenger.h"
#include "meta_ham\PlaylistSortMgr.h"
#include "meta_ham\ProfileMgr.h"
#include "net_ham\FitnessGoalJobs.h"
#include "net_ham\RCJobDingo.h"
#include "net_ham\RockCentral.h"
#include "obj\Data.h"
#include "obj\Dir.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\PlatformMgr.h"
#include "ui\UI.h"
#include "utl\DataPointMgr.h"
#include "utl\Std.h"
#include "utl\Symbol.h"

// Target: FitnessGoalMgr.obj .bss:0x0 (0x8311AA44), zero.
FitnessGoalMgr *TheFitnessGoalMgr;

FitnessGoalMgr::FitnessGoalMgr() {
    SetName("fitness_goal_mgr", ObjectDir::Main());
    mProfileName = gNullStr;
    mOnlineID = gNullStr;
    mIsProcessingCommand = false;
    mCurrentRCJob = nullptr;
    mCommands.clear();
    mCurrentProfile = nullptr;
}

FitnessGoalMgr::~FitnessGoalMgr() {}


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

    auto& _ref1 = mCurrentProfile;
    if (!mPendingProfiles.empty()) {
        _ref1 = *mPendingProfiles.begin();
        it = mPendingProfiles.erase(mPendingProfiles.begin());
    }
    if (_ref1)
        QueueCmdUpdateFitnessGoalToRC(_ref1);
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
    const char *sym = s.Str();
    MILO_LOG("[FitnessGoalMgr::BroadcastSyncMsg] Broadcasting msg (%s).\n", sym);
    Message msg(s);
    HandleType(msg);
    TheUI->Handle(msg, false);
}

void FitnessGoalMgr::StartCmdSendFitnessGoalToRC() {
    CmdSendFitnessGoalToRC *cmd = (CmdSendFitnessGoalToRC *)mCommands.front();
    mCurrentRCJob = new SetFitnessGoalJob(this, cmd->mData.profile);
    TheRockCentral.ManageJob(mCurrentRCJob);
}

void FitnessGoalMgr::StartCmdUpdateFitnessGoalToRC() {
    CmdUpdateFitnessGoalToRC *cmd = (CmdUpdateFitnessGoalToRC *)mCommands.front();
    mCurrentRCJob = new UpdateFitnessGoalJob(this, cmd->mData.profile);
    TheRockCentral.ManageJob(mCurrentRCJob);
}

void FitnessGoalMgr::StartCmdDeleteFitnessGoalFromRC() {
    CmdDeleteFitnessGoalFromRC *cmd = (CmdDeleteFitnessGoalFromRC *)mCommands.front();
    mCurrentRCJob = new DeleteFitnessGoalJob(this, cmd->mData.profile);
    TheRockCentral.ManageJob(mCurrentRCJob);
}

void FitnessGoalMgr::HandleCmdChangeProfileOnlineID() {
    MILO_LOG("===== HandleCmdChangeProfileOnlineID\n");
    mOnlineID = ((CmdChangeProfileOnlineID *)mCommands.front())->mOnlineID;
    RELEASE(mCommands.front());
    mCommands.pop_front();
    ProcessNextCommand();
}

void FitnessGoalMgr::HandleCmdGetFitnessGoalFromRC() {
    HamProfile *pProfile = TheProfileMgr.GetActiveProfile(true);
    if (pProfile && pProfile->IsSignedIn()) {
        int padNum = pProfile->GetPadNum();
        if (ThePlatformMgr.IsSignedIntoLive(padNum) && TheRockCentral.IsOnline()) {
            if (!(mProfileName != pProfile->GetName())) {
                MILO_LOG("===== [SUCCESS] HandleCmdGetFitnessGoalFromRC\n");
                ((GetFitnessGoalJob *)mCurrentRCJob)->GetFitnessGoal(pProfile);
                BroadcastSyncMsg(Symbol("fitness_goal_synced"));
                SendPassiveMsg(Symbol("fitness_synced_with_rc"));
                goto cleanup;
            }
        }
    }
    MILO_LOG("===== [FAIL] HandleCmdGetFitnessGoalFromRC\n");
    BroadcastSyncMsg(Symbol("sync_failed"));
cleanup:
    mCurrentRCJob = nullptr;
    RELEASE(mCommands.front());
    mCommands.pop_front();
    ProcessNextCommand();
}

void FitnessGoalMgr::HandleCmdSendFitnessGoalToRC() {
    MILO_LOG("===== HandleCmdSendFitnessGoalToRC\n");
    mCurrentRCJob = nullptr;
    {
        DataNode fitness("fitness");
        DataNode updated("updated");
        ThePlatformMgr.SmartGlassSend(0, DataArrayPtr(updated, fitness));
    }
    RELEASE(mCommands.front());
    mCommands.pop_front();
    ProcessNextCommand();
}

void FitnessGoalMgr::HandleCmdDeleteFitnessGoalFromRC() {
    mCurrentRCJob = nullptr;
    RELEASE(mCommands.front());
    mCommands.pop_front();
    ProcessNextCommand();
}

void FitnessGoalMgr::HandleCmdUpdateFitnessGoalToRC() {
    MILO_LOG("===== HandleCmdUpdateFitnessGoalToRC\n");
    mCurrentRCJob = nullptr;
    {
        // Scoped to control DataNode/DataArrayPtr lifetimes and Release order
        DataNode fitness("fitness");
        DataNode updated("updated");
        ThePlatformMgr.SmartGlassSend(0, DataArrayPtr(updated, fitness));
    }
    RELEASE(mCommands.front());
    mCommands.pop_front();
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
    mCommands.push_back(cmd);
    if (!mIsProcessingCommand) {
        ProcessNextCommand();
    }
}

void FitnessGoalMgr::QueueCmdSendFitnessGoalToRC(HamProfile *profile) {
    CmdSendFitnessGoalToRC *cmd = new CmdSendFitnessGoalToRC(profile);
    mCommands.push_back(cmd);
    if (!mIsProcessingCommand) {
        ProcessNextCommand();
    }
}

void FitnessGoalMgr::QueueCmdUpdateFitnessGoalToRC(HamProfile *profile) {
    CmdUpdateFitnessGoalToRC *cmd = new CmdUpdateFitnessGoalToRC(profile);
    mCommands.push_back(cmd);
    if (!mIsProcessingCommand) {
        ProcessNextCommand();
    }
}

void FitnessGoalMgr::QueueCmdDeleteFitnessGoalFromRC(HamProfile *profile) {
    CmdDeleteFitnessGoalFromRC *cmd = new CmdDeleteFitnessGoalFromRC(profile);
    mCommands.push_back(cmd);
    if (!mIsProcessingCommand) {
        ProcessNextCommand();
    }
}

void FitnessGoalMgr::QueueCmdChangeProfileOnlineID(String str) {
    CmdChangeProfileOnlineID *cmd = new CmdChangeProfileOnlineID(str);
    mCommands.push_back(cmd);
    if (!mIsProcessingCommand) {
        ProcessNextCommand();
    }
}

void PartyModeMgr::OnSmartGlassListen(int i) {
    if (i != 0) {
        ThePlatformMgr.AddSink(this, "smart_glass_msg");
    } else {
        ThePlatformMgr.RemoveSink(this, "smart_glass_msg");
    }
}

void FitnessGoalMgr::AddPendingProfile(HamProfile *profile) {
    MILO_ASSERT(profile, 0x1dc);
    bool found = false;
    for (std::list<HamProfile *>::iterator it = mPendingProfiles.begin();
         it != mPendingProfiles.end();
         ++it) {
        if (*it == profile) {
            found = true;
            break;
        }
    }
    if (!found) {
        mPendingProfiles.push_back(profile);
    }
}

void FitnessGoalMgr::OnSmartGlassListen(int i) {
    if (i != 0) {
        ThePlatformMgr.AddSink(this, "smart_glass_msg");
    } else {
        ThePlatformMgr.RemoveSink(this, "smart_glass_msg");
    }
}

void FitnessGoalMgr::SendPassiveMsg(Symbol sym) {
    static Symbol p1("p1");
    static Symbol p2("p2");
    static Symbol none("none");

    Symbol playerSym = none;
    for (int i = 0; i < 2; i++) {
        HamPlayerData *playerData = TheGameData->Player(i);
        MILO_ASSERT(playerData, 0x45);
        if (playerData->GetPlayerName() == mProfileName) {
            playerSym = (i == 0) ? p1 : p2;
            break;
        }
    }
    ThePassiveMessenger->TriggerGenericMsg(
        sym, playerSym, kPassiveMessageGeneral, Symbol(gNullStr), -1
    );
}

DataNode FitnessGoalMgr::OnMsg(const RCJobCompleteMsg &msg) {
    if (!msg.Success()) {
        MILO_ASSERT(!mCommands.empty(), 0x163);
        QueueableCommand *cmd = mCommands.front();
        switch (cmd->GetType()) {
        case 1:
            MILO_LOG("[FitnessGoalMgr::OnMsg] Fitness Goal net API ==kCmdGetFitnessGoalFromRC== failed.\n");
            break;
        case 2:
            MILO_LOG("[FitnessGoalMgr::OnMsg] Fitness Goal net API ==kCmdSendFitnessGoalToRC== failed.\n");
            break;
        case 3:
            MILO_LOG("[FitnessGoalMgr::OnMsg] Fitness Goal net API ==kCmdDeleteFitnessGoalFromRC== failed.\n");
            break;
        case 4:
            MILO_LOG("[FitnessGoalMgr::OnMsg] Fitness Goal net API ==kCmdUpdateFitnessGoalToRC== failed.\n");
            break;
        }
        mCurrentRCJob = nullptr;
        BroadcastSyncMsg(Symbol("sync_failed"));
        RELEASE(mCommands.front());
        mCommands.pop_front();
        ProcessNextCommand();
    } else {
        if (msg.Job() == mCurrentRCJob) {
            QueueableCommand *cmd = mCommands.front();
            switch (cmd->GetType()) {
            case 1:
                HandleCmdGetFitnessGoalFromRC();
                break;
            case 2:
                HandleCmdSendFitnessGoalToRC();
                break;
            case 3:
                HandleCmdDeleteFitnessGoalFromRC();
                break;
            case 4:
                HandleCmdUpdateFitnessGoalToRC();
                break;
            }
        }
    }
    return 1;
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

void FitnessGoalMgr::ProcessNextCommand() {
    if (mCommands.size() == 0) {
        mIsProcessingCommand = false;
    } else {
        mIsProcessingCommand = true;
        QueueableCommand *cmd = mCommands.front();
        switch (cmd->GetType()) {
        case 0:
            HandleCmdChangeProfileOnlineID();
            break;
        case 1:
            StartCmdGetFitnessGoalFromRC();
            break;
        case 2:
            StartCmdSendFitnessGoalToRC();
            break;
        case 3:
            StartCmdDeleteFitnessGoalFromRC();
            break;
        case 4:
            StartCmdUpdateFitnessGoalToRC();
            break;
        }
    }
}

DataNode FitnessGoalMgr::OnMsg(const SmartGlassMsg &msg) {
    MILO_LOG("SmartGlass: I should update fitness goal from RC\n");
    SendDataPoint("smartglass/fitness");
    QueueCmdGetFitnessGoalFromRC();
    return 1;
}

bool FitnessGoalMgr::IsProfileChanged() {
    HamProfile *profile = TheProfileMgr.GetActiveProfile(true);
    return mProfileName != (profile ? profile->GetName() : (const char *)gNullStr);
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
