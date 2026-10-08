#include "hamobj\PracticeSection.h"
#include "hamobj\DancerSequence.h"
#include "hamobj\Difficulty.h"
#include "hamobj\MoveDir.h"
#include "obj/Object.h"
#include "rndobj\Anim.h"
#include "synth\FxSend.h"
#include "utl\Std.h"

#ifndef HX_NATIVE
void (*gForceDestroyRange)(LevelData *, LevelData *) = stlpmtx_std::_Destroy_Range;
#endif

PracticeSection::PracticeSection() : mDifficulty(kDifficultyEasy), mTestStepSequence(0) {}

PracticeSection::~PracticeSection() { DeleteAll(mSeqs); }

BEGIN_HANDLERS(PracticeSection)
    HANDLE_SUPERCLASS(RndAnimatable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_CUSTOM_PROPSYNC(PracticeStep)
    SYNC_PROP_SET(type, o.mType, o.SetType(_val.Sym()))
    SYNC_PROP(start, o.mStart)
    SYNC_PROP(end, o.mEnd)
    SYNC_PROP(boundary, o.mBoundary)
    SYNC_PROP(name_override, o.mNameOverride)
END_CUSTOM_PROPSYNC

BEGIN_PROPSYNCS(PracticeSection)
    SYNC_PROP(display_name, mDisplayName)
    SYNC_PROP_SET(difficulty, mDifficulty, mDifficulty = (Difficulty)_val.Int())
    SYNC_PROP(steps, mSteps)
    SYNC_PROP_MODIFY(
        test_step_sequence,
        mTestStepSequence,
        {
            // A full clamp: the image's 0x824CF388 `bgt cr6, 0x824CF398` (index > size-1)
            // lands on the store `stw r10, 0(r30)` with r10 = size-1.  Ours
            // skipped the store there, so an out-of-range index was kept
            // instead of clamped to the last sequence (w20-a).
            mTestStepSequence = Clamp<int>(0, mSeqs.size() - 1, mTestStepSequence);
        }
    )
    SYNC_SUPERCLASS(RndAnimatable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BinStream &operator<<(BinStream &bs, const PracticeStep &step) {
    bs << step.mType;
    bs << step.mStart;
    bs << step.mEnd;
    bs << step.mBoundary;
    bs << step.mNameOverride;
    return bs;
}

BEGIN_SAVES(PracticeSection)
    SAVE_REVS(3, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndAnimatable)
    bs << mDisplayName;
    bs << mDifficulty;
    bs << mSteps;
    bs << mSeqs.size();
    for (int i = 0; i < mSeqs.size(); i++) {
        mSeqs[i]->Save(bs);
    }
    bs << mTestStepSequence;
END_SAVES

BEGIN_COPYS(PracticeSection)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndAnimatable)
    CREATE_COPY(PracticeSection)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mDisplayName)
        COPY_MEMBER(mDifficulty)
        mSteps.clear();
        mSteps.insert(mSteps.begin(), c->mSteps.begin(), c->mSteps.end());
        COPY_MEMBER(mTestStepSequence)
        DeleteAll(mSeqs);
        for (int i = 0; i < c->mSeqs.size(); i++) {
            DancerSequence *curSeq = Hmx::Object::New<DancerSequence>();
            curSeq->Copy(c->mSeqs[i], ty);
            mSeqs.push_back(curSeq);
        }
    END_COPYING_MEMBERS
END_COPYS

BinStreamRev &operator>>(BinStreamRev &bs, PracticeStep &step) {
    bs >> step.mType;
    bs >> step.mStart;
    bs >> step.mEnd;
    if (bs.rev > 0) {
        bs >> step.mBoundary;
    }
    if (bs.rev > 2) {
        bs >> step.mNameOverride;
    }
    if (step.mType != "learn") {
        step.mBoundary = true;
    }
    return bs;
}

INIT_REVS(3, 0)

