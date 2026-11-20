// MatchText: one line of text with some runs of it picked out in a second
// colour (the part of a title a search matched).
//
// The runs to pick out are given as BYTE spans of the text as written, which
// is what songbrowser::SongSearch reports. The line is drawn in the style's
// display case, and upper-casing can change both the bytes and the number of
// glyphs ("ß" becomes "SS"), so spans are never applied to the cased string:
// the text is cut at the span edges first and each piece is cased on its own.
//
// A line that does not fit is cut with the theme's ellipsis exactly as
// nativeui::FitLine cuts it; a highlighted run that is cut stays highlighted
// up to the cut, and the ellipsis itself is never highlighted.
#pragma once

#include "nativeui/DisplayList.h"
#include "nativeui/Text.h"

#include <string>
#include <string_view>
#include <vector>

namespace songbrowser_ui {

    // A run of a string as written, in bytes of its UTF-8.
    struct ByteSpan {
        int begin = 0;
        int length = 0;
    };

    struct LineRun {
        std::string text; // display-cased
        float x = 0.0f; // where the run starts, from the line's left end
        bool highlighted = false;
    };

    struct MatchLine {
        std::vector<LineRun> runs; // left to right; plain and highlighted alternate
        float width = 0.0f;
        bool truncated = false;
    };

    // Spans may arrive in any order, overlap, or reach past the text; they are
    // sorted, merged and clamped.
    MatchLine FitMatchLine(
        const nativeui::TextMeasurer &measurer,
        const nativeui::TextStyle &style,
        std::string_view text,
        const std::vector<ByteSpan> &spans,
        float maxWidth,
        std::string_view ellipsis
    );

    // `origin` is the left end of the baseline.
    void PaintMatchLine(
        nativeui::Painter &painter,
        nativeui::Vec2 origin,
        const nativeui::TextStyle &style,
        const MatchLine &line,
        const nativeui::Color &plain,
        const nativeui::Color &highlight
    );

}
