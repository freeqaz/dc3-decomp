#include "synth_xbox/Voice.h"
#include "synth_xbox/FxSend.h"
#include "synth_xbox/Synth.h"
#include "math/Utl.h"
#include "os/CritSec.h"
#include "os/Debug.h"
#include "os/Timer.h"
#include <deque>
#include <list>
#include <vector>
#include "xdk/win_types.h"
#include "xdk/xapilibi/processthreadsapi.h"
#include "xdk/xapilibi/synchapi.h"
#include "xdk/xapilibi/xbase.h"
#include "xdk/xapilibi/xbox.h"

HANDLE gEvent;
HANDLE gVoiceThread;
int Voice::sHeadsetTarget;
CriticalSection gLockPendingLists;
CriticalSection gVoiceGC;
std::list<Voice *> gPendingVoices;
std::list<Voice *> gPendingSyncVoices;
std::list<Voice *> gInProgressVoices;
std::list<Voice *> gInProgressSyncVoices;
std::deque<PoolVoice> s_voiceGC;
std::deque<PoolVoice> s_voiceGCInProgress;

static bool gShutdownVoiceThread = false;
static bool gCommitSyncVoices = false;
static int gCommitTag = 0;
static bool gHasPendingStopCommits = false;
static bool gWasCommitSyncVoices = false;
static int gWasCommitTag = 0;
static int rolling = 0;
void StartSynchronizedVoices();

typedef void (*VoiceCallFunc)(int*, int*);
typedef void (*PoolVoiceCallFunc)(int*, int, int);
typedef HRESULT (*EndLoopFunc)(int *, int);

Voice::Voice(bool b1, int i, bool b2)
    : mState(0), mAudioData(0), mAudioBytes(0), mNumSamples(0), mSampleRate(0), mStartSamp(0), mLoopStart(-1),
      mLoopEnd(-1), mVolume(1.0f), mPan(0), mSpeed(1.0f), mAttackRate(0.001f), mReleaseRate(0.001f),
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

void Voice::dispose(int *, unsigned int) {}

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

void StartSynchronizedVoices() {
    if (gShutdownVoiceThread != false)
        return;
    gLockPendingLists.Enter();
    gCommitSyncVoices = true;
    gCommitTag = 1;
    if (gEvent != (HANDLE)-1) {
        SetEvent(gEvent);
    }
    gLockPendingLists.Exit();
}

void StopSynchronizedVoices() {
    if (gShutdownVoiceThread || !gHasPendingStopCommits)
        return;
    gLockPendingLists.Enter();
    gHasPendingStopCommits = false;
    gCommitSyncVoices = true;
    gCommitTag = 2;
    if (gEvent != INVALID_HANDLE_VALUE) {
        SetEvent(gEvent);
    }
    gLockPendingLists.Exit();
}

void TerminateVoiceThread() {
    gShutdownVoiceThread = true;
    if (gEvent != INVALID_HANDLE_VALUE) {
        SetEvent(gEvent);
    }
    if (gVoiceThread != INVALID_HANDLE_VALUE) {
        WaitForSingleObject(gVoiceThread, 500);
        CloseHandle(gVoiceThread);
    }
}

bool Voice::HasPendingVoices() {
    if (gShutdownVoiceThread)
        return false;
    gLockPendingLists.Enter();
    int count1 = 0;
    for (std::list<Voice *>::iterator it = gPendingVoices.begin();
         it != gPendingVoices.end(); ++it) {
        count1++;
    }
    int count2 = 0;
    for (std::list<Voice *>::iterator it = gPendingSyncVoices.begin();
         it != gPendingSyncVoices.end(); ++it) {
        count2++;
    }
    bool result = (count1 + count2) > 0;
    gLockPendingLists.Exit();
    return result;
}

void Voice::blockingStart(bool b) {
    if (gShutdownVoiceThread || TheXboxSynth->unkf0 == 0)
        return;
    gLockPendingLists.Enter();
    Init(b);
    int *pVoice = (int *)mSourceVoice;
    HRESULT hr =
        ((HRESULT(*)(int *, int, bool))(*(int *)(*(int *)pVoice + 0x4c)))(pVoice, 0, mSynchronized);
    MILO_ASSERT(SUCCEEDED(hr), 0x29b);
    mState = 3;
    gLockPendingLists.Exit();
}

void Voice::Stop(bool immediate) {
    if (mSourceVoice) {
        if (immediate) {
            int *pVoice = (int *)mSourceVoice;
            ((void (*)(int *, int, int))(*(int *)(*(int *)pVoice + 0x50)))(pVoice, 0, 0);
        } else {
            MILO_ASSERT(unk60, 0x14d);
            *(float *)((int *)unk60 + 2) = 1.0f;
            int *pVoice = (int *)mSourceVoice;
            HRESULT hr = ((HRESULT(*)(int *, int, int, int, int))(*(int *)(*(int *)pVoice + 0x18)))(
                pVoice, 0, unk60, 0x10, 0
            );
            MILO_ASSERT(SUCCEEDED(hr), 0x150);
        }
    }
    mState = 1;
}

