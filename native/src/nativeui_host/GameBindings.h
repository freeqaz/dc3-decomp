// GameBindings: the three places NativeUiHost meets game types.
//
//   GameUiSeconds()            the game's UI clock (TheTaskMgr.UISeconds()), for
//                              NativeUiHost::Enter / Tick
//   OnButtonDown / OnButtonUp  a panel's button_down / button_up messages
//   SetOutputSize(host)        the renderer's output size, for baseline snapping
#pragma once

#include "nativeui_host/NativeUiHost.h"
#include "os/JoypadMsgs.h"

namespace nativeui_host {

    double GameUiSeconds();

    // True if the UI used the press (see InputBridge.h for the key map).
    bool OnButtonDown(NativeUiHost &host, const ButtonDownMsg &msg);
    void OnButtonUp(NativeUiHost &host, const ButtonUpMsg &msg);

    void SetOutputSize(NativeUiHost &host);

}
