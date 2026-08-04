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
#ifndef HX_NATIVE
    if (mNumSegments <= 0)
        return;

    mMesh->Verts().resize(mNumSides * mNumSegments * 2);

    unsigned int numFacePairs = (unsigned int)(mNumSegments * mNumSides);
    RndMesh::Face emptyFace;
    std::vector<RndMesh::Face> &faces = mMesh->Faces();
    unsigned int targetFaceCount = numFacePairs * 2;
    int facesBegin = (int)faces.begin();
    unsigned int curFaceCount = (unsigned int)(((int)faces.end() - facesBegin) / 6);

    if (targetFaceCount < curFaceCount) {
        faces.erase(
            (RndMesh::Face *)(facesBegin + (int)targetFaceCount * 6),
            (RndMesh::Face *)((int)faces.end())
        );
    } else {
        faces.insert(
            faces.end(),
            targetFaceCount - (unsigned int)(((int)faces.end() - facesBegin) / 6),
            emptyFace
        );
    }

    int seg = 0;
    if (mNumSegments > 0) {
        int numSides = mNumSides;
        do {
            int baseVert = numSides * seg;
            int baseVert2 = baseVert * 2;
            int side = 0;
            if (numSides > 0) {
                int faceOff = baseVert2 * 6;
                int vertIdx = baseVert2;
                int oneMinusBV2 = 1 - baseVert2;
                do {
                    int ns = mNumSides;
                    int nextVertOff = oneMinusBV2 + vertIdx;
                    unsigned short v0 = (unsigned short)vertIdx;
                    int rem = nextVertOff % ns;
                    int vNextRaw = rem + baseVert2;
                    int v0PlusNS = vertIdx + ns;
                    int vNextWrapRaw = ns + vNextRaw;
                    short *facePtr = (short *)((int)mMesh->Faces().begin() + faceOff);
                    unsigned short vNextWrap = (unsigned short)vNextWrapRaw;
                    unsigned short vNext = (unsigned short)vNextRaw;
                    unsigned short v0PlusNSu = (unsigned short)v0PlusNS;
                    side++;
                    facePtr[0] = v0;
                    vertIdx = vertIdx + 1;
                    facePtr[1] = vNext;
                    facePtr[2] = vNextWrap;
                    short *faceBase = (short *)((int)mMesh->Faces().begin() + faceOff);
                    *(short *)((int)faceBase + 6) = vNextWrap;
                    *(short *)((int)faceBase + 8) = v0PlusNSu;
                    faceOff = faceOff + 12;
                    *(short *)((int)faceBase + 10) = v0;
                    numSides = mNumSides;
                } while (side < numSides);
            }
            seg++;
        } while (seg < mNumSegments);
    }

    mMesh->Sync(0x3f);
#endif // !HX_NATIVE
}

