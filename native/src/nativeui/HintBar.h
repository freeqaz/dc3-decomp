// HintBar: the row of control hints along the bottom of a screen
// ("(A) SELECT   (B) BACK").
//
// Each hint is a glyph label and a text. The glyph label is drawn inside an
// outlined pill; the integration layer chooses what it says ("A", "ENTER", or
// a character the game's glyph font turns into a button). Hints are laid out
// left to right and never take focus.
#pragma once

#include "nativeui/Widget.h"

#include <string>
#include <vector>

namespace nativeui {

    struct Hint {
        std::string glyph; // what goes in the pill
        std::string text; // what the control does
    };

    class HintBar final : public Widget {
    public:
        explicit HintBar(std::vector<Hint> hints);

        void SetHints(std::vector<Hint> hints) { hints_ = std::move(hints); }
        const std::vector<Hint> &Hints() const { return hints_; }

    protected:
        Size OnMeasure(const Context &ctx, const Constraints &constraints) override;
        void OnPaint(const Context &ctx, Painter &painter) const override;

    private:
        struct Extents {
            float pill = 0.0f; // width of the pill
            float glyph = 0.0f; // width of the label inside it
            float text = 0.0f;
        };
        Extents ExtentsOf(const Context &ctx, const Hint &hint) const;

        std::vector<Hint> hints_;
    };

}
