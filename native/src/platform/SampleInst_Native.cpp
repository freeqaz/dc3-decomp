// DC3 Native Port - SampleInstNative implementation
// One-shot and looping sound effects via AudioDevice.

#include "platform/SampleInst_Native.h"
#include "synth/SampleData.h"
#include "synth/SynthSample.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// SynthSample::NewInst — native implementation
SampleInst *SynthSample::NewInst(bool loop, int startSample, int endSample) {
    return new SampleInstNative(this, loop, startSample, endSample);
}

SampleInstNative::SampleInstNative(SynthSample *sample, bool loop, int startSample, int endSample)
    : SampleInst(sample),
      mPCMData(nullptr), mPCMSamples(0), mPlayPos(startSample > 0 ? startSample : 0),
      mEndSample(endSample), mLoop(loop),
      mPlaying(false), mPaused(false),
      mInstVolume(1.0f), mInstPan(0.0f), mInstSpeed(1.0f),
      mSampleRate(44100), mNumChannels(1) {
}

SampleInstNative::~SampleInstNative() {
    if (mPlaying) {
        AudioDevice::GetInstance().RemoveSource(this);
        mPlaying = false;
    }
}

void SampleInstNative::StartImpl() {
    if (!mSample)
        return;

    const SampleData &data = mSample->GetSampleData();
    if (!data.HasData())
        return;

    mSampleRate = data.GetSampleRate();
    mNumChannels = data.NumChannels();
    mPCMData = (const int16_t *)data.DataPtr();
    mPCMSamples = data.GetNumSamples();

    if (mEndSample <= 0 || mEndSample > mPCMSamples)
        mEndSample = mPCMSamples;

    mPlaying = true;
    AudioDevice::GetInstance().AddSource(this);
}

void SampleInstNative::StopImpl(bool) {
    if (mPlaying) {
        AudioDevice::GetInstance().RemoveSource(this);
        mPlaying = false;
    }
}

int SampleInstNative::RenderAudio(float *output, int frameCount) {
    int totalSamples = frameCount * 2; // stereo output

    if (!mPlaying || mPaused || !mPCMData) {
        memset(output, 0, totalSamples * sizeof(float));
        return frameCount;
    }

    int endPos = (mEndSample > 0) ? mEndSample : mPCMSamples;
    float volL = mInstVolume * std::max(0.0f, 1.0f - mInstPan);
    float volR = mInstVolume * std::max(0.0f, 1.0f + mInstPan);

    for (int i = 0; i < frameCount; i++) {
        if (mPlayPos >= endPos) {
            if (mLoop) {
                mPlayPos = 0;
            } else {
                // Fill rest with silence and stop
                for (int j = i; j < frameCount; j++) {
                    output[j * 2 + 0] = 0.0f;
                    output[j * 2 + 1] = 0.0f;
                }
                mPlaying = false;
                return frameCount;
            }
        }

        float sample;
        if (mNumChannels == 2 && mPlayPos * 2 + 1 < mPCMSamples * 2) {
            // Stereo: interleaved L/R
            float left = mPCMData[mPlayPos * 2] / 32768.0f;
            float right = mPCMData[mPlayPos * 2 + 1] / 32768.0f;
            output[i * 2 + 0] = left * mInstVolume;
            output[i * 2 + 1] = right * mInstVolume;
        } else {
            // Mono: pan to stereo
            sample = mPCMData[mPlayPos] / 32768.0f;
            output[i * 2 + 0] = sample * volL;
            output[i * 2 + 1] = sample * volR;
        }

        mPlayPos++;
    }

    return frameCount;
}
