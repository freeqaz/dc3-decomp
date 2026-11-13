// DC3 Native Port - losing the X window ends the game loop, not the process (H-9)
//
// What used to happen when the window was destroyed under us (another client
// calling XDestroyWindow -- `xdotool windowclose`, a crashing or killed window
// manager, an X session tearing down a client's windows):
//
//   1. GLFW ignores DestroyNotify, so nothing noticed;
//   2. WgpuRnd::BeginDrawing's glfwGetWindowSize sent XGetWindowAttributes for
//      the dead window, and the X server answered BadWindow;
//   3. no error handler was installed, so Xlib's default one printed
//      "X Error of failed request" and called exit(1);
//   4. exit() ran the static destructors, and ObjDirPtr<ObjectDir>::operator=
//      died of SIGSEGV in one of them (rc 139, measured; np-dogfood2 saw rc 0).
//
// Now, while the window is an X11 one:
//   * every frame, before BeginDrawing, Dc3WindowLostPoll takes a DestroyNotify
//     for our window off the queue (GLFW selects StructureNotifyMask, so the
//     server sends one; GLFW would read and drop it). Seeing it requests a quit
//     (NativeQuit.h) and the loop leaves before drawing;
//   * an X error handler catches what slips past that -- a window destroyed
//     between the check and BeginDrawing's first request -- by treating
//     BadWindow/BadDrawable on OUR window as the same loss, instead of letting
//     Xlib exit. Any other X error still goes to the previous handler.
//
// Either way the process leaves through the main loop's return, the path a
// window close (WM_DELETE_WINDOW -> glfwWindowShouldClose) already takes:
// DC3_EXIT how=main_loop_returned detail=window_lost, rc 0.
//
// This TU includes Xlib and nothing from the decomp: Xlib's macros (None,
// Status, Bool, Success, ...) would collide with decomp headers.

#include "NativeQuit.h"

#include <cstdio>

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>

extern GLFWwindow *gNativeWindow;

namespace {

Display *sDisplay = nullptr;
Window sWindow = 0;
XErrorHandler sPrevHandler = nullptr;
std::atomic<bool> sLost{false};

void NoteWindowLost(const char *how) {
    if (sLost.exchange(true))
        return;
    fprintf(stderr, "DC3 Native: X window 0x%lx is gone (%s) - quitting\n",
            (unsigned long)sWindow, how);
    Dc3RequestQuit("window_lost", 0);
}

// An Xlib error handler may not make X requests; this one only sets flags.
int OnXError(Display *display, XErrorEvent *error) {
    if (display == sDisplay && sWindow != 0 && error->resourceid == sWindow &&
        (error->error_code == BadWindow || error->error_code == BadDrawable)) {
        NoteWindowLost("X error on the window");
        return 0;
    }
    return sPrevHandler ? sPrevHandler(display, error) : 0;
}

bool PollWindowLost() {
    if (!sLost.load() && sDisplay) {
        XEvent ev;
        if (XCheckTypedWindowEvent(sDisplay, sWindow, DestroyNotify, &ev))
            NoteWindowLost("DestroyNotify");
    }
    return sLost.load();
}

} // namespace

void Dc3WindowWatchInstall() {
    if (!gNativeWindow || glfwGetPlatform() != GLFW_PLATFORM_X11)
        return;
    sDisplay = glfwGetX11Display();
    sWindow = glfwGetX11Window(gNativeWindow);
    if (!sDisplay || !sWindow)
        return;
    // GLFW swaps its own handler in and out around the requests it expects to
    // fail, restoring whatever was installed before -- this one.
    sPrevHandler = XSetErrorHandler(OnXError);
    gDc3WindowLostPoll = PollWindowLost;
}

#else

void Dc3WindowWatchInstall() {}

#endif
