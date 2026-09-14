#include "hamobj\HamAudio.h"
#include "math\Utl.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\System.h"
#include "synth\Faders.h"
#include "synth\FxSend.h"
#include "synth\Synth.h"
#include "synth\StandardStream.h"
#include "utl/Loader.h"
#include "utl\MakeString.h"
#include "utl\MemMgr.h"
#include "utl\SongInfoAudioType.h"
#include "utl\SongInfo.h"
#include "utl\TimeConversion.h"

HamAudio::HamAudio()
    : mFileLoader(0), mRawBuffer(0), mSongInfo(0), mSongStream(0), mReady(0),
      mMasterFader(Hmx::Object::New<Fader>()), mMuteMaster(0), mFXSendApplied(0) {
    // mCrossfade / mActiveCrossfade zero their own mFlag in HamCrossfade().
    mStreams[0] = 0;
    mStreams[1] = 0;
    mCrossFaders[0] = Hmx::Object::New<Fader>();
    mCrossFaders[1] = Hmx::Object::New<Fader>();
}

HamAudio::~HamAudio() {
    Clear();
    RELEASE(mMasterFader);
    RELEASE(mCrossFaders[0]);
    RELEASE(mCrossFaders[1]);
}

BEGIN_HANDLERS(HamAudio)
    HANDLE_ACTION(toggle_mute_master, ToggleMuteMaster())
    HANDLE_ACTION(set_mute_master, SetMuteMaster(_msg->Int(2)))
    HANDLE_ACTION(print_faders, PrintFaders())
    HANDLE_EXPR(num_channels, (int)mChannelFaders.size())
    HANDLE_ACTION(set_channel_volume, SetChannelVolume(_msg->Int(2), _msg->Float(3)))
    HANDLE_ACTION_IF(
        set_track_volume,
        mTrackFaders[_msg->Sym(2)],
        mTrackFaders[_msg->Sym(2)]->SetVolume(_msg->Float(3))
    )
    HANDLE_ACTION(set_loop, SetLoop(_msg->Float(2), _msg->Float(3)))
    HANDLE_ACTION(clear_loop, ClearLoop())
    HANDLE(get_loop_beats, OnGetCurrentLoopBeats)
    HANDLE_ACTION(jump, Jump(_msg->Float(2)))
    HANDLE(set_crossfade_jump, OnSetCrossfadeJump)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(HamAudio)
    if (GetSongStream()) {
        SYNC_PROP_SET(
            speed, GetSongStream()->GetSpeed(), GetSongStream()->SetSpeed(_val.Float())
        )
    } else {
        SYNC_PROP_SET(speed, 1.0f, )
    }
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

bool HamAudio::IsReady() {
    if (!mSongStream && !mRawBuffer) {
        if (mFileLoader && mFileLoader->IsLoaded()) {
            FinishLoad();
        } else {
#ifdef HX_NATIVE
            // On native, the FileLoader may not be polled by TheLoadMgr
            // (e.g. after Game::Restart when there's no loading screen).
            // Drive it from here so IsReady() is self-contained.
            if (mFileLoader) {
                mFileLoader->PollLoading();
            }
#endif
            return false;
        }
    }
    mReady = mSongStream && mSongStream->IsReady();
    return mReady;
}

bool HamAudio::Paused() const { return !(mSongStream && mSongStream->IsPlaying()); }

void HamAudio::SetPaused(bool pause) {
    if (mSongStream) {
        if (pause) {
            mSongStream->Stop();
        } else if (!mSongStream->IsPlaying()) {
            mSongStream->Play();
        }
    }
}

void HamAudio::Poll() {
    if (gMiloTool && mFileLoader && !mFileLoader->IsLoaded()) {
        mFileLoader->PollLoading();
    }
    PollCrossfade();
}

float HamAudio::GetTime() const {
    if (mSongStream) {
        return mSongStream->GetTime();
    }
    return 0;
}

void HamAudio::SetMasterVolume(float vol) {
    mMasterVolume = vol;
    UpdateMasterFader();
}

void HamAudio::SetChannelVolume(int channel, float volume) {
    mChannelFaders[channel]->SetVolume(volume);
}

void HamAudio::SetMuteMaster(bool mute) {
    mMuteMaster = mute;
    UpdateMasterFader();
}

