// Theme: every design decision, in one value.
//
// Rules:
//  - Widgets contain no colour, size, duration or font literals. They read
//    this struct, which is handed down the tree in a Context (Widget.h).
//  - A widget is told *which* token to use (a pointer to a member, e.g.
//    &Palette::accent or &Typography::title), never the value, so swapping the
//    theme restyles a whole screen.
//  - A theme is a plain value: copy it, tweak a field, pass it on.
//
// Dc3Theme() is the one theme shipped; its values were sampled from the game's
// own song-select screen (see Theme.cpp for where each came from).
#pragma once

#include "nativeui/Animation.h"
#include "nativeui/Color.h"
#include "nativeui/Text.h"

#include <string>

namespace nativeui {

    struct Palette {
        Color background; // behind everything
        Color panelTop; // glassy panel fill, top of its gradient
        Color panelBottom; // and the bottom
        Color panelBorder; // thin panel outline
        Color shadow; // soft drop shadow under panels
        Color fieldFill; // inside of a text field
        Color accent; // the selection colour: highlight bar, focus ring, cursor
        Color accentDim; // the far end of the highlight bar's gradient
        Color accentGlow; // soft halo around accented things
        Color highlightIdle; // highlight of a list that does not have focus
        Color streakBlue; // background streaks, for section headers and accents
        Color streakMagenta;
        Color textPrimary;
        Color textSecondary;
        Color textDisabled; // placeholders and unavailable items
        Color imagePlaceholder; // shown where an image has not arrived
        Color imageTint; // multiplied into every image (white = unchanged)
        // A screen drawn over the game's animated background lays this over the
        // whole view, and again behind its columns of text, so text stays readable
        // while the background still shows through.
        Color scrim;
        Color star; // an earned star
    };

    struct Typography {
        TextStyle display; // screen titles
        TextStyle title; // card and detail headings
        TextStyle body; // list rows, fields, tabs
        TextStyle caption; // section headers, secondary lines
        TextStyle hint; // hint bar
        std::string ellipsis; // appended to a line that had to be cut
    };

    struct Spacing {
        float xs = 0.0f;
        float sm = 0.0f;
        float md = 0.0f;
        float lg = 0.0f;
        float xl = 0.0f;
    };

    struct Radii {
        float none = 0.0f;
        float small = 0.0f; // highlight bars, thumbnails
        float medium = 0.0f; // cards, fields
        float large = 0.0f; // panels
    };

    struct Motion {
        float fast = 0.0f; // seconds
        float normal = 0.0f;
        float slow = 0.0f;
        Easing easing = Easing::EaseOut;
        SpringParams scrollSpring; // list and shelf scrolling, in logical units
        SpringParams focusSpring; // highlights and indicators following a selection
        SpringParams fadeSpring; // 0..1 amounts such as focus emphasis
    };

    // Sizes of the built-in widgets' own parts.
    struct Metrics {
        float borderWidth = 0.0f;
        float focusRingWidth = 0.0f;
        float cursorWidth = 0.0f;
        float glowSoftness = 0.0f; // width of a glow's falloff
        float shadowSoftness = 0.0f;
        float shadowOffset = 0.0f; // how far below its panel a shadow sits
        float controlHeight = 0.0f; // text fields and tab strips
        float tabIndicatorHeight = 0.0f;
        float hintGlyphHeight = 0.0f; // the pill around a hint's button label
        float listRowHeight = 0.0f;
        float listHeaderHeight = 0.0f;
        float listScrollMargin = 0.0f; // kept visible past the selected row
        float shelfCardWidth = 0.0f;
        float shelfCardHeight = 0.0f;
        float shelfFocusScale = 1.0f; // how much the selected card grows
        // Browsing screens (the song browser):
        float searchFieldWidth = 0.0f;
        float sidePaneWidth = 0.0f; // the detail pane beside a list
        float listSecondaryShare = 0.0f; // fraction of a list row's width for its
                                         // second text column (the artist)
        float meterCellWidth = 0.0f; // one cell of a segmented meter in a list row
        float meterCellHeight = 0.0f;
        float meterCellGap = 0.0f;
        float starSize = 0.0f; // diameter of one star mark
        float starGap = 0.0f;
    };

    struct Theme {
        Palette color;
        Typography type;
        Spacing space;
        Radii radius;
        Motion motion;
        Metrics metric;
    };

    // Tokens: how a widget is told which theme value to use.
    using ColorToken = Color Palette::*;
    using TextStyleToken = TextStyle Typography::*;
    using RadiusToken = float Radii::*;

    // Font ids used by Dc3Theme. The integration layer maps them to game fonts.
    inline constexpr FontId kDc3MenuFont { 1 }; // the thin uppercase geometric face

    Theme Dc3Theme();

}
