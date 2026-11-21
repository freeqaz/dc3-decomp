#pragma once
// DC3 Native Port - the cursor panel's two player boxes when there is no
// camera (PLAN H-147).
//
// ui/gen/cursor.milo draws one framed box per player side: in the top corners
// of every menu and under each score in a song.  On a 360 the box shows the
// player as the Kinect's depth camera sees them, or a "step into view" figure
// while that side has no player.  Natively the picture comes from a camera
// stream (CameraStream_Native.h: the webcam through the pose server's
// protocol v3, or the page's dc3_camera_push).
//
// With no stream neither state says anything true.  The side whose player is
// present -- the one playing with a pad or the keyboard -- drew an empty
// frame, because its picture is never drawn (MeshFilter.cpp, "depth-image"),
// and the other side drew the figure that asks a player to step in front of a
// sensor that is not there.  So under `auto` the boxes are not drawn until a
// camera publishes a frame, and they go again when the stream stops.
//
// DC3_PLAYER_BOX = auto (default) | on | off.  `on` draws them always, as
// before this file; `off` never draws them, camera or not.  It changes at run
// time: {native_settings set_player_box on}; {native_settings player_box}
// reads it.
//
// Draw time only.  The groups' own `showing` is put back before the draw
// returns, so the scripts that hide and show the boxes (movie_overlay.dta,
// rhythm_battle.dta, the party mode screens) read and write what they always
// did.

#include <string>

class ObjectDir;
class RndDrawable;

namespace NativePlayerBox {

enum Mode { kAuto = 0, kOn = 1, kOff = 2 };

// A mode word, case-insensitive: "auto"; "on", "1", "true", "yes", "always";
// "off", "0", "false", "no", "never".  False for anything else (null and ""
// included), and *mode is then left alone.
bool ParseMode(const char *text, Mode *mode);
const char *ModeName(Mode mode); // "auto" / "on" / "off"

// The rule.  `cameraLive` is NativeCamera::Active().
bool ShouldDraw(Mode mode, bool cameraLive);

// DC3_PLAYER_BOX's value -> the mode a run starts in: auto when it is unset,
// empty or not a mode word (*unknown is set for the last).
Mode ModeFromEnv(const char *value, bool *unknown);

// The mode now: ModeFromEnv(getenv("DC3_PLAYER_BOX")) at the first call (a
// value that is not a mode word is named on stderr), then what SetMode set.
Mode GetMode();
void SetMode(Mode mode);

// ShouldDraw(GetMode(), NativeCamera::Active()).
bool DrawnNow();

// The two groups a scope hides, by name in the cursor panel's dir.
extern const char *const kGroupNames[2];

// ShellInput::Draw holds one around the cursor panel's draw.  While the boxes
// are not to be drawn it hides each of the two groups that is showing, and
// its destructor shows exactly those again.  A group a script has hidden is
// not touched; a dir without the groups, or a null dir, is a no-op.
class DrawScope {
public:
    explicit DrawScope(ObjectDir *cursorDir);
    ~DrawScope();
    int Hidden() const { return mCount; } // groups this scope hid

private:
    DrawScope(const DrawScope &);
    DrawScope &operator=(const DrawScope &);
    RndDrawable *mHidden[2];
    int mCount;
};

// GET /api/camera's "playerBox" member: the mode, whether the boxes are drawn
// now, how many cursor-panel draws ran with them drawn and with them hidden,
// and how many groups the last draw hid.
std::string StateJson();

// Tests: the mode back to "not read yet" and the counters to zero.
void ResetForTest();

} // namespace NativePlayerBox
