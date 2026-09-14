#include "gesture\ArcDetector.h"
#include "gesture\BaseSkeleton.h"
#include "gesture\GestureMgr.h"
#include "gesture\SkeletonViz.h"
#include "os\Debug.h"
#include "rndobj\Rnd.h"
#include "rndobj\Utl.h"
#include "utl\DebugMeter.h"
#include "utl\Std.h"

static int sDefaultHoverTimer = 600;
static float _acceptablePathErrorRatio = 0.89999998f;
static float _swipeRetentionFactor = 0.5f;
static float sZErrorScale = 2.0f;

ArcDetector::ArcDetector()
    : mArcOffset(0, 0, 0), mSwipeExtentX(0), mSwipeExtentY(0), mSwipeThreshold(0.15f), mInitialized(0), mHadProgress(0),
      mHoverTimer(sDefaultHoverTimer) {
    Clear();
}

ArcDetector::~ArcDetector() {}

void ArcDetector::ResetHoverTimer() { mHoverTimer = sDefaultHoverTimer; }

void ArcDetector::Initialize(
    SkeletonSide side, SkeletonJoint j1, SkeletonJoint j2, float f4
) {
    mSwipeThreshold = f4;
    mSide = side;
    mInitialized = true;
    mPrimaryJoint = j1;
    mSecondaryJoint = j2;
}

Vector3 ArcDetector::GetCurveStart() const {
    MILO_ASSERT(!mJointPath.empty(), 0xE9);
    return Vector3((mSide == kSkeletonLeft ? 1 : -1) * mSwipeExtentX, mSwipeExtentY, 0.0f);
}

void ArcDetector::Clear() {
    mCurrentSwipeAmt = 0;
    mJointPath.clear();
    mSwipeExtentY = 0;
    mSwipeExtentX = 0;
}

void ArcDetector::PrintJointPath() const {
    MILO_LOG("*** Hand path:\n");
    for (std::list<Vector3>::const_reverse_iterator it = mJointPath.rbegin();
         it != mJointPath.rend(); ++it) {
        const Vector3 &pos = *it;
        MILO_LOG("%f, %f, %f,\n", pos.x, pos.y, pos.z);
    }
    MILO_LOG("GetPathLength() %f\n", GetPathLength());
    MILO_LOG(
        "pow(GetPathLength(), _swipeRetentionFactor + 1) %f\n",
        (float)pow(GetPathLength(), _swipeRetentionFactor + 1.0f)
    );
    MILO_LOG("GetPathError() %f\n", GetPathError());
    MILO_LOG(
        "GetPathError() / _acceptablePathErrorRatio %f\n",
        GetPathError() / _acceptablePathErrorRatio
    );
    float threshold = mSwipeThreshold * 0.3f;
    MILO_LOG(
        "desiredLength %f\n",
        (float)mHoverTimer / (float)sDefaultHoverTimer * (mSwipeThreshold - threshold) + threshold
    );
    MILO_LOG("GetSwipeAmount() %f\n", GetSwipeAmount());
}

void ArcDetector::SwipeFailed(const Skeleton &skeleton) {
    if (mCurrentSwipeAmt > 0.5)
        mHadProgress = true;
    Vector3 vec = mJointPath.front();
    Clear();
    TryToStartSwipe(vec, skeleton);
}

void ArcDetector::CullPath() {
    if (!mJointPath.empty()) {
        std::list<Vector3> other;
        float first = mJointPath.front().x;
        FOREACH (it, mJointPath) {
            const Vector3 &cur = *it;
            if (mSide == kSkeletonLeft && cur.x >= first) {
                other.push_back(cur);
            }
            if (mSide == kSkeletonRight && cur.x <= first) {
                other.push_back(cur);
            }
        }
        mJointPath = other;
    }
}

