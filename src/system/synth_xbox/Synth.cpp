#include "synth_xbox\Synth.h"
#include "synth\CompressionEffect.h"
#include "synth_xbox\HeadsetXferEffect.h"
#include "synth_xbox\MeterEffect.h"
#include "dsp\StandardEffect.h"
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

// The XAudio2 engine interface. Only the three factory slots this file needs are
// spelled out; slots 0-7 (IUnknown + GetDeviceCount/GetDeviceDetails/Initialize/
// RegisterForCallbacks/UnregisterForCallbacks) are padded so CreateSourceVoice
// lands on vtable index 8 (0x20), CreateSubmixVoice on 9 (0x24) and
// CreateMasteringVoice on 10 (0x28) -- the slots Synth360::PreInit and
// Synth360::SetupHeadsetSubmixes call. Abstract and never constructed, so no
// vtable or RTTI is emitted for it.
struct IXAudio2 {
    virtual HRESULT QueryInterface(const void *, void **) = 0;
    virtual UINT32 AddRef() = 0;
    virtual UINT32 Release() = 0;
    virtual HRESULT GetDeviceCount(UINT32 *) = 0;
    virtual HRESULT GetDeviceDetails(UINT32, void *) = 0;
    virtual HRESULT Initialize(UINT32, UINT32) = 0;
    virtual HRESULT RegisterForCallbacks(void *) = 0;
    virtual void UnregisterForCallbacks(void *) = 0;
    virtual HRESULT CreateSourceVoice(
        IXAudio2Voice **,
        const void *,
        UINT32,
        float,
        void *,
        const XAUDIO2_VOICE_SENDS *,
        const XAUDIO2_EFFECT_CHAIN *
    ) = 0;
    virtual HRESULT CreateSubmixVoice(
        IXAudio2Voice **,
        UINT32,
        UINT32,
        UINT32,
        UINT32,
        const XAUDIO2_VOICE_SENDS *,
        const XAUDIO2_EFFECT_CHAIN *
    ) = 0;
    virtual HRESULT CreateMasteringVoice(
        IXAudio2Voice **, UINT32, UINT32, UINT32, UINT32, const XAUDIO2_EFFECT_CHAIN *
    ) = 0;
};

extern "C" HRESULT XAudio2Create(IXAudio2 **, UINT32, UINT32);

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

namespace {
    // One I3DL2 environmental reverb preset paired with the Symbol name it answers to.
    // Same 0x38-byte layout as the table in FxSendReverb.cpp -- both TUs carry their
    // own copy, exactly as the target does.
    struct ReverbPreset {
        Symbol name;
        XAUDIO2FX_REVERB_I3DL2_PARAMETERS params;
    };
}

