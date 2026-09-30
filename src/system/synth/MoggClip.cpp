#include "synth\MoggClip.h"
#include "math\Utl.h"
#include "obj\Data.h"
#include "obj\Msg.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\File.h"
#include "synth\FxSend.h"
#include "synth\Stream.h"
#include "synth\Synth.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include "utl\MakeString.h"
#include "utl\MemMgr.h"
#include "utl\Symbol.h"

bool IsLoadingMusicMogg(const char *mogg) {
    static Symbol is_loading_music_mogg("is_loading_music_mogg");
    static DataArrayPtr func(new DataArray(2));
    func->Node(0) = is_loading_music_mogg;
    func->Node(1) = mogg;
    DataNode exec = func->Execute(false);
    return exec.Int();
}

bool IsUselessMogg(const char *mogg) {
    static Symbol is_useless_mogg_load("is_useless_mogg_load");
    static DataArrayPtr func(new DataArray(2));
    func->Node(0) = is_useless_mogg_load;
    func->Node(1) = mogg;
    DataNode exec = func->Execute(false);
    return exec.Int();
}

#pragma region Hmx::Object

MoggClip::MoggClip()
    : mVolume(0), mControllerVolume(0), mStream(nullptr), unk4c(0), mData(nullptr), mDataSize(0),
      mLoader(nullptr), mFxSend(this), mFader(Hmx::Object::New<Fader>()),
      mUnloadWhenFinished(false), mPlaying(false), mLoop(false), mLoopStartSample(0), mLoopEndSample(-1),
      mBufSecs(0) {
    mFaders.push_back(mFader);
    StartPolling();
}

MoggClip::~MoggClip() {
    RELEASE(mLoader);
    RELEASE(mFader);
    KillStream();
    UnloadData();
}

BEGIN_HANDLERS(MoggClip)
    HANDLE_ACTION(play, Play(0))
    HANDLE_ACTION(stop, Stop(0))
    HANDLE_ACTION(set_pan, SetPan(_msg->Int(2), _msg->Float(3)))
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(MoggClip)
    SYNC_PROP_SET(file, mMoggFile, SetFile(_val.Str()))
    SYNC_PROP_SET(volume, mControllerVolume, SetControllerVolume(_val.Float()))
    SYNC_PROP(buf_secs, mBufSecs)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(MoggClip)
    SAVE_REVS(3, 2)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mMoggFile << mControllerVolume;
    bs << mBufSecs;
    bool loading = IsLoadingMusicMogg(mMoggFile.c_str());
    if (bs.Cached() && !loading) {
        FileLoader::SaveData(bs, mData, mDataSize);
    }
END_SAVES

BEGIN_COPYS(MoggClip)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(MoggClip)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mMoggFile)
        COPY_MEMBER(mControllerVolume)
        COPY_MEMBER(mBufSecs)
    END_COPYING_MEMBERS
END_COPYS

BEGIN_LOADS(MoggClip)
    PreLoad(bs);
    PostLoad(bs);
END_LOADS

INIT_REVS(3, 2)

void MoggClip::PreLoad(BinStream &bs) {
    LOAD_REVS(bs)
    ASSERT_REVS(3, 2)
    LOAD_SUPERCLASS(Hmx::Object)
    bs >> mMoggFile;
    bs >> mControllerVolume;
    if (d.rev <= 2) {
        bool b60;
        d >> b60;
        if (d.rev > 1) {
            int x, y;
            bs >> x >> y;
        }
    }
    if (d.altRev > 1) {
        bs >> mBufSecs;
    }
    LoadFile(d.rev > 0 ? &bs : 0);
    if (d.altRev == 1) {
        bs >> mBufSecs;
    }
}

void MoggClip::PostLoad(BinStream &bs) {
    EnsureLoaded();
    LoadNumChannels();
}

#pragma endregion
#pragma region SynthPollable

const char *MoggClip::GetSoundDisplayName() {
    return !IsPlaying() ? gNullStr
                        : MakeString("MoggClip: %s", FileGetName(mMoggFile.c_str()));
}

