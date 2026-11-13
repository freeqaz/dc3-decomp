#include "rndobj\Spline.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "math\Rot.h"
#include "os\Debug.h"
#include "rndobj\Poll.h"
#include "rndobj\ShaderMgr.h"
#include "utl/BinStream.h"

RndSpline *RndSpline::sGlobalDefaultSpline;

/** Y separation given to a freshly added control point on a two-point spline.
 *  The target reloads this from .data inside the loop (lbl_8209F850), which is
 *  the tell for a mutable file-scope float rather than a literal. */
static float gNewCtrlPointYOffset = 10.0f;
// Lane history (5 lines moved): docs/decomp/notes/system/rndobj/Spline.md

/** Per-frame advance of the test pulse: Poll loads it from the named slot
 *  0x8209F85C (`lfs f0, lbl_8209F85C@l`), not from a __real@ literal.  Being
 *  defined before INIT_REVS also takes gRev off offset 0 of this TU's .rdata,
 *  which is what makes MSVC anchor Load's ASSERT_REVS pair on gAltRev like the
 *  image (97.97 -> 100). */
const float kPulseStep = 1.0f / 30.0f;

RndSpline::CtrlPoint::CtrlPoint()
    : mPos(Vector3::ZeroVec()), mRoll(0), mDirtyPosition(1), mDirtyConstants(1),
      mCoeff0(Vector4::ZeroVec()), mCoeff1(Vector4::ZeroVec()), mCoeff2(Vector4::ZeroVec()),
      mCoeff3(Vector4::ZeroVec()) {}

void RndSpline::CtrlPoint::Save(BinStream &bs) const {
    bs << mPos;
    bs << mRoll;
}

void RndSpline::CtrlPoint::Load(BinStreamRev &d) {
    d >> mPos;
    d >> mRoll;
    mDirtyPosition = false;
}

RndSpline::RndSpline()
    : mManual(false), mPulseLength(10), mPulseAmplitude(10), mStartCtrlPoint(-1),
      mEndCtrlPoint(-1), mYOffset(0), mYPerCtrlPoint(10), unk144(0), unk145(0), mPulseDrawing(0),
      mPulseOffset(-1000), mTestPulseActive(0) {}

BEGIN_HANDLERS(RndSpline)
    HANDLE(test_pulse, OnTestPulse)
    HANDLE(set_global_default, OnSetGlobalDefaultSpline)
    HANDLE(clear_global_default, OnClearGlobalDefaultSpline)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_CUSTOM_PROPSYNC(RndSpline::CtrlPoint)
    SYNC_PROP(pos, o.mPos)
    SYNC_PROP_SET(roll, o.mRoll * RAD2DEG, o.mRoll = _val.Float() * DEG2RAD)
END_CUSTOM_PROPSYNC

BEGIN_PROPSYNCS(RndSpline)
    SYNC_PROP_MODIFY(ctrl_points, mCtrlPoints, SyncPristineCtrlPoints())
    SYNC_PROP(manual, mManual)
    SYNC_PROP(pulse_length, mPulseLength)
    SYNC_PROP(pulse_amplitude, mPulseAmplitude)
    SYNC_PROP_SET(start_ctrl_point, mStartCtrlPoint, SetStartCtrlPoint(_val.Int()))
    SYNC_PROP_SET(end_ctrl_point, mEndCtrlPoint, SetEndCtrlPoint(_val.Int()))
    SYNC_PROP_SET(y_offset, mYOffset, mYOffset = _val.Float())
    SYNC_PROP_SET(
        y_per_ctrl_point, mYPerCtrlPoint, mYPerCtrlPoint = Max(_val.Float(), 0.1f)
    )
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BinStream &operator<<(BinStream &bs, const RndSpline::CtrlPoint &pt) {
    pt.Save(bs);
    return bs;
}

BEGIN_SAVES(RndSpline)
    SAVE_REVS(1, 0)
    SAVE_SUPERCLASS(RndPollable)
    bs << mCtrlPoints;
    bs << mManual;
    bs << mPulseLength;
    bs << mPulseAmplitude;
    bs << mStartCtrlPoint;
    bs << mEndCtrlPoint;
    bs << mYOffset;
    bs << mYPerCtrlPoint;
    bs << (this == sGlobalDefaultSpline);
END_SAVES

