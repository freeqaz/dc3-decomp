#include "synth/StandardStream.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/System.h"
#include "synth/ADSR.h"
#include "synth/Synth.h"
#include "utl/MemMgr.h"
#include "utl/Std.h"
#include "synth/StreamReceiver.h"
#include "synth/StreamReceiverFile.h"
#include "utl/Symbol.h"
#include <cmath>
#include <functional>
#include "math/Decibels.h"
#include "math/Utl.h"

const float StandardStream::kStreamEndMs = -1.1920929E-7f;

bool StandardStream::sReportLargeTimerErrors = true;
#ifdef HX_NATIVE
float StandardStream::sAudioOffsetMs = 0.0f;
#endif

StandardStream::ChannelParams::ChannelParams()
    : mPan(0.0f), mSlipSpeed(1.0f), mSlipEnabled(0), mADSR(), mFaders(nullptr),
      mFxSend(nullptr) {
    static Symbol _parent("_parent");
    static Symbol _default("_default");
    mFaders.AddLocal(_parent)->SetVolume(0);
    mFaders.AddLocal(_default)->SetVolume(0);
}

StandardStream::StandardStream(
    File *f, float f1, float f2, Symbol ext, bool b1, bool b2, bool b3
)
    : mPollingEnabled(b2), unk158(b3) {
    MILO_ASSERT(f, 0x4A);
    mExt = ext;
    mFile = f;
    mInfoChannels = -1;
    unkec = -1;
    Init(f1, f2, ext, false);
}

StandardStream::~StandardStream() {
    RELEASE(mFile);
    for (int i = 0; i < unk10c.size(); i++) {
        delete unk10c[i];
    }
    Destroy();
    DeleteAll(mChanParams);
    while (!mVirtBufs.empty()) {
        MemFree(mVirtBufs.back());
        mVirtBufs.pop_back();
    }
}

bool StandardStream::Fail() { return mRdr && mRdr->Fail(); }
bool StandardStream::IsReady() const {
    return mState == kReady || mState == kPlaying || mState == kStopped;
}
bool StandardStream::IsFinished() const { return mState == kFinished; }
int StandardStream::GetNumChannels() const { return mChannels.size(); }
int StandardStream::GetNumChanParams() const { return mChanParams.size(); }

void StandardStream::Play() {
    if (!IsReady() && mState != kSuspended) {
        MILO_FAIL(
            "StandardStream::Play() failed. IsReady=%d mState=%d", IsReady(), mState
        );
    }
    UpdateVolumes();
    std::for_each(mChannels.begin(), mChannels.end(), std::mem_fun(&StreamReceiver::Play));
    mState = kPlaying;
    mTimer.Start();
}

void StandardStream::Stop() {
    if (mState == kPlaying) {
        std::for_each(
            mChannels.begin(), mChannels.end(), std::mem_fun(&StreamReceiver::Stop)
        );
        mState = kStopped;
        mTimer.Stop();
    }
}

bool StandardStream::IsPlaying() const { return mState == kPlaying; }
bool StandardStream::IsPaused() const { return mState == kStopped; }

void StandardStream::Resync(float f) {
    int bytes;
    while (!mFile->ReadDone(bytes))
        ;
    Destroy();
    mFile->Seek(0, 0);
    float f88 = mJumpFromMs;
    float f8c = mJumpToMs;
    String str94 = mJumpFile;
    Init(f, mBufSecs, mExt, true);
    if (f88 != 0) {
        SetJump(f88, f8c, str94.c_str());
    }
}

void StandardStream::EnableReads(bool b) {
    if (mRdr)
        mRdr->EnableReads(b);
}

float StandardStream::GetTime() {
    if (!mChannels.empty() && mSampleRate != 0) {
#ifdef HX_NATIVE
        return mLastStreamTime + sAudioOffsetMs;
#else
        return mLastStreamTime;
#endif
    } else
        return mStartMs;
}

