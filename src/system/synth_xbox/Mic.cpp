#include "synth_xbox\Mic.h"
#include "macros.h"
#include "math\Decibels.h"
#include "math\Utl.h"
#include "obj\Data.h"
#include "obj\DataFunc.h"
#include "os\CritSec.h"
#include "os\Debug.h"
#include "os\Joypad.h"
#include "os\PlatformMgr.h"
#include "os\System.h"
#include "rnddx9\Rnd.h"
#include "synth\MicClientMapper.h"
#include "synth\MicManagerInterface.h"
#include "synth\Synth.h"
#include "synth_xbox\ExternalMic.h"
#include "synth_xbox\FxSend.h"
#include "synth_xbox\GainEffect.h"
#include "synth_xbox\HeadsetPlaybackEffect.h"
#include "synth_xbox\HeadsetXferEffect.h"
#include "synth_xbox\Voice.h"
#include "utl\MemStream.h"
#include "utl\Std.h"
#include "utl\Symbol.h"
#include <cmath>
#include <cstring>

extern "C" void XMemCpy(void *, const void *, int);

// Target: Mic.obj .bss:0x0 (0x8316C854), zero.  This was a file-scope global that
// no code could reach: every use is inside a MicManagerXbox member, where
// unqualified lookup finds the class member first -- which was declared and
// defined nowhere.
MicManagerXbox *MicManagerXbox::sInstance;

