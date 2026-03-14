// DC3 Native Port - StreamReceiverNative implementation
// Bridges engine's StreamReceiver protocol to AudioDevice output.

#include "platform/StreamReceiver_Native.h"

#include <cstring>
#include <algorithm>
#include <cmath>

// Define the static factory pointer (declared in StreamReceiver.h)
StreamReceiverFactoryFunc *StreamReceiver::sFactory = nullptr;

// StreamReceiver::New — dispatches to platform factory (declared in StreamReceiver.h)
StreamReceiver *StreamReceiver::New(int numBuffers, int sampleRate, bool slip, int channel) {
    MILO_ASSERT(sFactory, 0x20);
    return sFactory(numBuffers, sampleRate, slip, channel);
}

// StreamReceiver::GetBytesPlayed and ::Poll — now in StreamReceiver.cpp

StreamReceiverNative::StreamReceiverNative(int numBuffers, bool slip)
    : StreamReceiver(numBuffers, slip),
      mWriteCursor(0), mPlayCursor(0),
      mVolume(1.0f), mPan(0.0f), mSpeed(1.0f),
      mPlaying(false), mPaused(false), mSampleRate(44100),
      mTotalBytesPlayed(0) {
    memset(mPCMBuf, 0, sizeof(mPCMBuf));
    mState = kReady;
}

StreamReceiverNative::~StreamReceiverNative() {
    if (mPlaying) {
        AudioDevice::GetInstance().RemoveSource(this);
    }
}

StreamReceiver *StreamReceiverNative::Create(int numBuffers, int sampleRate, bool slip, int /*channel*/) {
    StreamReceiverNative *rcvr = new StreamReceiverNative(numBuffers, slip);
    rcvr->mSampleRate = sampleRate;
    return rcvr;
}

void StreamReceiverNative::PlayImpl() {
    static int sPlayCount = 0;
    sPlayCount++;
    fprintf(stderr, "DC3 StreamReceiverNative::PlayImpl[%d] this=%p wc=%d pc=%d\n",
        sPlayCount, (void*)this, mWriteCursor, mPlayCursor);
    mPlaying = true;
    mPaused = false;
    AudioDevice::GetInstance().AddSource(this);
}

void StreamReceiverNative::PauseImpl(bool pause) {
    mPaused = pause;
}

void StreamReceiverNative::StartSendImpl(unsigned char *data, int size, int /*targetIdx*/) {
    static int sSendCount = 0;
    sSendCount++;
    if (sSendCount <= 10) {
        fprintf(stderr, "DC3 StartSendImpl[%d] size=%d wc=%d pc=%d\n",
            sSendCount, size, mWriteCursor, mPlayCursor);
    }
    int wc = mWriteCursor;
    int pc = mPlayCursor;
    int bufSamples = kPCMBufSize / 2;

    // Flow control: don't overflow the ring buffer
    int availBytes = kPCMBufSize - (wc - pc);
    if (size > availBytes) {
        size = availBytes;
        if (size <= 0) return;
    }

    int writePos = (wc / 2) % bufSamples;
    int samplesIn = size / 2;

    int16_t *src = (int16_t *)data;
    for (int i = 0; i < samplesIn; i++) {
        mPCMBuf[(writePos + i) % bufSamples] = src[i];
    }
    mWriteCursor = wc + size;
    mSending = true;
    mWantToSend = false;
}

bool StreamReceiverNative::SendDoneImpl() {
    return true;
}

int StreamReceiverNative::GetPlayCursor() {
    // Update base class mLastPlayCursor with total bytes played for GetBytesPlayed()
    mLastPlayCursor = mPlayCursor;
    return mPlayCursor % kStreamRcvrBufSize;
}

int StreamReceiverNative::RenderAudio(float *output, int frameCount) {
    static int sRenderDbg = 0;
    if (!mPlaying || mPaused) {
        if (sRenderDbg < 5) {
            sRenderDbg++;
            fprintf(stderr, "DC3 RenderAudio: NOT playing/paused — playing=%d paused=%d\n", mPlaying, mPaused);
        }
        memset(output, 0, frameCount * 2 * sizeof(float));
        return frameCount;
    }

    int wc = mWriteCursor;
    int pc = mPlayCursor;
    int availBytes = wc - pc;
    int availSamples = availBytes / 2;
    if (sRenderDbg < 20) {
        sRenderDbg++;
        fprintf(stderr, "DC3 RenderAudio: wc=%d pc=%d avail=%d frames=%d vol=%.3f\n",
            wc, pc, availSamples, frameCount, mVolume);
    }
    int samplesToRender = std::min(frameCount, availSamples);

    if (samplesToRender <= 0) {
        memset(output, 0, frameCount * 2 * sizeof(float));
        return frameCount;
    }

    int bufSamples = kPCMBufSize / 2;
    int readPos = (pc / 2) % bufSamples;

    float volL = mVolume * std::max(0.0f, 1.0f - mPan);
    float volR = mVolume * std::max(0.0f, 1.0f + mPan);

    for (int i = 0; i < samplesToRender; i++) {
        float sample = mPCMBuf[(readPos + i) % bufSamples] / 32768.0f;
        output[i * 2 + 0] = sample * volL;
        output[i * 2 + 1] = sample * volR;
    }

    for (int i = samplesToRender; i < frameCount; i++) {
        output[i * 2 + 0] = 0.0f;
        output[i * 2 + 1] = 0.0f;
    }

    int bytesConsumed = samplesToRender * 2;
    mPlayCursor = pc + bytesConsumed;
    mTotalBytesPlayed += bytesConsumed;

    return frameCount;
}
