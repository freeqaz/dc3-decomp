#include "net_ham/RockCentral.h"
#include "meta/ConnectionStatusPanel.h"
#include "meta_ham/Challenges.h"
#include "meta_ham/ProfileMgr.h"
#include "net/DingoSvr.h"
#include "net_ham/DataMinerJobs.h"
#include "net_ham/KinectShareJobs.h"
#include "net_ham/RCJobDingo.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/PlatformMgr.h"
#include "os/System.h"
#include "rnddx9/Rnd.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Tex.h"
#include "ui/UIPanel.h"
#include "utl/DataPointMgr.h"
#include "utl/Symbol.h"
#include "xdk/XNET.h"

char *g_szMachineIdString;
const String RockCentral::kServerVer = "1";
RockCentral TheRockCentral;

namespace {
    void RockCentralTerminate() { TheRockCentral.Terminate(); }

    class DataPointJob : public RCJob {
    public:
        DataPointJob(DataPoint &pt, String &url) : RCJob(url.c_str(), nullptr) {
            SetDataPoint(pt);
        }
    };

    void SendDataPointNoReturn(DataPoint &dataPoint) {
        const char *type = dataPoint.Type();
        String str;
        if (type && strlen(type) != 0 && type[0] != '/') {
            if (type[strlen(type) - 1] == '/') {
                str = MakeString("dataminer/%s", dataPoint.Type());
            } else {
                str = MakeString("dataminer/%s/", dataPoint.Type());
            }
        } else {
            MILO_WARN(
                "SendDataPointNoReturn: dataPoint.mType must be in '<url>/' format!"
            );
            str = "dataminer/undefined/";
        }
        // so if login IS blocked...what do we do with this job?
        DataPointJob *job = new DataPointJob(dataPoint, str);
        if (!TheRockCentral.IsLoginBlocked()) {
            TheServer.ManageJob(job);
        }
    }
}

RockCentral::RockCentral()
    : mState(), mNextControllerUploadMs(0), mMOTDJob(0), mChallengeInterval(60000), mRockCentralTime(-1), mMotdXPFlag(0),
      mMotdFreq(0), mMiscArt(0), mLoginBlocked(0), mJustConnected(0), mKinectShareConnection(0),
      mKinectShareCallback(0), mControllerModeEnterCount(0), mControllerModeExitCount(0) {}

RockCentral::~RockCentral() { RELEASE(mKinectShareConnection); }

BEGIN_HANDLERS(RockCentral)
    HANDLE_MESSAGE(ServerStatusChangedMsg)
    HANDLE_MESSAGE(ConnectionStatusChangedMsg)
    HANDLE_MESSAGE(TmsDownloadedMsg)
    HANDLE_MESSAGE(RCJobCompleteMsg)
    HANDLE_MESSAGE(UserLoginMsg)
    HANDLE_EXPR(state, mState)
    HANDLE_ACTION(force_logout, ForceLogout())
    HANDLE_EXPR(is_online, IsOnline())
    HANDLE_EXPR(toggle_block_login, mLoginBlocked = !mLoginBlocked)
    HANDLE_EXPR(is_login_blocked, mLoginBlocked)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

void RockCentral::ForceLogout() {
    if (mState == 2 || mState == 1) {
        mState = (State)3;
        TheServer.Logout();
    }
}

bool RockCentral::IsOnline() {
    if (mLoginBlocked) {
        return false;
    } else {
        return mState == 2;
    }
}

void RockCentral::SetLoginName(const char *name) { TheServer.SetLoginName(name); }
void RockCentral::SetLoginPassword(const char *password) {
    TheServer.SetLoginPassword(password);
}

void RockCentral::Login() {
    mState = (State)1;
    mJustConnected = false;
    if (!TheServer.Authenticate(TheServer.GetAuthedPadNum())) { // should be TheServer->unk74
        Export(ServerStatusChangedMsg((ServerStatusResult)4), false);
    }
}

void RockCentral::CreateAccount() {
    MILO_ASSERT(mState == kFailed, 0x124);
    TheServer.CreateAccount();
}

void RockCentral::OnJobFinished(RCJob *job) {
    MILO_ASSERT(job->IsFinished(), 0x24A);
    delete job;
}

static bool sSentScreenRes;

