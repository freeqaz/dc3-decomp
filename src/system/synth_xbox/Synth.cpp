#include "synth_xbox/Synth.h"
#include "synth_xbox/HeadsetXferEffect.h"
#include "FxSendBitCrush.h"
#include "FxSendChorus.h"
#include "FxSendCompress.h"
#include "FxSendDelay.h"
#include "FxSendDistortion.h"
#include "FxSendEQ.h"
#include "FxSendFlanger.h"
#include "FxSendMeterEffect.h"
#include "synth_xbox/FxSendPitchShift360.h"
#include "FxSendReverb.h"
#include "synth_xbox/FxSendSynapse360.h"
#include "FxSendWah.h"
#include "Synth.h"
#include "macros.h"
#include "math/Decibels.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/BufFile.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/System.h"
#include "synth/BinkReader.h"
#include "synth/StandardStream.h"
#include "synth/StreamNull.h"
#include "synth/VorbisReader.h"
#include "synth/WavReader.h"
#include "utl/MakeString.h"
#include "utl/Str.h"
#include "stl/_algobase.h"
#include "synth/Synth.h"
#include "synth_xbox/ExternalMic.h"
#include "synth_xbox/FxSend.h"
#include "synth_xbox/Mic.h"
#include "synth_xbox/StreamReceiver360.h"
#include "synth_xbox/SynthSample.h"
#include "utl/Std.h"
#include "xdk/xapilibi/xbox.h"
#include "xdk/xaudio2/xaudio2.h"

Synth360 *TheXboxSynth;

static unsigned char sHeadsetSilence[0x100];

Synth *Synth::New() { return new Synth360(); }

Synth360::Synth360()
    : unke8(0), unkec(0), unkf0(0), unkf4(0), unkf8(0), unkfc(0), mDolbyEnabled(true),
      mDolbyPending(false), unk138(false), unk13c(0), unk14c(false) {}

BEGIN_HANDLERS(Synth360)
    HANDLE_ACTION(set_headset_target, Voice::sHeadsetTarget = _msg->Int(2))
    HANDLE_SUPERCLASS(Synth)
END_HANDLERS

void Synth360::PreInit() {}

void Synth360::Init() {
    Synth::Init();
    SynthSample360::Init();
    StreamReceiver360::Init();
    REGISTER_OBJ_FACTORY(FxSendReverb360)
    REGISTER_OBJ_FACTORY(FxSendDelay360)
    REGISTER_OBJ_FACTORY(FxSendCompress360)
    REGISTER_OBJ_FACTORY(FxSendEQ360)
    REGISTER_OBJ_FACTORY(FxSendFlanger360)
    REGISTER_OBJ_FACTORY(FxSendMeterEffect360)
    REGISTER_OBJ_FACTORY(FxSendWah360)
    REGISTER_OBJ_FACTORY(FxSendBitCrush360)
    REGISTER_OBJ_FACTORY(FxSendDistortion360)
    REGISTER_OBJ_FACTORY(FxSendChorus360)
    REGISTER_OBJ_FACTORY(FxSendPitchShift360)
    REGISTER_OBJ_FACTORY(FxSendSynapse360)

    Symbol enableHeadsetSym("enable_headset_output");
    DataArray *synthCfg = SystemConfig(Symbol("synth"));
    if (synthCfg->FindArray(enableHeadsetSym, true)->Int(1)) {
        SetupHeadsetSubmixes();
    }

    float micVolume = 0.0f;
    Symbol volumeSym("volume");
    DataArray *micCfg = SystemConfig(Symbol("synth"), Symbol("mic"));
    micCfg->FindData(volumeSym, micVolume, false);

    if (GetNumMics() > 0) {
        MicManagerXbox::GetInstance()->Init();
        mMics.resize(GetNumMics(), nullptr);
        ExternalMic::Init();
        for (unsigned int i = 0; i < mMics.size(); i++) {
            mMics[i] = new MicXbox(-1, DbToRatio(micVolume));
            ExternalMicClientMgr::Associate(i, dynamic_cast<MicXbox *>(mMics[i]));
        }
    }
}

Mic *Synth360::GetMic(int index) { return mMics[index]; }

bool Synth360::HasPendingVoices() { return Voice::HasPendingVoices(); }

bool Synth360::DidMicsChange() const {
    if (mMics.empty())
        return false;
    else {
        MicManagerXbox *x = MicManagerXbox::GetInstance();
        return x->mMicsChanged;
    }
}

