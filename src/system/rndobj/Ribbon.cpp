#include "rndobj/Ribbon.h"
#include "math/Mtx.h"
#include "obj/Object.h"
#include "os/File.h"
#include "obj/Task.h"
#include "rndobj/Draw.h"
#include "rndobj/Mesh.h"
#include "rndobj/Poll.h"
#include "rndobj/Trans.h"
#include "utl/Loader.h"
#include <cmath>

RndRibbon::RndRibbon()
    : mLastTime(-1.0f), mNumSides(4), mMat(this), mWidth(1), mDirty(1), mActive(true),
      mNumSegments(0), mDecay(1), mFollowA(this), mFollowB(this), mFollowWeight(0),
      mTaper(0) {
    mMesh = Hmx::Object::New<RndMesh>();
    mMesh->SetMutable(0x1F);
}

RndRibbon::~RndRibbon() { RELEASE(mMesh); }

BEGIN_HANDLERS(RndRibbon)
    HANDLE_ACTION(expose_mesh, ExposeMesh())
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(RndRibbon)
    SYNC_PROP_SET(active, mActive, SetActive(_val.Int()));
    SYNC_PROP_MODIFY(num_sides, mNumSides, mDirty |= 1)
    SYNC_PROP_MODIFY(num_segments, mNumSegments, mDirty |= 1)
    SYNC_PROP_MODIFY(mat, mMat, mMesh->SetMat(mMat))
    SYNC_PROP_MODIFY(width, mWidth, mDirty |= 2)
    SYNC_PROP(follow_a, mFollowA)
    SYNC_PROP(follow_b, mFollowB)
    SYNC_PROP(follow_weight, mFollowWeight)
    SYNC_PROP(taper, mTaper)
    SYNC_PROP(decay, mDecay)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(RndPollable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(RndRibbon)
    SAVE_REVS(0, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndDrawable)
    bs << mNumSides;
    bs << mMat;
    bs << mActive;
    bs << mWidth;
    bs << mNumSegments;
    bs << mFollowA;
    bs << mFollowB;
    bs << mFollowWeight;
    bs << mTaper;
    bs << mDecay;
END_SAVES

BEGIN_COPYS(RndRibbon)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndDrawable)
    CREATE_COPY(RndRibbon)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mNumSides)
        COPY_MEMBER(mMat)
        COPY_MEMBER(mActive)
        COPY_MEMBER(mWidth)
        COPY_MEMBER(mNumSegments)
        COPY_MEMBER(mFollowA)
        COPY_MEMBER(mFollowB)
        COPY_MEMBER(mFollowWeight)
        COPY_MEMBER(mTaper)
        COPY_MEMBER(mDecay)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(0, 0)