BEGIN_COPYS(RndSpline)
    if (this != o) {
        COPY_SUPERCLASS(RndPollable)
        CREATE_COPY(RndSpline)
        BEGIN_COPYING_MEMBERS
            COPY_MEMBER(mCtrlPoints)
            COPY_MEMBER(mManual)
            COPY_MEMBER(mPulseLength)
            COPY_MEMBER(mPulseAmplitude)
            COPY_MEMBER(mStartCtrlPoint)
            COPY_MEMBER(mEndCtrlPoint)
            COPY_MEMBER(mYOffset)
            COPY_MEMBER(mYPerCtrlPoint)
            SyncPristineCtrlPoints();
        END_COPYING_MEMBERS
    }
END_COPYS

BinStreamRev &operator>>(BinStreamRev &d, RndSpline::CtrlPoint &pt) {
    pt.Load(d);
    return d;
}

INIT_REVS(1, 0)

// Lane history (6 lines moved): docs/decomp/notes/system/rndobj/Spline.md
// > w15-r: the w9-f RESIDUAL that sat here (97.970, nine rows, "the anchor pick is a
// > function-local allocation decision") is CLOSED.
BEGIN_LOADS(RndSpline)
    LOAD_REVS(bs)
    ASSERT_REVS(1, 0)
    LOAD_SUPERCLASS(RndPollable)
    d >> mCtrlPoints;
    d >> mManual;
    if (d.rev >= 1) {
        d >> mPulseLength;
        d >> mPulseAmplitude;
    }
    d >> mStartCtrlPoint;
    d >> mEndCtrlPoint;
    d >> mYOffset;
    d >> mYPerCtrlPoint;
    bool sync;
    d >> sync;
    if (sync) {
        sGlobalDefaultSpline = this;
    }
    SyncPristineCtrlPoints();
END_LOADS

DataNode RndSpline::OnTestPulse(DataArray *) {
    if (!mTestPulseActive) {
        mTestPulseActive = true;
        mPulseDrawing = true;
        mPulseOffset = -1;
    }
    return 0;
}

DataNode RndSpline::OnSetGlobalDefaultSpline(DataArray *) {
    sGlobalDefaultSpline = this;
    return 0;
}

DataNode RndSpline::OnClearGlobalDefaultSpline(DataArray *) {
    sGlobalDefaultSpline = nullptr;
    return 0;
}

const RndSpline::CtrlPoint &RndSpline::GetDeformedCtrlPointOrDummy(int iIndex) const {
    MILO_ASSERT_RANGE_EQ(iIndex, -1, (int)(mDeformedCtrlPoints.size()) + 1, 0x2F7);
    if (iIndex == -1) {
        return mDummyBefore;
    } else if (iIndex == (int)mDeformedCtrlPoints.size()) {
        return mDummyAfter;
    } else if (iIndex == (int)mDeformedCtrlPoints.size() + 1) {
        return mDummyAfterEnd;
    } else {
        return mDeformedCtrlPoints[iIndex];
    }
}

