#pragma once

// Menu gestures on a webcam (PLAN P-3, branch np-menu-gestures).
//
// DC3's shell lists are HamNavLists driven by the image's own detectors
// (HandHeightGestureFilter, DirectionGestureFilterSingleUser / ArcDetector);
// natively the skeleton comes from a webcam through MediaPipe instead of a
// Kinect.  This is the native side of measuring that: what every polled list
// is doing this frame and every select with its source, for
// scripts/menu_gesture_bench.py (GET /api/menu/state).  Read-only: nothing
// here changes what a list does.

#include <string>

class HamNavList;

namespace Dc3MenuGesture {

// What made a list select.  The image has two: a swipe
// (HamNavList::UpdateGestures -> SetSelecting(false)) and a controller
// confirm (OnMsg(ButtonDownMsg) -> SetSelecting(true)).
enum Source {
    kSourceSwipe = 0,
    kSourceButton = 1,
};

// HamNavList::SetSelecting, once per select that went through.
void NoteSelect(const HamNavList *list, int selected, int firstShowing, const char *sym,
                bool byButton);

// HamNavList::PostUpdate, once per skeleton frame the list's gesture filters
// were updated with (camera frames and DC3_POSE_FILL frames alike).
void NoteSensorUpdate(const HamNavList *list);

// The end of HamNavList::Poll, once per frame per polled list.
void NotePoll(HamNavList *list);

// {"ui_seconds":..,"controller_mode":..,"selects":N,"lists":[..],"events":[..]}
// -- the lists polled within the last 0.2 UI seconds and every select with
// seq > `since` (the last 256 are kept).  `reset` clears each list's running
// maxima and counters after reading them.  `rows`: also each list's data
// symbols, captured on its next poll (so ask, wait a frame, ask again).
// Main thread only.
std::string StateJson(unsigned since, bool reset, bool rows);

} // namespace Dc3MenuGesture
