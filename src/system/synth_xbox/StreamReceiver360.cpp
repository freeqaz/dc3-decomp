#include "synth_xbox/StreamReceiver360.h"
#include "synth_xbox/FxSend.h"
#include "os/Debug.h"
#include "math/Utl.h"
#include "utl/PoolAlloc.h"
#include "utl/Std.h"
#include "utl/MemMgr.h"

extern void *_MemAllocTemp(int, const char *, int, const char *, int);
extern "C" void XMemCpy(void *, const void *, int);

extern "C" int lbl_8316C864;
extern "C" int lbl_8316C860;

StreamReceiver360::StreamReceiver360(int numBuffers, int sampleRate, bool slip)
    : StreamReceiver(numBuffers, slip), mStreamBuf(0), unk8038(0), mVoice(0),
      mSampleRate(sampleRate), mNumBufs(numBuffers), mVolume(1.0f), mPan(0.0f), mSpeed(1.0f),
      unk8078(0), unk807C(false) {
    mStreamBuf = (unsigned char *)_MemAllocTemp(
        numBuffers << 14, "StreamReceiver.cpp", 0x33, "StreamBuffer", 0);

    Voice *v = (Voice *)PoolAlloc(0x7c, 0x7c, "e:\\lazer_build_gmc1\\system\\src\\synth360\\Voice.h", 0x28, "Voice");
    if (v) {
        v = new (v) Voice(false, 1, false);
    }
    mVoice = v;

    if (mVoice) {
        mVoice->SetData(mStreamBuf, numBuffers << 14, 0);
        mVoice->SetLoopRegion(0, -1);
        mVoice->SetSampleRate(sampleRate);

        if (!slip) {
            unk8038 = mVoice;
        } else {
            mVoice->SetVolume(0.0f);
        }
    }
}

StreamReceiver360::~StreamReceiver360() {
    if (mVoice != 0) {
        delete mVoice;
    }
    if (mSlipEnabled) {
        if (unk8038 != 0) {
            delete unk8038;
        }
    }
    DeleteAll(mPendingVoices);
    MemFree(mStreamBuf);
}

void StreamReceiver360::SetVolume(float f) {
    mVolume = f;
    if (unk8038 == 0) return;
    unk8038->SetVolume(f);
}

void StreamReceiver360::SetPan(float f) {
    mPan = f;
    if (unk8038 == 0) return;
    unk8038->SetPan(f);
}

void StreamReceiver360::SetSpeed(float f) {
    mSpeed = f;
    mVoice->SetSpeed(f);
}

void StreamReceiver360::SetADSR(const ADSRImpl &adsr) {
    memcpy(&mADSR, &adsr, sizeof(ADSRImpl));
    UpdateADSR();
}

void StreamReceiver360::Tag() {
    unk807C = true;
    if (unk8038) {
        int val;
        Voice *target;
        if (mVoice != 0) {
            unk8038->unk50 = 1;
            val = 2;
            target = mVoice;
        } else {
            if (unk8038 == 0) return;
            val = 3;
            target = unk8038;
        }
        target->unk50 = val;
        return;
    }
    if (mVoice == 0) return;
    mVoice->unk50 = 4;
}

void StreamReceiver360::Poll() {
    StreamReceiver::Poll();
    if (mVoice != 0 && mVoice->IsPlaying()) {
        lbl_8316C864 = lbl_8316C864 + 1;
    }
    if (unk8038 != 0 && unk8038->IsPlaying()) {
        lbl_8316C860 = lbl_8316C860 + 1;
    }
    while (mPendingVoices.begin() != mPendingVoices.end()) {
        if (mPendingVoices.front()->IsPlaying()) break;
        Voice *v = mPendingVoices.front();
        if (v != 0) {
            delete v;
        }
        mPendingVoices.erase(mPendingVoices.begin());
    }
}

void StreamReceiver360::SetSlipOffset(float f) {
    MILO_ASSERT(mSlipEnabled, 0xC5);
    SlipStop();
    Voice *v = (Voice *)PoolAlloc(0x7c, 0x7c, "e:\\lazer_build_gmc1\\system\\src\\synth360\\Voice.h", 0x28, "Voice");
    if (v) {
        v = new (v) Voice(false, 1, false);
    }
    unk8038 = v;
    if (unk807C) {
        Tag();
    }
    unk8038->SetData(mStreamBuf, mNumBufs << 14, 0);
    unk8038->SetLoopRegion(0, -1);
    unk8038->SetSampleRate(mSampleRate);
    int cursor = GetPlayCursor();
    int halfCursor = cursor / 2;
    int halfBuf = (mNumBufs << 14) / 2;
    int startSamp;
    if (halfBuf == 0) {
        startSamp = 0;
    } else {
        float fOff = f * 0.001f;
        int offset = (int)(fOff * (float)mSampleRate);
        startSamp = (offset + halfCursor) % halfBuf;
        if (startSamp < 0) startSamp += halfBuf;
    }
    unk8038->SetStartSamp(startSamp);
    unk8038->SetVolume(mVolume);
    unk8038->SetPan(mPan);
    unk8038->SetSpeed(mSpeed);
    UpdateADSR();
    SetFXSend((FxSend *)unk8078);
    unk8038->Start();
}

void StreamReceiver360::SlipStop() {
    MILO_ASSERT(mSlipEnabled, 0xEC);
    if (unk8038 != 0) {
        unk8038->Stop(false);
        mPendingVoices.push_back(unk8038);
        unk8038 = 0;
    }
}

void StreamReceiver360::SetSlipSpeed(float f) {
    MILO_ASSERT(mSlipEnabled, 0xFA);
    if (unk8038 != 0) {
        unk8038->SetSpeed(f);
    }
}

float StreamReceiver360::GetSlipOffset() {
    MILO_ASSERT(mSlipEnabled, 0x102);
    if (unk8038 != 0) {
        int mainAddr = mVoice->GetAddr();
        int slipAddr = unk8038->GetAddr();
        float halfBuf = (float)(mNumBufs << 14) * 0.5f;
        float neg = -halfBuf;
        float slipOff = Mod((float)(slipAddr - mainAddr) - neg, halfBuf - neg) + neg;
        return ((slipOff * 0.5f) / (float)mSampleRate) * 1000.0f;
    }
    return 0.0f;
}

int StreamReceiver360::GetPlayCursor() {
    return mVoice->GetAddr();
}

void StreamReceiver360::PauseImpl(bool b) {
    mVoice->Pause(b);
    if (mSlipEnabled && unk8038 != 0) {
        unk8038->Pause(b);
    }
}

void StreamReceiver360::PlayImpl() {
    mVoice->Start();
}

void StreamReceiver360::StartSendImpl(unsigned char *buf, int len, int idx) {
    XMemCpy((idx << 14) + mStreamBuf, buf, len);
}

bool StreamReceiver360::SendDoneImpl() {
    return true;
}

void StreamReceiver360::SetFXSend(FxSend *fx) {
    unk8078 = (int)fx;
    if (unk8038) {
        FxSend360 *fx360 = dynamic_cast<FxSend360 *>(fx);
        unk8038->SetSend(fx360);
    }
}

void StreamReceiver360::UpdateADSR() {
    if (unk8038 != 0) {
        unk8038->unk30 = mADSR.GetAttackRate();
        unk8038->unk34 = mADSR.GetReleaseRate();
    }
}

StreamReceiver *New360Receiver(int numBuffers, int sampleRate, bool slip, int) {
    return new StreamReceiver360(numBuffers, sampleRate, slip);
}

void StreamReceiver360::Init() {
    StreamReceiver::sFactory = New360Receiver;
}
