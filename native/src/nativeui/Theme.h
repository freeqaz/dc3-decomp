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
// own song-select screen (see Theme.cpp for where each came from, and which
// were chosen rather than sampled).
//
// TEXT SIZES. A size is the height of the font's cell; capitals of the menu
// face are 0.55 of it and of the heavy face 0.625. No style may have capitals
// under 10 output pixels at the smallest output the port supports (720 rows,
// where one unit is one pixel): Theme.cpp states each style's cap height and
// NativeUiTheme.NoStyleHasCapitalsUnderTenPixels holds them to it.
#pragma once

#include "nativeui/Animation.h"
#include "nativeui/Color.h"
#include "nativeui/Text.h"

#include <string>

namespace nativeui {

    struct Palette {
        Color background; // behind everything
        // A screen's own backdrop (Backdrop.h): a wash laid over whatever the
        // game draws behind the screen, and the bands of light on it.
        Color backdropTop;
        Color backdropBottom;
        Color bandViolet;
        Color bandBlue;
        Color bandMagenta;
        // The dark a column of text sits in. Lists and shelves fade into it at
        // their edges, so it must be what is actually behind them.
        Color well;
        Color wellBorder; // the faint line around it
        Color panelTop; // glassy panel fill, top of its gradient
        Color panelBottom; // and the bottom
        Color panelBorder; // panel outline, at its top
        Color panelBorderEnd; // and at its bottom
        Color shadow; // soft drop shadow under panels
        Color fieldFill; // inside of a text field
        Color fieldBorder; // its outline while it has no focus
        Color accent; // the accent on dark: focus rings, cursors, matched text
        Color accentGlow; // soft halo around accented things
        // The filled bar under the selection of the region that HAS focus. It is
        // darker than the accent so that white text on it stays readable.
        Color highlightStart; // its left end
        Color highlightEnd; // its right end
        Color highlightRim; // the bright line around it
        // The quiet outline of a selection whose region does not have focus.
        Color selectionMarker;
        Color streakBlue; // the game's streaks, for rules and accents
        Color streakMagenta;
        Color textPrimary;
        Color textSecondary;
        Color textDisabled; // placeholders and unavailable items
        Color imagePlaceholder; // shown where an image has not arrived
        Color imageTint; // multiplied into every image (white = unchanged)
        Color star; // an earned star
        Color starEmpty; // the outline of one not earned
        // Hint glyphs (HintBar.h): the pad's face buttons, and a key's cap.
        Color buttonAccept;
        Color buttonBack;
        Color buttonText; // the letter on a face button
        Color keycapFill;
        Color keycapBorder;
        Color tileTop; // an icon tile (where a row that is not a song has no art)
        Color tileBottom;
    };

    struct Typography {
        TextStyle display; // screen titles
        TextStyle title; // card and detail headings
        TextStyle body; // list rows, fields, tabs
        TextStyle caption; // secondary lines
        TextStyle label; // small headings: section names, card captions, tags
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
        float ambientPeriod = 0.0f; // seconds for one cycle of a backdrop's drift
    };

    // Sizes of the built-in widgets' own parts.
    struct Metrics {
        float borderWidth = 0.0f;
        float focusRingWidth = 0.0f;
        float cursorWidth = 0.0f;
        float glowSoftness = 0.0f; // how far a glow reaches past what glows
        float shadowSoftness = 0.0f;
        float shadowOffset = 0.0f; // how far below its panel a shadow sits
        float controlHeight = 0.0f; // text fields and tab strips
        float tabIndicatorHeight = 0.0f;
        float hintGlyphHeight = 0.0f; // a hint's button disc or key cap
        float listRowHeight = 0.0f;
        float listHeaderHeight = 0.0f;
        float listScrollMargin = 0.0f; // kept visible past the selected row
        float edgeFade = 0.0f; // how far from its edge a list or shelf fades out
        float shelfCardWidth = 0.0f;
        float shelfCardHeight = 0.0f;
        float shelfFocusScale = 1.0f; // how much the selected card grows
        float backdropBandSoftness = 0.0f; // falloff of a backdrop's band of light
        // The game's menu frame: its background draws two hairlines across the
        // view at these heights. A screen keeps its body between them and its
        // top bar and hints outside them.
        float frameTop = 0.0f;
        float frameBottom = 0.0f;
        // Browsing screens (the song browser):
        float searchFieldWidth = 0.0f;
        float sidePaneWidth = 0.0f; // the detail pane beside a list
        float listSecondaryShare = 0.0f; // fraction of a list row's width for its
                                         // second text column (the artist)
        float meterCellWidth = 0.0f; // one cell of a segmented meter in a list row
        float meterCellHeight = 0.0f;
        float meterCellGap = 0.0f;
        float starSize = 0.0f; // the side of the square one star is drawn in
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
    inline constexpr FontId kDc3HeavyFont { 2 }; // the same face, heavier: small labels

    // Capital height as a fraction of the text size, for the two faces above
    // (measured from the game's atlases; docs/native/reference/NATIVE_UI.md).
    inline constexpr float kDc3MenuCapRatio = 0.55f;
    inline constexpr float kDc3HeavyCapRatio = 0.625f;

    Theme Dc3Theme();

}