void ArcDetector::Draw(const Skeleton &skeleton, SkeletonViz &viz) {
    unsigned int count = mJointPath.size();
    if (count != 0U) {
        {
            // arcPath is block-scoped in the image: there is exactly ONE
            // _List_base::clear (0x1c0, between the two DrawPath calls) and none
            // at the epilogue, so the call we had been spelling as an explicit
            // arcPath.clear() is really the destructor at end of scope.
            std::list<Vector3> arcPath;
            for (int i = 0; i < 100; i++) {
                float f0 = (float)i * mSwipeExtentX * 0.02f;
                int sign = (mSide != 0 ? 1 : -1);
                float comp = mSwipeExtentX * f0 * 2.0f - (f0 * f0);
                float arcY;
                if (comp <= 0.0f) {
                    arcY = 0.0f;
                } else {
                    arcY = sqrtf(comp);
                }
                arcY = -arcY;
                Vector3 vec(f0 * (float)sign, 0.0f, arcY);
                arcPath.insert(arcPath.begin(), vec);
            }

            const Vector3 &jpos =
                skeleton.TrackedJoints()[mSecondaryJoint].mJointPos[0];
            const Vector3 &off = mArcOffset;
            Vector3 pos(jpos.x + off.x, jpos.y + off.y, jpos.z + off.z);

            DrawPath(arcPath, viz, Hmx::Color(1.0f, 1.0f, 0.0f, 1.0f), pos);
        }

        DrawPath(
            mJointPath,
            viz,
            Hmx::Color(1.0f, 0.0f, 1.0f, 1.0f),
            skeleton.TrackedJoints()[mSecondaryJoint].mJointPos[0]
        );
    }
}

void ArcDetector::DrawPath(
    std::list<Vector3> path, SkeletonViz &viz, Hmx::Color color, const Vector3 &offset
) const {
    std::list<Vector3>::const_iterator it = path.begin();
    Vector3 prev = *it;
    prev.z = prev.z + offset.z;
    prev.y = prev.y + offset.y;
    prev.x = prev.x + offset.x;
    Vector3 cur;
    for (++it; it != path.end(); ++it) {
        cur = *it;
        cur.x = cur.x + offset.x;
        cur.y = cur.y + offset.y;
        cur.z = cur.z + offset.z;
        viz.DrawLine3D(prev, cur, 0.01f, color, NULL);
        prev = cur;
    }
}

float ArcDetector::GetPathLength() const {
    if (mJointPath.size() <= 1) {
        return 0.0f;
    }
    std::list<Vector3>::const_iterator it = mJointPath.begin();
    float length = 0.0f;
    Vector3 prev = *it;
    ++it;
    if (it != mJointPath.end()) {
        do {
            float dx = mArcOffset.x - it->x;
            if (mSide == kSkeletonRight) {
                dx = dx * -1.0f;
            }
            float prevDx = mArcOffset.x - prev.x;
            if (mSide == kSkeletonRight) {
                prevDx = prevDx * -1.0f;
            }
            if (dx > 0.0f && prevDx > 0.0f) {
                float comp1 = mSwipeExtentX * prevDx * 2.0f - prevDx * prevDx;
                float arcY1;
                if (!(comp1 > 0.0f)) {
                    arcY1 = 0.0f;
                } else {
                    arcY1 = sqrtf(comp1);
                }
                Vector3 p1(prevDx, arcY1, mSwipeExtentY);
                float comp2 = mSwipeExtentX * dx * 2.0f - dx * dx;
                float arcY2;
                if (!(comp2 > 0.0f)) {
                    arcY2 = 0.0f;
                } else {
                    arcY2 = sqrtf(comp2);
                }
                Vector3 p2(dx, arcY2, mSwipeExtentY);
                float ex = p2.x - p1.x;
                float ey = p2.y - p1.y;
                float ez = p2.z - p1.z;
                length = sqrtf(ez * ez + ey * ey + ex * ex) + length;
            }
            prev = *it;
            ++it;
        } while (it != mJointPath.end());
    }
    return length;
}

