// Screen: one screen of UI. It owns the widget tree and is the only object the
// integration layer talks to each frame:
//
//   screen.HandleEvent(event);                 // for every input event
//   const DisplayList &list = screen.BuildFrame();
//   Replay(list, renderer);                    // Renderer.h
//
// BuildFrame reads the clock, advances animations by the time since the last
// frame, lays the tree out in the viewport, and paints it.
//
// Event routing, in order:
//   1. the focused widget, then its ancestors (Focus.h);
//   2. if a directional action is still unconsumed, focus moves that way;
//   3. onUnhandledEvent, the screen author's hook for everything else
//      (type-to-search, global tab switching, Back).
//
// Rules:
//  - The theme is copied in; the text measurer and the clock are borrowed and
//    must outlive the screen.
//  - Nothing here assumes a viewport size. The game's is 1280x720.
//  - AtRest() is true when no animation is running: the next frame would be
//    identical to the last one unless an event arrives.
#pragma once

#include "nativeui/Animation.h"
#include "nativeui/Clock.h"
#include "nativeui/DisplayList.h"
#include "nativeui/Focus.h"
#include "nativeui/Input.h"
#include "nativeui/Theme.h"
#include "nativeui/Widget.h"

#include <functional>
#include <memory>

namespace nativeui {

    class Screen {
    public:
        Screen(Size viewport, Theme theme, const TextMeasurer &text, const Clock &clock);
        Screen(const Screen &) = delete;
        Screen &operator=(const Screen &) = delete;

        // Replaces the tree. Focus goes to the first focusable widget in it.
        void SetRoot(std::unique_ptr<Widget> root);
        Widget *Root() const { return root_.get(); }

        const Theme &GetTheme() const { return theme_; }
        Size Viewport() const { return viewport_; }
        void SetViewport(Size viewport) { viewport_ = viewport; }

        Widget *Focused() const { return focus_.Focused(); }
        void SetFocus(Widget *widget) { focus_.SetFocus(widget); }

        // True if anything consumed the event.
        bool HandleEvent(const Event &event);
        std::function<bool(const Event &)> onUnhandledEvent;

        // Starts the entrance transition: the whole screen fades in while sliding
        // up into place.
        void PlayEnter();

        const DisplayList &BuildFrame();
        bool AtRest() const;

    private:
        Size viewport_;
        Theme theme_;
        const TextMeasurer &text_;
        const Clock &clock_;
        std::unique_ptr<Widget> root_;
        FocusManager focus_;
        Tween enter_ { 1.0f }; // 0 = not arrived, 1 = in place
        bool enterPending_ = false;
        bool hasLastTime_ = false;
        double lastTime_ = 0.0;
        DisplayList frame_;
    };

}