float StandardStream::GetJumpBackTotalTime(float time) const {
    bool foundJump = true;
    float lastTime = 0.0f;
    float loopback = 0.0f;

    int count = (int)mJumpInstances.size() - 1;
    int i = count;
    while (i >= 0) {
        const JumpInstance& jump = mJumpInstances[i];
        float jumpTime = jump.unk8;
        if (time >= jumpTime) {
            loopback = jump.unkc;
            lastTime = jumpTime;
            break;
        }
        foundJump = false;
        i--;
    }

    if (foundJump && mJumpFromSamples != mJumpToSamples) {
        float totalLoopback = loopback + lastTime;
        float jumpFromMs = SampToMs(mJumpFromSamples);
        float jumpToMs = SampToMs(mJumpToSamples);
        if (totalLoopback < jumpFromMs) {
            float adjustedTime = (time - lastTime) + totalLoopback;
            if (adjustedTime >= jumpFromMs) {
                loopback = loopback + (jumpToMs - jumpFromMs);
            }
        }
    }

    return loopback;
}

float StandardStream::GetInSongTime() {
    float time = GetTime();
    return time + GetJumpBackTotalTime(time);
}

void StandardStream::SetVolume(int chan, float vol) {
    MILO_ASSERT_RANGE(chan, 0, mChanParams.size(), 0x41E);
    static Symbol _default("_default");
    mChanParams[chan]->mFaders.FindLocal(_default, true)->SetVolume(vol);
}

float StandardStream::GetVolume(int chan) const {
    MILO_ASSERT_RANGE(chan, 0, mChanParams.size(), 0x444);
    return mChanParams[chan]->mFaders.GetVolume();
}

void StandardStream::SetPan(int chan, float pan) {
    MILO_ASSERT_RANGE(chan, 0, mChanParams.size(), 0x426);
    mChanParams[chan]->mPan = pan;
    if (!mChannels.empty()) {
        mChannels[chan]->SetPan(pan);
    }
}

float StandardStream::GetPan(int chan) const {
    MILO_ASSERT_RANGE(chan, 0, mChanParams.size(), 0x44C);
    return mChanParams[chan]->mPan;
}

void StandardStream::SetFXSend(int channel, FxSend *send) {
    MILO_ASSERT_RANGE(channel, 0, mChanParams.size(), 0x4D9);
    mChanParams[channel]->mFxSend = send;
    if (!mChannels.empty()) {
        mChannels[channel]->SetFXSend(send);
    }
}

void StandardStream::SetSpeed(float speed) {
    mSpeed = speed;
    for (int i = 0; i < mChannels.size(); i++) {
        UpdateSpeed(i);
    }
    mTimer.SetSpeed(speed);
}

void StandardStream::LoadMarkerList(const char *cc) {
    ClearMarkerList();
    FileStream stream(cc, FileStream::kRead, 1);
    int i94 = 0;
    stream >> i94;
    int i98 = 0;
    stream >> i98;
    for (int i = 0; i < i98; i++) {
        Marker marker;
        stream >> marker.name;
        stream >> marker.position;
        marker.posMS = ((float)marker.position * 1000.0f) / (float)i94;
        AddMarker(marker);
    }
}

void StandardStream::ClearMarkerList() { mMarkerList.clear(); }
void StandardStream::AddMarker(const Marker &marker) { mMarkerList.push_back(marker); }
int StandardStream::MarkerListSize() const { return mMarkerList.size(); }

bool StandardStream::MarkerAt(int idx, Marker &marker) const {
    if (idx >= MarkerListSize() || idx < 0)
        return false;
    else {
        marker = mMarkerList[idx];
        return true;
    }
}

void StandardStream::SetJump(float fromMs, float toMs, const char *file) {
    MILO_ASSERT(toMs >= 0, 0x2C0);
    MILO_ASSERT(fromMs >= 0 || fromMs == kStreamEndMs, 0x2C1);
    if (IsPastStreamJumpPointOfNoReturn()) {
        MILO_NOTIFY(
            "Trying to set loop points when we're already past the point of no return!"
        );
    }
    mJumpFromMs = fromMs;
    mJumpToMs = toMs;
    mJumpFile = file;
    if (!mJumpFile.empty()) {
        mJumpFile += ".";
        mJumpFile += mExt.Str();
    }
    if (GetSampleRate() == 0)
        mJumpSamplesInvalid = true;
    else
        setJumpSamplesFromMs(fromMs, toMs);
}

