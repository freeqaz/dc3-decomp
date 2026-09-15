#include "synth_xbox\Voice.h"
#include "synth360\EnvelopeGenerator.h"
#include "synth_xbox\FxSend.h"
#include "synth_xbox\Synth.h"
#include "math\Decibels.h"
#include "math\Utl.h"
#include "os\CritSec.h"
#include "os\Debug.h"
#include "os\System.h"
#include "os\Timer.h"
#include "utl\MemMgr.h"
#include <deque>
#include <list>
#include <vector>
#include "xdk\win_types.h"
#include "xdk\xapilibi\processthreadsapi.h"
#include "xdk\xapilibi\synchapi.h"
#include "xdk\xapilibi\winbase.h"
#include "xdk\xapilibi\xbase.h"
#include "xdk\xapilibi\xbox.h"

// Original has these three in initialized data, not .bss:
// gEvent / gVoiceThread = 0xFFFFFFFF (INVALID_HANDLE_VALUE), sHeadsetTarget = -1.
HANDLE gEvent = INVALID_HANDLE_VALUE;
HANDLE gVoiceThread = INVALID_HANDLE_VALUE;
int Voice::sHeadsetTarget = -1;
CriticalSection gLockPendingLists;
CriticalSection gVoiceGC;
std::list<Voice *> gPendingVoices;
std::list<Voice *> gPendingSyncVoices;
std::list<Voice *> gInProgressVoices;
std::list<Voice *> gInProgressSyncVoices;
std::deque<PoolVoice> s_voiceGC;
std::deque<PoolVoice> s_voiceGCInProgress;

bool gShutdownVoiceThread = false;
bool gCommitSyncVoices = false;
int gCommitTag = 0;
bool gHasPendingStopCommits = false;
bool gWasCommitSyncVoices = false;
static int gVoiceCounters[2];
int gWasCommitTag = 0;
int rolling = 0;
void StartSynchronizedVoices();


IXAudio2SourceVoice *Voice::GetVoice() { return (IXAudio2SourceVoice *)mPoolVoice.sourceVoice; }

Voice::Voice(bool b1, int i, bool b2)
    : mState(0), mBuffer(0), mAudioBytes(0), mNumSamples(0), mSampleRate(0), mStartSamp(0), mLoopStart(-1),
      mLoopEnd(-1), mVolume(1.0f), mPan(0), mSpeed(1.0f), mAttackRate(0.001f), mReleaseRate(0.001f),
      mXMA(b1), mFxSend(), mReverbEnabled(false), mReverbMixDb(-96.0f), unk48(false), mSynchronized(b2),
      mChannels(i), mTagState(0), unk54(false) {
    mPoolVoice.eg = 0;
    mPoolVoice.egParams = 0;
    mPoolVoice.sourceVoice = 0;
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
    while (mState == 2) {
        if (mSynchronized) {
            StartSynchronizedVoices();
        }
        Sleep(0);
    }
    if (mFxSend) {
        mFxSend->RemoveOwnerVoice(this);
    }
    if (GetVoice()) {
        GetVoice()->Stop(0, 0);
        dispose(&mPoolVoice, unk0);
    }
}

void Voice::dispose(PoolVoice *pv, unsigned int) {
    ((IXAudio2SourceVoice *)pv->sourceVoice)->FlushSourceBuffers();
    if (unk54) {
        XAUDIO2_VOICE_SENDS emptySends;
        emptySends.SendCount = 0;
        emptySends.pSends = 0;
        ((IXAudio2SourceVoice *)pv->sourceVoice)->SetOutputVoices(&emptySends);
        unk54 = false;
    }
    pv->disposeTick = GetTickCount() - 500000;
    {
        // A scoped CritSecTracker, not a bare Enter()/Exit() pair: deque::push_back
        // allocates, so the guard's destructor has to run on the unwind path.  That
        // EH state is what buys the target's r31 frame pointer, its extra
        // callee-saved register and the `stw r28, 0x50(r31)` that materialises
        // CritSecTracker::mCritSec -- see the inverse lever in
        // docs/decomp/patterns/fixable-inline-boundary.md.
        CritSecTracker lock(&gVoiceGC);
        s_voiceGC.push_back(*pv);
        // [1]++ before [0]--: the target loads gVoiceCounters[1] into the lower
        // scratch register, which is a statement-order tell, not scheduling noise.
        gVoiceCounters[1]++;
        gVoiceCounters[0]--;
    }
    pv->eg = 0;
    pv->egParams = 0;
    pv->sourceVoice = 0;
}

