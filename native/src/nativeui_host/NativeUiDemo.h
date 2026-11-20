// NativeUiDemo: DC3_NATIVEUI_DEMO=1 draws nativeui's SampleBrowser over the
// running game through the whole host -- the game's real fonts, the engine
// canvas, album art from the game's loader, pad and text input. It is the
// proof that the binding works and the thing the engine-run tests drive; it is
// not a game screen (the game's own menu keeps running underneath and still
// sees the same pad presses).
//
// With the variable unset nothing here runs: no font or texture is loaded, no
// hook is installed, nothing is drawn.
//
//   DemoInstall()    once, before the main loop: reads the variable and, if
//                    set, installs the per-frame poll (src/App.cpp's
//                    gDc3NativeFrameHook) and the overlay draw
//                    (dc3_render_hook.cpp's SetGameOverlayDraw).
//   DemoStateJson()  main thread: what the demo shows, as JSON, for
//                    GET /api/nativeui/state.
#pragma once

#include <string>

namespace nativeui_host {

    void DemoInstall();
    bool DemoActive();
    std::string DemoStateJson();

}
