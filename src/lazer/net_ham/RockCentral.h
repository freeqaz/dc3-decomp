#pragma once
#include "meta/ConnectionStatusPanel.h"
#include "net/DingoSvr.h"
#include "net_ham/KinectShare.h"
#include "net_ham/MotdJobs.h"
#include "net_ham/RCJobDingo.h"
#include "obj/Data.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/Timer.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Tex.h"
#include "utl/HxGuid.h"
#include "utl/Str.h"

DECLARE_MESSAGE(TmsDownloadedMsg, "tms_downloaded")
END_MESSAGE

DECLARE_MESSAGE(UserLoginMsg, "user_login")
END_MESSAGE

class RockCentral : public Hmx::Object {
public:
    enum State {
        kDisconnected = 0,
        kAuthenticating = 1,
        kConnected = 2,
        kLoggingOut = 3,
        kFailed = 4
    };
    RockCentral();
    virtual ~RockCentral();
    virtual DataNode Handle(DataArray *, bool);
    virtual void SetLoginName(const char *);
    virtual void SetLoginPassword(const char *);
    virtual void Login();
    virtual void CreateAccount();
    virtual unsigned int GetPrincipalID() const { return 0; }

    void Poll();
    void Init();
    void Terminate();
    void GetCommunityMsg(int, String &) const;
    int GetCommunityMsgCount() const;
    bool HasDlcMsg();
    void GetDlcMsg(String &) const;
    bool HasUtilityMsg();
    void GetUtilityMsg(String &) const;
    void ForceLogout();
    bool IsOnline();
    void ManageJob(RCJob *);
    void CancelOutstandingCalls(Hmx::Object *);
    void SetMiscArtBitMap(RndBitmap &);
    void DeleteMiscArt();

    DataNode OnMsg(const ServerStatusChangedMsg &);
    DataNode OnMsg(const ConnectionStatusChangedMsg &);
    DataNode OnMsg(const TmsDownloadedMsg &);
    DataNode OnMsg(const RCJobCompleteMsg &);
    DataNode OnMsg(const UserLoginMsg &);

    bool IsLoginBlocked() const { return mLoginBlocked; }
    String GetDLCImage() { return mDLCImagePath; }
    String GetUtilityImage() { return mUtilityImagePath; }
    String GetUtilitySound() { return mUtilitySoundPath; }
    String GetMiscImage() { return mMiscArtImagePath; }
    int GetRockCentralTime() { return mRockCentralTime; }
    void SetRockCentralTime(int i) { mRockCentralTime = i; }
    unsigned int GetChallengeInterval() const { return mChallengeInterval; }
    int GetControllerModeExitCount() const { return mControllerModeExitCount; }
    void SetControllerModeExitCount(int i) { mControllerModeExitCount = i; }
    int GetControllerModeEnterCount() const { return mControllerModeEnterCount; }
    void SetControllerModeEnterCount(int i) { mControllerModeEnterCount = i; }
    bool GetMotdXPFlag() const { return mMotdXPFlag; }
    int GetMotdFreq() const { return mMotdFreq; }

private:
    static const String kServerVer;

protected:
    virtual void OnJobFinished(RCJob *);

    std::vector<RCJob *> mManagedJobs;
    std::vector<RCJob *> unk38;
    State mState; // 0x44
    Timer mTimer;
    float mNextLoginMs;
    float mNextControllerUploadMs;
    GetMotdJob *mMOTDJob; // 0x80
    unsigned int mChallengeInterval; // 0x84
    int mRockCentralTime; // 0x88
    bool mMotdXPFlag; // 0x8c
    int mMotdFreq; // 0x90
    std::vector<String> mCommunityMsgs; // 0x94
    String mDLCMsg; // 0xa0
    String mDLCImagePath; // 0xa8
    String mDLCSoundPath;
    String mUtilityMsg; // 0xb8
    String mUtilityImagePath; // 0xc0
    String mUtilitySoundPath; // 0xc8
    String mMiscArtImagePath; // 0xd0
    RndTex *mMiscArt; // 0xd8
    bool mLoginBlocked; // 0xdc
    bool mJustConnected;
    HxGuid unke0;
    XNADDR mXNetAddr; // 0xf0
    ULONGLONG mMachineID; // 0x118
    KinectShareConnection *mKinectShareConnection; // 0x120
    Hmx::Object *mKinectShareCallback;
    int mControllerModeEnterCount; // 0x128
    int mControllerModeExitCount; // 0x12c
};

extern RockCentral TheRockCentral;

class RockCentralOpCompleteMsg : public Message, public Hmx::Object {
public:
    RockCentralOpCompleteMsg();
    RockCentralOpCompleteMsg(bool b, int i, DataNode n) : Message(Type(), b, i, n) {}
    RockCentralOpCompleteMsg(DataArray *da) : Message(da) {}
    static Symbol Type() {
        static Symbol t("rock_central_op_complete_msg");
        return t;
    }
    bool Success() const { return mData->Int(2); }
    int Arg1() const { return mData->Int(3); }
    DataNode Arg2() const { return mData->Node(4); }
};