float ArcDetector::GetPathError() const {
    std::list<Vector3>::const_iterator it = mJointPath.begin();
    std::list<Vector3>::const_iterator pathEnd = mJointPath.end();
    if (it == pathEnd) {
        return 0.0f;
    }
    float error = 0.0f;
    do {
        Vector3 pt = *it;
        float dx = mArcOffset.x - pt.x;
        if (mSide == kSkeletonRight) {
            dx = dx * -1.0f;
        }
        float arcZBase = mArcOffset.z - pt.z;
        float comp = mSwipeExtentX * dx * 2.0f - dx * dx;
        float arcY;
        if (!(comp > 0.0f)) {
            arcY = 0.0f;
        } else {
            arcY = sqrtf(comp);
        }
        float errY = arcZBase - arcY;
        float errZ = (1.0f / sZErrorScale) * (pt.y - mSwipeExtentY);
        float dz = 0.0f;
        // RESIDUAL (w7-az, 93.55 canonical, 3 rows): 0x82E00B98 spends a DEAD
        // `fmr f4, f13` (f4 = 0.0f) that `fmuls f4, f8, f3` overwrites two
        // instructions later, and schedules `cmplw cr6, r11, r10` three
        // instructions later than we do.  Writing errZ as `float errZ = 0.0f;`
        // followed by the assignment is byte-inert -- MSVC deletes the dead
        // store -- so the zero comes from somewhere else in the original.
        // Also charged under name_check: the image loads sZErrorScale as
        // `lbl_82F446F8`, a .data float (value 2.0f, verified) that dtk
        // attributes to the StandingStillGestureFilter TU, not this one.
        error = errZ * errZ + (errY * errY + dz * dz) + error;
        ++it;
    } while (it != pathEnd);
    return error;
}

float ArcDetector::GetSwipeAmount() const {
    float threshold = mSwipeThreshold * 0.3f;
    float adjustedThreshold = (float)mHoverTimer / (float)sDefaultHoverTimer * (mSwipeThreshold - threshold) + threshold;
    float exponent = _swipeRetentionFactor + 1.0f;
    float powered = (float)pow((double)GetPathLength(), (double)exponent);
    float pathErr = GetPathError();
    float swipeAmt = (powered - (pathErr / _acceptablePathErrorRatio)) / adjustedThreshold;

    std::list<Vector3>::const_iterator it = mJointPath.begin();
    unsigned int count = 0;
    if (it != mJointPath.end()) {
        do {
            ++it;
            count++;
        } while (it != mJointPath.end());
    }
    if (count <= 2) {
        swipeAmt = 0.5f - swipeAmt >= 0.0f ? swipeAmt : 0.5f;
    }
    if (mJointPath.begin() != mJointPath.end()) {
        Vector3 front = mJointPath.front();
        Vector3 second = mJointPath.back();
        Vector3 dir(front.x - second.x, front.y - second.y, front.z - second.z);
        Normalize(dir, dir);
        Vector3 boneDir(unk40.z, 0.0f, unk40.x);
        Normalize(boneDir, boneDir);
        if (fabsf(boneDir.y * dir.y + boneDir.z * dir.z + boneDir.x * dir.x) < 0.2f) {
            swipeAmt = 0.9f - swipeAmt >= 0.0f ? swipeAmt : 0.9f;
        }
    }
    return swipeAmt;
}

bool ArcDetector::IsLockedIn() const {
    static float sMinSwipeForLocked = 0.2f;
    static int sMinNodesForLocked = 2;
    std::list<Vector3>::const_iterator it = mJointPath.begin();
    unsigned int count = 0;
    if (it != mJointPath.end()) {
        do {
            ++it;
            count++;
        } while (it != mJointPath.end());
    }
    return count > (unsigned int)sMinNodesForLocked || GetSwipeAmount() > sMinSwipeForLocked;
}

bool ArcDetector::IsPathAcceptable() const {
    unsigned int count = 0;
    const std::list<Vector3> &jointPath = mJointPath;
    std::list<Vector3>::const_iterator it = jointPath.begin();
    static float sSlopeRatioThreshold = 0.5f;
    if (it != jointPath.end()) {
        do {
            ++it;
            count++;
        } while (it != jointPath.end());
    }
    if (count <= 1) {
        return true;
    }
    if (!IsLockedIn()) {
        Vector3 front = jointPath.front();
        const Vector3 &back = jointPath.back();
        float sign = (float)(mSide != 0 ? 1 : -1);
        float diffX = front.x - back.x;
        // front-first: the image has a single `fsubs f12, f9, f12`
        // (front.y - back.y). Spelled `-(back.y - front.y)` MSVC emits the
        // subtraction the other way round plus an `fneg`.
        float dy = front.y - back.y;
        float diffZ = front.z - back.z;
        float dx = sign * diffX;
        if (dx < 0.0f) {
            return false;
        }
        if (dy == 0.0f) {
            return true;
        }
        float invDy = 1.0f / dy;
        if (invDy * dx >= sSlopeRatioThreshold
            || invDy * diffZ >= sSlopeRatioThreshold) {
            return true;
        }
        return false;
    }
    return GetSwipeAmount() > 0.0f;
}