void Synth360::ResetMicsChanged() {
    if (!mMics.empty()) {
        MicManagerXbox *x = MicManagerXbox::GetInstance();
        x->mMicsChanged = false;
    }
}

void Synth360::CaptureMic(int micID) {
    MILO_ASSERT_RANGE(micID, 0, mMics.size(), 0x350);
    MILO_ASSERT(!mMics[micID]->IsInUse(), 0x351);
    mMics[micID]->MarkAsInUse(true);
}

void Synth360::ReleaseAllMics() {
    for (int i = 0; i < mMics.size(); i++) {
        mMics[i]->MarkAsInUse(false);
    }
}

void Synth360::AddFxSend(FxSend360 *fx) { mFxSends.push_back(fx); }

bool Synth360::IsMicConnected(int i) const {
    if (i < 0 || i >= mMics.size())
        return false;
    else {
        return mMics[i]->GetType() != 0;
    }
}

void Synth360::RequirePushToTalk(bool b, int i) {
    if (!mMics.empty()) {
        MicManagerXbox::GetInstance()->RequirePushToTalk(b, i);
    }
}

void Synth360::ReleaseMic(int micID) {
    MILO_ASSERT_RANGE(micID, 0, mMics.size(), 0x35b);
    if (!mMics[micID]->IsInUse()) {
        MILO_NOTIFY_ONCE("Releasing a microphone [%d]that was not in use\n", micID);
    }
    mMics[micID]->MarkAsInUse(false);
}

void Synth360::RemoveFxSend(FxSend360 *fx) {
    auto *findFx = std::find(mFxSends.begin(), mFxSends.end(), fx);
    if (findFx != mFxSends.end()) {
        mFxSends.erase(findFx);
    }
}

IXAudio2SubmixVoice *Synth360::GetHeadsetSubmix(int i) {
    if (!mHeadsetSubmixes.empty() && i != -1) {
        return mHeadsetSubmixes[i];
    }
    return nullptr;
}

int Synth360::GetNextAvailableMicID() const {
    for (int i = 0; i < mMics.size(); i++) {
        if (!mMics[i]->IsInUse() && mMics[i]->GetType() != 0)
            return i;
    }
    return -1;
}

void Synth360::SetupHeadsetSubmixes() {
    // Ensure mHeadsetSubmixes has exactly 4 entries
    std::vector<IXAudio2SubmixVoice *> &submixes = mHeadsetSubmixes;
    if (submixes.size() > 4) {
        submixes.erase(submixes.begin() + 4, submixes.end());
    } else {
        submixes.resize(4, 0);
    }

    // Create a submix voice (with a headset transfer effect) for each headset.
    for (int i = 0; i < 4; i++) {
        HeadsetXferEffect *effect = new HeadsetXferEffect();

        XAUDIO2_EFFECT_DESCRIPTOR effectDesc;
        effectDesc.pEffect = (IUnknown *)effect;
        effectDesc.InitialState = 0;
        effectDesc.OutputChannels = 1;

        XAUDIO2_EFFECT_CHAIN effectChain;
        effectChain.EffectCount = 1;
        effectChain.pEffectDescriptors = &effectDesc;

        int *pEngine = (int *)unkec;
        ((HRESULT(*)(int *, IXAudio2SubmixVoice **, int, int, int, int, int, XAUDIO2_EFFECT_CHAIN *)
        )(*(int *)(*(int *)pEngine + 0x24)))(
            pEngine, &submixes[i], 1, 48000, 0, 0, 0, &effectChain
        );
    }

    // Build the send list that routes everything to the headset submixes.
    std::vector<XAUDIO2_SEND_DESCRIPTOR> sendDescs;

    WAVEFORMATEX format;
    format.wFormatTag = 1;
    for (int i = 0; i < 4; i++) {
        XAUDIO2_SEND_DESCRIPTOR desc;
        desc.Flags = 0;
        desc.pOutputVoice = submixes[i];
        sendDescs.push_back(desc);
    }
    format.cbSize = 0;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = 96000;
    format.wBitsPerSample = 16;
    format.nSamplesPerSec = 48000;
    format.nChannels = 1;

    XAUDIO2_VOICE_SENDS voiceSends;
    voiceSends.pSends = &sendDescs[0];
    voiceSends.SendCount = sendDescs.size();

    IXAudio2SourceVoice *headsetVoice;
    int *pEngine = (int *)unkec;
    HRESULT hr = ((HRESULT(*)(
        int *, IXAudio2SourceVoice **, WAVEFORMATEX *, int, float, int, XAUDIO2_VOICE_SENDS *, int
    ))(*(int *)(*(int *)pEngine + 0x20)))(
        pEngine, &headsetVoice, &format, 0, 2.0f, 0, &voiceSends, 0
    );
    MILO_ASSERT(SUCCEEDED(hr), 0x30a);

    XAUDIO2_BUFFER buffer;
    memset(&buffer.AudioBytes, 0, sizeof(buffer) - 4);
    buffer.LoopCount = 0xff;
    buffer.AudioBytes = 0x100;
    buffer.pAudioData = (const BYTE *)sHeadsetSilence;
    buffer.Flags = 0;
    int *pSourceVoice = (int *)unke8;
    hr = ((HRESULT(*)(int *, XAUDIO2_BUFFER *, int))(*(int *)(*(int *)pSourceVoice + 0x54)))(
        pSourceVoice, &buffer, 0
    );
    MILO_ASSERT(SUCCEEDED(hr), 0x319);

    pSourceVoice = (int *)unke8;
    hr = ((HRESULT(*)(int *, int, int))(*(int *)(*(int *)pSourceVoice + 0x4c)))(pSourceVoice, 0, 0);
    MILO_ASSERT(SUCCEEDED(hr), 0x31c);
}

