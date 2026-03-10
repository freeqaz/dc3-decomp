#include "hamobj/HamRibbon.h"
#include "math/Mtx.h"
#include "math/Rot.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os/File.h"
#include "os/System.h"
#include "rndobj/Draw.h"
#include "rndobj/Mesh.h"
#include "rndobj/Poll.h"
#include "rndobj/Trans.h"
#include "utl/Loader.h"

HamRibbon::HamRibbon()
    : mLastTime(-1), mNumSides(4), mMat(this), mWidth(1), mDirtyFlags(1), mActive(1),
      mSegTrans(this), mNumSegments(0), mDecay(1), mFollowA(this), mFollowB(this),
      mFollowWeight(0), mTaper(0) {
    mMesh = Hmx::Object::New<RndMesh>();
    mCreateTrans = false;
}

HamRibbon::~HamRibbon() { RELEASE(mMesh); }

BEGIN_HANDLERS(HamRibbon)
    HANDLE_ACTION(expose_mesh, ExposeMesh())
    HANDLE_ACTION(create_transs, mCreateTrans = true)
    HANDLE_ACTION(reset, Reset())
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(HamRibbon)
    SYNC_PROP_SET(active, mActive, SetActive(_val.Int()))
    SYNC_PROP_MODIFY(num_sides, mNumSides, mDirtyFlags |= 1)
    SYNC_PROP_MODIFY(num_segments, mNumSegments, mDirtyFlags |= 1)
    SYNC_PROP_MODIFY(mat, mMat, mMesh->SetMat(mMat))
    SYNC_PROP_MODIFY(width, mWidth, mDirtyFlags |= 2)
    SYNC_PROP(follow_a, mFollowA)
    SYNC_PROP(follow_b, mFollowB)
    SYNC_PROP(follow_weight, mFollowWeight)
    SYNC_PROP(taper, mTaper)
    SYNC_PROP(decay, mDecay)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(RndPollable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(HamRibbon)
    SAVE_REVS(1, 0)
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

INIT_REVS(1, 0)

BEGIN_LOADS(HamRibbon)
    LOAD_REVS(bs)
    ASSERT_REVS(1, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    LOAD_SUPERCLASS(RndDrawable)
    d >> mNumSides;
    d >> mMat;
    d >> mActive;
    d >> mWidth;
    d >> mNumSegments;
    d >> mFollowA;
    d >> mFollowB;
    d >> mFollowWeight;
    d >> mTaper;
    if (d.rev > 0) {
        d >> mDecay;
    }
    mMesh->SetMat(mMat);
    ConstructMesh();
    mDirtyFlags = 0;
END_LOADS

BEGIN_COPYS(HamRibbon)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndDrawable)
    CREATE_COPY(HamRibbon)
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

void HamRibbon::Poll() {
    if (mDirtyFlags & 1) {
        ConstructMesh();
        mDirtyFlags = 0;
    }
    UpdateChase();
    mDirtyFlags = 0;
}

void HamRibbon::DrawShowing() {
    if (mActive || TheLoadMgr.EditMode()) {
        mMesh->DrawShowing();
    }
}

void HamRibbon::Reset() { mChaseKeys.clear(); }

void HamRibbon::ExposeMesh() {
    if (!mMesh->Dir()) {
        mMesh->SetName(MakeString("%s_mesh.mesh", FileGetBase(mMesh->Name())), Dir());
    }
}

void HamRibbon::SetActive(bool active) {
    if (mActive != active) {
        mChaseKeys.clear();
        mLastTime = -1;
    }
    mActive = active;
}

#pragma fp_contract(off)
void HamRibbon::UpdateChase() {
    if (!mFollowA) {
        return;
    }

    float now = TheTaskMgr.Seconds(TaskMgr::kRealTime);
    auto& _ref1 = mLastTime;
    if (now < _ref1) {
        auto _tmp1 = mChaseKeys.begin();
        mChaseKeys.erase(_tmp1, mChaseKeys.end());
    }

    int added = 0;
    if (mActive) {
        Vector3 followed = mFollowA->WorldXfm().v;
        if (mFollowB) {
            Interp(followed, mFollowB->WorldXfm().v, mFollowWeight, followed);
        }

        unsigned int numKeys = mChaseKeys.size();
        unsigned int removeCount = 0;
        if (numKeys != 0) {
            float cutoff = now - mDecay;
            while (removeCount < numKeys) {
                if (mChaseKeys[removeCount].frame >= cutoff) {
                    break;
                }
                removeCount++;
            }
        }

        Key<Transform> key;
        key.frame = 0.0f;
#ifdef HX_NATIVE
        // Native: copy elements down first, then resize.
        // STLport doesn't bounds-check operator[], libstdc++ does —
        // the original code accesses past-end-of-vector after resize.
        key.value = Transform::IDXfm();
        if (removeCount > 0 && removeCount < numKeys) {
            for (int i = 0; i < numKeys - removeCount; ++i) {
                mChaseKeys[i] = mChaseKeys[i + removeCount];
            }
        }
        mChaseKeys.resize(numKeys - removeCount, key);
#else
        if (removeCount < numKeys) {
            for (unsigned int i = removeCount; i < mChaseKeys.size(); i++) {
                mChaseKeys[i - removeCount] = mChaseKeys[i];
            }
        }
        mChaseKeys.resize(numKeys - removeCount, key);
        key.value = Transform::IDXfm();
        key.frame = 0.0f;
#endif
        if (mChaseKeys.size() == 0) {
            key.value.v = followed;
            key.frame = now;
            mChaseKeys.push_back(key);
        } else {
            float step = mDecay / mNumSegments;
            float minDistSq = mWidth * mWidth * 0.125f;
            float nextTime = mChaseKeys.back().frame + step;
            while (now > nextTime) {
                key.frame = mChaseKeys.back().frame + step;
                Interp(
                    mChaseKeys.back().value.v,
                    followed,
                    step / (now - mChaseKeys.back().frame),
                    key.value.v
                );
                Vector3 delta;
                Subtract(key.value.v, mChaseKeys.back().value.v, delta);
                if (minDistSq < LengthSquared(delta)) {
                    mChaseKeys.push_back(key);
                    added++;
                } else {
                    mChaseKeys.back().frame = key.frame;
                }
                nextTime = mChaseKeys.back().frame + step;
            }
        }
    }

    int firstDirty = mChaseKeys.size() - added;
    if (firstDirty < mChaseKeys.size()) {
        float prevAngle = -1.0f;
        for (int i = firstDirty; i < mChaseKeys.size(); ++i) {
            if (i != 0) {
                Vector3 dir;
                Subtract(mChaseKeys[i].value.v, mChaseKeys[i - 1].value.v, dir);
                Normalize(dir, dir);

                Vector3 smoothDir = dir;
                float angle = -1.0f;
                if (2 < i) {
                    Vector3 prevDir;
                    Subtract(
                        mChaseKeys[i - 1].value.v, mChaseKeys[i - 2].value.v, prevDir
                    );
                    float dot = Clamp(0.0f, 1.0f, Dot(prevDir, dir));
                    angle = std::acos(dot);
                    prevDir *= prevAngle;
                    Interp(dir, prevDir, 0.5f, smoothDir);
                    Normalize(smoothDir, smoothDir);
                }

                static Vector3 up(0.0f, 0.0f, 1.0f);
                Transform invPrev;
                Invert(mChaseKeys[i - 1].value, invPrev);
                Vector3 localPos;
                Multiply(mChaseKeys[i].value.v, invPrev, localPos);
                Transform tf = Transform::IDXfm();
                tf.LookAt(localPos, up);
                Multiply(tf, mChaseKeys[i - 1].value.m, tf);
                Normalize(tf.m, tf.m);
                tf.v = mChaseKeys[i].value.v;

                if (angle != -1.0f) {
                    Hmx::Matrix3 inv;
                    Invert(tf.m, inv);
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
                    Multiply(bend, tf.m, tf.m);
                }

                mChaseKeys[i].value = tf;
                prevAngle = angle;
            }
        }
    }

    UpdateMesh();
    _ref1 = now;
}

void HamRibbon::UpdateMesh() {
    auto& _ref0 = mChaseKeys;
    int histSize = _ref0.size();
    if (histSize == 0) {
        return;
    }

    float endTime = _ref0.back().frame;
    Keys<Transform, Transform>::iterator keyIt = _ref0.begin();
    int i = 0;
    for (ObjPtrList<RndTransformable>::iterator it = mSegTrans.begin();
         it != mSegTrans.end() && i < mNumSegments;
         ++it, ++i) {
        MILO_ASSERT(histSize > 0, 0x12A);

        Transform tf = keyIt->value;
        if (mTaper) {
            Hmx::Matrix3 taper;
            float scale = 1.0f - (endTime - keyIt->frame) / mDecay;
            taper.Set(scale, 0.0f, 0.0f, 0.0f, scale, 0.0f, 0.0f, 0.0f, scale);
            Multiply(taper, tf.m, tf.m);
        }

        RndTransformable *trans = *it;
        trans->SetLocalXfm(tf);
        ++keyIt;
        if (i + 1 >= histSize) {
            keyIt = _ref0.begin() + histSize - 1;
        }
    }
}

void HamRibbon::ConstructMesh() {
    mSegTrans.DeleteAll();
    if (0 < mNumSegments) {
        mMesh->SetLocalXfm(Transform::IDXfm());
        mMesh->SetNumBones(mNumSegments);
        for (int i = 0; i < mNumSegments; i++) {
            RndTransformable *t = Hmx::Object::New<RndTransformable>();
            t->SetLocalXfm(Transform::IDXfm());
            mMesh->SetBone(i, t, true);
            mSegTrans.push_back(t);
        }
        mMesh->Verts().resize(mNumSides * mNumSegments * 2);
        mMesh->Faces().resize(mNumSides * mNumSegments * 2);

        for (int seg = 0; seg < mNumSegments; seg++) {
            for (int side = 0; side < mNumSides; side++) {
                int nextSide = (side + 1) % mNumSides;
                int base = seg * mNumSides * 2;

                int faceIdx = (seg * mNumSides + side) * 2;
                mMesh->Faces()[faceIdx].v1 = base + side;
                mMesh->Faces()[faceIdx].v2 = base + nextSide;
                mMesh->Faces()[faceIdx].v3 = base + mNumSides + nextSide;

                int faceIdx1 = faceIdx + 1;
                mMesh->Faces()[faceIdx1].v1 = base + mNumSides + nextSide;
                mMesh->Faces()[faceIdx1].v2 = base + mNumSides + side;
                mMesh->Faces()[faceIdx1].v3 = base + side;
            }
        }

        float angleStep = 6.2831855f / mNumSides;
        float radius = mWidth * 0.5f;
        float uStep = 1.0f / mNumSides;
        Vector3 zeroVec(0.0f, 0.0f, 0.0f);

        for (int seg = 0; seg < mNumSegments; seg++) {
            for (int side = 0; side < mNumSides; side++) {
                float angle = side * angleStep;
                float u = side * uStep;
                Vector3 norm;

                float cosA = std::cos(angle);
                float sinA = std::sin(angle);

                for (int v = 0; v < 2; v++) {
                    int vertIdx = seg * mNumSides * 2 + v * mNumSides + side;

                    Transform xfm = Transform::IDXfm();

                    float scale = (v == 0) ? 1.0f : (0.5f - u);
                    Vector3 pos(sinA * radius * scale, 0.0f, cosA * radius * scale);
                    Multiply(pos, xfm, pos);

                    mMesh->Verts()[vertIdx].pos = pos;

                    if (v == 0) {
                        Subtract(pos, zeroVec, norm);
                        Normalize(norm, norm);
                    }
                    mMesh->Verts()[vertIdx].norm = norm;

                    int boneIdx = mMesh->NumBones() - 1;
                    if (seg + v <= boneIdx) {
                        boneIdx = seg + v;
                    }
                    mMesh->Verts()[vertIdx].boneIndices[0] = (short)boneIdx;
                    mMesh->Verts()[vertIdx].tex.x = (float)boneIdx / (float)mNumSegments;
                    mMesh->Verts()[vertIdx].tex.y = u;
                }
            }
        }

        mMesh->Sync(0x3f);
    }
}