void HamAudio::ToggleMuteMaster() {
    mMuteMaster = !mMuteMaster;
    UpdateMasterFader();
}

void HamAudio::UpdateMasterFader() {
    float masterVolume;
    if (mMuteMaster != 0) {
        masterVolume = kDbSilence;
    } else {
        masterVolume = mMasterVolume;
    }
    mMasterFader->SetVolume(masterVolume);
}

bool HamAudio::Fail() { return mSongStream && mSongStream->Fail(); }
bool HamAudio::IsFinished() const { return mSongStream && mSongStream->IsFinished(); }

void HamAudio::Jump(float f1) {
    if (mSongStream) {
        mSongStream->Stop();
        mCrossFaders[0]->SetVolume(0);
        mCrossFaders[1]->SetVolume(kDbSilence);
        mActiveCrossfade.mFlag = 0;
        if (mStreams[1]) {
            mStreams[1]->Stop();
        }
        mReady = false;
        mSongStream->Resync(f1);
    }
}

void HamAudio::ClearLoop() {
    if (GetSongStream()) {
        GetSongStream()->ClearJump();
    }
    mCrossfade.mFlag = 0;
}

void HamAudio::DeleteFaders() {
    DeleteAll(mChannelFaders);
    FOREACH (it, mTrackFaders) {
        RELEASE(it->second);
    }
    mTrackFaders.clear();
}

void HamAudio::Clear() {
    if (mSongStream) {
        for (int i = 0; i < 2; i++) {
            mSongStream->SetFX(i, false);
        }
    }
    RELEASE(mStreams[0]);
    RELEASE(mStreams[1]);
    mSongStream = nullptr;
    RELEASE(mFileLoader);
    if (mRawBuffer) {
        MemFree(mRawBuffer);
        mRawBuffer = nullptr;
        mRawBufferSize = 0;
    }
    mSongInfo = nullptr;
    DeleteFaders();
    mCrossfade.mFlag = 0;
    mActiveCrossfade.mFlag = 0;
}

void HamAudio::Load(SongInfo *info, bool b2) {
    Clear();
    mSongInfo = info;
    String str(info->GetBaseFileName());
    if (b2) {
        Stream *stream = TheSynth->NewStream(str.c_str(), 0, 0, false);
        mSongStream = stream;
        mStreams[0] = stream;
        FinishLoad();
    } else {
        String moggStr(MakeString("%s.mogg", str.c_str()));
        mFileLoader =
            new FileLoader(
                moggStr.c_str(), moggStr.c_str(), kLoadFront, 0, false, true, 0, "main"
            );
    }
}

void HamAudio::Play() {
    MILO_ASSERT(mSongStream, 0x11B);
    mSongStream->Play();
    if (!mFXSendApplied) {
        if (TheSynth->CheckCommonBank(false)) {
            FxSend *send = TheSynth->Find<FxSend>("song.send", false);
            if (send) {
                for (int i = 0; i < 2; i++) {
                    if (mStreams[i]) {
                        for (int j = 0; j < mStreams[i]->GetNumChannels(); j++) {
                            mStreams[i]->SetFXSend(j, send);
                        }
                    }
                }
                mFXSendApplied = true;
            }
        }
    }
}

void HamAudio::PrintFaders() {
    MILO_LOG("MasterFader %.2f\n", mMasterFader->DuckedValue());
    MILO_LOG("CrossFaders[0] %.2f\n", mCrossFaders[0]->DuckedValue());
    MILO_LOG("CrossFaders[1] %.2f\n", mCrossFaders[1]->DuckedValue());
}

// intentionally unimplemented
void HamAudio::SetBackgroundVolume(float) {}
void HamAudio::SetForegroundVolume(float) {}
void HamAudio::SetStereo(bool) {}

bool HamAudio::GetCurrLoopMarkers(float &f1, float &f2) const {
    Marker m2, m1;
    Stream *s = mSongStream;
    if (!s || !s->CurrentJumpPoints(m1, m2)) {
        return false;
    }
    f1 = m2.posMS;
    f2 = m1.posMS;
    return true;
}

bool HamAudio::GetCurrLoopBeats(int &i1, int &i2) const {
    float f1, f2;
    if (!GetCurrLoopMarkers(f1, f2)) {
        return false;
    } else {
        i1 = SecondsToBeat(f1 / 1000.0f) + 0.5f;
        i2 = SecondsToBeat(f2 / 1000.0f) + 0.5f;
        return true;
    }
}