long Voice::createOrReuse(
    PoolVoice *pPoolVoice, unsigned int &, tWAVEFORMATEX &wfx, XAUDIO2_VOICE_SENDS *sends
) {
    if (!TheXboxSynth->OutputVoice()) {
        return 0;
    }
    long result;
    MILO_ASSERT(pPoolVoice->eg == 0, 0x1c1);
    pPoolVoice->eg = new EnvelopeGenerator();
    MILO_ASSERT(mPoolVoice.egParams == 0, 0x1c3);
    pPoolVoice->egParams = new EnvelopeGeneratorParams;

    XAUDIO2_EFFECT_DESCRIPTOR effectDesc;
    XAUDIO2_EFFECT_CHAIN effectChain;
    effectDesc.InitialState = 0;
    effectChain.EffectCount = 1;
    effectDesc.pEffect = (IUnknown *)pPoolVoice->eg;
    effectDesc.OutputChannels = mChannels;
    effectChain.pEffectDescriptors = &effectDesc;

    MemPushTemp();

    // The image almost certainly holds this in a CritSecTracker, not a bare
    // pointer: right after the `addic. r30, r11, 0xb0` that both forms &unkb0
    // and tests it, the image does `stw r30, 0x50(r31)` -- a store we have no
    // reason to emit, and exactly CritSecTracker::mCritSec being materialised --
    // and createOrReuse carries an unwind region (pdata 0xC000AF05, bit31 set)
    // that a plain pointer local would not need.  The tracker's scope would end
    // before the success bookkeeping, with MSVC duplicating the inlined
    // destructor into both arms of the following `if (hr)`, which is what the
    // two `bl Exit` sites look like (error one after MILO_FAIL, success one
    // before the counter/memcpy work).
    //
    // MEASURED AND REJECTED (wave 7, lane w7-y): writing it that way -- tracker
    // scope around the call plus the error print, then `if (hr) result = hr;
    // else {...}` outside -- fixes the whole 16-row r29/r30 permutation, but our
    // MSVC does NOT merge the second `if (hr)` into the duplicated destructor.
    // It emits a fresh `cmpwi cr6, r28, 0x0` and re-lays the tail, losing the
    // shared `bl MemPopTemp` block: 96.19 -> 94.06 (39 mismatch rows -> 26, but
    // 7 of them deletes).  Kept the explicit spelling and named the loss.
    CriticalSection *cs = &TheXboxSynth->unkb0;
    if (cs) {
        cs->Enter();
    }

    int *pEngine = (int *)TheXboxSynth->unkec;
    HRESULT hr = ((HRESULT(*)(
        int *,
        IXAudio2SourceVoice **,
        tWAVEFORMATEX *,
        int,
        float,
        int,
        XAUDIO2_VOICE_SENDS *,
        XAUDIO2_EFFECT_CHAIN *
    ))(*(int *)(*(int *)pEngine + 0x20)))(
        pEngine,
        (IXAudio2SourceVoice **)pPoolVoice,
        &wfx,
        0,
        4.0f,
        0,
        sends,
        &effectChain
    );

    if (hr) {
        char buf[0x800] = "";
        MEMORYSTATUS memStatus;
        GlobalMemoryStatus(&memStatus);
        Hx_snprintf(buf, 0x800, "XAudio2: CreateSourceVoice failed with 0x%X\n", hr);
        MemPrintOverview(kNoHeap, buf + strlen(buf));
        MILO_FAIL(buf);
        if (cs) {
            cs->Exit();
        }
        result = hr;
    } else {
        if (cs) {
            cs->Exit();
        }
        gVoiceCounters[0]++;
        memcpy(&pPoolVoice->wfx, &wfx, 0x12);
        unk54 = (sends == nullptr || sends->SendCount > 0);
        result = 0;
    }
    MemPopTemp();
    return result;
}

