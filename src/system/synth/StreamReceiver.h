#pragma once
#include "synth\ADSR.h"
#include "utl\MemMgr.h"
#ifdef HX_NATIVE
#include <vector>
#endif

class StreamReceiver;
typedef StreamReceiver *StreamReceiverFactoryFunc(int, int, bool, int);

#define kStreamRcvrBufSize 0x8000

class StreamReceiver {
    friend class StandardStream;
public:
    enum State {
        kInit = 0,
        kReady = 1,
        kPlaying = 2,
        kStopped = 3,
    };
    StreamReceiver(int, bool);
    virtual ~StreamReceiver();
    virtual void SetVolume(float) = 0;
    virtual void SetPan(float) = 0;
    virtual void SetSpeed(float) = 0;
    virtual void SetADSR(const ADSRImpl &) {}
    virtual void Tag() {}
    virtual void Poll();
    virtual void SetSlipOffset(float) = 0;
    virtual void SlipStop() = 0;
    virtual void SetSlipSpeed(float) = 0;
    virtual float GetSlipOffset() = 0;
    virtual void SetFXSend(class FxSend *) {}
    virtual int GetPlayCursor() = 0;
    virtual void PauseImpl(bool) = 0;
    virtual void PlayImpl() = 0;
    virtual void StartSendImpl(unsigned char *, int, int) = 0;
    virtual bool SendDoneImpl() = 0;
#ifdef HX_NATIVE
    /** True when the audio output has consumed all buffered data.
     *  Nothing calls it any more (Poll() places kFinished from byte counts,
     *  051646060); it stays because both StreamReceiver_Native.h copies --
     *  this repo's and the shared engine's -- override it. */
    virtual bool IsOutputDrained() const { return true; }
#endif

#ifdef HX_NATIVE
    static void *operator new(size_t s) {
#else
    static void *operator new(unsigned int s) {
#endif
        return _MemAllocTemp(s, __FILE__, 0x23, "StreamReceiver", 0);
    }
#ifdef HX_NATIVE
    static void *operator new(size_t s, void *place) { return place; }
#else
    static void *operator new(unsigned int s, void *place) { return place; }
#endif
    static void operator delete(void *v) { MemFree(v, __FILE__, 0x23, "StreamReceiver"); }

    int BytesWriteable();
    bool Ready();
    void EndData();
    void Play();
    void Stop();
    u64 GetBytesPlayed();
    void WriteData(const void *, int);

    static StreamReceiver *New(int, int, bool, int);

protected:
#ifdef HX_NATIVE
public:
    static StreamReceiverFactoryFunc *sFactory;
protected:
#else
    static StreamReceiverFactoryFunc *sFactory;
#endif

    bool mSlipEnabled; // 0x4
    int mNumBuffers; // 0x8
    unsigned char mBuffer[kStreamRcvrBufSize]; // 0xc
    int mRingFreeSpace; // 0x800c
    State mState; // 0x8010
    int mSendTarget; // 0x8014
    bool mWantToSend; // 0x8018
    bool mSending; // 0x8019
    int mBuffersSent; // 0x801c
    bool mStarving; // 0x8020
    bool mEndData; // 0x8021
    int mDoneBufferCounter; // 0x8024
    int mLastPlayCursor; // 0x8028
#ifdef HX_NATIVE
    /** Native only: bytes handed to WriteData(). Poll() needs it to place
     *  kFinished where the image's buffer cycle does. */
    int mNativeBytesWritten;
    /** Native only: bytes the image's voice ring would have taken from the
     *  local ring so far -- Poll()'s model of the image's buffer cycle, which
     *  keeps mRingFreeSpace (and so BytesWriteable(), ConsumeData's flow
     *  control) the image's count. */
    unsigned long long mNativeVoiceBytes;
    /** Native only: PCM handed to WriteData() that the platform receiver's
     *  ring has not taken yet (main thread only; the audio thread never reads
     *  it). The image keeps up to mNumBuffers * 0x4000 + 0x8000 bytes in
     *  flight; the platform ring is smaller, so the rest waits here. */
    std::vector<unsigned char> mNativeStage;
    int mNativeStageHead;
    /** Native only: EndData() arrived while PCM was still staged. */
    bool mNativeEndPending;
    void NativePump();
#endif
};
