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
    // A press that travels less than this (frame pixels) is a tap.  A phone's
    // frame is drawn small, so this is a little over a finger's wobble there.
    float slop = 16.0f;
    // Release faster than this (frame pixels/s) and the list keeps going.
    float flickVelocity = 700.0f;
    // The fling slows as exp(-friction * t) ...
    float friction = 3.5f;
    // ... and stops under this speed.
    float stopVelocity = 150.0f;
    // A fling covers at most this many rows (a long list is not run off in
    // one flick past the point of reading it).
    float maxFlingRows = 40.0f;
    // Velocity is taken over the last this-many seconds of the drag.
    float velocityWindow = 0.08f;
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
};

struct Output {
    bool hover = false; // a mouse is over (x, y) and moved since the last tick
    float x = 0.0f, y = 0.0f;
    bool tap = false; // a tap: select what is at (tapX, tapY)
    float tapX = 0.0f, tapY = 0.0f;
    int steps = 0;     // rows to scroll: > 0 = later rows
    bool back = false; // the secondary button was pressed
    bool dragging = false;
    bool flinging = false;
};

class Tracker {
public:
    void Feed(const Input &in, const Params &p);
    // Once a frame.  `pitch`: what one row is, in frame pixels, on the list
    // under the pointer (RowPitch).
    Output Tick(float now, float pitch, const Params &p);
    bool Pressed() const { return mPressed; }
    bool Dragging() const { return mDragging; }
    bool Flinging() const { return mFlingVelocity != 0.0f; }
    void Reset() { *this = Tracker(); }

private:
    struct Sample {
        float y, t;
    };
    float Velocity(const Params &p) const;

    bool mPressed = false;
    bool mDragging = false;
    bool mSuppressTap = false; // the press stopped a fling
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
    float mTapX = 0.0f, mTapY = 0.0f;
    bool mBackPending = false;
};

} // namespace Dc3PointerGesture