// Target: Mic.obj .bss:0xC/0x10 (0x8316C860/64), both zero, in this order.
// Unnamed in the map (dtk names them lbl_<addr>); the Poll() counters declared
// `extern "C" int` in synth_xbox/StreamReceiver360.cpp.
extern "C" {
int lbl_8316C860;
int lbl_8316C864;
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

void ChatReceiver::ProcessChatData(void *data, unsigned int size, int *flag) {
    // pi / 8000 in float precision (0x39cde32e); 0.000392699f is a ulp short.
    float w = gLowCut * 0.0003926991f;
    float b1 = Sine(w + 1.5707964f) * -2.0f;
    float a1 = -b1;
    float disc = b1 * b1 - (Sine(w + 1.5707964f) * 8.0f - 7.0f) * 4.0f;
    float coef = (a1 - sqrtf(disc)) * 0.5f;
    float gain = (coef + 1.0f) * 0.5f;

    float z1 = unkc;
    float z2 = unk10;
    unsigned int samps = size >> 1;
    if (samps != 0) {
        short *p = (short *)data - 1;
        for (unsigned int i = 0; i != samps; i++) {
            float in = (float)p[1];
            float out = (in - z1) * gain * 2.0f + z2 * coef;
            z1 = in;
            out = Clamp(-32767.0f, 32767.0f, out);
            p++;
            *p = (short)out;
            z2 = (float)(short)out;
        }
    }
    unk10 = z2;
    unkc = z1;

    short maxSamp = 0;
    short minSamp = 0;
    *flag = 1;
    float localRatio = DbToRatio(gLocalGain);
    if (samps != 0) {
        short *p = (short *)data - 1;
        for (unsigned int i = 0; i < samps; i++) {
            short s = p[1];
            if (s >= maxSamp) {
                maxSamp = s;
            }
            if (s < minSamp) {
                minSamp = s;
            }
            p++;
            p[0] = (short)((float)s * localRatio);
        }
    }

    int newCount;
    if ((float)maxSamp * (1.0f / 32767.0f) > DbToRatio(gNoiseThreshold) ||
        (float)minSamp * (1.0f / 32767.0f) < -DbToRatio(gNoiseThreshold)) {
        newCount = gNoiseInt;
    } else if (unk14 > 0) {
        newCount = unk14 - 1;
    } else {
        *flag = 0;
        return;
    }
    unk14 = newCount;
    if (*flag != 0) {
        unk18 = 5;
    }
}

#pragma region MicXbox

MicXbox::MicXbox(int, float volume)
    : mRunning(false), unk10(0), mChangeNotify(false), mPlaybackVoice(0), unk301c(mPlaybackBuffer),
      unk9054(1.0f), unk9058(0), unk905c(0), mFxSend(0), mVolume(volume), mMute(false),
      unk906c(0), mGain(1.0f), mOutputGain(1.0f), mSensitivity(1.0f), unk907c(0),
      mDroppedSamples(0), mDeviceName("generic_usb"), mClipping(false) {
    unk302c.Init(0xc00);
    unk3040.Init(0x6000);
    unk3020.reserve(0x1800);
    memset(mPlaybackBuffer, 0, 0x3000);
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
        unk301c = mPlaybackBuffer;
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
    mPlaybackVoice->SetData(mPlaybackBuffer, sizeof(mPlaybackBuffer), 0);
    mPlaybackVoice->SetLoopRegion(0, -1);
    mPlaybackVoice->SetSend(dynamic_cast<FxSend360 *>(mFxSend));
    mPlaybackVoice->Start();
    mPlaybackVoice->SetVolume(0);
}

void MicXbox::StopPlayback() {
    CritSecTracker t(&MicManagerXbox::GetInstance()->unk68);
    RELEASE(mPlaybackVoice);
    memset(mPlaybackBuffer, 0, sizeof(mPlaybackBuffer));
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

MicrophonesChangedMsg::MicrophonesChangedMsg(bool wasConnected) : Message(Type(), wasConnected) {}

// Keeps the playback voice's read cursor a fixed distance behind our write
// cursor by nudging its playback speed. The nominal lead is 1800 samples on a
// wired headset and 2700 on a wireless one (unkc); both the 6144-sample
// wrap window and the +-12288 unwrap match the 6144-sample playback buffer.
void MicXbox::Poll() {
    if (mPlaybackVoice && mPlaybackVoice->IsPlaying()) {
        int written = (char *)unk301c - (char *)mPlaybackBuffer;
        int lag = written - mPlaybackVoice->GetAddr();

        unk905c = ModRange(unk905c - 6144.0f, unk905c + 6144.0f, (float)lag);
        unk9058 = unk9058 * 0.9f + unk905c * 0.1f;
        if (unk905c > 12288.0f && unk9058 > 12288.0f) {
            unk905c -= 12288.0f;
            unk9058 -= 12288.0f;
        }
        if (unk905c < -12288.0f && unk9058 < -12288.0f) {
            unk905c += 12288.0f;
            unk9058 += 12288.0f;
        }

        float lead = ModRange(
            (unkc ? 2700.0f : 1800.0f) - 600.0f,
            (unkc ? 2700.0f : 1800.0f) - 600.0f + 12288.0f, unk9058
        );
        float volume = mMute ? 0.0f : mVolume;

        if (lead > (unkc ? 2700.0f : 1800.0f) + 600.0f) {
            // Far out of range: jump hard in whichever direction we are already
            // heading and mute until it settles.
            unk9054 = unk9054 > 1.0f ? 1.08f : 0.92f;
            volume = 0.0f;
        } else if (lead > (unkc ? 2700.0f : 1800.0f) + 150.0f) {
            unk9054 = 1.0002f;
        } else if (lead < (unkc ? 2700.0f : 1800.0f) - 150.0f) {
            unk9054 = 0.99979f;
        } else if (lead > (unkc ? 2700.0f : 1800.0f) + 300.0f) {
            unk9054 = 1.00059f;
        } else if (lead < (unkc ? 2700.0f : 1800.0f) - 300.0f) {
            unk9054 = 0.99941f;
        } else if ((unk9054 > 1.0f && lead < (unkc ? 2700.0f : 1800.0f) * 0.5f)
                   || (unk9054 < 1.0f && lead > (unkc ? 2700.0f : 1800.0f) * 0.5f)) {
            unk9054 = 1.0f;
        }

        mPlaybackVoice->SetVolume(volume);
        mPlaybackVoice->SetSpeed(unk9054);
    }

    if (mChangeNotify) {
        if (GetType() != unk10) {
            MicrophonesChangedMsg msg(unk10 != 0);
            ThePlatformMgr.Handle(msg, false);
            unk10 = GetType();
        }
    }
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
// ham_xbox_r.map folds MicXbox::GetSampleRate into MicNull::GetSampleRate at
// 0x82E3DC20, whose body is `lis r3,0; ori r3,r3,0xbb80` = 48000, not 16000.
int MicXbox::GetSampleRate() const { return 48000; }

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

void MicXbox::AddData(void *data, int bytes) {
    CritSecTracker t(&MicManagerXbox::GetInstance()->unk68);
    MILO_ASSERT((bytes&1) == 0, 0x344);
    if (mOutputGain != 1.0f) {
        int n = bytes / 2;
        if (n > 0) {
            short *p = (short *)data;
            do {
                float prod = (float)*p * mOutputGain;
                float rounded = (float)floor(prod + 0.5f);
                *p = (short)Clamp(-32767.0f, 32767.0f, rounded);
                p++;
            } while (--n);
        }
    }
    if (mPlaybackVoice) {
        short *bufEnd = mPlaybackBuffer + 6144;
        if ((char *)unk301c + bytes <= (char *)bufEnd) {
            XMemCpy(unk301c, data, bytes);
            unk301c = (short *)((char *)unk301c + bytes);
        } else {
            int firstPart = (char *)bufEnd - (char *)unk301c;
            XMemCpy(unk301c, data, firstPart);
            int remaining = bytes - firstPart;
            XMemCpy(mPlaybackBuffer, (char *)data + firstPart, remaining);
            unk301c = (short *)((char *)mPlaybackBuffer + remaining);
        }
        if (!mPlaybackVoice->IsPlaying() &&
            (char *)unk301c - (char *)mPlaybackBuffer >= 0xf00) {
            mPlaybackVoice->SetVolume(mVolume);
        }
    }
    AddToBuffer(unk3020, data, bytes, 0);
    unk302c.Write(data, bytes);
    mDroppedSamples = unk3040.Write(data, bytes);
}

void MicXbox::ReadChatBuffer(void *data, unsigned int size) {
    MILO_ASSERT(size < DIM(mPlaybackBuffer), 0x2d6);
    if (ExternalMicClientMgr::ConnectedForClient(this)) {
        unsigned int samps = size / 2;
        if ((int)(unk3020.size()) >= samps * 3) {
            short *out = (short *)data;
            for (unsigned int i = 0; i < samps; i++) {
                out[i] = unk3020[i * 3];
            }
            unk3020.erase(unk3020.begin(), unk3020.begin() + samps * 3);
        }
    }
}

bool MicXbox::AddToBuffer(std::vector<short> &buf, void *data, int bytes, int *dropped) {
    unsigned int samps = bytes / 2;
    bool overflowed = false;
    MILO_ASSERT(samps <= buf.capacity(), 0x3ac);
    if (buf.size() + samps > buf.capacity()) {
        if (dropped) {
            *dropped += buf.size();
        }
        buf.erase(buf.begin(), buf.end());
        overflowed = true;
    }
    short zero = 0;
    unsigned int oldSize = buf.size();
    if (oldSize + samps < oldSize) {
        buf.erase(buf.begin() + (oldSize + samps), buf.end());
    } else {
        buf.insert(buf.end(), (oldSize + samps) - oldSize, zero);
    }
    XMemCpy(&buf[oldSize], data, bytes);
    return overflowed;
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

void MicManagerXbox::Init() {
    MILO_ASSERT(this == sInstance, 0xB8);

    void *processingModes[2];
    XAUDIO2_EFFECT_CHAIN chain;
    XAUDIO2_EFFECT_DESCRIPTOR desc;
    HeadsetXferEffect *xfer[4];
    XHV2INIT init;

    processingModes[0] = _xhv_voicechat_mode;
    processingModes[1] = _xhv_loopback_mode;

    memset(&init, 0, sizeof(init));
    init.MaxRemoteTalkers = 5;
    init.MaxLocalTalkers = 4;
    init.LocalProcessingModes = processingModes;
    init.NumLocalProcessingModes = 2;
    init.RemoteProcessingModes = processingModes;
    init.NumRemoteProcessingModes = 1;
    init.MaxNumPackets = 1;
    init.Unk1c = 1;
    init.pfnMicrophoneRawDataReady = DataReadyCallback;
    init.Unk30 = TheXboxSynth->unkec;

    HRESULT hr = XHV2CreateEngine(&init, (DWORD *)&unk2c, &mXHVEngine);
    DX_ASSERT_CODE(hr, 0xCD);

    if (!TheXboxSynth->mHeadsetSubmixes.empty()) {
        // One HeadsetXferEffect capture ring per player, pulled back out of the
        // submix it was installed on.
        for (int i = 0; i < 4; i++) {
            IXAudio2SubmixVoice *submix = TheXboxSynth->GetHeadsetSubmix(i);
            HeadsetXferEffect *effect;
            HRESULT hr = submix->GetEffectParameters(0, &effect, sizeof(effect));
            MILO_ASSERT(SUCCEEDED(hr), 0xD9);
            xfer[i] = effect;
        }
        HeadsetPlaybackEffect *playback = new HeadsetPlaybackEffect(xfer);

        desc.pEffect = static_cast<IXAPO *>(playback);
        desc.InitialState = 0;
        desc.OutputChannels = 1;
        chain.EffectCount = 1;
        chain.pEffectDescriptors = &desc;
        AddRemoteMic(0x00DEADBEEFFACEF0ULL, &chain);
    }

    for (int i = 0; i < 4; i++) {
        unkc[i] = new ChatReceiver(mXHVEngine, i);
    }
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

void MicManagerXbox::Poll() {
    FOREACH (it, unk0) {
        (*it)->Poll();
    }
    FOREACH (it, unkc) {
        ChatReceiver *receiver = *it;
        if (receiver->unk18 >= 2) {
            receiver->unk18--;
        } else {
            receiver->unk18 = 0;
        }
    }
    FOREACH (it, unk20) {
        ChatBuffer &cb = *it;
        if (cb.unk8[250] != 0) {
            UINT32 count = cb.unk8[250];
            mXHVEngine->SubmitIncomingChatData(*(UINT64 *)&cb, (unsigned char *)cb.unk8, &count);
            cb.unk8[250] -= count;
            memcpy(cb.unk8, (char *)cb.unk8 + count, cb.unk8[250]);
        } else if (!TheXboxSynth->mHeadsetSubmixes.empty() &&
                   *(UINT64 *)&cb == 0x00DEADBEEFFACEF0ULL) {
            unk38.Split();
            if (!unk38.Running() || unk38.Ms() > 2000.0f) {
                unsigned char buf[0x14];
                memset(buf, 0, sizeof(buf));
                UINT32 count = sizeof(buf);
                mXHVEngine->SubmitIncomingChatData(*(UINT64 *)&cb, buf, &count);
                unk38.Restart();
            }
        }
    }
}

void MicManagerXbox::AddRemoteMic(unsigned long long const &xuid,
                                 XAUDIO2_EFFECT_CHAIN *chain) {
    bool noChain = chain == 0;
    GainEffect *gainEffect = new GainEffect();

    XAUDIO2_EFFECT_DESCRIPTOR desc;
    desc.OutputChannels = 1;
    // Via IXAPO (the CXAPOBase sub-object at offset 0) -- GainEffect reaches
    // IUnknown through both IXAPO and IXAPOParameters, so a direct cast is
    // ambiguous. Same idiom as Synth.cpp's HeadsetXferEffect chain.
    desc.pEffect = static_cast<IXAPO *>(gainEffect);
    XAUDIO2_EFFECT_CHAIN effectChain;
    effectChain.EffectCount = 1;
    effectChain.pEffectDescriptors = &desc;
    desc.InitialState = 0;

    HRESULT hr = mXHVEngine->RegisterRemoteTalker(
        xuid, noChain ? &effectChain : 0, chain, 0);
    DX_ASSERT_CODE(hr, 0x150);

    void *mode = _xhv_voicechat_mode;
    hr = mXHVEngine->StartRemoteProcessingModes(xuid, &mode, 1);
    DX_ASSERT_CODE(hr, 0x155);

    ChatBuffer chatBuffer;
    *(unsigned long long *)&chatBuffer = xuid;
    chatBuffer.unk8[250] = 0;
    unk20.push_back(chatBuffer);

    gainEffect->Release();
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

void MicManagerXbox::DataReadyCallback(unsigned long userIndex, void *data,
                                       unsigned long dataSize, int *flag) {
    MILO_ASSERT(sInstance, 0x183);
    sInstance->OnDataReady(userIndex, data, dataSize, flag);
}

void MicManagerXbox::OnDataReady(unsigned long userIndex, void *data, unsigned long dataSize,
                                 int *flag) {
    MILO_ASSERT(userIndex >= 0 && userIndex < 4, 0x18a);
    CritSecTracker t(&unk68);
    ChatReceiver *receiver = unkc[userIndex];
    MILO_ASSERT(receiver, 0x191);
    if (receiver->unk8) {
        if ((int)receiver->mXHV->IsHeadsetPresent(receiver->unk4)) {
            if ((unsigned int)unk18 == userIndex) {
                unk18 = -1;
            }
        } else {
            if (unk18 == -1) {
                unk18 = userIndex;
            }
            if ((unsigned int)unk18 == userIndex) {
                if (mPushToTalkPad == -1 ||
                    (JoypadGetPadData(mPushToTalkPad)->mButtons & 1) ||
                    JoypadGetPadData(mPushToTalkPad)->Pressed(kPad_R2)) {
                    MicClientMapper *mapper = TheSynth->GetMicClientMapper();
                    for (int i = 0; i < 4; i++) {
                        MicClientID id(i, -1);
                        int micID = mapper->GetMicIDForClientID(id);
                        if (micID != -1) {
                            MicXbox *mic = (MicXbox *)TheSynth->GetMic(micID);
                            if (mic) {
                                mic->ReadChatBuffer(data, dataSize);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    if ((int)receiver->mXHV->IsSharedMicPresent(receiver->unk4) == 0) {
        receiver->ProcessChatData(data, dataSize, flag);
    } else {
        MicXbox *mic = (MicXbox *)TheSynth->GetMic(0);
        if (mic) {
            mic->AddData(data, dataSize);
        }
        *flag = 1;
    }
}

#pragma endregion MicManagerXbox