void StandardStream::SetJump(String &s1, String &s2) {
    for (int i = 0; i < mMarkerList.size(); i++) {
        if (mMarkerList[i].name == s1) {
            mStartMarker = mMarkerList[i];
        }
        if (mMarkerList[i].name == s2) {
            mEndMarker = mMarkerList[i];
        }
    }
    SetJump(mStartMarker.posMS, mEndMarker.posMS, nullptr);
}

bool StandardStream::CurrentJumpPoints(Marker &start, Marker &end) {
    if (mJumpFromSamples == 0) {
        return false;
    } else {
        start = mStartMarker;
        end = mEndMarker;
        return true;
    }
}

void StandardStream::ClearJump() {
    mJumpFromSamples = 0;
    mJumpFromMs = 0;
    mJumpToSamples = 0;
    mJumpToMs = 0;
}

void StandardStream::EnableSlipStreaming(int channel) {
    MILO_ASSERT(mChannels.empty(), 0x4AF);
    MILO_ASSERT_RANGE(channel, 0, mChanParams.size(), 0x4B0);
    mChanParams[channel]->mSlipEnabled = true;
}

void StandardStream::SetSlipOffset(int channel, float offset) {
    MILO_ASSERT_RANGE(channel, 0, mChanParams.size(), 0x4B7);
    mChannels[channel]->SetSlipOffset(offset);
}

void StandardStream::SlipStop(int channel) {
    MILO_ASSERT_RANGE(channel, 0, mChanParams.size(), 0x4BE);
    mChannels[channel]->SlipStop();
}

float StandardStream::GetSlipOffset(int channel) {
    MILO_ASSERT_RANGE(channel, 0, mChanParams.size(), 0x4CF);
    return mChannels[channel]->GetSlipOffset();
}

void StandardStream::SetSlipSpeed(int channel, float speed) {
    MILO_ASSERT_RANGE(channel, 0, mChanParams.size(), 0x4C5);
    MILO_ASSERT(mChanParams[channel]->mSlipEnabled, 0x4C6);
    mChanParams[channel]->mSlipSpeed = speed;
    if (!mChannels.empty()) {
        UpdateSpeed(channel);
    }
}

FaderGroup &StandardStream::ChannelFaders(int channel) {
    MILO_ASSERT_RANGE(channel, 0, mChanParams.size(), 0x48D);
    return mChanParams[channel]->mFaders;
}

void StandardStream::AddVirtualChannels(int i) {
    MILO_ASSERT(mChannels.empty(), 0x495);
    mVirtualChans += i;
}

void StandardStream::RemapChannel(int i1, int i2) {
    mChanMaps.push_back(std::make_pair(i1, i2));
}

float StandardStream::GetRawTime() {
    float bytes = mChannels[0]->GetBytesPlayed() / 2;
    return (bytes / (float)mSampleRate) * 1000.0f + mStartMs;
}

void StandardStream::SetADSR(int chan, const ADSRImpl &adsr) {
    MILO_ASSERT_RANGE(chan, 0, mChanParams.size(), 0x43A);
    mChanParams[chan]->mADSR = adsr;
    if (!mChannels.empty()) {
        mChannels[chan]->SetADSR(adsr);
    }
}

void StandardStream::SetJumpSamples(int fromSamples, int toSamples, const char *file) {
    MILO_ASSERT(toSamples >= 0, 0x2F6);
    MILO_ASSERT(fromSamples >= 0 || fromSamples == kStreamEndSamples, 0x2F7);
    MILO_ASSERT(file || fromSamples > toSamples || fromSamples == kStreamEndSamples, 0x2F8);
    MILO_ASSERT(mJumpFromSamples == 0, 0x2FA);
    mJumpFromSamples = fromSamples;
    mJumpToSamples = toSamples;
    mJumpFile = file;
    if (!mJumpFile.empty()) {
        mJumpFile += ".";
        mJumpFile += mExt.Str();
    }
    mJumpSamplesInvalid = false;
}

const char *StandardStream::GetSoundDisplayName() {
    if (!IsPlaying()) {
        return gNullStr;
    } else if (mFile) {
        return MakeString("StandardStream: %s", FileGetName(mFile->Filename().c_str()));
    } else {
        return MakeString("StandardStream: --no file--");
    }
}

void StandardStream::SynthPoll() { PollStream(); }

