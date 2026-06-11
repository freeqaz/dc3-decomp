#include "synth_xbox/Mic.h"
#include "macros.h"
#include "math/Decibels.h"
#include "math/Utl.h"
#include "obj/Data.h"
#include "obj/DataFunc.h"
#include "os/CritSec.h"
#include "os/Debug.h"
#include "os/System.h"
#include "rnddx9/Rnd.h"
#include "synth_xbox/ExternalMic.h"
#include "synth_xbox/FxSend.h"
#include "synth_xbox/Voice.h"
#include "utl/MemStream.h"
#include "utl/Std.h"
#include "utl/Symbol.h"
#include <cstring>

MicManagerXbox *sInstance;

namespace GainEffect {
static float sGain;
}

// Headset config values. The target lays these out as 5 separate statics that
// the linker places contiguously at lbl_82F474C8 (noiseThreshold@0x0,
// gNoiseInt@0x4, lowCut@0x8, localGain@0xc, remoteGain@0x10). The single-store
// Set* helpers reference each by its own label (e.g. lbl_82F474D0 for lowCut),
// while the ctor's consecutive FindData calls address them as base+offset — both
// behaviours fall out of the separate-static model (verified vs target asm).
static float gNoiseThreshold = -10;
static int gNoiseInt = 5;
static float gLowCut = 800;
static float gLocalGain = -3;
static float gRemoteGain = 3;

ChatReceiver::ChatReceiver(IXHV2Engine *engine, int i2)
    : mXHV(engine), unk4(i2), unk8(0), unk9(0), unkc(0), unk10(0), unk14(0), unk18(0),
      unk50(new MemStream(true)) {
    MILO_ASSERT(mXHV, 0x3F2);
}

ChatReceiver::~ChatReceiver() {
    ActivateProcessing(false);
    RELEASE(unk50);
}

void ChatReceiver::ActivateProcessing(bool b1) {
    if (b1 != unk9) {
        unk9 = b1;
        void *mode = _xhv_voicechat_mode;
        if (b1) {
            HRESULT hr = mXHV->RegisterLocalTalker(unk4);
            DX_ASSERT_CODE(hr, 0x40D);
            hr = mXHV->StartLocalProcessingModes(unk4, &mode, 1);
            DX_ASSERT_CODE(hr, 0x40E);
        } else {
            HRESULT hr = mXHV->StopLocalProcessingModes(unk4, &mode, 1);
            DX_ASSERT_CODE(hr, 0x412);
            hr = mXHV->UnregisterLocalTalker(unk4);
            DX_ASSERT_CODE(hr, 0x413);
        }
    }
}

#pragma region MicXbox

MicXbox::MicXbox(int, float volume)
    : mRunning(false), unk10(0), mChangeNotify(false), mPlaybackVoice(0), unk301c(unk1c),
      unk9054(1.0f), unk9058(0), unk905c(0), mFxSend(0), mVolume(volume), mMute(false),
      unk906c(0), mGain(1.0f), mOutputGain(1.0f), mSensitivity(1.0f), unk907c(0),
      mDroppedSamples(0), mDeviceName("generic_usb"), mClipping(false) {
    unk302c.Init(0xc00);
    unk3040.Init(0x6000);
    unk3020.reserve(0x1800);
    memset(unk1c, 0, 0x3000);
}

MicXbox::~MicXbox() {
    if (mRunning)
        Stop();
    delete mPlaybackVoice;
    mPlaybackVoice = 0;
}

bool MicXbox::GetClipping() const { return mClipping; }

float MicXbox::GetGain() const { return mGain; }

int MicXbox::GetDroppedSamples() { return mDroppedSamples; }

float MicXbox::GetOutputGain() const { return mOutputGain; }

float MicXbox::GetSensitivity() const { return mSensitivity; }

const Symbol &MicXbox::GetName() const { return mDeviceName; }

void MicXbox::SetGain(float gain) { mGain = Clamp(0.0f, 1.0f, gain); }

Mic::Type MicXbox::GetType() const {
    return ExternalMicClientMgr::ConnectedForClient(this) ? kUSBMic : kDisconnected;
}

void MicXbox::ClearBuffers() {
    unk302c.Reset();
    unk3040.Reset();
}

void MicXbox::SetOutputGain(float f) {
    mOutputGain = f;
    MILO_ASSERT(mOutputGain >= 0.0f, 0x32c);
}

void MicXbox::SetSensitivity(float f) {
    mSensitivity = f;
    MILO_ASSERT(mOutputGain >= 0.0f, 0x337);
}

void MicXbox::SetVolume(float f) { mVolume = DbToRatio(f); }

void MicXbox::SetChangeNotify(bool b) { mChangeNotify = b; }

void MicXbox::SetMute(bool b) { mMute = b; }

bool MicXbox::IsPlaying() { return mPlaybackVoice; }

void MicXbox::Start() {
    if (!mRunning) {
        unk301c = unk1c;
        MicManagerXbox *x = MicManagerXbox::GetInstance();
        x->AddMic(this);
        mRunning = true;
    }
}

void MicXbox::Stop() {
    if (mRunning) {
        MicManagerXbox *x = MicManagerXbox::GetInstance();
        x->RemoveMic(this);
        mRunning = false;
        if (mPlaybackVoice) {
            StopPlayback();
        }
    }
}

void MicXbox::SetFxSend(FxSend *fx) {
    CritSecTracker t(&MicManagerXbox::GetInstance()->unk68);
    mFxSend = fx;
    if (mPlaybackVoice) {
        StopPlayback();
        StartPlayback();
    }
}

