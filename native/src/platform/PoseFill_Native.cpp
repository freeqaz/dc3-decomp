// PoseFill_Native.cpp -- see PoseFill_Native.h.
#include "PoseFill_Native.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Dc3PoseFill {

Mode ParseMode(const char *env, bool *ok) {
    if (ok)
        *ok = true;
    if (!env || !*env || !strcmp(env, "extrap"))
        return kExtrap;
    if (!strcmp(env, "interp"))
        return kInterp;
    if (!strcmp(env, "off") || !strcmp(env, "0"))
        return kOff;
    if (!strcmp(env, "hold"))
        return kHold;
    if (!strcmp(env, "extrapc"))
        return kExtrapClamped;
    if (ok)
        *ok = false;
    return kExtrap;
}

float ParseGain(const char *env, bool *ok) {
    if (ok)
        *ok = true;
    if (!env || !*env)
        return kDefaultGain;
    char *end = nullptr;
    double g = strtod(env, &end);
    if (end == env || *end != '\0' || !(g >= 0.0 && g <= 1.0)) {
        if (ok)
            *ok = false;
        return kDefaultGain;
    }
    return (float)g;
}

const char *ModeName(Mode m) {
    switch (m) {
    case kOff:
        return "off";
    case kHold:
        return "hold";
    case kExtrap:
        return "extrap";
    case kExtrapClamped:
        return "extrapc";
    case kInterp:
        return "interp";
    }
    return "?";
}

static const Person *FindValid(const Source &s, int trackId) {
    if (trackId < 0)
        return nullptr;
    for (int i = 0; i < s.numPersons; i++) {
        if (s.persons[i].valid && s.persons[i].trackId == trackId)
            return &s.persons[i];
    }
    return nullptr;
}

static bool AnyValid(const Source &s) {
    for (int i = 0; i < s.numPersons; i++) {
        if (s.persons[i].valid)
            return true;
    }
    return false;
}

bool SamePeople(const Source &a, const Source &b) {
    int na = 0, nb = 0;
    for (int i = 0; i < a.numPersons; i++) {
        if (!a.persons[i].valid)
            continue;
        na++;
        if (!FindValid(b, a.persons[i].trackId))
            return false;
    }
    for (int i = 0; i < b.numPersons; i++) {
        if (b.persons[i].valid)
            nb++;
    }
    return na == nb;
}

static void CopyFrame(const Source &s, Output &out) {
    out.numPersons = s.numPersons;
    for (int i = 0; i < s.numPersons; i++)
        out.persons[i] = s.persons[i];
    out.floorNormal = s.floorNormal;
    out.levelled = s.levelled;
}

void Interpolate(const Source &a, const Source &b, float alpha, Output &out) {
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;
    CopyFrame(a, out);
    for (int i = 0; i < out.numPersons; i++) {
        Person &p = out.persons[i];
        if (!p.valid)
            continue;
        const Person *q = FindValid(b, p.trackId);
        if (!q)
            continue;
        for (int j = 0; j < kNumJoints; j++) {
            if (p.confidence[j] <= kConfidenceNotTracked || q->confidence[j] <= kConfidenceNotTracked)
                continue; // the image's joint hold, not a line to a guess
            const Vector3 &x = p.joints[j], &y = q->joints[j];
            p.joints[j].Set(x.x + (y.x - x.x) * alpha, x.y + (y.y - x.y) * alpha,
                            x.z + (y.z - x.z) * alpha);
            if (q->confidence[j] < p.confidence[j])
                p.confidence[j] = q->confidence[j];
        }
    }
}

