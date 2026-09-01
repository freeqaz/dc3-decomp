#pragma once
#include "stl\_vector.h"
#include "xdk\win_types.h"
#include "xdk\xapilibi\xbase.h"

class MicXbox;
class Symbol;

class ExternalMic {
public:
    ~ExternalMic();
    ExternalMic(unsigned long);
    long gatherGainAttribs(unsigned long);
    long processGain(unsigned long);
    void dataReady(unsigned long, unsigned long, _XOVERLAPPED *);
    unsigned long sampleProcessThread();

    static int NumConnectedMics();
    static void Terminate();
    static void Init();

    HANDLE mThread; // 0x0
    unsigned long mDeviceId;
    bool mQuit;
    bool unk9;
    float mLastGain;  // 0xc
    float mGainLeft;  // 0x10
    float mGainRight; // 0x14
};

// One of these is created lazily per physical mic device the game binds to a
// client slot. Layout is proven by the target: mIndex is loaded from 0x0 and
// mConnected stored to 0x4, sizeof == 8 (`li r3, 0x8` before operator new in
// ExternalMicClientMgr::GetMasterForIndex).
class ExternalMicClientProxy {
public:
    ExternalMicClientProxy(unsigned long index) : mIndex(index) {}

    long OnMicConnected(unsigned long, bool, const Symbol &);

    unsigned long mIndex; // 0x0
    bool mConnected; // 0x4
};

class ExternalMicClientMgr {
    friend class ExternalMicClientProxy;

public:
    static void Init();
    static void Terminate();
    static ExternalMicClientProxy *GetMasterForIndex(unsigned long);
    static void Associate(int, MicXbox *);
    static bool ConnectedForClient(const MicXbox *);
    static void AddAudio(unsigned long, unsigned char *, unsigned long);
    static float GetRequiredGain(unsigned long);
    static void OnMicDisconnected(unsigned long);

private:
    static std::vector<ExternalMicClientProxy *> mMicMasters;
    static std::vector<unsigned long> mDevToMicMaster;
    static std::vector<unsigned long> mMicMasterToDev;
    static std::vector<MicXbox *> mAssocMicXbox;
};
