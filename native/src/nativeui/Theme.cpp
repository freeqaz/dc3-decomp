#include "nativeui/Theme.h"

namespace nativeui {

    // SAMPLED values were read off a 1920x1080 capture of the game's song select
    // ("song_select_song.png": Van McCoy / The Hustle selected, WARMUP header
    // highlighted), each as the mean of a 5x5 patch. Coordinates are in that
    // capture. Sizes are in the game's 1280x720 logical space, measured from the
    // same capture and scaled by 2/3.
    //
    // CHOSEN values are marked as such, with the reason. The rule behind most of
    // them: text must stay readable. Contrast figures are WCAG ratios against
    // white text, worked out from the colour as written.
    Theme Dc3Theme() {
        Theme theme;

        Palette &c = theme.color;
        // The menu background is pure black between the streaks: (900,150), (800,1000).
        c.background = Color::FromRgb8(0x000000);
        // Chosen. The game's song select is lit by violet and blue ribbons that
        // belong to its own list; a screen that replaces the list brings its own
        // light. The wash is the ribbons' violet at night-sky depth, thinning to
        // near black at the foot where the hints are; it is translucent so the
        // game's background still moves under it.
        c.backdropTop = Color::FromRgb8(0x23145C, 0.80f);
        c.backdropBottom = Color::FromRgb8(0x070618, 0.88f);
        // The bands are the ribbon colours: blue-violet #694FD5 at (1750,365), the
        // SORT BY streak's blue #2274BC at (60,60) and magenta #780885 at (500,60),
        // each brightened one step because a band is drawn soft and thin.
        c.bandViolet = Color::FromRgb8(0x7A5BEA);
        c.bandBlue = Color::FromRgb8(0x2F8FE0);
        c.bandMagenta = Color::FromRgb8(0xA01CB0);
        // Chosen: the wash's own indigo at about the depth the wash has behind a
        // list, so the well reads as dark glass and not as a black box, and a row
        // fading into this colour at the well's edge fades into what is there.
        // SONG_BROWSER_UI.md has the contrast measured in the running game.
        c.well = Color::FromRgb8(0x0D0A2C, 0.62f);
        c.wellBorder = Color::FromRgb8(0x8A76F0, 0.22f); // the panel border's violet, faint
        // Chosen. The game's PERFORM panel is glassy grey (#9594A0 to #3B3843); as a
        // large pane beside album art that reads as a flat grey card, so the pane
        // keeps the glass and takes the ribbons' indigo.
        c.panelTop = Color::FromRgb8(0x2C2466, 0.80f);
        c.panelBottom = Color::FromRgb8(0x0D0A22, 0.88f);
        // The outline runs from the ribbon violet down to the selection cyan, the
        // way the game's album frame is cyan on a violet field.
        c.panelBorder = Color::FromRgb8(0x8A76F0, 0.85f);
        c.panelBorderEnd = Color::FromRgb8(0x37A7E3, 0.45f);
        c.shadow = Color::FromRgb8(0x000000, 0.55f);
        // The empty cells of the SKILL LEVEL meter, the darkest "inset" in the art:
        // (900,890).
        c.fieldFill = Color::FromRgb8(0x011520, 0.85f);
        // The panel's bevelled edge, (116,500), lifted so a resting field is visible
        // on the wash.
        c.fieldBorder = Color::FromRgb8(0x6A64B4, 0.85f);
        // The cyan highlight bar under WARMUP: #37A7E3 at (1600,442); the same cyan
        // is the scroll thumb, #3FBCF4 at (1124,640). On black it is 7.8:1.
        c.accent = Color::FromRgb8(0x37A7E3);
        // The bar's bright core, #50B0E4 at (1300,442), used as its halo.
        c.accentGlow = Color::FromRgb8(0x50B0E4, 0.50f);
        // Chosen. White on the sampled cyan is 2.7:1, too little for a row of text,
        // so the bar is the same hue a step deeper (4.7:1) and runs out to the
        // ribbons' blue-violet (7.5:1); the sampled cyan stays as its rim.
        c.highlightStart = Color::FromRgb8(0x1C78BE);
        c.highlightEnd = Color::FromRgb8(0x4A3FC4);
        c.highlightRim = Color::FromRgb8(0x7CCBF5, 0.90f);
        // The magenta of the row that is selected but not active, THE HUSTLE:
        // #A75AEB at (1700,672). Drawn as an outline, never a fill.
        c.selectionMarker = Color::FromRgb8(0xA75AEB, 0.80f);
        c.streakBlue = Color::FromRgb8(0x2274BC);
        c.streakMagenta = Color::FromRgb8(0x780885);
        // Menu text is white; SOLO/CO-OP read as roughly 75% white. Chosen: the
        // disabled step is 50%, not the 45% of the game's faded rows, which is
        // under 4.5:1 on the wash.
        c.textPrimary = Color::FromRgb8(0xFFFFFF);
        c.textSecondary = Color::FromRgb8(0xFFFFFF, 0.76f);
        c.textDisabled = Color::FromRgb8(0xFFFFFF, 0.50f);
        // The empty player box, a flat violet: #68007E at (1740,160).
        c.imagePlaceholder = Color::FromRgb8(0x68007E);
        c.imageTint = Color::FromRgb8(0xFFFFFF);
        // Not sampled from the capture (no star is on it): the warm gold of the
        // game's score stars, to be checked against a results screen.
        c.star = Color::FromRgb8(0xFFC83C);
        c.starEmpty = Color::FromRgb8(0xFFFFFF, 0.34f);
        // The pad's face buttons as the game's help bar draws them: a coloured disc
        // with a dark letter. In the classic capture ("wire_classic_1080.png") B is
        // #BC0019 at (173,1023) and A, on a faded hint, is (67,97,36) at (1309,21).
        // Chosen from those: A's hue at full strength, and B a step brighter than
        // sampled, because the dark letter on #BC0019 is only 2.9:1 (here 4.3:1).
        c.buttonAccept = Color::FromRgb8(0x7FB844);
        c.buttonBack = Color::FromRgb8(0xE0393E);
        c.buttonText = Color::FromRgb8(0x0C0C16);
        // Chosen: a key cap is a lit glass chip, so a key name never looks like a
        // pad button.
        c.keycapFill = Color::FromRgb8(0xFFFFFF, 0.10f);
        c.keycapBorder = Color::FromRgb8(0xFFFFFF, 0.55f);
        // Chosen: an icon tile is a small piece of the highlight bar.
        c.tileTop = Color::FromRgb8(0x3B6FD8);
        c.tileBottom = Color::FromRgb8(0x5B2FB0);

        // Two faces, all upper case, loosely set. Cap heights in the capture:
        // PERFORM about 40 px, list rows about 28 px, SORT BY about 22 px.
        // Capitals in units (= pixels at 720 rows):  display 24.2, title 18.7,
        // body 14.3, caption 11.0, label 10.3, hint 10.6.
        Typography &t = theme.type;
        t.display = { kDc3MenuFont, 44.0f, 0.02f, TextCase::Upper };
        t.title = { kDc3MenuFont, 34.0f, 0.02f, TextCase::Upper };
        t.body = { kDc3MenuFont, 26.0f, 0.03f, TextCase::Upper };
        t.caption = { kDc3MenuFont, 20.0f, 0.05f, TextCase::Upper };
        // The thin face goes grey below about 11-pixel capitals, so the two
        // smallest styles use the heavy one.
        t.label = { kDc3HeavyFont, 16.5f, 0.08f, TextCase::Upper };
        t.hint = { kDc3HeavyFont, 17.0f, 0.06f, TextCase::Upper };
        // The game cuts long titles with three full stops ("MACARENA (BAYSIDE B...").
        t.ellipsis = "...";

        theme.space = { 4.0f, 8.0f, 16.0f, 24.0f, 40.0f };
        theme.radius = { 0.0f, 6.0f, 12.0f, 18.0f };

        // Chosen: every transition lands in 120 to 220 ms and eases out. A spring
        // is within 1% of its target after 6.6 / angularFrequency seconds.
        Motion &m = theme.motion;
        m.fast = 0.12f; // focus emphasis, small fades
        m.normal = 0.18f; // the shelf folding, the detail pane's crossfade
        m.slow = 0.32f; // the screen's entrance only
        m.easing = Easing::EaseOut;
        m.scrollSpring = { 30.0f, 0.05f }; // 220 ms; rests within 1/20 unit
        m.focusSpring = { 38.0f, 0.05f }; // 175 ms: the highlight leads the scroll
        m.fadeSpring = { 44.0f, 0.002f }; // 150 ms; a 0..1 amount: rest within 1/500
        m.ambientPeriod = 48.0f; // slow enough to be felt rather than watched

        Metrics &k = theme.metric;
        k.borderWidth = 1.5f;
        k.focusRingWidth = 3.0f;
        k.cursorWidth = 2.0f;
        k.glowSoftness = 22.0f;
        k.shadowSoftness = 28.0f;
        k.shadowOffset = 6.0f;
        k.controlHeight = 44.0f;
        k.tabIndicatorHeight = 3.0f;
        k.hintGlyphHeight = 26.0f;
        // The game's rows sit 77 px apart at 1080p (WARMUP 442, Y.M.C.A. 519): 51 units.
        k.listRowHeight = 51.0f;
        k.listHeaderHeight = 36.0f;
        k.listScrollMargin = 51.0f; // one row stays visible past the selection
        k.edgeFade = 44.0f; // most of a row
        // A card is square art over a caption of up to three label lines. Five fit
        // beside the detail pane with a sixth showing its edge, which is how the
        // shelf says there is more.
        k.shelfCardWidth = 132.0f;
        k.shelfCardHeight = 192.0f;
        k.shelfFocusScale = 1.05f;
        k.backdropBandSoftness = 30.0f;
        // The hairlines of the game's background: rows 125-126 and 955-956 of 1080.
        k.frameTop = 83.5f;
        k.frameBottom = 637.0f;
        // The song browser's own proportions (logical units). The pane is the
        // width of the game's PERFORM panel, 240..476 px at 1080p scaled up to
        // hold album art; the meter cell is the SKILL LEVEL meter's, 56x22 px
        // at 1080p, at a third of its size for a list row.
        k.searchFieldWidth = 300.0f;
        k.sidePaneWidth = 340.0f;
        k.listSecondaryShare = 0.30f;
        k.meterCellWidth = 7.0f;
        k.meterCellHeight = 12.0f;
        k.meterCellGap = 2.0f;
        k.starSize = 14.0f;
        k.starGap = 2.0f;

        return theme;
    }

}