// The tracker's left/right confusion: some pair of `last` sits much closer to
// the other side's joint of `prev` than to its own (by more than half).  Real
// motion does that on 0.1% of 8 Hz frame pairs of the np-video-calib footage
// (hands crossing); a whole-body swap does it on 99.7%.
static bool LooksSwapped(const Person &prev, const Person &last) {
    static const int kPairs[][2] = {
        { kJointShoulderLeft, kJointShoulderRight }, { kJointElbowLeft, kJointElbowRight },
        { kJointWristLeft, kJointWristRight },       { kJointHandLeft, kJointHandRight },
        { kJointHipLeft, kJointHipRight },           { kJointKneeLeft, kJointKneeRight },
        { kJointAnkleLeft, kJointAnkleRight },       { kJointFootLeft, kJointFootRight },
    };
    auto dist = [](const Vector3 &a, const Vector3 &b) {
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return sqrtf(dx * dx + dy * dy + dz * dz);
    };
    for (const auto &pr : kPairs) {
        int l = pr[0], r = pr[1];
        if (prev.confidence[l] <= kConfidenceNotTracked || prev.confidence[r] <= kConfidenceNotTracked
            || last.confidence[l] <= kConfidenceNotTracked || last.confidence[r] <= kConfidenceNotTracked)
            continue;
        float straight = dist(last.joints[l], prev.joints[l]) + dist(last.joints[r], prev.joints[r]);
        float swapped = dist(last.joints[l], prev.joints[r]) + dist(last.joints[r], prev.joints[l]);
        if (swapped < kSwapMargin * straight)
            return true;
    }
    return false;
}

void ExtrapolateGuarded(const Source &prev, const Source &last, float seconds, float period, float gain,
                        Output &out) {
    CopyFrame(last, out);
    if (period <= 1e-3f || seconds <= 0.0f || gain <= 0.0f)
        return;
    for (int i = 0; i < out.numPersons; i++) {
        Person &p = out.persons[i];
        if (!p.valid)
            continue;
        const Person *q = FindValid(prev, p.trackId);
        if (!q || LooksSwapped(*q, p))
            continue; // held: nothing to step from, or a step that is a swap
        for (int j = 0; j < kNumJoints; j++) {
            if (p.confidence[j] <= kConfidenceNotTracked || q->confidence[j] <= kConfidenceNotTracked)
                continue; // a guess is not extrapolated
            const Vector3 &x = p.joints[j], &xp = q->joints[j];
            float vx = (x.x - xp.x) / period, vy = (x.y - xp.y) / period, vz = (x.z - xp.z) / period;
            float speed = sqrtf(vx * vx + vy * vy + vz * vz);
            if (speed > kMaxJointSpeed) {
                float k = kMaxJointSpeed / speed;
                vx *= k;
                vy *= k;
                vz *= k;
            }
            float t = gain * seconds;
            p.joints[j].Set(x.x + vx * t, x.y + vy * t, x.z + vz * t);
        }
    }
}

void Extrapolate(const Source &prev, const Source &last, float a, bool clamped, Output &out) {
    CopyFrame(last, out);
    for (int i = 0; i < out.numPersons; i++) {
        Person &p = out.persons[i];
        if (!p.valid)
            continue;
        const Person *q = nullptr;
        for (int k = 0; k < prev.numPersons; k++) {
            if (prev.persons[k].valid && prev.persons[k].trackId == p.trackId) {
                q = &prev.persons[k];
                break;
            }
        }
        if (!q)
            continue;
        for (int j = 0; j < kNumJoints; j++) {
            if (clamped
                && (p.confidence[j] < kConfidenceTracked || q->confidence[j] < kConfidenceTracked))
                continue;
            const Vector3 &x = p.joints[j], &xp = q->joints[j];
            float dx = (x.x - xp.x) * a, dy = (x.y - xp.y) * a, dz = (x.z - xp.z) * a;
            if (clamped) {
                float len = sqrtf(dx * dx + dy * dy + dz * dz);
                if (len > kMaxStepMetres) {
                    float k = kMaxStepMetres / len;
                    dx *= k;
                    dy *= k;
                    dz *= k;
                }
            }
            p.joints[j].Set(x.x + dx, x.y + dy, x.z + dz);
        }
    }
}

void Filler::SetMode(Mode m) {
    if (m != mMode) {
        mMode = m;
        Reset();
    }
}

void Filler::Reset() {
    mEngaged = false;
    mDelay = 0.0;
    mNumIntervals = mIntervalHead = 0;
    mCount = 0;
    mDelivered = -1;
    mHaveOut = false;
    mLastShows = 0.0;
    mSigma = -1e30;
    mArrivalElapsedMs = 33;
    mFills = mReals = 0;
}

void Filler::SetGain(float g) {
    mGain = g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
}

