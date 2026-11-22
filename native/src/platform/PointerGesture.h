#pragma once

// Pointer input for the game's own menus (branch np-pointer): the rules alone.
//
// A mouse (desktop and web) or a finger (web) is turned into four things the
// menus understand: a hover position, a tap at a position, a number of rows
// to scroll, and "back".  Nothing here knows a list, a clock or a device:
// Pointer_Native.cpp feeds it events and acts on what it says, and
// native/tests/test_pointer_gesture.cpp drives it directly.
//
// Coordinates are the game's frame (the Rnd virtual resolution, 1280x720),
// after the present scaling has been undone (BoxToFrame).

#include <vector>

namespace Dc3PointerGesture {

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

// The largest frameW:frameH rectangle that fits a boxW x boxH box, centred:
// the engine's present rule (gfx/PresentScale FitPresentRect, PLAN H-18), so a
// window that is not 16:9 has bars and the picture is in between.
Rect FitFrame(float frameW, float frameH, float boxW, float boxH);

// A point in a box (window pixels, or canvas CSS pixels) -> the frame.
// `letterbox`: the picture is FitFrame's rectangle in the box (desktop under
// the present scale); otherwise it is stretched over the whole box (the web
// canvas, whose buffer the page sizes).  False when the point is on a bar or
// outside the box; fx/fy are still written (clamped is the caller's choice).
bool BoxToFrame(float bx, float by, float boxW, float boxH, float frameW, float frameH,
                bool letterbox, float &fx, float &fy);

// A point on the presenting surface (window framebuffer or canvas buffer
// pixels) -> the frame, through `present`: where the engine last put the frame
// on that surface (GpuDevice::LastPresentRect -- the bars, the scale, a canvas
// of any shape).  False outside it.
bool PresentToFrame(float sx, float sy, const Rect &present, float frameW, float frameH,
                    float &fx, float &fy);

// A row of a list as drawn this frame, in frame pixels: centre and half-size.
struct Row {
    int index = -1; // what the list calls the row (HamNavList: its showing index)
    float cx = 0.0f, cy = 0.0f;
    float halfW = 0.0f, halfH = 0.0f;
    bool active = true; // a header or a disabled row is not a target
};

// The active row under (x, y), or -1.  Rows that overlap (a swelled row) go
// to the one whose centre is nearest.
int HitRow(const std::vector<Row> &rows, float x, float y);

// The vertical distance between neighbouring rows' centres (the median), or
// `fallback` with fewer than two rows.  What a drag of one row is.
float RowPitch(const std::vector<Row> &rows, float fallback);

struct Params {
    // A press that travels less than this (frame pixels) is a tap.
    float slop = 16.0f;
    // ... and for a finger, at least this many of the screen's own pixels
    // (CSS pixels on the web; Input::unitPx says how many frame pixels one
    // is).  A phone held upright draws the 1280-wide frame ~412 CSS px wide, so
    // 16 frame px is 5 CSS px there -- under a finger's wobble (Fable review).
    float touchSlopUnits = 10.0f;
    // Release faster than this (frame pixels/s) and the list keeps going.
    float flickVelocity = 700.0f;
    // The fling slows as exp(-friction * t) ...
    float friction = 3.5f;
    // ... and stops under this speed.
    float stopVelocity = 150.0f;
    // A fling covers at most this many rows (a long list is not run off in
    // one flick past the point of reading it).
    float maxFlingRows = 40.0f;
    // Velocity at release is the travel over [t_up - window, t_up] (the
    // finger's position interpolated between samples): a finger that rested
    // for the window before lifting has none.
    float velocityWindow = 0.08f;
    // ... over at least this long (a burst of events with near-equal times is
    // not a 10000 px/s flick).
    float minVelocityDt = 0.01f;
    // A fling advances by at most this much time a tick (a 3 s frame gap was
    // 149 rows in one tick before this).
    float maxTickDt = 0.05f;
    // A scroll remainder (part of a row, a part-notch of the wheel) older than
    // this is dropped: it does not join the next gesture.
    float remainderIdle = 0.5f;
};

enum Kind {
    kMove,   // the pointer is here (button state unchanged)
    kDown,   // primary button / finger down here
    kUp,     // primary button / finger up here
    kWheel,  // `wheel` rows: > 0 = down the list (later rows)
    kCancel, // the pointer left, or the browser took the touch: no tap
    kBack,   // secondary button: the menu's back
};

struct Input {
    Kind kind = kMove;
    float x = 0.0f, y = 0.0f; // frame pixels
    float t = 0.0f;           // seconds, any monotonic clock
    float wheel = 0.0f;
    bool touch = false;       // a finger: no hover
    float unitPx = 1.0f;      // frame pixels per screen pixel (CSS px on the web)
};

struct Output {
    bool hover = false; // a mouse is over (x, y) and moved since the last tick
    float x = 0.0f, y = 0.0f;
    bool tap = false; // a tap: select what is at (tapX, tapY)
    float tapX = 0.0f, tapY = 0.0f;
    bool tapTouch = false; // the tap was a finger's (Pointer_Native: the first highlights)
    bool stopped = false;  // a press stopped the list moving (a fling, or rows owed)
    int steps = 0;     // rows to scroll: > 0 = later rows
    bool back = false; // the secondary button was pressed
    bool dragging = false;
    bool flinging = false;
};

// Gesture safety (the GPD: a camera, the menus on the hands, a mouse that may
// be bumped).  Whether what the pointer did this frame takes the menus from the
// hands (controller mode): only a deliberate act -- a tap or click, the back
// button, a scroll by wheel or drag.  A mouse that moves, or rests, never does,
// and nor does a drag on a frame that scrolls nothing (a finger resting on the
// glass).
bool TakesTheMenus(const Output &out);
// Whether a hover may move a list's highlight: only while the menus are
// already the pad's, keyboard's or pointer's.  Under the hands, the hand
// cursor owns the highlight and a passing mouse leaves it alone.
bool HoverMayHighlight(bool controllerMode);

class Tracker {
public:
    void Feed(const Input &in, const Params &p);
    // Once a frame.  `pitch`: what one row is, in frame pixels, on the list
    // under the pointer (RowPitch).
    Output Tick(float now, float pitch, const Params &p);
    bool Pressed() const { return mPressed; }
    bool Dragging() const { return mDragging; }
    bool Flinging() const { return mFlingVelocity != 0.0f; }
    // The last drag's release: its velocity (frame px/s), how many samples
    // and over how long they ran, and how many drags have been released.
    float ReleaseVelocity() const { return mReleaseV; }
    int ReleaseSamples() const { return mReleaseN; }
    float ReleaseSpan() const { return mReleaseSpan; }
    unsigned Releases() const { return mReleases; }
    // Whether the list is still moving on its own (rows owed from a fling the
    // caller is still running, a scroll animation).  A press then stops it and
    // picks nothing, as a press that stops this tracker's own fling does.
    void SetListMoving(bool moving) { mListMoving = moving; }
    void Reset() { *this = Tracker(); }

private:
    struct Sample {
        float y, t;
    };
    float Velocity(float tUp, const Params &p) const;

