#include "synth/StreamReceiver.h"
#include "os/Debug.h"
#ifdef HX_NATIVE
#include "platform/StreamReceiver_Native.h"
#endif

StreamReceiver::StreamReceiver(int numBuffers, bool slip)
    : mSlipEnabled(slip), mNumBuffers(numBuffers), mBuffer(), mRingFreeSpace(0),
      mState(kInit), mSendTarget(0), mWantToSend(false), mSending(false), mBuffersSent(0),
      mStarving(false), mEndData(false), mDoneBufferCounter(0), mLastPlayCursor(0) {
    MILO_ASSERT(numBuffers > 0, 0x33);
}

StreamReceiver::~StreamReceiver() {}

int StreamReceiver::BytesWriteable() { return kStreamRcvrBufSize - mRingFreeSpace; }
bool StreamReceiver::Ready() { return mState != kInit; }

void StreamReceiver::EndData() {
    if (!mEndData) {
        if (mRingFreeSpace < kStreamRcvrBufSize) {
            memset(&mBuffer[mRingFreeSpace], 0, kStreamRcvrBufSize - mRingFreeSpace);
            mRingFreeSpace = kStreamRcvrBufSize;
        }
        mEndData = true;
    }
}

void StreamReceiver::Play() {
    MILO_ASSERT(Ready(), 0x91);
    if (mState != kPlaying) {
        if (mState == kStopped) {
            PauseImpl(false);
        } else {
            PlayImpl();
        }
        mState = kPlaying;
    }
}

void StreamReceiver::Stop() {
    MILO_ASSERT(mState == kPlaying || mState == kStopped, 0xA6);
    if (mState == kPlaying) {
        PauseImpl(true);
        mState = kStopped;
    }
}

u64 StreamReceiver::GetBytesPlayed() {
    if (mState == kInit) {
        return 0;
    }
#ifdef HX_NATIVE
    // Native: GetPlayCursor() updates mLastPlayCursor with total bytes consumed
    GetPlayCursor();
    return (u64)mLastPlayCursor;
#else
    unsigned long long numBuffers = (unsigned long long)mNumBuffers;
    unsigned long long buffersSent = (unsigned long long)mBuffersSent;
    unsigned long long bufferOffset = buffersSent << 0xe;
    unsigned long long totalPlayed = (unsigned long long)mLastPlayCursor + (buffersSent / numBuffers) * numBuffers * 0x4000;

    for (; bufferOffset <= totalPlayed; totalPlayed = totalPlayed - numBuffers * 0x4000)
        ;
    return totalPlayed;
#endif
}

void StreamReceiver::WriteData(const void *data, int size) {
    MILO_ASSERT(size > 0 && size <= BytesWriteable(), 0x51);
#ifdef HX_NATIVE
    // On native, forward data directly to the platform receiver's ring buffer
    // via StartSendImpl, then reset mRingFreeSpace. The base class mBuffer is
    // not used — audio output reads from StreamReceiverNative::mPCMBuf instead.
    StartSendImpl((unsigned char *)data, size, 0);
    // Keep mRingFreeSpace at 0 so BytesWriteable() always returns full capacity.
    // Flow control is handled by the PCM ring buffer's write/play cursor gap.
#else
    memcpy(mBuffer + mRingFreeSpace, data, size);
    mRingFreeSpace += size;
#endif
}

void StreamReceiver::Poll() {
#ifdef HX_NATIVE
    if (mSending && SendDoneImpl()) {
        mSending = false;
        mBuffersSent++;
    }
#endif
}

#ifndef HX_NATIVE
StreamReceiver *StreamReceiver::New(int i1, int i2, bool b3, int i4) {
    MILO_ASSERT(sFactory, 0x1C);
    return sFactory(i1, i2, b3, i4);
}
#endif
