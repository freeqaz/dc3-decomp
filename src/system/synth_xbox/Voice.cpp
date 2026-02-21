#include "synth_xbox/Voice.h"
#include "math/Utl.h"
#include "os/Debug.h"
#include "xdk/win_types.h"
#include "xdk/xapilibi/processthreadsapi.h"
#include "xdk/xapilibi/synchapi.h"
#include "xdk/xapilibi/xbase.h"
#include "xdk/xapilibi/xbox.h"

HANDLE gEvent;
HANDLE gVoiceThread;
int Voice::sHeadsetTarget;

extern void StartSynchronizedVoices();

typedef void (*VoiceCallFunc)(int*, int*);
typedef void (*PoolVoiceCallFunc)(int*, int, int);
typedef HRESULT (*EndLoopFunc)(int *, int);

Voice::Voice(bool b1, int i, bool b2)
    : mState(0), mAudioData(0), mAudioBytes(0), mNumSamples(0), mSampleRate(0), mStartSamp(0), mLoopStart(-1),
      mLoopEnd(-1), mVolume(1.0f), mPan(0), unk2c(1.0f), mAttackRate(0.001f), mReleaseRate(0.001f),
      mXMA(b1), unk3c(), mReverbEnabled(false), mReverbMixDb(-96.0f), unk48(false), mSynchronized(b2),
      mChannels(i), mTagState(0), unk54(false) {
    unk5c = 0;
    unk60 = 0;
    mSourceVoice = 0;
    if (gEvent == INVALID_HANDLE_VALUE) {
        gEvent = CreateEventA(0, 0, 0, 0);
        MILO_ASSERT(gEvent, 0xfa);
        gVoiceThread = CreateThread(0, 0x10000, StartVoiceThreadEntry, 0, 4, 0);
        MILO_ASSERT(gVoiceThread, 0xff);
        SetThreadPriority(gVoiceThread, 0xf);
        DWORD ret = XSetThreadProcessor(gVoiceThread, 2);
        MILO_ASSERT(ret != -1, 0x107);
        ret = ResumeThread(gVoiceThread);
        MILO_ASSERT(ret != -1, 0x10c);
    }
}

Voice::~Voice() {
    for (;;) {
        int state = mState;
        if (state != 2)
            break;

        if (mSynchronized) {
            StartSynchronizedVoices();
        }
        Sleep(0);
    }

    if (unk3c) {
        int *pVar1 = (int *)unk3c;
        int *pVar2 = (int *)(*pVar1);
        VoiceCallFunc fn = (VoiceCallFunc)(*(int *)(*pVar2 + 0x10));
        fn(pVar1, (int*)this);
    }

    if (mSourceVoice) {
        int *pVar1 = (int *)mSourceVoice;
        int *pVar2 = (int *)(*pVar1);
        PoolVoiceCallFunc fn = (PoolVoiceCallFunc)(*(int *)(*pVar2 + 0x50));
        fn(pVar1, 0, 0);
        dispose(pVar1, mState);
    }
}

void Voice::SetSampleRate(int i) {
    mSampleRate = i;
    MILO_ASSERT(0 < mSampleRate && mSampleRate <= 48000, 0x2c9);
}

void Voice::SetLoopRegion(int loopStart, int loopEnd) {
    MILO_ASSERT_RANGE(loopStart, 0, mNumSamples, 0x2cf);
    MILO_ASSERT(loopEnd == -1 || loopEnd > loopStart, 0x2d0);
    mLoopStart = loopStart;
    mLoopEnd = loopEnd;
}

void Voice::SetReverbEnable(bool b) {
    if (mReverbEnabled == b)
        return;
    mReverbEnabled = b;
    UpdateSends();
}

void Voice::SetVolume(float f) {
    if (f != mVolume) {
        mVolume = f;
        if (4.0f < f) {
            MILO_NOTIFY("A gain of %f is rather loud", mVolume);
            mVolume = 4.0f;
        }
        UpdateMix();
    }
}

void Voice::SetPan(float f) {
    float mod = Mod(f - -4.0f, 8.0f);
    if (mod - 4.0f != mPan) {
        mPan = mod - 4.0f;
        UpdateMix();
    }
}

void Voice::SetStartSamp(int samp) {
    MILO_ASSERT(samp >= 0, 0x31e);
    MILO_ASSERT(samp < mNumSamples, 799);
    mStartSamp = samp;
}

void Voice::SetReverbMixDb(float f) {
    mReverbMixDb = f;
    UpdateMix();
}

void Voice::EndLoop() {
    // Call IXAudio2SourceVoice::ExitLoop(0) via vtable at offset 0x60
    int *pSourceVoice = (int *)mSourceVoice;
    HRESULT hr = ((EndLoopFunc)(*(int *)(*(int *)pSourceVoice + 0x60)))(pSourceVoice, 0);
    MILO_ASSERT(SUCCEEDED(hr), 0x2da);
}

void Voice::Start() { blockingStart(false); }

void Voice::SetData(const void *buffer, int bytes, int i) {
    MILO_ASSERT(buffer, 299);
    MILO_ASSERT(bytes >= 0, 300);
    mAudioData = buffer;
    mAudioBytes = bytes;
    if (i != 0) {
        mNumSamples = i;
    } else {
        MILO_ASSERT(!mXMA, 0x136);
        mNumSamples = bytes / 2;
        if (1 < mChannels) {
            MILO_ASSERT((mNumSamples & (mChannels)) == 0, 0x13a);
            mNumSamples = mNumSamples / mChannels;
        }
    }
}

void Voice::InitSourceBuffer(XAUDIO2_BUFFER &audio_buffer) {
    audio_buffer.pAudioData = (BYTE *)mAudioData;
    audio_buffer.AudioBytes = mAudioBytes;
    audio_buffer.pContext = 0;
    audio_buffer.PlayBegin = mStartSamp;
    audio_buffer.PlayLength = 0;
    if (mLoopStart >= 0) {
        if (mLoopEnd < 0) {
            mLoopEnd = mNumSamples;
        }
        if (mXMA) {
            mLoopStart = mLoopStart - (mLoopStart % 128);
            mLoopEnd = mLoopEnd - (mLoopEnd % 128);
        }
        audio_buffer.LoopCount = 0xff;
        audio_buffer.LoopBegin = mLoopStart;
        audio_buffer.LoopLength = mLoopEnd - mLoopStart;
    } else {
        audio_buffer.LoopBegin = 0;
        audio_buffer.LoopCount = 0;
        audio_buffer.LoopLength = 0;
    }
    audio_buffer.Flags = 0x40;
}
