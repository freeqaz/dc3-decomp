#include "synth\Synth.h"
#include "math\Utl.h"
#include "AudioDucker.h"
#include "Emitter.h"
#include "FxSendBitCrush.h"
#include "FxSendChorus.h"
#include "FxSendCompress.h"
#include "FxSendDistortion.h"
#include "FxSendEQ.h"
#include "FxSendFlanger.h"
#include "FxSendMeterEffect.h"
#include "FxSendSynapse.h"
#include "FxSendWah.h"
#include "KeyChain.h"
#include "MeterEffectMonitor.h"
#include "MidiInstrument.h"
#include "MoggClip.h"
#include "Sound.h"
#include "ThreeDSound.h"
#include "Utl.h"
#include "flow\Flow.h"
#include "math\Decibels.h"
#include "obj\Data.h"
#include "obj\DataFile.h"
#include "obj\DataFunc.h"
#include "obj\Dir.h"
#include "obj/Object.h"
#include "os/BufFile.h"
#include "os\Debug.h"
#include "os\Platform.h"
#include "os\System.h"
#include "rndobj\Overlay.h"
#include "rndobj\Rnd.h"
#include "synth\ADSR.h"
#include "synth\FxSend.h"
#include "synth\FxSendDelay.h"
#include "synth\FxSendPitchShift.h"
#include "synth\FxSendReverb.h"
#include "synth\MicNull.h"
#include "synth\Pollable.h"
#include "synth\Sequence.h"
#include "synth\Sfx.h"
#include "synth\StreamNull.h"
#include "synth\SynthSample.h"
#ifdef HX_NATIVE
#include "synth\StandardStream.h"
#include "synth\VorbisReader.h"
#endif
#include "synth\WavMgr.h"
#include "utl\Cache.h"
#include "utl/Loader.h"
#include <cstdio>

namespace {
    struct DebugGraph {
        DebugGraph(const Hmx::Color &c) {
            unk0.resize(200);
            unk8 = 0;
            unkc = c;
        }

        std::vector<float> unk0;
        int unk8;
        Hmx::Color unkc;
    };

    std::vector<DebugGraph> gDebugGraphs;
}

Loader *WavFactory(const FilePath &path, LoaderPos pos) {
    CacheResourceResult res;
    return new FileLoader(
        path, CacheWav(path.c_str(), res), pos, 0, false, true, nullptr, nullptr
    );
}

DataNode returnMasterKey(DataArray *a) {
    unsigned char str[16];
    unsigned char masher[64];
    if (a->Size() > 1) {
        KeyChain::getMasher(masher);
        str[0] = 'z';
        str[1] = 'M';
        str[2] = '`';
        str[3] = '|';
        str[4] = '\xFF';
        for (int i = 0; i < 5; i++) {
            str[i]++;
        }
        DataArray *data = DataReadString((char *)str);
        int i2 = data->Evaluate(0).Int();
        data->Release();
        int i3 = a->Int(1);
        memcpy((void *)(i3 ^ i2), masher, 0x40);
    }
    return 0;
}

Synth *TheSynth;

Synth::Synth()
    : mTrackLevels(false), mMuted(false), mMicClientMapper(nullptr), unk98(0),
      mDebugStream(0), mADSR(nullptr) {
    SetName("synth", ObjectDir::Main());
    DataArray *cfg = SystemConfig("synth");
    cfg->FindData("mics", mNumMics, true);
    cfg->FindData("track_levels", mTrackLevels, false);
    mMidiSynth = new MidiSynth();
    gDebugGraphs.push_back(DebugGraph(Hmx::Color(1, 0, 0)));
    gDebugGraphs.push_back(DebugGraph(Hmx::Color(0, 1, 0)));
    gDebugGraphs.push_back(DebugGraph(Hmx::Color(1, 1, 0)));
    gDebugGraphs.push_back(DebugGraph(Hmx::Color(1, 1, 1)));
    mMicClientMapper = new MicClientMapper();
    MILO_ASSERT(!TheSynth, 0xC0);
    mADSR = new ADSRImpl();
}

