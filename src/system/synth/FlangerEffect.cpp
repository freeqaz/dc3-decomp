#include "synth/FlangerEffect.h"
#include "Common_Xbox.h"
#include "math/Rot.h"
#include "os/Debug.h"
#include "xdk/xaudio2/xaudio2.h"

FlangerEffect::FlangerEffect(IXAudioBatchAllocator *ix)
    : mWritePos(0), mDelaySamples(100), mDepthFrac(0), unk1c(0), mFeedbackFrac(0.5f), unk24(0), mRateRadians(0), unk2c(0),
      mWetFrac(0.1f) {
    for (int i = 0; i < 2; i++) {
        DspAllocate(mDelayBuffers[i], 0x2580, ix);
        DspAllocate(mDelayBuffers[i + 2], 0x2580, ix);
    }
}

FlangerEffect::~FlangerEffect() {
    for (int i = 0; i < 2; i++) {
        DspFree(mDelayBuffers[i]);
        DspFree(mDelayBuffers[i + 2]);
    }
}

void FlangerEffect::Reset() {
    mWritePos = 0;
    unk1c = 0;
    unk24 = 0;
    unk2c = 0;
    for (int i = 0; i < 2; i++) {
        DspClearBuffer(mDelayBuffers[i], 0x2580);
        DspClearBuffer(mDelayBuffers[i + 2], 0x2580);
    }
}

void FlangerEffect::SetParameters(FlangerEffect::Params const &params) {
    float sampleRate = 48000.0f;
    mDelaySamples = (int)(params.mDelayMs * 48.0f);
    mRateRadians = (params.mRate / sampleRate) * 6.2831853f;
    mDepthFrac = params.mDepth / 100.0f;
    mFeedbackFrac = params.mFeedback / 100.0f;
    mWetFrac = params.mWet / 100.0f;
}

void FlangerEffect::Process(float *buf, int numSamples, int numChans) {
    MILO_ASSERT(numChans <= 2, 0x27);

    // Load LFO phase and smoothed parameter state
    double lfoPhase = (double)(float)unk24;

    float phaseOffsets[2];
    if (numChans == 1) {
        phaseOffsets[0] = 0.0f;
        phaseOffsets[1] = 0.0f;
    } else {
        phaseOffsets[1] = (float)mWetFrac * 1.5707964f;
        phaseOffsets[0] = (float)mWetFrac * -1.5707964f;
    }

    double curRate = (double)(float)unk2c;
    double curDepth = (double)(float)unk1c;
    long long local_e8 = (long long)(numSamples * 0x14);
    double depthStep = (double)((float)((double)(float)mDepthFrac - curDepth) / (float)local_e8);
    double rateStep = (double)((float)((double)(float)mRateRadians - curRate) / (float)local_e8);

    if (0 < numSamples) {
        long long frameBase = 0;
        long long sampleIdx = 0;

        do {
            local_e8 = (long long)mDelaySamples;
            long long chanIdx = 0;
            double delayCenter = (double)(float)(-(double)(float)((float)(curDepth * 0.5) - (float)1.0) * (double)local_e8);
            double delayRange = (double)(float)((double)(float)((double)local_e8 * curDepth) * 0.5);

            if (0 < numChans) {
                int writeOff = (int)(((unsigned int)mWritePos + sampleIdx +
                                      (long long)((int)((unsigned int)mWritePos + sampleIdx) / 0x2580) * -0x2580 &
                                     0xffffffff) << 2);
                float **chanPtr = mDelayBuffers;
                do {
                    double lfo = (double)sin((double)(float)((double)*(float *)((int)phaseOffsets + ((int)chanPtr - (int)mDelayBuffers)) + lfoPhase));

                    unsigned long long frameAndChan = (unsigned long long)(frameBase + chanIdx);
                    int chanBuf = (int)*chanPtr;
                    chanIdx = chanIdx + 1;
                    unsigned int writePos = mWritePos;
                    int chanBuf2 = (int)chanPtr[2];
                    chanPtr = chanPtr + 1;

                    int frameBufOff = (int)((frameAndChan & 0x3fffffff) << 2);
                    float input = *(float *)(frameBufOff + (int)buf);

                    *(float *)(chanBuf + writeOff) = input;

                    double delayF = (double)(float)((double)(float)lfo * delayRange + delayCenter);
                    double clamped = 1.0;
                    if ((float)(1.0 - delayF) < 0.0f) clamped = delayF;
                    double finalDelay = 4799.0;
                    if ((float)(clamped - 4799.0) < 0.0f) finalDelay = clamped;

                    long long intDelay1 = (long long)(int)finalDelay;
                    long long rp1 = ((unsigned long long)writePos - (unsigned long long)(unsigned int)(int)finalDelay) + sampleIdx;
                    long long rp1hi = rp1 + 0x2580;
                    long long rp1lo = rp1 + 0x257f;

                    unsigned int intDelay2 = (unsigned int)(finalDelay * 2.0);
                    long long intDelay2L = (long long)(int)intDelay2;
                    double frac2 = (double)((float)(finalDelay * 2.0) - (float)intDelay2L);
                    long long rp2 = ((unsigned long long)writePos - (unsigned long long)intDelay2) + sampleIdx;
                    long long rp2hi = rp2 + 0x2580;
                    long long rp2lo = rp2 + 0x257f;

                    *(float *)(frameBufOff + (int)buf) =
                        *(float *)((int)((rp1hi + (long long)((int)rp1hi / 0x2580) * -0x2580 & 0xffffffffU) << 2) + chanBuf) *
                        (float)(1.0 - (double)(float)(finalDelay - (double)intDelay1)) + input;

                    float mixed = (float)((double)(float)(
                        (double)*(float *)((int)((rp1lo + (long long)((int)rp1lo / 0x2580) * -0x2580 & 0xffffffffU) << 2) + chanBuf) *
                        (double)(float)(finalDelay - (double)intDelay1) +
                        (double)*(float *)(frameBufOff + (int)buf)) * 0.5);
                    *(float *)(frameBufOff + (int)buf) = mixed;

                    *(float *)(frameBufOff + (int)buf) =
                        *(float *)((int)((rp2hi + (long long)((int)rp2hi / 0x2580) * -0x2580 & 0xffffffffU) << 2) + chanBuf2) *
                        (float)(1.0 - frac2) * (float)mFeedbackFrac + mixed;

                    float outSample = (float)((double)*(float *)((int)((rp2lo + (long long)((int)rp2lo / 0x2580) * -0x2580 & 0xffffffffU) << 2) + chanBuf2) * frac2) *
                        (float)mFeedbackFrac + *(float *)(frameBufOff + (int)buf);
                    *(float *)(frameBufOff + (int)buf) = outSample;

                    *(float *)(chanBuf2 + writeOff) = outSample;

                    *(float *)(frameBufOff + (int)buf) = (float)((double)outSample * 2.0 - (double)input);
                } while ((int)chanIdx < numChans);
            }

            sampleIdx = sampleIdx + 1;
            lfoPhase = (double)(float)(curRate + lfoPhase);
            curRate = (double)(float)(rateStep + curRate);
            frameBase = frameBase + (long long)numChans;
            curDepth = (double)(float)(depthStep + curDepth);
        } while ((int)sampleIdx < numSamples);
    }

    unk1c = (float)curDepth;
    unk2c = (float)curRate;
    unk24 = (float)lfoPhase;
    mWritePos = (mWritePos + numSamples) % 0x2580;
    if (6.2831854820251465 < lfoPhase) {
        unk24 = (float)(lfoPhase - 6.2831854820251465);
    }
}
