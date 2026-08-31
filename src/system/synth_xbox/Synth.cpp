#include "synth_xbox\Synth.h"
#include "synth\CompressionEffect.h"
#include "synth_xbox\HeadsetXferEffect.h"
#include "FxSendBitCrush.h"
#include "FxSendChorus.h"
#include "FxSendCompress.h"
#include "FxSendDelay.h"
#include "FxSendDistortion.h"
#include "FxSendEQ.h"
#include "FxSendFlanger.h"
#include "FxSendMeterEffect.h"
#include "synth_xbox\FxSendPitchShift360.h"
#include "FxSendReverb.h"
#include "synth_xbox\FxSendSynapse360.h"
#include "FxSendWah.h"
#include "Synth.h"
#include "macros.h"
#include "math\Decibels.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "os/BufFile.h"
#include "os\Debug.h"
#include "os\File.h"
#include "os\System.h"
#include "synth\BinkReader.h"
#include "synth\StandardStream.h"
#include "synth\StreamNull.h"
#include "synth\VorbisReader.h"
#include "synth\WavReader.h"
#include "utl\MakeString.h"
#include "utl\Str.h"
#include "stl\_algobase.h"
#include "synth\Synth.h"
#include "synth_xbox\ExternalMic.h"
#include "synth_xbox\FxSend.h"
#include "synth_xbox\Mic.h"
#include "synth_xbox\Voice.h"
#include "synth_xbox\StreamReceiver360.h"
#include "synth_xbox\SynthSample.h"
#include "utl\Std.h"
#include "xdk\xapilibi\xbox.h"
#include "xdk\xaudio2\xaudio2.h"
#include "xdk\xaudio2\xaudio2fx.h"
#include "xdk\LIBCMT\math.h"

Synth360 *TheXboxSynth;

void ReverbConvertI3DL2ToNative(
    const XAUDIO2FX_REVERB_I3DL2_PARAMETERS *pI3DL2, XAUDIO2FX_REVERB_PARAMETERS *pNative
) {
    pNative->PositionMatrixLeft = 27;
    pNative->PositionMatrixRight = 27;
    pNative->PositionLeft = 6;
    pNative->PositionRight = 6;
    pNative->HighEQCutoff = 6;
    pNative->RoomSize = 100.0f;
    pNative->RearDelay = 5;
    pNative->LowEQCutoff = 4;
    pNative->RoomFilterMain = pI3DL2->Room * 0.01f;
    pNative->RoomFilterHF = pI3DL2->RoomHF * 0.01f;

    if (pI3DL2->DecayHFRatio >= 1.0f) {
        int gain = (int)((float)log10(pI3DL2->DecayHFRatio) * -4.0);
        if (gain < -8)
            gain = -8;
        pNative->LowEQGain = (gain < 0) ? gain + 8 : 8;
        pNative->HighEQGain = 8;
        pNative->DecayTime = pI3DL2->DecayTime * pI3DL2->DecayHFRatio;
    } else {
        int gain = (int)((float)log10(pI3DL2->DecayHFRatio) * 4.0);
        if (gain < -8)
            gain = -8;
        pNative->LowEQGain = 8;
        pNative->HighEQGain = (gain < 0) ? gain + 8 : 8;
        pNative->DecayTime = pI3DL2->DecayTime;
    }

    float reflectionsDelay = pI3DL2->ReflectionsDelay * 1000.0f;
    if (reflectionsDelay >= 300.0f) {
        reflectionsDelay = 299.0f;
    } else if (reflectionsDelay <= 1.0f) {
        reflectionsDelay = 1.0f;
    }
    pNative->ReflectionsDelay = (unsigned int)reflectionsDelay;

    float reverbDelay = pI3DL2->ReverbDelay * 1000.0f;
    if (reverbDelay >= 85.0f) {
        reverbDelay = 84.0f;
    }
    pNative->ReverbDelay = (BYTE)reverbDelay;

    pNative->ReflectionsGain = pI3DL2->Reflections * 0.01f;
    pNative->ReverbGain = pI3DL2->Reverb * 0.01f;
    pNative->EarlyDiffusion = (BYTE)(pI3DL2->Diffusion * 0.15f);
    pNative->LateDiffusion = pNative->EarlyDiffusion;
    pNative->Density = pI3DL2->Density;
    pNative->RoomFilterFreq = pI3DL2->HFReference;
    pNative->WetDryMixPct = 0;
    pNative->WetDryMix = pI3DL2->WetDryMix;
}

