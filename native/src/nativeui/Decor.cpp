#include "nativeui/Decor.h"

#include <algorithm>

namespace nativeui {

    namespace {

        // How much of the well's colour a fading edge reaches at the very edge.
        // Not 1: the last sliver of a row stays just visible, so the edge reads as
        // "more", not as a wall.
        constexpr float kEdgeFadeDepth = 0.94f;
        // The rim of a focused bar is brightest along its top and thins out
        // toward its foot, like light on glass.
        constexpr float kRimFootOpacity = 0.35f;

    }

    void PaintFocusGlow(
        Painter &painter, const Theme &theme, const Rect &rect, float radius, float amount
    ) {
        if (amount <= 0.0f) {
            return;
        }
        // A fill's softness is a ramp centred on its edge, so a ramp twice the
        // reach wide leaves half of it outside.
        painter.FillRoundRect(
            rect,
            radius,
            CornerColors::Solid(theme.color.accentGlow.Faded(amount)),
            2.0f * theme.metric.glowSoftness
        );
    }

    void PaintSelectionBar(
        Painter &painter, const Theme &theme, const Rect &bar, float radius, float focus
    ) {
        const Palette &color = theme.color;
        if (focus > 0.0f) {
            PaintFocusGlow(painter, theme, bar, radius, focus);
            painter.FillRoundRect(
                bar,
                radius,
                CornerColors::Horizontal(
                    color.highlightStart.Faded(focus), color.highlightEnd.Faded(focus)
                )
            );
            painter.StrokeRoundRect(
                bar,
                radius,
                CornerColors::Vertical(
                    color.highlightRim.Faded(focus),
                    color.highlightRim.Faded(focus * kRimFootOpacity)
                ),
                theme.metric.borderWidth
            );
        }
        if (focus < 1.0f) {
            painter.StrokeRoundRect(
                bar,
                radius,
                CornerColors::Solid(color.selectionMarker.Faded(1.0f - focus)),
                theme.metric.borderWidth
            );
        }
    }

    void PaintSelectionRing(
        Painter &painter,
        const Theme &theme,
        const Rect &card,
        float radius,
        float focus,
        float amount
    ) {
        if (amount <= 0.0f) {
            return;
        }
        const Palette &color = theme.color;
        const float lit = focus * amount;
        if (lit > 0.0f) {
            painter.StrokeRoundRect(
                card, radius, CornerColors::Solid(color.accent.Faded(lit)), theme.metric.focusRingWidth
            );
        }
        const float quiet = (1.0f - focus) * amount;
        if (quiet > 0.0f) {
            painter.StrokeRoundRect(
                card,
                radius,
                CornerColors::Solid(color.selectionMarker.Faded(quiet)),
                theme.metric.borderWidth
            );
        }
    }

    void PaintEdgeFade(
        Painter &painter, const Theme &theme, const Rect &area, Edge edge, float amount
    ) {
        const float opacity = std::min(1.0f, std::max(0.0f, amount)) * kEdgeFadeDepth;
        if (opacity <= 0.0f) {
            return;
        }
        const Color solid = theme.color.well.WithAlpha(opacity);
        const Color clear = theme.color.well.WithAlpha(0.0f);
        const float reach = theme.metric.edgeFade;
        switch (edge) {
        case Edge::Top:
            painter.FillRoundRect(
                { area.x, area.y, area.width, std::min(reach, area.height) },
                theme.radius.none,
                CornerColors::Vertical(solid, clear)
            );
            break;
        case Edge::Bottom: {
            const float height = std::min(reach, area.height);
            painter.FillRoundRect(
                { area.x, area.Bottom() - height, area.width, height },
                theme.radius.none,
                CornerColors::Vertical(clear, solid)
            );
            break;
        }
        case Edge::Left:
            painter.FillRoundRect(
                { area.x, area.y, std::min(reach, area.width), area.height },
                theme.radius.none,
                CornerColors::Horizontal(solid, clear)
            );
            break;
        case Edge::Right: {
            const float width = std::min(reach, area.width);
            painter.FillRoundRect(
                { area.Right() - width, area.y, width, area.height },
                theme.radius.none,
                CornerColors::Horizontal(clear, solid)
            );
            break;
        }
        }
    }

}