void RndSpline::SyncPristineCtrlPoints() {
    bool foundNew = false;
    for (int i = mCtrlPoints.size() - 1; 0 <= i; i--) {
        CtrlPoint &pt = mCtrlPoints[i];
        if (pt.mDirtyPosition) {
            MILO_ASSERT(!foundNew, 0x227);
            foundNew = true;
            pt.mDirtyPosition = false;
            if (mCtrlPoints.size() == 2) {
                if (i == 0) {
                    pt = mCtrlPoints[1];
                    pt.mPos.y -= gNewCtrlPointYOffset;
                } else {
                    pt = mCtrlPoints[i - 1];
                    pt.mPos.y += gNewCtrlPointYOffset;
                }
            } else if (mCtrlPoints.size() > 2) {
                if (i == 0) {
                    CtrlPoint &p1 = mCtrlPoints[1];
                    CtrlPoint &p2 = mCtrlPoints[2];
                    Vector3 d;
                    Subtract(p1.mPos, p2.mPos, d);
                    Add(p1.mPos, d, pt.mPos);
                    pt.mRoll = p1.mRoll;
                } else if (i == mCtrlPoints.size() - 1) {
                    CtrlPoint &pPrev = mCtrlPoints[i - 1];
                    CtrlPoint &pPrev2 = mCtrlPoints[i - 2];
                    Vector3 d;
                    Subtract(pPrev.mPos, pPrev2.mPos, d);
                    Add(pPrev.mPos, d, pt.mPos);
                    pt.mRoll = pPrev.mRoll;
                } else {
                    pt.Interp(mCtrlPoints[i - 1], mCtrlPoints[i + 1], 0.5f);
                }
            }
        }
    }
    if (mCtrlPoints.size() >= 2) {
        if (mEndCtrlPoint != -1) {
            mEndCtrlPoint = Clamp(1, (int)mCtrlPoints.size() - 1, mEndCtrlPoint);
        }
        if (mStartCtrlPoint != -1) {
            // Clamp, not a hand-written if/else -- and parallel to the
            // mEndCtrlPoint line above.  Clamp's body is
            //   value > max ? max : (value < min ? min : value)
            // which is exactly the image's shape at 826B478C-826B47B8: one
            // `bgt` to a SHARED store (826B47B4 `stw r10, 0x20(r20)`), with the
            // lower bound applied branchlessly in the fall-through as
            // `srwi r10, r11, 31 / subi r10, r10, 0x1 / and r10, r10, r11`
            // (826B47A8-47B0) -- the sign-bit mask MSVC emits for `x < 0`.
            // The hand-written form cost 4 instructions and the whole tail:
            // `Max(0, mStartCtrlPoint)` is `(0 < x) ? x : 0`, whose comparison
            // is against a materialised zero, so MSVC used the generic carry
            // mask (`li r10, 0` / `subfc` / `subfe`) instead of the sign bit,
            // and the two arms each stored to mStartCtrlPoint separately.
            mStartCtrlPoint = Clamp(0, mEndCtrlPoint - 1, mStartCtrlPoint);
        }
    } else {
        mStartCtrlPoint = -1;
        mEndCtrlPoint = -1;
    }
    mDeformedCtrlPoints = mCtrlPoints;
    unk144 = true;
    unk145 = true;
}

void RndSpline::SyncDeformedDummyCtrlPoints(int iStartIndex, int iEndIndex) const {
    MILO_ASSERT_RANGE(iStartIndex, 0, (int)mDeformedCtrlPoints.size(), 0x2C5);
    MILO_ASSERT_RANGE(iEndIndex, 0, (int)mDeformedCtrlPoints.size(), 0x2C6);
    MILO_ASSERT(iStartIndex <= iEndIndex, 0x2C7);
    if ((unsigned int)mDeformedCtrlPoints.size() >= 2) {
        // Lane history (20 lines moved): docs/decomp/notes/system/rndobj/Spline.md
        if (unk144 && iStartIndex == 0) {
            const CtrlPoint &pt0 = mDeformedCtrlPoints[0];
            const CtrlPoint &pt1 = mDeformedCtrlPoints[1];
            unk144 = false;
            Vector3 delta;
            Subtract(pt0.mPos, pt1.mPos, delta);
            CtrlPoint &before = mDummyBefore;
            Add(pt0.mPos, delta, mDummyBefore.mPos);
            before.mRoll = pt0.mRoll;
            mDeformedCtrlPoints[0].mDirtyConstants = true;
        }
        int lastIdx = (int)mDeformedCtrlPoints.size() - 1;
        if (unk145 && iEndIndex >= lastIdx - 1) {
            const CtrlPoint &last = mDeformedCtrlPoints[lastIdx];
            const CtrlPoint &prev = mDeformedCtrlPoints[lastIdx - 1];
            unk145 = false;
            Vector3 delta;
            Subtract(last.mPos, prev.mPos, delta);
            CtrlPoint &after = mDummyAfter;
            Add(last.mPos, delta, mDummyAfter.mPos);
            after.mRoll = last.mRoll;
            Subtract(mDummyAfter.mPos, mDeformedCtrlPoints[lastIdx].mPos, delta);
            CtrlPoint &afterEnd = mDummyAfterEnd;
            Add(mDummyAfter.mPos, delta, mDummyAfterEnd.mPos);
            afterEnd.mRoll = after.mRoll;
            mDeformedCtrlPoints[lastIdx - 1].mDirtyConstants = true;
            mDeformedCtrlPoints[lastIdx].mDirtyConstants = true;
        }
    }
}