void Synth360::SetGlobalReverbPreset(const char *preset) {
    // Built once on first call (static-local guard).
    static ReverbPreset presets[] = {
        { Symbol("default"),          { 100.0f, -10000,     0, 0.0f,  1.00f, 0.50f, -10000, 0.020f, -10000, 0.040f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("generic"),          { 100.0f,  -1000,  -100, 0.0f,  1.49f, 0.83f,  -2602, 0.007f,    200, 0.011f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("padded_cell"),      { 100.0f,  -1000, -6000, 0.0f,  0.17f, 0.10f,  -1204, 0.001f,    207, 0.002f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("room"),             { 100.0f,  -1000,  -454, 0.0f,  0.40f, 0.83f,  -1646, 0.002f,     53, 0.003f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("bath_room"),        { 100.0f,  -1000, -1200, 0.0f,  1.49f, 0.54f,   -370, 0.007f,   1030, 0.011f, 100.0f,  60.0f, 5000.0f } },
        { Symbol("living_room"),      { 100.0f,  -1000, -6000, 0.0f,  0.50f, 0.10f,  -1376, 0.003f,  -1104, 0.004f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("stone_room"),       { 100.0f,  -1000,  -300, 0.0f,  2.31f, 0.64f,   -711, 0.012f,     83, 0.017f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("auditorium"),       { 100.0f,  -1000,  -476, 0.0f,  4.32f, 0.59f,   -789, 0.020f,   -289, 0.030f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("concert_hall"),     { 100.0f,  -1000,  -500, 0.0f,  3.92f, 0.70f,  -1230, 0.020f,     -2, 0.029f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("cave"),             { 100.0f,  -1000,     0, 0.0f,  2.91f, 1.30f,   -602, 0.015f,   -302, 0.022f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("arena"),            { 100.0f,  -1000,  -698, 0.0f,  7.24f, 0.33f,  -1166, 0.020f,     16, 0.030f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("hangar"),           { 100.0f,  -1000, -1000, 0.0f, 10.05f, 0.23f,   -602, 0.020f,    198, 0.030f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("carpeted_hallway"), { 100.0f,  -1000, -4000, 0.0f,  0.30f, 0.10f,  -1831, 0.002f,  -1630, 0.030f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("hallway"),          { 100.0f,  -1000,  -300, 0.0f,  1.49f, 0.59f,  -1219, 0.007f,    441, 0.011f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("stone_corridor"),   { 100.0f,  -1000,  -237, 0.0f,  2.70f, 0.79f,  -1214, 0.013f,    395, 0.020f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("alley"),            { 100.0f,  -1000,  -270, 0.0f,  1.49f, 0.86f,  -1204, 0.007f,     -4, 0.011f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("forest"),           { 100.0f,  -1000, -3300, 0.0f,  1.49f, 0.54f,  -2560, 0.162f,   -613, 0.088f,  79.0f, 100.0f, 5000.0f } },
        { Symbol("city"),             { 100.0f,  -1000,  -800, 0.0f,  1.49f, 0.67f,  -2273, 0.007f,  -2217, 0.011f,  50.0f, 100.0f, 5000.0f } },
        { Symbol("mountains"),        { 100.0f,  -1000, -2500, 0.0f,  1.49f, 0.21f,  -2780, 0.300f,  -2014, 0.100f,  27.0f, 100.0f, 5000.0f } },
        { Symbol("quarry"),           { 100.0f,  -1000, -1000, 0.0f,  1.49f, 0.83f, -10000, 0.061f,    500, 0.025f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("plain"),            { 100.0f,  -1000, -2000, 0.0f,  1.49f, 0.50f,  -2466, 0.179f,  -2514, 0.100f,  21.0f, 100.0f, 5000.0f } },
        { Symbol("parking_lot"),      { 100.0f,  -1000,     0, 0.0f,  1.65f, 1.50f,  -1363, 0.008f,  -1153, 0.012f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("sewer_pipe"),       { 100.0f,  -1000, -1000, 0.0f,  2.81f, 0.14f,    429, 0.014f,    648, 0.021f,  80.0f,  60.0f, 5000.0f } },
        { Symbol("underwater"),       { 100.0f,  -1000, -4000, 0.0f,  1.49f, 0.10f,   -449, 0.007f,   1700, 0.011f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("small_room"),       { 100.0f,  -1000,  -600, 0.0f,  1.10f, 0.83f,   -400, 0.005f,    500, 0.010f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("medium_room"),      { 100.0f,  -1000,  -600, 0.0f,  1.30f, 0.83f,  -1000, 0.010f,   -200, 0.020f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("large_room"),       { 100.0f,  -1000,  -600, 0.0f,  1.50f, 0.83f,  -1600, 0.020f,  -1000, 0.040f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("medium_hall"),      { 100.0f,  -1000,  -600, 0.0f,  1.80f, 0.70f,  -1300, 0.015f,   -800, 0.030f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("large_hall"),       { 100.0f,  -1000,  -600, 0.0f,  1.80f, 0.70f,  -2000, 0.030f,  -1400, 0.060f, 100.0f, 100.0f, 5000.0f } },
        { Symbol("plate"),            { 100.0f,  -1000,  -200, 0.0f,  1.30f, 0.90f,      0, 0.002f,      0, 0.010f, 100.0f,  75.0f, 5000.0f } },
    };

    XAUDIO2FX_REVERB_PARAMETERS native;
    if (preset && *preset) {
        unsigned int idx;
        for (idx = 0; idx < 30; idx++) {
            if (presets[idx].name == preset)
                break;
        }
        if (idx == 30)
            MILO_FAIL("Unexpected environment preset.");
        ReverbConvertI3DL2ToNative(&presets[idx].params, &native);
    } else {
        ((IXAudio2Voice *)unkf4)->GetEffectParameters(0, &native, sizeof(native));
        native.DecayTime = 1.6f;
    }
    ((IXAudio2Voice *)unkf4)->SetEffectParameters(0, &native, sizeof(native), 0);
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

void Synth360::PreInit() {
    TheXboxSynth = this;

    // One meter bus per speaker, in the order MeterEffect writes them. The two
    // trailing entries are the downmixed stereo pair Poll() synthesizes; index 6
    // is unused and its name is the shared empty literal.
    {
        const char *busNames[9] = { "  FL", "  FR", "   C", " LFE", "  SL",
                                    "  SR", "",     " DML", " DMR" };
        for (int i = 0; i < 9; i++) {
            mLevelData.push_back(LevelData(busNames[i]));
        }
    }

    XAudio2Create((IXAudio2 **)&unkec, 0, 0x3c);
    ((IXAudio2 *)unkec)->CreateMasteringVoice((IXAudio2Voice **)&unkf0, 0, 0, 0, 0, 0);

    {
        XAUDIO2_EFFECT_DESCRIPTOR effectDescs[2];
        XAUDIO2_EFFECT_CHAIN effectChain;
        XAUDIO2_VOICE_SENDS voiceSends;
        XAUDIO2_SEND_DESCRIPTOR sendDesc;

        effectDescs[0].pEffect =
            static_cast<CXAPOBase *>(new StandardEffect<CompressionEffect>());
        effectDescs[0].InitialState = 1;
        effectDescs[0].OutputChannels = 6;
        effectDescs[1].pEffect = static_cast<CXAPOBase *>(new MeterEffect());
        effectDescs[1].InitialState = 1;
        effectDescs[1].OutputChannels = 6;

        // The MeterEffect reads the bus levels through one indirection that Poll()
        // hands back to it every frame; point it at mLevelData's element array.
        unk13c = (int)new int;
        *(int *)unk13c = *(int *)&mLevelData;

        effectChain.EffectCount = 2;
        effectChain.pEffectDescriptors = effectDescs;
        if ((IXAudio2Voice *)unkf0) {
            ((IXAudio2Voice *)unkf0)->SetEffectChain(&effectChain);
        }

        DataArray *limiterCfg = SystemConfig(Symbol("synth"), Symbol("limiter"));
        float threshold = limiterCfg->FindArray(Symbol("threshold"), true)->Float(1);
        float ratio = limiterCfg->FindArray(Symbol("ratio"), true)->Float(1);
        float attack = limiterCfg->FindArray(Symbol("attack_ms"), true)->Float(1) * 0.001f;
        float release =
            limiterCfg->FindArray(Symbol("release_ms"), true)->Float(1) * 0.001f;
        float outputDb = limiterCfg->FindArray(Symbol("output_db"), true)->Float(1);

        CompressionEffect::Params params;
        if ((IXAudio2Voice *)unkf0) {
            ((IXAudio2Voice *)unkf0)->GetEffectParameters(0, &params, sizeof(params));
        }
        params.mThresholdDb = threshold;
        params.mRatio = ratio;
        params.mAttackTime = attack;
        params.mReleaseTime = release;
        params.mGateThreshDb = -140.0f;
        params.mOutputGainDb = (1.0f - 1.0f / ratio) * threshold + outputDb;
        if ((IXAudio2Voice *)unkf0) {
            ((IXAudio2Voice *)unkf0)->SetEffectParameters(0, &params, sizeof(params), 0);
        }

        if ((IXAudio2 *)unkec && (IXAudio2Voice *)unkf0) {
            // The global reverb pair: a 2-channel submix carrying the reverb APO,
            // fed by a 6-channel submix that everything else sends into.
            CreateAudioReverb((IUnknown **)&unk100);
            effectDescs[0].pEffect = (IUnknown *)unk100;
            effectDescs[0].InitialState = 1;
            effectDescs[0].OutputChannels = 2;
            effectChain.EffectCount = 1;
            effectChain.pEffectDescriptors = effectDescs;
            ((IXAudio2 *)unkec)
                ->CreateSubmixVoice(
                    (IXAudio2Voice **)&unkf4, 2, 48000, 0, 0x8000, 0, &effectChain
                );

            sendDesc.Flags = 0;
            sendDesc.pOutputVoice = (IXAudio2Voice *)unkf4;
            voiceSends.SendCount = 1;
            voiceSends.pSends = &sendDesc;
            ((IXAudio2 *)unkec)
                ->CreateSubmixVoice(
                    (IXAudio2Voice **)&unkf8, 6, 48000, 0, 0x7fff, &voiceSends, 0
                );

            ((IXAudio2Voice *)unkf4)->SetVolume(4.0f, 0);

            String preset;
            DataArray *synthCfg = SystemConfig(Symbol("synth"));
            synthCfg->FindData(Symbol("reverb_environment"), preset, false);
            SetGlobalReverbPreset(preset.c_str());
        }
    }

    EnableLevels(mTrackLevels);
}

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

// The XAPO base-class bodies the target keeps in this object. `Release` is a
// pure forward to CXAPOBase's: CXAPOParametersBase overrides it only so that
// the IXAPOParameters vtable (at +0x20) has a slot of its own, and the body is
// a single tail branch.
ULONG CXAPOParametersBase::Release() { return CXAPOBase::Release(); }

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
