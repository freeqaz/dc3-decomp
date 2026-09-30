#include "synth\StreamReceiver.h"
#include "os\Debug.h"
#ifdef HX_NATIVE
#include "platform\StreamReceiver_Native.h"
#else
extern "C" void XMemCpy(void *, const void *, int);
#endif

// Target: StreamReceiver.obj .bss:0x0 (0x830E3284), zero.
StreamReceiverFactoryFunc *StreamReceiver::sFactory;

StreamReceiver::StreamReceiver(int numBuffers, bool slip)
    : mSlipEnabled(slip), mNumBuffers(numBuffers), mBuffer(), mRingFreeSpace(0),
      mState(kInit), mSendTarget(0), mWantToSend(false), mSending(false), mBuffersSent(0),
      mStarving(false), mEndData(false), mDoneBufferCounter(0), mLastPlayCursor(0) {
    MILO_ASSERT(numBuffers > 0, 0x33);
#ifdef HX_NATIVE
    mNativeBytesWritten = 0;
#endif
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

    for (; totalPlayed >= bufferOffset; totalPlayed = totalPlayed - numBuffers * 0x4000)
        ;
    return totalPlayed;
#endif
}

void StreamReceiver::WriteData(const void *data, int bytes) {
#ifdef HX_NATIVE
    // On native, forward data directly to the platform receiver's ring buffer
    // via StartSendImpl. The base class mBuffer is not used — audio output
    // reads from StreamReceiverNative::mPCMBuf instead.
    StartSendImpl((unsigned char *)data, bytes, 0);
    mNativeBytesWritten += bytes;
    mSending = true;
    mWantToSend = false;
#else
    MILO_ASSERT(bytes > 0 && bytes <= BytesWriteable(), 0x51);
    XMemCpy(mBuffer + mRingFreeSpace, data, bytes);
    mRingFreeSpace += bytes;
#endif
}

void StreamReceiver::Poll() {
#ifdef HX_NATIVE
    if (mSending && SendDoneImpl()) {
        mSending = false;
        mBuffersSent++;
    }
    // On the image, after EndData() every 0x4000-byte buffer the voice
    // finishes is refilled from the zero-padded local ring and counted in
    // mDoneBufferCounter (the #else body below), and StandardStream reports
    // kFinished once the count passes mNumBuffers + 2. Because the local ring's
    // head is always a whole number of buffers into the stream, that lands when
    // the play cursor reaches the buffer boundary one whole buffer past the one
    // holding the last byte written -- 1..2 buffers of silence after the audio,
    // whatever mNumBuffers is. Native has no buffer cycle (WriteData feeds the
    // platform ring directly), so place the same point from the byte counts.
    // Counting one per Poll() once drained, as native used to, finished
    // mNumBuffers + 3 frames after the audio: 36..90 ms, not 186..372.
    if (mEndData && mDoneBufferCounter <= mNumBuffers + 2) {
        unsigned long long finishAt =
            ((unsigned long long)(mNativeBytesWritten + 0x3FFF) / 0x4000 + 1) * 0x4000;
        if (GetBytesPlayed() >= finishAt) {
            mDoneBufferCounter = mNumBuffers + 3;
        }
    }
#else
    if ((unsigned int)mState >= kReady) {
        if ((unsigned int)mState == kReady) {
            // Already primed; nothing to poll.
        } else if ((unsigned int)mState >= kStopped + 1) {
            MILO_FAIL("bad state logic.\n");
        } else {
            int playCursor = GetPlayCursor();
            int activeBuf = playCursor / 0x4000;
            mLastPlayCursor = playCursor;
            MILO_ASSERT(activeBuf >= 0 && activeBuf < mNumBuffers, 0xc2);
            if (!mSlipEnabled && activeBuf != mSendTarget) {
                mWantToSend = true;
            }
            int diff = activeBuf - mSendTarget;
            if (diff == mNumBuffers / 2 || diff == -(mNumBuffers / 2)) {
                mWantToSend = true;
            }
        }
    } else {
        mWantToSend = true;
    }
    if (mWantToSend && mState != kInit && kStreamRcvrBufSize - mRingFreeSpace != 0) {
        mStarving = true;
    }
    if (mWantToSend && mRingFreeSpace >= 0x4000 && !mSending) {
        StartSendImpl(mBuffer, 0x4000, mSendTarget);
        mBuffersSent++;
        if (mBuffersSent >= 700000) {
            mBuffersSent -= mNumBuffers;
        }
        int sendTarget = mSendTarget;
        mWantToSend = false;
        mSending = true;
        mSendTarget = sendTarget + 1;
        if (sendTarget + 1 == mNumBuffers) {
            mSendTarget = 0;
        }
    }
    if (mSending) {
        if (SendDoneImpl()) {
            mSending = false;
            mStarving = false;
            if (mSendTarget == 0 && mState == kInit) {
                mState = kReady;
                mWantToSend = false;
            }
            int overflow = mRingFreeSpace - 0x4000;
            MILO_ASSERT(overflow >= 0, 0x134);
            if (overflow != 0) {
                XMemCpy(mBuffer, mBuffer + 0x4000, overflow);
            }
            mRingFreeSpace -= 0x4000;
            if (mEndData) {
                memset(&mBuffer[mRingFreeSpace], 0, kStreamRcvrBufSize - mRingFreeSpace);
                mRingFreeSpace = kStreamRcvrBufSize;
                mDoneBufferCounter++;
            }
        }
    }
#endif
}

#ifndef HX_NATIVE
StreamReceiver *StreamReceiver::New(int i1, int i2, bool b3, int i4) {
    MILO_ASSERT(sFactory, 0x1C);
    return sFactory(i1, i2, b3, i4);
}
#endif
