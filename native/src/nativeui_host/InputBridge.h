// InputBridge: the game's pad messages and the engine's text queue as
// nativeui events. Pure logic: pad buttons arrive as the game's JoypadButton /
// JoypadAction NUMBERS (GameBindings.cpp static_asserts that the mirrors below
// are the game's values) and time arrives as seconds, so tests drive it with
// no game and no keyboard.
//
// THE KEY MAP (pad button -> nativeui action; the keyboard column is the
// engine's pad map, platform/JoypadKeyMap.h)
//
//   pad                         keyboard        nativeui
//   D-pad / left stick up       Up              Action::Up        (repeats)
//   D-pad / left stick down     Down            Action::Down      (repeats)
//   D-pad / left stick left     Left            Action::Left      (repeats)
//   D-pad / left stick right    Right           Action::Right     (repeats)
//   A   (kPad_X)                Enter           Action::Accept
//   B   (kPad_Circle)           Esc, Backspace* Action::Back
//   LB  (kPad_L1)               Q*              Action::PrevTab
//   RB  (kPad_R1)               E*              Action::NextTab
//   LT  (kPad_L2)               Z*              Action::PageUp
//   RT  (kPad_R2)               C*              Action::PageDown
//   Back (kPad_Select)          Tab             Action::NextTab
//   X, Y, Start, sticks' clicks X*, Y*, Space*  not mapped: OnButtonDown
//                                               returns false and the owning
//                                               panel keeps them
//   * a text key: while text capture is on it types instead (TextInput_Native.h).
//     Arrows, Enter, Esc and Tab keep their pad meaning while typing, so Tab
//     still changes tab and Esc still goes back from a focused text field.
//
// A button the pad's controller mapping gives a navigation ACTION to is mapped
// by that action when its button is not in the table (kAction_Confirm ->
// Accept, kAction_Cancel -> Back, kAction_Up/Down/Left/Right, kAction_PageUp /
// PageDown -> PrevTab / NextTab), so a remapped controller still navigates.
//
// TEXT (engine TextInputEvent -> nativeui)
//   kTextInputChar       TextEvent { codepoint }
//   kTextInputBackspace  EditEvent Backspace
//   kTextInputDelete     EditEvent Delete
//   kTextInputHome/End   EditEvent CursorHome / CursorEnd
//   kTextInputLeft/Right DROPPED: the same key press also arrives as the pad's
//                        Left/Right, which a focused TextField already treats
//                        as cursor movement (nativeui/Input.h). Forwarding
//                        both would move the caret twice.
//
// REPEAT
//   Measured in the game's code and config, not assumed: the UI's JoypadClient
//   (src/system/ui/UI.cpp UseJoypad, src/system/os/JoypadClient.cpp) DOES
//   re-send button_down for a held D-pad direction -- after hold_ms and then
//   every repeat_ms, which DC3's config/joypad.dta sets to 1000 ms and 80 ms.
//   A second's wait before a list starts to run is too slow for a browser, and
//   a host fed from the raw joypad source gets no repeats at all, so the host
//   times its own: a held direction fires again after `initialDelay` and then
//   every `interval`. A button_down that arrives for a direction already held
//   (the game's own repeat) is swallowed, so the two never add up.
#pragma once

#include "nativeui/Input.h"
#include "platform/TextInput_Native.h"

#include <optional>
#include <vector>

namespace nativeui_host {

    // Mirrors of the game's JoypadButton (src/system/os/Joypad.h).
    enum PadButton {
        kPadL2 = 0,
        kPadR2 = 1,
        kPadL1 = 2,
        kPadR1 = 3,
        kPadTri = 4,
        kPadCircle = 5,
        kPadX = 6,
        kPadSquare = 7,
        kPadSelect = 8,
        kPadStart = 11,
        kPadDUp = 12,
        kPadDRight = 13,
        kPadDDown = 14,
        kPadDLeft = 15,
        kPadLStickUp = 16,
        kPadLStickRight = 17,
        kPadLStickDown = 18,
        kPadLStickLeft = 19
    };

    // Mirrors of the game's JoypadAction.
    enum PadAction {
        kPadActionNone = 0,
        kPadActionConfirm = 1,
        kPadActionCancel = 2,
        kPadActionUp = 6,
        kPadActionRight = 7,
        kPadActionDown = 8,
        kPadActionLeft = 9,
        kPadActionPageUp = 10,
        kPadActionPageDown = 11
    };

    // The nativeui action of a pad press, or nothing for a button the UI
    // does not use.
    std::optional<nativeui::Action> MapPad(int padButton, int padAction);

    // The nativeui event of an engine text event, or nothing (see TEXT above).
    std::optional<nativeui::Event> MapText(const TextInputEvent &event);

    inline bool IsDirection(nativeui::Action action) {
        return action == nativeui::Action::Up || action == nativeui::Action::Down
            || action == nativeui::Action::Left || action == nativeui::Action::Right;
    }

    struct RepeatConfig {
        double initialDelay = 0.35; // seconds a direction is held before it repeats
        double interval = 0.08; // seconds between repeats after that
    };

    // Held-direction repeat. One direction repeats at a time: the one pressed
    // last. Time is the host's clock, in seconds.
    class DirectionRepeater {
    public:
        explicit DirectionRepeater(RepeatConfig config = RepeatConfig()) : config_(config) {}
        void SetConfig(RepeatConfig config) { config_ = config; }

        // A direction went down at `now`. True if this is a new press (the
        // caller sends the action); false if that direction was already held
        // (a repeat from the game: swallow it).
        bool Press(nativeui::Action direction, double now);
        void Release(nativeui::Action direction);
        void ReleaseAll();
        // Appends the repeats due by `now` to `out` (at most `maxPerCall`, so a
        // long hitch cannot fire a burst).
        void Tick(double now, std::vector<nativeui::Action> &out, int maxPerCall = 4);
        bool Holding() const { return held_ != 0; }

    private:
        static unsigned Bit(nativeui::Action direction);

        RepeatConfig config_;
        unsigned held_ = 0; // bit per direction
        bool repeating_ = false;
        nativeui::Action current_ = nativeui::Action::Up;
        double nextFire_ = 0.0;
    };

}