int Synth360::GetNumConnectedMics() { return ExternalMic::NumConnectedMics(); }

void Synth360::EnableLevels(bool enable) {
    if (!OutputVoice())
        return;
    if (enable) {
        OutputVoice()->EnableEffect(0, 0);
    } else {
        OutputVoice()->DisableEffect(0, 0);
    }
}

bool Synth360::IsUsingDolby() const {
    DWORD speakerConfig;
    XAudioGetSpeakerConfig(&speakerConfig);
    return (speakerConfig >> 16) & 1;
}

void Synth360::UpdateDolby() {
    DWORD speakerConfig;
    XAudioGetSpeakerConfig(&speakerConfig);
    DWORD mask = mDolbyEnabled ? 0x10000 : 0x80000000;
    if ((speakerConfig & mask) != mask) {
        XAudioOverrideSpeakerConfig(mask);
    }
}

void Synth360::SetDolby(bool b1, bool b2) {
    if (b2) {
        mDolbyEnabled = b1;
        UpdateDolby();
    } else if (mDolbyEnabled != b1) {
        mDolbyTimer.Restart();
        mDolbyEnabled = b1;
        mDolbyPending = true;
    }
}

void Synth360::NewStreamFile(const char *name, File *&file, Symbol &sym) {
    String bikPath(MakeString("%s.bik", name));
    String moggPath(MakeString("%s.mogg", name));
    String wavPath(MakeString("%s.wav", name));
    file = NewFile(bikPath.c_str(), 2);
    if (file) {
        static Symbol bik("bik");
        sym = bik;
    } else {
        file = NewFile(moggPath.c_str(), 2);
        if (file) {
            static Symbol mogg("mogg");
            sym = mogg;
        } else {
            file = NewFile(wavPath.c_str(), 2);
            if (file) {
                static Symbol wav("wav");
                sym = wav;
            } else {
                Synth::NewStreamFile(name, file, sym);
            }
        }
    }
}

Stream *Synth360::NewStream(const char *name, float volume, float pan, bool b) {
    File *file;
    Symbol sym;
    NewStreamFile(name, file, sym);
    if (file) {
        return new StandardStream(file, volume, pan, sym, b, true, false);
    }
    TheDebug.Notify(MakeString("couldn't find stream %s", name));
    return new StreamNull(volume);
}

Stream *Synth360::NewBufStream(const void *buf, int size, Symbol ext, float startMs, bool b) {
    return new StandardStream(new BufFile(buf, size), startMs, 0.0f, ext, false, b, false);
}

StreamReader *Synth360::NewStreamDecoder(File *file, StandardStream *stream, Symbol ext) {
    if (ext == "bik") {
        return new BinkReader(file, stream);
    } else if (ext == "mogg") {
        return new VorbisReader(file, true, stream, true);
    } else if (ext == "wav") {
        return new WavReader(file, stream);
    } else {
        TheDebug.Fail(MakeString("bad decoder type: %s", ext), nullptr);
        return nullptr;
    }
}
