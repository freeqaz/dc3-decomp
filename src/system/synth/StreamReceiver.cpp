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
    mNativeVoiceBytes = 0;
    mNativeStageHead = 0;
    mNativeEndPending = false;
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
#ifdef HX_NATIVE
        // The platform receiver plays silence (and counts it as played) once
        // mEndData is set and its ring is empty, so the flag must not reach it
        // while PCM is still staged for it: NativePump() sets it once the last
        // staged byte is in the platform ring.
        mNativeEndPending = true;
        NativePump();
#else
        mEndData = true;
#endif
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
    // The image copies into its 0x8000-byte local ring (#else) and Poll() hands
    // that ring to the voice one 0x4000-byte buffer at a time. Natively the
    // bytes queue in mNativeStage and NativePump() moves them into the platform
    // receiver's ring (StreamReceiverNative's, which the audio thread plays) as
    // it has room; mRingFreeSpace counts them as the image's local ring does, so
    // BytesWriteable() -- the flow control StandardStream::ConsumeData reads --
    // is the image's.
    const unsigned char *src = (const unsigned char *)data;
    mNativeStage.insert(mNativeStage.end(), src, src + bytes);
    mRingFreeSpace += bytes;
    mNativeBytesWritten += bytes;
    NativePump();
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
    NativePump();
    // The image's buffer cycle (#else): the voice ring holds mNumBuffers
    // 0x4000-byte buffers, and each Poll() moves at most one buffer from the
    // local ring into it -- while priming, then each time the play cursor has
    // left a buffer (activeBuf != mSendTarget, refilling the buffer just
    // played), and only when the local ring holds a whole buffer. So the voice
    // never runs more than mNumBuffers buffers past the start of the buffer
    // being played, and the local ring then refills to 0x8000: in steady state
    // ConsumeData has decoded (mNumBuffers + 2) * 0x4000 bytes past that point.
    // After EndData() the local ring stays zero-padded full. (Slip channels,
    // which refill mNumBuffers / 2 behind, are not modelled: nothing in DC3
    // enables slip streaming and the native receiver cannot slip.)
    {
        unsigned long long voiceLimit =
            (GetBytesPlayed() / 0x4000 + (unsigned long long)mNumBuffers) * 0x4000;
        if (mNativeVoiceBytes < voiceLimit && mRingFreeSpace >= 0x4000) {
            mNativeVoiceBytes += 0x4000;
            mRingFreeSpace -= 0x4000;
            if (mEndData || mNativeEndPending)
                mRingFreeSpace = kStreamRcvrBufSize;
        }
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

#ifdef HX_NATIVE
// Move staged PCM into the platform receiver's ring, as much as it has room
// for. StreamReceiverNative's ring is a fixed 64 KB (743 ms of 44.1 kHz) that
// StartSendImpl silently truncates at, so ask it first; any other receiver (a
// test sink, StreamReceiverFile) takes everything.
void StreamReceiver::NativePump() {
    int staged = (int)mNativeStage.size() - mNativeStageHead;
    if (staged > 0) {
        int room = staged;
        StreamReceiverNative *rcvr = dynamic_cast<StreamReceiverNative *>(this);
        if (rcvr)
            room = rcvr->AvailableWriteBytes();
        int n = staged < room ? staged : room;
        if (n > 0) {
            StartSendImpl(&mNativeStage[mNativeStageHead], n, 0);
            mNativeStageHead += n;
            mSending = true;
            mWantToSend = false;
            if (mNativeStageHead == (int)mNativeStage.size()) {
                mNativeStage.clear();
                mNativeStageHead = 0;
            } else if (mNativeStageHead >= 0x10000) {
                mNativeStage.erase(
                    mNativeStage.begin(), mNativeStage.begin() + mNativeStageHead
                );
                mNativeStageHead = 0;
            }
        }
    }
    if (mNativeEndPending && mNativeStageHead == (int)mNativeStage.size()) {
        mNativeEndPending = false;
        mEndData = true;
    }
}
#endif

#ifndef HX_NATIVE
StreamReceiver *StreamReceiver::New(int i1, int i2, bool b3, int i4) {
    MILO_ASSERT(sFactory, 0x1C);
    return sFactory(i1, i2, b3, i4);
}
#endif
