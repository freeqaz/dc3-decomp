#include "hamobj/HamAudio.h"
#include "math/Utl.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/System.h"
#include "synth/Faders.h"
#include "synth/FxSend.h"
#include "synth/Synth.h"
#include "utl/Loader.h"
#include "utl/MemMgr.h"
#include "utl/SongInfoAudioType.h"
#include "utl/SongInfoCopy.h"
#include "utl/TimeConversion.h"

HamAudio::HamAudio()
    : mFileLoader(0), mRawBuffer(0), mSongInfo(0), mSongStream(0), mReady(0),
      mMasterFader(Hmx::Object::New<Fader>()), mMuteMaster(0), mFXSendApplied(0),
      mCrossfadePending(0), mCrossfadeState(0) {
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
        } else
            return false;
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
        mCrossfadeState = 0;
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
    mCrossfadePending = 0;
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
    mCrossfadePending = 0;
    mCrossfadeState = 0;
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
            new FileLoader(moggStr.c_str(), "main", kLoadFront, 0, false, true, 0, 0);
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

    if (mCrossfadePending) {
        MILO_NOTIFY("Stomping on current queued crossfade");
    }

    // Store crossfade parameters
    mCrossfadeEndTime = endTime;
    mCrossfadeStartTime = startTime;
    mCrossfadeDuration = fadeDuration;
    mCrossfadePending = 1;

    // Check if crossfade is valid
    float halfFade = 0.5f;
    bool crossfadeInvalid = startTime >= fadeDuration * halfFade;

    if (crossfadeInvalid) {
        MILO_NOTIFY(
            "Crossfade begins before start of song. Setting up hard jump instead of crossfade."
        );
    }

    // Check if crossfade overlaps with existing crossfade
    if (mCrossfadeState > 1) {
        if (-(mCrossfadeDuration * halfFade - mCrossfadeStartTime) <= (mActiveCrossfadeDuration * halfFade) + mActiveCrossfadeStart) {
            MILO_NOTIFY(
                "Crossfade begins before existing crossfade ends. Setting up hard jump instead of crossfade."
            );
            crossfadeInvalid = true;
        }
    }

    if (crossfadeInvalid) {
        mCrossfadePending = 0;
    }

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

        static Symbol main("main");
        mStreams[0] = TheSynth->NewBufStream(mRawBuffer, mRawBufferSize, main, 0.25f, true);
        mStreams[1] = TheSynth->NewBufStream(mRawBuffer, mRawBufferSize, main, 0.25f, false);
        mSongStream = mStreams[0];
    }

    unsigned int counter = 2;
    Stream **pStream = &mStreams[0];
    const char *multiLevelFade = "multi_level.fade";
    const char *vocalsLevelFade = "vocals_level.fade";

    do {
        if (*pStream) {
            (*pStream)->Faders()->Add(mMasterFader);
            Fader *crossFader = (&mCrossFaders[0])[pStream - &mStreams[0]];
            (*pStream)->Faders()->Add(crossFader);
            crossFader->SetVolume(0.0f);

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
                    Fader *vocalsFader = TheSynth->Find<Fader>(vocalsLevelFade, false);
                    if (vocalsFader && audioType == kAudioTypeVocals) {
                        for (unsigned int c = 0; c < channels.size(); c++) {
                            (*pStream)->ChannelFaders(channels[c]).Add(vocalsFader);
                        }
                    }

                    Fader *multiFader = TheSynth->Find<Fader>(multiLevelFade, false);
                    if (multiFader && audioType == kAudioTypeMulti) {
                        for (unsigned int c = 0; c < channels.size(); c++) {
                            (*pStream)->ChannelFaders(channels[c]).Add(multiFader);
                        }
                    }

                    FxSend *reverbSend = TheSynth->Find<FxSend>("reverb_send", false);
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
            MILO_NOTIFY("HamAudio::FinishLoad - stream 1 not ready for resync");
        }
    }
}

void HamAudio::PollCrossfade() {
    float currentTime = mSongStream->GetInSongTime();
    float kEpsilon = 1.0f / 120.0f;
    float halfFade = 0.5f;

    if ((unsigned int)mCrossfadePending == 1 && mCrossfadeState <= 1) {
        MILO_ASSERT_FMT(mStreams[1], "Crossfade requires 2 song streams");
        float jumpPoint = mCrossfadeEndTime
            - (mCrossfadeStartTime - (-(mCrossfadeDuration * halfFade - mCrossfadeStartTime)));
        if (mStreams[1]->GetTime() != jumpPoint) {
            if (mStreams[1]->IsReady()) {
                mStreams[1]->Resync(jumpPoint);
                SetLoop(mCrossfadeStartTime, mCrossfadeEndTime, mStreams[1]);
            } else {
                MILO_NOTIFY("HamAudio::PollCrossFade() - almost ready for crossfade");
            }
        }
        bool shouldActivate
            = currentTime
            > (-(mCrossfadeDuration * halfFade - mCrossfadeStartTime) - kEpsilon);
        if (mCrossfadeStartTime < mCrossfadeEndTime) {
            shouldActivate = shouldActivate && currentTime < mCrossfadeEndTime;
        }
        if (shouldActivate) {
            unk6c = mCrossfadeStartTime;
            mActiveCrossfadeStart = mCrossfadeEndTime;
            mActiveCrossfadeDuration = mCrossfadeDuration;
            mCrossfadeState = mCrossfadePending;
        }
    }

    unsigned int state = mCrossfadeState;
    if (state == 0) {
        goto done;
    }
    if (state == 1) {
        mStreams[1]->Play();
        state = 2;
    } else if (!(state < 3)) {
        if (state != 3) {
            MILO_ASSERT(0, 0x18E);
            goto done;
        }
        float halfFade = mActiveCrossfadeDuration * 0.5f;
        bool ready = (mActiveCrossfadeStart + halfFade) < currentTime;
        if (mActiveCrossfadeStart <= unk6c) {
            ready = ready
                && currentTime < (unk6c - halfFade) - kEpsilon;
        }
        if (!ready) {
            goto done;
        }
        mCrossFaders[0]->SetVolume(0);
        mStreams[1]->Stop();
        mStreams[1]->ClearJump();
        state = 0;
    } else {
        bool ready = mActiveCrossfadeStart <= currentTime;
        if (mActiveCrossfadeStart <= unk6c) {
            ready = ready
                && currentTime
                    < (unk6c - mActiveCrossfadeDuration * 0.5f) - kEpsilon;
        }
        if (!ready) {
            goto done;
        }
        state = 3;
    }
    done:
    if (mCrossfadeState = state > 1) {
        float fadePos;
        if (mCrossfadeState == 2) {
            float start = unk6c;
            float fadeStart = -(mActiveCrossfadeDuration * 0.5f - start);
            float ratio = 1.0f;
            if (start != fadeStart) {
                ratio = (currentTime - fadeStart) / (start - fadeStart);
            }
            float clamped = Clamp(0.0f, 1.0f, ratio);
            fadePos = clamped * -0.5f + 1.0f;
        } else {
            float end = mActiveCrossfadeDuration * 0.5f + mActiveCrossfadeStart;
            float start2 = mActiveCrossfadeStart;
            float ratio = 1.0f;
            if (end != start2) {
                ratio = (currentTime - start2) / (end - start2);
            }
            float clamped = Clamp(0.0f, 1.0f, ratio);
            fadePos = clamped * 0.5f + 0.5f;
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
