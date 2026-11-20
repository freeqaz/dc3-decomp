#include "nativeui/TextField.h"

#include "nativeui/Decor.h"
#include "nativeui/Utf8.h"

#include <algorithm>

namespace nativeui {

    // ---------------------------------------------------------------------------
    // TextEdit
    // ---------------------------------------------------------------------------

    bool TextEdit::Insert(char32_t codePoint) {
        const bool control = codePoint < 0x20 || (codePoint >= 0x7F && codePoint < 0xA0);
        if (control) {
            return false;
        }
        std::string encoded;
        utf8::Append(encoded, codePoint);
        text.insert(cursor, encoded);
        cursor += encoded.size();
        return true;
    }

    bool TextEdit::Backspace() {
        if (cursor == 0) {
            return false;
        }
        const std::size_t start = utf8::Prev(text, cursor);
        text.erase(start, cursor - start);
        cursor = start;
        return true;
    }

    bool TextEdit::Delete() {
        if (cursor >= text.size()) {
            return false;
        }
        text.erase(cursor, utf8::Next(text, cursor) - cursor);
        return true;
    }

    bool TextEdit::MoveLeft() {
        const std::size_t to = utf8::Prev(text, cursor);
        const bool moved = to != cursor;
        cursor = to;
        return moved;
    }

    bool TextEdit::MoveRight() {
        const std::size_t to = utf8::Next(text, cursor);
        const bool moved = to != cursor;
        cursor = to;
        return moved;
    }

    bool TextEdit::MoveHome() {
        const bool moved = cursor != 0;
        cursor = 0;
        return moved;
    }

    bool TextEdit::MoveEnd() {
        const bool moved = cursor != text.size();
        cursor = text.size();
        return moved;
    }

    // ---------------------------------------------------------------------------
    // TextField
    // ---------------------------------------------------------------------------

    namespace {

        // The leading icon's side as a share of the text size.
        constexpr float kLeadingIconShare = 0.72f;

    }

    TextField::TextField(std::string placeholder)
        : placeholder_(std::move(placeholder)) {}

    void TextField::SetText(std::string text) {
        edit_.text = std::move(text);
        edit_.cursor = edit_.text.size();
    }

    bool TextField::Edited(bool textChanged) {
        if (textChanged && onChanged) {
            onChanged(edit_.text);
        }
        return textChanged;
    }

    bool TextField::HandleEvent(const Event &event) {
        if (const TextEvent *typed = std::get_if<TextEvent>(&event)) {
            return Edited(edit_.Insert(typed->codePoint));
        }
        if (const EditEvent *edit = std::get_if<EditEvent>(&event)) {
            switch (edit->edit) {
            case Edit::Backspace:
                Edited(edit_.Backspace());
                break;
            case Edit::Delete:
                Edited(edit_.Delete());
                break;
            case Edit::CursorLeft:
                edit_.MoveLeft();
                break;
            case Edit::CursorRight:
                edit_.MoveRight();
                break;
            case Edit::CursorHome:
                edit_.MoveHome();
                break;
            case Edit::CursorEnd:
                edit_.MoveEnd();
                break;
            }
            return true;
        }
        if (const ActionEvent *action = std::get_if<ActionEvent>(&event)) {
            switch (action->action) {
            case Action::Left:
                return edit_.MoveLeft();
            case Action::Right:
                return edit_.MoveRight();
            case Action::Home:
                edit_.MoveHome();
                return true;
            case Action::End:
                edit_.MoveEnd();
                return true;
            default:
                return false;
            }
        }
        return false;
    }

    Size TextField::OnMeasure(const Context &ctx, const Constraints &constraints) {
        return { constraints.Biggest().width, ctx.theme.metric.controlHeight };
    }

    void TextField::OnAdvance(const Context &ctx, float deltaSeconds) {
        focusAmount_.SetTarget(HasFocus() ? 1.0f : 0.0f);
        focusAmount_.Advance(deltaSeconds, ctx.theme.motion.fadeSpring);
    }

    void TextField::OnPaint(const Context &ctx, Painter &painter) const {
        const Theme &theme = ctx.theme;
        const TextStyle &style = theme.type.body;
        const Rect &bounds = Bounds();
        const float focus = focusAmount_.Value();

        PaintFocusGlow(painter, theme, bounds, theme.radius.medium, focus);
        painter.FillRoundRect(
            bounds, theme.radius.medium, CornerColors::Solid(theme.color.fieldFill)
        );
        painter.StrokeRoundRect(
            bounds,
            theme.radius.medium,
            CornerColors::Solid(Mix(theme.color.fieldBorder, theme.color.accent, focus)),
            theme.metric.borderWidth
        );

        Rect inner = bounds.Deflated(Insets::Symmetric(theme.space.md, 0.0f));
        if (leadingIcon_) {
            // The icon is as tall as the text's capitals look, and brightens with focus.
            const float side = style.size * kLeadingIconShare;
            painter.DrawImage(
                { inner.x, bounds.y + (bounds.height - side) * 0.5f, side, side },
                IconImage(*leadingIcon_),
                kWholeImage,
                Mix(theme.color.textDisabled, theme.color.accent, focus),
                theme.radius.none
            );
            inner = inner.Deflated(Insets { side + theme.space.sm, 0.0f, 0.0f, 0.0f });
        }
        const FontMetrics metrics = ctx.text.Metrics(style.font, style.size);
        const float baseline = CenteredBaseline(metrics, inner.y, inner.height);

        painter.PushClip(inner);
        float cursorX = 0.0f;
        float scrolled = 0.0f;
        if (edit_.text.empty()) {
            const FittedLine line =
                FitLine(ctx.text, style, placeholder_, inner.width, theme.type.ellipsis);
            painter.DrawText(
                { inner.x, baseline }, style, theme.color.textDisabled, line.text
            );
        } else {
            const std::string beforeCursor = ApplyCase(
                style.textCase, std::string_view(edit_.text).substr(0, edit_.cursor)
            );
            cursorX = MeasureWidth(ctx.text, style, beforeCursor);
            // Keep the cursor inside the field when the text is longer than it.
            scrolled = std::max(0.0f, cursorX + theme.metric.cursorWidth - inner.width);
            painter.DrawText(
                { inner.x - scrolled, baseline },
                style,
                theme.color.textPrimary,
                ApplyCase(style.textCase, edit_.text)
            );
        }
        // The cursor is part of having focus: it fades in and out with it.
        if (focus > 0.0f) {
            painter.FillRoundRect(
                { inner.x + cursorX - scrolled,
                  baseline - metrics.ascent,
                  theme.metric.cursorWidth,
                  metrics.ascent + metrics.descent },
                theme.radius.none,
                CornerColors::Solid(theme.color.accent.Faded(focus))
            );
        }
        painter.PopClip();
    }

}
