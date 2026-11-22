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

float Tracker::Velocity(const Params &p) const {
    if (mSamples.size() < 2)
        return 0.0f;
    const Sample &last = mSamples.back();
    // the oldest sample still inside the window
    size_t first = mSamples.size() - 1;
    while (first > 0 && last.t - mSamples[first - 1].t <= p.velocityWindow)
        first--;
    if (first == mSamples.size() - 1)
        first = mSamples.size() - 2; // a window with one sample: the last two
    const float dt = last.t - mSamples[first].t;
    if (dt <= 1e-4f)
        return 0.0f;
    // finger up the screen (y falling) = later rows
    return -(last.y - mSamples[first].y) / dt;
}

void Tracker::Feed(const Input &in, const Params &p) {
    switch (in.kind) {
    case kMove:
        if (mPressed) {
            const float dx = in.x - mDownX, dy = in.y - mDownY;
            if (!mDragging && dx * dx + dy * dy > p.slop * p.slop) {
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
        mSuppressTap = mFlingVelocity != 0.0f; // a press that stops a fling picks nothing
        mFlingVelocity = 0.0f;
        mFlingTravel = 0.0f;
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
            const float v = Velocity(p);
            if (std::fabs(v) >= p.flickVelocity)
                mFlingVelocity = v;
        } else if (!mSuppressTap) {
            mTapPending = true;
            mTapX = mDownX;
            mTapY = mDownY;
        }
        mDragging = false;
        mSuppressTap = false;
        mSamples.clear();
        break;
    case kWheel:
        mFlingVelocity = 0.0f;
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
    const float dt = mLastTick < 0.0f ? 0.0f : std::max(0.0f, now - mLastTick);
    mLastTick = now;

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
