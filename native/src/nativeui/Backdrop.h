// Backdrop: a screen's own background, in the theme's colours.
//
// The game's menus are lit by violet and blue ribbons that belong to the
// widgets of each screen. A natively drawn screen that replaces those widgets
// puts a Backdrop first in its root Stack and gets, back to front:
//   a wash      a vertical gradient over whatever the game draws behind
//               (Palette::backdropTop / backdropBottom, translucent)
//   two glows   large soft pools of the band colours, top right and bottom left
//   bands       thin horizontal ribbons of light, bright at the screen's edge
//               and fading toward its middle, each a soft halo with a fine
//               core; two of them lie on the game's frame hairlines
//
// The bands drift sideways and breathe on a cycle of Motion::ambientPeriod
// seconds, driven by the time handed to Advance and nothing else.
//
// Rules:
//  - It takes the whole area it is given and never takes focus.
//  - It is ambient: AtRest() is always true, so a screen with a backdrop still
//    comes to rest ("no transition is running"). A host that redraws only
//    while a screen is not at rest gets a still backdrop, which is fine.
//  - It is drawn under text, so it stays dark: no band is brighter than
//    kBackdropPeakOpacity of its colour, and a column of text adds
//    Palette::well on top. NativeUiWidgets.BackdropStaysDarkUnderText holds
//    the worst case to a contrast ratio.
#pragma once

#include "nativeui/Widget.h"

namespace nativeui {

    // The most of a band colour that the backdrop lays anywhere.
    inline constexpr float kBackdropPeakOpacity = 0.5f;

    class Backdrop final : public Widget {
    public:
        // Where the drift is in its cycle, 0..1.
        float Phase() const { return phase_; }

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnAdvance(const Context &ctx, float deltaSeconds) override;
        void OnPaint(const Context &ctx, Painter &painter) const override;

    private:
        float phase_ = 0.0f;
    };

}