void MoggClip::SynthPoll() {
    if (mPlaying && mStream) {
        mStream->PollStream();
        if (!mStream->IsPlaying() && mStream->IsReady()) {
            if (mPanInfos.empty()) {
                int chans = mStream->GetNumChannels();
                if (chans == 1) {
                    mStream->SetPan(0, 0);
                } else if (chans == 2) {
                    mStream->SetPan(0, -1);
                    mStream->SetPan(1, 1);
                }
            }
            mStream->Play();
            static Message msg("mogg_ready");
            Export(msg, true);
        } else {
            if (mStream->IsFinished() || mFader->DuckedValue() == kDbSilence)
                Stop(0);
        }
    }
}

#pragma endregion
#pragma region PlayableSample

void MoggClip::Play(float f1) {

    if (EnsureLoaded()) {
        KillStream();
        Stream *stream = TheSynth->NewBufStream(mData, mDataSize, "mogg", 0, false);
        mStream = dynamic_cast<StandardStream *>(stream);
        if (mBufSecs > 0) {
            mStream->SetBufSecs(mBufSecs);
        }
        if (!mStream) {
            delete stream;
        } else {
#ifdef HX_WEB
            mStream->SetDebugTag(MakeString("MoggClip[%s]", mMoggFile.c_str()));
#endif
            mFader->SetVolume(0);
            SetVolume(f1);
            SetControllerVolume(mControllerVolume);
            UpdateFaders();
            UpdatePanInfo();
            ApplyLoop(mLoop, mLoopStartSample, mLoopEndSample);
            for (int i = 0; i < mStream->GetNumChanParams(); i++) {
                mStream->SetFXSend(i, mFxSend);
            }
            mPlaying = true;
        }
    } else
        MILO_NOTIFY("Mogg file not loaded: '%s'", mMoggFile.c_str());
}

void MoggClip::Stop(bool b1) {
    KillStream();
    if (mUnloadWhenFinished) {
        UnloadData();
    }
}

void MoggClip::Pause(bool pause) {
    mPlaying = !pause;
    if (mStream && !mPlaying) {
        mStream->Stop();
    }
}

bool MoggClip::DonePlaying() { return !mStream; }

void MoggClip::SetVolume(float vol) {
    mVolume = vol;
    if (mStream) {
        mStream->Stream::SetVolume(mControllerVolume + mVolume);
    }
}

void MoggClip::SetPan(float f1) {
    if (mNumChannels == 1) {
        SetPan(0, f1);
    }
}

void MoggClip::SetSend(FxSend *send) { mFxSend = send; }

void MoggClip::EndLoop() { SetLoop(false, mLoopStartSample, mLoopEndSample); }

float MoggClip::ElapsedTime() {
    if (!IsStreaming())
        return 0;
    else
        return mStream->GetTime() / 1000;
}

#pragma endregion
#pragma region MoggClip

bool MoggClip::IsStreaming() const { return mStream && mStream->IsPlaying(); }

void MoggClip::ApplyLoop(bool b1, int i2, int i3) {
    if (mStream) {
        mStream->ClearJump();
        if (b1) {
            mStream->SetJumpSamples(i3, i2, 0);
        }
    }
}

void MoggClip::FadeOut(float f1) { mFader->DoFade(kDbSilence, f1); }

void MoggClip::UnloadWhenFinishedPlaying(bool unload) { mUnloadWhenFinished = unload; }

bool MoggClip::IsReadyToPlay() const {
    if (mLoader)
        return mLoader->IsLoaded();
    else
        return mData && mDataSize > 0;
}

void MoggClip::KillStream() {
    mPlaying = false;
    RELEASE(mStream);
}

void MoggClip::UnloadData() {
    if (mData) {
        MemFree(mData);
        mData = nullptr;
        mDataSize = 0;
    }
}

void MoggClip::SetLoop(bool b1, int i2, int i3) {
    mLoop = b1;
    mLoopStartSample = i2;
    mLoopEndSample = i3;
    ApplyLoop(mLoop, mLoopStartSample, mLoopEndSample);
}

bool MoggClip::EnsureLoaded() {
    if (mLoader) {
        if (!mLoader->IsLoaded()) {
            MILO_NOTIFY("MoggClip blocked while loading '%s'", mMoggFile.c_str());
            TheLoadMgr.PollUntilLoaded(mLoader, nullptr);
        }
        mData = mLoader->GetBuffer(&mDataSize);
        RELEASE(mLoader);
    }
    return mData && mDataSize > 0;
}

