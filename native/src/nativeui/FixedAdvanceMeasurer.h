// FixedAdvanceMeasurer: a deterministic TextMeasurer with no fonts behind it.
//
// Every code point advances by the same fraction of the font size, whatever
// the font id. Tests and headless tools use it so layout
// can be exercised with no game data. It is not for shipping screens.
#pragma once

#include "nativeui/Text.h"
#include "nativeui/Utf8.h"

namespace nativeui {

    class FixedAdvanceMeasurer final : public TextMeasurer {
    public:
        // Fractions of the font size.
        static constexpr float kAdvance = 0.5f;
        static constexpr float kAscent = 0.8f;
        static constexpr float kDescent = 0.2f;
        static constexpr float kLineHeight = 1.2f;

        float AdvanceWidth(FontId, float size, std::string_view utf8) const override {
            return static_cast<float>(utf8::Count(utf8)) * size * kAdvance;
        }

        FontMetrics Metrics(FontId, float size) const override {
            return { size * kAscent, size * kDescent, size * kLineHeight };
        }
    };

}