void MicXbox::StartPlayback() {
    CritSecTracker t(&MicManagerXbox::GetInstance()->unk68);
    if (mPlaybackVoice) {
        return;
    }
    Start();
    mMute = false;
    unk9058 = unkc ? 2700.0f : 1800.0f;
    unk905c = 0;
    unk9054 = 1;
    mPlaybackVoice = new Voice(false, 1, false);
    mPlaybackVoice->SetSampleRate(48000);
    mPlaybackVoice->SetData(unk1c, sizeof(unk1c), 0);
    mPlaybackVoice->SetLoopRegion(0, -1);
    mPlaybackVoice->SetSend(dynamic_cast<FxSend360 *>(mFxSend));
    mPlaybackVoice->Start();
    mPlaybackVoice->SetVolume(0);
}

void MicXbox::StopPlayback() {
    CritSecTracker t(&MicManagerXbox::GetInstance()->unk68);
    RELEASE(mPlaybackVoice);
    memset(unk1c, 0, sizeof(unk1c));
}

short *MicXbox::GetRecentBuf(int &iref) {
    CritSecTracker t(&MicManagerXbox::GetInstance()->unk68);
    unk302c.Peek(unk3054, 0xC00);
    iref = 0x600;
    return (short *)unk3054;
}

short *MicXbox::GetContinuousBuf(int &iref) {
    CritSecTracker t(&MicManagerXbox::GetInstance()->unk68);
    iref = unk3040.Read(unk3054, 0x6000) / sizeof(short);
    return (short *)unk3054;
}

bool MicXbox::IsRunning() const { return mRunning; }
void MicXbox::SetDMA(bool b) {}
bool MicXbox::GetDMA() const { return false; }
void MicXbox::SetEarpieceVolume(float f) {}
float MicXbox::GetEarpieceVolume() const { return 0.0f; }
void MicXbox::SetCompressor(bool b) {}
bool MicXbox::GetCompressor() const { return false; }
void MicXbox::SetCompressorParam(float f) {}
float MicXbox::GetCompressorParam() const { return 0.0f; }
int MicXbox::GetSampleRate() const { return 16000; }

void MicXbox::OnMicConnected(unsigned long ul, bool b, Symbol const &s) {
    unkc = b;
    mDeviceName = s;
    MicManagerXbox *x = MicManagerXbox::GetInstance();
    x->mMicsChanged = true;
}

void MicXbox::OnMicDisconnected() {
    MicManagerXbox *x = MicManagerXbox::GetInstance();
    x->mMicsChanged = true;
}

#pragma endregion MicXbox
#pragma region MicManagerXbox

DataNode SetNoiseGate(DataArray *a) {
    gNoiseThreshold = a->Float(1);
    if (a->Size() >= 3) {
        gNoiseInt = a->Int(2);
    }
    return 0;
}

DataNode SetLowCut(DataArray *a) {
    gLowCut = a->Float(1);
    return 0;
}

DataNode SetLocalGain(DataArray *a) {
    gLocalGain = a->Float(1);
    return 0;
}

DataNode SetRemoteGain(DataArray *a) {
    gRemoteGain = a->Float(1);
    GainEffect::sGain = DbToRatio(gRemoteGain);
    return 0;
}

MicManagerXbox::MicManagerXbox()
    : unk18(-1), mXHVEngine(0), unk2c(0), mMicsChanged(false), mPushToTalkPad(-1) {
    for (int i = 0; i < 4; i++) {
        unkc.push_back(0);
    }
    unk20.reserve(4);

    DataRegisterFunc("set_noise_gate", SetNoiseGate);
    DataRegisterFunc("set_low_cut", SetLowCut);
    DataRegisterFunc("set_local_gain", SetLocalGain);
    DataRegisterFunc("set_remote_gain", SetRemoteGain);
    DataArray *synthConfig = SystemConfig("synth", "xbox_headset");
    synthConfig->FindData("noise_threshold", gNoiseThreshold);
    synthConfig->FindData("low_cut", gLowCut);
    synthConfig->FindData("local_gain", gLocalGain);
    synthConfig->FindData("remote_gain", gRemoteGain);
    GainEffect::sGain = DbToRatio(gRemoteGain);
}

MicManagerXbox::~MicManagerXbox() {}

void MicManagerXbox::RequirePushToTalk(bool b, int pad) {
    CritSecTracker t(&unk68);
    if (b) {
        MILO_ASSERT(pad >=0, 0x2c7);
        mPushToTalkPad = pad;
    } else {
        mPushToTalkPad = -1;
    }
}

void MicManagerXbox::AddMic(MicXbox *mic) {
    FOREACH (it, unk0) {
        if (*it == mic) {
            return;
        }
    }
    unk0.push_back(mic);
    mic->SetChangeNotify(true);
}

void MicManagerXbox::RemoveMic(MicXbox *mic) {
    FOREACH (it, unk0) {
        if (*it == mic) {
            unk0.erase(it);
            mic->SetChangeNotify(false);
            return;
        }
    }
}

void MicManagerXbox::Shutdown() {
    MILO_ASSERT(this == sInstance, 0xF0);
    for (int i = 0; i < 4; i++) {
        RELEASE(unkc[i]);
    }
    if (mXHVEngine) {
        mXHVEngine->Release();
        mXHVEngine = nullptr;
    }
    sInstance = nullptr;
    delete this;
}

MicManagerXbox *MicManagerXbox::GetInstance() {
    if (!sInstance) {
        sInstance = new MicManagerXbox();
    }
    return sInstance;
}

#pragma endregion MicManagerXbox
