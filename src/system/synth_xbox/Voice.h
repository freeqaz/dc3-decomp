#pragma once
#include "types.h"
#include "xdk/win_types.h"
#include "xdk/XAPILIB.h"
#include "xdk/XAUDIO2.h"
#include "utl/PoolAlloc.h"

class FxSend360;

class Voice {
public:
    POOL_OVERLOAD(Voice, 0x28);
    Voice(bool, int, bool);
    ~Voice();
    void InitSourceBuffer(XAUDIO2_BUFFER &);
    int GetAddr();
    void SetData(void const *, int, int);
    void Stop(bool);
    void InitVoiceParameters(XMA2WAVEFORMATEX &, XAUDIO2_BUFFER);
    void SetSampleRate(int);
    void SetLoopRegion(int, int);
    void EndLoop();
    bool IsPlaying();
    void SetStartSamp(int);
    void SetReverbMixDb(float);
    void Pause(bool);
    void SetVolume(float);
    void SetPan(float);
    void SetReverbEnable(bool);
    void SetSend(FxSend360 *);
    static bool HasPendingVoices();
    void SetSpeed(float);

    static int sHeadsetTarget;
    void Init(bool);
    void blockingStart(bool);
    void Start();

    u32 unk0;
    int mState; // 0x4 - voice play state (2 = pending)
    const void *mAudioData; // 0x8 - audio buffer pointer (pAudioData)
    int mAudioBytes; // 0xc - audio buffer size in bytes
    int mNumSamples; // 0x10
    int mSampleRate; // 0x14
    int mStartSamp; // 0x18 - start sample position (PlayBegin)
    int mLoopStart; // 0x1c
    int mLoopEnd; // 0x20
    float mVolume; // 0x24
    float mPan; // 0x28
    float unk2c;
    float mAttackRate; // 0x30 - ADSR attack rate
    float mReleaseRate; // 0x34 - ADSR release rate
    bool mXMA; // 0x38
    int *unk3c; // 0x3c
    bool mReverbEnabled; // 0x40
    float mReverbMixDb; // 0x44 - reverb mix in dB
    bool unk48;
    bool mSynchronized; // 0x49 - requires synchronized voice start
    int mChannels; // 0x4c
    int mTagState; // 0x50 - stream tag state
    bool unk54;
    int mSourceVoice; // 0x58 - IXAudio2SourceVoice* (as int for vtable dispatch)
    int unk5c;
    int unk60; // PoolVoice

private:
    // long createOrReuse(PoolVoice *, unsigned int &, tWAVEFORMATEX &,
    // XAUDIO2_VOICE_SENDS *);
    void UpdateMix();
    void UpdateSends();
    void SafeRestart();
    void SetSendImpl(FxSend360 *);
    void dispose(int *, unsigned int);
};

unsigned long StartVoiceThreadEntry(void *);