void HamAudio::SetLoop(float f1, float f2) {
    SetLoop(BeatToMs(f1), BeatToMs(f2), GetSongStream());
}

void HamAudio::SetCrossfadeJump(float startTime, float endTime, float fadeDuration) {
    MILO_ASSERT_FMT(mStreams[0] && mStreams[1], "Crossfade requires 2 song streams");

    if (mCrossfade.mFlag) {
        MILO_NOTIFY("Stomping on current queued crossfade");
    }

    // The image holds &mCrossfade in a THIRD callee-saved GPR, r30
    // (`addi r30, r31, 0x5c` at 8252A308), and reads the two fields of the
    // overlap test back through it after the notify calls
    // (`lfs f13, 0x0(r30)` / `lfs f0, 0x8(r30)` at 8252A360/64).
    HamCrossfade &crossfade = mCrossfade;
    crossfade.mEnd = endTime;
    crossfade.mStart = startTime;
    crossfade.mDuration = fadeDuration;
    crossfade.mFlag = 1;

    // The fade is centred on startTime, so it reaches back to
    // startTime - fadeDuration/2; if that is at or before zero the crossfade
    // would start before the song does.
    //
    // The flag starts FALSE and is raised inside the branch: the image sets
    // r11 = 0 before the compare (8252A318) and r11 = 1 as the last
    // instruction of the notify block (8252A350), and never tests it here.
    bool crossfadeInvalid = false;
    if (startTime - fadeDuration * 0.5f <= 0.0f) {
        MILO_NOTIFY(
            "Crossfade begins before start of song. Setting up hard jump instead of crossfade."
        );
        crossfadeInvalid = true;
    }

    // Check if crossfade overlaps with existing crossfade
    if (mActiveCrossfade.mFlag > 1) {
        if (crossfade.mStart - crossfade.mDuration * 0.5f <= (mActiveCrossfade.mDuration * 0.5f) + mActiveCrossfade.mEnd) {
            MILO_NOTIFY(
                "Crossfade begins before existing crossfade ends. Setting up hard jump instead of crossfade."
            );
            crossfadeInvalid = true;
        }
    }

    if (crossfadeInvalid) {
        crossfade.mFlag = 0;
    }

    // RESIDUAL (w7-ap, 90.5 canonical, up from 86.147): the three levers above
    // (the &mCrossfade reference bound AFTER the mCrossfade.mFlag read, the
    // false-then-raise bool, and the fnmsubs-shaped overlap test) land. What is
    // left is pure register permutation:
    //   * image r31 = this, r30 = &mCrossfade; ours is the other way round, so
    //     the `crossfade.mFlag = 0` store reads `stw ... 0xc(r31)` where the
    //     image reads `stw ... 0x68(r31)` -- the SAME address, one row.
    //   * image saves f29/f30/f31 with three inline `stfd` (8252A250/54/58);
    //     we allocate f28/f30/f31 and therefore take `bl __savefpr_28`, which
    //     saves a fourth double and widens the frame from -0x10a0 to -0x10b0.
    //     f29 is never used on our side -- it is the helper's contiguous range,
    //     not an extra live value, so the double-literal lever does not apply.
    //
    // NEGATIVE RESULT (w7-ap, 2026-09-14): respelling the first check as
    // `crossfade.mStart - crossfade.mDuration * 0.5f <= 0.0f` (to shorten
    // fadeDuration's live range and drop to three callee-saved FPRs) is exactly
    // score-neutral at 90.5 -- MSVC forwards the stores -- and it is also the
    // LESS faithful spelling: `fnmsubs f13, f13, f31, f30` at 8252A320 consumes
    // the incoming parameter registers (f30 = startTime, f31 = fadeDuration,
    // fmr'd in at 8252A270/7C), not reloads through r30. Keep the parameters.
    SetLoop(endTime, startTime, mStreams[0]);
}

void HamAudio::SetLoop(float f1, float f2, Stream *stream) {
    Marker m1, m2;
    if (stream->CurrentJumpPoints(m2, m1) && m1.posMS == f1 && m2.posMS == f2) {
        return;
    } else {
        stream->ClearJump();
        stream->ClearMarkerList();
        String start = "start";
        String end = "end";
        m1.name = start;
        m1.posMS = f1;
        m2.name = end;
        m2.posMS = f2;
        stream->AddMarker(m1);
        stream->AddMarker(m2);
        stream->SetJump(end, start);
    }
}

