#pragma once

// Pointer input for the game's own menus (branch np-pointer): a mouse on the
// desktop and the web, a finger on the web.
//
//   hover         a mouse over a row of a list highlights it (the list's own
//                 highlight: its sound, its handle_highlight script)
//   click / tap   on a row selects it (the list's own select, as a press of
//                 the pad's A on that row); on a screen with no list drawn (the
//                 title, a movie) it is a press of A
//   right click   a press of B (back)
//   wheel / drag  scrolls the list under the pointer (or the focused one) a
//                 row at a time, with a fling's momentum after a flick; a
//                 press that stops a fling, and a drag, select nothing
//
// The lists are the image's HamNavLists, hit-tested where they were DRAWN
// last frame (HamNavList::DrawShowing reports each row's centre through its
// camera), so every screen built of them is reached without a per-screen
// table.  The natively drawn song browser is reached through its own hook
// (SongBrowserHooks_Native.h).  Everything runs in controller mode: the first
// pointer action takes the menus from the hands, as a key press does.
//
// DC3_POINTER=0 (or {native_settings set_pointer 0}) is the original: the
// pointer does nothing in the game's menus.
//
// The rules (tap / drag / flick, the present mapping, the hit test) are
// PointerGesture.h, tested without a game.

#include "PointerGesture.h"

#include <string>

class HamNavList;
class RndCam;
class Transform;
struct UIListWidgetDrawState;

namespace Dc3Pointer {

bool Enabled();
void SetEnabled(bool on);

// An event in frame pixels (1280x720), from any thread.  `delaySeconds`: feed
// it this long from now (the HTTP endpoint's scripted drags).
// `windowSpace`: x/y are desktop window coordinates, mapped through the present
// scale on the main thread when the event is fed (POST /api/input/pointer
// space=window: the same path a real mouse takes).
void Push(const Dc3PointerGesture::Input &in, float delaySeconds = 0.0f, bool windowSpace = false);

// The clock the events are stamped with (seconds).
float Now();

// A point in the desktop window's coordinates -> the frame, through the
// present scale (GpuDevice::WindowToFrame).  False on a bar.
bool WindowToFrame(double wx, double wy, float &fx, float &fy);

// HamNavList::DrawShowing, just before the list's widgets draw: where its
// rows are this frame.
void NoteListDrawn(HamNavList *list, const UIListWidgetDrawState &state,
                   const Transform &world, const RndCam *cam);
// ~HamNavList: the list is gone.
void Forget(HamNavList *list);

// Once a frame on the main thread (App.cpp, beside the menu-select key).
void FramePoll();

// GET /api/pointer/state: the switch, the lists drawn last frame with their
// rows' rectangles, what the pointer did.
std::string StateJson();

} // namespace Dc3Pointer
