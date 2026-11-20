#include "nativeui/Label.h"

namespace nativeui {

    Label::Label(std::string text, TextStyleToken style, ColorToken color, TextAlign align)
        : text_(std::move(text)), style_(style), color_(color), align_(align) {}

    Size Label::OnMeasure(const Context &ctx, const Constraints &) {
        const TextStyle &style = ctx.theme.type.*style_;
        return { MeasureWidth(ctx.text, style, ApplyCase(style.textCase, text_)),
                 ctx.text.Metrics(style.font, style.size).lineHeight };
    }

    void Label::OnPaint(const Context &ctx, Painter &painter) const {
        const TextStyle &style = ctx.theme.type.*style_;
        const Rect &bounds = Bounds();
        const FittedLine line =
            FitLine(ctx.text, style, text_, bounds.width, ctx.theme.type.ellipsis);
        const FontMetrics metrics = ctx.text.Metrics(style.font, style.size);
        painter.DrawText(
            { bounds.x + AlignOffset(align_, bounds.width, line.width),
              CenteredBaseline(metrics, bounds.y, bounds.height) },
            style,
            ctx.theme.color.*color_,
            line.text
        );
    }

}
