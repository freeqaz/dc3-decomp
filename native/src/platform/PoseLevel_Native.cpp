// Camera tilt for a sensor without an accelerometer.  See PoseLevel_Native.h.
#include "platform/PoseLevel_Native.h"

#include "gesture/Skeleton.h"
#include "xdk/nui/nuiskeleton.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const float kRadToDeg = 57.29577951308232f;
const float kDegToRad = 0.017453292519943295f;

float AngleDeg(const Vector3 &a, const Vector3 &b) {
    float la = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    float lb = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
    if (la <= 0.0f || lb <= 0.0f)
        return 180.0f;
    float c = (a.x * b.x + a.y * b.y + a.z * b.z) / (la * lb);
    c = std::max(-1.0f, std::min(1.0f, c));
    return std::acos(c) * kRadToDeg;
}

float AngleDeg2D(float ax, float ay, float bx, float by) {
    return AngleDeg(Vector3(ax, ay, 0.0f), Vector3(bx, by, 0.0f));
}

float Median(const float *v, int n, float *scratch) {
    memcpy(scratch, v, n * sizeof(float));
    int mid = n / 2;
    std::nth_element(scratch, scratch + mid, scratch + n);
    float hi = scratch[mid];
    if (n % 2)
        return hi;
    float lo = *std::max_element(scratch, scratch + mid);
    return 0.5f * (lo + hi);
}

} // namespace

// ---------------------------------------------------------------------------
// NativeGravityEstimator
// ---------------------------------------------------------------------------
void NativeGravityEstimator::Reset() {
    mCount = 0;
    mHead = 0;
    mClock = 0.0f;
    mHave = false;
    mEstPitch = 0.0f;
    mEstRoll = 0.0f;
}

bool NativeGravityEstimator::Lean(const Vector3 *j, const JointConfidence *conf,
                                  float &pitchDeg, float &rollDeg) {
    // Every joint the axis and the gates read must be really tracked: with
    // the legs out of frame MediaPipe still places them (np-pose-tilt
    // measured knees/ankles at confidence 0.02-0.45 there, leaning 15 deg).
    static const int kNeeded[] = {
        kJointHipCenter, kJointShoulderCenter, kJointShoulderLeft, kJointShoulderRight,
        kJointHipLeft, kJointKneeLeft, kJointAnkleLeft,
        kJointHipRight, kJointKneeRight, kJointAnkleRight,
    };
    for (int k : kNeeded) {
        if (conf[k] != kConfidenceTracked)
            return false;
    }
    Vector3 feet((j[kJointAnkleLeft].x + j[kJointAnkleRight].x) * 0.5f,
                 (j[kJointAnkleLeft].y + j[kJointAnkleRight].y) * 0.5f,
                 (j[kJointAnkleLeft].z + j[kJointAnkleRight].z) * 0.5f);
    Vector3 axis(j[kJointHipCenter].x - feet.x, j[kJointHipCenter].y - feet.y,
                 j[kJointHipCenter].z - feet.z);
    float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (len < 0.3f) // a crouch, or garbage: not a standing body
        return false;
    // Upright: the legs' axis near the camera's vertical (a camera tilted
    // more than this is out of scope), the trunk in line with the legs (not
    // bent over), knees straight in the picture (depth is MediaPipe's
    // weakest axis -- its shins read ~30 deg back while standing), and
    // facing the camera (a profile view's depth-only shoulder line).
    if (AngleDeg(axis, Vector3(0.0f, 1.0f, 0.0f)) > kLeanGateDeg)
        return false;
    Vector3 trunk(j[kJointShoulderCenter].x - j[kJointHipCenter].x,
                  j[kJointShoulderCenter].y - j[kJointHipCenter].y,
                  j[kJointShoulderCenter].z - j[kJointHipCenter].z);
    if (AngleDeg(trunk, axis) > kSpineGateDeg)
        return false;
    static const int kLegs[2][3] = {
        { kJointHipLeft, kJointKneeLeft, kJointAnkleLeft },
        { kJointHipRight, kJointKneeRight, kJointAnkleRight },
    };
    for (const auto &leg : kLegs) {
        const Vector3 &h = j[leg[0]], &k = j[leg[1]], &a = j[leg[2]];
        if (AngleDeg2D(k.x - h.x, k.y - h.y, a.x - k.x, a.y - k.y) > kKneeGateDeg)
            return false;
    }
    Vector3 shoulders(j[kJointShoulderLeft].x - j[kJointShoulderRight].x,
                      j[kJointShoulderLeft].y - j[kJointShoulderRight].y,
                      j[kJointShoulderLeft].z - j[kJointShoulderRight].z);
    float sl = std::sqrt(shoulders.x * shoulders.x + shoulders.y * shoulders.y
                         + shoulders.z * shoulders.z);
    if (sl <= 0.0f || std::fabs(shoulders.x) < kFacingMin * sl)
        return false;
    pitchDeg = std::atan2(-axis.z, axis.y) * kRadToDeg;
    rollDeg = std::atan2(axis.x, axis.y) * kRadToDeg;
    return true;
}