double Filler::MedianInterval() const {
    if (mNumIntervals < kMinIntervals)
        return 0.0;
    double v[kWindow];
    for (int i = 0; i < mNumIntervals; i++)
        v[i] = mIntervals[i];
    std::nth_element(v, v + mNumIntervals / 2, v + mNumIntervals);
    return v[mNumIntervals / 2];
}

double Filler::RateInterval() const {
    if (mNumIntervals < kMinIntervals)
        return 0.0;
    double v[kWindow];
    for (int i = 0; i < mNumIntervals; i++)
        v[i] = mIntervals[i];
    std::sort(v, v + mNumIntervals);
    const int trim = mNumIntervals / 5;
    double sum = 0.0;
    for (int i = trim; i < mNumIntervals - trim; i++)
        sum += v[i];
    return sum / (mNumIntervals - 2 * trim);
}

void Filler::Drop(int n) {
    if (n <= 0)
        return;
    if (n > mCount)
        n = mCount;
    for (int i = n; i < mCount; i++)
        mBuf[i - n] = mBuf[i];
    mCount -= n;
    mDelivered -= n;
    if (mDelivered < -1)
        mDelivered = -1;
}

int Filler::FindDue(double sigma) const {
    int k = -1;
    for (int i = 0; i < mCount; i++) {
        if (mBuf[i].arrival <= sigma)
            k = i;
    }
    return k;
}

bool Filler::Push(const Source &src) {
    if (mCount > 0) {
        const Source &last = mBuf[mCount - 1];
        if (src.arrival < last.arrival - 0.5) {
            // The clock went back (a new song, the menus): the frames before
            // are of another timeline.  The camera's rate is unchanged.
            mCount = 0;
            mDelivered = -1;
            mHaveOut = false;
            mSigma = -1e30;
        } else {
            double interval = src.arrival - last.arrival;
            if (interval > 0.0 && interval < 1.0) {
                mIntervals[mIntervalHead] = interval;
                mIntervalHead = (mIntervalHead + 1) % kWindow;
                if (mNumIntervals < kWindow)
                    mNumIntervals++;
            }
            double ms = interval * 1000.0;
            if (src.ts >= 0.0 && last.ts >= 0.0 && src.ts > last.ts)
                ms = (src.ts - last.ts) * 1000.0;
            long r = (long)(ms + 0.5);
            mArrivalElapsedMs = (int)(r < 1 ? 1 : r > 200 ? 200 : r);
        }
    }
    if (mCount == kBuffer)
        Drop(1);
    mBuf[mCount++] = src;

    double med = MedianInterval();
    double rate = RateInterval();
    if (mMode == kOff) {
        mEngaged = false;
    } else if (!mEngaged && rate > 1.0 / kEngageHz) {
        mEngaged = true;
        mDelay = 0.0;
    } else if (mEngaged && rate > 0.0 && rate < 1.0 / kReleaseHz) {
        mEngaged = false;
    }
    if (mEngaged && mMode == kInterp) {
        double d = std::min(std::max(med * kDelayFactor, kStep), kMaxDelay);
        if (mDelay == 0.0 || fabs(d - mDelay) > kDelayHysteresis)
            mDelay = d;
    }
    if (!mEngaged) {
        // The caller delivers it itself, now.
        mDelivered = mCount - 1;
        mHaveOut = true;
        mLastShows = src.arrival;
        mSigma = src.arrival;
        Drop(mCount - 2); // keep it and the one before (extrap, a later engage)
    }
    return mEngaged;
}

void Filler::MarkLevel(const Vector3 &floorNormal) {
    if (mCount == 0)
        return;
    Source &s = mBuf[mCount - 1];
    s.levelled = floorNormal.x != 0.0f || floorNormal.y != 0.0f || floorNormal.z != 0.0f;
    s.floorNormal = floorNormal;
}

