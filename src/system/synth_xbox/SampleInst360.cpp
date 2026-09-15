#include "synth360\SampleInst.h"
#include "synth_xbox\Voice.h"
#include "synth_xbox\FxSend.h"

SampleInst360::SampleInst360(SynthSample360 *sample, bool loop, int startSample, int endSample)
    : SampleInst(sample) {
    mVoice = new Voice(sample->IsXMA(), sample->GetNumChannels(), false);
    mVoice->SetSampleRate(sample->GetSampleRate());
    mVoice->SetData(sample->GetData(), sample->GetNumBytes(), sample->GetNumSamples());
    if (loop) {
        mVoice->SetLoopRegion(startSample, endSample);
    }
}

SampleInst360::~SampleInst360() {
    Voice *voice = mVoice;
    if (voice) {
        delete voice;
    }
}

#ifdef HX_NATIVE
bool SampleInst360::IsPlaying() const { return mVoice->IsPlaying(); }
#else
bool SampleInst360::IsPlaying() { return mVoice->IsPlaying(); }
#endif

void SampleInst360::SetFXCore(FXCore core) {}

float SampleInst360::GetProgress() {
    XAUDIO2_VOICE_STATE state;
    ((IXAudio2SourceVoice *)mVoice->mPoolVoice.sourceVoice)->GetState(&state, 0);
    // RESIDUAL (w7-ak, 89.55 canonical): the residual is one extra instruction plus
    // the regalloc cascade it drives. The target re-materialises `voice` inside the
    // `if` with a no-op truncating move (`clrrwi r10, r10, 0` at 0x82...+20) that we
    // do not emit, and keeps `pos` in r11 where we keep it in r9; the `%` operator's
    // two traps (`twllei` / `twi 5`) are scheduled either side of the remainder
    // subtraction instead of before and after it. NEGATIVE RESULT: moving this
    // declaration inside the `if` (testing `mVoice->mLoopStart` in the condition),
    // which is what would produce that re-materialisation, drops it to 88.13.
    // NEGATIVE (w7-bq, 2026-09-15): writing the sum the other way round,
    // `pos = voice->mStartSamp + offset % len;`, is inert -- 89.55224 before and
    // after, same 70 instructions and the same 15 diff_arg / 3 insert / 4 delete
    // profile -- so the trap scheduling is not commutative-operand order.
    // Measured shape of the whole residual: the instruction SET is identical on
    // both sides and only its order differs.  Target runs mullw, andc, subf,
    // twllei, twi, add (0x324..0x338); we run mullw, twllei, subf, andc, add,
    // twi.  That plus the target-only `clrrwi r10, r10, 0` at 0x2f8 is the
    // entire gap: regions 0-15 and 42-69 are 100%, and every one of the 15
    // diff_arg rows is the r11/r9/r8 renaming those two drive.  Note that
    // `lwz` already zero-extends on Xenon, so 0x2f8 is a true no-op the backend
    // chose to emit -- there is no source-level cast that removes an
    // instruction we do not have.
    // BUG FIX (w7-br, 89.6 -> 98.5 canonical, 22 -> 1 rows): the "pure
    // scheduling" residual above was a real behavioural divergence.  The image
    // subtracts mLoopStart on BOTH arms of the loop-end select: the `bne cr6`
    // at 0x82E44744 lands ON `subf r8, r7, r8` at 0x82E4474C (r7 = mLoopStart,
    // r8 = mLoopEnd or, at 0x82E44748, mNumSamples), so len is
    // `(mLoopEnd == -1 ? mNumSamples : mLoopEnd) - mLoopStart`.  Our old
    // `mLoopEnd == -1 ? mNumSamples - mLoopStart : mLoopEnd` jumped PAST the
    // subtraction and took the absolute loop-end position as the loop length
    // whenever an explicit loop end was set, so the modulo wrapped at the
    // wrong period.  Every trap/regalloc row was a consequence of that one
    // missing subf.  The sole remaining row is the no-op `clrrwi r10, r10, 0`
    // at 0x82E44730 on the voice pointer.  NEGATIVE (w7-br), post-fix:
    // declaring `voice` inside the if with `mVoice->mLoopStart` in the
    // condition, 92.2 (the field is reloaded); keeping the outer local but
    // testing `mVoice->mLoopStart`, the same 92.2; `unsigned int pos =
    // state.SamplesPlayed` (implicit narrowing), 97.6 -- pos must be a signed
    // int, the image's `extsw r11, r11` at 0x82E44778 becomes `rldicl`.
    Voice *voice = mVoice;
    int pos = (unsigned int)state.SamplesPlayed;
    if (voice->mLoopStart >= 0) {
        int offset = pos - voice->mStartSamp;
        int len = (voice->mLoopEnd == -1 ? voice->mNumSamples : voice->mLoopEnd)
            - voice->mLoopStart;
        pos = offset % len + voice->mStartSamp;
    }
    SynthSample360 *sample = (SynthSample360 *)mSample.Ptr();
    return pos * 1000.0f / (sample->LengthMs() * sample->GetSampleRate());
}

void SampleInst360::StartImpl() { mVoice->Start(); }

void SampleInst360::StopImpl(bool b) { mVoice->Stop(b); }

void SampleInst360::EndLoopImpl() { mVoice->EndLoop(); }

void SampleInst360::SetVolumeImpl(float vol) { mVoice->SetVolume(vol); }

void SampleInst360::SetPanImpl(float pan) { mVoice->SetPan(pan); }

void SampleInst360::SetSpeedImpl(float speed) { mVoice->SetSpeed(speed); }

void SampleInst360::Pause(bool b) { mVoice->Pause(b); }

void SampleInst360::SetADSR(const ADSRImpl &adsr) {
    mVoice->mAttackRate = adsr.GetAttackRate();
    mVoice->mReleaseRate = adsr.GetReleaseRate();
}

float SampleInst360::ElapsedTime() {
    XAUDIO2_VOICE_STATE state;
    ((IXAudio2SourceVoice *)mVoice->mPoolVoice.sourceVoice)->GetState(&state, 0);
    float samples = (double)state.SamplesPlayed;
    return samples / (float)mSample->GetSampleRate();
}

void SampleInst360::SetSendImpl(FxSend *send) {
    mVoice->SetSend(dynamic_cast<FxSend360 *>(send));
}

void SampleInst360::SetReverbMixDbImpl(float db) { mVoice->SetReverbMixDb(db); }

void SampleInst360::SetReverbEnableImpl(bool enable) { mVoice->SetReverbEnable(enable); }