static unsigned char sHeadsetSilence[0x100];

Synth *Synth::New() { return new Synth360(); }

Synth360::Synth360()
    : unke8(0), unkec(0), unkf0(0), unkf4(0), unkf8(0), unkfc(0), mDolbyEnabled(true),
      mDolbyPending(false), unk138(false), unk13c(0), unk14c(false) {}

BEGIN_HANDLERS(Synth360)
    HANDLE_ACTION(set_headset_target, Voice::sHeadsetTarget = _msg->Int(2))
    HANDLE_SUPERCLASS(Synth)
END_HANDLERS

// Not reconstructed (0x564 bytes at 0x82E30650). It builds the LevelData table,
// calls XAudio2Create, creates the mastering voice and attaches its effect chain
// -- a StandardEffect<CompressionEffect> plus a MeterEffect -- then reads the
// limiter settings out of SystemConfig("limiter", "synth"). See the note at the
// bottom of this file: this stub is why CompressionEffect's instantiation has to
// be forced by hand here.
void Synth360::PreInit() {}

void Synth360::Poll() {
    START_AUTO_TIMER("synth");

    ((IXAudio2Voice *)unkf0)->SetEffectParameters(1, (const void *)unk13c, 4, 0);

    static float gainCenter = DbToRatio(-3.0f);
    static float gainSide = DbToRatio(-1.2f);
    static float gainRear = DbToRatio(-6.2f);

    mLevelData[7].mRMS = mLevelData[2].mRMS * gainCenter +
        (mLevelData[5].mRMS * gainRear + mLevelData[4].mRMS * gainSide) + mLevelData[0].mRMS;
    mLevelData[7].mPeak = mLevelData[2].mPeak * gainCenter +
        (mLevelData[5].mPeak * gainRear + mLevelData[4].mPeak * gainSide) + mLevelData[0].mPeak;
    mLevelData[8].mRMS = mLevelData[2].mRMS * gainCenter +
        (mLevelData[5].mRMS * gainSide + mLevelData[4].mRMS * gainRear) + mLevelData[1].mRMS;
    mLevelData[8].mPeak = mLevelData[2].mPeak * gainCenter +
        (mLevelData[5].mPeak * gainSide + mLevelData[4].mPeak * gainRear) + mLevelData[1].mPeak;

    Synth::Poll();

    if (!mMics.empty()) {
        MicManagerXbox::GetInstance()->Poll();
    }

    if (mDolbyTimer.Running()) {
        float ms = mDolbyTimer.SplitMs();
        float volume;
        if (ms < 300.0f) {
            volume = ms * -0.32f;
        } else if (ms < 600.0f) {
            volume = -96.0f;
        } else if (mDolbyPending) {
            UpdateDolby();
            mDolbyPending = false;
            volume = -96.0f;
        } else if (ms < 900.0f) {
            volume = -96.0f;
        } else if (ms < 1800.0f) {
            volume = (1800.0f - ms) * -0.10666667f;
        } else {
            mDolbyTimer.Reset();
            volume = 0.0f;
        }
        SetMasterVolume(volume);
    }

    StartSynchronizedVoices();
    StopSynchronizedVoices();
    VorbisReader::SignalDecodeThread();
}

