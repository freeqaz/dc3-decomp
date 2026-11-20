// Input: the one event type the framework understands.
//
// Rules:
//  - Events are semantic. The integration layer turns pads, keys and gestures
//    into these; no widget ever sees a key code or a button.
//  - An Event is exactly one of four things:
//      ActionEvent   a navigation or command action
//      TextEvent     one typed Unicode code point
//      EditEvent     an editing key that is not a character
//      PointerEvent  a position and a pressed flag (reserved: defined so hand
//                    and mouse input have a place to arrive, but no widget
//                    handles it yet and there is no hit testing)
//  - Routing (Focus.h): an event goes to the focused widget, then up through
//    its ancestors until one returns true ("consumed").
//  - A physical arrow key should be sent as an ActionEvent. A focused
//    TextField treats Left/Right/Home/End actions as cursor movement; the
//    EditEvent cursor kinds exist for sources that mean the cursor and nothing
//    else (an on-screen keyboard, say).
#pragma once

#include "nativeui/Geometry.h"

#include <variant>

namespace nativeui {

    enum class Action {
        Up,
        Down,
        Left,
        Right,
        Accept,
        Back,
        PageUp,
        PageDown,
        NextTab,
        PrevTab,
        Home,
        End
    };

    enum class Edit {
        Backspace,
        Delete,
        CursorLeft,
        CursorRight,
        CursorHome,
        CursorEnd
    };

    struct ActionEvent {
        Action action = Action::Accept;
    };

    struct TextEvent {
        char32_t codePoint = 0;
    };

    struct EditEvent {
        Edit edit = Edit::Backspace;
    };

    struct PointerEvent {
        Vec2 position; // logical coordinates
        bool pressed = false;
    };

    using Event = std::variant<ActionEvent, TextEvent, EditEvent, PointerEvent>;

}