void StandardStream::PollStream() {
    mFrameTimer.Restart();

    // Throttle the reader: poll with (throttle * bufSecs) seconds budget
    float pollTime = mThrottle * mBufSecs;
    if (mState == kBuffering) {
        pollTime = 8.0f;
    }
    if (pollTime < 1.0f) pollTime = 1.0f;
    if (mRdr) mRdr->Poll(pollTime);

    // Poll all channels (advance hardware cursors)
    for (int i = 0; i < mChannels.size(); i++) {
        mChannels[i]->Poll();
    }

    // State machine
    if (mState != kInit) {
        if (mState == kBuffering) {
            if (StuffChannels()) {
                mState = kReady;
            }
        } else if (mState > kReady && mState < kFinished) {
            StuffChannels();
            // Check if reader is done and all data consumed
            if (mRdr && mRdr->Done() && mJumpFromSamples == 0) {
                mState = kFinished;
            }
        } else if (mState != kFinished) {
            MILO_FAIL(MakeString("Bad stream state: %d", mState));
        }
    }

    // Jump handling
    if (mJumpFromSamples != 0 && mCurrentSamp >= mJumpFromSamples) {
        JumpInstance ji;
        ji.unk0 = 0;
        ji.unk4 = 0;
        ji.unk8 = GetTime();
        ji.unkc = GetJumpBackTotalTime(ji.unk8);
        mJumpInstances.push_back(ji);
        DoJump();
    }

    UpdateVolumes();
    UpdateTime();
}

void StandardStream::UpdateTime() {
    if (mChannels.empty() || mSampleRate == 0) {
        mLastStreamTime = mStartMs;
        return;
    }

    float rawTime = GetRawTime();

    float quantized = floorf(rawTime * 0.1875f + 0.5f) * 5.3333335f;

    float timerMs = mTimer.Ms();
    float adjusted = timerMs - (rawTime - quantized);
    float adjustedQuantized = floorf(adjusted * 0.1875f);

    if (quantized != adjustedQuantized * 5.3333335f) {
        float drift = quantized - adjusted;
        if (drift < 0.0f) {
            drift += 5.3333335f;
        }

        if (fabsf(drift) < 5.3333335f) {
            drift *= 0.1f;
        }

        mTimer.Reset(mTimer.Ms() + drift);

        if (fabsf(drift) > 50.0f && sReportLargeTimerErrors) {
            MILO_LOG("timer error is large: %f\n", drift);
        }
    }

    mLastStreamTime = mTimer.Ms();
}

void StandardStream::UpdateTimeByFiltering() {
    if (mChannels.empty() || mSampleRate == 0) {
        mLastStreamTime = mStartMs;
        return;
    }

    float drift = GetRawTime() - mTimer.Ms();

    if (fabsf(drift) > 50.0f) {
        if (sReportLargeTimerErrors) {
            MILO_LOG("timer error is large: %f\n", drift);
        }
    } else {
        drift *= 0.1f;
    }

    mTimer.Reset(mTimer.Ms() + drift);
    mLastStreamTime = mTimer.Ms();
}


void StandardStream::Init(float f1, float f2, Symbol s, bool b4) {
    ClearJumpMarkers();
    mBufSecs = f2;
    mGetInfoOnly = false;
    mState = kInit;
    mSampleRate = 0;
    if (mBufSecs == 0) {
        SystemConfig("synth")->FindData("stream_buf_size", mBufSecs);
    }
    mFileStartMs = f1;
    mStartMs = f1;
    mLastStreamTime = f1;
    mTimer.Reset(f1);
    mFloatSamples = false;
    if (!b4) {
        MILO_ASSERT(mChanParams.empty(), 0x6E);
        mChanParams.resize(32);
        for (int i = 0; i < 32; i++) {
            mChanParams[i] = new ChannelParams();
        }
        mVirtualChans = 0;
        mSpeed = 1;
    } else {
        while (mChanParams.size() < 32) {
            mChanParams.push_back(new ChannelParams());
        }
    }
    mJumpFromMs = 0;
    mJumpFromSamples = 0;
    mJumpToMs = 0;
    mJumpToSamples = 0;
    mCurrentSamp = 0;
    mThrottle = SystemConfig("synth", "oggvorbis")->FindFloat("throttle");
    if (mPollingEnabled) {
        StartPolling();
    }
    mRdr = TheSynth->NewStreamDecoder(mFile, this, s);
}