void RndRibbon::UpdateMesh() {
#ifndef HX_NATIVE
    if (mTransforms.size() == 0)
        return;

    int numSides = mNumSides;
    RndMesh::VertVector &verts = mMesh->Verts();
    int seg = 0;
    float angleStep = 6.2831855f / (float)(long long)numSides;
    float halfWidth = mWidth * 0.5f;
    float latestFrame = mTransforms.back().frame;
    Vector3 norm(0.0f, 0.0f, 0.0f);
    if (mNumSegments > 0) {
        do {
            int side = 0;
            int vertRowBase = mNumSides * seg * 2;
            float vCoord = 1.0f / (float)(long long)mNumSides;
            if (numSides > 0) {
                do {
                    int row = 0;
                    float angle = (float)side * angleStep;
                    float uFrac = (float)side * vCoord;
                    do {
                        unsigned int rowSeg = (unsigned int)(row + seg);
                        int vertIdx = mNumSides * row + vertRowBase;
                        unsigned int lastIdx = mTransforms.size() - 1;
                        if ((int)rowSeg <= (int)lastIdx) {
                            lastIdx = ((rowSeg >> 31) - 1) & rowSeg;
                        }
                        float segFrame = mTransforms[lastIdx].frame;
                        float taperScale;
                        if (mTaper) {
                            taperScale = 1.0f - (latestFrame - segFrame) / mDecay;
                        } else {
                            taperScale = 1.0f;
                        }
                        float cosA = (float)cos((double)angle);
                        float sinA = (float)sin((double)angle);
                        Transform *xfm = &mTransforms[lastIdx].value;
                        float posZ = cosA * taperScale * halfWidth;
                        float posX = sinA * taperScale * halfWidth;
                        Vector3 pos(posX, 0.0f, posZ);
                        Multiply(pos, *xfm, pos);
                        RndMesh::Vert &vert = verts[vertIdx];
                        vert.pos = pos;
                        if (row == 0) {
                            norm.x = pos.x - xfm->v.x;
                            norm.y = pos.y - xfm->v.y;
                            norm.z = pos.z - xfm->v.z;
                            Normalize(norm, norm);
                        }
                        row++;
                        vert.norm = norm;
                        vert.tex.x =
                            1.0f - (latestFrame - segFrame) / mDecay;
                        vert.tex.y = uFrac;
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
#endif // !HX_NATIVE
}

#pragma fp_contract(off)
void RndRibbon::UpdateChase() {
#ifndef HX_NATIVE
    if (!mFollowA) {
        return;
    }

    float now = TheTaskMgr.Seconds(TaskMgr::kRealTime);
    float &lastTime = mLastTime;
    if (now < lastTime) {
        Keys<Transform, Transform>::iterator firstKey = mTransforms.begin();
        mTransforms.erase(firstKey, mTransforms.end());
    }

    int added = 0;
    if (mActive) {
        Vector3 followed = mFollowA->WorldXfm().v;
        if (mFollowB) {
            Interp(followed, mFollowB->WorldXfm().v, mFollowWeight, followed);
        }

        unsigned int numKeys = mTransforms.size();
        unsigned int removeCount = 0;
        if (numKeys != 0) {
            float cutoff = now - mDecay;
            unsigned int i = 0;
            do {
                if (mTransforms[i].frame >= cutoff) {
                    break;
                }
                removeCount++;
                i++;
            } while (i < numKeys);
        }

        Key<Transform> key;
        unsigned int srcIdx = removeCount;
        if (removeCount < numKeys) {
            unsigned int dstIdx = 0;
            do {
                memcpy(&mTransforms[dstIdx], &mTransforms[srcIdx], sizeof(Key<Transform>));
                srcIdx++;
                dstIdx++;
                numKeys = mTransforms.size();
            } while (srcIdx < numKeys);
        }
        key.frame = 0.0f;
        mTransforms.resize(numKeys - removeCount, key);
        key.frame = 0.0f;
        key.value = Transform::IDXfm();
        if (mTransforms.size() == 0) {
            key.value.v = followed;
            key.frame = now;
            mTransforms.push_back(key);
        } else {
            float step = mDecay / mNumSegments;
            float minDistSq = mWidth * mWidth * 0.125f;
            float nextTime = mTransforms.back().frame + step;
            while (now > nextTime) {
                // NOTE: caching `&mTransforms.back()` in a named local reproduces the
                // target's +0x30/+0x40 element-relative offsets but costs a register
                // (85.8% vs 86.7%) — the shipped build reloads mTransforms.mEnd every
                // iteration instead of keeping it live. Do not re-try.
                key.frame = mTransforms.back().frame + step;
                Interp(
                    mTransforms.back().value.v,
                    followed,
                    step / (now - mTransforms.back().frame),
                    key.value.v
                );
                Vector3 delta;
                Subtract(mTransforms.back().value.v, key.value.v, delta);
                if (LengthSquared(delta) < minDistSq) {
                    mTransforms.back().frame = key.frame;
                } else {
                    mTransforms.push_back(key);
                    added++;
                }
                nextTime = mTransforms.back().frame + step;
            }
        }
    }

    int firstDirty = mTransforms.size() - added;
    if (firstDirty < mTransforms.size()) {
        float prevAngle = -1.0f;
        for (int i = firstDirty; i < mTransforms.size(); ++i) {
            if (i != 0) {
                Key<Transform> &cur = mTransforms[i];
                Key<Transform> &prev = mTransforms[i - 1];
                Vector3 dir;
                Subtract(cur.value.v, prev.value.v, dir);
                Normalize(dir, dir);

                Vector3 smoothDir;
                float angle = -1.0f;
                if (2 < i) {
                    Vector3 prevDir;
                    Subtract(prev.value.v, mTransforms[i - 2].value.v, prevDir);
                    float dot = Clamp(0.0f, 1.0f, Dot(prevDir, dir));
                    angle = std::acos(dot);
                    Vector3 scaledPrev = prevDir;
                    scaledPrev *= prevAngle;
                    Interp(dir, scaledPrev, 0.5f, smoothDir);
                    Normalize(smoothDir, smoothDir);
                }

                static Vector3 up(0.0f, 0.0f, 1.0f);
                Transform invPrev;
                Invert(prev.value, invPrev);
                Vector3 localPos;
                Multiply(cur.value.v, invPrev, localPos);
                Transform tf = Transform::IDXfm();
                tf.LookAt(localPos, up);
                Transform result;
                Multiply(tf, prev.value.m, result);
                Normalize(result.m, result.m);
                result.v = cur.value.v;

                if (angle != -1.0f) {
                    Hmx::Matrix3 inv;
                    Invert(result.m, inv);
                    Vector3 localSmooth;
                    Multiply(smoothDir, inv, localSmooth);
                    float clamped = Clamp(0.0f, 1.0f, localSmooth.x);
                    float a = std::acos(clamped);
                    float cosHalf = std::cos(angle * 0.5f);
                    float invCos = 1.0f / cosHalf;
                    float c = std::cos(a * 2.0f);
                    float s = std::sin(a * 2.0f);
                    Hmx::Matrix3 bend(
                        ((c + 1.0f) * (invCos - 1.0f)) * 0.5f + 1.0f,
                        (s * (1.0f - invCos)) * 0.5f,
                        0.0f,
                        (s * (1.0f - invCos)) * 0.5f,
                        ((1.0f - c) * (invCos - 1.0f)) * 0.5f + 1.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f
                    );
                    Multiply(bend, result.m, result.m);
                }

                cur.value.m = result.m;
                prevAngle = angle;
            }
        }
    }

    UpdateMesh();
    lastTime = now;
#endif // !HX_NATIVE
}