BEGIN_HANDLERS(Synth)
    HANDLE(play, OnPassthrough)
    HANDLE(stop, OnPassthrough)
    HANDLE_ACTION(run_flow, RunFlow(_msg->Str(2)))
    HANDLE(start_mic, OnStartMic)
    HANDLE(stop_mic, OnStopMic)
    HANDLE_ACTION(stop_playback_all_mics, StopPlaybackAllMics())
    HANDLE(num_connected_mics, OnNumConnectedMics)
    HANDLE_EXPR(did_mics_change, DidMicsChange())
    HANDLE_ACTION(reset_mics_changed, ResetMicsChanged())
    HANDLE(set_mic_volume, OnSetMicVolume)
    HANDLE(set_fx, OnSetFX)
    HANDLE(set_fx_vol, OnSetFXVol)
    HANDLE_ACTION(stop_all_sfx, StopAllSfx(_msg->Size() == 3 ? _msg->Int(2) : false))
    HANDLE_ACTION(pause_all_sfx, PauseAllSfx(_msg->Int(2)))
    HANDLE_EXPR(master_vol, GetMasterVolume())
    HANDLE_ACTION(set_master_vol, SetMasterVolume(_msg->Float(2)))
    HANDLE_EXPR(find, Find<Hmx::Object>(_msg->Str(2), true))
    HANDLE_ACTION(toggle_hud, ToggleHud())
    HANDLE_EXPR(
        get_sample_mem, GetSampleMem(_msg->Obj<ObjectDir>(2), (Platform)_msg->Int(3))
    )
    HANDLE_EXPR(spu_overhead, GetSPUOverhead())
    HANDLE_ACTION(set_headset_target, 0)
    HANDLE_ACTION(stop_all_sounds, StopAllSounds())
    HANDLE_ACTION(set_vo_edit_sound, unka8 = _msg->Str(2))
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

void Synth::Init() {
    SynthUtlInit();
    REGISTER_OBJ_FACTORY(Fader);
    Sfx::Init();
    REGISTER_OBJ_FACTORY(MidiInstrument)
    SynthSample::Init();
    Sequence::Init();
    SynthEmitter::Init();
    REGISTER_OBJ_FACTORY(FxSendReverb)
    REGISTER_OBJ_FACTORY(FxSendDelay)
    REGISTER_OBJ_FACTORY(FxSendBitCrush)
    REGISTER_OBJ_FACTORY(FxSendDistortion)
    REGISTER_OBJ_FACTORY(FxSendCompress)
    REGISTER_OBJ_FACTORY(FxSendEQ)
    REGISTER_OBJ_FACTORY(FxSendFlanger)
    REGISTER_OBJ_FACTORY(FxSendChorus)
    REGISTER_OBJ_FACTORY(FxSendMeterEffect)
    REGISTER_OBJ_FACTORY(FxSendPitchShift)
    REGISTER_OBJ_FACTORY(FxSendSynapse)
    REGISTER_OBJ_FACTORY(FxSendWah)
    REGISTER_OBJ_FACTORY(MoggClip)
    REGISTER_OBJ_FACTORY(MeterEffectMonitor)
    REGISTER_OBJ_FACTORY(Sound)
    REGISTER_OBJ_FACTORY(ADSR)
    REGISTER_OBJ_FACTORY(ThreeDSound)
    REGISTER_OBJ_FACTORY(AudioDuckerTrigger)
    mMasterFader = Hmx::Object::New<Fader>();
    mSfxFader = Hmx::Object::New<Fader>();
    mMidiInstrumentFader = Hmx::Object::New<Fader>();
    DataArray *cfg = SystemConfig("synth");
    mMuted = cfg->FindInt("mute");
    TheLoadMgr.RegisterFactory("wav", WavFactory);
    mMics.resize(mNumMics);
    for (int i = 0; i < mMics.size(); i++) {
        mMics[i] = new MicNull();
    }
    mHud = RndOverlay::Find("synth_hud", true);
    mHud->SetCallback(this);
    InitSecurity();
}