void RockCentral::Poll() {
    mTimer.Split();

    switch (mState) {
    case kDisconnected:
    case kFailed:
        if (ThePlatformMgr.IsConnected()) {
            if (mTimer.Ms() >= mNextLoginMs && !mLoginBlocked) {
                Login();
            }
        }
        break;
    case kAuthenticating:
    case kConnected:
    case kLoggingOut:
        break;
    default:
        MILO_FAIL("Bad Rock Central state");
        break;
    }

    if (IsOnline()) {
        TheProfileMgr.UploadDeferredFlaunt();
        TheProfileMgr.UploadDeferredFitnessGoal();
    }

    if (mJustConnected) {
        mMOTDJob = new GetMotdJob(this);
        if (!mLoginBlocked) {
            TheServer.ManageJob(mMOTDJob);
        }
        if (!sSentScreenRes) {
            sSentScreenRes = true;
#ifndef HX_NATIVE
            ScreenResJob *job = new ScreenResJob(nullptr, TheDxRnd.VideoMode());
            if (!TheRockCentral.IsLoginBlocked()) {
                TheServer.ManageJob(job);
            }
#endif
        }
        TheChallenges->DownloadOfficialChallenges();
        mJustConnected = false;
    }

    if (mKinectShareConnection) {
        mKinectShareConnection->Poll();
        int kinectState = mKinectShareConnection->GetState();
        if (kinectState == 3) {
            if (mKinectShareCallback) {
                RockCentralOpCompleteMsg msg(false, -1, DataNode());
                mKinectShareCallback->Handle(msg, true);
                mKinectShareCallback = nullptr;
            }
            RELEASE(mKinectShareConnection);
        } else if (kinectState == 2) {
            if (mKinectShareCallback) {
                RockCentralOpCompleteMsg msg(true, 0, DataNode());
                mKinectShareCallback->Handle(msg, true);
                mKinectShareCallback = nullptr;
            }
            RELEASE(mKinectShareConnection);
            KinectShareJob *job = new KinectShareJob(nullptr);
            if (!TheRockCentral.IsLoginBlocked()) {
                TheServer.ManageJob(job);
            }
        }
    }

    if (mTimer.Ms() >= mNextControllerUploadMs) {
        if (mControllerModeEnterCount != 0 || mControllerModeExitCount != 0) {
            ControllerModeJob *job =
                new ControllerModeJob(nullptr, mControllerModeEnterCount, mControllerModeExitCount);
            if (!mLoginBlocked) {
                TheServer.ManageJob(job);
            }
        }
        mControllerModeEnterCount = 0;
        mControllerModeExitCount = 0;
        mNextControllerUploadMs = mTimer.Ms() + 600000.0f;
    }

    TheServer.Poll();
}

void RockCentral::Init() {
    SetName("rock_central", ObjectDir::Main());
    TheServer.AddSink(this);
    static Symbol connection_status_changed("connection_status_changed");
    ThePlatformMgr.AddSink(this, ConnectionStatusChangedMsg::Type());
    ThePlatformMgr.AddSink(this, TmsDownloadedMsg::Type());
    TheDebug.AddExitCallback(RockCentralTerminate);
    TheDataPointMgr.SetDataPointRecorder(SendDataPointNoReturn);
    unke0.Generate();
    mTimer.Start();
    mNextLoginMs = mTimer.Ms();
    mNextControllerUploadMs = mTimer.Ms() + 600000.0f;
    mDLCMsg = gNullStr;
    mUtilityMsg = gNullStr;
    mCommunityMsgs.clear();
}

void RockCentral::Terminate() {
    TheServer.RemoveSink(this);
    ThePlatformMgr.RemoveSink(this, ConnectionStatusChangedMsg::Type());
    ThePlatformMgr.RemoveSink(this, TmsDownloadedMsg::Type());
}

void RockCentral::GetCommunityMsg(int index, String &str) const {
    MILO_ASSERT_RANGE(index, 0, mCommunityMsgs.size(), 0x1AE);
    str = mCommunityMsgs[index];
}

int RockCentral::GetCommunityMsgCount() const { return mCommunityMsgs.size(); }
bool RockCentral::HasDlcMsg() { return !(mDLCMsg == gNullStr); }
void RockCentral::GetDlcMsg(String &str) const { str = mDLCMsg; }
bool RockCentral::HasUtilityMsg() { return !(mUtilityMsg == gNullStr); }
void RockCentral::GetUtilityMsg(String &str) const { str = mUtilityMsg; }
DataNode RockCentral::OnMsg(const UserLoginMsg &) { return 1; }