void Voice::Pause(bool b) {
    int isPaused = (mState == 4);
    if (!(b == isPaused) && IsPlaying()) {
        if (mSynchronized && mState == 2 && b) {
            StartSynchronizedVoices();
        }
        while (mState == 2) {
            Sleep(0);
        }
        MILO_ASSERT(mSourceVoice, 0x2b4);
        if (b) {
            gHasPendingStopCommits = true;
            int *pVoice = (int *)mSourceVoice;
            int flags = mSynchronized ? 2 : 0;
            HRESULT hr =
                ((HRESULT(*)(int *, int, int))(*(int *)(*(int *)pVoice + 0x50)))(pVoice, 0, flags);
            MILO_ASSERT(SUCCEEDED(hr), 700);
            mState = 4;
        } else {
            SafeRestart();
        }
    }
}

void Voice::SetSpeed(float speed) {
    float min_speed = 0.0099999998f;
    float max_speed = 2.0f;
    float clamped = speed;

    if (clamped <= min_speed)
        clamped = min_speed;
    if (clamped > max_speed && mXMA) {
        MILO_NOTIFY("can't pitch an XMA sound up more than one octave");
        clamped = max_speed;
    }
    mSpeed = clamped;
    if (mSourceVoice != 0) {
        void *pVoice = (void*)mSourceVoice;
        ((void (*)(void *, float))(*(int *)(*(int *)pVoice + 0x68)))(pVoice, mSpeed);
    }
}

void Voice::SetSend(FxSend360 *send) {
    if ((FxSend360 *)unk3c == send)
        return;
    SetSendImpl(send);
}

void Voice::SetSendImpl(FxSend360 *send) {
    if (unk3c) {
        int *pSend = (int *)unk3c;
        ((void (*)(int *, Voice *))(*(int *)(*(int *)pSend + 0x10)))(pSend, this);
    }
    if (send) {
        ((void (*)(FxSend360 *, Voice *))(*(int *)(*(int *)send + 0x0c)))(send, this);
    }
    unk3c = (int *)send;
    UpdateSends();
}

void Voice::SafeRestart() {
    MILO_ASSERT(mSourceVoice, 0x471);
    int *pVoice = (int *)mSourceVoice;
    bool sync = mSynchronized != 0;
    ((void (*)(int *, int, bool))(*(int *)(*(int *)pVoice + 0x4c)))(pVoice, 0, sync);
    mState = 3;
}

int Voice::GetAddr() {
    int *pVoice = (int *)mSourceVoice;
    if (pVoice == 0 || mXMA)
        return 0;

    int state[3] = {0, 0, 0};
    ((void (*)(int *, int *, int))(*(int *)(*(int *)pVoice + 100)))(pVoice, state, 0);

    int addr = mStartSamp + state[2];
    if ((int)mAudioData == 0) {
        addr = mChannels * addr;
    } else {
        int bytesPerSample = mChannels * 2;
        int samplesInBuffer = mAudioBytes / bytesPerSample;
        addr = (addr - (addr / samplesInBuffer) * samplesInBuffer) * mChannels;
    }
    return addr << 1;
}

bool Voice::IsPlaying() {
    if (mState == 2)
        return true;
    if (!mSourceVoice || mState == 1)
        return false;
    if (mState == 4)
        return true;

    int state[4] = {0, 0, 0, 0};
    int *pVoice = (int *)mSourceVoice;
    ((void (*)(int *, int *, int))(*(int *)(*(int *)pVoice + 100)))(pVoice, state, 0);

    if (state[0] == 0 && state[1] == 0 && state[2] == 0)
        return false;

    return true;
}

void Voice::Init(bool b) {
    if (!TheXboxSynth || TheXboxSynth->unkf0 == 0)
        return;
    if (!b) {
        mState = 1;
    }
    MILO_ASSERT(0 < mSampleRate && mSampleRate <= 48000, 0x160);
    MILO_ASSERT(mAudioData, 0x161);

    XAUDIO2_BUFFER audioBuffer;
    InitSourceBuffer(audioBuffer);

    UpdateMix();
    if (mSourceVoice) {
        int *pVoice = (int *)mSourceVoice;
        ((void (*)(int *, float))(*(int *)(*(int *)pVoice + 0x68)))(pVoice, mSpeed);
    }
}