void Synth::InitSecurity() {
#ifndef HX_NATIVE
    // Letter-function DTA handlers (A-M) for masterKey obfuscation.
    // Not needed on native — setupCypher bypasses the DTA address dance.
    char buf[256];
    buf[1] = '\0';
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            buf[0] = j + ('A' + i * 4);
            DataRegisterFunc(buf, returnMasterKey);
        }
    }
    buf[0] = 'M';
    DataRegisterFunc(buf, returnMasterKey);
#endif
    // ByteGrinder must init on ALL platforms — registers Na, ha, O## DTA functions
    // needed by setupCypher (GrindArray, magic hash generation).
    mByteGrinder.Init();
}

void Synth::Terminate() {
    MILO_ASSERT(mZombieInsts.empty(), 0x116);
    DeleteAll(mMics);
    RELEASE(mMidiSynth);
    RELEASE(mMasterFader);
    RELEASE(mSfxFader);
    RELEASE(mMidiInstrumentFader);
    RELEASE(mMicClientMapper);
    SynthUtlTerm();
}

void Synth::Poll() {
    for (int i = 0; i < mLevelData.size(); i++) {
        LevelData &data = mLevelData[i];
        if ((data.mPeak > data.mPeakHold) || ++data.mPeakAge >= 0x3C) {
            data.mPeakHold = data.mPeak;
            data.mPeakAge = 0;
        }
    }
    if (mMuted)
        mMasterFader->SetVolume(-96.0f);
    SynthPollable::PollAll();
    if (DidMicsChange()) {
        MILO_ASSERT(mMicClientMapper, 0x14E);
        mMicClientMapper->HandleMicsChanged();
        ResetMicsChanged();
    }
    if (!mZombieInsts.empty()) {
        CullZombies();
    }
}

Stream *Synth::NewStream(const char *filename, float f1, float f2, bool) {
#ifdef HX_NATIVE
    File *file;
    Symbol ext;
    NewStreamFile(filename, file, ext);
    return new StandardStream(file, f1, f2, ext, true, true, false);
#else
    return new StreamNull(f1);
#endif
}

Stream *Synth::NewBufStream(const void *buf, int size, Symbol ext, float f1, bool b1) {
#ifdef HX_NATIVE
    File *file = new BufFile(buf, size);
    return new StandardStream(file, 0, f1, ext, b1, true, false);
#else
    return new StreamNull(f1);
#endif
}

void Synth::NewStreamFile(const char *cc, File *&file, Symbol &sym) {
#ifdef HX_NATIVE
    // Resolve mogg file from ark/filesystem
    String path(MakeString("%s.mogg", cc));
    file = NewFile(path.c_str(), 2); // 2 = kRead
    sym = "mogg";
    if (!file) {
        // Fallback: try without .mogg extension
        file = NewFile(cc, 2);
        sym = "mogg";
    }
    if (!file) {
        static char gFakeFile[16];
        file = new BufFile(gFakeFile, sizeof(gFakeFile));
        sym = "fake";
    }
#else
    static char gFakeFile[16];
    file = new BufFile(gFakeFile, sizeof(gFakeFile));
    sym = "fake";
#endif
}

StreamReader *Synth::NewStreamDecoder(File *file, StandardStream *stream, Symbol ext) {
#ifdef HX_NATIVE
    if (ext == "mogg" || ext == "main") {
        return new VorbisReader(file, true, stream, true);
    }
#endif
    return nullptr;
}

FxSendPitchShift *Synth::CreatePitchShift(int stage, SendChannels channels) {
    FxSendPitchShift *pitchShift = Hmx::Object::New<FxSendPitchShift>();
    pitchShift->SetStage(stage);
    pitchShift->SetChannels(channels);
    return pitchShift;
}