bool Filler::Poll(double song, Output &out) {
    if (!mEngaged || mCount == 0)
        return false;
    if (mHaveOut && song - Delay() < mLastShows - 0.5)
        return false; // the clock went back; Push resets on the next frame

    if (mMode == kInterp) {
        double sigma = song - mDelay;
        if (sigma < mSigma)
            sigma = mSigma; // never show an earlier moment than already shown
        mSigma = sigma;
        // Camera frames first, each exactly, oldest first.
        int u = mDelivered + 1;
        if (u < mCount && mBuf[u].arrival <= sigma) {
            const Source &s = mBuf[u];
            CopyFrame(s, out);
            out.ts = s.ts;
            out.shows = s.arrival;
            out.real = true;
            mDelivered = u;
            mHaveOut = true;
            mLastShows = s.arrival;
            mReals++;
            Drop(u); // keep it: the next fill starts from it
            return true;
        }
        if (mDelivered < 0 || mDelivered + 1 >= mCount)
            return false; // nothing newer to go towards: a stall stays a stall
        const Source &a = mBuf[mDelivered];
        const Source &b = mBuf[mDelivered + 1];
        if (sigma - mLastShows < kStep - 1e-6)
            return false;
        if (b.arrival - sigma < kStep * 0.5)
            return false; // the camera frame itself is nearly due
        double gap = b.arrival - a.arrival;
        double maxGap = std::min(kGapFactor * MedianInterval(), kMaxGap);
        if (gap <= 0.0 || gap > maxGap)
            return false;
        if (!SamePeople(a, b) || !AnyValid(a))
            return false; // a dropout (or nobody): the camera's own frames only
        float alpha = (float)((sigma - a.arrival) / gap);
        Interpolate(a, b, alpha, out);
        out.ts = (a.ts >= 0.0 && b.ts > a.ts) ? a.ts + (b.ts - a.ts) * alpha : -1.0;
        out.shows = sigma;
        out.real = false;
        mLastShows = sigma;
        mFills++;
        return true;
    }

    // hold / extrap / extrapc: no delay.  The newest camera frame at once.
    int n = mCount - 1;
    if (mDelivered < n) {
        const Source &s = mBuf[n];
        CopyFrame(s, out);
        out.ts = s.ts;
        out.shows = s.arrival;
        out.real = true;
        mDelivered = n;
        mHaveOut = true;
        mLastShows = song;
        mReals++;
        Drop(n - 1);
        return true;
    }
    if (mDelivered < 0)
        return false;
    const Source &a = mBuf[mDelivered];
    if (!AnyValid(a))
        return false; // nobody in the newest frame: nothing to fill (P-12's hold sees the camera's frames)
    double sinceReal = song - a.arrival;
    if (song - mLastShows < kStep - 1e-6)
        return false;
    const Source *prev = mDelivered > 0 ? &mBuf[mDelivered - 1] : nullptr;
    double period = -1.0;
    if (prev && a.ts >= 0.0 && prev->ts >= 0.0 && a.ts > prev->ts)
        period = a.ts - prev->ts;
    else if (prev && a.arrival > prev->arrival)
        period = a.arrival - prev->arrival;
    if (mMode == kExtrap) {
        // One source period at most: the last pair's, and never more than the
        // camera's usual one (a dropped frame doubles the pair's), then the
        // stream waits for the camera.
        double horizon = period;
        double med = MedianInterval();
        if (med > 0.0 && (horizon <= 0.0 || horizon > med))
            horizon = med;
        if (horizon <= 0.0)
            horizon = kStep;
        if (sinceReal > horizon + 1e-6)
            return false;
    } else if (mMode == kExtrapClamped) {
        if (period <= 0.0 || sinceReal > period)
            return false;
    } else if (sinceReal >= kHoldCap) {
        return false;
    }
    if (mMode == kHold || !prev || period <= 1e-3) {
        CopyFrame(a, out);
    } else if (mMode == kExtrap) {
        if (prev->arrival < a.arrival - std::min(kGapFactor * MedianInterval(), kMaxGap))
            CopyFrame(a, out); // the pair straddles a camera gap: no velocity from it
        else
            ExtrapolateGuarded(*prev, a, (float)sinceReal, (float)period, mGain, out);
    } else {
        double horizon = mMode == kExtrapClamped ? 0.5 * period : period;
        float k = (float)(std::min(sinceReal, horizon) / period);
        Extrapolate(*prev, a, k, mMode == kExtrapClamped, out);
    }
    out.ts = -1.0;
    if (a.ts >= 0.0) {
        double step = sinceReal;
        if (period > 0.0 && step > 0.9 * period)
            step = 0.9 * period; // short of where the next camera frame lands
        out.ts = a.ts + step;
    }
    out.shows = song;
    out.real = false;
    mLastShows = song;
    mFills++;
    return true;
}

} // namespace Dc3PoseFill