DataNode HamAudio::OnSetCrossfadeJump(DataArray *a) {
    float a2 = a->Float(2);
    float f6 = BeatToMs(a2);
    float f7 = BeatToMs(a->Float(3));
    float f8;
    if (a->Size() > 4) {
        f8 = a->Float(4);
    } else {
        f8 = SystemConfig("synth", "crossfade_beats")->Float(1);
    }
    f8 = BeatToMs(f8 + a2) - f6;
    SetCrossfadeJump(f6, f7, f8);
    return 0;
}

void HamAudio::FinishLoad() {
    if (mFileLoader) {
        mRawBuffer = mFileLoader->GetBuffer(&mRawBufferSize);
        delete mFileLoader;
        mFileLoader = NULL;
        const char *mogg = "mogg";
        mStreams[0] = TheSynth->NewBufStream(mRawBuffer, mRawBufferSize, mogg, 0.25f, true);
        mStreams[1] = TheSynth->NewBufStream(mRawBuffer, mRawBufferSize, mogg, 0.25f, true);
        mSongStream = mStreams[0];
#ifdef HX_WEB
        const char *baseName = mSongInfo ? mSongInfo->GetBaseFileName() : "<no-song>";
        StandardStream *primary = dynamic_cast<StandardStream *>(mStreams[0]);
        if (primary) {
            primary->SetDebugTag(MakeString("HamAudio[%s] primary", baseName));
        }
        StandardStream *crossfade = dynamic_cast<StandardStream *>(mStreams[1]);
        if (crossfade) {
            crossfade->SetDebugTag(MakeString("HamAudio[%s] crossfade", baseName));
        }
#endif
    }
    unsigned int counter = 2;
    Stream **pStream = &mStreams[0];
    do {
        if (*pStream) {
            (*pStream)->Faders()->Add(mMasterFader);
#ifdef HX_NATIVE
            Fader *crossFader = mCrossFaders[pStream - mStreams];
            (*pStream)->Faders()->Add(crossFader);
            crossFader->SetVolume(0.0f);
#else
            // PPC compiler strength-reduces this to lwz r4, 0x38, rPStream
            // but only with hardcoded byte offset — pointer subtraction generates
            // srawi/addi/slwi (5 extra instructions) that the compiler won't fold
            (*pStream)->Faders()->Add(*(Fader**)((char*)pStream + 0x38));
            (*(Fader**)((char*)pStream + 0x38))->SetVolume(0.0f);
#endif

            const std::vector<float> &vols = mSongInfo->GetVols();
            const std::vector<float> &pans = mSongInfo->GetPans();
            int numChannels = (int)vols.size();
            MILO_ASSERT(pans.size() == numChannels, 0x9d);

            for (int ch = 0; ch < numChannels; ch++) {
                Fader *fader;
                if (!((unsigned int)ch < mChannelFaders.size())) {
                    fader = Hmx::Object::New<Fader>();
                    fader->SetVolume(vols[ch]);
                    mChannelFaders.push_back(fader);
                } else {
                    fader = mChannelFaders[ch];
                }
                (*pStream)->ChannelFaders(ch).Add(fader);
                (*pStream)->SetPan(ch, pans[ch]);
            }

            const std::vector<TrackChannels> &tracks = mSongInfo->GetTracks();
            for (unsigned int t = 0; t < tracks.size(); t++) {
                SongInfoAudioType audioType = tracks[t].mAudioType;
                Symbol trackSym = SongInfoAudioTypeToSym(audioType);

                Fader *trackFader;
                if (mTrackFaders.find(trackSym) == mTrackFaders.end()) {
                    trackFader = Hmx::Object::New<Fader>();
                    mTrackFaders[trackSym] = trackFader;
                } else {
                    trackFader = mTrackFaders[trackSym];
                }

                const std::vector<int> &channels = tracks[t].mChannels;
                for (unsigned int c = 0; c < channels.size(); c++) {
                    (*pStream)->ChannelFaders(channels[c]).Add(trackFader);
                }

                if (TheSynth->CheckCommonBank(false)) {
                    Fader *vocalsFader = TheSynth->Find<Fader>("vocals_level.fade", false);
                    if (vocalsFader && audioType == kAudioTypeVocals) {
                        for (unsigned int c = 0; c < channels.size(); c++) {
                            (*pStream)->ChannelFaders(channels[c]).Add(vocalsFader);
                        }
                    }

                    Fader *multiFader = TheSynth->Find<Fader>("multi_level.fade", false);
                    if (multiFader && audioType == kAudioTypeMulti) {
                        for (unsigned int c = 0; c < channels.size(); c++) {
                            (*pStream)->ChannelFaders(channels[c]).Add(multiFader);
                        }
                    }

                    FxSend *reverbSend = TheSynth->Find<FxSend>("song.send", false);
                    if (reverbSend) {
                        for (int ch = 0; ch < numChannels; ch++) {
                            (*pStream)->SetFXSend(ch, reverbSend);
                        }
                        mFXSendApplied = true;
                    }
                }
            }
        }
        pStream++;
        counter--;
    } while (counter != 0);

    if (mStreams[1]) {
        if (mStreams[1]->IsReady()) {
            mStreams[1]->Resync(10000.0f);
        } else {
            MILO_NOTIFY("HamAudio::FinishLoad() - almost tried to resync stream before it was ready");
        }
    }
}