void Synth::DestroyPitchShift(FxSendPitchShift *shift) { delete shift; }

float Synth::UpdateOverlay(RndOverlay *o, float y) {
    Hmx::Color white(1, 1, 1, 1);
    // The PARAMETER is reused, not copied into a local: the image stores the
    // scaled value into `y`'s home slot in the CALLER's frame (`stfs f0,
    // 0x124(r1)` with its own frame only 0x100 deep) and hands that same
    // address to every DrawMeterScale/DrawMeter as the `float &`.  A separate
    // local costs its own slot at 0x50, pushes the Vector2 temp to 0x54 and
    // adds 0x20 of frame.
    y = (float)TheRnd.Height() * (y + 0.265f);
    if (mDebugStream) {
        DrawMeterScale(y);
        float volume = mDebugStream->Faders()->GetVolume();
        DrawMeter(y, volume, 0, "stream");
        for (int i = 0; i < mDebugStream->GetNumChannels(); i++) {
            DrawMeter(
                y,
                mDebugStream->ChannelFaders(i).GetVolume(),
                0,
                MakeString("chan %i", i)
            );
        }
    }
    if (!mLevelData.empty()) {
        DrawMeterScale(y);
    }
    for (int i = 0; i < mLevelData.size(); i++) {
        float rms = RatioToDb(mLevelData[i].mRMS);
        float peakhold = RatioToDb(mLevelData[i].mPeakHold);
        if (rms > 2) {
            rms = -30;
        }
        DrawMeter(y, rms, peakhold, mLevelData[i].mName.c_str());
    }
    // buf is 40 bytes, not 64: it sits at 0x80(r1) on BOTH sides and the image's
    // frame is 0x100 where ours was 0x120.  The save area is 0x58 deep below the
    // caller's r1, so the image's locals must end by 0xa8 -- i.e. 0x28 bytes of
    // buffer.  (Any size in 0x19..0x28 rounds to the same 0x100 frame; 0x28 is
    // the largest that fits, and still covers the 25-char prefix plus an int.)
    char buf[40];
    // ONE begin(), shared by the count and the walk.  The image loads
    // `sPollables.begin()` into a callee-saved register (r31) before sprintf and
    // still has it after DrawString -- which a `Pollables().size()` call
    // followed by a separate FOREACH cannot produce, because the intervening
    // sprintf/DrawString calls stop MSVC from CSEing a load out of a global.
    // Same shape RB3's Synth::UpdateOverlay carries.
    int count = 0;
    std::list<SynthPollable *>::iterator it = SynthPollable::Pollables().begin();
    for (std::list<SynthPollable *>::iterator it2 = it;
         it2 != SynthPollable::Pollables().end();
         ++it2) {
        ++count;
    }
    sprintf(buf, "Total active Sequences: %d", count);
    TheRnd.DrawString(buf, Vector2(100, y), white, true);
    float f12 = y + 12.0f;
    for (; it != SynthPollable::Pollables().end(); ++it) {
        const char *name = (*it)->GetSoundDisplayName();
        if (*name != '\0') {
            TheRnd.DrawString(name, Vector2(100, f12), white, true);
            f12 += 12.0f;
        }
    }
    return f12 / (float)TheRnd.Height();
}

void Synth::SetMasterVolume(float volume) { mMasterFader->SetVolume(volume); }

float Synth::GetMasterVolume() { return mMasterFader->DuckedValue(); }

void Synth::ToggleHud() {
    mHud->SetShowing(!mHud->Showing());
    if (!mTrackLevels) {
        EnableLevels(mHud->Showing());
    }
}

const ADSRImpl *Synth::DefaultADSR() {
    MILO_ASSERT(mADSR, 0x498);
    return mADSR;
}

static const float sMeterConsts[] = { 0.2f, 40.0f, 0.7f, 0.0f };

