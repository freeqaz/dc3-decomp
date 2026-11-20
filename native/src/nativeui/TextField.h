// TextField: a single-line editable text box (the search field).
//
// TextEdit is the editing itself, as a plain value: a UTF-8 string and a
// cursor that always sits on a code point boundary. TextField is the widget
// around it.
//
// Behaviour:
//  - TextEvent inserts the code point at the cursor (control characters are
//    ignored and not consumed).
//  - EditEvent: Backspace and Delete remove one whole character; the cursor
//    kinds move it. Edit events are always consumed.
//  - ActionEvent Left/Right move the cursor and are consumed only if it
//    moved, so focus can leave past either end; Home/End jump and are consumed.
//  - onChanged fires after every edit that changed the text, not for SetText.
//  - With no text, the placeholder shows. The cursor shows only while the
//    field has focus, and does not blink (a blinking cursor would mean a
//    screen is never at rest). Text longer than the field scrolls to keep the
//    cursor in view.
//
// The stored text is exactly what was typed; the theme's body style decides
// the case it is shown in.
#pragma once

#include "nativeui/Animation.h"
#include "nativeui/Icon.h"
#include "nativeui/Widget.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace nativeui {

    struct TextEdit {
        std::string text;
        std::size_t cursor = 0; // byte offset, always on a code point boundary

        // Each returns true if it changed the text or moved the cursor.
        bool Insert(char32_t codePoint);
        bool Backspace();
        bool Delete();
        bool MoveLeft();
        bool MoveRight();
        bool MoveHome();
        bool MoveEnd();
    };

    class TextField final : public Widget {
    public:
        explicit TextField(std::string placeholder);

        const std::string &Text() const { return edit_.text; }
        std::size_t Cursor() const { return edit_.cursor; }
        // Replaces the text and puts the cursor at its end. Does not call onChanged.
        void SetText(std::string text);
        // A small icon at the field's left edge, before the text (a magnifying
        // glass on a search field). It brightens when the field has focus.
        void SetLeadingIcon(Icon icon) { leadingIcon_ = icon; }

        std::function<void(const std::string &text)> onChanged;

        bool IsFocusable() const override { return true; }
        bool WantsTextInput() const override { return true; }
        bool HandleEvent(const Event &event) override;

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnAdvance(const Context &ctx, float deltaSeconds) override;
        void OnPaint(const Context &ctx, Painter &painter) const override;
        bool IsAtRest() const override { return focusAmount_.AtRest(); }

    private:
        bool Edited(bool textChanged);

        TextEdit edit_;
        std::string placeholder_;
        std::optional<Icon> leadingIcon_;
        Spring focusAmount_; // 0 = not focused, 1 = focused
    };

}
