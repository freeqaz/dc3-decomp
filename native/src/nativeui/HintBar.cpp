#include "nativeui/HintBar.h"

#include <algorithm>

namespace nativeui {

    namespace {

        // UTF-8 code points in `text` (continuation bytes are not counted).
        int CodePointCount(const std::string &text) {
            return static_cast<int>(std::count_if(text.begin(), text.end(), [](char c) {
                return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
            }));
        }

    }

    HintBar::HintBar(std::vector<Hint> hints) : hints_(std::move(hints)) {}

    HintBar::Extents HintBar::ExtentsOf(const Context &ctx, const Hint &hint) const {
        const TextStyle &style = ctx.theme.type.hint;
        Extents extents;
        extents.glyph =
            MeasureWidth(ctx.text, style, ApplyCase(style.textCase, hint.glyph));
        extents.text =
            MeasureWidth(ctx.text, style, ApplyCase(style.textCase, hint.text));
        // One character makes a round button; more make a keycap as wide as its label.
        extents.round = CodePointCount(hint.glyph) <= 1;
        extents.pill = extents.round
            ? ctx.theme.metric.hintGlyphHeight
            : std::max(
                  ctx.theme.metric.hintGlyphHeight, extents.glyph + 2.0f * ctx.theme.space.sm
              );
        return extents;
    }

    Size HintBar::OnMeasure(const Context &ctx, const Constraints &) {
        float width = 0.0f;
        for (std::size_t i = 0; i < hints_.size(); ++i) {
            const Extents extents = ExtentsOf(ctx, hints_[i]);
            width += extents.pill + ctx.theme.space.sm + extents.text;
            if (i + 1 < hints_.size()) {
                width += ctx.theme.space.lg;
            }
        }
        return { width, ctx.theme.metric.hintGlyphHeight };
    }

    void HintBar::OnPaint(const Context &ctx, Painter &painter) const {
        const Theme &theme = ctx.theme;
        const TextStyle &style = theme.type.hint;
        const Rect &bounds = Bounds();
        const float pillHeight = theme.metric.hintGlyphHeight;
        const float pillTop = bounds.y + (bounds.height - pillHeight) * 0.5f;
        const float baseline = CenteredBaseline(
            ctx.text.Metrics(style.font, style.size), bounds.y, bounds.height
        );

        float x = bounds.x;
        for (const Hint &hint : hints_) {
            const Extents extents = ExtentsOf(ctx, hint);
            const Rect control { x, pillTop, extents.pill, pillHeight };
            Color label = theme.color.textPrimary;
            if (extents.round && hint.tone) {
                painter.FillRoundRect(
                    control, pillHeight * 0.5f, CornerColors::Solid(theme.color.*hint.tone)
                );
                label = theme.color.buttonText;
            } else {
                const float radius = extents.round ? pillHeight * 0.5f : theme.radius.small;
                painter.FillRoundRect(
                    control, radius, CornerColors::Solid(theme.color.keycapFill)
                );
                painter.StrokeRoundRect(
                    control,
                    radius,
                    CornerColors::Solid(theme.color.keycapBorder),
                    theme.metric.borderWidth
                );
            }
            painter.DrawText(
                { x + (extents.pill - extents.glyph) * 0.5f, baseline },
                style,
                label,
                ApplyCase(style.textCase, hint.glyph)
            );
            x += extents.pill + theme.space.sm;
            painter.DrawText(
                { x, baseline },
                style,
                theme.color.textSecondary,
                ApplyCase(style.textCase, hint.text)
            );
            x += extents.text + theme.space.lg;
        }
    }

}