/** SURVEYED w7-aj at 73.0% canonical, 688 B (same size as the image).  Every
 *  statement below was checked against 0x82737418-827376C4 and is correct:
 *  the five colour constructors land in the image's own stack slots
 *  (white 0x90, red 0xa0, grey 0xb0, white2 0xc0, green 0xd0, black 0xe0 --
 *  33 of 43 slots MATCH), Clamp(0,1,x) expands to exactly the image's
 *  fneg/fsel/fsubs/fsel quartet (Utl.h Clamp = Min(Max(min,value),max)), the
 *  peak colour really is `red` with `if (peakNorm != 1.0f) peakColor = &green`
 *  (fcmpu against 1.0 then `addi r5, r1, 0xd0`, 0x82737608), TheRnd.Width() is
 *  read twice as the image reads it, and dbLabelPos is `barWidth + barLeft`
 *  in that order (`fadds f0, f28, f29`, 0x82737644).
 *
 *  The residual is one scheduling decision: the image finishes the level
 *  Clamp into f24 BEFORE the background DrawRect (hence its __savefpr_24
 *  against our __savefpr_25 and its 0x170 frame against our 0x160), while we
 *  compute it after.  NEGATIVE RESULT, measured, do not re-derive: hoisting
 *  the `levelNorm` statement above `TheRnd.DrawRect(bgRect, ...)` -- tried
 *  both before and after the barLeft/barWidth pair, which MSVC normalises to
 *  the same code -- DOES fix the frame size, the callee-saved FPR count and
 *  the f24/f29 register assignment, but MSVC then sinks only the SECOND fsel
 *  of the Clamp past the call, and the function drops 73.0 -> 70.3 (196
 *  instructions against the image's 194).  Whatever keeps both fsels above
 *  the call is not statement order.
 *
 *  w7-bo (2026-09-15) re-measured that negative and added two more, all
 *  from the SAME hoisted shape (frame 0x170, __savefpr_24, regions 0-55 and
 *  176-195 both 100%, but 196 instructions and 70.3 canonical -- exactly the
 *  numbers w7-aj recorded):
 *    (a) hoisting the MULTIPLY as well, i.e.
 *        `float levelWidth = Clamp(0.f,1.f,(level+sMeterConsts[1])*0.025f)
 *             * barWidth;` above the bgRect DrawRect, then
 *        `Hmx::Rect levelRect(barLeft, y, levelWidth, 12.f);`
 *        -- 70.3, 196 instructions.  MSVC still sinks the high clamp:
 *        base gets `fsel f24, f10, f30, f12` (LOW clamp) before the bctrl and
 *        `fsubs f0, f24, f31` / `fsel f0, f0, f31, f24` / `fmuls f0, f0, f28`
 *        after it, where the image has BOTH fsels before (0x827374F0,
 *        0x82737504) and only `fmuls f0, f24, f28` after (0x8273751C).
 *        So the thing MSVC sinks is not the multiply -- giving it a multiply
 *        to sink instead does not buy back the high clamp.
 *    (b) splitting Clamp into three statements in the hoisted position
 *        (`levelNorm = (level+sMeterConsts[1])*0.025f;`
 *         `levelNorm = Max(0.0f, levelNorm);`
 *         `levelNorm = Min(1.0f, levelNorm);`)
 *        -- BYTE-IDENTICAL to (a): 70.3, 196 instructions, same 66 rows.
 *        Statement separation does not pin the fsel either.
 *  Net: the hoist buys the whole prologue/epilogue (13 rows: __savefpr_24,
 *  __restfpr_24, stwu -0x170, addi 0x170, fmr f29) and costs more than it
 *  buys in the body, so the 73.0 non-hoisted spelling below is kept.
 *  FLOOR 73.0 canonical, 194 instructions, 123 equal / 23 diff_arg / 4
 *  replace / 22 insert / 22 delete.  Residual, verbatim:
 *    - prologue/epilogue: __savefpr_24 vs _25, stwu -0x170 vs -0x160,
 *      `fmr f29, f1` vs `fmr f26, f1` (the levelNorm-across-the-call FPR)
 *    - idx 56-99: the level Clamp scheduled before vs after the bgRect
 *      DrawRect, and the f11/f12 + f0/f13 relabelling it drags with it
 *    - idx 119-148: the peakRect member stores interleaved with the peak
 *      Clamp (image) vs emitted as a block after it (ours) -- pure schedule,
 *      same 6 stores, same values
 *    - idx 160-174: the white2 stores + the TheRnd reload reordered around
 *      `stfs f0, 0x54(r1)`; includes the (0xc0,0xc4) OFFSET_SWAP, which is
 *      two stores of the SAME value f31 (1.0f) and cannot be spelled apart
 *    - idx 120: `fadds f25,f0` vs `f0,f25` -- commutative operand order on
 *      `peakHold + sMeterConsts[1]`; MSVC picks this from its own FPR
 *      assignment, not from source order (refuted repeatedly this wave)
 *
 *  Also noted, not chased: the image's MakeString here is
 *  ??$MakeString@W4_D3DFORMAT@@@@... where ours is ??$MakeString@H@@... --
 *  the usual identical-COMDAT fold of MakeString<int> onto another 4-byte
 *  instantiation, not a source defect. */
