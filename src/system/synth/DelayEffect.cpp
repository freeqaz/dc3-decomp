#include "synth/DelayEffect.h"
#include "Common_Xbox.h"
#include "math/Decibels.h"
#include "os/Debug.h"
#include "xdk/xaudio2/xaudio2.h"

DelayEffect::DelayEffect(IXAudioBatchAllocator *ix)
    : mDelaySamples(24000), mWritePos(0), mDecay(0.3f), mWetAmount(0.5f) {
    DspAllocate(mBuffer, 0x2ee00, ix);
}

DelayEffect::~DelayEffect() { DspFree(mBuffer); }

void DelayEffect::Reset() { DspClearBuffer(mBuffer, 0x2ee00); }

void DelayEffect::SetParameters(DelayEffect::Params const &params) {
    SetParameter(0, params.mDelaySamples);
    mDecay = DbToRatio(params.mDecayDb);
    mWetAmount = params.mWetPercent / 100.0f;
}

// SetParameter must NOT be in this TU on PPC — the compiler inlines it
// into SetParameters, breaking the 100% match. On native it's needed for linking.
#ifdef HX_NATIVE
void DelayEffect::SetParameter(int param, float value) {
    if ((unsigned int)param >= 1) {
        if ((unsigned int)param != 1) {
            if ((unsigned int)param >= 3) {
                TheDebug.Fail(MakeString("bad parameter %i", param), 0);
                return;
            }
            mWetAmount = value * 0.01f;
            return;
        }
        mDecay = DbToRatio(value);
        return;
    }

    int delaySamples = (int)(value * 48000.0f);
    mDelaySamples = delaySamples;
    if (delaySamples > 0x176FF) {
        delaySamples = 0x176FF;
    } else if (delaySamples < 1) {
        delaySamples = 1;
    }
    mDelaySamples = delaySamples;
}
#endif

static const int kMaxDelaySamps = 96000;

void DelayEffect::Process(float *buf, int numSamples, int numChans) {
    MILO_ASSERT(numChans <= 2, 0x27);
    int writePos = mWritePos;
    if (numChans == 1) {
        for (int i = 0; i < numSamples; i++) {
            int readPos = writePos - mDelaySamples;
            if (readPos < 0) readPos += kMaxDelaySamps;
            MILO_ASSERT((0) <= (readPos) && (readPos) < (kMaxDelaySamps), 0x32);
            MILO_ASSERT((0) <= (writePos) && (writePos) < (kMaxDelaySamps), 0x33);
            float input = buf[i];
            float delayed = mBuffer[readPos] * mDecay;
            buf[i] = delayed;
            int nextWritePos = writePos + 1;
            if (nextWritePos >= kMaxDelaySamps) nextWritePos = 0;
            mBuffer[writePos] = delayed + input;
            writePos = nextWritePos;
        }
    } else {
        float wetAmount = mWetAmount;
        float dryAmount = 1.0f - mWetAmount;
        for (int i = 0; i < numSamples; i++) {
            int readPos = writePos - mDelaySamples;
            if (readPos < 0) readPos += kMaxDelaySamps;
            float *frame = &buf[i * numChans];
            float inLeft = frame[0];
            float inRight = frame[1];
            int nextWritePos = writePos + 1;
            if (nextWritePos >= kMaxDelaySamps) nextWritePos = 0;
            float outLeft = (mBuffer[readPos + kMaxDelaySamps] * wetAmount + mBuffer[readPos] * dryAmount) * mDecay;
            frame[0] = outLeft;
            mBuffer[writePos] = outLeft + inLeft * dryAmount + (inRight + inLeft) * 0.5f * wetAmount;
            float delayedDry = mBuffer[readPos + kMaxDelaySamps] * mDecay;
            float delayedWet = mBuffer[readPos] * mDecay;
            float outRight = delayedDry * dryAmount + delayedWet * wetAmount;
            frame[1] = outRight;
            mBuffer[writePos + kMaxDelaySamps] = inRight * dryAmount + outRight;
            writePos = nextWritePos;
        }
    }
    mWritePos = writePos;
}