void StandardStream::InitInfo(int i1, int sampleRate, bool floatSamples, int i4) {
    unk154 = i4;
    int numChannels = mVirtualChans + i1;
    unkec = (mInfoChannels / sampleRate);
    mInfoChannels = numChannels;
    auto& _ref2 = mSampleRate;
    if (!mGetInfoOnly) {
        if (_ref2 == 0) {
            mFloatSamples = floatSamples;
            _ref2 = sampleRate;
            int bufBytes = mBufSecs * sampleRate * 2.0f;
            MILO_ASSERT(bufBytes % (2*kStreamBufSize) == 0, 0x13F);
            bufBytes >>= 0xE;
            SystemConfig("synth", "iop")->FindInt("max_slip");
            for (int i = 0; i < numChannels; i++) {
                if (unk158) {
                    mChannels.push_back(
                        new StreamReceiverFile(bufBytes, mChanParams[i]->mSlipEnabled)
                    );
                } else {
                    mChannels.push_back(
                        StreamReceiver::New(
                            bufBytes, sampleRate, mChanParams[i]->mSlipEnabled, i
                        )
                    );
                }
            }
            for (int i = 0; i < mVirtualChans; i++) {
                void *buf = MemAlloc(
                    mFloatSamples ? 0x1000 : 0x800, __FILE__, 0x159, "stream mVirtBufs"
                );
                mVirtBufs.push_back(buf);
            }
            mState = kBuffering;
        } else {
            MILO_ASSERT(numChannels == mChannels.size(), 0x161);
            MILO_ASSERT(_ref2 == sampleRate, 0x162);
            MILO_ASSERT(mFloatSamples == floatSamples, 0x163);
        }
        if (mJumpSamplesInvalid) {
            setJumpSamplesFromMs(mJumpFromMs, mJumpToMs);
        }
        int i;
        MILO_ASSERT(mChanParams.size() >= numChannels, 0x16C);
        for (i = 0; i < numChannels; i++) {
            mChannels[i]->SetPan(mChanParams[i]->mPan);
            UpdateSpeed(i);
            mChannels[i]->SetADSR(mChanParams[i]->mADSR);
        }
        for (; i < mChanParams.size(); i++) {
            delete mChanParams[i];
        }
        mChanParams.resize(numChannels);
        mCurrentSamp = MsToSamp(mFileStartMs);
        if (mCurrentSamp != 0) {
            mRdr->Seek(mCurrentSamp);
        }
        mFaders->SetDirty();
        UpdateFXSends();
    }
}

void StandardStream::ClearJumpMarkers() {
    mStartMarker.position = 0;
    mStartMarker.name = "";
    mEndMarker.position = 0;
    mEndMarker.name = "";
    mJumpInstances.clear();
}

void StandardStream::Destroy() {
    RELEASE(mRdr);
    DeleteAll(mChannels);
}

int StandardStream::MsToSamp(float ms) const {
    MILO_ASSERT(mSampleRate, 0x459);
    return mSampleRate * ms / 1000.0f;
}

float StandardStream::SampToMs(int samples) const {
    MILO_ASSERT(mSampleRate, 0x460);
    float ms = (float)samples / (float)mSampleRate;
    return ms * 1000.0f;
}

void StandardStream::UpdateVolumes() {
    static Symbol _parent("_parent");
    static Symbol _default("_default");

    // If the master faders are dirty, propagate to each channel's _parent local fader
    if (mFaders->Dirty()) {
        float masterVol = mFaders->GetVolume();
        for (std::vector<ChannelParams *>::iterator it = mChanParams.begin();
             it != mChanParams.end(); ++it) {
            (*it)->mFaders.FindLocal(_parent, true)->SetVolume(masterVol);
        }
        mFaders->ClearDirty();
    }

    // Apply per-channel volume to StreamReceiver
    for (int i = 0; i < mChannels.size(); i++) {
        if (mChanParams[i]->mFaders.Dirty()) {
            float vol = mChanParams[i]->mFaders.GetVolume();
            float ratio = DbToRatio(vol);
            ClampEq(ratio, 0.0f, 1.0f);
            mChannels[i]->SetVolume(ratio);
            mChanParams[i]->mFaders.ClearDirty();
        }
    }
}

