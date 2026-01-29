#include "synth_xbox/StreamReceiver360.h"
#include "os/Debug.h"

extern Voice *PoolAlloc(int, int, const char *, int, const char *);
extern void *_MemAllocTemp(int, const char *, int, const char *, int);

StreamReceiver360::StreamReceiver360(int numBuffers, int sampleRate, bool slip)
    : StreamReceiver(numBuffers, slip), mStreamBuffer(0), mVoice(0), unk8034(0),
      unk8038(0), unk803C(sampleRate) {
    mStreamBuffer = (unsigned char *)_MemAllocTemp(
        numBuffers << 14, "StreamBuffer", 0x33,
        "e:\\lazer_build_gmc1\\system\\src\\synth_xbox\\StreamReceiver.h", 0);

    mVoice = PoolAlloc(0x7c, 0x7c, "Voice", 0x28,
                       "e:\\lazer_build_gmc1\\system\\src\\synth\\StreamReceiver.h");

    if (mVoice) {
        mVoice = new (mVoice) Voice(false, 0, true);
    }

    if (mVoice) {
        mVoice->SetData(mStreamBuffer, numBuffers << 14, 0);
        mVoice->SetLoopRegion(0, -1);
        mVoice->SetSampleRate(sampleRate);

        if (!slip) {
            unk8038 = (int)mVoice;
        } else {
            mVoice->SetVolume(0.0f);
        }
    }
}

StreamReceiver360::~StreamReceiver360() {}

void StreamReceiver360::SetVolume(float f) {}
void StreamReceiver360::SetPan(float f) {}
void StreamReceiver360::SetSpeed(float f) {}
void StreamReceiver360::Poll() {}
void StreamReceiver360::SetSlipOffset(float f) {}
void StreamReceiver360::SlipStop() {}
void StreamReceiver360::SetSlipSpeed(float f) {}
float StreamReceiver360::GetSlipOffset() { return 0.0f; }
int StreamReceiver360::GetPlayCursor() { return 0; }
void StreamReceiver360::PauseImpl(bool b) {}
void StreamReceiver360::PlayImpl() {}
void StreamReceiver360::StartSendImpl(unsigned char *p, int i, int j) {}
bool StreamReceiver360::SendDoneImpl() { return false; }
