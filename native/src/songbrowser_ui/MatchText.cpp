#include "songbrowser_ui/MatchText.h"

#include <algorithm>

namespace songbrowser_ui {

    using namespace nativeui;

    namespace {

        // The spans sorted, clamped to the text and merged where they touch.
        std::vector<ByteSpan> Tidy(std::vector<ByteSpan> spans, int size) {
            for (ByteSpan &span : spans) {
                const int begin = std::min(size, std::max(0, span.begin));
                const int end = std::min(size, std::max(begin, span.begin + span.length));
                span = { begin, end - begin };
            }
            std::sort(spans.begin(), spans.end(), [](const ByteSpan &a, const ByteSpan &b) {
                return a.begin < b.begin;
            });
            std::vector<ByteSpan> merged;
            for (const ByteSpan &span : spans) {
                if (span.length <= 0) {
                    continue;
                }
                if (!merged.empty()
                    && span.begin <= merged.back().begin + merged.back().length) {
                    const int end = std::max(
                        merged.back().begin + merged.back().length,
                        span.begin + span.length
                    );
                    merged.back().length = end - merged.back().begin;
                } else {
                    merged.push_back(span);
                }
            }
            return merged;
        }

        // A piece of the cased line: where it starts in the cased string.
        struct Piece {
            std::size_t begin = 0;
            bool highlighted = false;
        };

    }

    MatchLine FitMatchLine(
        const TextMeasurer &measurer,
        const TextStyle &style,
        std::string_view text,
        const std::vector<ByteSpan> &spans,
        float maxWidth,
        std::string_view ellipsis
    ) {
        // Cut at the span edges, case each piece, and remember where each piece
        // starts in the cased line.
        std::string cased;
        std::vector<Piece> pieces;
        const auto add = [&](std::size_t begin, std::size_t end, bool highlighted) {
            if (end > begin) {
                pieces.push_back({ cased.size(), highlighted });
                cased += ApplyCase(style.textCase, text.substr(begin, end - begin));
            }
        };
        std::size_t at = 0;
        for (const ByteSpan &span : Tidy(spans, static_cast<int>(text.size()))) {
            const std::size_t begin = static_cast<std::size_t>(span.begin);
            add(at, begin, false);
            add(begin, begin + static_cast<std::size_t>(span.length), true);
            at = begin + static_cast<std::size_t>(span.length);
        }
        add(at, text.size(), false);

        const FittedLine fitted = FitLine(measurer, style, text, maxWidth, ellipsis);
        MatchLine line;
        line.width = fitted.width;
        line.truncated = fitted.truncated;
        if (fitted.text.empty()) {
            return line;
        }
        // How much of the cased line survived the cut; the rest is the ellipsis.
        const std::size_t kept =
            fitted.truncated ? fitted.text.size() - ellipsis.size() : cased.size();

        const auto startOf = [&](std::size_t offset) {
            if (offset == 0) {
                return 0.0f;
            }
            return MeasureWidth(
                       measurer, style, std::string_view(fitted.text).substr(0, offset)
                   )
                + LetterSpacing(style);
        };
        for (std::size_t i = 0; i < pieces.size() && pieces[i].begin < kept; ++i) {
            const std::size_t end =
                std::min(kept, i + 1 < pieces.size() ? pieces[i + 1].begin : cased.size());
            line.runs.push_back({ fitted.text.substr(pieces[i].begin, end - pieces[i].begin),
                                  startOf(pieces[i].begin),
                                  pieces[i].highlighted });
        }
        if (fitted.truncated) {
            if (!line.runs.empty() && !line.runs.back().highlighted) {
                line.runs.back().text += std::string(ellipsis);
            } else {
                line.runs.push_back({ std::string(ellipsis), startOf(kept), false });
            }
        }
        return line;
    }

    void PaintMatchLine(
        Painter &painter,
        Vec2 origin,
        const TextStyle &style,
        const MatchLine &line,
        const Color &plain,
        const Color &highlight
    ) {
        for (const LineRun &run : line.runs) {
            painter.DrawText(
                { origin.x + run.x, origin.y },
                style,
                run.highlighted ? highlight : plain,
                run.text
            );
        }
    }

}