void ArcDetector::TryToStartSwipe(const Vector3 &pos, const Skeleton &skeleton) {
    MILO_ASSERT(mJointPath.empty(), 0x8B);
    bool tracked = true;
    if (skeleton.TrackedJoints()[mPrimaryJoint].mJointConf != kConfidenceTracked ||
        skeleton.TrackedJoints()[mSecondaryJoint].mJointConf != kConfidenceTracked) {
        tracked = false;
    }
    if (tracked) {
        mJointPath.insert(mJointPath.begin(), pos);
        // No `const TrackedJoint *joints` base local -- the image keeps &skeleton
        // itself as the base (`add r11, r11, r30` at 0x82E01078 with r30 == the
        // Skeleton) and folds TrackedJoints()'s 0x4 into the float displacements
        // (0xc = .z, 0x4 = .x).  What it DOES materialise is the address of each
        // mJointPos[0], twice: the two dead `addi r9, r11, 0x4` / `addi r9, r10,
        // 0x4` at 0x82E01080-0x82E01084 -- both into the same scratch, both
        // immediately overwritten, which is what a pair of references that the
        // loads then fold away leaves behind.  SECONDARY is evaluated first
        // (`lwz r11, 0xc(r31)` precedes `lwz r10, 0x8(r31)`).
        const PaddedJointPos &secondaryPos =
            skeleton.TrackedJoints()[mSecondaryJoint].mJointPos[0];
        const PaddedJointPos &primaryPos =
            skeleton.TrackedJoints()[mPrimaryJoint].mJointPos[0];
        float dz = primaryPos.z - secondaryPos.z;
        float dx = primaryPos.x - secondaryPos.x;
        mSwipeExtentX = sqrtf(dx * dx + dz * dz);
    }
}

