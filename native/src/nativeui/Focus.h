// Focus: which widget receives events, and how focus moves.
//
// Rules:
//  - At most one widget has focus. Only widgets whose IsFocusable() is true
//    can take it.
//  - Dispatch sends an event to the focused widget and then to each ancestor
//    until one consumes it (returns true from HandleEvent).
//  - MoveFocus is spatial: it picks the focusable widget nearest in the given
//    direction, by arranged bounds, preferring ones that line up with the
//    current one. A screen calls it when a directional action came back
//    unconsumed, so a list keeps Up/Down until it runs out of rows and only
//    then lets focus leave.
//  - The focused widget must stay alive while it has focus. Clear the focus
//    before removing the subtree it lives in.
#pragma once

#include "nativeui/Input.h"
#include "nativeui/Widget.h"

namespace nativeui {

    enum class Direction {
        Up,
        Down,
        Left,
        Right
    };

    // The focusable widget under `root` nearest to `from` in `direction`, or null.
    Widget *FindFocusNeighbour(Widget &root, const Widget &from, Direction direction);

    // The first focusable widget under `root` in tree order, or null.
    Widget *FirstFocusable(Widget &root);

    class FocusManager {
    public:
        Widget *Focused() const { return focused_; }
        // Null clears the focus. A widget that is not focusable is ignored.
        void SetFocus(Widget *widget);

        // Routes `event` from the focused widget up; true if something consumed it.
        bool Dispatch(const Event &event);

        // Moves focus to the nearest focusable widget under `root` in `direction`.
        // Returns false, changing nothing, when there is none.
        bool MoveFocus(Widget &root, Direction direction);

    private:
        Widget *focused_ = nullptr;
    };

}
