#include "synth360\SampleInst.h"
#include "synth_xbox\Voice.h"
#include "synth_xbox\FxSend.h"

SampleInst360::SampleInst360(SynthSample360 *sample, bool loop, int startSample, int endSample)
    : SampleInst(sample) {
    mVoice = new Voice(sample->IsXMA(), sample->GetNumChannels(), false);
    mVoice->SetSampleRate(sample->GetSampleRate());
    mVoice->SetData((const void *)sample->GetDataAddr(), sample->GetNumBytes(), sample->GetNumSamples());
    if (loop) {
        mVoice->SetLoopRegion(startSample, endSample);
    }
}

SampleInst360::~SampleInst360() {
    Voice *voice = mVoice;
    if (voice) {
        delete voice;
    }
}

#ifdef HX_NATIVE
bool SampleInst360::IsPlaying() const { return mVoice->IsPlaying(); }
#else
bool SampleInst360::IsPlaying() { return mVoice->IsPlaying(); }
#endif

void SampleInst360::SetFXCore(FXCore core) {}

float SampleInst360::GetProgress() {
    XAUDIO2_VOICE_STATE state;
    ((IXAudio2SourceVoice *)mVoice->mPoolVoice.sourceVoice)->GetState(&state, 0);
    Voice *voice = mVoice;
    int pos = (unsigned int)state.SamplesPlayed;
    if (voice->mLoopStart >= 0) {
        int offset = pos - voice->mStartSamp;
        int len = voice->mLoopEnd == -1 ? voice->mNumSamples - voice->mLoopStart
                                        : voice->mLoopEnd;
        pos = offset % len + voice->mStartSamp;
    }
    SynthSample360 *sample = (SynthSample360 *)mSample.Ptr();
    return pos * 1000.0f / (sample->LengthMs() * sample->GetNumSamples());
}

void SampleInst360::StartImpl() { mVoice->Start(); }

void SampleInst360::StopImpl(bool b) { mVoice->Stop(b); }

void SampleInst360::EndLoopImpl() { mVoice->EndLoop(); }

void SampleInst360::SetVolumeImpl(float vol) { mVoice->SetVolume(vol); }

void SampleInst360::SetPanImpl(float pan) { mVoice->SetPan(pan); }

void SampleInst360::SetSpeedImpl(float speed) { mVoice->SetSpeed(speed); }

void SampleInst360::Pause(bool b) { mVoice->Pause(b); }

void SampleInst360::SetADSR(const ADSRImpl &adsr) {
    mVoice->mAttackRate = adsr.GetAttackRate();
    mVoice->mReleaseRate = adsr.GetReleaseRate();
}

float SampleInst360::ElapsedTime() {
    XAUDIO2_VOICE_STATE state;
    ((IXAudio2SourceVoice *)mVoice->mPoolVoice.sourceVoice)->GetState(&state, 0);
    float samples = (double)state.SamplesPlayed;
    return samples / (float)mSample->GetSampleRate();
}

void SampleInst360::SetSendImpl(FxSend *send) {
    mVoice->SetSend(dynamic_cast<FxSend360 *>(send));
}

void SampleInst360::SetReverbMixDbImpl(float db) { mVoice->SetReverbMixDb(db); }

void SampleInst360::SetReverbEnableImpl(bool enable) { mVoice->SetReverbEnable(enable); }
