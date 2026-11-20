#include "nativeui/IconTile.h"

#include <algorithm>

namespace nativeui {

    namespace {

        // The icon's side as a share of the tile's shorter side.
        constexpr float kIconShare = 0.56f;

    }

    IconTile::IconTile(Icon icon, RadiusToken radius) : icon_(icon), radius_(radius) {}

    Size IconTile::OnMeasure(const Context &, const Constraints &constraints) {
        return constraints.Biggest();
    }

    void IconTile::OnPaint(const Context &ctx, Painter &painter) const {
        const Theme &theme = ctx.theme;
        const Rect &bounds = Bounds();
        const float radius = theme.radius.*radius_;
        painter.FillRoundRect(
            bounds, radius, CornerColors::Vertical(theme.color.tileTop, theme.color.tileBottom)
        );
        const float side = std::min(bounds.width, bounds.height) * kIconShare;
        const Vec2 centre = bounds.Center();
        painter.DrawImage(
            { centre.x - side * 0.5f, centre.y - side * 0.5f, side, side },
            IconImage(icon_),
            kWholeImage,
            theme.color.textPrimary,
            theme.radius.none
        );
    }

}
