// Pointer input for the game's menus: the rules alone.  PointerGesture.h.

#include "PointerGesture.h"

#include <algorithm>
#include <cmath>

namespace Dc3PointerGesture {

Rect FitFrame(float frameW, float frameH, float boxW, float boxH) {
    Rect r;
    if (frameW <= 0 || frameH <= 0 || boxW <= 0 || boxH <= 0)
        return r;
    const float scale = std::min(boxW / frameW, boxH / frameH);
    r.w = frameW * scale;
    r.h = frameH * scale;
    r.x = (boxW - r.w) * 0.5f;
    r.y = (boxH - r.h) * 0.5f;
    return r;
}

bool BoxToFrame(float bx, float by, float boxW, float boxH, float frameW, float frameH,
                bool letterbox, float &fx, float &fy) {
    Rect r;
    if (letterbox) {
        r = FitFrame(frameW, frameH, boxW, boxH);
    } else {
        r.w = boxW;
        r.h = boxH;
    }
    if (r.w <= 0 || r.h <= 0) {
        fx = bx;
        fy = by;
        return false;
    }
    fx = (bx - r.x) * frameW / r.w;
    fy = (by - r.y) * frameH / r.h;
    return fx >= 0 && fy >= 0 && fx < frameW && fy < frameH;
}

bool PresentToFrame(float sx, float sy, const Rect &present, float frameW, float frameH,
                    float &fx, float &fy) {
    if (present.w <= 0 || present.h <= 0) {
        fx = sx;
        fy = sy;
        return false;
    }
    fx = (sx - present.x) * frameW / present.w;
    fy = (sy - present.y) * frameH / present.h;
    return fx >= 0 && fy >= 0 && fx < frameW && fy < frameH;
}

bool TakesTheMenus(const Output &out) {
    return out.tap || out.back || out.steps != 0;
}

bool HoverMayHighlight(bool controllerMode) { return controllerMode; }

int HitRow(const std::vector<Row> &rows, float x, float y) {
    int best = -1;
    float bestDist = 0.0f;
    for (const Row &row : rows) {
        if (!row.active)
            continue;
        if (std::fabs(x - row.cx) > row.halfW || std::fabs(y - row.cy) > row.halfH)
            continue;
        const float d = std::fabs(y - row.cy);
        if (best < 0 || d < bestDist) {
            best = row.index;
            bestDist = d;
        }
    }
    return best;
}

float RowPitch(const std::vector<Row> &rows, float fallback) {
    if (rows.size() < 2)
        return fallback;
    std::vector<float> ys;
    ys.reserve(rows.size());
    for (const Row &row : rows)
        ys.push_back(row.cy);
    std::sort(ys.begin(), ys.end());
    std::vector<float> gaps;
    for (size_t i = 1; i < ys.size(); i++) {
        const float g = ys[i] - ys[i - 1];
        if (g > 1.0f)
            gaps.push_back(g);
    }
    if (gaps.empty())
        return fallback;
    std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
    return gaps[gaps.size() / 2];
}

float Tracker::Velocity(float tUp, const Params &p) const {
    if (mSamples.size() < 2)
        return 0.0f;
    // The travel over [tUp - window, tUp]: where the finger was at the
    // window's start, interpolated between the samples either side of it.  A
    // finger that rested the whole window has none (its position at the start
    // is its position at the end); a burst of events stamped close together
    // is measured against where the finger was a window ago, not against the
    // burst's own first event.
    const float yEnd = mSamples.back().y;
    float tStart = tUp - p.velocityWindow;
    float yStart;
    if (mSamples.front().t >= tStart) {
        // the drag is younger than the window: from the press
        tStart = mSamples.front().t;
        yStart = mSamples.front().y;
    } else {
        size_t i = 0;
        while (i + 1 < mSamples.size() && mSamples[i + 1].t <= tStart)
            i++;
        if (i + 1 >= mSamples.size()) {
            yStart = mSamples.back().y; // no sample in the window: it rested
        } else {
            const Sample &a = mSamples[i], &b = mSamples[i + 1];
            const float span = b.t - a.t;
            yStart = span > 1e-6f ? a.y + (b.y - a.y) * (tStart - a.t) / span : b.y;
        }
    }
    const float dt = std::max(tUp - tStart, p.minVelocityDt);
    // finger up the screen (y falling) = later rows
    return -(yEnd - yStart) / dt;
}

void Tracker::Feed(const Input &in, const Params &p) {
    switch (in.kind) {
    case kMove:
        if (mPressed) {
            const float dx = in.x - mDownX, dy = in.y - mDownY;
            if (!mDragging && dx * dx + dy * dy > mSlop * mSlop) {
                mDragging = true;
                // the slop is not lost: the content catches up with the finger
                mPixels += -(in.y - mDownY);
            } else if (mDragging) {
                mPixels += -(in.y - mLastY);
            }
            mLastY = in.y;
            mSamples.push_back({in.y, in.t});
            if (mSamples.size() > 64)
                mSamples.erase(mSamples.begin(), mSamples.begin() + 32);
        } else if (!in.touch) {
            mHoverPending = true;
            mHoverX = in.x;
            mHoverY = in.y;
        }
        break;
    case kDown:
        // a press that stops a fling, or a list still running rows owed,
        // picks nothing (Fable review: a tap meant to stop a moving list
        // selected the row that was passing under it)
        mSuppressTap = mFlingVelocity != 0.0f || mListMoving;
        mStoppedPending = mSuppressTap;
        mFlingVelocity = 0.0f;
        mFlingTravel = 0.0f;
        // a new gesture: what the last one left over is not this one's
        mPixels = 0.0f;
        mWheelRows = 0.0f;
        mTouch = in.touch;
        mSlop = in.touch ? std::max(p.slop, p.touchSlopUnits * std::max(in.unitPx, 0.0f)) : p.slop;
        mPressed = true;
        mDragging = false;
        mDownX = in.x;
        mDownY = in.y;
        mLastY = in.y;
        mSamples.clear();
        mSamples.push_back({in.y, in.t});
        if (!in.touch) {
            mHoverPending = true;
            mHoverX = in.x;
            mHoverY = in.y;
        }
        break;
    case kUp:
        if (!mPressed)
            break;
        if (in.y != mLastY) {
            Input move = in;
            move.kind = kMove;
            Feed(move, p);
        }
        mPressed = false;
        if (mDragging) {
            const float v = Velocity(in.t, p);
            mReleaseV = v;
            mReleaseN = (int)mSamples.size();
            mReleaseSpan = mSamples.empty() ? 0.0f : in.t - mSamples.front().t;
            mReleases++;
            if (std::fabs(v) >= p.flickVelocity)
                mFlingVelocity = v;
        } else if (!mSuppressTap) {
            mTapPending = true;
            mTapTouch = mTouch;
            mTapX = mDownX;
            mTapY = mDownY;
        }
        mDragging = false;
        mSuppressTap = false;
        mSamples.clear();
        break;
    case kWheel:
        mFlingVelocity = 0.0f;
        if (mLastScrollT >= 0.0f && in.t - mLastScrollT > p.remainderIdle)
            mWheelRows = 0.0f; // a part-notch from a while ago is not this scroll's
        mLastScrollT = in.t;
        mWheelRows += in.wheel;
        break;
    case kCancel:
        mPressed = false;
        mDragging = false;
        mSuppressTap = false;
        mSamples.clear();
        break;
    case kBack:
        mBackPending = true;
        break;
    }
}

Output Tracker::Tick(float now, float pitch, const Params &p) {
    Output out;
    if (pitch < 1.0f)
        pitch = 1.0f;
    // a long frame (a load, a stall, a backgrounded tab) advances a fling by
    // one ordinary frame, not by the gap
    const float dt =
        mLastTick < 0.0f ? 0.0f : std::min(p.maxTickDt, std::max(0.0f, now - mLastTick));
    mLastTick = now;
    if (mDragging || mFlingVelocity != 0.0f)
        mLastScrollT = now;
    else if (mLastScrollT >= 0.0f && now - mLastScrollT > p.remainderIdle) {
        // nothing has scrolled for a while: the part of a row left over goes
        mPixels = 0.0f;
        mWheelRows = 0.0f;
    }

    if (mFlingVelocity != 0.0f && !mPressed) {
        const float step = mFlingVelocity * dt;
        mPixels += step;
        mFlingTravel += std::fabs(step);
        mFlingVelocity *= std::exp(-p.friction * dt);
        if (std::fabs(mFlingVelocity) < p.stopVelocity
            || mFlingTravel >= p.maxFlingRows * pitch) {
            mFlingVelocity = 0.0f;
            mFlingTravel = 0.0f;
        }
    }

    // whole rows out; the remainder stays for the next frame
    const float rows = mPixels / pitch;
    const int whole = (int)rows; // toward zero
    mPixels -= whole * pitch;
    const int wheelWhole = (int)mWheelRows;
    mWheelRows -= wheelWhole;
    out.steps = whole + wheelWhole;

    out.hover = mHoverPending;
    out.x = mHoverX;
    out.y = mHoverY;
    mHoverPending = false;
    out.tap = mTapPending;
    out.tapTouch = mTapTouch;
    out.stopped = mStoppedPending;
    mStoppedPending = false;
    out.tapX = mTapX;
    out.tapY = mTapY;
    mTapPending = false;
    out.back = mBackPending;
    mBackPending = false;
    out.dragging = mDragging;
    out.flinging = mFlingVelocity != 0.0f;
    return out;
}

} // namespace Dc3PointerGesture
