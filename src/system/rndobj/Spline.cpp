#include "rndobj/Spline.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "math/Rot.h"
#include "os/Debug.h"
#include "rndobj/Poll.h"
#include "rndobj/ShaderMgr.h"
#include "utl/BinStream.h"

RndSpline *RndSpline::sGlobalDefaultSpline;

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

void RndSpline::SyncPristineCtrlPoints() {
    bool foundNew = false;
    int size = mCtrlPoints.size();
    for (int i = size - 1; i >= 0; i--) {
        CtrlPoint &pt = mCtrlPoints[i];
        if (pt.mDirtyPosition) {
            MILO_ASSERT(!foundNew, 0x227);
            foundNew = true;
            pt.mDirtyPosition = false;
            if (size == 2) {
                if (i == 0) {
                    mCtrlPoints[0] = mCtrlPoints[1];
                    mCtrlPoints[0].mPos.y -= 10.0f;
                } else {
                    mCtrlPoints[i] = mCtrlPoints[i - 1];
                    mCtrlPoints[i].mPos.y += 10.0f;
                }
            } else if (size > 2) {
                if (i == 0) {
                    CtrlPoint &p1 = mCtrlPoints[1];
                    CtrlPoint &p2 = mCtrlPoints[2];
                    pt.mPos.x = p1.mPos.x + (p1.mPos.x - p2.mPos.x);
                    pt.mPos.y = p1.mPos.y + (p1.mPos.y - p2.mPos.y);
                    pt.mPos.z = p1.mPos.z + (p1.mPos.z - p2.mPos.z);
                    pt.mRoll = p1.mRoll;
                } else if (i == size - 1) {
                    CtrlPoint &pPrev = mCtrlPoints[i - 1];
                    CtrlPoint &pPrev2 = mCtrlPoints[i - 2];
                    pt.mPos.x = pPrev.mPos.x + (pPrev.mPos.x - pPrev2.mPos.x);
                    pt.mPos.y = pPrev.mPos.y + (pPrev.mPos.y - pPrev2.mPos.y);
                    pt.mPos.z = pPrev.mPos.z + (pPrev.mPos.z - pPrev2.mPos.z);
                    pt.mRoll = pPrev.mRoll;
                } else {
                    pt.Interp(mCtrlPoints[i - 1], mCtrlPoints[i + 1], 0.5f);
                }
            }
        }
    }
    int count = mCtrlPoints.size();
    if (count < 2) {
        mStartCtrlPoint = -1;
        mEndCtrlPoint = -1;
    } else {
        if (mEndCtrlPoint != -1) {
            int maxIdx = count - 1;
            if (mEndCtrlPoint > maxIdx) {
                mEndCtrlPoint = maxIdx;
            } else if (mEndCtrlPoint < 1) {
                mEndCtrlPoint = 1;
            }
        }
        if (mStartCtrlPoint != -1) {
            int maxStart = mEndCtrlPoint - 1;
            if (mStartCtrlPoint > maxStart) {
                mStartCtrlPoint = maxStart;
            } else if (mStartCtrlPoint < 0) {
                mStartCtrlPoint = 0;
            }
        }
    }
    mDeformedCtrlPoints = mCtrlPoints;
    unk144 = true;
    unk145 = true;
}

