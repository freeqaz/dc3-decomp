#pragma once
// DC3 Native Port - which camera the pose server reads, as a setting
// (docs/native/POSE_INPUT.md "Which camera, and a camera that goes away").
//
// Until this, the only way to pick a camera was DC3_POSE_CAMERA at launch.
// Now the player changes it on Options > Gameplay Settings ("Camera"), over
// HTTP (/api/pose_camera) or with {native_settings set_pose_camera ...}; the
// change takes effect at once (NativeSkeletonProvider::SwitchCamera replaces
// the pose server) and is remembered in one small text file beside the saves,
// like song select's (SongSelectState_Native.h):
//
//     $DC3_SAVE_DIR/pose_camera.txt
//
//     dc3-pose-camera 1
//     camera id:0x220000046d0944     <- what pose_server.py --camera is given
//     name MX Brio                   <- for the notice when it is not plugged in
//
// The cameras come from `pose_server.py --list-cameras tsv`, run under the
// interpreter the pose server runs under.  A choice is saved as the camera's
// IDENTITY ("id:..."), never its index: on macOS OpenCV numbers cameras in
// order of their unique ids, so unplugging one renumbers the others.
//
// At launch: DC3_POSE_CAMERA, when set, wins and the file is left alone;
// otherwise the saved choice; otherwise auto.  A saved camera that is not
// plugged in gives auto for this run, with a notice, and stays saved.
//
// No engine types here on purpose: the unit tests drive the pure half against
// fixed lists and a scratch file (native/tests/test_native_pose_camera.cpp).

#include <string>
#include <vector>

namespace Dc3PoseCamera {

struct Camera {
    std::string spec; // pose_server.py --camera value: "id:<identity>", or a /dev/videoN
    std::string id;   // what stays with the device
    std::string name; // "MX Brio"
};

// ---- pure ------------------------------------------------------------------

// `pose_server.py --list-cameras tsv` -> the cameras, in the server's order
// (the first is the one "auto" opens).  False when the text is not that list
// (no header line); *error is the list's own "error" line, if it has one.
bool ParseList(const std::string &tsv, std::vector<Camera> *cameras, std::string *error = nullptr);
// The entry `what` names -- its spec, its identity (with or without "id:"), or
// its name in any case -- or -1.
int Find(const std::vector<Camera> &cameras, const std::string &what);
// The entry in use under `spec`: auto (or "") is the first; -1 when the list
// is empty or `spec` names nothing in it (an index, a path).
int InUse(const std::vector<Camera> &cameras, const std::string &spec);
// The entry after the one in use, wrapping; -1 on an empty list.
int Next(const std::vector<Camera> &cameras, const std::string &spec);

// The saved choice.  empty() = none (also a saved "auto").
struct Saved {
    std::string spec;
    std::string name;
    bool empty() const { return spec.empty() || spec == "auto"; }
};
Saved ParseSaved(const std::string &text);
std::string FormatSaved(const Saved &saved);
Saved LoadSaved(const std::string &path);                 // a missing file is no choice
bool StoreSaved(const std::string &path, const Saved &saved); // temp file, then rename

enum Source {
    kFromDefault = 0,  // nothing chosen: auto
    kFromEnv = 1,      // DC3_POSE_CAMERA
    kFromSaved = 2,    // the saved choice
    kFromSavedMissing = 3, // a choice is saved, but that camera is not plugged in: auto
    kFromSetting = 4,  // changed during this run
};
const char *SourceName(Source source);

// What a launch uses.  `env` is getenv("DC3_POSE_CAMERA"); `cameras` is the
// list, or null when the box could not be asked (the saved choice is then
// passed on as it is, for the server to resolve).
struct Launch {
    std::string spec = "auto";
    std::string name;   // of the camera, when known
    Source source = kFromDefault;
    std::string notice; // for the player; "" = nothing to say
};
Launch ResolveLaunch(const char *env, const Saved &saved, const std::vector<Camera> *cameras);

// ---- this process ----------------------------------------------------------

// $DC3_SAVE_DIR/pose_camera.txt.
std::string SavedPath();
// GestureMgr_NativeInit: decide this launch's camera (once; later calls return
// the same).  Posts the notice, if any.
const Launch &Boot();

// Whether the setting means anything now: a desktop build whose pose provider
// launched pose_server.py on a camera -- not web (the browser owns the
// camera), not DC3_POSE=off, DC3_POSE_NO_SPAWN, DC3_POSE_VIDEO or the
// in-process performer.  The options row is shown on this.
bool Available();
// This box's cameras.  `refresh` runs the lister again (about 50 ms on Linux;
// macOS runs system_profiler); otherwise the last answer, listing once if
// there is none.
const std::vector<Camera> &List(bool refresh = true);
std::string Current();     // the --camera spec in force ("auto", "id:...", ...)
std::string CurrentName(); // "MX Brio"; "MX Brio (automatic)" under auto; "" when unknown
Source CurrentSource();
// Switch to the camera `what` names (Find), to "auto", or to a raw --camera
// value (an index, a path, "id:...").  Restarts the pose server on it, saves
// the choice and posts a notice naming the camera.  False (with a notice)
// when `what` names nothing, or the setting is not Available().
bool Set(const std::string &what);
// Move to the next camera.  With one camera -- the one in use -- nothing is
// restarted or saved; the notice says it is the only one.
bool Cycle();
// One line for the options screen: the camera in use and what selecting does.
std::string Description();
// {"available":..,"current":..,"name":..,"source":..,"saved":..,"cameras":[..]}
std::string StateJson(bool refresh);
// Once a frame (App.cpp): takes the notice down when its time is up.
void FramePoll();

// Tests: stand in for running the lister (returns its tsv output; "" = the
// box could not be asked) and for the saved file's path, and forget
// everything decided so far (the two stand-ins are kept).
void SetListerForTest(std::string (*lister)());
void SetSavedPathForTest(const std::string &path);
void ResetForTest();

} // namespace Dc3PoseCamera