void HamAudio::PollCrossfade() {
    // NEGATIVE RESULT on the residual f29<->f30 permutation (26 of the 45 rows
    // left at 97.1%): the image holds currentTime in f29 and 0.5 in f30, ours the
    // other way round.  Neither declaring kEpsilon/halfFade ahead of the
    // GetInSongTime() call nor dropping the `halfFade` local for a bare 0.5f
    // literal moves it -- both produced byte-identical output (97.1 / 96.3, same
    // 45 rows).  Callee-saved FPR numbering here is regalloc, not declaration
    // order.
    float currentTime = mSongStream->GetInSongTime();
    float kEpsilon = 1.0f / 120.0f;
    float halfFade = 0.5f;

    if (mCrossfade.mFlag == 1 && mActiveCrossfade.mFlag <= 1) {
        MILO_ASSERT_FMT(mStreams[1], "Crossfade requires 2 song streams");
        // &mCrossfade is materialised ONCE into a callee-saved register and every
        // later read goes through it: 0x82529EA4 `addi r30, r31, 0x5c`, then
        // 0x0(r30)/0x4(r30)/0x8(r30) at 0x82529F14, 0x82529F44, 0x82529F4C,
        // 0x82529F64 and the four-word copy at 0x82529FAC.  r30 stays live across
        // the GetTime/IsReady/Resync/SetLoop calls, which is what makes it
        // callee-saved and the prologue `bl __savegprlr_29`.
        HamCrossfade &cf = mCrossfade;
        float jumpPoint = cf.mEnd
            - (cf.mStart - (-(cf.mDuration * halfFade - cf.mStart)));
        if (mStreams[1]->GetTime() != jumpPoint) {
            if (mStreams[1]->IsReady()) {
                mStreams[1]->Resync(jumpPoint);
                SetLoop(cf.mStart, cf.mEnd, mStreams[1]);
            } else {
                MILO_NOTIFY("HamAudio::PollCrossFade() - almost tried to resync stream before it was ready");
            }
        }
        bool shouldActivate
            = currentTime > (-(cf.mDuration * halfFade - cf.mStart) - kEpsilon);
        // Each of the three windowing tests in this function MATERIALISES its
        // ordering comparison into a byte and then branches on the byte, and
        // combines the two halves with a bitwise `and` rather than short-circuiting
        // -- 0x82529F70-0x82529F80 (`li r11,1` / `blt` / `li r11,0` / `clrlwi.` /
        // `beq`) and 0x82529F9C `and r10, r10, r11`; likewise 0x8252A058-0x8252A068
        // + 0x8252A088, and 0x8252A0F0-0x8252A104 + 0x8252A128.  Written as a bare
        // `if (a < b) x = x && y;` MSVC branches straight off the fcmpu and
        // short-circuits on x, which costs both the two `li`s and the `and`.
        bool startBeforeEnd = cf.mStart < cf.mEnd;
        if (startBeforeEnd) {
            shouldActivate = shouldActivate & (currentTime < mCrossfade.mEnd);
        }
        // The copy is written off the MEMBERS, not off `cf`: 0x82529FAC-0x82529FB8
        // batches all four loads into r10/r11/r9/r8 and only then stores them.
        // Spelling it `mActiveCrossfade = cf` makes the source a reference MSVC
        // cannot prove disjoint from the destination, and it degrades to four
        // interleaved load/store pairs through r11 (measured 88.0 -> 85.6).
        if (shouldActivate) {
            mActiveCrossfade = mCrossfade;
        }
    }

    // A SWITCH, not an if/else chain, and each arm writes mActiveCrossfade.mFlag
    // itself.  The dispatch at 0x82529FD4-0x82529FEC is MSVC's balanced compare
    // tree with UNSIGNED compares (`cmplwi 1` / blt -> case 0 / beq -> case 1,
    // `cmplwi 3` / blt -> case 2 / beq -> case 3, default falling through inline),
    // and the case bodies are laid out in REVERSE source order -- default first
    // at 0x82529FF0, then case 3 at 0x8252A02C, case 2 at 0x8252A0D0 and case 1
    // last at 0x8252A13C, which is what lets case 1 fall through into the shared
    // `stw r11, 0x78(r31)` at 0x8252A154 that the other two arms branch to.
    // The `lwz r11, 0x78(r31)` immediately after it (0x8252A158) is the next
    // statement re-reading the member; a `state` local carried in a register
    // stores once and never reloads.
    switch (mActiveCrossfade.mFlag) {
    case 0:
        break;
    case 1:
        mStreams[1]->Play();
        mActiveCrossfade.mFlag = 2;
        break;
    case 2: {
        bool ready = currentTime >= mActiveCrossfade.mEnd;
        bool startBeforeEnd = mActiveCrossfade.mStart < mActiveCrossfade.mEnd;
        if (!startBeforeEnd) {
            ready = ready
                & (currentTime
                   < (mActiveCrossfade.mStart - mActiveCrossfade.mDuration * 0.5f)
                       - kEpsilon);
        }
        if (ready) {
            mActiveCrossfade.mFlag = 3;
        }
        break;
    }
    case 3: {
        float halfFade = mActiveCrossfade.mDuration * 0.5f;
        bool ready = currentTime > (mActiveCrossfade.mEnd + halfFade);
        bool startBeforeEnd = mActiveCrossfade.mStart < mActiveCrossfade.mEnd;
        if (!startBeforeEnd) {
            ready = ready
                & (currentTime < (mActiveCrossfade.mStart - halfFade) - kEpsilon);
        }
        if (ready) {
            mCrossFaders[0]->SetVolume(0);
            mStreams[1]->Stop();
            mStreams[1]->ClearJump();
            mActiveCrossfade.mFlag = 0;
        }
        break;
    }
    default:
        MILO_ASSERT(0, 0x18E);
        break;
    }

    if (mActiveCrossfade.mFlag > 1) {
        float fadePos;
        if (mActiveCrossfade.mFlag == 2) {
            float start = mActiveCrossfade.mStart;
            float fadeStart = -(mActiveCrossfade.mDuration * halfFade - start);
            float ratio;
            if (start != fadeStart) {
                ratio = (currentTime - fadeStart) / (start - fadeStart);
            } else {
                ratio = 1.0f;
            }
            float clamped = Clamp(0.0f, 1.0f, ratio);
            fadePos = clamped * -0.5f + 1.0f;
        } else {
            float end = mActiveCrossfade.mDuration * halfFade + mActiveCrossfade.mEnd;
            float start2 = mActiveCrossfade.mEnd;
            float ratio;
            if (end != start2) {
                ratio = (currentTime - start2) / (end - start2);
            } else {
                ratio = 1.0f;
            }
            float clamped = Clamp(0.0f, 1.0f, ratio);
            fadePos = clamped * halfFade + halfFade;
        }
        float vol = (float)fadePos;
        float db0 = (float)log10((double)vol) * 10.0f;
        mCrossFaders[0]->SetVolume(db0);
        float db1 = (float)log10((double)(1.0f - vol)) * 10.0f;
        mCrossFaders[1]->SetVolume(db1);
    }
}

DataNode HamAudio::OnGetCurrentLoopBeats(DataArray *a) {
    int i40, i3c;
    if (!GetCurrLoopBeats(i40, i3c)) {
        return 0;
    } else {
        *a->Var(2) = i40;
        *a->Var(3) = i3c;
        return 1;
    }
}