void Synth::DrawMeter(float &y, float level, float peakHold, const char *name) {
    Hmx::Color grey(0.5f, 0.5f, 0.5f, 1.0f);
    Hmx::Color black(0.0f, 0.0f, 0.0f, 1.0f);
    Hmx::Color white(1.0f, 1.0f, 1.0f, 1.0f);
    Hmx::Color green(0.5f, 1.0f, 0.0f, 1.0f);
    Hmx::Color red(1.0f, 0.5f, 0.5f, 1.0f);

    Vector2 labelPos((float)TheRnd.Width() * 0.1f, y);
    TheRnd.DrawString(name, labelPos, white, true);

    float rndWidth = (float)TheRnd.Width();
    float barLeft = rndWidth * sMeterConsts[0];
    float barWidth = rndWidth * sMeterConsts[2];
    Hmx::Rect bgRect(barLeft, y, barWidth, 12.0f);
    TheRnd.DrawRect(bgRect, black, 0, 0, 0);

    float levelNorm = Clamp(0.0f, 1.0f, (level + sMeterConsts[1]) * 0.025f);

    Hmx::Rect levelRect(barLeft, y, levelNorm * barWidth, 12.0f);
    TheRnd.DrawRect(levelRect, grey, 0, 0, 0);

    float peakNorm = Clamp(0.0f, 1.0f, (peakHold + sMeterConsts[1]) * 0.025f);

    Hmx::Color *peakColor = &red;
    if (peakNorm != 1.0f)
        peakColor = &green;

    Hmx::Rect peakRect(barLeft + peakNorm * barWidth, y, 8.0f, 12.0f);
    TheRnd.DrawRect(peakRect, *peakColor, 0, 0, 0);

    Hmx::Color white2(1.0f, 1.0f, 1.0f, 1.0f);
    Vector2 dbLabelPos(barWidth + barLeft, y);
    TheRnd.DrawString(MakeString("%i", (int)peakHold), dbLabelPos, white2, true);

    y += 16.0f;
}

void Synth::DrawMeterScale(float &y) {
    int db = -40;
    float height = (float)TheRnd.Width();
    Hmx::Color color(1.0f, 1.0f, 1.0f, 1.0f);
    float left = height * sMeterConsts[0];
    float width = height * sMeterConsts[2];
    Vector2 pos(left, y);
    TheRnd.DrawString(MakeString("%i", db), pos, color, true);
    db = -20;
    Vector2 pos2(left + width * 0.5f, y);
    TheRnd.DrawString(MakeString("%i", db), pos2, color, true);
    Vector2 pos3(left + width, y);
    TheRnd.DrawString("0", pos3, color, true);
    y += 16.0f;
}

