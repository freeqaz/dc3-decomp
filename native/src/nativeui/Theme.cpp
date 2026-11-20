#include "nativeui/Theme.h"

namespace nativeui {

    // Colours were read off a 1920x1080 capture of the game's song select
    // ("song_select_song.png": Van McCoy / The Hustle selected, WARMUP header
    // highlighted), each as the mean of a 5x5 patch. Coordinates are in that
    // capture. Sizes are in the game's 1280x720 logical space, measured from the
    // same capture and scaled by 2/3.
    Theme Dc3Theme() {
        Theme theme;

        Palette &c = theme.color;
        // The menu background is pure black between the streaks: (900,150), (800,1000).
        c.background = Color::FromRgb8(0x000000);
        // The PERFORM panel's glassy grey runs light to dark down its height:
        // #9594A0 at (300,340), #5D5D64 at (300,620), #3B3843 at (300,880).
        c.panelTop = Color::FromRgb8(0x9594A0, 0.92f);
        c.panelBottom = Color::FromRgb8(0x3B3843, 0.92f);
        // The panel's bevelled edge: (116,500).
        c.panelBorder = Color::FromRgb8(0x423F5C);
        c.shadow = Color::FromRgb8(0x000000, 0.55f);
        // The empty cells of the SKILL LEVEL meter, the darkest "inset" in the art:
        // (900,890).
        c.fieldFill = Color::FromRgb8(0x011520, 0.85f);
        // The cyan highlight bar under WARMUP: #37A7E3 at (1600,442); the same cyan
        // is the scroll thumb, #3FBCF4 at (1124,640).
        c.accent = Color::FromRgb8(0x37A7E3);
        // The bar thins out toward its left end: #2EA0D9 at (1200,442), fading on.
        c.accentDim = Color::FromRgb8(0x2EA0D9, 0.35f);
        // The bar's bright core, #50B0E4 at (1300,442), used as its halo.
        c.accentGlow = Color::FromRgb8(0x50B0E4, 0.45f);
        // The dimmer magenta bar on the row that is selected but not active,
        // THE HUSTLE: #A75AEB at (1700,672).
        c.highlightIdle = Color::FromRgb8(0xA75AEB, 0.55f);
        // The background streaks: blue-violet #694FD5 at (1750,365), and the SORT BY
        // streak's two ends, blue #2274BC at (60,60) and magenta #780885 at (500,60).
        c.streakBlue = Color::FromRgb8(0x2274BC);
        c.streakMagenta = Color::FromRgb8(0x780885);
        // Menu text is white; the unlit rows (STARSHIPS, (1316,826) = #6E4383 through
        // the fade) read as roughly 45% white, and SOLO/CO-OP as roughly 75%.
        c.textPrimary = Color::FromRgb8(0xFFFFFF);
        c.textSecondary = Color::FromRgb8(0xFFFFFF, 0.72f);
        c.textDisabled = Color::FromRgb8(0xFFFFFF, 0.40f);
        // The empty player box, a flat violet: #68007E at (1740,160).
        c.imagePlaceholder = Color::FromRgb8(0x68007E);
        c.imageTint = Color::FromRgb8(0xFFFFFF);

        // One face throughout, all upper case, loosely set. Cap heights in the
        // capture: PERFORM about 40 px, list rows about 28 px, SORT BY about 22 px.
        Typography &t = theme.type;
        t.display = { kDc3MenuFont, 44.0f, 0.02f, TextCase::Upper };
        t.title = { kDc3MenuFont, 34.0f, 0.02f, TextCase::Upper };
        t.body = { kDc3MenuFont, 26.0f, 0.03f, TextCase::Upper };
        t.caption = { kDc3MenuFont, 18.0f, 0.06f, TextCase::Upper };
        t.hint = { kDc3MenuFont, 20.0f, 0.04f, TextCase::Upper };
        // The game cuts long titles with three full stops ("MACARENA (BAYSIDE B...").
        t.ellipsis = "...";

        theme.space = { 4.0f, 8.0f, 16.0f, 24.0f, 40.0f };
        theme.radius = { 0.0f, 6.0f, 12.0f, 18.0f };

        Motion &m = theme.motion;
        m.fast = 0.12f;
        m.normal = 0.22f;
        m.slow = 0.40f;
        m.easing = Easing::EaseOut;
        m.scrollSpring = { 16.0f, 0.05f }; // settles in about 0.4 s, rests within 1/20
                                           // unit
        m.focusSpring = { 24.0f, 0.05f }; // a little quicker than the scroll it rides on
        m.fadeSpring = { 20.0f, 0.002f }; // a 0..1 amount: rest within 1/500

        Metrics &k = theme.metric;
        k.borderWidth = 1.5f;
        k.focusRingWidth = 3.0f;
        k.cursorWidth = 2.0f;
        k.glowSoftness = 24.0f;
        k.shadowSoftness = 28.0f;
        k.shadowOffset = 6.0f;
        k.controlHeight = 44.0f;
        k.tabIndicatorHeight = 3.0f;
        k.hintGlyphHeight = 26.0f;
        // The game's rows sit 77 px apart at 1080p (WARMUP 442, Y.M.C.A. 519): 51 units.
        k.listRowHeight = 51.0f;
        k.listHeaderHeight = 36.0f;
        k.listScrollMargin = 51.0f; // one row stays visible past the selection
        k.shelfCardWidth = 150.0f;
        k.shelfCardHeight = 150.0f;
        k.shelfFocusScale = 1.08f;

        return theme;
    }

}