void ArcDetector::Update(const Skeleton &skeleton, int elapsed) {
    if (!mInitialized) {
        MILO_ASSERT(false, 0x4A);
    }
    if (!skeleton.IsTracked()) {
        Clear();
    } else {
        const TrackedJoint &secondary = skeleton.TrackedJoints()[mSecondaryJoint];
        const TrackedJoint &primary = skeleton.TrackedJoints()[mPrimaryJoint];
        float dx = primary.mJointPos[0].x - secondary.mJointPos[0].x;
        float dy = primary.mJointPos[0].y - secondary.mJointPos[0].y;
        float dz = primary.mJointPos[0].z - secondary.mJointPos[0].z;
        Vector3 boneVec(dx, dy, dz);
        unk40 = boneVec;

        // `empty()`, not `begin() == end()`.  STLport lowers both to the same
        // compare, but spelling it as two iterator calls lets MSVC CSE the
        // `lwz rN, 0x10(r31)` with the later `*mJointPath.begin()` and hoist it
        // above the whole dx/dy/dz computation and the `unk40 = boneVec` copy.
        // The image loads it twice -- once here and again as `lwz r11, 0x0(r30)`
        // inside the mHadProgress arm -- which is what `empty()` produces.
        // 79.1 -> 97.5 canonical on this one change.
        if (mJointPath.empty()) {
            TryToStartSwipe(boneVec, skeleton);
        } else if (mHadProgress) {
            Vector3 frontPt = *mJointPath.begin();
            Clear();
            mJointPath.insert(mJointPath.begin(), boneVec);
            if (mSide == kSkeletonLeft && !(dx < frontPt.x + 0.01f)) {
                mHadProgress = false;
            }
            if (mSide == kSkeletonRight && !(dx > frontPt.x - 0.01f)) {
                mHadProgress = false;
            }
        } else {
            mArcOffset = GetCurveStart();
            Vector3 frontPt = *mJointPath.begin();
            float distX = dx - frontPt.x;
            float distZ = dz - frontPt.z;
            float distY = dy - frontPt.y;
            // RESIDUAL (w7-an, 97.5 canonical): 19 rows in two clusters, both
            // scheduling.  (1) The `Vector3 frontPt` 16-byte copy: the image
            // issues all four `lwz` (w,y,x,z off the node) before all four
            // `stw`, clobbering r11 -- the head-node pointer -- with the last
            // load, so it must RELOAD `lwz r11, 0x0(r30)` for the insert()
            // below.  We keep r11 live, CSE the second begin() away, and
            // interleave one store into the loads.  (2) f11/f12 are swapped
            // across the three fsubs and the image squares distY with `fmuls`
            // immediately after its fsubs, while we defer and square distZ.
            // NEGATIVE RESULT: `mJointPath.front()` for the copy is
            // byte-identical to `*mJointPath.begin()`; hoisting `distY * distY`
            // into its own local is byte-identical too.  Neither touches the
            // r11 liveness that drives cluster (1).
            if (distY * distY + distZ * distZ + distX * distX > 0.0001f) {
                mJointPath.insert(mJointPath.begin(), boneVec);
            }
            const TrackedJoint &armSecondary = skeleton.TrackedJoints()[mSecondaryJoint];
            const TrackedJoint &armPrimary = skeleton.TrackedJoints()[mPrimaryJoint];
            float armDx = armPrimary.mJointPos[0].x - armSecondary.mJointPos[0].x;
            float armDz = armPrimary.mJointPos[0].z - armSecondary.mJointPos[0].z;
            mSwipeExtentX = (sqrtf(armDx * armDx + armDz * armDz) + mSwipeExtentX) * 0.5f;
        }
        mSwipeExtentY = skeleton.TrackedJoints()[mPrimaryJoint].mJointPos[0].y
            - skeleton.TrackedJoints()[mSecondaryJoint].mJointPos[0].y;
        CullPath();
        float swipe = GetSwipeAmount();
        mCurrentSwipeAmt = mCurrentSwipeAmt - swipe >= 0.0f ? mCurrentSwipeAmt : swipe;
        if (!IsPathAcceptable()) {
            SwipeFailed(skeleton);
        }
        float swipe2 = GetSwipeAmount();
        if (swipe2 < 0.1f) {
            mHoverTimer = Max(0, mHoverTimer - elapsed);
        }
    }
}