bool NativeGravityEstimator::AddPerson(const Vector3 *joints, const JointConfidence *conf) {
    float p, r;
    if (!Lean(joints, conf, p, r))
        return false;
    mPitch[mHead] = p;
    mRoll[mHead] = r;
    mHead = (mHead + 1) % kWindow;
    if (mCount < kWindow)
        mCount++;
    return true;
}

bool NativeGravityEstimator::Tick(float dtSecs) {
    if (!(dtSecs > 0.0f))
        return false;
    mClock += dtSecs;
    if (mClock < kUpdatePeriodSecs)
        return false;
    mClock = std::fmod(mClock, kUpdatePeriodSecs);
    if (mCount < kMinSamples)
        return false;
    static float sScratch[kWindow];
    float candPitch = Median(mPitch, mCount, sScratch) - kPriorPitchDeg;
    float candRoll = Median(mRoll, mCount, sScratch);
    candPitch = std::max(-kMaxTiltDeg, std::min(kMaxTiltDeg, candPitch));
    candRoll = std::max(-kMaxTiltDeg, std::min(kMaxTiltDeg, candRoll));
    float oldPitch = mEstPitch, oldRoll = mEstRoll;
    if (!mHave) {
        mEstPitch = candPitch;
        mEstRoll = candRoll;
        mHave = true;
        return true;
    }
    mEstPitch += kBlend * (candPitch - mEstPitch);
    mEstRoll += kBlend * (candRoll - mEstRoll);
    return mEstPitch != oldPitch || mEstRoll != oldRoll;
}

Vector3 NativeGravityEstimator::NormalFromPitchRoll(float pitchDeg, float rollDeg) {
    // pitch = atan2(-z, y), roll = atan2(x, y) for y = 1.
    Vector3 n(std::tan(rollDeg * kDegToRad), 1.0f, -std::tan(pitchDeg * kDegToRad));
    float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    return Vector3(n.x / len, n.y / len, n.z / len);
}

Vector3 NativeGravityEstimator::NormalToGravity() const {
    if (!mHave)
        return Vector3(0.0f, 1.0f, 0.0f);
    return NormalFromPitchRoll(mEstPitch, mEstRoll);
}

// ---------------------------------------------------------------------------
// Create's gravity step
// ---------------------------------------------------------------------------
static Vector3 TransformRow(const Vector3 &v, const XMMATRIX &m) {
    // XMVector3Transform as Skeleton.cpp spells it: z*r2 + r3, + y*r1, + x*r0.
    float x = v.z * m.r[2].x + m.r[3].x;
    float y = v.z * m.r[2].y + m.r[3].y;
    float z = v.z * m.r[2].z + m.r[3].z;
    x = v.y * m.r[1].x + x;
    y = v.y * m.r[1].y + y;
    z = v.y * m.r[1].z + z;
    x = v.x * m.r[0].x + x;
    y = v.x * m.r[0].y + y;
    z = v.x * m.r[0].z + z;
    return Vector3(x, y, z);
}

