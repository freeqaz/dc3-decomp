// IconTile: a coloured tile with an icon on it, for the place where an item
// that has no picture would otherwise show an empty square (a command in a
// list of songs, say). It fills whatever rectangle it is given.
#pragma once

#include "nativeui/Icon.h"
#include "nativeui/Theme.h"
#include "nativeui/Widget.h"

namespace nativeui {

    class IconTile final : public Widget {
    public:
        IconTile(Icon icon, RadiusToken radius);

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnPaint(const Context &ctx, Painter &painter) const override;

    private:
        Icon icon_;
        RadiusToken radius_;
    };

}