void RockCentral::ManageJob(RCJob *job) {
#ifdef HX_NATIVE
    delete job;
    return;
#endif
    if (!mLoginBlocked) {
        TheServer.ManageJob(job);
    }
}

void RockCentral::SetMiscArtBitMap(RndBitmap &bmap) {
    DeleteMiscArt();
    mMiscArt = Hmx::Object::New<RndTex>();
    mMiscArt->SetBitmap(bmap, nullptr, false, RndTex::kRegular);
}

void RockCentral::DeleteMiscArt() {
    if (mMiscArt) {
        RELEASE(mMiscArt);
    }
}

void RockCentral::CancelOutstandingCalls(Hmx::Object *obj) {
    for (auto it = mManagedJobs.begin(); it != mManagedJobs.end();) {
        RCJob *cur = *it;
        if (cur->GetCallback() == obj) {
            mManagedJobs.erase(it);
            cur->Cancel(false);
            OnJobFinished(cur);
            it = mManagedJobs.begin();
        } else {
            ++it;
        }
    }
}

DataNode RockCentral::OnMsg(const ConnectionStatusChangedMsg &msg) {
    if (msg.Connected() && (mState == 4 || mState == 0)) {
        mState = (State)0;
        mNextLoginMs = mTimer.Ms();
    } else if (!msg.Connected() && (mState == 2 || mState == 1)) {
        mState = (State)3;
        TheServer.Logout();
    }
    return 1;
}

DataNode RockCentral::OnMsg(const TmsDownloadedMsg &msg) {
    if (ThePlatformMgr.IsConnected() && (mState == 4 || mState == 0)) {
        mState = (State)0;
        mNextLoginMs = mTimer.Ms();
    }
    return 1;
}

DataNode RockCentral::OnMsg(const RCJobCompleteMsg &msg) {
    if (msg.Success()) {
        mMOTDJob->GetMotdData(
            mChallengeInterval,
            mRockCentralTime,
            mMotdXPFlag,
            mMotdFreq,
            mCommunityMsgs,
            mDLCMsg,
            mDLCImagePath,
            mDLCSoundPath,
            mUtilityMsg,
            mUtilityImagePath,
            mUtilitySoundPath,
            mMiscArtImagePath
        );

        // void GetMotdData(
        //     unsigned int &challengeInterval,
        //     int &lastNewSongDt,
        //     bool &motdXPFlag,
        //     int &motdFreq,
        //     std::vector<String> &toasts,
        //     String &motd,
        //     String &motdImage,
        //     String &motdSound,
        //     String &motdAux,
        //     String &motdImageAux,
        //     String &motdSoundAux,
        //     String &motdMiscImage
        // );

        TheProfileMgr.CheckForServerCrewUnlock();
        static Symbol motd_loaded("motd_loaded");
        static Message msg(motd_loaded);
        UIPanel *panel = ObjectDir::Main()->Find<UIPanel>("main_panel");
        if (panel->GetState() == UIPanel::kUp) {
            panel->HandleType(msg);
        }
    }
    return 1;
}

DataNode RockCentral::OnMsg(const ServerStatusChangedMsg &msg) {
    if (msg.Result() == kServerStatusConnected && mState != kConnected) {
        mJustConnected = true;
        mState = kConnected;
        XNetGetTitleXnAddr(&mXNetAddr);
        XNetXnAddrToMachineId(&mXNetAddr, &mMachineID);
        Hx_snprintf(g_szMachineIdString, 20, "%llu", mMachineID);
    } else if (msg.Result() != kServerStatusConnected) {
        if (mState == kLoggingOut) {
            mState = kDisconnected;
            mNextLoginMs = mTimer.Ms() + 8000.0f;
        } else if (msg.Result() == kServerStatusInvalidUserName) {
            mState = kFailed;
            CreateAccount();
            mNextLoginMs = mTimer.Ms() + 8000.0f;
        } else {
            mState = kFailed;
            mNextLoginMs = mTimer.Ms() + 40000.0f;
        }
    }
    Hmx::Object::Handle(msg, false);
    return 1;
}