void NativeLevelSensorFrame(SkeletonFrame &frame, const Vector3 &normal) {
    SkeletonFrame::sUpVectorSmoother.Smooth(normal, frame.mElapsedMs * 0.001f, false);
    frame.mFloorNormal = SkeletonFrame::sUpVectorSmoother.Value();
    XMVECTOR grav;
    grav.x = frame.mFloorNormal.x;
    grav.y = frame.mFloorNormal.y;
    grav.z = frame.mFloorNormal.z;
    grav.w = 0.0f;
    XMMATRIX mat = NuiTransformMatrixLevel(grav);
    for (int s = 0; s < 6; s++) {
        SkeletonData &data = frame.mSkeletonDatas[s];
        if (data.mTracking != kSkeletonTracked)
            continue;
        for (int j = 0; j < kNumJoints; j++) {
            Vector3 raw(data.mJointPositions[j].x, data.mJointPositions[j].y,
                        data.mJointPositions[j].z);
            Vector3 lv = TransformRow(raw, mat);
            data.mJointPositions[j].Set(lv.x, lv.y, lv.z);
        }
        data.mHipCenter = TransformRow(data.mHipCenter, mat);
    }
}

// ---------------------------------------------------------------------------
// The sensor's policy
// ---------------------------------------------------------------------------
NativePoseLevelPolicy NativePoseLevelParse(const char *env) {
    NativePoseLevelPolicy p;
    if (!env || !*env || !strcmp(env, "auto") || !strcmp(env, "1"))
        return p;
    if (!strcmp(env, "0") || !strcmp(env, "off")) {
        p.mode = NativePoseLevelPolicy::kOff;
        return p;
    }
    char *end = nullptr;
    double deg = strtod(env, &end);
    if (end != env && *end == '\0' && deg >= -30.0 && deg <= 30.0) {
        p.mode = NativePoseLevelPolicy::kFixed;
        p.fixedPitchDeg = (float)deg;
        return p;
    }
    fprintf(stderr, "DC3_POSE_LEVEL='%s' ignored (want auto, 0/off, or a pitch in -30..30 deg)\n",
            env);
    return p;
}

static NativeGravityEstimator sEstimator;
static NativePoseLevelPolicy sPolicy;
static bool sPolicyRead = false;

static const NativePoseLevelPolicy &Policy() {
    if (!sPolicyRead) {
        sPolicyRead = true;
        sPolicy = NativePoseLevelParse(getenv("DC3_POSE_LEVEL"));
    }
    return sPolicy;
}

const NativeGravityEstimator &NativeSensorGravityEstimator() { return sEstimator; }

void NativeSensorLevelSetPolicy(const NativePoseLevelPolicy &policy) {
    sPolicy = policy;
    sPolicyRead = true;
    sEstimator.Reset();
}

void NativeSensorLevel(SkeletonFrame &frame, const NativeSkeletonProvider::PersonData *persons,
                       int numPersons, bool unlevelled, float dtSecs) {
    const NativePoseLevelPolicy &policy = Policy();
    if (policy.mode == NativePoseLevelPolicy::kOff)
        return;
    // A process that never ran GestureInit (SkeletonFrame::Init: smoothing
    // from config, ForceValue((0,1,0))) has a zero-initialised smoother,
    // whose Value() would hand the game a zero floor normal.  Seed it as
    // Init would; with no config its gain stays 0 and the frame stays level.
    Vector3 cur = SkeletonFrame::sUpVectorSmoother.Value();
    if (cur.x == 0.0f && cur.y == 0.0f && cur.z == 0.0f)
        SkeletonFrame::sUpVectorSmoother.ForceValue(Vector3(0.0f, 1.0f, 0.0f));
    Vector3 normal(0.0f, 1.0f, 0.0f);
    if (unlevelled) {
        if (policy.mode == NativePoseLevelPolicy::kFixed) {
            normal = NativeGravityEstimator::NormalFromPitchRoll(policy.fixedPitchDeg, 0.0f);
        } else {
            for (int i = 0; i < numPersons; i++) {
                if (persons[i].valid)
                    sEstimator.AddPerson(persons[i].joints, persons[i].confidence);
            }
            if (sEstimator.Tick(dtSecs)) {
                static bool sLog = getenv("DC3_POSE_LEVEL_LOG") != nullptr;
                if (sLog)
                    fprintf(stderr, "DC3 POSE_LEVEL pitch=%.2f roll=%.2f samples=%d\n",
                            sEstimator.PitchDeg(), sEstimator.RollDeg(), sEstimator.NumSamples());
            }
            normal = sEstimator.NormalToGravity();
        }
    }
    NativeLevelSensorFrame(frame, normal);
}