void Synth360::Terminate() {
    for (unsigned int i = 0; i < mFxSends.size(); i++) {
        mFxSends[i]->CleanChain();
    }
    TerminateVoiceThread();
    TheXboxSynth = nullptr;
    Synth::Terminate();
    ExternalMic::Terminate();

    std::for_each(mMics.begin(), mMics.end(), Delete());
    if (!mMics.empty()) {
        MicManagerXbox::GetInstance()->Shutdown();
    }

    if (!mHeadsetSubmixes.empty()) {
        ((IXAudio2SourceVoice *)unke8)->Stop(0, 0);
        ((IXAudio2SourceVoice *)unke8)->DestroyVoice();
        unke8 = 0;
        for (unsigned int i = 0; i < mHeadsetSubmixes.size(); i++) {
            mHeadsetSubmixes[i]->DestroyVoice();
        }
        mHeadsetSubmixes.erase(mHeadsetSubmixes.begin(), mHeadsetSubmixes.end());
    }

    if ((IXAudio2Voice *)unkf8) {
        ((IXAudio2Voice *)unkf8)->DestroyVoice();
        unkf8 = 0;
    }
    if ((IXAudio2Voice *)unkf4) {
        ((IXAudio2Voice *)unkf4)->DestroyVoice();
        unkf4 = 0;
    }
    if ((IXAudio2Voice *)unkfc) {
        ((IXAudio2Voice *)unkfc)->DestroyVoice();
        unkfc = 0;
    }
    if ((IXAudio2Voice *)unkf0) {
        ((IXAudio2Voice *)unkf0)->DestroyVoice();
        unkf0 = 0;
    }
    if ((IUnknown *)unkec) {
        ((IUnknown *)unkec)->Release();
    }
    delete (int *)unk13c;
    unk13c = 0;
}

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
    if (SystemConfig(Symbol("synth"))->FindArray(enableHeadsetSym, true)->Int(1)) {
        SetupHeadsetSubmixes();
    }

    float micVolume = 0.0f;
    SystemConfig(Symbol("synth"), Symbol("mic"))->FindData(Symbol("volume"), micVolume, false);

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
        // Via IXAPO (the CXAPOBase sub-object at offset 0) -- HeadsetXferEffect
        // reaches IUnknown through both IXAPO and IXAPOParameters, so a direct
        // cast is ambiguous. The target stores the pointer unadjusted.
        effectDesc.pEffect = static_cast<IXAPO *>(effect);
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
    if ((unsigned int)unkf0 == 0)
        return;
    if (enable) {
        ((IXAudio2Voice *)unkf0)->EnableEffect(0, 0);
    } else {
        ((IXAudio2Voice *)unkf0)->DisableEffect(0, 0);
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

// The target links CompressionEffect's whole CSampleXAPOBase instantiation --
// all 18 symbols, vtables and RTTI included -- out of Synth.obj, not out of
// FxSendCompress.obj where FxSendCompress360::CreateFx lives.
//
// THE CALL SITE IS Synth360::PreInit, which is still a stub above. In the target
// it builds the master voice's effect chain, and its first descriptor is a
// heap-allocated StandardEffect<CompressionEffect> -- the "limiter":
//
//     82E3075C  li      r3, 0xd4                    ; sizeof(StandardEffect<CompressionEffect>)
//     82E30760  bl      ??2@YAPAXI@Z                ; operator new
//     82E30774  bl      ??0?$StandardEffect@VCompressionEffect@@@@QAA@XZ
//     82E30788  stw     r3,  0x80(r31)              ; desc.pEffect
//     82E3078C  stw     r29, 0x84(r31)              ; desc.InitialState = 1
//     82E30794  stw     r28, 0x88(r31)              ; desc.OutputChannels = 6
//
// (the next descriptor is a MeterEffect, `li r3, 0x94` at 82E30790, and the
// chain is followed by the SystemConfig("limiter", "synth") reads at 82E30804).
// That is the only reference to the class anywhere in Synth.obj, and it is why
// Synth.obj wins the COMDAT for the whole instantiation: FxSendCompress360::
// CreateFx only emits a folding copy. It is NOT a factory, a registration table
// or a sizeof -- it is one `new` in the master chain.
//
// Synth360::PreInit is 0x564 bytes and is not reconstructed, so the reference
// does not exist in this build yet. Until it does, instantiate the registration
// block explicitly so this object defines the symbol the target's does. THIS IS
// A PLACEHOLDER FOR PreInit, not source truth -- delete it when PreInit lands.
namespace ATG {
template XAPO_REGISTRATION_PROPERTIES
    CSampleXAPOBase<CompressionEffect, CompressionEffect::Params>::m_regProps;
}