// w7-bu (2026-09-15): 92.4 -> 95.0 canonical, three levers on the MONO arm:
// (1) the two 0x3d9 asserts are ONE assert on a shared `HRESULT hr = 0`
// (the image compares it in cr6, 0x82E378F4, where a call result tested in
// place is cr0 -- compare the 0x37a site at 0x82E3758C -- and the
// `!unk54` path jumps straight past the assert, 0x82E378A4, because
// SUCCEEDED(0) folds).  Keeping the per-site `voice` locals matters:
// `hr = GetVoice()->SetOutputMatrix(...)` swaps this/r24 with the assert
// string's r25 (92.9), and `hr` assigned in every arm is 91.9.
// (2) loChannel/loPan/hiChannel/hiPan are declared interleaved: the
// image materialises the uninitialised set as lwz/lfs/lwz/lfs from 0x50(r1)
// (0x82E376A4 on).  (3) each pan arm assigns loChannel, loPan, hiChannel,
// hiPan in that order (0x82E37700-0x82E37780) -- with (2) this also removed
// the r30/r29/r28 rotation w7-an recorded.
// RESIDUAL (w7-bu, 95.0, 37 rows): all in the STEREO arm plus the 6 fmuls
// rows below.  We partially-redundancy-eliminate the `*TheXboxSynth` load out
// of the two `mFxSend ? ... : TheXboxSynth->OutputVoice()` ternaries into r8
// (hoisted between the `cmplwi` and its `beq`) and reload it after each call;
// the image reloads the global inside each arm (0x82E37474/0x82E37494).  The
// textually identical MONO copy of the same ternary pair (0x82E375C0 on)
// matches exactly, so this is contextual, not a spelling.  Tied to it: the
// image lays the cos/sin arm out as the fall-through of the `== 6 || == 2`
// test (`bne cr6` at 0x82E374F8) and puts the fill-1.0 loop out of line; we
// do the reverse.  Measured inert for both: stereo/mono as if/else instead of
// an early return (95.0, and it shrinks the frame by 0x20), and the 6/2 test
// as a `switch` (95.0, and the compares come out sorted 2-then-6).
// NEGATIVE RESULT (w7-an, 2026-09-14): the 6 `fmuls` commutative operand rows
// are NOT reachable from the source -- writing `(float)cos(angle) * mVolume`
// instead of `mVolume * (float)cos(angle)` at all six sites is byte-for-byte
// inert (MSVC normalises the order).  So is inverting the
// `destChannels == 6 || destChannels == 2` if/else into
// `!= 6 && != 2` with the arms swapped: the image falls through into the
// cos/sin arm (`bne cr6` at 0x82E374F8), we fall through into the fill-1.0
// arm, and neither spelling of the condition moves that.
void Voice::UpdateMix() {
    if (mPoolVoice.sourceVoice == 0)
        return;

    if (mChannels > 1) {
        // Stereo source: no panning law, the two channels go straight out.
        MILO_ASSERT(mChannels == 2, 0x349);
        GetVoice()->SetVolume(1.0f, 0);

        int destChannels = 6;
        if ((mFxSend ? mFxSend->GetOutputVoice() : TheXboxSynth->OutputVoice()) != nullptr) {
            XAUDIO2_VOICE_DETAILS details;
            (mFxSend ? mFxSend->GetOutputVoice() : TheXboxSynth->OutputVoice())
                ->GetVoiceDetails(&details);
            destChannels = details.InputChannels;
        }

        float levels[12];
        for (int i = 0; i < 12; i++) {
            levels[i] = 0.0f;
        }
        float angle = ((mPan + 1.0f) * 0.5f) * 1.5707964f;
        if (destChannels == 6 || destChannels == 2) {
            levels[0] = mVolume * (float)cos(angle);
            levels[3] = mVolume * (float)sin(angle);
        } else {
            for (int i = 0; i < 12; i++) {
                levels[i] = 1.0f;
            }
        }
        IXAudio2SourceVoice *voice = GetVoice();
        HRESULT hr = voice->SetOutputMatrix(
            mFxSend ? mFxSend->GetOutputVoice() : TheXboxSynth->OutputVoice(),
            mChannels,
            destChannels,
            levels,
            0
        );
        MILO_ASSERT(SUCCEEDED(hr), 0x37a);
        return;
    }

    // Mono source: constant-power pan around the 5.1 ring.  mPan runs -4..4;
    // the ring is split into six arcs, each interpolating between two speakers.
    int destChannels = 6;
    if ((mFxSend ? mFxSend->GetOutputVoice() : TheXboxSynth->OutputVoice()) != nullptr) {
        XAUDIO2_VOICE_DETAILS details;
        (mFxSend ? mFxSend->GetOutputVoice() : TheXboxSynth->OutputVoice())
            ->GetVoiceDetails(&details);
        destChannels = details.InputChannels;
    }

    float levels[6];
    for (int i = 0; i < 6; i++) {
        levels[i] = 0.0f;
    }

    int loChannel;
    float loPan;
    int hiChannel;
    float hiPan;
    if (destChannels == 6 || destChannels == 2) {
        if (mPan < -3.0f) {
            loChannel = 4;
            loPan = -3.0f;
            hiChannel = 5;
            hiPan = -5.0f;
        } else if (mPan < -1.0f) {
            loChannel = 0;
            loPan = -1.0f;
            hiChannel = 4;
            hiPan = -3.0f;
        } else if (mPan < 0.0f) {
            loChannel = 0;
            loPan = -1.0f;
            hiChannel = 2;
            hiPan = 0.0f;
        } else if (mPan < 1.0f) {
            loChannel = 2;
            loPan = 0.0f;
            hiChannel = 1;
            hiPan = 1.0f;
        } else if (mPan < 3.0f) {
            loChannel = 1;
            loPan = 1.0f;
            hiChannel = 5;
            hiPan = 3.0f;
        } else {
            loChannel = 5;
            loPan = 3.0f;
            hiChannel = 4;
            hiPan = 5.0f;
        }
        float angle = (mPan - loPan) / (hiPan - loPan) * 1.5707964f;
        if (destChannels == 6) {
            levels[loChannel] = mVolume * (float)cos(angle);
            levels[hiChannel] = mVolume * (float)sin(angle);
        } else {
            MILO_ASSERT(-1.0 <= mPan && mPan <= 1.0, 0x3c2);
            levels[0] = mVolume * (float)cos(((mPan + 1.0f) * 0.5f) * 1.5707964f);
            levels[1] = mVolume * (float)sin(((mPan + 1.0f) * 0.5f) * 1.5707964f);
        }
    } else if (destChannels == 1) {
        levels[0] = mVolume;
    } else {
        MILO_NOTIFY("Output voice has unexpected number of channels %d", destChannels);
    }

    HRESULT hr = 0;
    if ((mFxSend ? mFxSend->GetOutputVoice() : TheXboxSynth->OutputVoice()) == nullptr) {
        if (unk54) {
            IXAudio2SourceVoice *voice = GetVoice();
            hr = voice->SetOutputMatrix(nullptr, 1, 6, levels, 0);
        }
    } else {
        IXAudio2SourceVoice *voice = GetVoice();
        hr = voice->SetOutputMatrix(
            mFxSend ? mFxSend->GetOutputVoice() : TheXboxSynth->OutputVoice(),
            1,
            destChannels,
            levels,
            0
        );
    }
    MILO_ASSERT(SUCCEEDED(hr), 0x3d9);

    if (mReverbEnabled && unk48) {
        float reverbRatio = DbToRatio(mReverbMixDb);
        for (int i = 0; i < 6; i++) {
            levels[i] = 0.0f;
        }
        float angle = (mPan - loPan) / (hiPan - loPan) * 1.5707964f;
        if (destChannels == 6) {
            levels[loChannel] = (float)cos(angle) * reverbRatio;
            levels[hiChannel] = (float)sin(angle) * reverbRatio;
        } else {
            MILO_ASSERT(-1.0 <= mPan && mPan <= 1.0, 0x3ee);
            levels[0] = (float)cos(angle) * reverbRatio;
            // Shipping-game bug, reproduced verbatim: the right channel is fed
            // cos() again instead of sin(), so a stereo reverb send is
            // correlated rather than panned.  Both call sites resolve to the
            // same `cos` in the target (0x829A09B8).
            levels[1] = (float)cos(angle) * reverbRatio;
        }
        HRESULT hr =
            GetVoice()->SetOutputMatrix(TheXboxSynth->UnkF8(), 1, destChannels, levels, 0);
        MILO_ASSERT(SUCCEEDED(hr), 0x3f5);
    }
}

