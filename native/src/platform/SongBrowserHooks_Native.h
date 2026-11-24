#pragma once
// DC3 Native Port - where the image's song select meets the natively drawn
// song browser (docs/native/SONG_SELECT.md, "The song browser").
//
// SongSelectPanel, HelpBarPanel, ShellInput, HamUI and HttpServer call the functions
// below.  They are defined in SongSelect_Native.cpp, which every native binary
// links, and forward to the browser when the binary has one: dc3-native,
// dc3-web and milo-tests list SongBrowserPanel_Native.cpp, which installs
// itself at static-init time.  In a binary without it (milo-viewer, the export
// tools) every one of them is a no-op that says "the classic list".

#include "obj/Data.h"

#include <string>

class UIPanel;

namespace NativeSongBrowser {

// SongSelectPanel::Enter / Exit / Poll, after the image's own.
void OnPanelEnter(UIPanel *panel);
void OnPanelExit(UIPanel *panel);
void OnPanelPoll(UIPanel *panel);

// SongSelectPanel::Draw.  True: the browser is what this visit shows right
// now (it drew itself, in the first draw pass), so the classic list must not
// draw.
bool OnPanelDraw(UIPanel *panel);

// First in SongSelectPanel::Handle.  Anything but kDataUnhandled: the browser
// took the message (a pad press while it is shown).
DataNode OnPanelHandle(UIPanel *panel, DataArray *msg);

// True while the browser is drawn: the help bar and the cursor panel (the
// skeleton box and the player camera box in the screen's top corners) are not.
bool HidesGameChrome();

// The body of GET /api/songbrowser/state.
std::string StateJson();

// HamUI::OnMsg(ButtonDownMsg): every pad press of the run, on any screen, as
// it arrives and before anything handles it.  The browser's hint bar names
// the device last used (keys, or a pad's buttons); this is where it learns
// which one a press came from.
void OnAnyButtonDown(int padNum);

// POST /api/input/press with "device":"pad": the press it is about to send
// stands for a real pad's (a headless run has no pad to press).
void NoteInjectedPadPress();

// POST /api/songbrowser/command: runs the browser's command `name`
// ("random_song" or "playlists") exactly as the screen's onCommand does.
// False with `error` set when it did not run (the browser is not shown, the
// name is unknown, the classic list has no such row in this mode).
bool RunCommand(const std::string &name, std::string &error);

// The mouse or a finger tapped (x, y) in the 1280x720 frame while the
// browser is shown (Pointer_Native.cpp).  2: it was on a row of the song
// list, and that row is now picked, as the pad's Down/Up and A would.  With
// `firstHighlights` (a finger) a row that was not the selection only becomes
// it: 1.  3: on a sort tab, which is now the sort (already it: no change).
// 0: off the rows and the tabs (nothing done).
int PointerTap(float x, float y, bool firstHighlights);
// The mouse rests over (x, y) while the browser is shown and the menus are
// the pad's (controller mode): the row under it becomes the selection, as
// the pad's Down/Up would; nothing is picked.  True when on a song row.
bool PointerHover(float x, float y);

struct Hooks {
    void (*enter)(UIPanel *);
    void (*exit)(UIPanel *);
    void (*poll)(UIPanel *);
    bool (*draw)(UIPanel *);
    DataNode (*handle)(UIPanel *, DataArray *);
    bool (*hidesChrome)();
    std::string (*stateJson)();
    void (*anyButtonDown)(int);
    void (*injectedPadPress)();
    bool (*command)(const std::string &, std::string &);
    int (*pointerTap)(float, float, bool);
    bool (*pointerHover)(float, float);
};
// Called once, by the browser's translation unit.
void Install(const Hooks *hooks);

} // namespace NativeSongBrowser