void MoggClip::UpdateFaders() {
    if (mStream) {
        FOREACH (it, mFaders) {
            mStream->Faders()->Add(*it);
        }
    }
}

void MoggClip::UpdatePanInfo() {
    if (mStream) {
        FOREACH (it, mPanInfos) {
            mStream->SetPan(it->channel, it->panning);
        }
    }
}

void MoggClip::LoadNumChannels() {
    // Early exit if no mogg file configured
    if (mMoggFile.empty()) {
        mNumChannels = -1;
        return;
    }

    // Ensure loader has completed if present
    if (mLoader && !mLoader->IsLoaded()) {
        TheLoadMgr.PollUntilLoaded(mLoader, nullptr);
    }

    // Poll to initialize stream.
    // The target binary calls Play(0) here (PlayableSample vtable slot 0xc, float
    // 0.0 arg) rather than SynthPoll() (SynthPollable slot 0x8). Play(0) is the
    // PPC-correct form (og-dc3 verified) and is what the matching build must emit,
    // but on the native port it drives a real StandardStream init through
    // StreamReceiver::New, whose engine-side factory (StreamReceiver::sFactory,
    // milo-native-engine StreamReceiver_Native.cpp:44) is unregistered in the
    // asset-loading test harness -> NULL-pointer SegFault in 27 native tests. The
    // substitution is therefore a native-only workaround and lives behind
    // HX_NATIVE so the PPC codegen stays byte-identical to the target; drop the
    // guard once the engine registers the factory in that harness.
#ifdef HX_NATIVE
    SynthPoll();
#else
    Play(0);
#endif
    if (!mStream) {
        mNumChannels = -1;
        return;
    }

    // Poll synth up to 200 times waiting for channel count to become available
    int retries = 0;
    int numChannels = 0;
    while ((int)retries < 200) {
        Timer::Sleep(1);
        TheSynth->Poll();
        numChannels = mStream->NumInfoChannels();
        if (numChannels > 0) {
            break;
        }
        retries++;
    }

    mNumChannels = numChannels;
    Stop(false);

    // Log error if channel count retrieval failed
    if (mNumChannels < 0) {
        TheDebug.Notify(
            MakeString("[GetNumChannels] Ret = %d.  Unable to get the number of channels from mogg: %s!",
                       mNumChannels, (const String &)mMoggFile));
        mNumChannels = -1;
    }
}

void MoggClip::LoadFile(BinStream *bs) {
    RELEASE(mLoader);
    KillStream();
    UnloadData();
    mNumChannels = -1;
    if (!mMoggFile.empty()) {
        bool loadingMusic = IsLoadingMusicMogg(mMoggFile.c_str());
        bool useless = IsUselessMogg(mMoggFile.c_str());
        if (!useless) {
            if (!(bs && bs->Cached()) || loadingMusic) {
                bs = nullptr;
            }
            mLoader = new FileLoader(
                mMoggFile,
                FileLocalize(mMoggFile.c_str(), nullptr),
                kLoadFront,
                0,
                false,
                true,
                bs,
                0
            );
            if (!mLoader) {
                MILO_NOTIFY("Could not load mogg file '%s'", mMoggFile.c_str());
            }
        } else {
            MILO_ASSERT(!mLoader && !mData, 0x23C);
        }
    }
}

void MoggClip::SetFile(const char *file) {
    MILO_ASSERT(file != NULL, 0x14C);
    mMoggFile.Set(FilePath::Root().c_str(), file);
    LoadFile(nullptr);
    LoadNumChannels();
}

void MoggClip::AddFader(Fader *fader) {
    if (fader) {
        bool b1 = false;
        FOREACH (it, mFaders) {
            if (*it == fader) {
                b1 = true;
                break;
            }
        }
        if (!b1) {
            mFaders.push_back(fader);
        }
        if (mStream) {
            mStream->Faders()->Add(fader);
        }
    }
}

void MoggClip::SetPan(int i1, float f2) {
    bool found = false;
    PanInfo info;
    info.channel = i1;
    info.panning = f2;
    FOREACH (it, mPanInfos) {
        if (it->channel == i1) {
            found = true;
            *it = info;
            break;
        }
    }
    if (!found) {
        mPanInfos.push_back(info);
    }
    if (mStream) {
        mStream->SetPan(i1, f2);
    }
}

