#include "rndobj\Ribbon.h"
#include "math\Mtx.h"
#include "obj/Object.h"
#include "os\File.h"
#include "obj/Task.h"
#include "rndobj\Draw.h"
#include "rndobj\Mesh.h"
#include "rndobj\Poll.h"
#include "rndobj\Trans.h"
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

/** w16-a (75.27 -> 79.8 canonical): written as the plain code it is -- a
 *  faces.size() erase/insert (as BuildFromBSP has), plain for loops over seg
 *  and side, and two Face::Set calls per quad re-reading mMesh->Faces() -- in
 *  place of the hand-strength-reduced byte-offset version, whose int-cast
 *  pointer arithmetic needed an HX_NATIVE shadow body (now gone).  Same
 *  faces: (v0, v1, v1+ns) and (v1+ns, v0+ns, v0) with v1 = base + (side+1)%ns.
 *  Left: the w7-aj register cascade -- the image keeps `1 - base` (from which
 *  MSVC derives side+1 off the strength-reduced vertex index) in callee-saved
 *  r30 (__savegprlr_26 vs our _27), and schedules all four u16 truncations
 *  before the first triple's stores.  Holding the four indices in
 *  `unsigned short` locals first is byte-identical to this. */
void RndRibbon::ConstructMesh() {
    if (mNumSegments <= 0)
        return;

    mMesh->Verts().resize(mNumSides * mNumSegments * 2);

    RndMesh::Face emptyFace;
    std::vector<RndMesh::Face> &faces = mMesh->Faces();
    unsigned int targetFaceCount = (unsigned int)(mNumSegments * mNumSides) * 2;
    if (targetFaceCount < faces.size()) {
        faces.erase(faces.begin() + targetFaceCount, faces.end());
    } else {
        faces.insert(faces.end(), targetFaceCount - faces.size(), emptyFace);
    }

    for (int seg = 0; seg < mNumSegments; seg++) {
        int base = mNumSides * seg * 2;
        for (int side = 0; side < mNumSides; side++) {
            int v0 = base + side;
            int v1 = base + (side + 1) % mNumSides;
            int v2 = v1 + mNumSides;
            int v3 = v0 + mNumSides;
            mMesh->Faces()[base + side * 2].Set(v0, v1, v2);
            mMesh->Faces()[base + side * 2 + 1].Set(v2, v3, v0);
        }
    }

    mMesh->Sync(0x3f);
}