void RndSpline::SyncDeformedDummyCtrlPoints(int startIdx, int endIdx) const {
    int size = mDeformedCtrlPoints.size();
    MILO_ASSERT_RANGE(startIdx, 0, size, 0x2C5);
    MILO_ASSERT_RANGE(endIdx, 0, size, 0x2C6);
    MILO_ASSERT(startIdx <= endIdx, 0x2C7);
    if (size > 1) {
        if (unk144 && startIdx == 0) {
            unk144 = false;
            const CtrlPoint &p0 = mDeformedCtrlPoints[0];
            const CtrlPoint &p1 = mDeformedCtrlPoints[1];
            mDummyBefore.mPos.x = p0.mPos.x + (p0.mPos.x - p1.mPos.x);
            mDummyBefore.mPos.y = p0.mPos.y + (p0.mPos.y - p1.mPos.y);
            mDummyBefore.mPos.z = p0.mPos.z + (p0.mPos.z - p1.mPos.z);
            mDummyBefore.mRoll = p0.mRoll;
            mDeformedCtrlPoints[0].mDirtyConstants = true;
        }
        int numPts = mDeformedCtrlPoints.size();
        if (unk145 && endIdx >= numPts - 2) {
            unk145 = false;
            const CtrlPoint &pLast = mDeformedCtrlPoints[numPts - 1];
            const CtrlPoint &pPrev = mDeformedCtrlPoints[numPts - 2];
            mDummyAfter.mPos.x = pLast.mPos.x + (pLast.mPos.x - pPrev.mPos.x);
            mDummyAfter.mPos.y = pLast.mPos.y + (pLast.mPos.y - pPrev.mPos.y);
            mDummyAfter.mPos.z = pLast.mPos.z + (pLast.mPos.z - pPrev.mPos.z);
            mDummyAfter.mRoll = pLast.mRoll;
            mDummyAfterEnd.mPos.x = mDummyAfter.mPos.x + (mDummyAfter.mPos.x - pLast.mPos.x);
            mDummyAfterEnd.mPos.y = mDummyAfter.mPos.y + (mDummyAfter.mPos.y - pLast.mPos.y);
            mDummyAfterEnd.mPos.z = mDummyAfter.mPos.z + (mDummyAfter.mPos.z - pLast.mPos.z);
            mDummyAfterEnd.mRoll = mDummyAfter.mRoll;
            mDeformedCtrlPoints[numPts - 2].mDirtyConstants = true;
            mDeformedCtrlPoints[numPts - 1].mDirtyConstants = true;
        }
    }
}

void RndSpline::SyncDeformedCtrlPoints(int startIdx, int endIdx) const {
    int size = mDeformedCtrlPoints.size();
    MILO_ASSERT_RANGE(startIdx, 0, size, 0x278);
    MILO_ASSERT_RANGE(endIdx, 0, size, 0x279);
    MILO_ASSERT(startIdx <= endIdx, 0x27A);
    if (size > 1) {
        SyncDeformedDummyCtrlPoints(startIdx, endIdx);
        for (int i = startIdx; i <= endIdx; i++) {
            CtrlPoint &pt = mDeformedCtrlPoints[i];
            if (pt.mDirtyConstants) {
                pt.mDirtyConstants = false;
                const CtrlPoint &prev = GetDeformedCtrlPointOrDummy(i - 1);
                const CtrlPoint &next = GetDeformedCtrlPointOrDummy(i + 1);
                const CtrlPoint &nextNext = GetDeformedCtrlPointOrDummy(i + 2);
                float px = prev.mPos.x, py = prev.mPos.y, pz = prev.mPos.z, pr = prev.mRoll;
                float cx = pt.mPos.x, cy = pt.mPos.y, cz = pt.mPos.z, cr = pt.mRoll;
                float nx = next.mPos.x, ny = next.mPos.y, nz = next.mPos.z, nr = next.mRoll;
                float nnx = nextNext.mPos.x, nny = nextNext.mPos.y, nnz = nextNext.mPos.z,
                      nnr = nextNext.mRoll;
                pt.mCoeff0.Set(-0.5f * px + 1.5f * cx - 1.5f * nx + 0.5f * nnx,
                    -0.5f * py + 1.5f * cy - 1.5f * ny + 0.5f * nny,
                    -0.5f * pz + 1.5f * cz - 1.5f * nz + 0.5f * nnz,
                    -0.5f * pr + 1.5f * cr - 1.5f * nr + 0.5f * nnr);
                pt.mCoeff1.Set(px - 2.5f * cx + 2.0f * nx - 0.5f * nnx,
                    py - 2.5f * cy + 2.0f * ny - 0.5f * nny,
                    pz - 2.5f * cz + 2.0f * nz - 0.5f * nnz,
                    pr - 2.5f * cr + 2.0f * nr - 0.5f * nnr);
                pt.mCoeff2.Set(-0.5f * px + 0.5f * nx, -0.5f * py + 0.5f * ny,
                    -0.5f * pz + 0.5f * nz, -0.5f * pr + 0.5f * nr);
                pt.mCoeff3.Set(cx, cy, cz, cr);
            }
        }
    }
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