void Synth::SetFX(const DataArray *data) {
    MILO_ASSERT(data, 0x165);
    SetFXChain(data->FindInt("chain"));
    for (int i = 0; i < 2; i++) {
        DataArray *coreArr = data->FindArray(MakeString("core_%i", i));
        int mode = coreArr->FindArray("mode")->Int(1);
        float volume = coreArr->FindArray("volume")->Float(1);
        float delay = coreArr->FindArray("delay")->Float(1);
        float feedback = coreArr->FindArray("feedback")->Float(1);
        SetFXMode(i, (FXMode)mode);
        SetFXVolume(i, volume);
        SetFXDelay(i, delay);
        SetFXFeedback(i, feedback);
    }
}

void Synth::SetMic(const DataArray *data) {
    for (int i = 0; i < mNumMics; i++) {
        Mic *mic = GetMic(i);
        if (mic)
            mic->Set(data);
    }
    SetMicFX(data->FindInt("fx"));
    SetMicVolume(data->FindFloat("volume"));
}

bool Synth::CheckCommonBank(bool notify) {
    bool loaded = mCommonBank && mCommonBank.IsLoaded();
    if (!loaded && notify) {
        MILO_LOG("Synth::Find() - Common sound bank not loaded!\n");
    }
    return loaded;
}

int Synth::GetFXOverhead() {
    int overheads[10] = { 0x80,   0x26c0, 8000,    0x4c28,  0x6fe0,
                          0xade0, 0xf6c0, 0x18040, 0x18040, 0x3c00 };
    DataArray *cfg = SystemConfig("synth");
    int mode = cfg->FindArray("fx")->FindArray("core_0")->FindInt("mode");
    return overheads[mode] + 0x20000;
}

int Synth::GetSPUOverhead() {
    DataArray *cfg = SystemConfig("synth");
    int spuBufs = cfg->FindArray("iop")->FindInt("spu_buffers");
    spuBufs *= 0x800;
    spuBufs += 0x5010;
    return spuBufs + GetFXOverhead();
}

void Synth::StopPlaybackAllMics() {
    if (mMicClientMapper->GetMicMgrInterface()) {
        mMicClientMapper->GetMicMgrInterface()->SetPlayback(false);
    }
}

void Synth::AddPlayHandler(Hmx::Object *obj) { mPlayHandlers.push_back(obj); }
void Synth::RemovePlayHandler(Hmx::Object *obj) { mPlayHandlers.remove(obj); }

void Synth::SendToPlayHandlers(Sound *sound) {
    SoundPlayMsg msg(sound);
    auto end_it = mPlayHandlers.end();
    for (auto it = mPlayHandlers.begin(); it != end_it; ++it) {
        (*it)->Handle(msg, false);
    }
}

void Synth::RunFlow(const char *flowName) {
    if (CheckCommonBank(false)) {
        Flow *flow = Find<Flow>(flowName, false);
        if (flow) {
            flow->Activate();
        } else {
            MILO_NOTIFY(
                "Synth::RunFlow() - %s not found in %s", flowName, mCommonBank->GetPathName()
            );
        }
    }
}

void Synth::StopAllSfx(bool stop) {
    FOREACH (it, SynthPollable::Pollables()) {
        Sequence *seq = dynamic_cast<Sequence *>(*it);
        if (seq) {
            seq->Stop(stop);
        }
    }
}

void Synth::PauseAllSfx(bool pause) {
    FOREACH (it, SynthPollable::Pollables()) {
        Sfx *sfx = dynamic_cast<Sfx *>(*it);
        if (sfx) {
            sfx->Pause(pause);
        }
        Sound *sound = dynamic_cast<Sound *>(*it);
        if (sound) {
            sound->Pause(pause);
        }
    }
}