BEGIN_LOADS(PracticeSection)
    LOAD_REVS(bs)
    ASSERT_REVS(3, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    if (d.rev > 1) {
        LOAD_SUPERCLASS(RndAnimatable)
    }
    bs >> mDisplayName;
    int diff;
    bs >> diff;
    mDifficulty = (Difficulty)diff;
    d >> mSteps;
    DeleteAll(mSeqs);
    if (d.rev > 1) {
        int numSeqs;
        bs >> numSeqs;
        for (int i = 0; i < numSeqs; i++) {
            DancerSequence *curSeq = Hmx::Object::New<DancerSequence>();
            curSeq->Load(bs);
            mSeqs.push_back(curSeq);
        }
        bs >> mTestStepSequence;
    }
END_LOADS

void PracticeSection::SetFrame(float frame, float blend) {
    RndAnimatable::SetFrame(frame, blend);
    DancerSequence *seq;
    if (mTestStepSequence >= 0 && mTestStepSequence < mSeqs.size()) {
        seq = mSeqs[mTestStepSequence];
    } else {
        seq = nullptr;
    }
    if (seq) {
        seq->SetFrame(frame, blend);
        MoveDir *dir = dynamic_cast<MoveDir *>(Dir());
        if (dir) {
            dir->SetDancerSequence(seq);
        }
    }
}

float PracticeSection::StartFrame() {
    DancerSequence *seq;
    if (mTestStepSequence >= 0 && mTestStepSequence < mSeqs.size()) {
        seq = mSeqs[mTestStepSequence];
    } else {
        seq = nullptr;
    }
    if (seq) {
        return seq->StartFrame();
    } else
        return 0;
}

float PracticeSection::EndFrame() {
    DancerSequence *seq;
    if (mTestStepSequence >= 0 && mTestStepSequence < mSeqs.size()) {
        seq = mSeqs[mTestStepSequence];
    } else {
        seq = nullptr;
    }
    if (seq) {
        return seq->EndFrame();
    } else
        return 0;
}

const std::vector<PracticeStep> &PracticeSection::Steps() const { return mSteps; }
void PracticeSection::ClearSteps() { ClearAndShrink(mSteps); }
void PracticeSection::AddStep(PracticeStep step) { mSteps.push_back(step); }

// RESIDUAL (w8-i, 99.90 canonical / 97.21212 fuzzy): 15 rows of 33, ONE cause --
// the four volatile registers are allocated one slot rotated from the image's.
// Image: r6=idx, r7=gNullStr, r8=mSteps.end(), r9=it->mStart.  Ours: r6=gNullStr,
// r7=end, r8=mStart, r9=idx.  Everything else follows, including the OFFSET_SWAP
// at idx 17/18, which is just the two induction bumps in the opposite order
// (image `addi r6,r6,0x1` then `addi r11,r11,0x18`).
// REFUTED, five spellings, every one BIT-IDENTICAL to this body (same 15 rows,
// same registers): (a) `++idx, ++it` instead of `++it, ++idx`; (b) hoisting the
// iterator out of the for-init so `int idx = 0` is the for-init declaration;
// (c) moving `idx++` to the END of the loop body, which is the source position
// that would put the idx bump first if the bump order were reachable at all;
// (d) `unsigned int idx`; (e) binding `PracticeStep &step = *it;` at the top of
// the body -- the lever that closed DetectFrame::Reset, inert here because `it`
// is already an induction variable rather than a body-derived address.
// The permutation is a whole-set rotation, i.e. an allocator tie-break upstream
// of anything the source can say.
// BEHAVIOURAL FIX (w9-a): `idx` indexes mSeqs by the count of steps that have
// BOTH symbols set, not by the raw step position.  The image increments it inside
// the non-null test, and only on the path where the start/end match FAILS:
//   824CD1BC  lwz r9, 0x4(r11)      ; it->mStart
//   824CD1C0  cmplw cr6, r9, r7     ; == gNullStr?
//   824CD1C4  beq cr6, 824CD1E8     ; -> straight to ++it, SKIPPING ++idx
//   824CD1C8  lwz r10, 0x8(r11)     ; it->mEnd
//   824CD1D0  beq cr6, 824CD1E8     ; -> same
//   824CD1D4  cmplw cr6, r9, r4     ; mStart == start?
//   824CD1D8  bne cr6, 824CD1E4     ; -> ++idx
//   824CD1DC  cmplw cr6, r10, r5    ; mEnd == end?
//   824CD1E0  beq cr6, 824CD1FC     ; -> found, idx NOT incremented
//   824CD1E4  addi r6, r6, 0x1      ; ++idx
//   824CD1E8  addi r11, r11, 0x18   ; ++it
// A step with an unset mStart or mEnd therefore does not consume an mSeqs slot.
// The old `++it, ++idx` in the for-increment counted every step, so any section
// holding a partially-filled step returned the WRONG DancerSequence (or a
// spurious null once idx ran past mSeqs.size()) for every later step.
DancerSequence *PracticeSection::SequenceForDetection(Symbol start, Symbol end) {
    int idx = 0;
    for (std::vector<PracticeStep>::iterator it = mSteps.begin(); it != mSteps.end();
         ++it) {
        if (!it->mStart.Null() && !it->mEnd.Null()) {
            if (it->mStart == start && it->mEnd == end) {
                if (idx < mSeqs.size()) {
                    return mSeqs[idx];
                } else
                    return nullptr;
            }
            ++idx;
        }
    }
    return nullptr;
}