// Residual at 95.5 is ONE cause, measured 2026-09-14: our frame is 0x2e0+0x20 =
// 0x300 (`stwu r1, -0x300` vs the image's `-0x2e0`) with the SAME callee-saved
// counts (10 GPR / 18 FPR), and that +0x20 is exactly two extra 16-byte Hmx::Color
// temporaries.  The image allocates 15 by-reference Color slots, 0x100 through
// 0x1ec, and puts `prevScaled` immediately above them at 0x1f0; we allocate 17,
// 0x100 through 0x20c, which pushes prevScaled to 0x210.  The body has exactly 15
// `Hmx::Color(...)` arguments taken by const reference (7 in the point loop, 8
// after it) -- the four passed BY VALUE (the two DebugMeter ctors and the two
// DrawBar calls) are built in the shared low scratch at 0x50/0x70 on both sides,
// so they are not the surplus.  170 of the 206 diff_arg rows are the resulting
// uniform displacement shift, and the 33-row r29<->r30 swap is the same story in
// registers: the image keeps `this` in r30 and the TheRnd@ha anchor in r29, we do
// the reverse.  Close the two surplus slots and most of this function follows.
//
// Measured negatives, both reverted:
//   - Naming the second head circle's Vector2 and declaring it before the first
//     circle -- the image does store both halves of it (0xc0/0xc4) above the FIRST
//     UtilDrawCircle2D call at 82E020F8, which an unnamed temporary in an argument
//     list cannot do -- measures 95.0 (237 rows vs 229): the named local buys a
//     THIRD surplus slot.
//   - Binding `const Vector3 &` to joints[..].mJointPos[0] instead of
//     `const TrackedJoint &` to the joint, to recover the two dead
//     `addi r9, r11, 0x4` / `addi r9, r10, 0x4` at 82E0221C: byte-identical, 229
//     rows either way.  Those two addis are still missing.
float ArcDetector::UpdateOverlay(RndOverlay *overlay, float y) {
    static std::list<Vector3> jointPathCopy;
    // lbl_82F44758 (.data, .float 0.1) is a MUTABLE function-local static, not a
    // __real@ literal: the target hoists `lis r24, lbl_82F44758@ha` into a
    // callee-saved GPR before the loop and reloads it every iteration. Its
    // neighbour lbl_82F44754 (.float 0.5) is IsPathAcceptable's
    // sSlopeRatioThreshold, which is already spelled as a static there.
    static float sPathColorStep = 0.1f;
    int numPts = mJointPath.size();
    if (numPts > 1) {
        jointPathCopy.clear();
        for (std::list<Vector3>::const_iterator it = mJointPath.begin(); it != mJointPath.end(); ++it) {
            jointPathCopy.insert(jointPathCopy.end(), *it);
        }
    }
    // Early return, not a wrapping if: the target has TWO returns loading two
    // different source registers (`fmr f1, f17` = the y parameter at 82E01E5C,
    // `fmr f1, f18` = the drawY accumulator at 82E025E4).
    if (jointPathCopy.begin() == jointPathCopy.end())
        return y;
    {
        // The shipped build loads mWidth (TheRnd+0x40) first; MSVC evaluates the
        // operands right to left, so Width() is the *divisor*.  Cross-checked against
        // rb3-xenon's ArcDetector::UpdateOverlay, which spells Height() / Width().
        float aspectRatio = (float)TheRnd.Height() / (float)TheRnd.Width();
        float halfArcScale = aspectRatio / (mSwipeExtentX * 2.0f);
        float drawY = y;
        float fade = 0.0f;

        Vector2 prevScaled;
        for (std::list<Vector3>::const_iterator it = jointPathCopy.begin(); it != jointPathCopy.end(); ++it) {
            Vector3 pt = *it;
            float dx = mArcOffset.x - pt.x;
            if (mSide == kSkeletonRight) {
                dx = dx * -1.0f;
            }
            float zd = mArcOffset.z - pt.z;
            TheRnd.DrawStringScreen(
                MakeString("%f %f", +dx, zd),
                Vector2(0.6f, drawY),
                Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),
                true
            );

            float scaledX = dx * halfArcScale;
            // One named Vector2, used for circle 2, as the polyline's far end,
            // and as next iteration's near end: the target stores it once at
            // 0x80(r1) (82E01FD8/82E01FE4) and passes &0x80(r1) to both draws,
            // then `ld/std` copies it to 0x1f0(r1) for the next pass.
            Vector2 zPt(scaledX, zd);
            float comp = mSwipeExtentX * dx * 2.0f - dx * dx;
            // if/else with the zero in the THEN arm: the target's `bgt` jumps to
            // the fsqrts and falls through to `fmr f27, f30`.
            float arcY;
            if (comp <= 0.0f) {
                arcY = 0.0f;
            } else {
                arcY = sqrtf(comp);
            }
            TheRnd.DrawStringScreen(
                MakeString("%f", arcY),
                Vector2(0.8f, drawY),
                Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),
                true
            );

            // Each successive path point is drawn dimmer:
            // `fnmsubs f29, f25, f21, f31` with f21 = __real@3eb33333 (0.35).
            float c = 1.0f - fade * 0.35f;
            UtilDrawCircle2D(Vector2(scaledX, arcY), 0.01f, Hmx::Color(c, 0.0f, c, 1.0f), 37);
            UtilDrawCircle2D(zPt, 0.004f, Hmx::Color(c, c, 0.0f, 1.0f), 37);
            if (pt != jointPathCopy.front()) {
                UtilDrawLine(prevScaled, zPt, Hmx::Color(c, c, 0.0f, 1.0f));
            }
            Vector2 basePt(scaledX, 0.75f);
            Vector2 heightPt(scaledX, (mArcOffset.y - pt.y) + 0.75f);
            UtilDrawCircle2D(basePt, 0.01f, Hmx::Color(0.0f, 0.0f, c, 1.0f), 37);
            UtilDrawCircle2D(heightPt, 0.01f, Hmx::Color(0.0f, c, 0.0f, 1.0f), 37);

            prevScaled = zPt;
            drawY = drawY + 0.03125f;
            fade = Min(1.0f, fade + sPathColorStep);
        }

        // The LIVE list, and by value: `lwz r11, 0x0(r27)` with r27 = this+0x10
        // (&mJointPath). r31 holds &jointPathCopy and is not read here.
        Vector3 front = *mJointPath.begin();
        Skeleton *skel = TheGestureMgr->GetActiveSkeleton();
        float handX, handY, handZ;
        if (skel != NULL) {
            const TrackedJoint &secondary = skel->TrackedJoints()[mSecondaryJoint];
            const TrackedJoint &primary = skel->TrackedJoints()[mPrimaryJoint];
            handX = primary.mJointPos[0].x - secondary.mJointPos[0].x;
            handY = primary.mJointPos[0].y - secondary.mJointPos[0].y;
            handZ = primary.mJointPos[0].z - secondary.mJointPos[0].z;
        } else {
            handX = front.x;
            handY = front.y;
            handZ = front.z;
        }

        float curDx = mArcOffset.x - handX;
        if (mSide == kSkeletonRight) {
            curDx = curDx * -1.0f;
        }
        float curScaledX = curDx * halfArcScale;
        float curScaledY = mArcOffset.z - handZ;
        Vector2 curPt(curScaledX, curScaledY);
        UtilDrawCircle2D(curPt, 0.015f, Hmx::Color(1.0f, 1.0f, 0.0f, 1.0f), 37);
        UtilDrawLine(curPt, Vector2(aspectRatio * 0.5f, 0.0f), Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f));

        UtilDrawCircle2D(
            Vector2(curScaledX, (mArcOffset.y - handY) + 0.75f), 0.015f,
            Hmx::Color(0.0f, 0.0f, 1.0f, 1.0f), 37
        );

        float pathErr = GetPathError();
        TheRnd.DrawStringScreen(
            MakeString("Sum of error squares: %f", pathErr),
            Vector2(0.1f, y),
            Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),
            true
        );

        float pathLen = GetPathLength();
        TheRnd.DrawStringScreen(
            MakeString("Length of path: %f", pathLen),
            Vector2(0.1f, y + 0.03125f),
            Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),
            true
        );

        if (IsLockedIn()) {
            TheRnd.DrawStringScreen(
                "LOCKED IN",
                Vector2(0.1f, y + 0.0625f),
                Hmx::Color(0.0f, 1.0f, 0.0f, 1.0f),
                true
            );
        } else {
            TheRnd.DrawStringScreen(
                "NOT LOCKED IN",
                Vector2(0.1f, y + 0.0625f),
                Hmx::Color(1.0f, 0.0f, 0.0f, 1.0f),
                true
            );
        }

        TheRnd.DrawStringScreen(
            MakeString("Arc size: %f", mSwipeExtentX),
            Vector2(0.1f, y + 0.09375f),
            Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),
            true
        );

        static DebugMeter swipeMeter(0.1f, 0.55f, 0.8f, 0.03f, Hmx::Color(0.1f, 0.1f, 0.1f, 0.5f));
        swipeMeter.Draw();
        swipeMeter.DrawBar(0.0f, GetSwipeAmount(), Hmx::Color(0.0f, 1.0f, 0.0f, 1.0f), 1.0f, 0.0f);

        static DebugMeter hoverMeter(0.1f, 0.6f, 0.4f, 0.03f, Hmx::Color(0.1f, 0.1f, 0.1f, 0.5f));
        hoverMeter.Draw();
        hoverMeter.DrawBar(0.0f, (float)mHoverTimer / (float)sDefaultHoverTimer, Hmx::Color(0.0f, 0.0f, 1.0f, 1.0f), 1.0f, 0.0f);

        return drawY;
    }
}
