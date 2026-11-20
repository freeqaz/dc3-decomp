#include "nativeui_host/InputBridge.h"

namespace nativeui_host {

    using nativeui::Action;

    std::optional<Action> MapPad(int padButton, int padAction) {
        switch (padButton) {
        case kPadDUp:
        case kPadLStickUp:
            return Action::Up;
        case kPadDDown:
        case kPadLStickDown:
            return Action::Down;
        case kPadDLeft:
        case kPadLStickLeft:
            return Action::Left;
        case kPadDRight:
        case kPadLStickRight:
            return Action::Right;
        case kPadX:
            return Action::Accept;
        case kPadCircle:
            return Action::Back;
        case kPadL1:
            return Action::PrevTab;
        case kPadR1:
            return Action::NextTab;
        case kPadL2:
            return Action::PageUp;
        case kPadR2:
            return Action::PageDown;
        case kPadSelect:
            return Action::NextTab;
        case kPadSquare:
        case kPadTri:
        case kPadStart:
            return std::nullopt; // the panel's, whatever action they carry
        default:
            break;
        }
        switch (padAction) {
        case kPadActionConfirm:
            return Action::Accept;
        case kPadActionCancel:
            return Action::Back;
        case kPadActionUp:
            return Action::Up;
        case kPadActionDown:
            return Action::Down;
        case kPadActionLeft:
            return Action::Left;
        case kPadActionRight:
            return Action::Right;
        case kPadActionPageUp:
            return Action::PrevTab;
        case kPadActionPageDown:
            return Action::NextTab;
        default:
            return std::nullopt;
        }
    }

    std::optional<nativeui::Event> MapText(const TextInputEvent &event) {
        switch (event.type) {
        case kTextInputChar:
            if (event.codepoint < 0x20 || event.codepoint == 0x7F) {
                return std::nullopt;
            }
            return nativeui::Event {
                nativeui::TextEvent { static_cast<char32_t>(event.codepoint) }
            };
        case kTextInputBackspace:
            return nativeui::Event { nativeui::EditEvent { nativeui::Edit::Backspace } };
        case kTextInputDelete:
            return nativeui::Event { nativeui::EditEvent { nativeui::Edit::Delete } };
        case kTextInputHome:
            return nativeui::Event { nativeui::EditEvent { nativeui::Edit::CursorHome } };
        case kTextInputEnd:
            return nativeui::Event { nativeui::EditEvent { nativeui::Edit::CursorEnd } };
        case kTextInputLeft:
        case kTextInputRight:
        default:
            return std::nullopt; // the pad's Left/Right moves the caret
        }
    }

    unsigned DirectionRepeater::Bit(Action direction) {
        switch (direction) {
        case Action::Up:
            return 1u;
        case Action::Down:
            return 2u;
        case Action::Left:
            return 4u;
        case Action::Right:
            return 8u;
        default:
            return 0u;
        }
    }

    bool DirectionRepeater::Press(Action direction, double now) {
        const unsigned bit = Bit(direction);
        if (bit == 0u) {
            return true; // not a direction: nothing to time
        }
        if (held_ & bit) {
            return false;
        }
        held_ |= bit;
        repeating_ = true;
        current_ = direction;
        nextFire_ = now + config_.initialDelay;
        return true;
    }

    void DirectionRepeater::Release(Action direction) {
        const unsigned bit = Bit(direction);
        held_ &= ~bit;
        if (repeating_ && current_ == direction) {
            repeating_ = false; // an older direction still held does not resume
        }
    }

    void DirectionRepeater::ReleaseAll() {
        held_ = 0;
        repeating_ = false;
    }

    void DirectionRepeater::Tick(double now, std::vector<Action> &out, int maxPerCall) {
        if (!repeating_ || config_.interval <= 0.0) {
            return;
        }
        int fired = 0;
        while (now >= nextFire_ && fired < maxPerCall) {
            out.push_back(current_);
            nextFire_ += config_.interval;
            fired++;
        }
        if (now >= nextFire_) {
            // A hitch longer than the burst limit: do not owe the difference.
            nextFire_ = now + config_.interval;
        }
    }

}