void RndRibbon::UpdateMesh() {
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
                        // NOT a named Vert reference: the image re-reads the
                        // vertex array's data pointer (0x100) and re-adds the
                        // byte offset before each of the three field writes,
                        // where a named reference would compute the element
                        // address once and keep it in a callee-saved register.
                        verts[vertIdx].pos = pos;
                        if (row == 0) {
                            norm.x = pos.x - xfm->v.x;
                            norm.y = pos.y - xfm->v.y;
                            norm.z = pos.z - xfm->v.z;
                            Normalize(norm, norm);
                        }
                        row++;
                        verts[vertIdx].norm = norm;
                        // The tex pair is written through a member call on the
                        // sub-object: the image materialises &verts[i].tex once
                        // (addi r10, r11, 0x40, then folds both stores back onto
                        // r11+0x40/0x44), where two separate field assignments
                        // re-read the array's data pointer for the second store.
                        verts[vertIdx].tex.Set(
                            1.0f - (latestFrame - segFrame) / mDecay, uFrac
                        );
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

#pragma fp_contract(off)
// w20-t (99.8, branch-scan row 87 adjudicated ARTIFACT): the back-edge lands on
// the image's in-loop `lwz r8, 0x0(r31)` reload of mBegin (see w7-j below);
// the search loop stores nothing, so the hoisted value is the same.
void RndRibbon::UpdateChase() {
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
        unsigned int i = 0;
        if (numKeys != 0) {
            float cutoff = now - mDecay;
            // 15-row residual (lane w7-j, 2026-09-14).  The one structural row is
            // a target-only `lwz r8, 0x0(r31)` at the top of this loop body: the
            // shipped build RE-READS mTransforms.mBegin through r31 (= &mTransforms)
            // on every iteration, where we CSE it with the mBegin load that
            // `mTransforms.size()` already did two instructions earlier.  Everything
            // else in rows 67..95 is the register cascade that follows from having
            // one extra value live across the loop.
            //
            // MEASURED AND REVERTED: indexing through a container pointer --
            //     Keys<Transform, Transform> *xf = &mTransforms;  ... (*xf)[i].frame
            // -- does produce the target's `lwz r8, 0x0(r31)` / `add r8, r10, r8`
            // addressing AND closes the entire cascade (rows 67..78 all become
            // equal, 15 rows -> 10).  But MSVC still LICM-hoists the load into the
            // loop preheader instead of leaving it in the body, so the one row
            // becomes an insert/delete PAIR, which the canonical ruler charges more
            // than the diff_arg rows it removed: 99.757 -> 99.032.  The remaining
            // gap is loop-invariant code motion, not the addressing form, and no
            // behaviour-preserving spelling found here defeats it.
            // w16-a: the plain loop `for (; i < numKeys; i++) { if (...) break;
            // removeCount++; }` inside the guard is WORSE (99.757 -> 99.5): it
            // keeps a separate size test ahead of the loop and the mBegin load
            // still CSEs with size()'s.
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
            key.frame = now;
            key.value.v = followed;
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
                Key<Transform> &last = mTransforms.back();
                Interp(
                    last.value.v,
                    followed,
                    step / (now - mTransforms.back().frame),
                    key.value.v
                );
                Vector3 delta;
                Subtract(last.value.v, key.value.v, delta);
                if (LengthSquared(delta) < minDistSq) {
                    last.frame = key.frame;
                } else {
                    mTransforms.push_back(key);
                    added++;
                }
                nextTime = mTransforms.back().frame + step;
            }
        }
    }

    // smoothDir is declared outside the loop: the shipped build keeps its three
    // components in callee-saved FPRs across iterations (the preheader loads their
    // stack homes before anything writes them). Only read when the `2 < i` branch
    // below wrote it in the same iteration, since `angle` is reset to -1 every
    // iteration and gates both.
    Vector3 smoothDir;
    for (int i = mTransforms.size() - added; i < mTransforms.size(); ++i) {
        if (i != 0) {
            Key<Transform> &cur = mTransforms[i];
            Key<Transform> &prev = mTransforms[i - 1];
            Vector3 dir;
            Subtract(cur.value.v, prev.value.v, dir);
            Normalize(dir, dir);

            float angle = -1.0f;
            if (2 < i) {
                Vector3 prevDir;
                Subtract(prev.value.v, (&cur)[-2].value.v, prevDir);
                float dot = Clamp(0.0f, 1.0f, Dot(prevDir, dir));
                angle = std::acos(dot);
                Vector3 scaledPrev = prevDir;
                scaledPrev *= -1.0f;
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
                Multiply(smoothDir, inv, smoothDir);
                // The clamp result is written back into smoothDir.x: the shipped
                // build defers the store of Multiply()'s x output until after the
                // two fsels and then stores the clamped value to smoothDir's home.
                smoothDir.x = Clamp(0.0f, 1.0f, smoothDir.x);
                float a = std::acos(smoothDir.x);
                float cosHalf = std::cos(angle * 0.5f);
                float invCos = 1.0f / cosHalf;
                float c = std::cos(a * 2.0f);
                float s = std::sin(a * 2.0f);
                // The bend is a rotation in the x-z plane, not x-y: the shipped
                // build stores the two computed terms at m.x.x / m.z.z and the
                // shared off-diagonal at m.x.z / m.z.x (stack 0x120/0x148 and
                // 0x128/0x140), with the identity row in y.  Cross-checked against
                // rb3-xenon's RndRibbon::UpdateChase, which spells the same shape.
                float offDiag = (s * (1.0f - invCos)) * 0.5f;
                Hmx::Matrix3 bend(
                    ((c + 1.0f) * (invCos - 1.0f)) * 0.5f + 1.0f,
                    0.0f,
                    offDiag,
                    0.0f,
                    1.0f,
                    0.0f,
                    offDiag,
                    0.0f,
                    ((1.0f - c) * (invCos - 1.0f)) * 0.5f + 1.0f
                );
                Multiply(bend, result.m, result.m);
            }

            cur.value.m = result.m;
        }
    }

    UpdateMesh();
    lastTime = now;
}