    bool mPressed = false;
    bool mDragging = false;
    bool mSuppressTap = false; // the press stopped a fling
    bool mListMoving = false;
    float mReleaseV = 0.0f, mReleaseSpan = 0.0f;
    int mReleaseN = 0;
    unsigned mReleases = 0;
    bool mStoppedPending = false;
    bool mTouch = false;       // the press is a finger's
    float mSlop = 16.0f;       // this press's slop, frame pixels
    float mLastScrollT = -1.0f; // when mPixels / mWheelRows last grew
    float mDownX = 0.0f, mDownY = 0.0f;
    float mLastY = 0.0f;
    std::vector<Sample> mSamples;

    // Scroll waiting to become whole rows, in frame pixels (> 0 = later rows).
    float mPixels = 0.0f;
    float mWheelRows = 0.0f;
    float mFlingVelocity = 0.0f; // pixels/s, > 0 = later rows
    float mFlingTravel = 0.0f;
    float mLastTick = -1.0f;

    bool mHoverPending = false;
    float mHoverX = 0.0f, mHoverY = 0.0f;
    bool mTapPending = false;
    bool mTapTouch = false;
    float mTapX = 0.0f, mTapY = 0.0f;
    bool mBackPending = false;
};

} // namespace Dc3PointerGesture