// Vector4 has no arithmetic helpers in Vec.h; SyncDeformedCtrlPoints needs
// the by-reference accumulate form (see the note in its body).
static inline void ScaleAddEq(Vector4 &v, const Vector4 &a, float s) {
    v.x += a.x * s;
    v.y += a.y * s;
    v.z += a.z * s;
    v.w += a.w * s;
}

static inline void ScaleSubEq(Vector4 &v, const Vector4 &a, float s) {
    v.x -= a.x * s;
    v.y -= a.y * s;
    v.z -= a.z * s;
    v.w -= a.w * s;
}

void RndSpline::SyncDeformedCtrlPoints(int iStartIndex, int iEndIndex) const {
    MILO_ASSERT_RANGE(iStartIndex, 0, (int)mDeformedCtrlPoints.size(), 0x278);
    MILO_ASSERT_RANGE(iEndIndex, 0, (int)mDeformedCtrlPoints.size(), 0x279);
    MILO_ASSERT(iStartIndex <= iEndIndex, 0x27A);
    if ((unsigned int)mDeformedCtrlPoints.size() >= 2) {
        SyncDeformedDummyCtrlPoints(iStartIndex, iEndIndex);
        for (int i = iStartIndex; i <= iEndIndex; i++) {
            CtrlPoint &pt = mDeformedCtrlPoints[i];
            if (pt.mDirtyConstants) {
                pt.mDirtyConstants = false;
                const CtrlPoint &prev = GetDeformedCtrlPointOrDummy(i - 1);
                const CtrlPoint &next = GetDeformedCtrlPointOrDummy(i + 1);
                const CtrlPoint &nextNext = GetDeformedCtrlPointOrDummy(i + 2);

                Vector4 prevVec(prev.mPos.x, prev.mPos.y, prev.mPos.z, prev.mRoll);
                Vector4 nextVec(next.mPos.x, next.mPos.y, next.mPos.z, next.mRoll);
                Vector4 nextNextVec(
                    nextNext.mPos.x, nextNext.mPos.y, nextNext.mPos.z, nextNext.mRoll
                );

                // Each stage is an inlined by-reference helper: the image
                // stores every component after every stage and reloads some
                // of them for the next (826B3554 stfs y / 826B3574 lfs y), and
                // keeps dead `addi r11, r31, 0x18/0x28/0x38` (826B34C0,
                // 826B35F8, 826B35FC) -- the helpers' reference arguments.
                // Per-component `pt.mCoeff0.x -= ...` chains keep everything
                // in registers and store once.
                // mCoeff0 = -0.5*prev + 1.5*cur - 1.5*next + 0.5*nextNext
                // curVec is built from the point's own mPos here, ahead of the
                // accumulator zeroed below.  Declaring it one statement later
                // (after `pt.mCoeff0 = ZeroVec()`) reads equally naturally but
                // changes the whole coefficient schedule: with it declared
                // later the image walks the mCoeff2 block in mirror order
                // (w, z, y, x) and 24 of this function's 27 rows sit in that
                // block.  Hoisted, the function goes 98.203 -> 99.966
                // canonical with every instruction in the block equal.
                Vector4 curVec(pt.mPos.x, pt.mPos.y, pt.mPos.z, pt.mRoll);
                pt.mCoeff0 = Vector4::ZeroVec();
                ScaleSubEq(pt.mCoeff0, prevVec, 0.5f);
                ScaleAddEq(pt.mCoeff0, curVec, 1.5f);
                ScaleSubEq(pt.mCoeff0, nextVec, 1.5f);
                ScaleAddEq(pt.mCoeff0, nextNextVec, 0.5f);

                // mCoeff1 = prev - 2.5*cur + 2.0*next - 0.5*nextNext
                pt.mCoeff1 = prevVec;
                ScaleSubEq(pt.mCoeff1, curVec, 2.5f);
                ScaleAddEq(pt.mCoeff1, nextVec, 2.0f);
                ScaleSubEq(pt.mCoeff1, nextNextVec, 0.5f);

                // mCoeff2 = -0.5*prev + 0.5*next
                pt.mCoeff2 = Vector4::ZeroVec();
                ScaleSubEq(pt.mCoeff2, prevVec, 0.5f);
                ScaleAddEq(pt.mCoeff2, nextVec, 0.5f);

                // mCoeff3 = cur
                pt.mCoeff3 = curVec;
            }
        }
    }
}