void Voice::UpdateSends() {
    IXAudio2SourceVoice *voice = (IXAudio2SourceVoice *)mPoolVoice.sourceVoice;
    if (mPoolVoice.sourceVoice != 0) {
        XAUDIO2_VOICE_SENDS voiceSends;
        XAUDIO2_SEND_DESCRIPTOR mainDesc;
        mainDesc.Flags = 0;
        IXAudio2Voice *targetVoice =
            mFxSend ? mFxSend->GetOutputVoice() : TheXboxSynth->OutputVoice();
        mainDesc.pOutputVoice = targetVoice;
        voiceSends.SendCount = 1;
        voiceSends.pSends = &mainDesc;
        if (mReverbEnabled) {
            // Two descriptors: the target reserves 16 bytes here (frame slot
            // 0x50..0x5f, with 0x58 never written) and SendCount can reach 2
            // below.  reverbDesc[1] is left UNINITIALISED by the shipping
            // game -- when the second send is enabled XAudio2 reads a garbage
            // descriptor.  Reproduced as-is; do not "fix" without a matching
            // instruction budget.
            XAUDIO2_SEND_DESCRIPTOR reverbDesc[2];
            reverbDesc[0].Flags = 0;
            reverbDesc[0].pOutputVoice = TheXboxSynth->UnkF8();
            voiceSends.SendCount = 1;
            voiceSends.pSends = reverbDesc;
            IXAudio2Voice *target2 =
                mFxSend ? mFxSend->GetOutputVoice() : TheXboxSynth->OutputVoice();
            if (target2) {
#ifdef HX_NATIVE
                // The shipping game never writes reverbDesc[1], but sets
                // SendCount = 2 here -- so XAudio2 reads an uninitialised
                // descriptor.  On PPC that is reproduced faithfully (see
                // above); natively it is a live read of uninitialised stack,
                // so initialise the second send to what the code plainly
                // intended: the voice's own output target.
                reverbDesc[1].Flags = 0;
                reverbDesc[1].pOutputVoice = target2;
#endif
                voiceSends.SendCount = 2;
            }
        }
        HRESULT hr;
        if (targetVoice == 0 && !mReverbEnabled) {
            XAUDIO2_VOICE_SENDS noSends;
            noSends.SendCount = 0;
            noSends.pSends = 0;
            hr = voice->SetOutputVoices(&noSends);
            unk54 = false;
            unk48 = false;
        } else {
            hr = voice->SetOutputVoices(&voiceSends);
            unk54 = true;
            if (mReverbEnabled) {
                unk48 = true;
            }
        }
        MILO_ASSERT(SUCCEEDED(hr), 0x462);
        UpdateMix();
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
    HRESULT hr = GetVoice()->ExitLoop(0);
    MILO_ASSERT(SUCCEEDED(hr), 0x2da);
}

void Voice::Start() { blockingStart(false); }

void Voice::SetData(const void *buffer, int bytes, int i) {
    MILO_ASSERT(buffer, 299);
    MILO_ASSERT(bytes >= 0, 300);
    mBuffer = buffer;
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
    audio_buffer.pAudioData = (BYTE *)mBuffer;
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
    if (gShutdownVoiceThread)
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
    if (gShutdownVoiceThread) {
        return false;
    } else {
        CritSecTracker t(&gLockPendingLists);
        return gPendingVoices.size() + gPendingSyncVoices.size() != 0;
    }
}

void Voice::blockingStart(bool b1) {
    if (!gShutdownVoiceThread && TheXboxSynth->OutputVoice()) {
        CritSecTracker t(&gLockPendingLists);
        Init(b1);
        HRESULT hr = GetVoice()->Start(0, mSynchronized != false);
        MILO_ASSERT(SUCCEEDED(hr), 0x29B);
        mState = 3;
    }
}

void Voice::Stop(bool immediate) {
    if (mPoolVoice.sourceVoice) {
        if (immediate) {
            int *pVoice = (int *)mPoolVoice.sourceVoice;
            ((void (*)(int *, int, int))(*(int *)(*(int *)pVoice + 0x50)))(pVoice, 0, 0);
        } else {
            MILO_ASSERT(mPoolVoice.egParams, 0x14d);
            *(float *)((int *)mPoolVoice.egParams + 2) = 1.0f;
            int *pVoice = (int *)mPoolVoice.sourceVoice;
            HRESULT hr = ((HRESULT(*)(int *, int, int, int, int))(*(int *)(*(int *)pVoice + 0x18)))(
                pVoice, 0, (int)mPoolVoice.egParams, 0x10, 0
            );
            MILO_ASSERT(SUCCEEDED(hr), 0x150);
        }
    }
    mState = 1;
}

void Voice::Pause(bool b1) {
    if (b1 != (mState == 4) && IsPlaying()) {
        if (mSynchronized && mState == 2 && b1) {
            StartSynchronizedVoices();
        }
        while (mState == 2) {
            Sleep(0);
        }
        MILO_ASSERT(GetVoice(), 0x2B4);
        if (b1) {
            gHasPendingStopCommits = true;
            HRESULT hr = GetVoice()->Stop(0, mSynchronized ? 2 : 0);
            MILO_ASSERT(SUCCEEDED(hr), 700);
            mState = 4;
        } else {
            SafeRestart();
        }
    }
}

void Voice::SetSpeed(float speed) {
    float min_speed = 0.01f;
    float *pSpeed = &speed;
    if (speed <= min_speed)
        pSpeed = &min_speed;
    float clamped = *pSpeed;
    float max_speed = 2.0f;
    if (clamped > max_speed && mXMA) {
        MILO_NOTIFY_ONCE("can't pitch an XMA sound up more than one octave");
        clamped = max_speed;
    }
    mSpeed = clamped;
    if (mPoolVoice.sourceVoice != 0) {
        int *pVoice = (int *)mPoolVoice.sourceVoice;
        ((void (*)(int *, float, int))(*(int *)(*(int *)pVoice + 0x68)))(pVoice, mSpeed, 0);
    }
}

void Voice::SetSend(FxSend360 *send) {
    if (mFxSend == send)
        return;
    SetSendImpl(send);
}

void Voice::SetSendImpl(FxSend360 *send) {
    if (mFxSend) {
        int *pSend = (int *)mFxSend;
        ((void (*)(int *, Voice *))(*(int *)(*(int *)pSend + 0x10)))(pSend, this);
    }
    if (send) {
        ((void (*)(FxSend360 *, Voice *))(*(int *)(*(int *)send + 0x0c)))(send, this);
    }
    mFxSend = send;
    UpdateSends();
}

void Voice::SafeRestart() {
    MILO_ASSERT(GetVoice(), 0x471);
    int *pVoice = (int *)mPoolVoice.sourceVoice;
    bool sync = mSynchronized != 0;
    ((void (*)(int *, int, bool))(*(int *)(*(int *)pVoice + 0x4c)))(pVoice, 0, sync);
    mState = 3;
}

int Voice::GetAddr() {
    if (mPoolVoice.sourceVoice == 0 || mXMA)
        return 0;

    int *pVoice = (int *)mPoolVoice.sourceVoice;
    XAUDIO2_VOICE_STATE state;
    ((void (*)(int *, XAUDIO2_VOICE_STATE *, int))(*(int *)(*(int *)pVoice + 0x64)))(pVoice, &state, 0);

    int addr = mStartSamp + (unsigned int)state.SamplesPlayed;
    // UNSIGNED test: the image compares with `cmplwi cr6, r9, 0x0`, not
    // `cmpwi`.  `const void *buf = mBuffer; if (buf)` gives the signed form --
    // MSVC/Xenon picks cmpwi for a pointer-typed equality-with-zero and
    // cmplwi only for an unsigned integer one.
    if ((unsigned int)mBuffer != 0) {
        int bytesPerSample = mChannels * 2;
        int samplesInBuffer = mAudioBytes / bytesPerSample;
        unsigned int uaddr = (unsigned int)addr;
        addr = (int)(uaddr - (uaddr / (unsigned int)samplesInBuffer) * (unsigned int)samplesInBuffer) * mChannels;
    } else {
        addr = mChannels * addr;
    }
    return addr << 1;
}

bool Voice::IsPlaying() {
    START_AUTO_TIMER("voice_is_playing");
    if (mState == 2)
        return true;
    IXAudio2SourceVoice *voice = GetVoice();
    if (!voice)
        return false;
    if (mState == 1)
        return false;
    if (mState == 4)
        return true;

    XAUDIO2_VOICE_STATE state;
    voice->GetState(&state, 0);
    if (state.BuffersQueued == 0 && state.SamplesPlayed == 0)
        return false;

    EnvelopeGeneratorParams params;
    params.unkc = 0.0f;
    if (TheXboxSynth->unkb0.TryEnter()) {
        HRESULT hr = GetVoice()->GetEffectParameters(0, &params, 0x10);
        TheXboxSynth->unkb0.Exit();
        MILO_ASSERT(SUCCEEDED(hr), 0x2ff);
    }
    return params.unkc == 0.0f;
}

// clang-format off
void Voice::Init(bool b1) {
    if ((unsigned int)TheXboxSynth->unkf0 == 0)
        return;
    if (!b1) {
        mState = 1;
    }
    MILO_ASSERT(0 < mSampleRate && mSampleRate <= 48000, 0x160);
    MILO_ASSERT(mBuffer, 0x161);

    // If FxSend360 has no submix voices, rebuild the FxSend chain
    if (mFxSend) {
        if (!mFxSend->HasVoices()) {
            FxSend *fs = dynamic_cast<FxSend *>(mFxSend);
            fs->RebuildChain();
        }
    }

    // Build send descriptors
    XAUDIO2_SEND_DESCRIPTOR sendDesc;
    sendDesc.Flags = 0;
    IXAudio2Voice *outputVoice;
    if (mFxSend) {
        outputVoice = (IXAudio2Voice *)(*(int *)((char *)mFxSend + 4));
    } else {
        outputVoice = (IXAudio2Voice *)TheXboxSynth->unkf0;
    }
    sendDesc.pOutputVoice = outputVoice;

    std::vector<XAUDIO2_SEND_DESCRIPTOR> sends;
    if (outputVoice) {
        sends.push_back(sendDesc);
    }

    // Add reverb send if enabled
    if (mReverbEnabled) {
        sendDesc.Flags = 0;
        sendDesc.pOutputVoice = (IXAudio2Voice *)TheXboxSynth->unkf8;
        sends.push_back(sendDesc);
        unk48 = true;
    }

    // Add headset send if available
    XAUDIO2_SEND_DESCRIPTOR *pOldData = sends.data();
    sendDesc.Flags = 0;
    sendDesc.pOutputVoice = (IXAudio2Voice *)TheXboxSynth->GetHeadsetSubmix(sHeadsetTarget);
    if (sendDesc.pOutputVoice) {
        sends.push_back(sendDesc);
        pOldData = sends.data();
    }

    // Build voice sends structure
    int sendCount = ((char *)sends.end() - (char *)pOldData) >> 3;
    XAUDIO2_VOICE_SENDS voiceSends;
    voiceSends.SendCount = sendCount;
    voiceSends.pSends = (sendCount != 0) ? pOldData : 0;

    // Initialize source buffer and voice parameters
    XAUDIO2_BUFFER audioBuffer;
    InitSourceBuffer(audioBuffer);
    XMA2WAVEFORMATEX fmt;
    InitVoiceParameters(fmt, audioBuffer);

    // Create or reuse source voice
    MILO_ASSERT(!GetVoice(), 0x194);
    XAUDIO2_VOICE_SENDS *pSends = 0;
    if (voiceSends.SendCount != 0) {
        pSends = &voiceSends;
    }
    HRESULT hr = createOrReuse(&mPoolVoice, unk0, fmt.wfx, pSends);
    MILO_ASSERT(SUCCEEDED(hr), 0x19d);
    MILO_ASSERT(GetVoice(), 0x19e);

    // Submit source buffer
    hr = GetVoice()->SubmitSourceBuffer(&audioBuffer, nullptr);
    MILO_ASSERT(SUCCEEDED(hr), 0x1a3);

    // Update mix and frequency
    UpdateMix();
    if (GetVoice()) {
        GetVoice()->SetFrequencyRatio(mSpeed, 0);
    }

    // Set envelope parameters
    ((float *)mPoolVoice.egParams)[0] = mAttackRate;
    ((float *)mPoolVoice.egParams)[1] = mReleaseRate;
    ((float *)mPoolVoice.egParams)[2] = 0.0f;
    ((float *)mPoolVoice.egParams)[3] = 0.0f;
    hr = GetVoice()->SetEffectParameters(0, mPoolVoice.egParams, 0x10, 0);
    MILO_ASSERT(SUCCEEDED(hr), 0x1b0);
}
// clang-format on

void Voice::InitVoiceParameters(XMA2WAVEFORMATEX &fmt, XAUDIO2_BUFFER buf) {
    if (mXMA) {
        fmt.wfx.wFormatTag = 0x166;
        fmt.wfx.nChannels = mChannels;
        fmt.wfx.nSamplesPerSec = mSampleRate;
        fmt.wfx.wBitsPerSample = 0x10;
        fmt.wfx.cbSize = 0x22;
        fmt.NumStreams = 1;
        fmt.wfx.nBlockAlign = (fmt.wfx.nChannels * fmt.wfx.wBitsPerSample) / 8;
        if (mChannels == 1) {
            fmt.ChannelMask = 4;
        } else if (mChannels == 2) {
            fmt.ChannelMask = 3;
        } else if (mChannels == 5) {
            fmt.ChannelMask = 0x60f;
        }
        fmt.SamplesEncoded = mNumSamples;
        fmt.PlayBegin = buf.PlayBegin;
        fmt.BytesPerBlock = 0x10000;
        fmt.PlayLength = buf.PlayLength;
        fmt.LoopBegin = buf.LoopBegin;
        fmt.LoopLength = buf.LoopLength;
        fmt.LoopCount = buf.LoopCount;
        fmt.EncoderVersion = 4;
        float duration = (float)(long long)mAudioBytes * 1.5258789e-05f;
        fmt.BlockCount = (unsigned short)ceil(duration);
    } else {
        fmt.wfx.wFormatTag = 1;
        fmt.wfx.nChannels = mChannels;
        fmt.wfx.nSamplesPerSec = mSampleRate;
        fmt.wfx.wBitsPerSample = 16;
        fmt.wfx.nBlockAlign = (fmt.wfx.nChannels * fmt.wfx.wBitsPerSample) / 8;
        fmt.wfx.nAvgBytesPerSec = (unsigned int)fmt.wfx.nBlockAlign * fmt.wfx.nSamplesPerSec;
        fmt.wfx.cbSize = 0;
    }
}

// w7-bh: the MakeString template-instantiation row objdiff reports here is a
// benign ICF fold, not a wrong callee -- both the image's and our instantiation
// resolve to 0x824D1870 in build/373307D9/icf_aliases.map.  MakeString's array
// bounds are template parameters that never reach the code, so all such
// instantiations are byte-identical and the linker folds them.  The real
// residual at 90.7% / 1168 B is a prologue hoist-set permutation.
unsigned long StartVoiceThreadEntry(void *) {
    rolling++;
    WaitForSingleObject(gEvent, INFINITE);
    while (!gShutdownVoiceThread) {
        // Scoped CritSecTracker, not a bare Enter()/Exit() pair.  The target
        // stores &gLockPendingLists TWICE before the loop (0x82E392AC
        // `stw r11, 0x54(r31)` and 0x82E392B4 `stw r11, 0x78(r31)`) and
        // &gVoiceGC twice as well (0x50 and 0x7c): one copy is the hoisted
        // anchor, the other is the guard object's `mCritSec` member, which has
        // to stay in memory for the unwind funclet.  That EH state is also what
        // buys the target's `subi r31, r1, 0x140` frame pointer (0x82E39258)
        // and its 0x140 frame -- see the same lever in dispose() above.
        {
            CritSecTracker lock(&gLockPendingLists);
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
        }

        if (gInProgressVoices.size() > 0) {
            for (std::list<Voice *>::iterator it = gInProgressVoices.begin();
                 it != gInProgressVoices.end(); ++it) {
                (*it)->blockingStart(true);
            }
            gInProgressVoices.clear();
        }

        if (gInProgressSyncVoices.size() > 0) {
            for (std::list<Voice *>::iterator it = gInProgressSyncVoices.begin();
                 it != gInProgressSyncVoices.end(); ++it) {
                (*it)->blockingStart(true);
            }
            gInProgressSyncVoices.clear();
        }

        if (gWasCommitSyncVoices && TheXboxSynth) {
            int *pMasterVoice = (int *)TheXboxSynth->unkec;
            HRESULT hr =
                ((HRESULT(*)(int *, int))(*(int *)(*(int *)pMasterVoice + 0x34)))(pMasterVoice, 0);
            if (!(SUCCEEDED(hr))) {
                TheDebugFailer << MakeString(kAssertStr, "Voice.cpp", 0x76, "SUCCEEDED(hr)");
            }
        }

        // Process voice garbage collection
        {
            CritSecTracker lock(&gVoiceGC);
            int gcCount = 0;
            unsigned int now = GetTickCount() - 500000;
            // `begin()` is bound to a NAMED iterator, not left an unnamed temporary
            // inside the loop condition.  0x82E394D8-0x82E39510 copies all four
            // words of _M_start into the 16-byte slot at 0x90(r31) and then reloads
            // `0x90(r31)` to compare against `_M_finish._M_cur` read straight off
            // the deque at 0x10(r26); the same four-word copy is repeated at the
            // bottom of the loop, 0x82E39560-0x82E39590.  An unnamed temporary is
            // folded away and only `_M_start._M_cur` is read.
            for (;;) {
                std::deque<PoolVoice>::iterator front = s_voiceGC.begin();
                if (front == s_voiceGC.end()) {
                    break;
                }
                // The tick difference is computed and tested in 64 bits, with an
                // explicit wraparound fixup -- 0x82E39520 `subf r11, r11, r29`
                // over two zero-extended 32-bit ticks (0x82E39514
                // `rldicl r29, r10, 0, 32` and the `lwz` of disposeTick), then
                // 0x82E39524 `cmpdi cr6, r11, 0x0` / 0x82E3952C-0x82E39534
                // `li r12, 1` / `rldicr r12, r12, 32, 63` / `add r11, r11, r12`.
                // All three compares are `cmpdi` (signed doubleword), so the
                // variable is a 64-bit signed one; an `unsigned int` elapsed gives
                // `cmplwi` throughout and no fixup at all.
                long long elapsed =
                    (long long)now - (long long)(unsigned int)s_voiceGC.front().disposeTick;
                if (elapsed < 0) {
                    elapsed += 1LL << 32;
                }
                if (elapsed < 50 && elapsed != 0) {
                    break;
                }
                s_voiceGCInProgress.push_back(s_voiceGC.front());
                s_voiceGC.pop_front();
                gVoiceCounters[1]--;
                if (++gcCount >= 4) {
                    break;
                }
            }
        }

        // NOT `if (TheXboxSynth) { ... }`: the target computes &TheXboxSynth->unkb0
        // unconditionally (addic. r29, r11, 0xb0) and guards only the Enter()/Exit()
        // pair on the resulting pointer -- the same idiom createOrReuse() uses above.
        // The drain loop itself runs even with no synth.
        {
            CritSecTracker lock(&TheXboxSynth->unkb0);
            for (std::deque<PoolVoice>::iterator it = s_voiceGCInProgress.begin();
                 it != s_voiceGCInProgress.end(); ++it) {
                PoolVoice &pv = *it;
                // IXAudio2Voice::DestroyVoice() -- slot 0x48, no arguments, and the
                // target calls it without a null check on sourceVoice.
                ((void (*)(int))(*(int *)(*(int *)pv.sourceVoice + 0x48)))(pv.sourceVoice);
                // `delete`-shaped: the null check guards only the deleting destructor
                // call; the field clears and the egParams free are unconditional.
                if (pv.eg) {
                    ((void (*)(void *, int))(*(int *)(*(int *)pv.eg + 0x38)))(pv.eg, 1);
                }
                pv.eg = 0;
                PoolFree(0x10, pv.egParams, __FILE__, 0x1e, "EnvelopeGeneratorParams");
                pv.egParams = 0;
            }
        }
        s_voiceGCInProgress.clear();

        rolling++;
        WaitForSingleObject(gEvent, INFINITE);
    }
    return 0;
}
