#pragma once

#include <string>

// Player-facing notices from the native platform layer -- things the 360 would
// have told the player through the system UI (no Kinect sensor, sensor
// unplugged) and that the decompiled game has no screen for, because on the
// console the dashboard owned them.
//
// A notice is shown as a banner across the top of the screen for
// `bannerSeconds` from the first frame that draws it, then, if it has a short
// form, as one line in the corner for as long as it stays posted.  Posting is thread-safe (the pose socket reader
// posts from its own thread); every post is also printed to stderr, so a
// headless run's log carries it too.  Drawn by DebugPanel::DrawNotices from the
// ImGui overlay pass in App.cpp, independently of the debug panel's own
// visibility.
namespace NativeNotice {

// Post (or replace) the notice under `key`.
void Post(const char *key, const std::string &text, const std::string &shortText,
          float bannerSeconds);

// Remove the notice under `key` (no-op if absent).
void Clear(const char *key);

struct View {
    bool banner = false;    // draw `text` as a banner
    bool corner = false;    // draw `shortText` in the corner
    float bannerAlpha = 0;  // fades over the banner's last second
    std::string text;
    std::string shortText;
};

// What to draw now: the most recently posted notice.  False if none.
bool Current(View &out);

// The text of the notice under `key`, or "" (for tests and the HTTP server).
std::string Text(const char *key);

} // namespace NativeNotice
