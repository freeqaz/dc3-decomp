#include "nativeui/Text.h"

#include "nativeui/Utf8.h"

#include <vector>

namespace nativeui {

    std::string ApplyCase(TextCase textCase, std::string_view text) {
        if (textCase == TextCase::Upper) {
            return utf8::ToUpper(text);
        }
        return std::string(text);
    }

    float LetterSpacing(const TextStyle &style) { return style.tracking * style.size; }

    float MeasureWidth(
        const TextMeasurer &measurer, const TextStyle &style, std::string_view displayText
    ) {
        const std::size_t glyphs = utf8::Count(displayText);
        if (glyphs == 0) {
            return 0.0f;
        }
        return measurer.AdvanceWidth(style.font, style.size, displayText)
            + LetterSpacing(style) * static_cast<float>(glyphs - 1);
    }

    FittedLine FitLine(
        const TextMeasurer &measurer,
        const TextStyle &style,
        std::string_view text,
        float maxWidth,
        std::string_view ellipsis
    ) {
        FittedLine line;
        line.text = ApplyCase(style.textCase, text);
        line.width = MeasureWidth(measurer, style, line.text);
        if (line.width <= maxWidth) {
            return line;
        }
        line.truncated = true;

        // Boundaries of every code point, so a prefix is always whole characters.
        const std::string full = line.text;
        std::vector<std::size_t> boundaries;
        for (std::size_t offset = 0; offset < full.size();
             offset = utf8::Next(full, offset)) {
            boundaries.push_back(offset);
        }

        const auto candidate = [&](std::size_t keptCodePoints) {
            std::size_t end = boundaries[keptCodePoints];
            while (end > 0 && full[end - 1] == ' ') {
                --end;
            }
            return full.substr(0, end) + std::string(ellipsis);
        };

        // Longest prefix (dropping at least one character) that fits with the
        // ellipsis. Width grows with length, so search, then verify.
        std::size_t low = 0;
        std::size_t high = boundaries.size() - 1;
        while (low < high) {
            const std::size_t middle = (low + high + 1) / 2;
            if (MeasureWidth(measurer, style, candidate(middle)) <= maxWidth) {
                low = middle;
            } else {
                high = middle - 1;
            }
        }
        line.text = candidate(low);
        line.width = MeasureWidth(measurer, style, line.text);
        if (line.width > maxWidth) {
            line.text.clear();
            line.width = 0.0f;
        }
        return line;
    }

    float AlignOffset(TextAlign align, float available, float lineWidth) {
        switch (align) {
        case TextAlign::Center:
            return (available - lineWidth) * 0.5f;
        case TextAlign::Right:
            return available - lineWidth;
        case TextAlign::Left:
            break;
        }
        return 0.0f;
    }

    float CenteredBaseline(const FontMetrics &metrics, float boxTop, float boxHeight) {
        return boxTop + (boxHeight - (metrics.ascent + metrics.descent)) * 0.5f
            + metrics.ascent;
    }

}