void Voice::InitVoiceParameters(XMA2WAVEFORMATEX &fmt, XAUDIO2_BUFFER buf) {
    if (mXMA) {
        fmt.wfx.wFormatTag = 0x166;
        fmt.wfx.nChannels = mChannels;
        fmt.wfx.nSamplesPerSec = mSampleRate;
        fmt.wfx.wBitsPerSample = 0;
        fmt.wfx.nBlockAlign = 0;
        fmt.wfx.nAvgBytesPerSec = 0;
        fmt.wfx.cbSize = 0x10;

        unsigned int channels = mChannels;
        unsigned int sampleRate = mSampleRate;
        unsigned short blockAlign = (unsigned short)((unsigned int)(unsigned short)channels << 4) >> 3;
        fmt.wfx.nBlockAlign = blockAlign;

        if (channels == 1) {
            fmt.NumStreams = 4;
        } else if (channels == 2) {
            fmt.NumStreams = 3;
        } else if (channels == 5) {
            fmt.NumStreams = 0x60f;
        } else {
            return;
        }

        fmt.NumStreams = 1;
        fmt.ChannelMask = 0;
        fmt.SamplesEncoded = mNumSamples;
        fmt.BytesPerBlock = 0;
        fmt.PlayBegin = 0;
        fmt.PlayLength = 0;
        fmt.LoopBegin = (unsigned int)((unsigned long long)buf.LoopBegin >> 0x20);
        fmt.LoopLength = (unsigned int)((unsigned long long)buf.LoopLength >> 0x20);
        fmt.LoopCount = buf.LoopCount;
        fmt.EncoderVersion = 4;
        fmt.BlockCount = 0;

        float duration = (float)(long long)*(int *)&mNumSamples * 1.5258789e-05f;
        fmt.NumStreams = (unsigned short)(unsigned long long)(long long)ceil(duration);
    } else {
        fmt.wfx.wFormatTag = 1;
        fmt.wfx.nChannels = mChannels;
        fmt.wfx.nSamplesPerSec = mSampleRate;
        fmt.wfx.wBitsPerSample = 16;
        fmt.wfx.nBlockAlign = (unsigned short)((unsigned int)(unsigned short)mChannels << 4) >> 3;
        fmt.wfx.nAvgBytesPerSec = (unsigned int)fmt.wfx.nBlockAlign * mSampleRate;
        fmt.wfx.cbSize = 0;
    }
}

unsigned long StartVoiceThreadEntry(void *) {
    rolling++;
    WaitForSingleObject(gEvent, INFINITE);
    while (!gShutdownVoiceThread) {
        gLockPendingLists.Enter();
        gInProgressVoices = gPendingVoices;
        gPendingVoices.clear();

        gWasCommitSyncVoices = false;
        if (gCommitSyncVoices) {
            gCommitSyncVoices = false;
            gWasCommitSyncVoices = true;
            gWasCommitTag = gCommitTag;
            gInProgressSyncVoices = gPendingSyncVoices;
            gPendingSyncVoices.clear();
        }
        gLockPendingLists.Exit();

        if (!gInProgressVoices.empty()) {
            for (std::list<Voice *>::iterator it = gInProgressVoices.begin();
                 it != gInProgressVoices.end(); ++it) {
                (*it)->blockingStart(true);
            }
            gInProgressVoices.clear();
        }

        if (!gInProgressSyncVoices.empty()) {
            for (std::list<Voice *>::iterator it = gInProgressSyncVoices.begin();
                 it != gInProgressSyncVoices.end(); ++it) {
                (*it)->blockingStart(true);
            }
            gInProgressSyncVoices.clear();
        }

        if (gWasCommitSyncVoices && TheXboxSynth && TheXboxSynth->unkec) {
            int *pMasterVoice = (int *)TheXboxSynth->unkec;
            HRESULT hr =
                ((HRESULT(*)(int *, int))(*(int *)(*(int *)pMasterVoice + 0x34)))(pMasterVoice, 0);
            MILO_ASSERT(SUCCEEDED(hr), 0x76);
        }

        // Process voice garbage collection
        gVoiceGC.Enter();
        int gcCount = 0;
        while (!s_voiceGC.empty() && gcCount < 4) {
            s_voiceGCInProgress.push_back(s_voiceGC.front());
            s_voiceGC.pop_front();
            gcCount++;
        }
        gVoiceGC.Exit();

        if (TheXboxSynth) {
            CriticalSection *cs = &TheXboxSynth->unkb0;
            cs->Enter();
            while (s_voiceGCInProgress.empty()) {
                PoolVoice &pv = s_voiceGCInProgress.front();
                if (pv.eg) {
                    int *pEg = (int *)pv.eg;
                    ((void (*)(int *, int))(*(int *)(*(int *)pEg + 0x48)))(pEg, 0);
                }
                if (pv.egParams) {
                    int *pParams = (int *)pv.egParams;
                    ((void (*)(int *, int))(*(int *)(*(int *)pParams + 0x38)))(pParams, 1);
                }
                pv.egParams = 0;
                s_voiceGCInProgress.pop_front();
            }
            cs->Exit();
        }
        s_voiceGCInProgress.clear();

        WaitForSingleObject(gEvent, INFINITE);
    }
    return 0;
}
