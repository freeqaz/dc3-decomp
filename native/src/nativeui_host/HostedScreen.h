// HostedScreen: what NativeUiHost runs.
//
// The host needs four things of a screen: give it an event, build its frame,
// ask whether it wants the keyboard for text, and start its entrance.  A
// nativeui::Screen does all four; so does an object that owns one and is the
// screen as far as its user is concerned (songbrowser_ui::SongBrowserScreen).
// NativeUiHost::SetScreen takes either: a nativeui::Screen directly, or
// anything else through HostedScreenOf.
//
//     songbrowser_ui::SongBrowserScreen browser(...);
//     nativeui_host::HostedScreenOf<songbrowser_ui::SongBrowserScreen> hosted(browser);
//     host.SetScreen(&hosted);
#pragma once

#include "nativeui/DisplayList.h"
#include "nativeui/Input.h"

namespace nativeui_host {

    class HostedScreen {
    public:
        virtual ~HostedScreen() = default;
        // True if the event did something.
        virtual bool HandleEvent(const nativeui::Event &event) = 0;
        // Advances to the host clock's time, lays out and paints.  The list
        // is valid until the next call.
        virtual const nativeui::DisplayList &BuildFrame() = 0;
        // Whether the host should hold the engine's text capture right now
        // (under TextCapture::WhenTextFocused).
        virtual bool WantsTextInput() const = 0;
        // NativeUiHost::Enter: start the entrance, if the screen has one.
        virtual void PlayEnter() {}
    };

    // Any object with HandleEvent / BuildFrame / WantsTextInput, borrowed.
    template <class T>
    class HostedScreenOf final : public HostedScreen {
    public:
        explicit HostedScreenOf(T &screen) : screen_(screen) {}
        bool HandleEvent(const nativeui::Event &event) override {
            return screen_.HandleEvent(event);
        }
        const nativeui::DisplayList &BuildFrame() override { return screen_.BuildFrame(); }
        bool WantsTextInput() const override { return screen_.WantsTextInput(); }

    private:
        T &screen_;
    };

}
