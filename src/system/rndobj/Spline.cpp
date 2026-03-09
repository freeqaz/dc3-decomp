#include "rndobj/Spline.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "math/Rot.h"
#include "os/Debug.h"
#include "rndobj/Poll.h"
#include "rndobj/ShaderMgr.h"
#include "utl/BinStream.h"

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

const RndSpline::CtrlPoint &RndSpline::GetDeformedCtrlPoint(int iIndex) const {
    MILO_ASSERT_RANGE(iIndex, 0, (int)mDeformedCtrlPoints.size(), 0x56);
    return mDeformedCtrlPoints[iIndex];
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

void RndSpline::SyncDeformedCtrlPoints(int startCtrlPt, int endCtrlPt) const {
    // Stub implementation
}

void RndSpline::PrepareShader(float farg0, float farg1) const {
    int tempIdx = mCtrlPoints.size();
    int endIdx = mEndCtrlPoint;
    int startIdx = mStartCtrlPoint;
    if ((unsigned int)((int)(mDeformedCtrlPoints.capacity() - mDeformedCtrlPoints.size()) / 88) >= 2U) {
        int temp = 0 - (startIdx + 1);
        int actualStart = ((temp - temp) - !1) & startIdx;
        if (endIdx == -1) {
            endIdx = (((int)(mCtrlPoints.capacity() - mCtrlPoints.size()) / 88) - 1);
        }
        SyncDeformedCtrlPoints(actualStart, endIdx);
        int count = endIdx - actualStart;
        int idx = actualStart;
        if ((count + 1) >= 0xC) {
            MILO_ASSERT(false, 0x1C1);
        }
        if (actualStart <= endIdx) {
            int constIdx = 0xAF;
            do {
                const CtrlPoint &pt = GetDeformedCtrlPoint(idx);
                if ((unsigned char)pt.mDirtyConstants != 0) {
                    MILO_ASSERT(false, 0x1CF);
                }
#ifdef HX_NATIVE
                TheShaderMgr.SetVConstant((VShaderConstant)(constIdx - 2), pt.mCoeff0);
                TheShaderMgr.SetVConstant((VShaderConstant)(constIdx - 1), pt.mCoeff1);
                TheShaderMgr.SetVConstant((VShaderConstant)constIdx, pt.mCoeff2);
                TheShaderMgr.SetVConstant((VShaderConstant)(constIdx + 1), pt.mCoeff3);
#else
                TheShaderMgr.SetVConstant((VShaderConstant)(constIdx - 2), *(Vector4 *)((unsigned char *)&pt + 0x18));
                TheShaderMgr.SetVConstant((VShaderConstant)(constIdx - 1), *(Vector4 *)((unsigned char *)&pt + 0x28));
                TheShaderMgr.SetVConstant((VShaderConstant)constIdx, *(Vector4 *)((unsigned char *)&pt + 0x38));
                TheShaderMgr.SetVConstant((VShaderConstant)(constIdx + 1), *(Vector4 *)((unsigned char *)&pt + 0x48));
#endif
                idx++;
                constIdx += 4;
            } while (idx <= endIdx);
        }
        float zero = 0.0f;
        float invFarg1 = 1.0f / farg1;
        float countAsFloat = (float)(double)count;
        Vector4 shader1(countAsFloat, invFarg1, 0.0f, zero);
        TheShaderMgr.SetVConstant(kVS_SplineData1, shader1);
        if ((unsigned char)mPulseDrawing != 0) {
            float startAsFloat = (float)(double)actualStart;
            float amp = mPulseAmplitude;
            float offset = mPulseOffset - startAsFloat;
            float perPt = (mYPerCtrlPoint / mPulseLength) * 2.0f;
            Vector4 shader2(offset, amp, perPt, zero);
            TheShaderMgr.SetVConstant(kVS_SplineData2, shader2);
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
    float offset = mPulseOffset + 1.0f / 30.0f;
    mPulseOffset = offset;
    if (offset <= (float)((unsigned int)mCtrlPoints.size()))
        return;
    mTestPulseActive = false;
    mPulseDrawing = false;
    mPulseOffset = -1000.0f;
}