// SOLVED 2026-09-30 (w9-c), 78.387% -> 100.0%.  The lever is the two pairs of
// PARENTHESES below, and nothing else: `(panWidth * -0.5f) + pan` instead of
// `-panWidth / 2.0f + pan`.
//
// The target computes the two pans as two INDEPENDENT fused multiply-adds, each
// loading its own signed half:
//     lfs    f0, __real@bf000000   ; -0.5f
//     fmadds f1, f2,  f0, f1       ; panWidth * -0.5 + pan
//     bl     SetPan                ; channel 0
//     lfs    f0, __real@3f000000   ; +0.5f   (rematerialised after the call)
//     fmadds f1, f30, f0, f31      ; panWidth * +0.5 + pan
// Unparenthesised, Xenon MSVC's /fp:fast pass hoists the negative literal's sign
// out of the product (`x * -0.5f + y` -> `y - x * 0.5f`), which makes BOTH arms
// share the single product `panWidth * 0.5f`.  It then CSEs that product into f30
// across the intervening SetPan call and reaches the two call sites with
// `fmuls f30, f2, f0` / `fsubs f1, f1, f30` / `fadds f1, f30, f31` -- same values,
// one multiply fewer, and it cost 7 of 33 rows.  Explicit parentheses are a
// grouping barrier for that pass: the sign stays inside the product, the two
// constants stay distinct (bf000000 and 3f000000, both of which the target's
// .rdata holds), there is nothing left to CSE, and panWidth is homed into f30
// exactly as the image does it.  31 of 31 instructions equal.
//
// Twelve spellings measured here, one full ninja each.  GROUPING is the only axis
// that moves the score; constant spelling and operand order are all inert:
//   78.387% (byte-identical to the old `/ 2.0f` form, i.e. the CSE survives):
//     -panWidth / 2.0f + pan   |   panWidth / -2.0f + pan
//     pan + panWidth * -0.5f   |   -0.5f * panWidth + pan
//     pan + -0.5f * panWidth   |   -(panWidth * 0.5f) + pan
//     pan - panWidth * 0.5f  /  pan + panWidth * 0.5f
//     pan - panWidth / 2.0f  /  pan + panWidth / 2.0f
//     named `const float` halves (folded, then CSEd)
//     second product hoisted into a post-call local
//   76.903% (WORSE -- breaks the CSE but pays a runtime `fneg` the image folds
//     into its literal):  `float negWidth = -panWidth; negWidth * 0.5f + pan`
//     and `(-panWidth) / 2.0f + pan`
//   95.484%: `panWidth * -0.5f + pan` / `pan - panWidth * -0.5f` -- the asymmetry
//     leaves the two constants distinct so the CSE dies and the structure becomes
//     exact, but both arms come out as `fnmsubs` (the sign-hoisted form) where the
//     image has `fmadds`.
//   100.0%: the parenthesised form below, and equivalently each product bound to
//     its own local (`float leftOffset = panWidth * -0.5f; SetPan(0, leftOffset + pan);`).
//     A local is a grouping barrier for the same reason; the parens are cheaper.
// Parenthesising only ONE arm is not enough (78.710% / 78.387%): the surviving
// unparenthesised arm still offers the shared product.
//
// The channel/sign pairing is confirmed by the two distinct SINGLE-precision
// literals -- __real@bf000000 ahead of the `li r4, 0` SetPan and __real@3f000000
// ahead of the `li r4, 1` one -- so the arithmetic is float, not double.
//
// Retracted by the above: the earlier note that this was a "/fp:fast
// REASSOCIATION floor ... codegen wall; do not re-dig", and the imported
// rb3-xenon adjudication of four spellings.  Those four refutations reproduce
// exactly (all 78.387% here) -- they were just all drawn from the one axis that
// does not matter.  Also retracted: the claim that a `2.0` DOUBLE literal on
// channel 0 "scores HIGHER (92.9%)".  It does, and it is still provably not the
// original source (it emits an lfd of a double 0.5 plus an frsp the target lacks),
// but the honest spelling now scores 100.0% and the point is moot.
void MoggClip::SetupPanInfo(float pan, float panWidth, bool stereo) {
    if (stereo) {
        SetPan(0, (panWidth * -0.5f) + pan);
        SetPan(1, (panWidth * 0.5f) + pan);
    } else {
        SetPan(0, pan);
    }
}

