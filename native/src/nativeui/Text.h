// Text: measuring and fitting single lines of text.
//
// Rules:
//  - The framework never rasterises. A TextMeasurer (implemented by whoever
//    owns the fonts) answers sizes; a renderer draws the resulting TextRun.
//  - Fonts are opaque ids. The theme chooses them; the integration layer maps
//    each id to a real font.
//  - One line only. A line that does not fit is cut at a code point boundary
//    and ends with the theme's ellipsis.
//  - Tracking is the framework's job: the measurer returns plain advances and
//    the extra space between glyphs is added here and carried on the TextRun.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nativeui {

    struct FontId {
        std::uint32_t value = 0;
    };
    inline bool operator==(FontId a, FontId b) { return a.value == b.value; }
    inline bool operator!=(FontId a, FontId b) { return a.value != b.value; }

    enum class TextCase {
        AsWritten,
        Upper
    };

    enum class TextAlign {
        Left,
        Center,
        Right
    };

    // A type style: which font, how big, how loosely set, and in which case.
    struct TextStyle {
        FontId font;
        float size = 0.0f; // logical units; what the measurer and renderer are given
        float tracking = 0.0f; // extra space between glyphs, as a fraction of `size`
        TextCase textCase = TextCase::AsWritten;
    };

    // Vertical metrics of a font at a size, all positive, in logical units.
    struct FontMetrics {
        float ascent = 0.0f; // baseline up to the top of tall glyphs
        float descent = 0.0f; // baseline down to the bottom of hanging glyphs
        float lineHeight = 0.0f; // distance between the baselines of stacked lines
    };

    // The text seam. Implementations must be pure: the same question always gets
    // the same answer, and asking has no side effects.
    class TextMeasurer {
    public:
        virtual ~TextMeasurer() = default;

        // Sum of the glyph advances of `utf8` set in `font` at `size`, with no
        // tracking. `utf8` is already in its display case.
        virtual float
        AdvanceWidth(FontId font, float size, std::string_view utf8) const = 0;

        virtual FontMetrics Metrics(FontId font, float size) const = 0;
    };

    // `text` in the style's display case.
    std::string ApplyCase(TextCase textCase, std::string_view text);

    // The style's tracking in logical units (the gap added between two glyphs).
    float LetterSpacing(const TextStyle &style);

    // Width of `displayText` (already cased) in `style`: advances plus tracking
    // between glyphs, none after the last.
    float MeasureWidth(
        const TextMeasurer &measurer, const TextStyle &style, std::string_view displayText
    );

    struct FittedLine {
        std::string text; // display-cased, possibly cut and ending in the ellipsis
        float width = 0.0f;
        bool truncated = false;
    };

    // Cases `text` and, if it is wider than `maxWidth`, cuts it to the longest
    // prefix that fits with `ellipsis` appended (trailing spaces before the
    // ellipsis are dropped). If not even the ellipsis fits, the line is empty.
    FittedLine FitLine(
        const TextMeasurer &measurer,
        const TextStyle &style,
        std::string_view text,
        float maxWidth,
        std::string_view ellipsis
    );

    // Cases `text` and breaks it at spaces into at most `maxLines` lines, none
    // wider than `maxWidth`. Lines are filled greedily; the last one takes
    // whatever is left and is cut with the ellipsis like FitLine's, as is a
    // single word too long for a line of its own. Empty text gives no lines.
    std::vector<FittedLine> WrapLines(
        const TextMeasurer &measurer,
        const TextStyle &style,
        std::string_view text,
        float maxWidth,
        int maxLines,
        std::string_view ellipsis
    );

    // How far from the left of a box `available` wide a line `lineWidth` wide starts.
    float AlignOffset(TextAlign align, float available, float lineWidth);

    // The baseline's y for a line vertically centred in a box.
    float CenteredBaseline(const FontMetrics &metrics, float boxTop, float boxHeight);

}