BEGIN_LOADS(RndRibbon)
    LOAD_REVS(bs)
    ASSERT_REVS(0, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    LOAD_SUPERCLASS(RndDrawable)
    bs >> mNumSides;
    bs >> mMat;
    d >> mActive;
    bs >> mWidth;
    bs >> mNumSegments;
    bs >> mFollowA;
    bs >> mFollowB;
    bs >> mFollowWeight;
    d >> mTaper;
    bs >> mDecay;
    mDirty = 1;
    mMesh->SetMat(mMat);
END_LOADS

void RndRibbon::Poll() {
    if (mDirty & 1) {
        ConstructMesh();
        mDirty = 0;
    }
    UpdateChase();
    mDirty = 0;
}

void RndRibbon::DrawShowing() {
    if (mActive || TheLoadMgr.EditMode()) {
        mMesh->DrawShowing();
    }
}

void RndRibbon::SetActive(bool b) {
    if (mActive != b) {
        mTransforms.clear();
        mLastTime = -1.0;
    }
    mActive = b;
}

void RndRibbon::ExposeMesh() {
    if (!mMesh->Dir()) {
        const char *base = FileGetBase(Name());
        mMesh->SetName(MakeString("%s_mesh.mesh", base), Dir());
    }
}

void RndRibbon::ConstructMesh() {
    if (mNumSegments <= 0)
        return;

    mMesh->Verts().resize(mNumSides * mNumSegments * 2);

    unsigned int numFacePairs = (unsigned int)(mNumSegments * mNumSides);
    RndMesh::Face emptyFace;
    // Cache face vector reference — target sets up &faces before the if/else
    std::vector<RndMesh::Face> &faces = mMesh->Faces();
    unsigned int targetFaceCount = numFacePairs * 2;
    // Target uses signed (end-begin)/6 via divw, not .size()
#ifdef HX_NATIVE
    unsigned int curFaceCount = faces.size();
#else
    int facesBegin = (int)faces.begin();
    unsigned int curFaceCount = (unsigned int)(((int)faces.end() - facesBegin) / 6);
#endif

    if (targetFaceCount < curFaceCount) {
auto _tmp0 = faces.end();
#ifdef HX_NATIVE
        faces.erase(faces.begin() + targetFaceCount, _tmp0);
#else
        faces.erase(
            (RndMesh::Face *)(facesBegin + (int)targetFaceCount * 6),
            faces.end()
        );
#endif
    } else {
#ifdef HX_NATIVE
        faces.insert(faces.end(), targetFaceCount - curFaceCount, emptyFace);
#else
        // Target re-reads end and recomputes count inside insert branch
        faces.insert(
            faces.end(),
            targetFaceCount - (unsigned int)(((int)faces.end() - facesBegin) / 6),
            emptyFace
        );
#endif
    }

    int seg = 0;
    if (mNumSegments > 0) {
        int numSides = mNumSides;
        do {
            // Target uses 32-bit multiply (mullw), not 64-bit (mulld)
            int baseVert = numSides * seg;
            int baseVert2 = baseVert * 2;
            int side = 0;
            if (numSides > 0) {
                // Target: mulli r9, r3, 6 (baseVert2 * 6 = baseVert * 12)
                int faceOff = baseVert2 * 6;
                int vertIdx = baseVert2;
                do {
                    unsigned int ns = (unsigned int)mNumSides;
                    // Target: subfic r30, r3, 0x1 = 1 - baseVert*2, then add vertIdx
                    int nextVertOff = 1 - baseVert2 + vertIdx;
                    short v0 = (short)vertIdx;
                    short sNumSides = (short)ns;
                    short vNext = (short)((short)nextVertOff - (short)((int)nextVertOff / (int)ns) * sNumSides) + (unsigned short)baseVert2;
                    short vNextWrap = sNumSides + vNext;
#ifdef HX_NATIVE
                    short *facePtr = (short *)((char *)mMesh->Faces().data() + faceOff);
#else
                    short *facePtr = (short *)((int)mMesh->Faces().begin() + faceOff);
#endif
                    side++;
                    facePtr[0] = v0;
                    vertIdx = vertIdx + 1;
                    facePtr[1] = vNext;
                    facePtr[2] = vNextWrap;
#ifdef HX_NATIVE
                    short *faceBase = (short *)((char *)mMesh->Faces().data() + faceOff);
#else
                    int faceBase = (int)mMesh->Faces().begin() + faceOff;
#endif
                    *(short *)(faceBase + 6) = vNextWrap;
                    *(short *)(faceBase + 8) = v0 + sNumSides;
                    faceOff = faceOff + 12;
                    *(short *)(faceBase + 10) = v0;
                    numSides = mNumSides;
                } while (side < numSides);
            }
            seg++;
        } while (seg < mNumSegments);
    }

    mMesh->Sync(0x3f);
}

void RndRibbon::UpdateMesh() {
    // mTransforms is Keys<Transform> (std::vector<Key<Transform>>) at this+0x74
    // Raw pointer access needed: .empty()/.size()/[] generate different codegen than divw
    Keys<Transform, Transform> *keys = &mTransforms;
    int keysBegin = *(int *)((int)keys + 0);
    int keysEnd = *(int *)((int)keys + 4);
    if ((keysEnd - keysBegin) / 0x44 == 0)
        return;

    int numSides = mNumSides;
    // mMesh->mGeomOwner.mObject (offset 0x148 in RndMesh)
    int geomOwner = *(int *)((int)mMesh + 0x148);
    int seg = 0;
    double angleStep = (double)(6.2831855f / (float)(long long)numSides);
    float halfWidth = mWidth * 0.5f;
    float one = 1.0f;
    float normX = 0.0f, normY = 0.0f, normZ = 0.0f;
    if (mNumSegments > 0) {
        // Latest keyframe time = last element's .frame (end - 4 bytes)
        float latestFrame = *(float *)(*(int *)((int)keys + 4) - 4);
        do {
            int side = 0;
            int vertRowBase = mNumSides * seg * 2;
            double vCoord = (double)(float)(one / (double)(long long)mNumSides);
            if (numSides > 0) {
                do {
                    int row = 0;
                    double angle = (double)(float)((double)(long long)side * angleStep);
                    double uFrac = (double)(float)((double)(long long)side * vCoord);
                    do {
                        unsigned int rowSeg = (unsigned int)(row + seg);
                        int begin = *(int *)((int)keys + 0);
                        unsigned int lastIdx =
                            (unsigned int)(*(int *)((int)keys + 4) - begin) / 0x44 - 1;
                        if ((int)rowSeg <= (int)lastIdx) {
                            lastIdx = ~((unsigned int)((int)rowSeg >> 0x1f)) & rowSeg;
                        }
                        // Key<Transform>: .value (Transform, 0x40 bytes) then .frame (float)
                        float segFrame = *(float *)(begin + lastIdx * 0x44 + 0x40);
                        float taperScale = one;
                        if (mTaper) {
                            taperScale = one - (latestFrame - segFrame) / mDecay;
                        }
                        double cosA = (double)(float)cos(angle);
                        double sinA = sin(angle);
                        float posY = 0.0f;
                        Transform *xfm = (Transform *)(lastIdx * 0x44 + begin);
                        float posZ =
                            (float)((double)(float)(cosA * taperScale) * halfWidth);
                        float posX =
                            (float)((double)(float)((float)sinA * taperScale) * halfWidth);
                        Vector3 pos(posX, posY, posZ);
                        Multiply(pos, *xfm, pos);
                        posX = pos.x;
                        posY = pos.y;
                        posZ = pos.z;
                        // Vertex layout: pos(0x0), norm(0x10), tex(0x40), stride 0x60
                        int vertIdx = (mNumSides * row + vertRowBase) * sizeof(RndMesh::Vert);
                        int vertPtr = *(int *)(geomOwner + 0x100) + vertIdx;
                        *(float *)(*(int *)(geomOwner + 0x100) + vertIdx) = posX;
                        *(float *)(vertPtr + 4) = posY;
                        *(float *)(vertPtr + 8) = posZ;
                        if (row == 0) {
                            normX = posX - xfm->v.x;
                            normY = posY - xfm->v.y;
                            normZ = posZ - xfm->v.z;
                            Normalize((Vector3 &)normX, (Vector3 &)normX);
                        }
                        row++;
                        vertPtr = *(int *)(geomOwner + 0x100) + vertIdx;
                        *(float *)(vertPtr + 0x18) = normZ;
                        *(float *)(vertPtr + 0x10) = normX;
                        *(float *)(vertPtr + 0x14) = normY;
                        int uvPtr = *(int *)(geomOwner + 0x100) + vertIdx;
                        *(float *)(uvPtr + 0x40) =
                            (float)(one - (latestFrame - segFrame) / mDecay);
                        *(float *)(uvPtr + 0x44) = (float)uFrac;
                    } while (row < 2);
                    numSides = mNumSides;
                    side++;
                    vertRowBase = vertRowBase + 1;
                } while (side < numSides);
            }
            seg++;
        } while (seg < mNumSegments);
    }
    mMesh->Sync(0x1f);
}

void RndRibbon::UpdateChase() {
    if (!mFollowA)
        return;


    float currentTime = TheTaskMgr.Seconds(TaskMgr::kRealTime);
    auto& _ref1 = mLastTime;

    double dCurrentTime = (double)currentTime;
    if (dCurrentTime < (double)_ref1) {
        auto _tmp2 = mTransforms.end();
        auto _tmp1 = mTransforms.begin();
        if (_tmp1 != _tmp2) {
            mTransforms.erase(_tmp1, _tmp2);
        }
    }
    int newSegCount = 0;

    Vector3 followPos;
    if (mActive) {
        const Transform *xfmA = &mFollowA->WorldXfm();
        float followA_v_y = xfmA->v.y;
        float followA_v_z = xfmA->v.z;
        followPos.x = xfmA->v.x;
        followPos.y = followA_v_y;
        followPos.z = followA_v_z;
        if (mFollowB) {
            const Transform *xfmB = &mFollowB->WorldXfm();
            Interp(followPos, (const Vector3 &)xfmB->v, mFollowWeight, followPos);
        }

        unsigned int numTransforms = (unsigned int)((int)&*mTransforms.end() - (int)&*mTransforms.begin()) / 0x44;
        unsigned int removeCount = 0;
        if (numTransforms != 0) {
            unsigned int k = 0;
            do {
                if ((float)(dCurrentTime - (double)mDecay) <= *(float *)(((int)&*mTransforms.begin() + (k + 0x40))))
                    break;
                removeCount++;
                k += 0x44;
            } while (removeCount < numTransforms);
        }

        if (removeCount < numTransforms) {
            unsigned int srcIdx = removeCount;
            unsigned int dstIdx = 0;
            do {
                memcpy(&mTransforms[dstIdx], &mTransforms[srcIdx], 0x44);
                srcIdx++;
                dstIdx++;
                numTransforms = (unsigned int)((int)&*mTransforms.end() - (int)&*mTransforms.begin()) / 0x44;
            } while (srcIdx < numTransforms);
        }

        Key<Transform> newKey;
        newKey.frame = 0.0f;
        mTransforms.resize(numTransforms - (int)removeCount, newKey);

        numTransforms = (unsigned int)((int)&*mTransforms.end() - (int)&*mTransforms.begin()) / 0x44;
        if (numTransforms == 0) {
            newKey.frame = currentTime;
            newKey.value.v.y = followA_v_y;
            newKey.value.v.z = followA_v_z;
            mTransforms.push_back(newKey);
        } else {
            long long numSegs = (long long)mNumSegments;
            double minDistSq = (double)(mWidth * mWidth * 0.125f);
            double segInterval = (double)(mDecay / (float)numSegs);
            double nextTime = (double)mTransforms.back().frame + segInterval;
            while ((double)(float)nextTime < dCurrentTime) {
                Transform *backXfm = &mTransforms.back().value;
                newKey.frame = (float)((double)mTransforms.back().frame + segInterval);
                Interp((const Vector3 &)backXfm->v, followPos,
                       (float)(segInterval / (double)(float)(dCurrentTime - (double)mTransforms.back().frame)),
                       (Vector3 &)newKey.value.v);
                float dx = backXfm->v.z - followA_v_z;
                float dy = backXfm->v.x - newKey.value.v.x;
                float dz = backXfm->v.y - followA_v_y;
                if (minDistSq <= (double)((dz * dz + (dy * dy + dx * dx)))) {
                    mTransforms.push_back(newKey);
                    newSegCount++;
                } else {
                    mTransforms.back().frame = newKey.frame;
                }
                nextTime = (double)mTransforms.back().frame + segInterval;
            }
        }
    }

    // Orient each transform
    unsigned int numTransforms = (unsigned int)((int)&*mTransforms.end() - (int)&*mTransforms.begin()) / 0x44;
    unsigned int startIdx = (unsigned int)((int)numTransforms - newSegCount);
    if (startIdx < numTransforms) {
        double dSlerpFwdX = 0.0;
        double dSlerpFwdY = 0.0;
        double dSlerpFwdZ = 0.0;
        double dPrevAngle = -1.0;

        static int sUpVecFlag;
        static Vector3 sUpVec;

        unsigned int curIdx = startIdx;
        do {
            double dCurAngle = dPrevAngle;
            if (curIdx != 0) {
                Transform &curXfm = mTransforms[curIdx].value;
                Transform &prevXfm = mTransforms[curIdx - 1].value;
                double cx = (double)curXfm.v.x;
                double cy = (double)curXfm.v.y;
                double cz = (double)curXfm.v.z;
                double fdx = cx - (double)prevXfm.v.x;
                double fdy = cy - (double)prevXfm.v.y;
                double fdz = cz - (double)prevXfm.v.z;
                Vector3 forward;
                forward.x = (float)fdx;
                forward.y = (float)fdy;
                forward.z = (float)fdz;
                Normalize(forward, forward);

                if ((int)curIdx >= 3) {
                    Transform &prevPrevXfm = mTransforms[curIdx - 2].value;
                    double pdx = cx - (double)prevPrevXfm.v.x;
                    double pdy = cy - (double)prevPrevXfm.v.y;
                    double pdz = cz - (double)prevPrevXfm.v.z;
                    double dot = pdy * (double)forward.y + pdx * (double)forward.x + pdz * (double)forward.z;
                    double clampedDot = 0.0;
                    if (-dot < 0.0)
                        clampedDot = dot;
                    double clampedDot2 = 1.0;
                    if ((float)(clampedDot - 1.0) < 0.0f)
                        clampedDot2 = clampedDot;
                    double angle = (double)acos(clampedDot2);
                    dCurAngle = (double)(float)angle;
                    Vector3 newSlerpFwd;
                    Interp(forward, followPos, 0.5f, newSlerpFwd);
                    Normalize(newSlerpFwd, newSlerpFwd);
                    dSlerpFwdX = (double)newSlerpFwd.x;
                    dSlerpFwdY = (double)newSlerpFwd.y;
                    dSlerpFwdZ = (double)newSlerpFwd.z;
                }

                if ((sUpVecFlag & 1) == 0) {
                    sUpVecFlag |= 1;
                    sUpVec.x = 0.0f;
                    sUpVec.y = 0.0f;
                    sUpVec.z = 1.0f;
                }

                Transform invPrev;
                Invert(prevXfm, invPrev);
                Vector3 localVec;
                Multiply(curXfm.v, invPrev, localVec);
                Transform lookAt;
                memcpy(&lookAt, &Transform::IDXfm(), 0x40);
                lookAt.LookAt(localVec, sUpVec);
                Transform result;
                Multiply(lookAt, prevXfm.m, result);
                Normalize(result.m, result.m);

                float sv_y = curXfm.v.y;
                float sv_z = curXfm.v.z;

                if (dCurAngle != dPrevAngle) {
                    // Reuse invPrev.m (dead after Multiply above) to hold inverted result.m
                    Invert(result.m, invPrev.m);
                    double sdotX = (double)(float)((double)invPrev.m.z.x * dSlerpFwdZ +
                                   (double)(float)((double)invPrev.m.x.x * dSlerpFwdX +
                                   (double)(float)((double)invPrev.m.y.x * dSlerpFwdY)));
                    float sdotY_f = (float)((double)invPrev.m.z.y * dSlerpFwdZ +
                                   (double)(float)((double)invPrev.m.x.y * dSlerpFwdX +
                                   (double)(float)(dSlerpFwdY * (double)invPrev.m.y.y)));
                    dSlerpFwdY = (double)sdotY_f;
                    float sdotZ_f = (float)((double)invPrev.m.z.z * dSlerpFwdZ +
                                   (double)(float)((double)invPrev.m.x.z * dSlerpFwdX +
                                   (double)(float)((double)invPrev.m.y.z * dSlerpFwdY)));
                    dSlerpFwdZ = (double)sdotZ_f;

                    double newAcosIn = 0.0;
                    if (-sdotX < 0.0)
                        newAcosIn = sdotX;
                    double newAcosIn2 = 1.0;
                    if ((float)(newAcosIn - 1.0) < 0.0f)
                        newAcosIn2 = newAcosIn;
                    double newSlerpAngle = (double)(float)(double)acos(newAcosIn2);
                    double cosHalfCur = (double)(float)cos((double)(float)(dCurAngle * 0.5));
                    double halfNew2 = (double)(float)(newSlerpAngle * 2.0);
                    double invCosHalfCur = (double)(float)(1.0 / (double)(float)cosHalfCur);
                    double cosHN2 = (double)(float)cos(halfNew2);
                    double sinHN2 = (double)(float)sin(halfNew2);
                    float mXX = (float)((double)((float)(cosHN2 + 1.0) * (float)(invCosHalfCur - 1.0)) * 0.5 + 1.0);
                    float mXZ = (float)((double)((float)sinHN2 * (float)(1.0 - invCosHalfCur)) * 0.5);
                    float mZZ = (float)((double)((float)(1.0 - cosHN2) * (float)(invCosHalfCur - 1.0)) * 0.5 + 1.0);
                    // Reuse lookAt.m (dead after LookAt+Multiply above) for slerp matrix
                    lookAt.m.x.x = mXX; lookAt.m.x.y = 0.0f; lookAt.m.x.z = mXZ;
                    lookAt.m.y.x = 0.0f; lookAt.m.y.y = 1.0f; lookAt.m.y.z = 0.0f;
                    lookAt.m.z.x = mXZ;  lookAt.m.z.y = 0.0f; lookAt.m.z.z = mZZ;
                    Multiply(lookAt.m, result.m, result.m);
                }

                memcpy(&curXfm, &result, 0x30);
                curXfm.v.y = sv_y;
                curXfm.v.z = sv_z;
            }
            curIdx++;
            dPrevAngle = dCurAngle;
        } while (curIdx < (unsigned int)((int)&*mTransforms.end() - (int)&*mTransforms.begin()) / 0x44);
    }

    UpdateMesh();
    _ref1 = currentTime;
}
