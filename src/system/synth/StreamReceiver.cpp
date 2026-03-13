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
#ifdef HX_NATIVE
    // TODO: This is a hack — GetBytesPlayed() is non-virtual so we static_cast
    // to the native subclass. Once the audio subsystem is properly wired up,
    // this should be revisited (make virtual, or track in base class).
    auto *native = static_cast<StreamReceiverNative *>(this);
    return native->GetTotalBytesPlayed();
#else
    return 0;
#endif
}

void StreamReceiver::WriteData(const void *data, int size) {
#ifdef HX_NATIVE
    // On native, write directly to platform ring buffer via StartSendImpl
    StartSendImpl((unsigned char *)data, size, 0);
    if (mState == kInit)
        mState = kReady;
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
