// Panel: a rounded card behind (optionally) one child.
//
// It paints, back to front: a halo (a soft drop shadow or an accent glow), a
// vertical gradient fill, an outline, then the child, which gets the panel's
// whole rectangle (wrap the child in Padding for an inset).
//
// With a child, the panel is the child's size. Without one, it fills the space
// it is given, which makes it useful as a background.
//
// Every part of its look is a theme token; a null border token means no outline.
#pragma once

#include "nativeui/Widget.h"

#include <memory>

namespace nativeui {

    struct PanelStyle {
        enum class Halo {
            None,
            Shadow,
            Glow
        };

        ColorToken fillTop = &Palette::panelTop;
        ColorToken fillBottom = &Palette::panelBottom;
        ColorToken border = nullptr;
        RadiusToken radius = &Radii::large;
        Halo halo = Halo::None;
    };

    class Panel final : public Widget {
    public:
        explicit Panel(
            const PanelStyle &style = {}, std::unique_ptr<Widget> child = nullptr
        );

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnArrange(const Context &ctx, const Rect &bounds) override;
        void OnPaint(const Context &ctx, Painter &painter) const override;

    private:
        PanelStyle style_;
    };

}