void Synth::PlaySound(const char *name, float f1, float f2, float f3) {
    if (CheckCommonBank(false)) {
        Sound *sound = Find<Sound>(name, false);
        if (sound) {
            sound->Play(f1, f2, f3, nullptr, 0);
        } else {
            MILO_NOTIFY(
                "Synth::PlaySound() - Sound %s not found in %s",
                name,
                mCommonBank->GetPathName()
            );
        }
    }
}

void Synth::StopAllSounds() {
    FOREACH (it, SynthPollable::Pollables()) {
        Sound *sound = dynamic_cast<Sound *>(*it);
        if (sound) {
            sound->Stop(nullptr, true);
        }
    }
}

int Synth::GetNumMics() const { return mNumMics; }

int Synth::GetSampleMem(ObjectDir *dir, Platform p) {
    int num = 0;
    for (ObjDirItr<SynthSample> it(dir, true); it != nullptr; ++it) {
        num += it->GetPlatformSize(p);
    }
    return num;
}

void Synth::AddZombie(SampleInst *inst) {
    inst->Stop(false);
    mZombieInsts.push_back(inst);
}

void Synth::CullZombies() {
    std::list<SampleInst *>::iterator next = mZombieInsts.begin();
    std::list<SampleInst *>::iterator it;
    while (next != mZombieInsts.end()) {
        it = next;
        ++next;
        if ((*it)->DonePlaying()) {
            mZombieInsts.erase(it);
        }
    }
}

DataNode Synth::OnPassthrough(DataArray *a) {
    if (!CheckCommonBank(false))
        return 0;
    else {
        const char *name = a->Str(2);
        Hmx::Object *obj = Find<Hmx::Object>(name, false);
        if (obj)
            obj->Handle(a, true);
        else
            MILO_NOTIFY(
                "Synth::OnPassthrough() - %s not found in %s", name, mCommonBank->GetPathName()
            );
        return 0;
    }
}

DataNode Synth::OnStartMic(const DataArray *a) {
    GetMic(a->Int(2))->Start();
    return 0;
}

DataNode Synth::OnStopMic(const DataArray *a) {
    GetMic(a->Int(2))->Stop();
    return 0;
}

DataNode Synth::OnNumConnectedMics(const DataArray *) { return GetNumConnectedMics(); }

DataNode Synth::OnSetMicVolume(const DataArray *a) {
    SetMicVolume(a->Float(2));
    return 0;
}

DataNode Synth::OnSetFX(const DataArray *a) {
    SetFX(a->Array(2));
    return 0;
}

DataNode Synth::OnSetFXVol(const DataArray *a) {
    SetFXVolume(a->Int(2), a->Float(3));
    return 0;
}


#ifdef HX_NATIVE
extern Synth *CreateNativeSynth();
#endif

void SynthPreInit() {
    MILO_ASSERT(!TheSynth, 0x283);
    DataArray *cfg = SystemConfig("synth");
    bool useNullSynth = cfg->FindInt("use_null_synth");
    if (useNullSynth) {
        TheSynth = new Synth();
    } else {
#ifdef HX_NATIVE
        TheSynth = CreateNativeSynth();
#else
        TheSynth = Synth::New();
#endif
    }
    if (TheSynth->Fail()) {
        RELEASE(TheSynth);
        TheSynth = new Synth();
    }
    TheSynth->PreInit();
    InitWavMgr();
}

void SynthInit() {
    if (!TheSynth)
        SynthPreInit();
    DataArray *cfg = SystemConfig("synth");
    TheSynth->Init();
    TheSynth->SetMic(cfg->FindArray("mic"));
    TheSynth->SetFX(cfg->FindArray("fx"));
    TheSynth->MasterFader()->SetVolume(cfg->FindFloat("master_vol"));
    TheDebug.AddExitCallback(SynthTerminate);
    PreloadSharedSubdirs("synth");
}

void SynthTerminate() {
    TheSynth->StopAllSounds();
    TheSynth->Poll();
    TheDebug.RemoveExitCallback(SynthTerminate);
    TheSynth->Terminate();
    delete TheSynth;
    TheSynth = nullptr;
}
