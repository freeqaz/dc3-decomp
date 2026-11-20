#include "nativeui/Panel.h"

namespace nativeui {

    Panel::Panel(const PanelStyle &style, std::unique_ptr<Widget> child) : style_(style) {
        if (child) {
            Adopt(std::move(child));
        }
    }

    Size Panel::OnMeasure(const Context &ctx, const Constraints &constraints) {
        if (Children().empty()) {
            return constraints.Biggest();
        }
        return Children().front()->Measure(ctx, constraints);
    }

    void Panel::OnArrange(const Context &ctx, const Rect &bounds) {
        if (!Children().empty()) {
            Children().front()->Arrange(ctx, bounds);
        }
    }

    void Panel::OnPaint(const Context &ctx, Painter &painter) const {
        const Theme &theme = ctx.theme;
        const Rect &bounds = Bounds();
        const float radius = theme.radius.*style_.radius;

        if (style_.halo == PanelStyle::Halo::Shadow) {
            const float soft = theme.metric.shadowSoftness;
            painter.FillRoundRect(
                bounds.Translated({ 0.0f, theme.metric.shadowOffset })
                    .Inflated(soft * 0.5f),
                radius + soft * 0.5f,
                CornerColors::Solid(theme.color.shadow),
                soft
            );
        } else if (style_.halo == PanelStyle::Halo::Glow) {
            const float soft = theme.metric.glowSoftness;
            painter.FillRoundRect(
                bounds.Inflated(soft * 0.5f),
                radius + soft * 0.5f,
                CornerColors::Solid(theme.color.accentGlow),
                soft
            );
        }

        painter.FillRoundRect(
            bounds,
            radius,
            CornerColors::Vertical(
                theme.color.*style_.fillTop, theme.color.*style_.fillBottom
            )
        );
        if (style_.border) {
            painter.StrokeRoundRect(
                bounds,
                radius,
                CornerColors::Solid(theme.color.*style_.border),
                theme.metric.borderWidth
            );
        }
        PaintChildren(ctx, painter);
    }

}