void RndSpline::PrepareShader(float f1, float f2) const {
    if (mDeformedCtrlPoints.size() >= 2) {
        // Not Max(x, 0): the target's branchless idiom is
        //   addi r10, r11, 1 / subfic r10, r10, 0 / subfe r10, r10, r10 / and
        // i.e. mask = -(x != -1), so the guard is 'x == -1', not 'x < 0'.
        // Max() would emit srwi/subi/and off the sign bit, which is what we had.
        int startCtrlPt = mStartCtrlPoint == -1 ? 0 : mStartCtrlPoint;
        // A ternary, not 'endCtrlPt = mEndCtrlPoint; if (== -1) endCtrlPt = ...':
        // the patch-up form gives endCtrlPt a second def/ref, lifting its colour
        // priority (22 -> 30) above the loop index i (26), so i lost r27 to it.
        int endCtrlPt = mEndCtrlPoint == -1 ? mCtrlPoints.size() - 1 : mEndCtrlPoint;
        SyncDeformedCtrlPoints(startCtrlPt, endCtrlPt);
        MILO_ASSERT(((endCtrlPt - startCtrlPt) + 1) < kVShader_SplineMaxCtrlPoints, 0x1C1);
        int shaderConstant = 0xAE;
        for (int i = startCtrlPt; i <= endCtrlPt; i++, shaderConstant += 4) {
            const CtrlPoint &pt = GetDeformedCtrlPoint(i);
            MILO_ASSERT(!pt.mDirtyConstants, 0x1CF);
            TheShaderMgr.SetVConstant((VShaderConstant)(shaderConstant - 1), pt.mCoeff0);
            TheShaderMgr.SetVConstant((VShaderConstant)(shaderConstant), pt.mCoeff1);
            TheShaderMgr.SetVConstant((VShaderConstant)(shaderConstant + 1), pt.mCoeff2);
            TheShaderMgr.SetVConstant((VShaderConstant)(shaderConstant + 2), pt.mCoeff3);
        }
        TheShaderMgr.SetVConstant(
            kVS_SplineData1, Vector4(endCtrlPt - startCtrlPt, f1, 1.0f / f2, 0)
        );
        if (mPulseDrawing) {
            TheShaderMgr.SetVConstant(
                kVS_SplineData2,
                Vector4(
                    mPulseOffset - startCtrlPt,
                    (mYPerCtrlPoint / mPulseLength) * 2.0f,
                    mPulseAmplitude,
                    0
                )
            );
        }
    }
}

void RndSpline::SetStartCtrlPoint(int idx) {
    if (idx != -1) {
        int maxIdx = mCtrlPoints.size() - 2;
        if (idx > maxIdx)
            idx = maxIdx;
        else if (idx < 0)
            idx = 0;
    }
    if (idx == mStartCtrlPoint)
        return;
    mStartCtrlPoint = idx;
    if (idx == -1)
        return;
    if (mEndCtrlPoint != -1 && mEndCtrlPoint <= idx) {
        mEndCtrlPoint = idx + 1;
    }
}

void RndSpline::SetEndCtrlPoint(int idx) {
    if (idx != -1) {
        int maxIdx = mCtrlPoints.size() - 1;
        if (idx > maxIdx)
            idx = maxIdx;
        else if (idx < 1)
            idx = 1;
    }
    if (idx == mEndCtrlPoint)
        return;
    mEndCtrlPoint = idx;
    if (idx == -1)
        return;
    if (idx <= mStartCtrlPoint) {
        mStartCtrlPoint = idx - 1;
    }
}

void RndSpline::CtrlPoint::Interp(const CtrlPoint &a, const CtrlPoint &b, float t) {
    ::Interp(a.mPos, b.mPos, t, mPos);
    mRoll = a.mRoll + (b.mRoll - a.mRoll) * t;
}

void RndSpline::Poll() {
    if (!mTestPulseActive)
        return;
    if (!mPulseDrawing)
        return;
    float offset = mPulseOffset + kPulseStep;
    mPulseOffset = offset;
    if (offset <= (float)((unsigned int)mCtrlPoints.size()))
        return;
    mTestPulseActive = false;
    mPulseDrawing = false;
    mPulseOffset = -1000.0f;
}