void StandardStream::UpdateFXSends() {
    for (int i = 0; i < mChannels.size(); i++) {
        mChannels[i]->SetFXSend(mChanParams[i]->mFxSend);
    }
}

void StandardStream::UpdateSpeed(int chn) {
    MILO_ASSERT_RANGE(chn, 0, mChanParams.size(), 0x4A2);
    mChannels[chn]->SetSpeed(mSpeed);
    if (mChanParams[chn]->mSlipEnabled) {
        mChannels[chn]->SetSlipSpeed((float)mSpeed * mChanParams[chn]->mSlipSpeed);
    }
}

bool StandardStream::StuffChannels() {
    bool ret = true;
    for (int i = 0; i < mChannels.size(); i++) {
        if (!mChannels[i]->Ready())
            ret = false;
    }
    if (mRdr->Done() && mJumpFromSamples == 0) {
        std::for_each(
            mChannels.begin(), mChannels.end(), std::mem_fun(&StreamReceiver::EndData)
        );
    }
    return ret;
}

float StandardStream::GetBufferAheadTime() const {
    float time = 0;
    if (mSampleRate != 0) {
        time = SampToMs(mCurrentSamp);
    }
    return time;
}

// TODO: implement
#ifdef HX_NATIVE
int StandardStream::ConsumeData(void **v, int numSamples, int startSamp) {
    if (mGetInfoOnly)
        return 0;
    int numChannels = mChannels.size();
    int realChannels = numChannels - mVirtualChans;
    MILO_ASSERT(numChannels != 0, 0x1A9);
    if (startSamp >= 0 && startSamp != mCurrentSamp) {
        MILO_LOG("sample mismatch: expected %i, got %i\n", mCurrentSamp, startSamp);
        mCurrentSamp = startSamp;
    }

    int samplesToConsume = numSamples;
    if (mJumpFromSamples != 0) {
        MILO_ASSERT(mCurrentSamp <= mJumpFromSamples, 0x1CF);
        int remaining = mJumpFromSamples - mCurrentSamp;
        if (remaining < samplesToConsume) {
            samplesToConsume = remaining;
        }
    }

    int bytesPerSample = mFloatSamples ? 4 : 2;
    int bufSize = samplesToConsume * bytesPerSample;
    for (int i = 0; i < realChannels; i++) {
        int chanIdx = i;
        for (int j = 0; j < (int)mChanMaps.size(); j++) {
            if (mChanMaps[j].first == i) {
                chanIdx = mChanMaps[j].second;
                break;
            }
        }
        mChannels[chanIdx]->WriteData(v[i], bufSize);
    }
    for (int i = 0; i < mVirtualChans; i++) {
        memcpy(mVirtBufs[i], v[realChannels + i], bufSize);
    }
    mCurrentSamp += samplesToConsume;
    return samplesToConsume;
}

void StandardStream::setJumpSamplesFromMs(float fromMs, float toMs) {
    mJumpFromSamples = kStreamEndSamples;
    mJumpToSamples = 0;
    if (fromMs != kStreamEndMs) {
        mJumpFromSamples = MsToSamp(fromMs);
    }
    if (toMs != 0) {
        mJumpToSamples = MsToSamp(toMs);
    }
    mJumpSamplesInvalid = false;
}

bool StandardStream::IsPastStreamJumpPointOfNoReturn() {
    if (mJumpFromSamples == 0)
        return false;
    if (mChannels.empty())
        return false;
    return mCurrentSamp >= mJumpFromSamples;
}

void StandardStream::DoJump() {
    MILO_ASSERT(mJumpFromSamples != 0, 0x314);
    if (!mJumpFile.empty()) {
        delete mFile;
        delete mRdr;
        mFile = NewFile(mJumpFile.c_str(), 2);
        if (!mFile)
            MILO_FAIL("\nCould not open %s", mJumpFile.c_str());
        mRdr = TheSynth->NewStreamDecoder(mFile, this, mExt);
        mFileStartMs = SampToMs(mJumpToSamples);
        mCurrentSamp = 0;
        ClearJump();
    } else {
        if (mJumpFromSamples != mJumpToSamples) {
            if (mRdr)
                mRdr->Seek(mJumpToSamples);
            mCurrentSamp = mJumpToSamples;
        }
    }
}
#endif
